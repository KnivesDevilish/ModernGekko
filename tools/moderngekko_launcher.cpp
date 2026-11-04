#include "frontend_config.hpp"
#include "dol_patch.hpp"
#include "launcher_savestates.hpp"
#include "launcher_summary.hpp"
#include "moderngekko/game.hpp"
#include "netplay_session.hpp"

#include "DiscIO/DiscExtractor.h"
#include "DiscIO/Filesystem.h"
#include "DiscIO/Volume.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
#ifndef MODERNGEKKO_FRONTEND_NAME
#define MODERNGEKKO_FRONTEND_NAME "ModernGekko"
#endif

#ifndef MODERNGEKKO_RUNNER_FILENAME
#define MODERNGEKKO_RUNNER_FILENAME "moderngekko-run"
#endif

#ifndef MODERNGEKKO_USER_DIRECTORY_NAME
#define MODERNGEKKO_USER_DIRECTORY_NAME "moderngekko"
#endif

#ifndef MODERNGEKKO_LOG_FILENAME
#define MODERNGEKKO_LOG_FILENAME "ModernGekko.log"
#endif

#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
constexpr std::string_view CONTROLLER_PROFILE_NAME = "GCPadNew.ini";
constexpr std::string_view CONTROLLER_PROFILE_SECTION = "[GCPad1]";
constexpr std::string_view GENERATED_PROFILE_LABEL = "GameCube profile";
#else
constexpr std::string_view CONTROLLER_PROFILE_NAME = "WiimoteNew.ini";
constexpr std::string_view CONTROLLER_PROFILE_SECTION = "[Wiimote1]";
constexpr std::string_view GENERATED_PROFILE_LABEL = "sideways profile";
#endif

struct ExtractionState
{
  std::atomic<bool> running{false};
  std::atomic<unsigned> completed{0};
  std::atomic<unsigned> total{1};
  std::mutex mutex;
  std::string status;
  std::string error;
  std::optional<fs::path> finished_game;
};

struct DialogState
{
  std::mutex mutex;
  std::optional<fs::path> selected;
  std::string error;
};

struct ControllerOption
{
  std::string label;
  std::string device;
  SDL_JoystickID joystick_id = 0;  // lets the Controls page tester open this pad
};

std::string RequiredGameError(const moderngekko::GameMetadata& metadata)
{
#ifdef MODERNGEKKO_REQUIRED_DISC_ID
  if (metadata.disc_id != MODERNGEKKO_REQUIRED_DISC_ID)
    return "expected disc ID " MODERNGEKKO_REQUIRED_DISC_ID ", found " + metadata.disc_id;
#endif
#ifdef MODERNGEKKO_REQUIRED_DOL_SHA256
  if (metadata.dol_sha256 != MODERNGEKKO_REQUIRED_DOL_SHA256)
    return "main DOL does not match the pinned release";
#endif
#ifdef MODERNGEKKO_REQUIRED_REL_SHA256
  if (metadata.rel_sha256 != MODERNGEKKO_REQUIRED_REL_SHA256)
    return "_Main.rel does not match the pinned release";
#endif
#ifdef MODERNGEKKO_REQUIRED_ASSETS_SHA256
  if (metadata.assets_sha256 != MODERNGEKKO_REQUIRED_ASSETS_SHA256)
    return "game assets do not match the pinned release";
#endif
  return {};
}

// The launcher only compares assets_sha256 when a build pins it (see
// RequiredGameError). Without a pin, hashing the whole files/ tree just blocks
// the window for tens of seconds on a multi-GB game, so skip it. The disc ID,
// boot.bin, DOL and REL checks still run.
moderngekko::GameInspectResult InspectForLauncher(const fs::path& root)
{
  moderngekko::GameInspectOptions options;
#ifndef MODERNGEKKO_REQUIRED_ASSETS_SHA256
  options.hash_assets = false;
#endif
  return moderngekko::InspectGame(root, options);
}

std::vector<ControllerOption> EnumerateControllers()
{
  std::vector<ControllerOption> result;
  std::unordered_map<std::string, int> device_ids;
  int count = 0;
  SDL_JoystickID* joystick_ids = SDL_GetJoysticks(&count);
  for (int i = 0; i < count; ++i)
  {
    const SDL_JoystickID joystick_id = joystick_ids[i];
    const bool gamepad = SDL_IsGamepad(joystick_id);
    const char* name_value =
        gamepad ? SDL_GetGamepadNameForID(joystick_id) : SDL_GetJoystickNameForID(joystick_id);
    const std::string name = name_value && *name_value ? name_value : "Unknown Controller";
    const int device_id = device_ids[name]++;
    if (!gamepad)
      continue;
    ControllerOption option;
    option.label = device_id == 0 ? name : name + " (" + std::to_string(device_id + 1) + ")";
    option.device = "SDL/" + std::to_string(device_id) + "/" + name;
    option.joystick_id = joystick_id;
    result.emplace_back(std::move(option));
  }
  SDL_free(joystick_ids);
  return result;
}

int FindController(const std::vector<ControllerOption>& controllers, std::string_view device)
{
  const auto found = std::ranges::find(controllers, device, &ControllerOption::device);
  return found == controllers.end() ? -1 : static_cast<int>(found - controllers.begin());
}

fs::path DefaultUserDirectory()
{
#ifdef MODERNGEKKO_USER_DIRECTORY_IN_DOCUMENTS
#if defined(_WIN32)
  if (const char* user_profile = std::getenv("USERPROFILE"))
    return fs::path(user_profile) / "Documents" / MODERNGEKKO_USER_DIRECTORY_NAME;
#endif
  if (const char* home = std::getenv("HOME"))
    return fs::path(home) / "Documents" / MODERNGEKKO_USER_DIRECTORY_NAME;
#endif
#if defined(_WIN32)
  if (const char* local_app_data = std::getenv("LOCALAPPDATA"))
    return fs::path(local_app_data) / MODERNGEKKO_USER_DIRECTORY_NAME;
#endif
  if (const char* xdg = std::getenv("XDG_DATA_HOME"))
    return fs::path(xdg) / MODERNGEKKO_USER_DIRECTORY_NAME;
  if (const char* home = std::getenv("HOME"))
    return fs::path(home) / ".local/share" / MODERNGEKKO_USER_DIRECTORY_NAME;
  return std::string(MODERNGEKKO_USER_DIRECTORY_NAME) + "-user";
}

fs::path DocumentsDirectory()
{
#if defined(_WIN32)
  if (const char* user_profile = std::getenv("USERPROFILE"))
    return fs::path(user_profile) / "Documents";
#endif
  if (const char* home = std::getenv("HOME"))
    return fs::path(home) / "Documents";
  return fs::current_path();
}

fs::path ExecutableDirectory(const char* argv0)
{
  std::error_code ec;
#if defined(__linux__)
  const fs::path proc_executable = fs::read_symlink("/proc/self/exe", ec);
  if (!ec)
    return proc_executable.parent_path();
  ec.clear();
#endif
  const fs::path executable = fs::weakly_canonical(argv0, ec);
  return ec ? fs::current_path() : executable.parent_path();
}

fs::path SiblingExecutable(const fs::path& release_directory, std::string name)
{
#if defined(_WIN32)
  name += ".exe";
#endif
  const fs::path sibling = release_directory / name;
  return fs::is_regular_file(sibling) ? sibling : fs::path(name);
}

bool ApplyBundledDolPatch(const fs::path& game_root, const fs::path& release_directory,
                          bool* changed, std::string* error)
{
#ifdef MODERNGEKKO_DOL_PATCH_MANIFEST
  if (game_root.empty() || !fs::is_directory(game_root))
  {
    *changed = false;
    return true;
  }
  return moderngekko::frontend::ApplyDolPatchManifest(
      game_root / "sys" / "main.dol", release_directory / MODERNGEKKO_DOL_PATCH_MANIFEST,
      changed, error);
#else
  *changed = false;
  return true;
#endif
}

fs::path DefaultGameFile(const fs::path& user_directory, const fs::path& release_directory)
{
#ifdef MODERNGEKKO_PORTABLE_DEFAULT_GAME
  return release_directory / "default-game.txt";
#else
  return user_directory / "default-game.txt";
#endif
}

fs::path ReadDefaultGame(const fs::path& user_directory, const fs::path& release_directory)
{
  std::ifstream file(DefaultGameFile(user_directory, release_directory));
  std::string value;
  std::getline(file, value);
  if (!value.empty() && value.back() == '\r')
    value.pop_back();
  fs::path game(value);
  if (game.is_relative())
    game = release_directory / game;
  return game;
}

bool WriteDefaultGame(const fs::path& user_directory, const fs::path& release_directory,
                      const fs::path& game, std::string* error)
{
  const fs::path destination = DefaultGameFile(user_directory, release_directory);
  std::error_code ec;
  fs::create_directories(destination.parent_path(), ec);
  std::ofstream file(destination, std::ios::trunc);
  if (!file)
  {
    if (error)
      *error = "can't save default-game.txt";
    return false;
  }
  fs::path stored = game;
#ifdef MODERNGEKKO_PORTABLE_DEFAULT_GAME
  const fs::path relative = fs::relative(game, release_directory, ec);
  if (!ec && !relative.empty())
    stored = relative;
#endif
  file << stored.string() << '\n';
  return true;
}

