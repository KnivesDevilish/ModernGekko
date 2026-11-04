#include "frontend_config.hpp"
#include "dol_patch.hpp"
#include "launcher_savestates.hpp"
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

#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
// ---- Controller tester (Controls page) -------------------------------------
// Reads [Wiimote1] from WiimoteNew.ini and lights up the Wii Remote buttons
// that the pad's current input presses. It only reads: neither the profile
// nor the pad settings are changed here.

// Names that Dolphin's SDL backend gives to SDL3 gamepad buttons, in
// SDL_GamepadButton order (InputCommon/ControllerInterface/SDL/SDLGamepad.h).
constexpr std::array<std::string_view, 21> DOLPHIN_SDL_BUTTON_NAMES = {
    "Button S", "Button E",   "Button W",   "Button N", "Back",     "Guide",    "Start",
    "Thumb L",  "Thumb R",    "Shoulder L", "Shoulder R", "Pad N",  "Pad S",    "Pad W",
    "Pad E",    "Misc 1",     "Paddle 1",   "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad"};

// One physical input: a gamepad button, or one direction of an axis.
struct PadSource
{
  int button = -1;    // SDL_GamepadButton, or -1 for an axis
  int axis = -1;      // SDL_GamepadAxis
  int direction = 1;  // +1 or -1: the side of the axis that presses
};

std::optional<PadSource> ParseDolphinPadInput(std::string_view name)
{
  for (std::size_t i = 0; i < DOLPHIN_SDL_BUTTON_NAMES.size(); ++i)
  {
    if (name == DOLPHIN_SDL_BUTTON_NAMES[i])
      return PadSource{.button = static_cast<int>(i)};
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
      return PadSource{.button = static_cast<int>(button)};
  }
  if (name == "Trigger L")
    return PadSource{.axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER};
  if (name == "Trigger R")
    return PadSource{.axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
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
    return PadSource{.axis = static_cast<int>(i), .direction = direction};
  }
  return std::nullopt;
}

// The tester understands the bindings the launcher writes: input names joined
// by "|", such as "`Pad N` | `Left Y+`". Anything else (Dolphin's "&", "!",
// functions...) is reported as custom and not tested.
struct WiiBinding
{
  std::vector<PadSource> sources;
  bool custom = false;
};

