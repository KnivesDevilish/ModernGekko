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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
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
constexpr std::string_view GENERATED_PROFILE_LABEL = "GameCube profile";
#else
constexpr std::string_view CONTROLLER_PROFILE_NAME = "WiimoteNew.ini";
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

// Optional header image for the Play page. The build copies it next to the
// launcher when MODERNGEKKO_LAUNCHER_BANNER is set; a missing or unreadable
// file just means no image, and the page shows its text header alone.
struct LauncherBanner
{
  SDL_Texture* texture = nullptr;
  float aspect = 0.0f;  // height / width
};

LauncherBanner LoadLauncherBanner(SDL_Renderer* renderer, const fs::path& release_directory)
{
  LauncherBanner banner;
  const fs::path path = release_directory / "Resources" / "launcher-banner.png";
  std::error_code ec;
  if (!fs::is_regular_file(path, ec))
    return banner;
  SDL_Surface* surface = SDL_LoadPNG(path.string().c_str());
  if (!surface)
  {
    std::cout << "launcher banner: can't load " << path.string() << ": " << SDL_GetError()
              << '\n';
    return banner;
  }
  if (surface->w > 0 && surface->h > 0)
  {
    banner.texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (banner.texture)
    {
      banner.aspect = static_cast<float>(surface->h) / static_cast<float>(surface->w);
      std::cout << "launcher banner: " << path.string() << '\n';
    }
  }
  SDL_DestroySurface(surface);
  return banner;
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
    // Top bar: frontend name on the left, the CPU core PLAY will use on the
    // right (MODERNGEKKO_STATICRECOMP wins over config.ini, as in the runner).
    ImGui::PushFont(fonts.bold, 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(MODERNGEKKO_FRONTEND_NAME);
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
        // Header: the project banner when the build bundled one, then the title.
        if (banner.texture)
        {
          DrawLauncherBanner(banner);
          ImGui::Spacing();
        }
        ImGui::PushFont(fonts.bold, ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::TextColored(LAUNCHER_ACCENT, "SONIC RIDERS: ZERO GRAVITY");
        ImGui::PopFont();

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
          controller_index = static_cast<int>(i);
          selected_controller = controllers[i].device;
          controller_status =
              controller_profile_exists
                  ? "Existing " + std::string(CONTROLLER_PROFILE_NAME) + " unchanged"
                  : "Ready to generate controller profile";
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
  if (banner.texture)
    SDL_DestroyTexture(banner.texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}