std::vector<fs::path> FindDiscImages()
{
  std::vector<fs::path> images;
  std::error_code ec;
  const fs::path documents = DocumentsDirectory();
  if (!fs::is_directory(documents, ec))
    return images;
  fs::recursive_directory_iterator iterator(documents,
                                            fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  while (iterator != end)
  {
    if (iterator.depth() > 4)
      iterator.disable_recursion_pending();
    if (iterator->is_regular_file(ec))
    {
      std::string extension = iterator->path().extension().string();
      std::ranges::transform(extension, extension.begin(),
                             [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (extension == ".wbfs" || extension == ".iso" || extension == ".rvz")
        images.push_back(iterator->path());
    }
    iterator.increment(ec);
    if (ec)
      ec.clear();
  }
  std::ranges::sort(images);
  return images;
}

std::optional<fs::path> PrepareDisc(const fs::path& image, const fs::path& user_directory,
                                    const fs::path& release_directory, ExtractionState* state,
                                    std::string* error)
{
  std::unique_ptr<DiscIO::Volume> source = DiscIO::CreateVolume(image.string());
  if (!source)
  {
    *error = "Dolphin rejected the selected disc image";
    return std::nullopt;
  }
  const std::string source_id = source->GetGameID(source->GetGamePartition());
#ifdef MODERNGEKKO_REQUIRED_DISC_ID
  if (source_id == MODERNGEKKO_REQUIRED_DISC_ID)
    return image;
#endif
#if defined(MODERNGEKKO_DISC_PREPARER_FILENAME) && \
    defined(MODERNGEKKO_ACCEPTED_SOURCE_DISC_ID) && defined(MODERNGEKKO_REQUIRED_DISC_ID)
  if (source_id != MODERNGEKKO_ACCEPTED_SOURCE_DISC_ID)
  {
    *error = "expected clean " MODERNGEKKO_ACCEPTED_SOURCE_DISC_ID " or patched "
             MODERNGEKKO_REQUIRED_DISC_ID "; selected " + source_id;
    return std::nullopt;
  }
  const fs::path output = user_directory / "Setup" / "SonicRidersTE-2.4.6.1.iso";
  std::error_code ec;
  fs::create_directories(output.parent_path(), ec);
  if (ec)
  {
    *error = "can't create setup directory: " + ec.message();
    return std::nullopt;
  }
  {
    std::lock_guard lock(state->mutex);
    state->status = "Applying Tournament Edition 2.4.6.1";
  }
  const fs::path preparer =
      SiblingExecutable(release_directory, MODERNGEKKO_DISC_PREPARER_FILENAME);
  const std::array<std::string, 5> storage = {
      preparer.string(), "--input", image.string(), "--output", output.string()};
  std::array<const char*, 6> arguments{};
  for (std::size_t i = 0; i < storage.size(); ++i)
    arguments[i] = storage[i].c_str();
  SDL_Process* process = SDL_CreateProcess(arguments.data(), false);
  if (!process)
  {
    *error = "can't start Tournament Edition patcher: " + std::string(SDL_GetError());
    return std::nullopt;
  }
  int exit_code = 1;
  const bool waited = SDL_WaitProcess(process, true, &exit_code);
  SDL_DestroyProcess(process);
  if (!waited || exit_code != 0)
  {
    *error = "Tournament Edition patching failed";
    return std::nullopt;
  }
  return output;
#else
  *error = "this disc is not the pinned patched release";
  return std::nullopt;
#endif
}

bool ExtractDisc(const fs::path& image, const fs::path& user_directory,
                 const fs::path& release_directory, ExtractionState* state)
{
  auto fail = [&](std::string message)
  {
    std::lock_guard lock(state->mutex);
    state->error = std::move(message);
    state->running = false;
    return false;
  };

  {
    std::lock_guard lock(state->mutex);
    state->status = "Opening " + image.filename().string();
  }
  std::string preparation_error;
  const std::optional<fs::path> prepared =
      PrepareDisc(image, user_directory, release_directory, state, &preparation_error);
  if (!prepared)
    return fail(std::move(preparation_error));
  std::unique_ptr<DiscIO::Volume> volume = DiscIO::CreateVolume(prepared->string());
  if (!volume)
    return fail("Dolphin rejected the prepared disc image");

  const DiscIO::Partition partition = volume->GetGamePartition();
  const DiscIO::FileSystem* filesystem = volume->GetFileSystem(partition);
  if (!filesystem || !filesystem->IsValid())
    return fail("Dolphin could not read the game partition filesystem");

  std::string disc_id = volume->GetGameID(partition);
  if (disc_id.size() != 6)
    return fail("the selected image has an invalid disc ID");
#ifdef MODERNGEKKO_REQUIRED_DISC_ID
  if (disc_id != MODERNGEKKO_REQUIRED_DISC_ID)
    return fail("this frontend requires disc ID " MODERNGEKKO_REQUIRED_DISC_ID "; selected " +
                disc_id);
#endif
#ifdef MODERNGEKKO_PORTABLE_DEFAULT_GAME
  const fs::path games_directory = release_directory;
  const fs::path output = games_directory / "Game";
#else
  const fs::path games_directory = user_directory / "games";
  const fs::path output = games_directory / disc_id;
#endif
  std::error_code ec;
  if (fs::is_directory(output, ec))
  {
    bool changed = false;
    std::string patch_error;
    if (!ApplyBundledDolPatch(output, release_directory, &changed, &patch_error))
      return fail("existing extraction patching failed: " + patch_error);
  }
  const auto existing = InspectForLauncher(output);
  if (existing && RequiredGameError(*existing.metadata).empty())
  {
    std::string error;
    if (!WriteDefaultGame(user_directory, release_directory, output, &error))
      return fail(error);
    std::lock_guard lock(state->mutex);
    state->finished_game = output;
    state->status = "Using existing extraction";
    state->running = false;
    return true;
  }

  const fs::path staging = games_directory / (disc_id + ".extracting");
  fs::remove_all(staging, ec);
  fs::create_directories(staging / "files", ec);
  if (ec)
    return fail("can't create extraction directory: " + ec.message());

  {
    std::lock_guard lock(state->mutex);
    state->status = "Extracting system data";
  }
  if (!DiscIO::ExportSystemData(*volume, partition, staging.string()))
  {
    fs::remove_all(staging, ec);
    return fail("Dolphin failed while extracting the disc system data");
  }

  state->total = std::max(1u, filesystem->GetRoot().GetTotalChildren());
  {
    std::lock_guard lock(state->mutex);
    state->status = "Extracting game files";
  }
  DiscIO::ExportDirectory(*volume, partition, filesystem->GetRoot(), true, "",
                          (staging / "files").string(),
                          [state](const std::string& path)
                          {
                            ++state->completed;
                            std::lock_guard lock(state->mutex);
                            state->status = "Extracting " + path;
                            return false;
                          });

  bool dol_changed = false;
  std::string dol_patch_error;
  if (!ApplyBundledDolPatch(staging, release_directory, &dol_changed, &dol_patch_error))
  {
    fs::remove_all(staging, ec);
    return fail("extracted DOL patching failed: " + dol_patch_error);
  }

  const auto inspected = InspectForLauncher(staging);
  if (!inspected)
  {
    fs::remove_all(staging, ec);
    return fail("extracted game validation failed: " + inspected.error);
  }
  if (const std::string identity_error = RequiredGameError(*inspected.metadata);
      !identity_error.empty())
  {
    fs::remove_all(staging, ec);
    return fail("extracted game validation failed: " + identity_error);
  }

  fs::remove_all(output, ec);
  ec.clear();
  fs::rename(staging, output, ec);
  if (ec)
    return fail("can't publish extracted game: " + ec.message());
  std::string error;
  if (!WriteDefaultGame(user_directory, release_directory, output, &error))
    return fail(error);

  {
    std::lock_guard lock(state->mutex);
    state->finished_game = output;
    state->status = "Extraction complete";
  }
  state->running = false;
  return true;
}

void SDLCALL FileDialogCallback(void* userdata, const char* const* filelist, int)
{
  auto* state = static_cast<DialogState*>(userdata);
  std::lock_guard lock(state->mutex);
  if (!filelist)
    state->error = SDL_GetError();
  else if (filelist[0])
    state->selected = filelist[0];
}

fs::path SiblingRunner(const char* argv0)
{
  std::error_code ec;
  const fs::path self = fs::weakly_canonical(argv0, ec);
  fs::path runner = MODERNGEKKO_RUNNER_FILENAME;
#if defined(_WIN32)
  runner += ".exe";
#endif
  const fs::path sibling = self.parent_path() / runner;
  return fs::is_regular_file(sibling) ? sibling : runner;
}

// SR-ZGS accent: a neon lime green. Shared by the theme and the few texts that
// pick a colour explicitly (the Play header and the READY status).
constexpr ImVec4 LAUNCHER_ACCENT(0.72f, 1.0f, 0.18f, 1.0f);

// Launcher colours and shapes: near-black neutral background, soft grey panels
// and the lime accent. Sizes are unscaled; the caller applies ScaleAllSizes()
// for the display's DPI afterwards.
void ApplyLauncherTheme(ImGuiStyle& style)
{
  style.WindowPadding = ImVec2(18.0f, 16.0f);
  style.FramePadding = ImVec2(10.0f, 6.0f);
  style.ItemSpacing = ImVec2(10.0f, 8.0f);
  style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
  style.ScrollbarSize = 12.0f;
  style.WindowRounding = 0.0f;
  style.ChildRounding = 10.0f;
  style.FrameRounding = 6.0f;
  style.PopupRounding = 8.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabRounding = 6.0f;
  style.TabRounding = 6.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;

  const ImVec4 accent = LAUNCHER_ACCENT;
  const auto with_alpha = [](ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
  };
  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.58f, 0.60f, 1.0f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.031f, 0.031f, 0.035f, 1.0f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.067f, 0.067f, 0.075f, 1.0f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.075f, 0.082f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.157f, 0.157f, 0.169f, 1.0f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.098f, 0.098f, 0.110f, 1.0f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.133f, 0.133f, 0.149f, 1.0f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.165f, 0.165f, 0.184f, 1.0f);
  colors[ImGuiCol_Button] = ImVec4(0.110f, 0.110f, 0.122f, 1.0f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.153f, 0.153f, 0.169f, 1.0f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.196f, 0.196f, 0.216f, 1.0f);
  colors[ImGuiCol_Header] = with_alpha(accent, 0.18f);
  colors[ImGuiCol_HeaderHovered] = with_alpha(accent, 0.28f);
  colors[ImGuiCol_HeaderActive] = with_alpha(accent, 0.38f);
  colors[ImGuiCol_CheckMark] = accent;
  colors[ImGuiCol_SliderGrab] = accent;
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.82f, 1.0f, 0.45f, 1.0f);
  colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
  colors[ImGuiCol_SeparatorHovered] = with_alpha(accent, 0.6f);
  colors[ImGuiCol_SeparatorActive] = accent;
  colors[ImGuiCol_Tab] = colors[ImGuiCol_ChildBg];
  colors[ImGuiCol_TabHovered] = with_alpha(accent, 0.25f);
  colors[ImGuiCol_TabSelected] = ImVec4(0.14f, 0.19f, 0.06f, 1.0f);
  colors[ImGuiCol_TabSelectedOverline] = accent;
  colors[ImGuiCol_TabDimmed] = colors[ImGuiCol_ChildBg];
  colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.11f, 0.14f, 0.06f, 1.0f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.26f, 0.26f, 0.29f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.32f, 0.32f, 0.35f, 1.0f);
  colors[ImGuiCol_PlotHistogram] = accent;
  colors[ImGuiCol_TextLink] = accent;
  colors[ImGuiCol_NavCursor] = accent;
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);
  // StyleColorsDark() defaults that would still show up blue: the title bar of
  // popup windows (Netplay), selected text in input fields, the resize grip.
  colors[ImGuiCol_TitleBg] = colors[ImGuiCol_ChildBg];
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.110f, 0.110f, 0.122f, 1.0f);
  colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_ChildBg];
  colors[ImGuiCol_TextSelectedBg] = with_alpha(accent, 0.30f);
  colors[ImGuiCol_ResizeGripHovered] = with_alpha(accent, 0.5f);
  colors[ImGuiCol_ResizeGripActive] = with_alpha(accent, 0.8f);
}

// stb_truetype (used by ImGui) reads TrueType outlines only, and a file it cannot
// parse would trip an assert. Check the header before handing a file over.
bool IsTrueTypeFont(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  std::array<unsigned char, 4> tag{};
  if (!file.read(reinterpret_cast<char*>(tag.data()), tag.size()))
    return false;
  constexpr std::array<unsigned char, 4> version_1 = {0x00, 0x01, 0x00, 0x00};
  constexpr std::array<unsigned char, 4> apple_true = {'t', 'r', 'u', 'e'};
  return tag == version_1 || tag == apple_true;
}

struct LauncherFonts
{
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;  // the regular font when no bold face of its family exists
};

// A clean sans-serif for the interface: the system's usual UI fonts first
// (Windows, Fedora/Bazzite and its Distrobox mount, Debian/Ubuntu, Arch), then
// the font Dolphin ships in Sys/, then ImGui's built-in bitmap font. The bold
// face is taken from the same family as the regular one so the two match.
LauncherFonts LoadInterfaceFont(const fs::path& release_directory, float size)
{
  struct Candidate
  {
    fs::path regular;
    fs::path bold;
  };
  std::vector<Candidate> candidates;
#if defined(_WIN32)
  if (const char* windows = std::getenv("WINDIR"))
  {
    candidates.push_back({fs::path(windows) / "Fonts" / "segoeui.ttf",
                          fs::path(windows) / "Fonts" / "segoeuib.ttf"});
  }
#endif
  for (const char* directory : {"/usr/share/fonts/google-noto", "/usr/local/share/fonts/google-noto",
                                "/usr/share/fonts/noto", "/usr/share/fonts/truetype/noto"})
  {
    candidates.push_back({fs::path(directory) / "NotoSans-Regular.ttf",
                          fs::path(directory) / "NotoSans-Bold.ttf"});
  }
  for (const char* directory : {"/usr/share/fonts/liberation-sans-fonts",
                                "/usr/local/share/fonts/liberation-sans-fonts",
                                "/usr/share/fonts/truetype/liberation"})
  {
    candidates.push_back({fs::path(directory) / "LiberationSans-Regular.ttf",
                          fs::path(directory) / "LiberationSans-Bold.ttf"});
  }
  for (const char* directory : {"/usr/share/fonts/TTF", "/usr/share/fonts/truetype/dejavu",
                                "/usr/share/fonts/dejavu-sans-fonts"})
  {
    candidates.push_back(
        {fs::path(directory) / "DejaVuSans.ttf", fs::path(directory) / "DejaVuSans-Bold.ttf"});
  }
  candidates.push_back({release_directory / "Sys" / "Resources" / "OSD_Font.ttf", {}});

  const auto usable = [](const fs::path& path) {
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(path, ec) && IsTrueTypeFont(path);
  };
  ImGuiIO& io = ImGui::GetIO();
  for (const Candidate& candidate : candidates)
  {
    if (!usable(candidate.regular))
      continue;
    LauncherFonts fonts;
    fonts.regular = io.Fonts->AddFontFromFileTTF(candidate.regular.string().c_str(), size);
    if (!fonts.regular)
      continue;
    ImGui::GetStyle().FontSizeBase = size;
    if (usable(candidate.bold))
      fonts.bold = io.Fonts->AddFontFromFileTTF(candidate.bold.string().c_str(), size);
    std::cout << "launcher font: " << candidate.regular.string() << '\n';
    if (fonts.bold)
      std::cout << "launcher bold font: " << candidate.bold.string() << '\n';
    else
      fonts.bold = fonts.regular;
    return fonts;
  }
  LauncherFonts fonts;
  fonts.regular = io.Fonts->AddFontDefault();
  fonts.bold = fonts.regular;
  std::cout << "launcher font: ImGui default\n";
  return fonts;
}

enum class LauncherPage
{
  Play,
  Settings,
  Controls,
  GameFiles,
};

// One entry of the sidebar. The current page is drawn in bold with a lime bar
// on its left edge, the others in the dimmed text colour.
bool DrawNavigationItem(const char* label, bool selected, ImFont* bold, float scale)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Text, selected ? style.Colors[ImGuiCol_Text]
                                                : style.Colors[ImGuiCol_TextDisabled]);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.07f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
  if (selected)
    ImGui::PushFont(bold, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
  const ImVec2 position = ImGui::GetCursorScreenPos();
  const float height = ImGui::GetFrameHeight() + 6.0f * scale;
  ImGui::Indent(12.0f * scale);
  const bool clicked = ImGui::Selectable(label, selected, ImGuiSelectableFlags_None,
                                         ImVec2(0.0f, height));
  ImGui::Unindent(12.0f * scale);
  if (selected)
  {
    ImGui::GetWindowDrawList()->AddRectFilled(
        position, ImVec2(position.x + 3.0f * scale, position.y + height),
        ImGui::GetColorU32(LAUNCHER_ACCENT), 2.0f * scale);
  }
  ImGui::PopStyleVar();
  if (selected)
    ImGui::PopFont();
  ImGui::PopStyleColor(4);
  return clicked;
}

// Small rounded label, e.g. the CPU core in the top bar. With in_top_bar it is
// right-aligned to the window's content edge and centred on a frame-high line,
// so it sits level with text drawn after AlignTextToFramePadding().
void DrawStatusPill(const char* text, ImVec4 color, float scale, bool in_top_bar)
{
  const ImVec2 padding(10.0f * scale, 3.0f * scale);
  const ImVec2 text_size = ImGui::CalcTextSize(text);
  const ImVec2 size(text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
  if (in_top_bar)
  {
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - size.x));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         std::max(0.0f, (ImGui::GetFrameHeight() - size.y) * 0.5f));
  }
  const ImVec2 position = ImGui::GetCursorScreenPos();
  ImVec4 background = color;
  background.w = 0.14f;
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y),
                           ImGui::GetColorU32(background), size.y * 0.5f);
  draw_list->AddText(ImVec2(position.x + padding.x, position.y + padding.y),
                     ImGui::GetColorU32(color), text);
  ImGui::Dummy(size);
}

// Optional images the build copies into Resources/ next to the launcher: the
// Play page banner (MODERNGEKKO_LAUNCHER_BANNER) and the controller artwork
// of the Controls page (MODERNGEKKO_LAUNCHER_CONTROLLER_ART). A missing or
// unreadable file just means no image, and the page shows its text alone.
struct LauncherImage
{
  SDL_Texture* texture = nullptr;
  float aspect = 0.0f;  // height / width
};
using LauncherBanner = LauncherImage;

LauncherImage LoadLauncherImage(SDL_Renderer* renderer, const fs::path& release_directory,
                                std::string_view file_name, std::string_view log_label)
{
  LauncherImage image;
  const fs::path path = release_directory / "Resources" / file_name;
  std::error_code ec;
  if (!fs::is_regular_file(path, ec))
    return image;
  SDL_Surface* surface = SDL_LoadPNG(path.string().c_str());
  if (!surface)
  {
    std::cout << log_label << ": can't load " << path.string() << ": " << SDL_GetError()
              << '\n';
    return image;
  }
  if (surface->w > 0 && surface->h > 0)
  {
    image.texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (image.texture)
    {
      image.aspect = static_cast<float>(surface->h) / static_cast<float>(surface->w);
      std::cout << log_label << ": " << path.string() << '\n';
    }
  }
  SDL_DestroySurface(surface);
  return image;
}

LauncherBanner LoadLauncherBanner(SDL_Renderer* renderer, const fs::path& release_directory)
{
  return LoadLauncherImage(renderer, release_directory, "launcher-banner.png", "launcher banner");
}

// Draws the banner across the full content width with the panels' rounding.
void DrawLauncherBanner(const LauncherBanner& banner)
{
  const float width = ImGui::GetContentRegionAvail().x;
  const ImVec2 size(width, width * banner.aspect);
  const ImVec2 position = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddImageRounded(
      ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(banner.texture))),
      position, ImVec2(position.x + size.x, position.y + size.y), ImVec2(0.0f, 0.0f),
      ImVec2(1.0f, 1.0f), IM_COL32_WHITE, ImGui::GetStyle().ChildRounding);
  ImGui::Dummy(size);
}

// The page's main action: full width, accent colour, dark bold label.
bool DrawPrimaryButton(const char* label, ImFont* bold, float height)
{
  ImGui::PushStyleColor(ImGuiCol_Button, LAUNCHER_ACCENT);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 1.0f, 0.40f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.90f, 0.10f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.04f, 0.05f, 0.02f, 1.0f));
  ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.2f);
  const bool clicked = ImGui::Button(label, ImVec2(-1.0f, height));
  ImGui::PopFont();
  ImGui::PopStyleColor(4);
  return clicked;
}

// Title of the Settings, Controls and Game Files pages, with one line of help.
void DrawPageHeader(const char* title, const char* subtitle, ImFont* bold)
{
  ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.35f);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", subtitle);
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
}

