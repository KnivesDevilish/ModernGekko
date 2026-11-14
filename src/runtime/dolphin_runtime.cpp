#include "moderngekko/runtime.hpp"

#include "AudioCommon/AudioCommon.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"
#include "Core/Boot/Boot.h"
#include "Core/Boot/BootManager.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/Core.h"
#include "Core/HW/GBACore.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/Memmap.h"
#include "Core/Host.h"
#include "Core/NetPlay/NetPlayClient.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/StaticRecomp/StaticRecompModuleSource.h"
#include "Core/State.h"
#include "Core/System.h"
#include "DiscIO/RiivolutionParser.h"
#include "DiscIO/RiivolutionPatcher.h"
#include "DolphinNoGUI/Platform.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"
#include "InputCommon/InputConfig.h"
#include "UICommon/UICommon.h"
#include "VideoCommon/FrameDumper.h"
#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/VideoEvents.h"
#include "VideoCommon/VideoConfig.h"
#include "automation_protocol.hpp"
#include "dolphin_runtime_internal.hpp"
#include "game_border.hpp"
#include "game_menu.hpp"
#include "game_menu_controls.hpp"
#if MODERNGEKKO_HAS_PULSE
#include "pulse_volume.hpp"
#endif
#include "cvm_mods.hpp"
#include "data_patches.hpp"
#include "screenshot.hpp"
#include "texture_mods.hpp"
#include "window_size.hpp"
#include "moderngekko/cpu_state.h"
#include "moderngekko/mod_loader.hpp"
#include "moderngekko/module_loader.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <map>
#include <memory>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <fmt/format.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
static_assert(sizeof(ModernGekkoModuleDesc) == sizeof(StaticRecompModuleDesc));
static_assert(offsetof(ModernGekkoModuleDesc, chunk_hashes) ==
              offsetof(StaticRecompModuleDesc, chunk_hashes));
std::mutex s_runtime_mutex;
bool s_runtime_active = false;
Platform *s_platform = nullptr;
std::string s_window_title;
bool s_show_fps_in_title = true;
bool s_external_ui_common = false;
std::unique_ptr<BootSessionData> s_boot_session_data;
u64 s_previous_net_wait_ns = 0;
double s_net_wait_ms_per_second = 0.0;
std::chrono::steady_clock::time_point s_previous_net_wait_sample;

std::string FormatWindowTitle(const std::string &title, double fps) {
  if (!std::isfinite(fps) || fps < 0.0)
    fps = 0.0;
  const auto now = std::chrono::steady_clock::now();
  std::string formatted_title = fmt::format("{} | {:.1f} FPS", title, fps);
  const NetPlay::InputWaitTelemetry telemetry =
      NetPlay::NetPlayClient::GetInputWaitTelemetry();
  if (!telemetry.active) {
    s_previous_net_wait_ns = 0;
    s_net_wait_ms_per_second = 0.0;
    s_previous_net_wait_sample = {};
    return formatted_title;
  }
  if (s_previous_net_wait_sample.time_since_epoch().count() == 0) {
    s_previous_net_wait_sample = now;
    s_previous_net_wait_ns = telemetry.total_wait_ns;
  } else if (telemetry.total_wait_ns < s_previous_net_wait_ns) {
    s_previous_net_wait_sample = now;
    s_previous_net_wait_ns = telemetry.total_wait_ns;
    s_net_wait_ms_per_second = 0.0;
  } else if (now - s_previous_net_wait_sample >=
             std::chrono::milliseconds(500)) {
    const double seconds =
        std::chrono::duration<double>(now - s_previous_net_wait_sample).count();
    s_net_wait_ms_per_second =
        static_cast<double>(telemetry.total_wait_ns - s_previous_net_wait_ns) /
        1000000.0 / seconds;
    s_previous_net_wait_sample = now;
    s_previous_net_wait_ns = telemetry.total_wait_ns;
  }
  return fmt::format("{} | Net wait {:.1f} ms/s | Buffer {}", formatted_title,
                     s_net_wait_ms_per_second, telemetry.buffer_size);
}