void ParseBindingExpression(std::string_view expression, WiiBinding& binding)
{
  std::string term;
  bool quoted = false;
  const auto finish_term = [&]
  {
    std::string_view name = TrimView(term);
    // A device-qualified name, such as `SDL/0/Pad:Button S`, keeps its input part.
    if (const std::size_t colon = name.rfind(':'); colon != std::string_view::npos)
      name = TrimView(name.substr(colon + 1));
    // Inputs that are not pad buttons or axes (motors, IR, motion) are skipped.
    if (const std::optional<PadSource> source = ParseDolphinPadInput(name))
      binding.sources.push_back(*source);
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

// The Wii Remote inputs the tester shows, with the profile keys behind them.
enum WiiInput : std::size_t
{
  WII_A,
  WII_B,
  WII_1,
  WII_2,
  WII_MINUS,
  WII_PLUS,
  WII_HOME,
  WII_UP,
  WII_DOWN,
  WII_LEFT,
  WII_RIGHT,
  WII_SHAKE,
  WII_INPUT_COUNT,
};

constexpr std::array<std::array<std::string_view, 3>, WII_INPUT_COUNT> WII_PROFILE_KEYS = {{
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

struct ControllerTester
{
  bool loaded = false;  // WiimoteNew.ini read since the Controls page opened
  std::string device;   // [Wiimote1] Device: the pad Dolphin will use
  std::array<WiiBinding, WII_INPUT_COUNT> bindings;
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
  bool in_first_remote = false;
  while (std::getline(input, line))
  {
    const std::string_view trimmed = TrimView(line);
    if (trimmed.starts_with('['))
    {
      in_first_remote = trimmed == "[Wiimote1]";
      continue;
    }
    const std::size_t separator = trimmed.find('=');
    if (!in_first_remote || separator == std::string_view::npos)
      continue;
    values[std::string(TrimView(trimmed.substr(0, separator)))] =
        std::string(TrimView(trimmed.substr(separator + 1)));
  }
  if (const auto device = values.find("Device"); device != values.end())
    tester.device = device->second;
  for (std::size_t input_index = 0; input_index < WII_INPUT_COUNT; ++input_index)
  {
    for (const std::string_view key : WII_PROFILE_KEYS[input_index])
    {
      if (key.empty())
        continue;
      if (const auto found = values.find(std::string(key)); found != values.end())
        ParseBindingExpression(found->second, tester.bindings[input_index]);
    }
  }
  tester.loaded = true;
}

// Reads the profile when the Controls page opens and keeps the profile's pad
// open only while the page is shown.
void UpdateControllerTester(ControllerTester& tester, bool page_visible, const fs::path& profile,
                            const std::vector<ControllerOption>& controllers)
{
  if (!page_visible)
    tester.loaded = false;
  else if (!tester.loaded)
    LoadTesterProfile(tester, profile);
  SDL_JoystickID wanted = 0;
  if (page_visible)
  {
    if (const int index = FindController(controllers, tester.device); index >= 0)
      wanted = controllers[static_cast<std::size_t>(index)].joystick_id;
  }
  if (wanted == tester.open_id)
    return;
  if (tester.gamepad)
    SDL_CloseGamepad(tester.gamepad);
  tester.gamepad = wanted != 0 ? SDL_OpenGamepad(wanted) : nullptr;
  tester.open_id = wanted;
}

bool IsPadSourceDown(SDL_Gamepad* gamepad, const PadSource& source)
{
  if (source.button >= 0)
    return SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(source.button));
  const float value =
      static_cast<float>(SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(source.axis))) /
      32767.0f;
  return value * static_cast<float>(source.direction) > 0.5f;
}

bool IsWiiInputDown(const ControllerTester& tester, std::size_t input)
{
  if (!tester.gamepad)
    return false;
  return std::ranges::any_of(tester.bindings[input].sources, [&](const PadSource& source)
                             { return IsPadSourceDown(tester.gamepad, source); });
}

// A short, readable name for a pad input. The face buttons use the label SDL
// knows for this pad (A, Cross...), or their position when it knows none.
std::string PadSourceName(SDL_Gamepad* gamepad, const PadSource& source)
{
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

// The D-pad row names whole controls ("D-pad, Left stick") instead of listing
// the twelve directions.
std::string PadSourceGroup(SDL_Gamepad* gamepad, const PadSource& source)
{
  if (source.button >= SDL_GAMEPAD_BUTTON_DPAD_UP && source.button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
    return "D-pad";
  if (source.button < 0 && source.axis <= SDL_GAMEPAD_AXIS_LEFTY)
    return "Left stick";
  if (source.button < 0 && source.axis <= SDL_GAMEPAD_AXIS_RIGHTY)
    return "Right stick";
  return PadSourceName(gamepad, source);
}

// Button positions in the SR-ZGS controller artwork, in pixels of its
// 1024 x 1024 canvas. They belong to that drawing: other artwork bundled with
// MODERNGEKKO_LAUNCHER_CONTROLLER_ART needs its own positions here.
constexpr float CONTROLLER_ART_CANVAS = 1024.0f;
constexpr ImVec2 CONTROLLER_ART_MIN(40.0f, 258.0f);   // the drawing's bounds; the rest
constexpr ImVec2 CONTROLLER_ART_MAX(984.0f, 854.0f);  // of the canvas is transparent

struct ArtHotspot
{
  WiiInput input;
  int point_count;  // 1: circle at points[0], 2: rectangle, 3+: convex polygon
  std::array<ImVec2, 6> points;
  float radius;
};

constexpr std::array<ArtHotspot, WII_INPUT_COUNT> CONTROLLER_ART_HOTSPOTS = {{
    // Wii A and B on the shoulder buttons, as the generated profile maps them.
    {WII_A, 6, {{{189, 337}, {209, 314}, {244, 295}, {286, 284}, {336, 283}, {370, 312}}}, 0},
    {WII_B, 6, {{{654, 312}, {720, 282}, {749, 286}, {785, 297}, {827, 325}, {834, 337}}}, 0},
    {WII_1, 1, {{{599, 515}}}, 42},  // X (west)
    {WII_2, 1, {{{646, 605}}}, 42},  // A (south)
    {WII_MINUS, 1, {{{429, 405}}}, 22},
    {WII_PLUS, 1, {{{595, 405}}}, 22},
    {WII_HOME, 3, {{{512, 566}, {554, 642}, {470, 642}}}, 0},  // centre triangle
    {WII_UP, 2, {{{256, 430}, {298, 498}}}, 0},
    {WII_DOWN, 2, {{{256, 532}, {298, 600}}}, 0},
    {WII_LEFT, 2, {{{193, 494}, {261, 536}}}, 0},
    {WII_RIGHT, 2, {{{293, 494}, {361, 536}}}, 0},
    {WII_SHAKE, 1, {{{786, 427}}}, 42},  // Z
}};

void DrawArtHotspot(ImDrawList* draw_list, const ArtHotspot& spot, ImVec2 origin, float zoom,
                    float scale)
{
  const auto to_screen = [&](ImVec2 point)
  {
    return ImVec2(origin.x + (point.x - CONTROLLER_ART_MIN.x) * zoom,
                  origin.y + (point.y - CONTROLLER_ART_MIN.y) * zoom);
  };
  const ImU32 fill =
      ImGui::GetColorU32(ImVec4(LAUNCHER_ACCENT.x, LAUNCHER_ACCENT.y, LAUNCHER_ACCENT.z, 0.45f));
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

// The artwork (when the build bundled it) with the pressed buttons lit, and
// beside it the list of Wii Remote buttons and the pad inputs behind each one.
void DrawControllerTester(const ControllerTester& tester, const LauncherImage& art, ImFont* bold,
                          float scale)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  if (art.texture)
  {
    const float width = std::floor(ImGui::GetContentRegionAvail().x * 0.56f);
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
      if (IsWiiInputDown(tester, spot.input))
        DrawArtHotspot(draw_list, spot, origin, zoom, scale);
    }
    ImGui::Dummy(size);
    ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
  }

  struct LegendRow
  {
    const char* label;
    WiiInput first;
    WiiInput last;  // the D-Pad row covers four inputs
  };
  constexpr std::array<LegendRow, 9> rows = {{
      {"A", WII_A, WII_A},
      {"B", WII_B, WII_B},
      {"1", WII_1, WII_1},
      {"2", WII_2, WII_2},
      {"\xE2\x88\x92", WII_MINUS, WII_MINUS},
      {"+", WII_PLUS, WII_PLUS},
      {"HOME", WII_HOME, WII_HOME},
      {"D-Pad", WII_UP, WII_RIGHT},
      {"Shake", WII_SHAKE, WII_SHAKE},
  }};
  ImGui::BeginGroup();
  ImGui::PushFont(bold, 0.0f);
  float label_width = 0.0f;
  for (const LegendRow& row : rows)
    label_width = std::max(label_width, ImGui::CalcTextSize(row.label).x);
  ImGui::PopFont();
  label_width = std::max(label_width, ImGui::CalcTextSize("Wii Remote").x);
  label_width += style.ItemSpacing.x * 2.0f;
  // Rows sit close together so the list is about as tall as the artwork.
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
  // Inside a group, SameLine() offsets count from the group's left edge.
  ImGui::TextDisabled("Wii Remote");
  ImGui::SameLine(label_width);
  ImGui::TextDisabled("Your controller");
  for (const LegendRow& row : rows)
  {
    bool down = false;
    bool custom = false;
    std::vector<std::string> names;
    for (std::size_t input = row.first; input <= row.last; ++input)
    {
      down = down || IsWiiInputDown(tester, input);
      custom = custom || tester.bindings[input].custom;
      for (const PadSource& source : tester.bindings[input].sources)
      {
        std::string name = row.first == row.last ? PadSourceName(tester.gamepad, source) :
                                                   PadSourceGroup(tester.gamepad, source);
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
  // Last line: how to use the tester, or why it can't light anything up.
  ImGui::PushTextWrapPos(0.0f);
  if (tester.gamepad)
  {
    ImGui::TextColored(LAUNCHER_ACCENT, "Press buttons to test them.");
  }
  else if (tester.device.empty())
  {
    ImGui::TextDisabled("No controller in WiimoteNew.ini.");
  }
  else
  {
    // "SDL/0/PS3 Controller" -> "PS3 Controller"
    const std::size_t name_start = tester.device.find('/', tester.device.find('/') + 1);
    const std::string name =
        name_start == std::string::npos ? tester.device : tester.device.substr(name_start + 1);
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Connect %s to test it.", name.c_str());
  }
  ImGui::PopTextWrapPos();
  ImGui::PopStyleVar();
  ImGui::EndGroup();
}
#endif
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
#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
  const LauncherImage controller_art = LoadLauncherImage(
      renderer, release_directory, "launcher-controller.png", "launcher controller art");
#endif

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
#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
  ControllerTester controller_tester;
#endif
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
#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
    controller_tester.loaded = false;  // read the new profile on the next frame
#endif
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
#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
    controller_tester.loaded = false;
#endif
    return true;
  };
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
      std::lock_guard lock(dialog.mutex);
      dialog.error = "Connect an SDL-compatible controller before playing";
      return false;
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
        controller_status = "No SDL gamepad detected";
    }
    else if (controller_index < 0 && controllers.size() == 1)
    {
      retarget_controller(0);  // same rule as ensure_controller
    }
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
#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
    UpdateControllerTester(controller_tester, current_page == LauncherPage::Controls,
                           controller_profile, controllers);
#endif

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
      current_page = LauncherPage::Play;
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
    if (controller_index >= 0)
      DrawStatusPill("CONNECTED", LAUNCHER_ACCENT, scale, false);
    else
      DrawStatusPill("NOT DETECTED", ImVec4(1.0f, 0.75f, 0.3f, 1.0f), scale, false);
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    const char* controller_preview = controller_index >= 0
                                         ? controllers[controller_index].label.c_str()
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
    ImGui::Spacing();
#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
    if (controller_profile_exists)
    {
      DrawControllerTester(controller_tester, controller_art, fonts.bold, scale);
      ImGui::Spacing();
    }
#endif
    ImGui::BeginDisabled(controller_index < 0);
    const std::string replace_profile =
        "Replace with generated " + std::string(GENERATED_PROFILE_LABEL);
    if (ImGui::Button(replace_profile.c_str()))
      select_controller(controller_index);
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
#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
  if (controller_tester.gamepad)
    SDL_CloseGamepad(controller_tester.gamepad);
  if (controller_art.texture)
    SDL_DestroyTexture(controller_art.texture);
#endif
  if (banner.texture)
    SDL_DestroyTexture(banner.texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}