// A bordered panel that grows with its content, headed by a small caps label.
// Every BeginSection() needs a matching EndSection().
void BeginSection(const char* id, const char* heading)
{
  // A little less vertical padding than other panels, so that a page with
  // three sections (Settings) still fits the default window without scrolling.
  const ImVec2 padding = ImGui::GetStyle().WindowPadding;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x, padding.y * 0.75f));
  ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  ImGui::PopStyleVar();
  ImGui::TextDisabled("%s", heading);
}

void EndSection()
{
  ImGui::EndChild();
  ImGui::Spacing();
}

// A quiet text button, such as "Host Netplay" under PLAY.
bool DrawLinkButton(const char* label)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
  ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
  const bool clicked = ImGui::Button(label);
  ImGui::PopStyleColor(4);
  return clicked;
}

// One card of the PLAY page summary: a small heading over one value. With a
// destination it is a button; the tooltip says where it leads.
struct SummaryTile
{
  const char* id = "";
  const char* heading = "";
  std::string value;
  bool warning = false;  // the value is drawn in amber
  std::string tooltip;
  bool clickable = false;
  // Shown instead of value when value doesn't fit; "..." cuts it otherwise.
  std::string short_value;
};

// What the PLAY page shows below the button, built from plain values so the
// page code only gathers them.
struct PlaySummary
{
  std::uint64_t time_played_ms = 0;
  // The connected gamepad's name, or empty when the profile's pad is missing.
  std::string controller;
  bool keyboard_only = false;
  // GameCube mode: the first controller also answers to the keyboard, so the
  // game is playable while the pad is unplugged.
  bool keys_without_pad = false;
  std::string_view aspect_ratio;  // "16:9", "4:3" or "stretch"
  bool fullscreen = false;
  int installed_mods = 0;
  std::string mods_folder;
};

std::array<SummaryTile, 4> BuildPlaySummaryTiles(const PlaySummary& summary)
{
  std::array<SummaryTile, 4> tiles;
  tiles[0] = {"##summary_time", "TIME PLAYED",
              moderngekko::frontend::FormatTimePlayed(summary.time_played_ms), false, {}, false};

  SummaryTile& controls = tiles[1];
  controls = {"##summary_controls", "CONTROLS", {}, false, "Open Controls", true};
  if (!summary.controller.empty())
    controls.value = summary.controller;
  else if (summary.keyboard_only)
    controls.value = "Keyboard only";
  else if (summary.keys_without_pad)
  {
    controls.value = "Keyboard";
    controls.tooltip = "Your controller isn't connected; the keyboard still works.\n"
                       "Open Controls";
  }
  else
  {
    controls.value = "Not detected";
    controls.warning = true;
  }

  const std::string_view aspect = summary.aspect_ratio == "stretch" ? "Stretch"
                                  : summary.aspect_ratio.empty()   ? "16:9"
                                                                   : summary.aspect_ratio;
  tiles[2] = {"##summary_display", "DISPLAY",
              std::string(aspect) +
                  (summary.fullscreen ? " \xC2\xB7 Fullscreen" : " \xC2\xB7 Window"),
              false, "Open Settings", true,
              std::string(aspect) + (summary.fullscreen ? " \xC2\xB7 Full" : " \xC2\xB7 Win")};

  tiles[3] = {"##summary_mods", "MODS",
              summary.installed_mods == 0 ? std::string("None")
              : summary.installed_mods == 1
                  ? std::string("1 installed")
                  : std::to_string(summary.installed_mods) + " installed",
              false, "Mods go in " + summary.mods_folder, false};
  return tiles;
}

// Cuts text to max_width with "...", one whole UTF-8 character at a time.
std::string FitTextToWidth(ImFont* font, float size, const std::string& text, float max_width)
{
  if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= max_width)
    return text;
  std::string fitted = text;
  while (!fitted.empty())
  {
    std::size_t cut = fitted.size() - 1;
    while (cut > 0 && (static_cast<unsigned char>(fitted[cut]) & 0xC0) == 0x80)
      --cut;
    fitted.erase(cut);
    while (!fitted.empty() && fitted.back() == ' ')
      fitted.pop_back();
    const std::string candidate = fitted + "...";
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x <= max_width)
      return candidate;
  }
  return "...";
}

float SummaryTileHeight(float scale)
{
  const float value_size = ImGui::GetFontSize();
  return 9.0f * scale * 2.0f + value_size * 0.78f + 3.0f * scale + value_size;
}

// Draws one card at the cursor. Returns true when a clickable card is clicked.
bool DrawSummaryTile(const SummaryTile& tile, float width, ImFont* bold, float scale)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  ImFont* font = ImGui::GetFont();
  const float value_size = ImGui::GetFontSize();
  const float heading_size = value_size * 0.78f;
  const ImVec2 padding(12.0f * scale, 9.0f * scale);
  const ImVec2 size(width, SummaryTileHeight(scale));
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 max(min.x + size.x, min.y + size.y);
  const bool pressed = ImGui::InvisibleButton(tile.id, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool highlight = tile.clickable && hovered;

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(min, max,
                           ImGui::GetColorU32(highlight ? ImGuiCol_FrameBgHovered : ImGuiCol_ChildBg),
                           style.ChildRounding);
  ImVec4 border = style.Colors[ImGuiCol_Border];
  if (highlight)
  {
    border = LAUNCHER_ACCENT;
    border.w = 0.55f;
  }
  draw_list->AddRect(min, max, ImGui::GetColorU32(border), style.ChildRounding, 0,
                     std::max(1.0f, style.ChildBorderSize));
  draw_list->AddText(font, heading_size, ImVec2(min.x + padding.x, min.y + padding.y),
                     ImGui::GetColorU32(ImGuiCol_TextDisabled), tile.heading);
  const float text_width = size.x - padding.x * 2.0f;
  const bool use_short =
      !tile.short_value.empty() &&
      bold->CalcTextSizeA(value_size, FLT_MAX, 0.0f, tile.value.c_str()).x > text_width;
  const std::string shown =
      FitTextToWidth(bold, value_size, use_short ? tile.short_value : tile.value, text_width);
  const ImVec4 value_color =
      tile.warning ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) : style.Colors[ImGuiCol_Text];
  draw_list->AddText(bold, value_size,
                     ImVec2(min.x + padding.x, min.y + padding.y + heading_size + 3.0f * scale),
                     ImGui::GetColorU32(value_color), shown.c_str());
  if (hovered)
  {
    if (tile.clickable)
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    // A cut value is shown whole in the tooltip.
    std::string tooltip = shown != tile.value ? tile.value : std::string();
    if (!tile.tooltip.empty())
      tooltip += (tooltip.empty() ? "" : "\n") + tile.tooltip;
    if (!tooltip.empty())
      ImGui::SetTooltip("%s", tooltip.c_str());
  }
  return tile.clickable && pressed;
}

// The four cards in one row. Returns the index of a clicked card, or -1.
int DrawSummaryRow(const std::array<SummaryTile, 4>& tiles, ImFont* bold, float scale)
{
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float width =
      std::floor((ImGui::GetContentRegionAvail().x - spacing * (tiles.size() - 1)) /
                 static_cast<float>(tiles.size()));
  int clicked = -1;
  for (std::size_t i = 0; i < tiles.size(); ++i)
  {
    if (i > 0)
      ImGui::SameLine(0.0f, spacing);
    if (DrawSummaryTile(tiles[i], width, bold, scale))
      clicked = static_cast<int>(i);
  }
  return clicked;
}

std::string_view TrimView(std::string_view text)
{
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// Inside a container (Distrobox, Toolbx, Docker) udev's hotplug events don't
// reach SDL, so a controller plugged in after start goes unnoticed. SDL then
// has to watch /dev/input itself; it does so on its own in Flatpak and Snap,
// but it doesn't recognise these containers.
bool RunningInContainer()
{
#if defined(__linux__)
  std::error_code ec;
  return fs::exists("/run/.containerenv", ec) || fs::exists("/.dockerenv", ec) ||
         std::getenv("CONTAINER_ID") != nullptr;
#else
  return false;
#endif
}

// Points the first slot of an existing controller profile at another pad and
// keeps every binding. The same pad can show up under another name depending
// on how it's connected (one may be a "PS3 Controller" over USB and an "Xbox
// One S Controller" over Bluetooth); SDL gives both the same button layout,
// so the mapping still applies. The previous file is kept as <profile>.bak.
bool RetargetControllerProfile(const fs::path& profile, std::string_view device,
                               std::string* error)
{
  if (device.empty() || device.find_first_of("\r\n") != std::string_view::npos)
  {
    *error = "invalid controller device";
    return false;
  }
  std::vector<std::string> lines;
  {
    std::ifstream input(profile);
    if (!input)
    {
      *error = "can't read " + profile.string();
      return false;
    }
    for (std::string line; std::getline(input, line);)
      lines.push_back(std::move(line));
  }
  const std::string device_line = "Device = " + std::string(device);
  std::optional<std::size_t> section_line;
  bool replaced = false;
  for (std::size_t i = 0; i < lines.size() && !replaced; ++i)
  {
    const std::string_view trimmed = TrimView(lines[i]);
    if (trimmed.starts_with('['))
    {
      if (section_line)
        break;  // the slot ended without a Device line
      if (trimmed == CONTROLLER_PROFILE_SECTION)
        section_line = i;
      continue;
    }
    const std::size_t separator = trimmed.find('=');
    if (section_line && separator != std::string_view::npos &&
        TrimView(trimmed.substr(0, separator)) == "Device")
    {
      lines[i] = device_line;
      replaced = true;
    }
  }
  if (!section_line)
  {
    *error = profile.filename().string() + " has no " + std::string(CONTROLLER_PROFILE_SECTION) +
             " section";
    return false;
  }
  if (!replaced)
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*section_line + 1), device_line);

  std::error_code ec;
  fs::path backup = profile;
  backup += ".bak";
  fs::copy_file(profile, backup, fs::copy_options::overwrite_existing, ec);
  if (ec)
  {
    *error = "can't back up " + profile.string() + ": " + ec.message();
    return false;
  }
  fs::path temporary = profile;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    for (const std::string& line : lines)
      output << line << '\n';
    if (!output)
    {
      *error = "can't write " + temporary.string();
      return false;
    }
  }
  fs::rename(temporary, profile, ec);
  if (ec)
  {
    *error = "can't replace " + profile.string() + ": " + ec.message();
    return false;
  }
  return true;
}

// ---- Controller tester (Controls page) -------------------------------------
// Reads the first slot of the controller profile ([GCPad1] or [Wiimote1]) and
// lights up, on drawings of the controller and of the keyboard, the inputs
// the player presses. It only reads: neither the profile nor the pad
// settings are changed here.

// Names that Dolphin's SDL backend gives to SDL3 gamepad buttons, in
// SDL_GamepadButton order (InputCommon/ControllerInterface/SDL/SDLGamepad.h).
constexpr std::array<std::string_view, 21> DOLPHIN_SDL_BUTTON_NAMES = {
    "Button S", "Button E",   "Button W",   "Button N", "Back",     "Guide",    "Start",
    "Thumb L",  "Thumb R",    "Shoulder L", "Shoulder R", "Pad N",  "Pad S",    "Pad W",
    "Pad E",    "Misc 1",     "Paddle 1",   "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad"};

// One physical input: a gamepad button, one direction of a gamepad axis, or a
// keyboard key.
struct InputSource
{
  int button = -1;    // SDL_GamepadButton
  int axis = -1;      // SDL_GamepadAxis
  int direction = 1;  // +1 or -1: the side of the axis that presses
  int key = -1;       // SDL_Scancode

  bool IsKey() const { return key >= 0; }
};

std::optional<InputSource> ParseDolphinPadInput(std::string_view name)
{
  for (std::size_t i = 0; i < DOLPHIN_SDL_BUTTON_NAMES.size(); ++i)
  {
    if (name == DOLPHIN_SDL_BUTTON_NAMES[i])
      return InputSource{.button = static_cast<int>(i)};
  }
  // Dolphin also matches the face buttons by their XInput position names.
  constexpr std::array<std::pair<std::string_view, SDL_GamepadButton>, 4> face_aliases = {{
      {"Button A", SDL_GAMEPAD_BUTTON_SOUTH},
      {"Button B", SDL_GAMEPAD_BUTTON_EAST},
      {"Button X", SDL_GAMEPAD_BUTTON_WEST},
      {"Button Y", SDL_GAMEPAD_BUTTON_NORTH},
  }};
  for (const auto& [alias, button] : face_aliases)
  {
    if (name == alias)
      return InputSource{.button = static_cast<int>(button)};
  }
  if (name == "Trigger L")
    return InputSource{.axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER};
  if (name == "Trigger R")
    return InputSource{.axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
  constexpr std::array<std::string_view, 4> stick_axes = {"Left X", "Left Y", "Right X",
                                                          "Right Y"};
  for (std::size_t i = 0; i < stick_axes.size(); ++i)
  {
    if (name.size() != stick_axes[i].size() + 1 || !name.starts_with(stick_axes[i]))
      continue;
    const char sign = name.back();
    if (sign != '+' && sign != '-')
      continue;
    int direction = sign == '+' ? 1 : -1;
    // Dolphin names the vertical axes the XInput way: "Y+" is up, which SDL
    // reports as a negative value.
    if (i % 2 == 1)
      direction = -direction;
    return InputSource{.axis = static_cast<int>(i), .direction = direction};
  }
  return std::nullopt;
}

// Keyboard keys as Dolphin's backends name them; X11, DirectInput and macOS
// spell some of them differently.
std::optional<SDL_Scancode> ParseDolphinKey(std::string_view name)
{
  if (name.size() == 1)
  {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    if (c >= 'A' && c <= 'Z')
      return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'A'));
    if (c >= '1' && c <= '9')
      return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (c - '1'));
    if (c == '0')
      return SDL_SCANCODE_0;
  }
  constexpr std::array<std::pair<std::string_view, SDL_Scancode>, 29> named_keys = {{
      {"Return", SDL_SCANCODE_RETURN},     {"RETURN", SDL_SCANCODE_RETURN},
      {"Up", SDL_SCANCODE_UP},             {"UP", SDL_SCANCODE_UP},
      {"Up Arrow", SDL_SCANCODE_UP},       {"Down", SDL_SCANCODE_DOWN},
      {"DOWN", SDL_SCANCODE_DOWN},         {"Down Arrow", SDL_SCANCODE_DOWN},
      {"Left", SDL_SCANCODE_LEFT},         {"LEFT", SDL_SCANCODE_LEFT},
      {"Left Arrow", SDL_SCANCODE_LEFT},   {"Right", SDL_SCANCODE_RIGHT},
      {"RIGHT", SDL_SCANCODE_RIGHT},       {"Right Arrow", SDL_SCANCODE_RIGHT},
      {"Shift_L", SDL_SCANCODE_LSHIFT},    {"LSHIFT", SDL_SCANCODE_LSHIFT},
      {"Left Shift", SDL_SCANCODE_LSHIFT}, {"Shift_R", SDL_SCANCODE_RSHIFT},
      {"RSHIFT", SDL_SCANCODE_RSHIFT},     {"Right Shift", SDL_SCANCODE_RSHIFT},
      {"space", SDL_SCANCODE_SPACE},       {"SPACE", SDL_SCANCODE_SPACE},
      {"Space", SDL_SCANCODE_SPACE},       {"Tab", SDL_SCANCODE_TAB},
      {"TAB", SDL_SCANCODE_TAB},           {"Control_L", SDL_SCANCODE_LCTRL},
      {"LCONTROL", SDL_SCANCODE_LCTRL},    {"Escape", SDL_SCANCODE_ESCAPE},
      {"ESCAPE", SDL_SCANCODE_ESCAPE},
  }};
  for (const auto& [key_name, scancode] : named_keys)
  {
    if (name == key_name)
      return scancode;
  }
  return std::nullopt;
}