PowerPC::CPUCore SelectCPUCore() {
  static const bool static_recomp = [] {
    const char *v = std::getenv("MODERNGEKKO_STATICRECOMP");
    return !v || !*v || *v != '0';
  }();
  if (static_recomp)
    return PowerPC::CPUCore::StaticRecomp;
#ifdef _M_ARM_64
  return PowerPC::CPUCore::JITARM64;
#else
  return PowerPC::CPUCore::JIT64;
#endif
}
// RuntimeConfig::file_mods as Dolphin's file patches (the ones Riivolution
// packs use): each file under a mod's files/ replaces, or adds, the disc's
// file at the same path. The patched disc is built in memory as the game
// boots; nothing is written to the extracted game.
std::vector<DiscIO::Riivolution::Patch>
FileModPatches(const std::vector<std::filesystem::path> &mods, bool static_recomp) {
  namespace fs = std::filesystem;
  std::vector<DiscIO::Riivolution::Patch> patches;
  for (const fs::path &mod : mods) {
    const fs::path files = mod / "files";
    std::error_code ec;
    if (!fs::is_directory(files, ec)) {
      // A texture pack or a mod of data patches has no files: those load
      // apart.
      if (!moderngekko::texture_mods::HasTextures(mod) &&
          !fs::is_regular_file(mod / "patches.ini", ec))
        std::cerr << "mods: " << PathToString(mod.filename())
                  << ": no files folder, left out\n";
      continue;
    }
    DiscIO::Riivolution::Patch patch;
    patch.m_id = PathToString(mod.filename());
    // Paths starting with '/' are read from this folder.
    const std::string root = PathToString(files);
    patch.m_file_data_loader =
        std::make_shared<DiscIO::Riivolution::FileDataLoaderHostFS>(
            root, root + "/mod.ini", "");
    std::size_t code_left_out = 0;
    std::size_t inside_cvm = 0;
    for (fs::recursive_directory_iterator
             it(files, fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
      std::error_code entry_ec;
      if (!it->is_regular_file(entry_ec))
        continue;
      std::string relative = PathToString(it->path().lexically_relative(files));
      std::replace(relative.begin(), relative.end(), '\\', '/');
      // Files in a folder named after a .cvm go inside that archive
      // (CvmModPatches).
      if (const std::size_t slash = relative.find('/');
          slash != std::string::npos && slash >= 4 &&
          moderngekko::cvm_mods::detail::Lower(relative.substr(slash - 4, 4)) == ".cvm") {
        ++inside_cvm;
        continue;
      }
      std::string extension = PathToString(it->path().extension());
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (static_recomp && (extension == ".rel" || extension == ".dol")) {
        ++code_left_out;
        continue;
      }
      DiscIO::Riivolution::File file;
      file.m_disc = "/" + relative;
      file.m_external = "/" + relative;
      file.m_create = true;
      file.m_resize = true;
      patch.m_file_patches.push_back(std::move(file));
    }
    // A mod with only files inside .cvm archives: CvmModPatches says how many.
    if (!patch.m_file_patches.empty() || code_left_out != 0 || inside_cvm == 0) {
      std::cerr << "mods: " << patch.m_id << ": " << patch.m_file_patches.size()
                << " file(s)";
      if (code_left_out != 0)
        std::cerr << ", " << code_left_out
                  << " code file(s) left out (StaticRecomp runs the game's own "
                     "code; JIT64 runs the mod's)";
      std::cerr << '\n';
    }
    if (!patch.m_file_patches.empty())
      patches.push_back(std::move(patch));
  }
  return patches;
}

// The mods' files inside the game's .cvm archives (cvm_mods.hpp): a mod's
// files/<archive>.cvm/ folder. For each archive, the writes that put them in
// as disc patches: the files themselves from the mods' folders, and the
// small changes to its index and header from Cache/cvm/<disc ID>/.
std::vector<DiscIO::Riivolution::Patch>
CvmModPatches(const std::vector<std::filesystem::path> &mods,
              const std::filesystem::path &game_root,
              const std::filesystem::path &user_directory,
              const std::string &disc_id) {
  namespace fs = std::filesystem;
  namespace cvm = moderngekko::cvm_mods;
  using cvm::detail::Lower;
  std::vector<DiscIO::Riivolution::Patch> patches;
  const fs::path cache = user_directory / "Cache" / "cvm" /
                         (disc_id.empty() ? std::string("game") : disc_id);
  std::error_code ec;
  fs::remove_all(cache, ec);

  // Per archive (by its name, any capitals): the files, in mod order, and
  // whether a mod replaces the whole archive.
  std::map<std::string, std::vector<cvm::Replacement>> inside;
  std::map<std::string, std::string> replaced_whole;
  for (const fs::path &mod : mods) {
    const std::string id = PathToString(mod.filename());
    for (fs::directory_iterator it(mod / "files", ec), end; !ec && it != end;
         it.increment(ec)) {
      const std::string name = PathToString(it->path().filename());
      if (name.size() < 5 || Lower(name.substr(name.size() - 4)) != ".cvm")
        continue;
      std::error_code entry_ec;
      if (it->is_regular_file(entry_ec)) {
        replaced_whole[Lower(name)] = id;
        continue;
      }
      if (!it->is_directory(entry_ec))
        continue;
      for (fs::recursive_directory_iterator
               file(it->path(), fs::directory_options::skip_permission_denied, entry_ec),
           done;
           !entry_ec && file != done; file.increment(entry_ec)) {
        std::error_code file_ec;
        if (!file->is_regular_file(file_ec))
          continue;
        std::string inner = PathToString(file->path().lexically_relative(it->path()));
        std::replace(inner.begin(), inner.end(), '\\', '/');
        inside[Lower(name)].push_back({inner, file->path(), id});
      }
    }
    ec.clear();
  }

  for (const auto &[key, replacements] : inside) {
    if (const auto whole = replaced_whole.find(key); whole != replaced_whole.end()) {
      std::cerr << "mods: " << whole->second << " replaces " << key
                << " whole: the files other mods put inside it are left out\n";
      continue;
    }
    // The game's archive, with its capitals.
    fs::path archive;
    for (fs::directory_iterator it(game_root / "files", ec), end; !ec && it != end;
         it.increment(ec))
      if (Lower(PathToString(it->path().filename())) == key)
        archive = it->path();
    ec.clear();
    if (archive.empty()) {
      std::cerr << "mods: the game has no " << key << ": the files for it are left out\n";
      continue;
    }
    const std::string name = PathToString(archive.filename());
    const cvm::Plan plan = cvm::PlanReplacements(archive, name, replacements);
    for (const cvm::Problem &problem : plan.problems)
      std::cerr << "mods: " << problem.where << ": " << problem.why << ", left out\n";
    if (plan.writes.empty())
      continue;
    std::cerr << "mods: " << name << ": " << plan.in_place + plan.at_end
              << " file(s) inside it (" << plan.in_place << " in place, " << plan.at_end
              << " at the end)\n";

    // The mods' files, read from their folders; the small changes, from
    // files written to the cache.
    std::map<fs::path, DiscIO::Riivolution::Patch> by_folder;
    const fs::path small = cache / name;
    std::size_t count = 0;
    for (const cvm::Write &write : plan.writes) {
      fs::path folder;
      std::string external;
      if (!write.file.empty()) {
        folder = write.file.parent_path();
        external = "/" + PathToString(write.file.filename());
      } else {
        fs::create_directories(small, ec);
        const std::string file = std::to_string(count++) + ".bin";
        std::ofstream out(small / file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(write.bytes.data()),
                  static_cast<std::streamsize>(write.bytes.size()));
        if (!out) {
          std::cerr << "mods: " << name << ": can't write the cache, its files are left out\n";
          by_folder.clear();
          break;
        }
        folder = small;
        external = "/" + file;
      }
      DiscIO::Riivolution::Patch &patch = by_folder[folder];
      if (!patch.m_file_data_loader) {
        patch.m_id = name;
        const std::string root = PathToString(folder);
        patch.m_file_data_loader =
            std::make_shared<DiscIO::Riivolution::FileDataLoaderHostFS>(
                root, root + "/mod.ini", "");
      }
      DiscIO::Riivolution::File change;
      change.m_disc = "/" + name;
      change.m_external = external;
      change.m_offset = static_cast<u32>(write.offset);
      change.m_resize = false;
      change.m_create = false;
      patch.m_file_patches.push_back(std::move(change));
    }
    for (auto &[folder, patch] : by_folder)
      patches.push_back(std::move(patch));
  }
  return patches;
}

// The mods' data patches (data_patches.hpp): each patches.ini read, the
// patched copies of the files they change made in Cache/patched/<disc ID>/,
// and those copies as one more file patch, after the mods' own files.
std::optional<DiscIO::Riivolution::Patch>
DataPatchFiles(const std::vector<std::filesystem::path> &mods,
               const std::filesystem::path &game_root,
               const std::filesystem::path &user_directory,
               const std::string &disc_id, bool static_recomp) {
  namespace fs = std::filesystem;
  namespace dp = moderngekko::data_patches;
  std::vector<dp::ModPatches> with_patches;
  for (const fs::path &mod : mods) {
    std::error_code ec;
    if (!fs::is_regular_file(mod / "patches.ini", ec))
      continue;
    std::ifstream file(mod / "patches.ini", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    std::vector<dp::Problem> problems;
    dp::ModPatches entry{PathToString(mod.filename()), mod,
                         dp::Parse(text, disc_id, &problems)};
    for (const dp::Problem &problem : problems)
      std::cerr << "mods: " << entry.id << ": " << problem.where << ": "
                << problem.why << ", left out\n";
    with_patches.push_back(std::move(entry));
  }
  const fs::path out = user_directory / "Cache" / "patched" /
                       (disc_id.empty() ? std::string("game") : disc_id);
  if (with_patches.empty()) {
    std::error_code ec;
    fs::remove_all(out, ec);
    return std::nullopt;
  }
  const dp::Built built =
      dp::BuildPatchedFiles(with_patches, game_root / "files", out, static_recomp);
  for (const dp::Problem &problem : built.problems)
    std::cerr << "mods: " << problem.where << ": " << problem.why << ", left out\n";
  for (std::size_t i = 0; i < with_patches.size(); ++i)
    std::cerr << "mods: " << with_patches[i].id << ": "
              << built.applied_by_mod[i] << " data patch(es)\n";
  if (built.files.empty())
    return std::nullopt;
  DiscIO::Riivolution::Patch patch;
  patch.m_id = "data-patches";
  const std::string root = PathToString(out);
  patch.m_file_data_loader =
      std::make_shared<DiscIO::Riivolution::FileDataLoaderHostFS>(
          root, root + "/patches.ini", "");
  for (const std::string &file : built.files) {
    DiscIO::Riivolution::File patched;
    patched.m_disc = "/" + file;
    patched.m_external = "/" + file;
    patched.m_create = true;
    patched.m_resize = true;
    patch.m_file_patches.push_back(std::move(patched));
  }
  return patch;
}
} // namespace

std::vector<std::string> Host_GetPreferredLocales() { return {}; }
void Host_PPCSymbolsChanged() {}
void Host_PPCBreakpointsChanged() {}
// The in-game menu keeps the controllers' input from the game while it is open,
// and so does a paused game running a few frames for a screenshot.
bool Host_UIBlocksControllerState() {
  return moderngekko::detail::GameMenuBlocksInput() ||
         moderngekko::detail::ScreenshotBlocksInput();
}
void Host_Message(HostMessageID id) {
  if (id == HostMessageID::WMUserStop && s_platform)
    s_platform->Stop();
}
void Host_UpdateTitle(const std::string &) {
  if (!s_platform)
    return;

  std::string title = s_window_title;
  if (s_show_fps_in_title &&
      s_platform->GetWindowSystemInfo().type != WindowSystemType::Headless)
    title = FormatWindowTitle(
        title, Core::System::GetInstance().GetPerfMetrics().GetFPS());
  s_platform->SetTitle(title);
}
void Host_UpdateDisasmDialog() {}
void Host_JitCacheInvalidation() {}
void Host_JitProfileDataWiped() {}
void Host_RequestRenderWindowSize(int, int) {}
bool Host_RendererHasFocus() {
  return !s_platform || s_platform->IsWindowFocused();
}
bool Host_RendererHasFullFocus() { return Host_RendererHasFocus(); }
bool Host_RendererIsFullscreen() {
  return s_platform && s_platform->IsWindowFullscreen();
}
bool Host_TASInputHasFocus() { return false; }
void Host_YieldToUI() {}
void Host_TitleChanged() {}
void Host_UpdateDiscordClientID(const std::string &) {}
bool Host_UpdateDiscordPresenceRaw(const std::string &, const std::string &,
                                   const std::string &, const std::string &,
                                   const std::string &, const std::string &,
                                   std::int64_t, std::int64_t, int, int) {
  return false;
}
std::unique_ptr<GBAHostInterface>
Host_CreateGBAHost(std::weak_ptr<HW::GBA::Core>) {
  return nullptr;
}

namespace moderngekko {
namespace {
struct RuntimeAutomationState
{
  mutable std::mutex mutex;
  std::atomic<std::uint64_t> frame_count{0};
  std::atomic<std::uint64_t> present_count{0};
  std::uint64_t processed_commands = 0;
  std::string last_command;
  std::string last_error;
};

void EnsureAutomationDirectories(const std::filesystem::path& root)
{
  if (root.empty())
    return;
  std::error_code ec;
  std::filesystem::create_directories(root / "commands", ec);
  std::filesystem::create_directories(root / "processed", ec);
  std::filesystem::create_directories(root / "failed", ec);
}

void SetAutomationError(RuntimeAutomationState& state, std::string message)
{
  std::lock_guard lock(state.mutex);
  state.last_error = std::move(message);
}

void MarkAutomationCommand(RuntimeAutomationState& state, std::string command_name)
{
  std::lock_guard lock(state.mutex);
  ++state.processed_commands;
  state.last_command = std::move(command_name);
  state.last_error.clear();
}

void ClearAutomationPad(int port)
{
  for (int control = static_cast<int>(ciface::Touch::FIRST_GC_CONTROL);
       control <= static_cast<int>(ciface::Touch::LAST_WII_CONTROL); ++control)
  {
    ciface::Touch::SetControlState(
        port, static_cast<ciface::Touch::ControlID>(control), 0.0);
  }
}

void ApplyAutomationPad(const automation::PadState& pad)
{
  ClearAutomationPad(pad.port);
  for (std::size_t index = 0; index < pad.controls.size(); ++index)
  {
    ciface::Touch::SetControlState(
        pad.port,
        static_cast<ciface::Touch::ControlID>(
            static_cast<int>(ciface::Touch::FIRST_GC_CONTROL) +
            static_cast<int>(index)),
        pad.controls[index]);
  }
}

std::filesystem::path NormalizeScreenshotPath(const std::filesystem::path& path)
{
  if (!path.has_extension())
    return path.string() + ".png";
  return path;
}

void SaveAutomationScreenshot(const std::filesystem::path& path)
{
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const Core::CPUThreadGuard guard(Core::System::GetInstance());
  if (g_frame_dumper)
    g_frame_dumper->SaveScreenshot(path.string());
}

std::optional<RuntimeError> ReadAutomationMemory(const std::filesystem::path& path, u32 address,
                                                 u32 size)
{
  auto& system = Core::System::GetInstance();
  const Core::CPUThreadGuard guard(system);
  const u8* source = system.GetMemory().GetPointerForRange(address, size);
  if (!source)
  {
    return RuntimeError{RuntimeErrorCode::InvalidState,
                        fmt::format("guest range {:#010x}+{} is not readable", address, size)};
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec)
  {
    return RuntimeError{RuntimeErrorCode::InitializationFailed,
                        "could not create memory output directory: " + ec.message()};
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
  {
    return RuntimeError{RuntimeErrorCode::InitializationFailed,
                        "could not open memory output file"};
  }
  output.write(reinterpret_cast<const char*>(source), size);
  if (!output)
  {
    return RuntimeError{RuntimeErrorCode::InitializationFailed,
                        "could not write memory output file"};
  }
  return {};
}

std::optional<RuntimeError> WriteAutomationMemory(u32 address,
                                                  const std::vector<std::uint8_t>& data)
{
  auto& system = Core::System::GetInstance();
  const Core::CPUThreadGuard guard(system);
  if (!system.GetMemory().GetPointerForRange(address, data.size()))
  {
    return RuntimeError{
        RuntimeErrorCode::InvalidState,
        fmt::format("guest range {:#010x}+{} is not writable", address, data.size())};
  }
  system.GetMemory().CopyToEmu(address, data.data(), data.size());
  return {};
}

std::optional<RuntimeError> ApplyAutomationCommand(Runtime& runtime,
                                                   RuntimeAutomationState& state,
                                                   const automation::Command& command,
                                                   std::stop_token stop_token)
{
  auto& system = Core::System::GetInstance();
  switch (command.type)
  {
  case automation::CommandType::Pad:
    ApplyAutomationPad(command.pad);
    break;
  case automation::CommandType::PadFrames:
  {
    if (Core::GetState(system) != Core::State::Running)
    {
      return RuntimeError{RuntimeErrorCode::InvalidState,
                          "pad_frames requires a running emulated core"};
    }
    std::uint64_t first_frame = state.frame_count.load(std::memory_order_relaxed);
    ApplyAutomationPad(command.pad);
    while (!stop_token.stop_requested())
    {
      const std::uint64_t current_frame =
          state.frame_count.load(std::memory_order_relaxed);
      if (current_frame < first_frame)
        first_frame = current_frame;
      else if (current_frame - first_frame >= command.frames)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ClearAutomationPad(command.pad.port);
    break;
  }
  case automation::CommandType::ClearPad:
    ClearAutomationPad(command.pad.port);
    break;
  case automation::CommandType::Pause:
    if (auto error = runtime.Pause())
      return error;
    break;
  case automation::CommandType::Resume:
    if (auto error = runtime.Resume())
      return error;
    break;
  case automation::CommandType::SaveState:
    std::filesystem::create_directories(command.path.parent_path());
    State::SaveAs(system, command.path.string());
    break;
  case automation::CommandType::LoadState:
    State::LoadAs(system, command.path.string());
    break;
  case automation::CommandType::Screenshot:
    SaveAutomationScreenshot(NormalizeScreenshotPath(command.path));
    break;
  case automation::CommandType::ReadMemory:
    if (auto error = ReadAutomationMemory(command.path, command.address, command.size))
      return error;
    break;
  case automation::CommandType::WriteMemory:
    if (auto error = WriteAutomationMemory(command.address, command.data))
      return error;
    break;
  case automation::CommandType::Stop:
    runtime.RequestStop();
    break;
  }
  MarkAutomationCommand(state, command.source_name);
  return {};
}

bool AutomationCommandNeedsReadyCore(automation::CommandType type)
{
  switch (type)
  {
  case automation::CommandType::Pause:
  case automation::CommandType::Resume:
  case automation::CommandType::SaveState:
  case automation::CommandType::LoadState:
  case automation::CommandType::Screenshot:
  case automation::CommandType::ReadMemory:
  case automation::CommandType::WriteMemory:
  case automation::CommandType::PadFrames:
    return true;
  case automation::CommandType::Pad:
  case automation::CommandType::ClearPad:
  case automation::CommandType::Stop:
    return false;
  }
  return true;
}

bool AutomationCoreIsReady()
{
  const auto state = Core::GetState(Core::System::GetInstance());
  return state == Core::State::Running || state == Core::State::Paused;
}

void MoveAutomationCommand(const std::filesystem::path& source, const std::filesystem::path& root,
                           const char* destination_directory)
{
  std::error_code ec;
  const std::filesystem::path destination =
      root / destination_directory / source.filename();
  std::filesystem::remove(destination, ec);
  ec.clear();
  std::filesystem::rename(source, destination, ec);
  if (ec)
  {
    ec.clear();
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec)
      std::filesystem::remove(source, ec);
  }
}

template <typename ImplT>
void WriteAutomationStatus(const std::filesystem::path& root, ImplT& impl)
{
  automation::Status status;
  const auto core_state = impl.booted ? Core::GetState(Core::System::GetInstance())
                                      : Core::State::Uninitialized;
  status.state = core_state == Core::State::Paused
                     ? "paused"
                     : (core_state == Core::State::Running ? "running" : "stopped");
  status.booted = impl.booted;
  status.title = impl.title;
  status.game_id = impl.metadata.disc_id;
  status.game_name = impl.metadata.game_name;
  if (impl.booted)
  {
    const auto& perf = Core::System::GetInstance().GetPerfMetrics();
    status.fps = perf.GetFPS();
    status.vps = perf.GetVPS();
    status.speed = perf.GetSpeed();
  }
  status.frame_count =
      impl.automation_state.frame_count.load(std::memory_order_relaxed);
  status.present_count =
      impl.automation_state.present_count.load(std::memory_order_relaxed);
  {
    std::lock_guard lock(impl.automation_state.mutex);
    status.processed_commands = impl.automation_state.processed_commands;
    status.last_command = impl.automation_state.last_command;
    status.last_error = impl.automation_state.last_error;
  }

  const std::filesystem::path status_path = root / "status.txt";
  const std::filesystem::path temp_path = root / "status.tmp";
  std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
  output << automation::FormatStatus(status);
  output.close();
  std::error_code ec;
  std::filesystem::rename(temp_path, status_path, ec);
  if (ec)
  {
    ec.clear();
    std::filesystem::copy_file(temp_path, status_path,
                               std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(temp_path, ec);
  }
}

template <typename ImplT>
void AutomationLoop(Runtime& runtime, ImplT& impl, std::stop_token stop_token)
{
  const std::filesystem::path root = impl.config.automation.directory;
  if (root.empty())
    return;

  EnsureAutomationDirectories(root);
  auto next_status_write = std::chrono::steady_clock::now();
  while (!stop_token.stop_requested())
  {

    for (const auto& command_path : automation::ListCommandFiles(root / "commands"))
    {
      automation::Command command;
      std::string error;
      if (!automation::ParseCommandFile(command_path, &command, &error))
      {
        SetAutomationError(impl.automation_state,
                           command_path.filename().string() + ": " + error);
        MoveAutomationCommand(command_path, root, "failed");
        continue;
      }

      if (AutomationCommandNeedsReadyCore(command.type) && !AutomationCoreIsReady())
        break;

      if (!command.path.empty())
        command.path = automation::ResolveControlPath(root, command.path);

      if (auto runtime_error =
              ApplyAutomationCommand(runtime, impl.automation_state, command, stop_token))
      {
        SetAutomationError(impl.automation_state,
                           command.source_name + ": " + runtime_error->message);
        MoveAutomationCommand(command_path, root, "failed");
        continue;
      }

      MoveAutomationCommand(command_path, root, "processed");
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_status_write)
    {
      WriteAutomationStatus(root, impl);
      next_status_write = now + std::chrono::milliseconds(200);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  WriteAutomationStatus(root, impl);
}

// The GameCube ports' devices as GCPadNew.ini named them when the runtime
// started, while a PadDeviceWatch looks after them (EffectivePadDevice).
std::mutex s_pad_devices_mutex;
std::optional<std::vector<std::string>> s_profile_pad_devices;

constexpr int GAMECUBE_PORTS = 4;

std::string GameCubeProfilePath() {
  return File::GetUserPath(D_CONFIG_IDX) + "GCPadNew.ini";
}

std::string GameCubeSection(int port) { return "GCPad" + std::to_string(port + 1); }

// Each GameCube port reads the device detail::EffectivePadDevices gives it:
// when the profile's gamepad isn't connected (or the profile names the
// keyboard), a connected gamepad in its place. Applied whenever controllers
// come and go, and written to GCPadNew.ini as well while the game runs, since
// Dolphin loads that file again as the game starts; the profile gets its own
// devices back when the runtime ends.
class PadDeviceWatch {
public:
  PadDeviceWatch() {
    Common::IniFile profile;
    if (profile.Load(GameCubeProfilePath())) {
      for (int port = 0; port < GAMECUBE_PORTS; ++port) {
        if (const Common::IniFile::Section *section =
                std::as_const(profile).GetSection(GameCubeSection(port)))
          section->Get("Device", &m_profile[port]);
      }
    }
    m_applied = m_profile;
    {
      std::lock_guard lock(s_pad_devices_mutex);
      s_profile_pad_devices = m_profile;
    }
    m_hook = g_controller_interface.RegisterDevicesChangedCallback([this] { Apply(); });
    Apply();
  }

  ~PadDeviceWatch() {
    m_hook.reset();
    {
      std::lock_guard lock(m_mutex);
      for (int port = 0; port < GAMECUBE_PORTS; ++port) {
        if (m_applied[port] != m_profile[port])
          WriteDevice(port, m_profile[port]);
      }
    }
    std::lock_guard lock(s_pad_devices_mutex);
    s_profile_pad_devices.reset();
  }

  PadDeviceWatch(const PadDeviceWatch &) = delete;
  PadDeviceWatch &operator=(const PadDeviceWatch &) = delete;

private:
  void Apply() {
    std::lock_guard lock(m_mutex);
    const std::vector<std::string> effective = detail::EffectivePadDevices(
        m_profile, g_controller_interface.GetAllDeviceStrings());
    for (int port = 0; port < GAMECUBE_PORTS; ++port) {
      if (effective[port] == m_applied[port])
        continue;
      m_applied[port] = effective[port];
      WriteDevice(port, effective[port]);
      ControllerEmu::EmulatedController *controller = Pad::GetConfig()->GetController(port);
      {
        const auto state_lock = ControllerEmu::EmulatedController::GetStateLock();
        controller->SetDefaultDevice(effective[port]);
      }
      controller->UpdateReferences(g_controller_interface);
      if (effective[port] == m_profile[port])
        std::cout << "input: " << GameCubeSection(port) << " reads its profile's device "
                  << m_profile[port] << " again" << std::endl;
      else
        std::cout << "input: " << GameCubeSection(port) << " reads " << effective[port]
                  << " in place of the profile's " << m_profile[port] << std::endl;
    }
  }

  // The Device line of the port's section in GCPadNew.ini.
  static void WriteDevice(int port, const std::string &device) {
    Common::IniFile profile;
    profile.Load(GameCubeProfilePath());
    profile.GetOrCreateSection(GameCubeSection(port))->Set("Device", device);
    profile.Save(GameCubeProfilePath());
  }

  std::mutex m_mutex;
  std::vector<std::string> m_profile = std::vector<std::string>(GAMECUBE_PORTS);
  std::vector<std::string> m_applied;
  Common::EventHook m_hook;
};

}  // namespace

struct Runtime::Impl {
  RuntimeConfig config;
  GameMetadata metadata;
  std::string title;
  std::unique_ptr<Platform> platform;
  std::unique_ptr<ModManager> mods;
  Common::EventHook state_hook;
  bool ui_initialized = false;
  bool controllers_initialized = false;
  bool booted = false;
  std::atomic<bool> running{false};
  RuntimeAutomationState automation_state;
  Common::EventHook present_hook;
  // config.first_picture_shown has been called (once, for the first frame).
  std::atomic<bool> first_picture_told{false};
  bool automation_registered = false;
  std::jthread automation_thread;
  // The pictures beside a game that doesn't fill the window, added to every
  // frame by border_hook on the video thread. The slot lets the in-game
  // menu switch it for another.
  std::unique_ptr<detail::GameBorderSlot> border;
  Common::EventHook border_hook;
  // The menu opened over the game (config.menu), drawn by menu_hook.
  std::unique_ptr<detail::GameMenu> menu;
  Common::EventHook menu_hook;
#if MODERNGEKKO_HAS_PULSE
  // The game's volume, which Dolphin's PulseAudio backend leaves out.
  std::unique_ptr<detail::PulseStreamVolume> pulse_volume;
#endif
  // F9's and the menu's screenshots, at graphics.screenshot_scale.
  std::unique_ptr<detail::HighResScreenshot> screenshot;
  // The GameCube ports' devices, when a profile's pad isn't connected.
  std::unique_ptr<PadDeviceWatch> pad_devices;
};

namespace detail {
std::string EffectivePadDevice(int port, const std::string &profile_device) {
  std::vector<std::string> profile;
  {
    std::lock_guard lock(s_pad_devices_mutex);
    if (!s_profile_pad_devices || port < 0 ||
        port >= static_cast<int>(s_profile_pad_devices->size()))
      return profile_device;
    profile = *s_profile_pad_devices;
  }
  return EffectivePadDevices(profile, g_controller_interface.GetAllDeviceStrings())[port];
}

void SetExternalUICommon(bool external) {
  std::lock_guard lock(s_runtime_mutex);
  s_external_ui_common = external;
}

void SetBootSessionData(std::unique_ptr<BootSessionData> boot_session_data) {
  std::lock_guard lock(s_runtime_mutex);
  s_boot_session_data = std::move(boot_session_data);
}

void ApplyAspectRatio(AspectRatio aspect_ratio, int custom_width, int custom_height) {
  // The Wii's 16:9 setting decides what the game draws. Netplay replaces the
  // system setting with the host's.
  Config::SetBase(Config::SYSCONF_WIDESCREEN,
                  aspect_ratio != AspectRatio::Standard);
  // Everything but a custom shape fills the window (so no bars beside the
  // picture), each player's in its own shape.
  const bool fill = aspect_ratio != AspectRatio::Custom;
  Config::SetBase(Config::GFX_ASPECT_RATIO, aspect_ratio == AspectRatio::Custom ? AspectMode::Custom
                                            : fill                             ? AspectMode::Stretch
                                                                               : AspectMode::Auto);
  // Custom: the picture's shape. Dolphin takes it relative to the game's own
  // 4:3 output, as it does for 16:9.
  const bool custom = aspect_ratio == AspectRatio::Custom && custom_width > 0 && custom_height > 0;
  if (custom) {
    Config::SetBase(Config::GFX_CUSTOM_ASPECT_RATIO_WIDTH, custom_width);
    Config::SetBase(Config::GFX_CUSTOM_ASPECT_RATIO_HEIGHT, custom_height);
  }
  // Ultrawide, and a custom shape other than 16:9: the 3D view takes the
  // picture's shape, wider or narrower, instead of being stretched to it
  // (Dolphin's widescreen hack, which only changes what is drawn, so Netplay
  // is unaffected). Widescreen and Standard are stretched as they are.
  Config::SetBase(Config::GFX_WIDESCREEN_HACK,
                  aspect_ratio == AspectRatio::Ultrawide ||
                      (custom && custom_width * 9 != custom_height * 16));
}

PowerPC::CPUCore SelectedCPUCore() { return SelectCPUCore(); }
} // namespace detail

ModuleSource ModuleSource::DynamicPath(std::filesystem::path path) {
  ModuleSource source;
  source.kind = Kind::DynamicPath;
  source.path = std::move(path);
  return source;
}

ModuleSource
ModuleSource::AttachedDescriptor(const ModernGekkoModuleDesc *descriptor) {
  ModuleSource source;
  source.kind = Kind::AttachedDescriptor;
  source.descriptor = descriptor;
  return source;
}

Runtime::Runtime(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

RuntimeCreateResult Runtime::Create(RuntimeConfig config) {
  std::lock_guard lock(s_runtime_mutex);
  if (s_runtime_active)
    return {
        {},
        RuntimeError{RuntimeErrorCode::AlreadyActive,
                     "only one ModernGekko runtime may be active per process"}};

  GameInspectResult inspected =
      InspectGame(config.game_root, {.hash_assets = config.hash_game_assets});
  if (!inspected)
    return {{}, RuntimeError{RuntimeErrorCode::InvalidGame, inspected.error}};

  const ModernGekkoModuleRequirements requirements = {
      MODERNGEKKO_CPU_ABI_VERSION, static_cast<std::uint32_t>(sizeof(CPUState)),
      inspected.metadata->disc_id.c_str()};
  ModuleLibrary validation_library;
  ModuleLoadResult module_result{};
  if (config.module.kind == ModuleSource::Kind::DynamicPath)
    module_result =
        validation_library.Open(config.module.path.string(), requirements);
  else if (config.module.kind == ModuleSource::Kind::AttachedDescriptor)
    module_result =
        validation_library.Attach(config.module.descriptor, requirements);
  else if (!config.allow_interpreter)
    return {
        {},
        RuntimeError{
            RuntimeErrorCode::ModuleRequired,
            "no native module was supplied; use allow_interpreter explicitly"}};

  if (config.module.kind != ModuleSource::Kind::None &&
      module_result.status != ModuleLoadStatus::Ok) {
    if (!config.allow_interpreter) {
      std::string message = "native module was rejected";
      if (module_result.status == ModuleLoadStatus::DescriptorRejected)
        message += ": " + std::string(moderngekko_module_status_string(
                              module_result.validation_status));
      return {
          {},
          RuntimeError{RuntimeErrorCode::ModuleRejected, std::move(message)}};
    }
    config.module = {};
  }
  validation_library.Close();

  auto impl = std::make_unique<Impl>();
  impl->config = std::move(config);
  impl->metadata = std::move(*inspected.metadata);
  impl->title = impl->config.window_title.value_or(
      "ModernGekko - " + impl->metadata.game_name + " [" +
      impl->metadata.disc_id + "]");
  impl->mods = std::make_unique<ModManager>();
  const ModLoadReport mod_report = impl->mods->LoadDirectories(
      impl->config.mod_directories, impl->metadata.disc_id);
  for (const ModLoadIssue &issue : mod_report.issues)
    std::fprintf(stderr, "mod rejected: %s: %s\n", issue.source.c_str(),
                 issue.message.c_str());
  for (const LoadedModInfo &mod : mod_report.loaded)
    std::fprintf(stderr, "mod loaded: %s %s\n", mod.id.c_str(),
                 mod.version.c_str());

  if (!s_external_ui_common) {
    UICommon::SetUserDirectory(impl->config.user_directory.string());
    UICommon::Init();
    impl->ui_initialized = true;
  }
  Config::SetBase(Config::MAIN_FULLSCREEN, impl->config.fullscreen);
  // Read by the fork's windows (src/platform/) as they open.
  detail::RequestedWindowSize() = {impl->config.window_width, impl->config.window_height};
  detail::WindowToCentreOn() = impl->config.centre_on_window;

  if (impl->config.headless)
    impl->platform = Platform::CreateHeadlessPlatform();
#ifdef _WIN32
  else
    impl->platform = Platform::CreateWin32Platform();
#endif
#ifdef MODERNGEKKO_HAVE_COCOA
  else impl->platform = Platform::CreateMacOSPlatform();
#endif
#ifdef HAVE_X11
  else if (impl->config.window_system != WindowSystem::Wayland) impl->platform =
      Platform::CreateX11Platform();
#endif
#ifdef HAVE_WAYLAND
  else if (impl->config.window_system != WindowSystem::X11) impl->platform =
      Platform::CreateWaylandPlatform();
#endif
  if (!impl->platform || !impl->platform->Init()) {
    if (impl->ui_initialized)
      UICommon::Shutdown();
    return {{},
            RuntimeError{RuntimeErrorCode::PlatformUnavailable,
                         "the requested Dolphin host platform is unavailable"}};
  }

  const WindowSystemInfo wsi = impl->platform->GetWindowSystemInfo();
  UICommon::InitControllers(wsi);
  impl->controllers_initialized = true;
  if (!impl->config.headless)
    impl->pad_devices = std::make_unique<PadDeviceWatch>();
  EnsureAutomationDirectories(impl->config.automation.directory);
  if (!impl->config.automation.directory.empty()) {
    for (int port = 0; port < 4; ++port)
    {
      ciface::Touch::RegisterGameCubeInputOverrider(port);
      for (int id = ciface::Touch::FIRST_GC_CONTROL; id <= ciface::Touch::LAST_GC_CONTROL; ++id)
        ciface::Touch::SetControlState(port, static_cast<ciface::Touch::ControlID>(id), 0.0);
      ciface::Touch::RegisterWiiInputOverrider(port);
      for (int id = ciface::Touch::FIRST_WII_CONTROL; id <= ciface::Touch::LAST_WII_CONTROL; ++id)
        ciface::Touch::SetControlState(port, static_cast<ciface::Touch::ControlID>(id), 0.0);
    }
    impl->automation_registered = true;
  }
  impl->platform->SetTitle(impl->title);

  Config::SetBase(Config::MAIN_CPU_CORE, SelectCPUCore());
  if (!impl->config.graphics.backend.empty())
    Config::SetBase(Config::MAIN_GFX_BACKEND, impl->config.graphics.backend);
  else if (impl->config.headless)
    Config::SetBase(Config::MAIN_GFX_BACKEND, std::string("Null"));
  if (impl->config.graphics.internal_resolution_scale)
    Config::SetBase(Config::GFX_EFB_SCALE,
                    *impl->config.graphics.internal_resolution_scale);
  Config::SetBase(Config::GFX_ENHANCE_POST_SHADER,
                  impl->config.graphics.post_processing_shader);
  Config::SetBase(Config::GFX_SHADER_CACHE, true);
  Config::SetBase(Config::GFX_SHADER_COMPILATION_MODE,
                  ShaderCompilationMode::AsynchronousUberShaders);
  Config::SetBase(Config::GFX_WAIT_FOR_SHADERS_BEFORE_STARTING, true);
  detail::ApplyAspectRatio(impl->config.graphics.aspect_ratio,
                           impl->config.graphics.custom_aspect_width,
                           impl->config.graphics.custom_aspect_height);
  const std::vector<std::string> audio_backends =
      AudioCommon::GetSoundBackends();
  if (impl->config.headless) {
    impl->config.audio.backend = BACKEND_NULLSOUND;
  } else if (impl->config.audio.backend.empty() ||
             !std::ranges::contains(audio_backends,
                                    impl->config.audio.backend)) {
    constexpr std::array preferred_backends = {
        BACKEND_CUBEB, BACKEND_PULSEAUDIO, BACKEND_ALSA};
    const auto preferred =
        std::ranges::find_if(preferred_backends, [&](const char *backend) {
          return std::ranges::contains(audio_backends, backend);
        });
    impl->config.audio.backend =
        preferred != preferred_backends.end() ? *preferred : BACKEND_NULLSOUND;
  }
  Config::SetBase(Config::MAIN_AUDIO_BACKEND, impl->config.audio.backend);
  Config::SetBase(Config::MAIN_AUDIO_VOLUME,
                  impl->config.audio.mute ? 0 : std::clamp(impl->config.audio.volume, 0, 100));
  Config::SetBase(Config::MAIN_INPUT_BACKGROUND_INPUT,
                  impl->config.input.background_input);
  // Dolphin's on-screen messages ("Wii Remote 1 connected", savestate
  // notices...) are not shown over the game.
  Config::SetBase(Config::MAIN_OSD_MESSAGES, false);
  // Custom textures: only the texture packs among the mods turned on for
  // this game, gathered into one folder (texture_mods.hpp). With none, they
  // stay off.
  {
    namespace fs = std::filesystem;
    std::vector<fs::path> texture_packs;
    for (const fs::path &mod : impl->config.file_mods)
      if (texture_mods::HasTextures(mod))
        texture_packs.push_back(mod);
    const fs::path texture_root = impl->config.user_directory / "Cache" / "textures";
    bool custom_textures = false;
    if (texture_packs.empty()) {
      texture_mods::ClearTextureView(texture_root, impl->metadata.disc_id);
    } else {
      const texture_mods::TextureView view = texture_mods::BuildTextureView(
          texture_packs, texture_root, impl->metadata.disc_id);
      if (!view.error.empty()) {
        std::cerr << "mods: textures left out: " << view.error << '\n';
      } else {
        std::cerr << "mods: textures: " << view.textures << " from " << view.mods
                  << " mod(s)" << (view.reused ? ", as last time" : "");
        if (view.copied != 0)
          std::cerr << ", " << view.copied << " copied (no link possible)";
        std::cerr << '\n';
        if (view.textures != 0) {
          File::SetUserPath(D_HIRESTEXTURES_IDX, PathToString(texture_root));
          custom_textures = true;
        }
      }
    }
    Config::SetBase(Config::GFX_HIRES_TEXTURES, custom_textures);
  }

  auto &jit = Core::System::GetInstance().GetJitInterface();
  StaticRecompModuleSource recomp_source;
  if (impl->config.module.kind == ModuleSource::Kind::DynamicPath)
    recomp_source =
        StaticRecompModuleSource::Dynamic(impl->config.module.path.string());
  else if (impl->config.module.kind == ModuleSource::Kind::AttachedDescriptor)
    recomp_source = StaticRecompModuleSource::Attached(
        reinterpret_cast<const StaticRecompModuleDesc *>(
            impl->config.module.descriptor));
  // The mods' hooks into the game's code. StaticRecomp asks them about every
  // address it runs (a few percent of the CPU thread), so they are only
  // installed when a mod is loaded: without one, nothing can hook.
  if (!impl->mods->Empty()) {
    recomp_source.host_call = &ModManager::HostCall;
    recomp_source.host_call_contains = &ModManager::HostCallContains;
    recomp_source.host_call_range_contains =
        &ModManager::HostCallRangeContains;
    recomp_source.host_call_user = impl->mods.get();
  }
  jit.SetStaticRecompModuleSource(std::move(recomp_source));

  s_runtime_active = true;
  s_platform = impl->platform.get();
  s_window_title = impl->title;
  s_show_fps_in_title = impl->config.show_fps_in_title;
  return {std::unique_ptr<Runtime>(new Runtime(std::move(impl))), {}};
}

Runtime::~Runtime() {
  RequestStop();
  StopAutomation();
  if (m_impl->booted) {
    Core::Stop(Core::System::GetInstance());
    Core::Shutdown(Core::System::GetInstance());
  }
  if (m_impl->automation_registered) {
    for (int port = 0; port < 4; ++port)
    {
      ciface::Touch::UnregisterGameCubeInputOverrider(port);
      ciface::Touch::UnregisterWiiInputOverrider(port);
    }
    m_impl->automation_registered = false;
  }
  m_impl->state_hook = {};
  m_impl->pad_devices.reset();
  if (m_impl->controllers_initialized)
    UICommon::ShutdownControllers();
  if (m_impl->ui_initialized)
    UICommon::Shutdown();
  std::lock_guard lock(s_runtime_mutex);
  s_platform = nullptr;
  s_window_title.clear();
  s_show_fps_in_title = true;
  s_runtime_active = false;
}

void Runtime::StopAutomation() {
  if (m_impl->automation_thread.joinable()) {
    m_impl->automation_thread.request_stop();
    m_impl->automation_thread.join();
  }
  m_impl->present_hook = {};
}

RuntimeRunResult Runtime::Run() {
  if (m_impl->running.exchange(true))
    return {RuntimeExitReason::BootFailed,
            RuntimeError{RuntimeErrorCode::InvalidState,
                         "runtime is already running"}};

  std::unique_ptr<BootParameters> boot;
  {
    std::lock_guard lock(s_runtime_mutex);
    if (s_boot_session_data)
      boot = BootParameters::GenerateFromFile(
          PathToString(m_impl->metadata.main_dol), std::move(*s_boot_session_data));
    else if (m_impl->config.load_state_path)
      boot = BootParameters::GenerateFromFile(
          PathToString(m_impl->metadata.main_dol),
          BootSessionData(PathToString(*m_impl->config.load_state_path),
                          DeleteSavestateAfterBoot::No));
    else
      boot =
          BootParameters::GenerateFromFile(PathToString(m_impl->metadata.main_dol));
    s_boot_session_data.reset();
  }
  if (!boot) {
    m_impl->running = false;
    return {RuntimeExitReason::BootFailed,
            RuntimeError{RuntimeErrorCode::BootFailed,
                         "Dolphin rejected the extracted disc"}};
  }
  if (!m_impl->config.file_mods.empty()) {
    const bool static_recomp =
        SelectCPUCore() == PowerPC::CPUCore::StaticRecomp;
    std::vector<DiscIO::Riivolution::Patch> patches =
        FileModPatches(m_impl->config.file_mods, static_recomp);
    // Then the files inside the .cvm archives: their places are worked out
    // from the game's archives.
    for (DiscIO::Riivolution::Patch &patch :
         CvmModPatches(m_impl->config.file_mods, m_impl->config.game_root,
                       m_impl->config.user_directory, m_impl->metadata.disc_id))
      patches.push_back(std::move(patch));
    // Data patches last: their copies already start from the mods' files.
    if (auto data = DataPatchFiles(m_impl->config.file_mods,
                                   m_impl->config.game_root,
                                   m_impl->config.user_directory,
                                   m_impl->metadata.disc_id, static_recomp))
      patches.push_back(std::move(*data));
    AddRiivolutionPatches(boot.get(), std::move(patches));
  }
  m_impl->state_hook =
      Core::AddOnStateChangedCallback([this](Core::State state) {
        if (state == Core::State::Uninitialized && m_impl->platform)
          m_impl->platform->Stop();
      });
  if (!BootManager::BootCore(Core::System::GetInstance(), std::move(boot),
                             m_impl->platform->GetWindowSystemInfo())) {
    m_impl->running = false;
    return {RuntimeExitReason::BootFailed,
            RuntimeError{RuntimeErrorCode::BootFailed,
                         "Dolphin could not boot sys/main.dol"}};
  }
  m_impl->booted = true;
  const GraphicsSettings &graphics = m_impl->config.graphics;
#if MODERNGEKKO_HAS_PULSE
  // Dolphin's PulseAudio backend plays at full volume whatever its volume
  // setting says, so the game's stream gets the volume itself, every time:
  // the sound server may have kept another one from the last game.
  if (!m_impl->config.headless && m_impl->config.audio.backend == BACKEND_PULSEAUDIO) {
    m_impl->pulse_volume = std::make_unique<detail::PulseStreamVolume>(
        m_impl->config.audio.mute ? 0 : std::clamp(m_impl->config.audio.volume, 0, 100));
  }
#endif
  if (!m_impl->config.headless) {
    // A slot even without a border, so the in-game menu can put one up.
    std::unique_ptr<detail::GameBorderOverlay> border;
    if (!graphics.border_folder.empty() && !graphics.border_id.empty())
      border = detail::GameBorderOverlay::Create(graphics.border_folder, graphics.border_id);
    m_impl->border = std::make_unique<detail::GameBorderSlot>(std::move(border));
    m_impl->border_hook = GetVideoEvents().before_present_event.Register(
        [border = m_impl->border.get()](const PresentInfo &) { border->AddToFrame(); });
  }
  if (!m_impl->config.headless) {
    // A paused game runs a few frames for a screenshot; its sound stays out
    // of them.
    std::function<void(bool)> mute;
#if MODERNGEKKO_HAS_PULSE
    if (m_impl->pulse_volume) {
      mute = [volume = m_impl->pulse_volume.get()](bool muted) {
        volume->SetMuted(muted, std::chrono::milliseconds(80));
      };
    }
#endif
    m_impl->screenshot =
        detail::HighResScreenshot::Create(graphics.screenshot_scale, graphics.screenshot_name,
                                          std::move(mute));
  }
  if (!m_impl->config.headless) {
    detail::GameMenuHooks hooks;
    hooks.draw_others = [border = m_impl->border.get()] { border->AddToFrame(); };
    // As closing the game window does.
    hooks.stop_game = [platform = m_impl->platform.get()] { platform->Stop(); };
    // Dolphin's settings change on the host thread (the game window's loop),
    // as its own settings windows change them; Dolphin applies them to the
    // running game from there.
    hooks.apply_setting = [impl = m_impl.get()](const QuickSetting &setting,
                                                std::size_t choice) {
      const std::string value = setting.choices[choice].value;
      int number = 0;
      const bool numeric =
          !value.empty() &&
          std::from_chars(value.data(), value.data() + value.size(), number).ec ==
              std::errc{};
      switch (setting.kind) {
      case QuickSetting::Kind::Effect:
        Core::QueueHostJob([value](Core::System &) {
          Config::SetBaseOrCurrent(Config::GFX_ENHANCE_POST_SHADER, value);
        });
        break;
      case QuickSetting::Kind::InternalResolution:
        if (numeric && number >= 1) {
          Core::QueueHostJob([number](Core::System &) {
            Config::SetBaseOrCurrent(Config::GFX_EFB_SCALE, number);
          });
        }
        break;
      case QuickSetting::Kind::GameVolume:
        if (numeric) {
          const int volume = impl->config.audio.mute ? 0 : std::clamp(number, 0, 100);
          Core::QueueHostJob([volume](Core::System &system) {
            Config::SetBaseOrCurrent(Config::MAIN_AUDIO_VOLUME, volume);
            AudioCommon::UpdateSoundStream(system);
          });
#if MODERNGEKKO_HAS_PULSE
          if (impl->pulse_volume)
            impl->pulse_volume->Set(volume);
#endif
        }
        break;
      case QuickSetting::Kind::Border: {
        const std::filesystem::path &folder = impl->config.graphics.border_folder;
        impl->border->Replace(value.empty() || folder.empty()
                                  ? nullptr
                                  : detail::GameBorderOverlay::Create(folder, value));
        break;
      }
      }
    };
    if (detail::HighResScreenshot *screenshot = m_impl->screenshot.get()) {
      hooks.take_screenshot = [screenshot] { return screenshot->Take(); };
      hooks.screenshot_status = [screenshot] { return screenshot->Status(); };
    }
    m_impl->menu = detail::GameMenu::Create(m_impl->config.menu, std::move(hooks));
    if (m_impl->menu) {
      m_impl->menu_hook = GetVideoEvents().before_present_event.Register(
          [menu = m_impl->menu.get()](const PresentInfo &) { menu->AddToFrame(); });
    }
  }
  m_impl->present_hook =
      GetVideoEvents().after_present_event.Register([this](const PresentInfo &info) {
        m_impl->automation_state.frame_count.store(info.frame_count,
                                                   std::memory_order_relaxed);
        m_impl->automation_state.present_count.store(info.present_count,
                                                     std::memory_order_relaxed);
        // The window has its first picture: the launcher can step aside.
        if (m_impl->config.first_picture_shown &&
            !m_impl->first_picture_told.exchange(true, std::memory_order_relaxed))
          m_impl->config.first_picture_shown();
      });
  if (!m_impl->config.automation.directory.empty()) {
    m_impl->automation_thread =
        std::jthread([this](std::stop_token stop_token) {
          AutomationLoop(*this, *m_impl, std::move(stop_token));
        });
  }
  std::jthread title_thread;
  if (!m_impl->config.headless && m_impl->config.show_fps_in_title) {
    title_thread = std::jthread([](std::stop_token stop_token) {
      while (!stop_token.stop_requested()) {
        Host_UpdateTitle({});
        for (int i = 0; i < 10 && !stop_token.stop_requested(); ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
  }
  m_impl->platform->MainLoop();
  // Unhooked before Dolphin stops: the video thread calls the hooks until then.
  m_impl->menu_hook = {};
  m_impl->border_hook = {};
  const detail::GameMenuChoice menu_choice =
      m_impl->menu ? m_impl->menu->Choice() : detail::GameMenuChoice::None;
  // Its thread reads the controllers, which go away with the runtime.
  m_impl->menu.reset();
  // After the menu, which takes its screenshots; before the sound it mutes.
  m_impl->screenshot.reset();
#if MODERNGEKKO_HAS_PULSE
  m_impl->pulse_volume.reset();
#endif
  title_thread.request_stop();
  if (title_thread.joinable())
    title_thread.join();
  m_impl->platform->SaveWindowGeometry();
  StopAutomation();
  Core::Stop(Core::System::GetInstance());
  Core::Shutdown(Core::System::GetInstance());
  m_impl->booted = false;
  m_impl->running = false;
  if (menu_choice == detail::GameMenuChoice::Quit)
    return {RuntimeExitReason::Quit, {}};
  return {};
}

void Runtime::RequestStop() {
  if (m_impl && m_impl->platform)
    m_impl->platform->RequestShutdown();
}

std::optional<RuntimeError> Runtime::Pause() {
  if (!m_impl->running)
    return RuntimeError{RuntimeErrorCode::InvalidState,
                        "runtime is not running"};
  Core::SetState(Core::System::GetInstance(), Core::State::Paused);
  return {};
}

std::optional<RuntimeError> Runtime::Resume() {
  if (!m_impl->running)
    return RuntimeError{RuntimeErrorCode::InvalidState,
                        "runtime is not running"};
  Core::SetState(Core::System::GetInstance(), Core::State::Running);
  return {};
}

const RuntimeConfig &Runtime::GetConfig() const { return m_impl->config; }
const GameMetadata &Runtime::GetGameMetadata() const {
  return m_impl->metadata;
}
const std::string &Runtime::GetWindowTitle() const { return m_impl->title; }
} // namespace moderngekko