// The tester understands the bindings the launcher writes: input names joined
// by "|", such as "`Pad N` | `Left Y+`". A name may carry its device, as in
// "`XInput2/0/Virtual core pointer:A`"; names without one belong to the
// profile's Device. Anything else (Dolphin's "&", "!", functions...) is
// reported as custom and not tested.
struct ProfileBinding
{
  std::vector<InputSource> sources;
  bool custom = false;
};

void ParseBindingExpression(std::string_view expression, bool device_is_pad,
                            ProfileBinding& binding)
{
  std::string term;
  bool quoted = false;
  const auto finish_term = [&]
  {
    std::string_view name = TrimView(term);
    bool pad = device_is_pad;
    if (const std::size_t colon = name.rfind(':'); colon != std::string_view::npos)
    {
      pad = TrimView(name.substr(0, colon)).starts_with("SDL/");
      name = TrimView(name.substr(colon + 1));
    }
    // Inputs that are neither pad buttons, pad axes nor known keys (motors,
    // IR, motion sensors) are skipped.
    if (pad)
    {
      if (const std::optional<InputSource> source = ParseDolphinPadInput(name))
        binding.sources.push_back(*source);
    }
    else if (const std::optional<SDL_Scancode> key = ParseDolphinKey(name))
    {
      binding.sources.push_back(InputSource{.key = *key});
    }
    term.clear();
  };
  for (const char c : expression)
  {
    if (c == '`')
    {
      quoted = !quoted;
      continue;
    }
    if (quoted || std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '\t')
    {
      term += c;
      continue;
    }
    if (c == '|')
    {
      finish_term();
      continue;
    }
    binding.custom = true;
    return;
  }
  if (quoted)
  {
    binding.custom = true;
    return;
  }
  finish_term();
}

// A spot of the controller drawing: a circle, a rectangle or a convex polygon,
// in pixels of the drawing's 1024 x 1024 canvas.
struct ArtHotspot
{
  std::size_t input;
  int point_count;  // 1: circle at points[0], 2: rectangle, 3+: convex polygon
  std::array<ImVec2, 6> points;
  float radius;
};

// One row of the list beside the drawing; the stick and D-pad rows cover
// several inputs.
struct LegendRow
{
  const char* label;
  std::size_t first;
  std::size_t last;
};

// Where the SR-ZGS controller drawing has each of its buttons. These belong
// to that drawing: other artwork bundled with
// MODERNGEKKO_LAUNCHER_CONTROLLER_ART needs its own positions here.
constexpr float CONTROLLER_ART_CANVAS = 1024.0f;
constexpr ImVec2 CONTROLLER_ART_MIN(40.0f, 258.0f);   // the drawing's bounds; the rest
constexpr ImVec2 CONTROLLER_ART_MAX(984.0f, 854.0f);  // of the canvas is transparent
constexpr std::array<ImVec2, 6> ART_LEFT_SHOULDER = {
    {{189, 337}, {209, 314}, {244, 295}, {286, 284}, {336, 283}, {370, 312}}};
constexpr std::array<ImVec2, 6> ART_RIGHT_SHOULDER = {
    {{654, 312}, {720, 282}, {749, 286}, {785, 297}, {827, 325}, {834, 337}}};
constexpr std::array<ImVec2, 6> ART_DPAD_UP = {{{256, 430}, {298, 498}}};
constexpr std::array<ImVec2, 6> ART_DPAD_DOWN = {{{256, 532}, {298, 600}}};
constexpr std::array<ImVec2, 6> ART_DPAD_LEFT = {{{193, 494}, {261, 536}}};
constexpr std::array<ImVec2, 6> ART_DPAD_RIGHT = {{{293, 494}, {361, 536}}};

#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
// The GameCube controller inputs the tester shows, with the profile keys
// behind each one.
enum ProfileInput : std::size_t
{
  INPUT_A,
  INPUT_B,
  INPUT_X,
  INPUT_Y,
  INPUT_Z,
  INPUT_L,
  INPUT_R,
  INPUT_START,
  INPUT_C,  // the drawing's extra button, kept for mods; not a GameCube input
  INPUT_STICK_UP,
  INPUT_STICK_DOWN,
  INPUT_STICK_LEFT,
  INPUT_STICK_RIGHT,
  INPUT_DPAD_UP,
  INPUT_DPAD_DOWN,
  INPUT_DPAD_LEFT,
  INPUT_DPAD_RIGHT,
  INPUT_COUNT,
};

constexpr std::array<std::array<std::string_view, 3>, INPUT_COUNT> PROFILE_INPUT_KEYS = {{
    {"Buttons/A"},
    {"Buttons/B"},
    {"Buttons/X"},
    {"Buttons/Y"},
    {"Buttons/Z"},
    {"Triggers/L", "Triggers/L-Analog"},
    {"Triggers/R", "Triggers/R-Analog"},
    {"Buttons/Start"},
    {"Extra/C"},
    {"Main Stick/Up"},
    {"Main Stick/Down"},
    {"Main Stick/Left"},
    {"Main Stick/Right"},
    {"D-Pad/Up"},
    {"D-Pad/Down"},
    {"D-Pad/Left"},
    {"D-Pad/Right"},
}};

constexpr const char* PROFILE_LEGEND_TITLE = "GameCube";

// What a keyboard key shows on the keyboard drawing: the button's name, or
// an arrow (1 up, 2 down, 3 left, 4 right) for the stick and the D-pad.
constexpr std::array<const char*, INPUT_COUNT> INPUT_KEY_LABELS = {
    "A", "B", "X", "Y", "Z", "L", "R", "START", "C", "", "", "", "", "", "", "", ""};
constexpr std::array<int, INPUT_COUNT> INPUT_KEY_ARROWS = {0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                           1, 2, 3, 4, 1, 2, 3, 4};

constexpr std::array<LegendRow, 10> LEGEND_ROWS = {{
    {"A", INPUT_A, INPUT_A},
    {"B", INPUT_B, INPUT_B},
    {"X", INPUT_X, INPUT_X},
    {"Y", INPUT_Y, INPUT_Y},
    {"Z", INPUT_Z, INPUT_Z},
    {"L", INPUT_L, INPUT_L},
    {"R", INPUT_R, INPUT_R},
    {"Start", INPUT_START, INPUT_START},
    {"Stick", INPUT_STICK_UP, INPUT_DPAD_RIGHT},  // stick and D-pad
    {"C", INPUT_C, INPUT_C},
}};

// The drawing's six face buttons are A, B, C and X, Y, Z, and its shoulders
// are L and R. GameCube has no C: the profile keeps it apart (Extra/C) for
// mods. The D-pad arms light for the stick as well as for the D-pad.
constexpr std::array<ArtHotspot, 17> CONTROLLER_ART_HOTSPOTS = {{
    {INPUT_A, 1, {{{646, 605}}}, 42},
    {INPUT_B, 1, {{{735, 553}}}, 42},
    {INPUT_X, 1, {{{599, 515}}}, 42},
    {INPUT_Y, 1, {{{688, 461}}}, 42},
    {INPUT_Z, 1, {{{786, 427}}}, 42},
    {INPUT_C, 1, {{{831, 518}}}, 42},
    {INPUT_R, 6, ART_RIGHT_SHOULDER, 0},
    {INPUT_L, 6, ART_LEFT_SHOULDER, 0},
    {INPUT_START, 1, {{{595, 405}}}, 22},  // +
    {INPUT_STICK_UP, 2, ART_DPAD_UP, 0},
    {INPUT_STICK_DOWN, 2, ART_DPAD_DOWN, 0},
    {INPUT_STICK_LEFT, 2, ART_DPAD_LEFT, 0},
    {INPUT_STICK_RIGHT, 2, ART_DPAD_RIGHT, 0},
    {INPUT_DPAD_UP, 2, ART_DPAD_UP, 0},
    {INPUT_DPAD_DOWN, 2, ART_DPAD_DOWN, 0},
    {INPUT_DPAD_LEFT, 2, ART_DPAD_LEFT, 0},
    {INPUT_DPAD_RIGHT, 2, ART_DPAD_RIGHT, 0},
}};
#else
// The Wii Remote inputs the tester shows, with the profile keys behind each one.
enum ProfileInput : std::size_t
{
  INPUT_A,
  INPUT_B,
  INPUT_1,
  INPUT_2,
  INPUT_MINUS,
  INPUT_PLUS,
  INPUT_HOME,
  INPUT_UP,
  INPUT_DOWN,
  INPUT_LEFT,
  INPUT_RIGHT,
  INPUT_SHAKE,
  INPUT_COUNT,
};

constexpr std::array<std::array<std::string_view, 3>, INPUT_COUNT> PROFILE_INPUT_KEYS = {{
    {"Buttons/A"},
    {"Buttons/B"},
    {"Buttons/1"},
    {"Buttons/2"},
    {"Buttons/-"},
    {"Buttons/+"},
    {"Buttons/Home"},
    {"D-Pad/Up"},
    {"D-Pad/Down"},
    {"D-Pad/Left"},
    {"D-Pad/Right"},
    {"Shake/X", "Shake/Y", "Shake/Z"},
}};

constexpr const char* PROFILE_LEGEND_TITLE = "Wii Remote";

constexpr std::array<const char*, INPUT_COUNT> INPUT_KEY_LABELS = {
    "A", "B", "1", "2", "\xE2\x88\x92", "+", "HOME", "", "", "", "", "SHAKE"};
constexpr std::array<int, INPUT_COUNT> INPUT_KEY_ARROWS = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 0};

constexpr std::array<LegendRow, 9> LEGEND_ROWS = {{
    {"A", INPUT_A, INPUT_A},
    {"B", INPUT_B, INPUT_B},
    {"1", INPUT_1, INPUT_1},
    {"2", INPUT_2, INPUT_2},
    {"\xE2\x88\x92", INPUT_MINUS, INPUT_MINUS},
    {"+", INPUT_PLUS, INPUT_PLUS},
    {"HOME", INPUT_HOME, INPUT_HOME},
    {"D-Pad", INPUT_UP, INPUT_RIGHT},
    {"Shake", INPUT_SHAKE, INPUT_SHAKE},
}};

// Wii A and B on the shoulders, as the generated profile maps them, 1 and 2
// on X (west) and A (south), and the shake on Z.
constexpr std::array<ArtHotspot, 12> CONTROLLER_ART_HOTSPOTS = {{
    {INPUT_A, 6, ART_LEFT_SHOULDER, 0},
    {INPUT_B, 6, ART_RIGHT_SHOULDER, 0},
    {INPUT_1, 1, {{{599, 515}}}, 42},
    {INPUT_2, 1, {{{646, 605}}}, 42},
    {INPUT_MINUS, 1, {{{429, 405}}}, 22},
    {INPUT_PLUS, 1, {{{595, 405}}}, 22},
    {INPUT_HOME, 3, {{{512, 566}, {554, 642}, {470, 642}}}, 0},  // centre triangle
    {INPUT_UP, 2, ART_DPAD_UP, 0},
    {INPUT_DOWN, 2, ART_DPAD_DOWN, 0},
    {INPUT_LEFT, 2, ART_DPAD_LEFT, 0},
    {INPUT_RIGHT, 2, ART_DPAD_RIGHT, 0},
    {INPUT_SHAKE, 1, {{{786, 427}}}, 42},
}};
#endif

// Where the SR-ZGS keyboard drawing (1768 x 608) has the keys the generated
// profile uses: the top of each keycap. Keys missing here are still listed by
// name when the drawing isn't bundled. Like the controller positions, these
// belong to that drawing (MODERNGEKKO_LAUNCHER_KEYBOARD_ART).
constexpr ImVec2 KEYBOARD_ART_SIZE(1768.0f, 608.0f);
struct KeyboardArtKey
{
  SDL_Scancode key;
  ImVec2 min;
  ImVec2 max;
};
constexpr std::array<KeyboardArtKey, 12> KEYBOARD_ART_KEYS = {{
    {SDL_SCANCODE_Q, {226, 156}, {320, 232}},
    {SDL_SCANCODE_W, {330, 156}, {422, 232}},
    {SDL_SCANCODE_E, {432, 156}, {524, 232}},
    {SDL_SCANCODE_A, {246, 259}, {336, 334}},
    {SDL_SCANCODE_S, {351, 259}, {439, 334}},
    {SDL_SCANCODE_D, {455, 259}, {543, 334}},
    {SDL_SCANCODE_LSHIFT, {56, 360}, {278, 433}},
    {SDL_SCANCODE_RETURN, {1403, 260}, {1604, 332}},
    {SDL_SCANCODE_UP, {1528, 361}, {1612, 431}},
    {SDL_SCANCODE_LEFT, {1428, 462}, {1512, 535}},
    {SDL_SCANCODE_DOWN, {1537, 462}, {1619, 535}},
    {SDL_SCANCODE_RIGHT, {1636, 462}, {1722, 535}},
}};

struct ControllerTester
{
  bool loaded = false;  // the profile was read since the Controls page opened
  std::string device;   // the profile's Device: the pad Dolphin will use
  std::array<ProfileBinding, INPUT_COUNT> bindings;
  bool has_pad_sources = false;
  bool has_key_sources = false;
  bool show_keyboard = false;  // which drawing the page shows
  SDL_JoystickID open_id = 0;
  SDL_Gamepad* gamepad = nullptr;
};

void LoadTesterProfile(ControllerTester& tester, const fs::path& profile)
{
  tester.device.clear();
  tester.bindings = {};
  std::unordered_map<std::string, std::string> values;
  std::ifstream input(profile);
  std::string line;
  bool in_first_slot = false;
  while (std::getline(input, line))
  {
    const std::string_view trimmed = TrimView(line);
    if (trimmed.starts_with('['))
    {
      in_first_slot = trimmed == CONTROLLER_PROFILE_SECTION;
      continue;
    }
    const std::size_t separator = trimmed.find('=');
    if (!in_first_slot || separator == std::string_view::npos)
      continue;
    values[std::string(TrimView(trimmed.substr(0, separator)))] =
        std::string(TrimView(trimmed.substr(separator + 1)));
  }
  if (const auto device = values.find("Device"); device != values.end())
    tester.device = device->second;
  // Names without a device belong to the profile's Device: a gamepad unless
  // the profile points at a keyboard.
  const bool device_is_pad = tester.device.empty() || tester.device.starts_with("SDL/");
  for (std::size_t input_index = 0; input_index < INPUT_COUNT; ++input_index)
  {
    for (const std::string_view key : PROFILE_INPUT_KEYS[input_index])
    {
      if (key.empty())
        continue;
      if (const auto found = values.find(std::string(key)); found != values.end())
        ParseBindingExpression(found->second, device_is_pad, tester.bindings[input_index]);
    }
  }
  tester.has_pad_sources = false;
  tester.has_key_sources = false;
  for (const ProfileBinding& binding : tester.bindings)
  {
    for (const InputSource& source : binding.sources)
    {
      tester.has_pad_sources = tester.has_pad_sources || !source.IsKey();
      tester.has_key_sources = tester.has_key_sources || source.IsKey();
    }
  }
  tester.loaded = true;
}

bool IsSourceDown(SDL_Gamepad* gamepad, const InputSource& source)
{
  if (source.IsKey())
  {
    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);
    return keys && source.key < key_count && keys[source.key];
  }
  if (!gamepad)
    return false;
  if (source.button >= 0)
    return SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(source.button));
  const float value =
      static_cast<float>(SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(source.axis))) /
      32767.0f;
  return value * static_cast<float>(source.direction) > 0.5f;
}

bool IsInputDown(const ControllerTester& tester, std::size_t input)
{
  return std::ranges::any_of(tester.bindings[input].sources, [&](const InputSource& source)
                             { return IsSourceDown(tester.gamepad, source); });
}

// Reads the profile when the Controls page opens, keeps the profile's pad open
// only while the page is shown, and turns to the drawing of the controller or
// of the keyboard, whichever was pressed last.
void UpdateControllerTester(ControllerTester& tester, bool page_visible, const fs::path& profile,
                            const std::vector<ControllerOption>& controllers)
{
  bool just_loaded = false;
  if (!page_visible)
  {
    tester.loaded = false;
  }
  else if (!tester.loaded)
  {
    LoadTesterProfile(tester, profile);
    just_loaded = true;
  }
  SDL_JoystickID wanted = 0;
  if (page_visible)
  {
    if (const int index = FindController(controllers, tester.device); index >= 0)
      wanted = controllers[static_cast<std::size_t>(index)].joystick_id;
  }
  if (wanted != tester.open_id)
  {
    if (tester.gamepad)
      SDL_CloseGamepad(tester.gamepad);
    tester.gamepad = wanted != 0 ? SDL_OpenGamepad(wanted) : nullptr;
    tester.open_id = wanted;
  }
  if (!page_visible)
    return;
  if (just_loaded)
    tester.show_keyboard = !tester.gamepad && tester.has_key_sources;
  bool pad_down = false;
  bool key_down = false;
  for (const ProfileBinding& binding : tester.bindings)
  {
    for (const InputSource& source : binding.sources)
    {
      if (IsSourceDown(tester.gamepad, source))
        (source.IsKey() ? key_down : pad_down) = true;
    }
  }
  if (pad_down != key_down)
    tester.show_keyboard = key_down;
  if (!tester.has_key_sources)
    tester.show_keyboard = false;
  else if (!tester.has_pad_sources)
    tester.show_keyboard = true;
}

// A short, readable name for an input. The face buttons use the label SDL
// knows for this pad (A, Cross...), or their position when it knows none.
std::string SourceName(SDL_Gamepad* gamepad, const InputSource& source)
{
  if (source.IsKey())
    return SDL_GetScancodeName(static_cast<SDL_Scancode>(source.key));
  if (source.button < 0)
  {
    if (source.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
      return "L trigger";
    if (source.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
      return "R trigger";
    const bool horizontal = source.axis % 2 == 0;
    const std::string stick = source.axis < 2 ? "Left stick " : "Right stick ";
    if (horizontal)
      return stick + (source.direction > 0 ? "right" : "left");
    return stick + (source.direction > 0 ? "down" : "up");
  }
  const auto button = static_cast<SDL_GamepadButton>(source.button);
  if (source.button <= SDL_GAMEPAD_BUTTON_NORTH)
  {
    switch (gamepad ? SDL_GetGamepadButtonLabel(gamepad, button) :
                      SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN)
    {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
      return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B:
      return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X:
      return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
      return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
      return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
      return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
      return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
      return "Triangle";
    default:
      break;
    }
    constexpr std::array<const char*, 4> positions = {"South button", "East button",
                                                      "West button", "North button"};
    return positions[static_cast<std::size_t>(source.button)];
  }
  switch (button)
  {
  case SDL_GAMEPAD_BUTTON_BACK:
    return "Back";
  case SDL_GAMEPAD_BUTTON_GUIDE:
    return "Home";
  case SDL_GAMEPAD_BUTTON_START:
    return "Start";
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
    return "L stick press";
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    return "R stick press";
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    return "L shoulder";
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    return "R shoulder";
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return "D-pad up";
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return "D-pad down";
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return "D-pad left";
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return "D-pad right";
  default:
    return std::string(DOLPHIN_SDL_BUTTON_NAMES[static_cast<std::size_t>(source.button)]);
  }
}

// Rows that cover several directions name whole controls ("D-pad, Left
// stick", "Arrow keys") instead of listing every direction.
std::string SourceGroup(SDL_Gamepad* gamepad, const InputSource& source)
{
  if (source.IsKey())
  {
    if (source.key == SDL_SCANCODE_UP || source.key == SDL_SCANCODE_DOWN ||
        source.key == SDL_SCANCODE_LEFT || source.key == SDL_SCANCODE_RIGHT)
      return "Arrow keys";
    return SourceName(gamepad, source);
  }
  if (source.button >= SDL_GAMEPAD_BUTTON_DPAD_UP && source.button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
    return "D-pad";
  if (source.button < 0 && source.axis <= SDL_GAMEPAD_AXIS_LEFTY)
    return "Left stick";
  if (source.button < 0 && source.axis <= SDL_GAMEPAD_AXIS_RIGHTY)
    return "Right stick";
  return SourceName(gamepad, source);
}

ImU32 HighlightFill()
{
  return ImGui::GetColorU32(
      ImVec4(LAUNCHER_ACCENT.x, LAUNCHER_ACCENT.y, LAUNCHER_ACCENT.z, 0.45f));
}

void DrawArtHotspot(ImDrawList* draw_list, const ArtHotspot& spot, ImVec2 origin, float zoom,
                    float scale)
{
  const auto to_screen = [&](ImVec2 point)
  {
    return ImVec2(origin.x + (point.x - CONTROLLER_ART_MIN.x) * zoom,
                  origin.y + (point.y - CONTROLLER_ART_MIN.y) * zoom);
  };
  const ImU32 fill = HighlightFill();
  const ImU32 edge = ImGui::GetColorU32(LAUNCHER_ACCENT);
  const float thickness = 2.5f * scale;
  if (spot.point_count == 1)
  {
    const ImVec2 center = to_screen(spot.points[0]);
    draw_list->AddCircleFilled(center, spot.radius * zoom, fill);
    draw_list->AddCircle(center, spot.radius * zoom + thickness, edge, 0, thickness);
  }
  else if (spot.point_count == 2)
  {
    const float rounding = 4.0f * scale;
    draw_list->AddRectFilled(to_screen(spot.points[0]), to_screen(spot.points[1]), fill, rounding);
    draw_list->AddRect(to_screen(spot.points[0]), to_screen(spot.points[1]), edge, rounding, 0,
                       thickness);
  }
  else
  {
    std::array<ImVec2, 6> screen{};
    for (int i = 0; i < spot.point_count; ++i)
      screen[static_cast<std::size_t>(i)] = to_screen(spot.points[static_cast<std::size_t>(i)]);
    draw_list->AddConvexPolyFilled(screen.data(), spot.point_count, fill);
    draw_list->AddPolyline(screen.data(), spot.point_count, edge, ImDrawFlags_Closed, thickness);
  }
}

// A small filled triangle pointing up (1), down (2), left (3) or right (4).
void DrawArrowGlyph(ImDrawList* draw_list, ImVec2 center, float half, int direction, ImU32 color)
{
  const float x = center.x;
  const float y = center.y;
  switch (direction)
  {
  case 1:
    draw_list->AddTriangleFilled({x, y - half}, {x + half, y + half * 0.7f},
                                 {x - half, y + half * 0.7f}, color);
    break;
  case 2:
    draw_list->AddTriangleFilled({x - half, y - half * 0.7f}, {x + half, y - half * 0.7f},
                                 {x, y + half}, color);
    break;
  case 3:
    draw_list->AddTriangleFilled({x - half, y}, {x + half * 0.7f, y - half},
                                 {x + half * 0.7f, y + half}, color);
    break;
  case 4:
    draw_list->AddTriangleFilled({x - half * 0.7f, y - half}, {x + half, y},
                                 {x - half * 0.7f, y + half}, color);
    break;
  default:
    break;
  }
}

// The controller drawing, scaled to `width`, with the pressed inputs lit.
void DrawControllerArt(const ControllerTester& tester, const LauncherImage& art, float width,
                       float scale)
{
  const float zoom = width / (CONTROLLER_ART_MAX.x - CONTROLLER_ART_MIN.x);
  const ImVec2 size(width, (CONTROLLER_ART_MAX.y - CONTROLLER_ART_MIN.y) * zoom);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddImage(
      ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(art.texture))),
      origin, ImVec2(origin.x + size.x, origin.y + size.y),
      ImVec2(CONTROLLER_ART_MIN.x / CONTROLLER_ART_CANVAS,
             CONTROLLER_ART_MIN.y / CONTROLLER_ART_CANVAS),
      ImVec2(CONTROLLER_ART_MAX.x / CONTROLLER_ART_CANVAS,
             CONTROLLER_ART_MAX.y / CONTROLLER_ART_CANVAS));
  for (const ArtHotspot& spot : CONTROLLER_ART_HOTSPOTS)
  {
    if (IsInputDown(tester, spot.input))
      DrawArtHotspot(draw_list, spot, origin, zoom, scale);
  }
  ImGui::Dummy(size);
}

// The keyboard drawing, centred. Every key the profile uses is outlined and
// labelled with what it presses, and lights up while held.
void DrawKeyboardArt(const ControllerTester& tester, const LauncherImage& art, ImFont* bold,
                     float scale)
{
  // A little narrower than the panel, so that with its hint line it is no
  // taller than the controller drawing's list and the page doesn't scroll.
  const float available = ImGui::GetContentRegionAvail().x;
  const float width = std::floor(available * 0.9f);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((available - width) * 0.5f));
  const float zoom = width / KEYBOARD_ART_SIZE.x;
  const ImVec2 size(width, KEYBOARD_ART_SIZE.y * zoom);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddImage(
      ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(art.texture))),
      origin, ImVec2(origin.x + size.x, origin.y + size.y));
  const auto to_screen = [&](ImVec2 point)
  { return ImVec2(origin.x + point.x * zoom, origin.y + point.y * zoom); };

  const ImU32 edge = ImGui::GetColorU32(LAUNCHER_ACCENT);
  const ImU32 quiet_edge =
      ImGui::GetColorU32(ImVec4(LAUNCHER_ACCENT.x, LAUNCHER_ACCENT.y, LAUNCHER_ACCENT.z, 0.55f));
  const ImU32 label_background = IM_COL32(0, 0, 0, 190);
  const float rounding = 3.0f * scale;
  ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 0.68f);
  const float label_height = ImGui::GetFontSize();
  std::vector<std::pair<int, std::size_t>> drawn;  // L and L-Analog share a key
  for (std::size_t input = 0; input < INPUT_COUNT; ++input)
  {
    for (const InputSource& source : tester.bindings[input].sources)
    {
      if (!source.IsKey() ||
          std::ranges::find(drawn, std::pair{source.key, input}) != drawn.end())
        continue;
      const auto spot = std::ranges::find(KEYBOARD_ART_KEYS, static_cast<SDL_Scancode>(source.key),
                                          &KeyboardArtKey::key);
      if (spot == KEYBOARD_ART_KEYS.end())
        continue;
      drawn.emplace_back(source.key, input);
      const ImVec2 min = to_screen(spot->min);
      const ImVec2 max = to_screen(spot->max);
      const bool down = IsSourceDown(tester.gamepad, source);
      if (down)
      {
        draw_list->AddRectFilled(min, max, HighlightFill(), rounding);
        draw_list->AddRect(min, max, edge, rounding, 0, 2.5f * scale);
      }
      else
      {
        draw_list->AddRect(min, max, quiet_edge, rounding, 0, 1.5f * scale);
      }
      const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
      const ImU32 label_color = down ? edge : IM_COL32_WHITE;
      if (INPUT_KEY_ARROWS[input] != 0)
      {
        const float half = label_height * 0.5f;
        draw_list->AddRectFilled({center.x - half - 2.0f * scale, center.y - half - 2.0f * scale},
                                 {center.x + half + 2.0f * scale, center.y + half + 2.0f * scale},
                                 label_background, rounding);
        DrawArrowGlyph(draw_list, center, half * 0.7f, INPUT_KEY_ARROWS[input], label_color);
      }
      else
      {
        const char* label = INPUT_KEY_LABELS[input];
        const ImVec2 text = ImGui::CalcTextSize(label);
        const ImVec2 padding(4.0f * scale, 1.0f * scale);
        draw_list->AddRectFilled(
            {center.x - text.x * 0.5f - padding.x, center.y - text.y * 0.5f - padding.y},
            {center.x + text.x * 0.5f + padding.x, center.y + text.y * 0.5f + padding.y},
            label_background, rounding);
        draw_list->AddText({center.x - text.x * 0.5f, center.y - text.y * 0.5f}, label_color,
                           label);
      }
    }
  }
  ImGui::PopFont();
  ImGui::Dummy(size);
}

// The rows beside the controller drawing (or alone, without one): each
// button of the emulated controller and the pad inputs or keys behind it.
void DrawInputLegend(const ControllerTester& tester, bool keys, ImFont* bold)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::BeginGroup();
  ImGui::PushFont(bold, 0.0f);
  float label_width = 0.0f;
  for (const LegendRow& row : LEGEND_ROWS)
    label_width = std::max(label_width, ImGui::CalcTextSize(row.label).x);
  ImGui::PopFont();
  label_width = std::max(label_width, ImGui::CalcTextSize(PROFILE_LEGEND_TITLE).x);
  label_width += style.ItemSpacing.x * 2.0f;
  // Rows sit close together so the list is about as tall as the artwork.
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
  // Inside a group, SameLine() offsets count from the group's left edge.
  ImGui::TextDisabled("%s", PROFILE_LEGEND_TITLE);
  ImGui::SameLine(label_width);
  ImGui::TextDisabled(keys ? "Key" : "Your controller");
  for (const LegendRow& row : LEGEND_ROWS)
  {
    bool down = false;
    bool custom = false;
    std::vector<std::string> names;
    for (std::size_t input = row.first; input <= row.last; ++input)
    {
      down = down || IsInputDown(tester, input);
      custom = custom || tester.bindings[input].custom;
      for (const InputSource& source : tester.bindings[input].sources)
      {
        if (source.IsKey() != keys)
          continue;
        std::string name = row.first == row.last ? SourceName(tester.gamepad, source) :
                                                   SourceGroup(tester.gamepad, source);
        if (std::ranges::find(names, name) == names.end())
          names.push_back(std::move(name));
      }
    }
    std::string text;
    for (const std::string& name : names)
      text += (text.empty() ? "" : ", ") + name;
    if (custom)
      text = "edited by hand";
    else if (text.empty())
      text = "not set";
    ImGui::PushFont(bold, 0.0f);
    if (down)
      ImGui::TextColored(LAUNCHER_ACCENT, "%s", row.label);
    else
      ImGui::TextUnformatted(row.label);
    ImGui::PopFont();
    ImGui::SameLine(label_width);
    ImGui::PushTextWrapPos(0.0f);
    if (down)
      ImGui::TextWrapped("%s", text.c_str());
    else
      ImGui::TextDisabled("%s", text.c_str());
    ImGui::PopTextWrapPos();
  }
}

void EndInputLegend()
{
  ImGui::PopStyleVar();
  ImGui::EndGroup();
}

// A quiet tab: the current one in the accent colour.
bool DrawTesterTab(const char* label, bool selected)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Button,
                        selected ? ImVec4(LAUNCHER_ACCENT.x, LAUNCHER_ACCENT.y,
                                          LAUNCHER_ACCENT.z, 0.16f) :
                                   ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
  ImGui::PushStyleColor(ImGuiCol_Text,
                        selected ? LAUNCHER_ACCENT : style.Colors[ImGuiCol_TextDisabled]);
  const bool clicked = ImGui::SmallButton(label);
  ImGui::PopStyleColor(4);
  return clicked;
}

// The tester under the profile line of the Controls page. It starts on that
// line, with the Controller / Keyboard tabs at its right end when the profile
// has both, then shows the matching drawing (or its list of inputs).
void DrawControllerTester(ControllerTester& tester, const LauncherImage& controller_art,
                          const LauncherImage& keyboard_art, ImFont* bold, float scale)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  if (tester.has_pad_sources && tester.has_key_sources)
  {
    const float tabs_width = ImGui::CalcTextSize("Controller").x +
                             ImGui::CalcTextSize("Keyboard").x + style.FramePadding.x * 4.0f +
                             style.ItemSpacing.x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, ImGui::GetContentRegionAvail().x - tabs_width));
    if (DrawTesterTab("Controller", !tester.show_keyboard))
      tester.show_keyboard = false;
    ImGui::SameLine();
    if (DrawTesterTab("Keyboard", tester.show_keyboard))
      tester.show_keyboard = true;
  }
  ImGui::Spacing();

  if (tester.show_keyboard)
  {
    if (keyboard_art.texture)
    {
      DrawKeyboardArt(tester, keyboard_art, bold, scale);
      ImGui::TextColored(LAUNCHER_ACCENT, "Press keys to test them.");
    }
    else
    {
      DrawInputLegend(tester, true, bold);
      ImGui::TextColored(LAUNCHER_ACCENT, "Press keys to test them.");
      EndInputLegend();
    }
    return;
  }

  // How to use the tester, or why it can't light anything up.
  const auto draw_hint = [&]
  {
    if (tester.gamepad)
    {
      ImGui::TextColored(LAUNCHER_ACCENT, "Press buttons to test them.");
    }
    else if (tester.device.empty())
    {
      ImGui::TextDisabled("No controller in %s.", std::string(CONTROLLER_PROFILE_NAME).c_str());
    }
    else
    {
      // "SDL/0/PS3 Controller" -> "PS3 Controller"
      const std::size_t name_start = tester.device.find('/', tester.device.find('/') + 1);
      const std::string name =
          name_start == std::string::npos ? tester.device : tester.device.substr(name_start + 1);
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Connect %s.", name.c_str());
    }
  };
  if (!controller_art.texture)
  {
    DrawInputLegend(tester, false, bold);
    ImGui::PushTextWrapPos(0.0f);
    draw_hint();
    ImGui::PopTextWrapPos();
    EndInputLegend();
    return;
  }
  // The drawing with the hint under it, and the list beside them. The drawing
  // is sized so that both columns stay as tall as the list.
  const float width = std::floor(ImGui::GetContentRegionAvail().x * 0.5f);
  ImGui::BeginGroup();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
  DrawControllerArt(tester, controller_art, width, scale);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
  draw_hint();
  ImGui::PopTextWrapPos();
  ImGui::PopStyleVar();
  ImGui::EndGroup();
  ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
  DrawInputLegend(tester, false, bold);
  EndInputLegend();
}
} // namespace

int main(int argc, char** argv)
{
  bool use_wayland = false;
  std::optional<fs::path> extract_only;
  for (int i = 1; i < argc; ++i)
  {
    if (std::string_view(argv[i]) == "-X11" || std::string_view(argv[i]) == "--x11")
      use_wayland = false;
    else if (std::string_view(argv[i]) == "--wayland")
      use_wayland = true;
    else if (std::string_view(argv[i]) == "--extract" && i + 1 < argc)
      extract_only = argv[++i];
  }

  const fs::path user_directory = DefaultUserDirectory();
  const fs::path release_directory = ExecutableDirectory(argv[0]);
  if (extract_only)
  {
    ExtractionState extraction;
    extraction.running = true;
    const bool success =
        ExtractDisc(*extract_only, user_directory, release_directory, &extraction);
    std::lock_guard lock(extraction.mutex);
    if (!success)
      std::cerr << "extraction failed: " << extraction.error << '\n';
    else
      std::cout << extraction.status << ": " << *extraction.finished_game << '\n';
    return success ? 0 : 1;
  }

  auto config = moderngekko::frontend::LoadConfig(user_directory, true);
  if (!config)
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "Invalid " MODERNGEKKO_FRONTEND_NAME " config.ini",
                             config.error.c_str(), nullptr);
    return 2;
  }

#if defined(__linux__)
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, use_wayland ? "wayland" : "x11");
#endif
  SDL_setenv_unsafe("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", 0);
  // The game process inherits this too, so it also notices a reconnected pad.
  // A value already set in the environment wins (0 = don't overwrite).
  if (RunningInContainer())
    SDL_setenv_unsafe("SDL_JOYSTICK_DISABLE_UDEV", "1", 0);
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    return 1;

  const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  SDL_Window* window = SDL_CreateWindow(MODERNGEKKO_FRONTEND_NAME, static_cast<int>(820 * scale),
                                        static_cast<int>(700 * scale),
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window)
  {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  // Keeps the sidebar and the page readable when the window is resized.
  SDL_SetWindowMinimumSize(window, static_cast<int>(680 * scale), static_cast<int>(560 * scale));
  SDL_Renderer* renderer = SDL_CreateRenderer(window, "vulkan");
  if (!renderer)
    renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer)
  {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ApplyLauncherTheme(ImGui::GetStyle());
  ImGui::GetStyle().ScaleAllSizes(scale);
  ImGui::GetStyle().FontScaleDpi = scale;
  // ImGui sizes a font by its full line height. 18 px gives Noto Sans and Segoe
  // UI a comfortable reading size while the Play page still fits in 700 px.
  const LauncherFonts fonts = LoadInterfaceFont(release_directory, 18.0f);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);
  const LauncherBanner banner = LoadLauncherBanner(renderer, release_directory);
  const LauncherImage controller_art = LoadLauncherImage(
      renderer, release_directory, "launcher-controller.png", "launcher controller art");
  const LauncherImage keyboard_art = LoadLauncherImage(
      renderer, release_directory, "launcher-keyboard.png", "launcher keyboard art");

  std::vector<fs::path> images = FindDiscImages();
  std::optional<fs::path> selected_image = images.empty() ? std::nullopt : std::optional(images[0]);
  fs::path current_game = ReadDefaultGame(user_directory, release_directory);
  bool current_dol_changed = false;
  std::string current_dol_error;
  auto current_metadata =
      ApplyBundledDolPatch(current_game, release_directory, &current_dol_changed,
                           &current_dol_error) ?
          InspectForLauncher(current_game) :
          moderngekko::GameInspectResult{{}, current_dol_error};
  if (current_metadata && !RequiredGameError(*current_metadata.metadata).empty())
    current_metadata = {{}, RequiredGameError(*current_metadata.metadata)};
  const auto& resolutions = moderngekko::frontend::SupportedResolutions();
  const auto& graphics_backends =
      moderngekko::frontend::SupportedGraphicsBackends();
  bool show_fps_in_title = config.show_fps_in_title;
  bool fullscreen = config.fullscreen;
  std::array<char, 31> netplay_nickname{};
  std::array<char, 256> netplay_address{};
  std::snprintf(netplay_nickname.data(), netplay_nickname.size(), "%s",
                config.netplay_nickname.c_str());
  std::snprintf(netplay_address.data(), netplay_address.size(), "%s",
                config.netplay_address.c_str());
  int netplay_port = config.netplay_port;
  bool automatic_buffer = config.netplay_buffer == "auto";
  int manual_buffer = automatic_buffer ? 5 : std::stoi(config.netplay_buffer);
  int resolution_index = 0;
  for (std::size_t i = 0; i < resolutions.size(); ++i)
  {
    if (config.resolution == resolutions[i].text)
      resolution_index = static_cast<int>(i);
  }
  int graphics_backend_index = 0;
  for (std::size_t i = 0; i < graphics_backends.size(); ++i)
  {
    if (config.graphics_backend == graphics_backends[i].value)
      graphics_backend_index = static_cast<int>(i);
  }
  const auto& cpu_cores = moderngekko::frontend::SupportedCpuCores();
  int cpu_core_index = 0;
  for (std::size_t i = 0; i < cpu_cores.size(); ++i)
  {
    if (config.cpu_core == cpu_cores[i].value)
      cpu_core_index = static_cast<int>(i);
  }
  const auto& aspect_ratios = moderngekko::frontend::SupportedAspectRatios();
  int aspect_ratio_index = 0;
  for (std::size_t i = 0; i < aspect_ratios.size(); ++i)
  {
    if (config.aspect_ratio == aspect_ratios[i].value)
      aspect_ratio_index = static_cast<int>(i);
  }
  // The runner gives MODERNGEKKO_STATICRECOMP priority over config.ini and
  // inherits this process' environment, so say so when it is set.
  const char* cpu_core_environment_value = std::getenv("MODERNGEKKO_STATICRECOMP");
  const std::string cpu_core_override =
      cpu_core_environment_value ? cpu_core_environment_value : "";

  // StateSaves is Dolphin's own state directory, so states written in-game land
  // here without any extra configuration and show up on the next launch.
  //
  // Listed once at startup rather than every frame, so the dropdown does not
  // hit the filesystem on each redraw. A state written while the launcher is
  // open therefore appears on the next launch, which is the case that matters:
  // states are written by the running game, not by the launcher.
  const std::vector<fs::path> launcher_savestates =
      moderngekko::frontend::ListLauncherSavestates(user_directory / "StateSaves");
  // Loading is always explicit. StateSaves is shared by games, so selecting the
  // newest file automatically could try to resume a different title.
  int selected_savestate = -1;

  DialogState dialog;
  std::vector<ControllerOption> controllers = EnumerateControllers();
  bool controller_profile_exists = moderngekko::frontend::ControllerConfigExists(user_directory);
  std::vector<std::string> configured_controllers =
      moderngekko::frontend::ReadConfiguredControllers(user_directory);
  std::string selected_controller =
      configured_controllers.empty() ? config.controller : configured_controllers.front();
  int controller_index = FindController(controllers, selected_controller);
  std::string controller_status;
  const fs::path controller_profile = user_directory / "Config" / CONTROLLER_PROFILE_NAME;
  ControllerTester controller_tester;
  const auto select_controller = [&](int index)
  {
    std::string message;
    if (!moderngekko::frontend::GenerateControllerConfig(user_directory, controllers[index].device,
                                                         &message))
    {
      std::lock_guard lock(dialog.mutex);
      dialog.error = std::move(message);
      return false;
    }
    std::string error;
    if (!moderngekko::frontend::SaveConfig(user_directory, resolutions[resolution_index].text,
                                           show_fps_in_title, controllers[index].device, &error))
    {
      std::lock_guard lock(dialog.mutex);
      dialog.error = std::move(error);
      return false;
    }
    selected_controller = controllers[index].device;
    configured_controllers = {selected_controller};
    config.controller = selected_controller;
    config.controllers = configured_controllers;
    config.resolution = resolutions[resolution_index].text;
    config.show_fps_in_title = show_fps_in_title;
    controller_profile_exists = true;
    controller_status = std::move(message);
    controller_tester.loaded = false;  // read the new profile on the next frame
    return true;
  };
  // Like select_controller, but keeps the profile's button mapping and only
  // changes which pad it reads (see RetargetControllerProfile).
  const auto retarget_controller = [&](int index)
  {
    std::string error;
    if (!RetargetControllerProfile(controller_profile, controllers[index].device, &error) ||
        !moderngekko::frontend::SaveConfig(user_directory, resolutions[resolution_index].text,
                                           show_fps_in_title, controllers[index].device, &error))
    {
      std::lock_guard lock(dialog.mutex);
      dialog.error = std::move(error);
      return false;
    }
    selected_controller = controllers[index].device;
    configured_controllers = {selected_controller};
    config.controller = selected_controller;
    config.controllers = configured_controllers;
    config.resolution = resolutions[resolution_index].text;
    config.show_fps_in_title = show_fps_in_title;
    controller_index = index;
    controller_status = std::string(CONTROLLER_PROFILE_NAME) + " now uses " +
                        controllers[index].label + " with the same buttons";
    std::cout << "controller: " << controller_status << '\n';
    controller_tester.loaded = false;
    return true;
  };
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  // Without a gamepad the profile is written for the keyboard alone. A gamepad
  // connected later takes the keyboard's place as the profile's device
  // (retarget_controller), and the keys keep working.
  const std::string keyboard_device(moderngekko::frontend::KeyboardDevice());
  const std::string keyboard_only_status =
      "Keyboard only. A controller you connect is added to the same profile.";
  const auto select_keyboard_only = [&]
  {
    std::string message;
    std::string error;
    if (!moderngekko::frontend::GenerateControllerConfig(user_directory, keyboard_device,
                                                         &message) ||
        !moderngekko::frontend::SaveConfig(user_directory, resolutions[resolution_index].text,
                                           show_fps_in_title, keyboard_device, &error))
    {
      std::lock_guard lock(dialog.mutex);
      dialog.error = error.empty() ? std::move(message) : std::move(error);
      return false;
    }
    selected_controller = keyboard_device;
    configured_controllers = {selected_controller};
    config.controller = selected_controller;
    config.controllers = configured_controllers;
    config.resolution = resolutions[resolution_index].text;
    config.show_fps_in_title = show_fps_in_title;
    controller_profile_exists = true;
    controller_index = -1;
    controller_status = keyboard_only_status;
    controller_tester.loaded = false;
    return true;
  };
#endif
  const auto ensure_controller = [&]
  {
    if (moderngekko::frontend::ControllerConfigExists(user_directory))
    {
      controller_profile_exists = true;
      configured_controllers = moderngekko::frontend::ReadConfiguredControllers(user_directory);
      if (configured_controllers.empty())
      {
        std::lock_guard lock(dialog.mutex);
        dialog.error = std::string(CONTROLLER_PROFILE_NAME) +
                       " has no configured controller device";
        return false;
      }
      // The profile's pad isn't connected but exactly one other pad is: most
      // likely the same pad under another name (USB / Bluetooth).
      if (FindController(controllers, configured_controllers.front()) < 0 && controllers.size() == 1)
        return retarget_controller(0);
      selected_controller = configured_controllers.front();
      controller_index = FindController(controllers, selected_controller);
      controller_status = "Using existing " + std::string(CONTROLLER_PROFILE_NAME);
      return true;
    }
    if (controller_index < 0)
    {
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
      return select_keyboard_only();
#else
      std::lock_guard lock(dialog.mutex);
      dialog.error = "Connect an SDL-compatible controller before playing";
      return false;
#endif
    }
    return select_controller(controller_index);
  };
  const auto refresh_controllers = [&]
  {
    controllers = EnumerateControllers();
    controller_index = FindController(controllers, selected_controller);
    if (!controller_profile_exists)
    {
      if (controller_index < 0 && !controllers.empty())
      {
        controller_index = 0;
        selected_controller = controllers.front().device;
      }
      if (controller_index >= 0)
        select_controller(controller_index);
      else
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
        select_keyboard_only();
#else
        controller_status = "No SDL gamepad detected";
#endif
    }
    else if (controller_index < 0 && controllers.size() == 1)
    {
      retarget_controller(0);  // same rule as ensure_controller
    }
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
    else if (selected_controller == keyboard_device)
    {
      controller_status = keyboard_only_status;
    }
#endif
    else
    {
      controller_status = "Using existing " + std::string(CONTROLLER_PROFILE_NAME);
    }
  };
  refresh_controllers();
  ExtractionState extraction;
  std::jthread extraction_thread;
  enum class LaunchMode
  {
    None,
    Solo,
    Host,
    Join,
  };
  bool done = false;
  LaunchMode launch_mode = LaunchMode::None;
  LauncherPage current_page = LauncherPage::Play;
  // Which session the Netplay window is about to start (Host or Join).
  LaunchMode netplay_dialog_mode = LaunchMode::Host;
  // PLAY page summary, read again whenever the page is opened: the time
  // played changes while the game runs and mods are added by hand.
  std::uint64_t summary_time_played = 0;
  int summary_installed_mods = 0;
  bool summary_stale = true;
  while (!done)
  {
    bool controllers_changed = false;
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        done = true;
      if (event.type == SDL_EVENT_JOYSTICK_ADDED || event.type == SDL_EVENT_JOYSTICK_REMOVED ||
          event.type == SDL_EVENT_GAMEPAD_REMAPPED)
      {
        controllers_changed = true;
      }
    }
    if (controllers_changed)
      refresh_controllers();
    UpdateControllerTester(controller_tester, current_page == LauncherPage::Controls,
                           controller_profile, controllers);
    // On the Controls page the keyboard is being tested: its keys must not
    // also move ImGui's focus or press the page's buttons.
    if (current_page == LauncherPage::Controls)
      io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    else
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    {
      std::lock_guard lock(dialog.mutex);
      if (dialog.selected)
      {
        selected_image = std::move(dialog.selected);
        dialog.selected.reset();
      }
    }
    {
      std::lock_guard lock(extraction.mutex);
      if (extraction.finished_game)
      {
        current_game = *extraction.finished_game;
        current_metadata = InspectForLauncher(current_game);
        if (current_metadata && !RequiredGameError(*current_metadata.metadata).empty())
          current_metadata = {{}, RequiredGameError(*current_metadata.metadata)};
        extraction.finished_game.reset();
      }
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    // An error stays on screen until the next thing the user does. Cleared
    // before the widgets run, so an action that fails again this frame shows
    // its fresh message instead of an outdated one.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Space))
    {
      std::lock_guard lock(dialog.mutex);
      dialog.error.clear();
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(MODERNGEKKO_FRONTEND_NAME " Launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    // Top bar: "Launcher" on the left (the game's name is already in the
    // window title and the banner), the CPU core PLAY will use on the right
    // (MODERNGEKKO_STATICRECOMP wins over config.ini, as in the runner).
    ImGui::PushFont(fonts.bold, 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Launcher");
    ImGui::PopFont();
    ImGui::SameLine();
    {
      const bool static_recomp = cpu_core_override.empty() ?
                                     config.cpu_core == "staticrecomp" :
                                     cpu_core_override.front() != '0';
      DrawStatusPill(static_recomp ? "StaticRecomp" : "JIT64",
                     static_recomp ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) : LAUNCHER_ACCENT, scale,
                     true);
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Body: sidebar on the left and the selected page on the right, leaving
    // room under both for the footer (error line and controller status).
    bool has_dialog_error = false;
    {
      std::lock_guard lock(dialog.mutex);
      has_dialog_error = !dialog.error.empty();
    }
    const float footer_height =
        ImGui::GetTextLineHeightWithSpacing() * (has_dialog_error ? 2.0f : 1.0f) +
        ImGui::GetStyle().ItemSpacing.y * 2.0f + 1.0f;
    const float body_height = std::max(1.0f, ImGui::GetContentRegionAvail().y - footer_height);
    // The two columns show the window background; only panels inside a page
    // (such as GAME STATUS) use the theme's panel colour.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##navigation", ImVec2(170.0f * scale, body_height));
    ImGui::PopStyleColor();
    if (DrawNavigationItem("PLAY", current_page == LauncherPage::Play, fonts.bold, scale))
    {
      current_page = LauncherPage::Play;
      summary_stale = true;
    }
    if (DrawNavigationItem("Settings", current_page == LauncherPage::Settings, fonts.bold, scale))
      current_page = LauncherPage::Settings;
    if (DrawNavigationItem("Controls", current_page == LauncherPage::Controls, fonts.bold, scale))
      current_page = LauncherPage::Controls;
    if (DrawNavigationItem("Game Files", current_page == LauncherPage::GameFiles, fonts.bold,
                           scale))
      current_page = LauncherPage::GameFiles;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##page", ImVec2(0.0f, body_height));
    ImGui::PopStyleColor();
    {
      if (current_page == LauncherPage::Play)
      {
        // Header: the project banner when the build bundled one. The banner
        // already shows the game's name, so no text title follows it.
        if (banner.texture)
        {
          DrawLauncherBanner(banner);
          ImGui::Spacing();
        }

        if (current_metadata)
        {
          const std::string ready =
              "GAME READY \xC2\xB7 " + current_metadata.metadata->disc_id;
          DrawStatusPill(ready.c_str(), LAUNCHER_ACCENT, scale, false);
          ImGui::Spacing();
          ImGui::Spacing();
          if (!launcher_savestates.empty())
          {
            ImGui::TextUnformatted("Start from savestate");
            const std::string savestate_preview =
                selected_savestate >= 0 ?
                    moderngekko::frontend::LauncherSavestateLabel(
                        launcher_savestates[static_cast<std::size_t>(selected_savestate)],
                        selected_savestate == 0) :
                    "Normal boot";
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##launch_savestate", savestate_preview.c_str()))
            {
              const bool normal_boot_selected = selected_savestate < 0;
              if (ImGui::Selectable("Normal boot", normal_boot_selected))
                selected_savestate = -1;
              if (normal_boot_selected)
                ImGui::SetItemDefaultFocus();

              for (std::size_t i = 0; i < launcher_savestates.size(); ++i)
              {
                const std::string label =
                    moderngekko::frontend::LauncherSavestateLabel(
                        launcher_savestates[i], i == 0);
                const bool selected = selected_savestate == static_cast<int>(i);
                if (ImGui::Selectable(label.c_str(), selected))
                  selected_savestate = static_cast<int>(i);
                if (selected)
                  ImGui::SetItemDefaultFocus();
              }
              ImGui::EndCombo();
            }
            // Netplay starts every player from the same boot, so a state chosen by
            // one side would desync the session immediately.
            ImGui::TextDisabled("Savestates are available for solo play only.");
            ImGui::Spacing();
          }

          if (DrawPrimaryButton("PLAY", fonts.bold, 52 * scale))
          {
            if (ensure_controller())
            {
              launch_mode = LaunchMode::Solo;
              done = true;
            }
          }

          // Netplay: two quiet buttons under PLAY. Each opens a small window
          // with the connection fields and the button that starts the session.
          ImGui::Spacing();
          if (DrawLinkButton("Host Netplay"))
          {
            netplay_dialog_mode = LaunchMode::Host;
            ImGui::OpenPopup("Netplay##session");
          }
          ImGui::SameLine(0.0f, 0.0f);
          ImGui::TextDisabled("\xC2\xB7");
          ImGui::SameLine(0.0f, 0.0f);
          if (DrawLinkButton("Join Netplay"))
          {
            netplay_dialog_mode = LaunchMode::Join;
            ImGui::OpenPopup("Netplay##session");
          }

          ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                  ImVec2(0.5f, 0.5f));
          ImGui::SetNextWindowSize(ImVec2(460 * scale, 0.0f));
          if (ImGui::BeginPopupModal("Netplay##session", nullptr, ImGuiWindowFlags_NoResize))
          {
            const bool hosting = netplay_dialog_mode == LaunchMode::Host;
            ImGui::PushFont(fonts.bold, 0.0f);
            ImGui::TextUnformatted(hosting ? "Host a session" : "Join a session");
            ImGui::PopFont();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled(hosting ?
                                    "Other players connect to this computer's address and "
                                    "UDP port." :
                                    "Enter the address and UDP port of the player who hosts.");
            ImGui::TextDisabled("Every player must use the same CPU core and game files.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            // A negative width of the widest label leaves room for all of them.
            const float netplay_label_width = ImGui::CalcTextSize("Buffer frames").x +
                                              ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetNextItemWidth(-netplay_label_width);
            ImGui::InputText("Nickname", netplay_nickname.data(), netplay_nickname.size());
            if (!hosting)
            {
              ImGui::SetNextItemWidth(-netplay_label_width);
              ImGui::InputText("Host / IP", netplay_address.data(), netplay_address.size());
            }
            ImGui::SetNextItemWidth(150 * scale);
            ImGui::InputInt("UDP port", &netplay_port);
            netplay_port = std::clamp(netplay_port, 1, 65535);
            ImGui::Checkbox("Automatic input buffer", &automatic_buffer);
            if (!automatic_buffer)
            {
              ImGui::SetNextItemWidth(-netplay_label_width);
              ImGui::SliderInt("Buffer frames", &manual_buffer, 1, 20);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            const float button_width =
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Button, LAUNCHER_ACCENT);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 1.0f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.90f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.04f, 0.05f, 0.02f, 1.0f));
            const bool start = ImGui::Button(hosting ? "Host" : "Join",
                                             ImVec2(button_width, 38 * scale));
            ImGui::PopStyleColor(4);
            if (start)
            {
              if (ensure_controller())
              {
                config.netplay_nickname = netplay_nickname.data();
                config.netplay_address = netplay_address.data();
                config.netplay_port = static_cast<std::uint16_t>(netplay_port);
                config.netplay_buffer = automatic_buffer ? "auto" : std::to_string(manual_buffer);
                config.controllers = configured_controllers.empty()
                                         ? std::vector<std::string>{selected_controller}
                                         : configured_controllers;
                config.controller = config.controllers.front();
                config.resolution = resolutions[resolution_index].text;
                config.show_fps_in_title = show_fps_in_title;
                std::string error;
                if (moderngekko::frontend::SaveConfig(user_directory, config, &error))
                {
                  launch_mode = netplay_dialog_mode;
                  done = true;
                }
                else
                {
                  std::lock_guard lock(dialog.mutex);
                  dialog.error = std::move(error);
                }
              }
              // Close either way: on success the launcher leaves to start the
              // session, on failure the footer shows the error.
              ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(button_width, 38 * scale)))
              ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
          }

          // Summary cards along the bottom of the page: how long the game has
          // been played and what the next PLAY will use.
          if (summary_stale)
          {
            summary_time_played = moderngekko::frontend::ReadTimePlayed(
                user_directory, current_metadata.metadata->disc_id);
            // The same two folders the runner loads mods from.
            summary_installed_mods = moderngekko::frontend::CountInstalledMods(
                {release_directory / "Mods", user_directory / "Mods"});
            summary_stale = false;
          }
          PlaySummary summary;
          summary.time_played_ms = summary_time_played;
          if (controller_index >= 0)
            summary.controller = controllers[controller_index].label;
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
          summary.keyboard_only =
              controller_index < 0 && selected_controller == keyboard_device;
          summary.keys_without_pad = true;
#endif
          summary.aspect_ratio = config.aspect_ratio;
          summary.fullscreen = fullscreen;
          summary.installed_mods = summary_installed_mods;
          summary.mods_folder = moderngekko::frontend::PathText(user_directory / "Mods");
          const float tiles_top = ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y -
                                  SummaryTileHeight(scale);
          ImGui::SetCursorPosY(std::max(
              ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y * 2.0f, tiles_top));
          switch (DrawSummaryRow(BuildPlaySummaryTiles(summary), fonts.bold, scale))
          {
          case 1:
            current_page = LauncherPage::Controls;
            break;
          case 2:
            current_page = LauncherPage::Settings;
            break;
          default:
            break;
          }
        }
        else
        {
          DrawStatusPill("GAME NOT INSTALLED", ImVec4(1.0f, 0.75f, 0.3f, 1.0f), scale, false);
          ImGui::Spacing();
          ImGui::TextUnformatted("No extracted game is configured yet.");
          ImGui::TextDisabled("Open Game Files to select and extract your game.");
          ImGui::Spacing();
          if (ImGui::Button("Go to Game Files"))
            current_page = LauncherPage::GameFiles;
        }
      }
      if (current_page == LauncherPage::Settings)
      {
    DrawPageHeader("Settings",
                   "Changes are saved right away and apply the next time you press PLAY.",
                   fonts.bold);
    BeginSection("##settings_cpu", "CPU CORE");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##cpu_core", cpu_cores[cpu_core_index].text))
    {
      for (std::size_t i = 0; i < cpu_cores.size(); ++i)
      {
        const bool selected = cpu_core_index == static_cast<int>(i);
        if (ImGui::Selectable(cpu_cores[i].text, selected))
        {
          const std::string previous = config.cpu_core;
          config.cpu_core = cpu_cores[i].value;
          config.resolution = resolutions[resolution_index].text;
          config.show_fps_in_title = show_fps_in_title;
          config.controller = selected_controller;
          std::string error;
          if (moderngekko::frontend::SaveConfig(user_directory, config, &error))
          {
            cpu_core_index = static_cast<int>(i);
          }
          else
          {
            config.cpu_core = previous;
            std::lock_guard lock(dialog.mutex);
            dialog.error = std::move(error);
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("JIT64 is the validated full-speed mode. StaticRecomp runs the "
                        "recompiled game code and is still being optimized.");
    ImGui::TextDisabled("Netplay: every player must use the same CPU core.");
    if (!cpu_core_override.empty())
    {
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                         "MODERNGEKKO_STATICRECOMP=%s is set in the environment and "
                         "overrides this setting.",
                         cpu_core_override.c_str());
    }
    ImGui::PopTextWrapPos();
    EndSection();
    BeginSection("##settings_graphics", "GRAPHICS");
    ImGui::TextUnformatted("Graphics backend");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##graphics_backend",
                          graphics_backends[graphics_backend_index].text))
    {
      for (std::size_t i = 0; i < graphics_backends.size(); ++i)
      {
        const bool selected = graphics_backend_index == static_cast<int>(i);
        if (ImGui::Selectable(graphics_backends[i].text, selected))
        {
          const std::string previous = config.graphics_backend;
          config.graphics_backend = graphics_backends[i].value;
          config.resolution = resolutions[resolution_index].text;
          config.show_fps_in_title = show_fps_in_title;
          config.controller = selected_controller;
          std::string error;
          if (moderngekko::frontend::SaveConfig(user_directory, config, &error))
          {
            graphics_backend_index = static_cast<int>(i);
          }
          else
          {
            config.graphics_backend = previous;
            std::lock_guard lock(dialog.mutex);
            dialog.error = std::move(error);
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Internal resolution (Dolphin EFB upscale)");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##resolution", resolutions[resolution_index].text))
    {
      for (std::size_t i = 0; i < resolutions.size(); ++i)
      {
        const bool selected = resolution_index == static_cast<int>(i);
        if (ImGui::Selectable(resolutions[i].text, selected))
        {
          std::string error;
          if (moderngekko::frontend::SaveConfig(user_directory, resolutions[i].text,
                                                show_fps_in_title, selected_controller, &error))
          {
            resolution_index = static_cast<int>(i);
            config.resolution = resolutions[i].text;
          }
          else
          {
            std::lock_guard lock(dialog.mutex);
            dialog.error = std::move(error);
          }
        }
      }
      ImGui::EndCombo();
    }
    EndSection();
    BeginSection("##settings_display", "DISPLAY");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Aspect ratio");
    ImGui::SameLine(0.0f, 16.0f * scale);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##aspect_ratio", aspect_ratios[aspect_ratio_index].text))
    {
      for (std::size_t i = 0; i < aspect_ratios.size(); ++i)
      {
        const bool selected = aspect_ratio_index == static_cast<int>(i);
        if (ImGui::Selectable(aspect_ratios[i].text, selected))
        {
          const std::string previous = config.aspect_ratio;
          config.aspect_ratio = aspect_ratios[i].value;
          config.resolution = resolutions[resolution_index].text;
          config.show_fps_in_title = show_fps_in_title;
          config.controller = selected_controller;
          std::string error;
          if (moderngekko::frontend::SaveConfig(user_directory, config, &error))
          {
            aspect_ratio_index = static_cast<int>(i);
          }
          else
          {
            config.aspect_ratio = previous;
            std::lock_guard lock(dialog.mutex);
            dialog.error = std::move(error);
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
        // Each choice explains itself in the list, so the page stays short
        // enough for the default window.
        ImGui::Indent(12.0f * scale);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", aspect_ratios[i].description);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(12.0f * scale);
      }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Netplay: the host's 16:9 or 4:3 choice applies to every player.");
    ImGui::Spacing();
    const bool previous_fullscreen = fullscreen;
    if (ImGui::Checkbox("Start in fullscreen", &fullscreen))
    {
      config.fullscreen = fullscreen;
      config.resolution = resolutions[resolution_index].text;
      config.show_fps_in_title = show_fps_in_title;
      config.controller = selected_controller;
      std::string error;
      if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
      {
        fullscreen = previous_fullscreen;
        config.fullscreen = previous_fullscreen;
        std::lock_guard lock(dialog.mutex);
        dialog.error = std::move(error);
      }
    }
    ImGui::SameLine(0.0f, 40.0f * scale);
    const bool previous_show_fps_in_title = show_fps_in_title;
    if (ImGui::Checkbox("Show FPS in window title", &show_fps_in_title))
    {
      std::string error;
      if (!moderngekko::frontend::SaveConfig(user_directory, resolutions[resolution_index].text,
                                             show_fps_in_title, selected_controller, &error))
      {
        show_fps_in_title = previous_show_fps_in_title;
        std::lock_guard lock(dialog.mutex);
        dialog.error = std::move(error);
      }
      else
      {
        config.show_fps_in_title = show_fps_in_title;
      }
    }
    EndSection();
    }
      if (current_page == LauncherPage::Controls)
      {
    DrawPageHeader("Controls",
                   "Choose the controller to play with. The launcher writes its button "
                   "mapping for you.",
                   fonts.bold);
    BeginSection("##controls_controller", "CONTROLLER");
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
    const bool keyboard_only = controller_index < 0 && selected_controller == keyboard_device;
#else
    const bool keyboard_only = false;
#endif
    if (controller_index >= 0)
      DrawStatusPill("CONNECTED", LAUNCHER_ACCENT, scale, false);
    else if (keyboard_only)
      DrawStatusPill("KEYBOARD ONLY", LAUNCHER_ACCENT, scale, false);
    else
      DrawStatusPill("NOT DETECTED", ImVec4(1.0f, 0.75f, 0.3f, 1.0f), scale, false);
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    const char* controller_preview = controller_index >= 0
                                         ? controllers[controller_index].label.c_str()
                                     : keyboard_only ? "Keyboard only"
                                     : selected_controller.empty() ? "No SDL gamepad detected"
                                                                   : selected_controller.c_str();
    if (ImGui::BeginCombo("##controller", controller_preview))
    {
      for (std::size_t i = 0; i < controllers.size(); ++i)
      {
        const bool selected = controller_index == static_cast<int>(i);
        if (ImGui::Selectable(controllers[i].label.c_str(), selected))
        {
          if (controller_profile_exists)
          {
            // Keep the profile's buttons and point them at the chosen pad.
            if (!selected)
              retarget_controller(static_cast<int>(i));
          }
          else
          {
            controller_index = static_cast<int>(i);
            selected_controller = controllers[i].device;
            controller_status = "Ready to generate controller profile";
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", controller_status.c_str());
    ImGui::PopTextWrapPos();
    EndSection();
    BeginSection("##controls_mapping", "BUTTON MAPPING");
    ImGui::PushTextWrapPos(0.0f);
    if (controller_profile_exists)
      ImGui::TextDisabled("Using %s from your user folder.", std::string(CONTROLLER_PROFILE_NAME).c_str());
    else
      ImGui::TextDisabled("%s is created the first time you play.", std::string(CONTROLLER_PROFILE_NAME).c_str());
    ImGui::PopTextWrapPos();
    // The tester starts on the line above, where it puts its tabs.
    if (controller_profile_exists)
      DrawControllerTester(controller_tester, controller_art, keyboard_art, fonts.bold, scale);
    ImGui::Spacing();
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
    // Without a gamepad this writes the keyboard-only profile.
    ImGui::BeginDisabled(false);
#else
    ImGui::BeginDisabled(controller_index < 0);
#endif
    const std::string replace_profile =
        "Replace with generated " + std::string(GENERATED_PROFILE_LABEL);
    if (ImGui::Button(replace_profile.c_str()))
    {
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
      if (controller_index < 0)
        select_keyboard_only();
      else
#endif
        select_controller(controller_index);
    }
    ImGui::EndDisabled();
    EndSection();
    }
      if (current_page == LauncherPage::GameFiles)
      {
    DrawPageHeader("Game Files",
                   "Use your own copy of the game as an ISO, WBFS or RVZ image. It is "
                   "extracted once and reused afterwards.",
                   fonts.bold);
    BeginSection("##files_installed", "INSTALLED GAME");
    if (current_metadata)
    {
      const std::string ready =
          "GAME READY \xC2\xB7 " + current_metadata.metadata->disc_id;
      DrawStatusPill(ready.c_str(), LAUNCHER_ACCENT, scale, false);
      ImGui::Spacing();
      ImGui::TextUnformatted(current_metadata.metadata->game_name.c_str());
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", current_game.string().c_str());
      ImGui::PopTextWrapPos();
    }
    else
    {
      DrawStatusPill("NOT INSTALLED", ImVec4(1.0f, 0.75f, 0.3f, 1.0f), scale, false);
      ImGui::Spacing();
      ImGui::TextDisabled("Select a disc image below and extract it.");
    }
    EndSection();
    BeginSection("##files_image", "DISC IMAGE");
    if (selected_image)
      ImGui::TextWrapped("%s", selected_image->string().c_str());
    else
      ImGui::TextDisabled("No ISO, WBFS, or RVZ selected");
    ImGui::Spacing();
    if (!extraction.running)
    {
      if (ImGui::Button("Browse for ISO / WBFS / RVZ"))
      {
        static constexpr SDL_DialogFileFilter filters[] = {
            {"Disc images", "iso;wbfs;rvz"},
            {"ISO", "iso"},
            {"WBFS", "wbfs"},
            {"RVZ", "rvz"}};
        const std::string documents = DocumentsDirectory().string();
        SDL_ShowOpenFileDialog(FileDialogCallback, &dialog, window, filters,
                               static_cast<int>(std::size(filters)), documents.c_str(), false);
      }
      if (selected_image)
      {
        ImGui::SameLine();
        if (ImGui::Button("Extract Game"))
        {
          extraction.completed = 0;
          extraction.total = 1;
          extraction.running = true;
          {
            std::lock_guard lock(extraction.mutex);
            extraction.error.clear();
            extraction.finished_game.reset();
          }
          const fs::path image = *selected_image;
          extraction_thread = std::jthread([image, user_directory, release_directory, &extraction]
                                           {
                                             ExtractDisc(image, user_directory,
                                                         release_directory, &extraction);
                                           });
        }
      }
    }
    else
    {
      const float progress =
          std::min(1.0f, static_cast<float>(extraction.completed.load()) / extraction.total.load());
      ImGui::ProgressBar(progress, ImVec2(-1, 0));
    }
    {
      std::lock_guard lock(extraction.mutex);
      if (!extraction.status.empty())
        ImGui::TextWrapped("%s", extraction.status.c_str());
      if (!extraction.error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", extraction.error.c_str());
    }
    EndSection();
    }
    }
    ImGui::EndChild();
    {
      std::lock_guard lock(dialog.mutex);
      if (!dialog.error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", dialog.error.c_str());
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Controller: %s", controller_status.c_str());
    ImGui::End();

    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 8, 8, 9, 255);  // ImGuiCol_WindowBg of the theme
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  if (extraction_thread.joinable())
    extraction_thread.join();

  int result = 0;
  if (launch_mode != LaunchMode::None)
  {
    std::string launch_error;
    if (!WriteDefaultGame(user_directory, release_directory, current_game,
                          &launch_error))
    {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Launch failed", launch_error.c_str(), window);
      result = 1;
    }
    else
    {
      std::vector<std::string> argument_storage = {SiblingRunner(argv[0]).string(), "--game",
                                                   current_game.string(), "--user-dir",
                                                   user_directory.string()};
      // Solo only: see the note by the dropdown.
      if (launch_mode == LaunchMode::Solo && selected_savestate >= 0 &&
          static_cast<std::size_t>(selected_savestate) < launcher_savestates.size())
      {
        argument_storage.emplace_back("--load-state");
        argument_storage.emplace_back(
            moderngekko::frontend::PathText(
                launcher_savestates[static_cast<std::size_t>(selected_savestate)]));
      }
      if (launch_mode == LaunchMode::Host)
        argument_storage.emplace_back("--netplay-host");
      else if (launch_mode == LaunchMode::Join)
      {
        argument_storage.emplace_back("--netplay-join");
        argument_storage.emplace_back(config.netplay_address);
      }
      if (launch_mode == LaunchMode::Host || launch_mode == LaunchMode::Join)
      {
        argument_storage.emplace_back("--netplay-port");
        argument_storage.emplace_back(std::to_string(config.netplay_port));
        argument_storage.emplace_back("--nickname");
        argument_storage.emplace_back(config.netplay_nickname);
        argument_storage.emplace_back("--buffer");
        argument_storage.emplace_back(config.netplay_buffer);
        for (const std::string& controller : config.controllers)
        {
          argument_storage.emplace_back("--controller");
          argument_storage.emplace_back(controller);
        }
      }
      if (use_wayland)
        argument_storage.emplace_back("--wayland");
#if defined(__linux__)
      else
        argument_storage.emplace_back("-X11");
#endif
      std::vector<const char*> arguments;
      arguments.reserve(argument_storage.size() + 1);
      for (const std::string& argument : argument_storage)
        arguments.push_back(argument.c_str());
      arguments.push_back(nullptr);
      const std::filesystem::path log_path =
          user_directory / "Logs" / MODERNGEKKO_LOG_FILENAME;
      std::error_code log_error;
      std::filesystem::create_directories(log_path.parent_path(), log_error);
      SDL_IOStream* log_stream =
          log_error ? nullptr : SDL_IOFromFile(log_path.string().c_str(), "w");
      SDL_Process* process = nullptr;
      std::string process_error;
      if (log_stream)
      {
        const SDL_PropertiesID properties = SDL_CreateProperties();
        if (properties)
        {
          SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                                 arguments.data());
          SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                SDL_PROCESS_STDIO_REDIRECT);
          SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log_stream);
          SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
                                 true);
          process = SDL_CreateProcessWithProperties(properties);
          SDL_DestroyProperties(properties);
        }
        if (!process)
          process_error = SDL_GetError();
        SDL_CloseIO(log_stream);
      }
      else
      {
        process = SDL_CreateProcess(arguments.data(), false);
        if (!process)
          process_error = SDL_GetError();
      }
      if (!process)
      {
        launch_error = "Could not start " + SiblingRunner(argv[0]).string() + ": " + process_error;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Launch failed", launch_error.c_str(),
                                 window);
        result = 1;
      }
      else
      {
        SDL_HideWindow(window);
        int exit_code = 1;
        const bool waited = SDL_WaitProcess(process, true, &exit_code);
        SDL_DestroyProcess(process);
        if (!waited || exit_code != 0)
        {
          SDL_ShowWindow(window);
          if (!waited)
          {
            launch_error =
                "The game process could not be monitored: " + std::string(SDL_GetError());
          }
          else if (launch_mode == LaunchMode::Join)
          {
            switch (static_cast<moderngekko::frontend::NetplayExitCode>(exit_code))
            {
            case moderngekko::frontend::NetplayExitCode::VersionMismatch:
              launch_error = "The host is running an incompatible netplay build. Both "
                             "players must use the same release.";
              break;
            case moderngekko::frontend::NetplayExitCode::CompatibilityMismatch:
              launch_error = "The extracted game or recomp module does not match the "
                             "host. Both players need the same game revision and "
                             "release.";
              break;
            case moderngekko::frontend::NetplayExitCode::RoomFull:
              launch_error = "All four controller slots are already occupied.";
              break;
            case moderngekko::frontend::NetplayExitCode::GameRunning:
              launch_error = "The host has already started the game.";
              break;
            case moderngekko::frontend::NetplayExitCode::ServerFull:
              launch_error = "The netplay server is full.";
              break;
            case moderngekko::frontend::NetplayExitCode::NicknameRejected:
              launch_error = "The nickname was rejected by the host.";
              break;
            default:
              launch_error = "Could not reach the netplay host. Check the host name, UDP "
                             "port, and firewall.";
              break;
            }
          }
          else if (launch_mode == LaunchMode::Host)
          {
            launch_error = "Could not create the netplay session. Check the UDP port "
                           "and firewall settings.";
          }
          else
          {
            launch_error = "The game process exited with code " + std::to_string(exit_code) + ".";
          }
          if (!log_error)
            launch_error += "\n\nDetails were written to:\n" + log_path.string();
          SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Session failed", launch_error.c_str(),
                                   window);
          result = 1;
        }
      }
    }
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  if (controller_tester.gamepad)
    SDL_CloseGamepad(controller_tester.gamepad);
  if (controller_art.texture)
    SDL_DestroyTexture(controller_art.texture);
  if (keyboard_art.texture)
    SDL_DestroyTexture(keyboard_art.texture);
  if (banner.texture)
    SDL_DestroyTexture(banner.texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}
