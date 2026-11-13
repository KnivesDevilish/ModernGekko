#include "frontend_config.hpp"
#include "dol_patch.hpp"
#include "game_borders.hpp"
#include "launcher_savestates.hpp"
#include "launcher_summary.hpp"
#include "launcher_text.hpp"
#include "button_glyphs.hpp"
#include "launcher_music.hpp"
#include "launcher_patch_notes.hpp"
#include "launcher_gallery.hpp"
#include "launcher_game_select.hpp"
#include "launcher_banner.hpp"
#include "launcher_games.hpp"
#include "launcher_line_break.hpp"
#include "launcher_loading.hpp"
#include "launcher_transition.hpp"
#include "launcher_user_folder.hpp"
#include "launcher_save.hpp"
#include "launcher_ui.hpp"
#include "launcher_update.hpp"
#include "moderngekko/game.hpp"
#include "netplay_address.hpp"
#include "netplay_session.hpp"
#include "runner_exit_code.hpp"
#include "windows_firewall.hpp"

#include "DiscIO/DiscExtractor.h"
#include "DiscIO/Filesystem.h"
#include "DiscIO/Volume.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
// FocusItem() and the keys' held time, for moving through the launcher with a
// gamepad.
#include <imgui_internal.h>
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
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
// Interface texts are written in English and drawn through Tr().
using moderngekko::frontend::Format;
using moderngekko::frontend::Tr;

namespace
{
#ifndef MODERNGEKKO_FRONTEND_NAME
#define MODERNGEKKO_FRONTEND_NAME "ModernGekko"
#endif

#ifndef MODERNGEKKO_RUNNER_FILENAME
#define MODERNGEKKO_RUNNER_FILENAME "moderngekko-run"
#endif

// The launcher's own name (MODERNGEKKO_LAUNCHER_TITLE): its window's title
// and the top bar's heading. Without one, the window takes the frontend's
// name and the top bar says "Launcher".
#ifdef MODERNGEKKO_LAUNCHER_TITLE
#define MODERNGEKKO_LAUNCHER_HEADING MODERNGEKKO_LAUNCHER_TITLE
#else
#define MODERNGEKKO_LAUNCHER_TITLE MODERNGEKKO_FRONTEND_NAME
#define MODERNGEKKO_LAUNCHER_HEADING "Launcher"
#endif
// The user folder's earlier names (launcher_user_folder.hpp), as name|name.
#ifndef MODERNGEKKO_PREVIOUS_USER_DIRECTORY_NAMES
#define MODERNGEKKO_PREVIOUS_USER_DIRECTORY_NAMES ""
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
constexpr const char* REPLACE_PROFILE_BUTTON =
    MODERNGEKKO_UI_TEXT("Replace with generated GameCube profile");
constexpr const char* CONTROLLER_MAPPED_STATUS = MODERNGEKKO_UI_TEXT("1 GameCube controller mapped");
#else
constexpr std::string_view CONTROLLER_PROFILE_NAME = "WiimoteNew.ini";
constexpr std::string_view CONTROLLER_PROFILE_SECTION = "[Wiimote1]";
constexpr const char* REPLACE_PROFILE_BUTTON =
    MODERNGEKKO_UI_TEXT("Replace with generated sideways profile");
constexpr const char* CONTROLLER_MAPPED_STATUS = MODERNGEKKO_UI_TEXT("1 sideways Wii Remote mapped");
#endif

// A status line kept as its English text and values, so that it is drawn in
// the language chosen at that moment (the Controls page and the footer).
struct StatusText
{
  const char* format = "";
  std::string first;
  std::string second;

  std::string Text() const { return Format(Tr(format), first.c_str(), second.c_str()); }
  std::string English() const { return Format(format, first.c_str(), second.c_str()); }
};

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
  // One of the build's games (tools/launcher_games.hpp), or any without them.
  if (!moderngekko::frontend::AcceptsGame(metadata.disc_id))
    return Format(Tr("expected disc ID %s, found %s"),
                  moderngekko::frontend::KnownGameIds().c_str(), metadata.disc_id.c_str());
#ifdef MODERNGEKKO_REQUIRED_DOL_SHA256
  if (metadata.dol_sha256 != MODERNGEKKO_REQUIRED_DOL_SHA256)
    return Tr("main DOL does not match the pinned release");
#endif
#ifdef MODERNGEKKO_REQUIRED_REL_SHA256
  if (metadata.rel_sha256 != MODERNGEKKO_REQUIRED_REL_SHA256)
    return Tr("_Main.rel does not match the pinned release");
#endif
#ifdef MODERNGEKKO_REQUIRED_ASSETS_SHA256
  if (metadata.assets_sha256 != MODERNGEKKO_REQUIRED_ASSETS_SHA256)
    return Tr("game assets do not match the pinned release");
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
    *error = Tr("Image rejected: the selected file is not a valid disc image.");
    return std::nullopt;
  }
#ifndef MODERNGEKKO_REQUIRED_DISC_ID
  // A build that requires no disc (one for any GameCube or Wii game, such as
  // a test build): the disc is extracted as it is.
  return image;
#else
  const std::string source_id = source->GetGameID(source->GetGamePartition());
  if (moderngekko::frontend::AcceptsGame(source_id))
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
    state->status = Tr("Applying Tournament Edition 2.4.6.1");
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
    *error = Format(Tr("can't start Tournament Edition patcher: %s"), SDL_GetError());
    return std::nullopt;
  }
  int exit_code = 1;
  const bool waited = SDL_WaitProcess(process, true, &exit_code);
  SDL_DestroyProcess(process);
  if (!waited || exit_code != 0)
  {
    *error = Tr("Tournament Edition patching failed");
    return std::nullopt;
  }
  return output;
#elif defined(MODERNGEKKO_REQUIRED_DISC_ID)
  *error = Tr("this disc is not the pinned patched release");
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
    state->status = Format(Tr("Opening %s"), image.filename().string().c_str());
  }
  std::string preparation_error;
  const std::optional<fs::path> prepared =
      PrepareDisc(image, user_directory, release_directory, state, &preparation_error);
  if (!prepared)
    return fail(std::move(preparation_error));
  std::unique_ptr<DiscIO::Volume> volume = DiscIO::CreateVolume(prepared->string());
  if (!volume)
    return fail(Tr("Image rejected: the prepared disc image could not be opened."));

  const DiscIO::Partition partition = volume->GetGamePartition();
  const DiscIO::FileSystem* filesystem = volume->GetFileSystem(partition);
  if (!filesystem || !filesystem->IsValid())
    return fail(Tr("The game's files could not be read from the disc image."));

  std::string disc_id = volume->GetGameID(partition);
  if (disc_id.size() != 6)
    return fail(Tr("the selected image has an invalid disc ID"));
  if (!moderngekko::frontend::AcceptsGame(disc_id))
    return fail(Format(Tr("this frontend requires disc ID %s; selected %s"),
                       moderngekko::frontend::KnownGameIds().c_str(), disc_id.c_str()));
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
      return fail(Format(Tr("existing extraction patching failed: %s"), patch_error.c_str()));
  }
  const auto existing = InspectForLauncher(output);
  if (existing && RequiredGameError(*existing.metadata).empty())
  {
    std::string error;
    if (!WriteDefaultGame(user_directory, release_directory, output, &error))
      return fail(error);
    std::lock_guard lock(state->mutex);
    state->finished_game = output;
    state->status = Tr("Using existing extraction");
    state->running = false;
    return true;
  }

  const fs::path staging = games_directory / (disc_id + ".extracting");
  fs::remove_all(staging, ec);
  fs::create_directories(staging / "files", ec);
  if (ec)
    return fail(Format(Tr("can't create extraction directory: %s"), ec.message().c_str()));

  {
    std::lock_guard lock(state->mutex);
    state->status = Tr("Extracting system data");
  }
  if (!DiscIO::ExportSystemData(*volume, partition, staging.string()))
  {
    fs::remove_all(staging, ec);
    return fail(Tr("The disc's system data could not be extracted."));
  }

  state->total = std::max(1u, filesystem->GetRoot().GetTotalChildren());
  {
    std::lock_guard lock(state->mutex);
    state->status = Tr("Extracting game files");
  }
  DiscIO::ExportDirectory(*volume, partition, filesystem->GetRoot(), true, "",
                          (staging / "files").string(),
                          [state](const std::string& path)
                          {
                            ++state->completed;
                            std::lock_guard lock(state->mutex);
                            state->status = Format(Tr("Extracting %s"), path.c_str());
                            return false;
                          });

  bool dol_changed = false;
  std::string dol_patch_error;
  if (!ApplyBundledDolPatch(staging, release_directory, &dol_changed, &dol_patch_error))
  {
    fs::remove_all(staging, ec);
    return fail(Format(Tr("extracted DOL patching failed: %s"), dol_patch_error.c_str()));
  }

  const auto inspected = InspectForLauncher(staging);
  if (!inspected)
  {
    fs::remove_all(staging, ec);
    return fail(Format(Tr("extracted game validation failed: %s"), inspected.error.c_str()));
  }
  if (const std::string identity_error = RequiredGameError(*inspected.metadata);
      !identity_error.empty())
  {
    fs::remove_all(staging, ec);
    return fail(Format(Tr("extracted game validation failed: %s"), identity_error.c_str()));
  }

  fs::remove_all(output, ec);
  ec.clear();
  fs::rename(staging, output, ec);
  if (ec)
    return fail(Format(Tr("can't publish extracted game: %s"), ec.message().c_str()));
  std::string error;
  if (!WriteDefaultGame(user_directory, release_directory, output, &error))
    return fail(error);

  {
    std::lock_guard lock(state->mutex);
    state->finished_game = output;
    state->status = Tr("Extraction complete");
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

// The launcher's window as its window system names it, for the runner to
// open the game's window centred on it (--launcher-window): the X11 window ID
// or the Windows HWND. 0 elsewhere (Wayland, where windows can't place
// themselves).
std::uint64_t NativeWindowId(SDL_Window* window)
{
  const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
#if defined(_WIN32)
  return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
      SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)));
#else
  const Sint64 x11_window = SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  return x11_window > 0 ? static_cast<std::uint64_t>(x11_window) : 0;
#endif
}

// The look shared with the Netplay lobby (theme, fonts, status pills, panels,
// buttons, images) lives in launcher_ui.hpp.
using namespace moderngekko::frontend::ui;

// Launcher-only drawing helpers.

// A Netplay dialog row that can hold an address or invite code: [field] [Show]
// Label. The field shows asterisks until Show is clicked, for players who
// stream. label_width is the dialog's label column, so the label lines up
// with the other rows. Returns true when an edit of the field has finished.
bool DrawHiddenField(const char* id, char* buffer, std::size_t size, const char* label,
                     float label_width, bool* shown)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  const float show_width =
      std::max(ImGui::CalcTextSize(Tr("Show")).x, ImGui::CalcTextSize(Tr("Hide")).x) +
      style.FramePadding.x * 2.0f;
  ImGui::SetNextItemWidth(-(label_width + show_width + style.ItemInnerSpacing.x));
  ImGui::InputText(id, buffer, size,
                   *shown ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_Password);
  const bool edited = ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
  if (ImGui::Button((std::string(*shown ? Tr("Hide") : Tr("Show")) + id + "_show").c_str(),
                    ImVec2(show_width, 0.0f)))
    *shown = !*shown;
  ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
  ImGui::TextUnformatted(label);
  return edited;
}

enum class LauncherPage
{
  Play,
  Settings,
  Controls,
  GameFiles,
  Mods,
  Screenshots,
  Credits,
};

// The sidebar's pages in its order, which LB and RB also go through.
constexpr std::array<std::pair<LauncherPage, const char*>, 7> LAUNCHER_PAGES = {{
    {LauncherPage::Play, MODERNGEKKO_UI_TEXT("PLAY")},
    {LauncherPage::Settings, MODERNGEKKO_UI_TEXT("Settings")},
    {LauncherPage::Controls, MODERNGEKKO_UI_TEXT("Controls")},
    {LauncherPage::GameFiles, MODERNGEKKO_UI_TEXT("Game Files")},
    {LauncherPage::Mods, MODERNGEKKO_UI_TEXT("Mods & expansions")},
    {LauncherPage::Screenshots, MODERNGEKKO_UI_TEXT("Screenshots")},
    {LauncherPage::Credits, MODERNGEKKO_UI_TEXT("Credits")},
}};

// How a gamepad labels its buttons, for the footer's hints.
PadStyle PadStyleOf(SDL_GamepadType type)
{
  switch (type)
  {
  case SDL_GAMEPAD_TYPE_PS3:
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5:
    return PadStyle::PlayStation;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    return PadStyle::Nintendo;
  default:
    return PadStyle::Xbox;
  }
}

// A keyboard key in a footer hint: by its name ("Esc", "Q"), or an arrow.
struct FooterKey
{
  std::string name;
  int arrow = 0;  // -1 left, 1 right: drawn instead of a name
};

// The picture of a gamepad of this kind.
DeviceIcon DeviceIconOf(SDL_GamepadType type)
{
  switch (type)
  {
  case SDL_GAMEPAD_TYPE_XBOX360:
  case SDL_GAMEPAD_TYPE_XBOXONE:
    return DeviceIcon::Xbox;
  case SDL_GAMEPAD_TYPE_PS3:
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5:
    return DeviceIcon::PlayStation;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    return DeviceIcon::Nintendo;
  default:
    return DeviceIcon::Gamepad;
  }
}

// One hint of the footer: a button (or two) and what it does, and the key
// (or keys) that do it on the keyboard.
struct FooterHint
{
  PadButton button;
  std::optional<PadButton> second;
  const char* text;
  std::vector<FooterKey> keys;
};

// The footer's hints, right-aligned on the line at line_min (screen
// coordinates) whose right end is `right`: the gamepad's buttons, or with
// keyboard true the keys (a hint without keys is left out then). Returns how
// wide they are; with draw false, only measures them.
float DrawFooterHints(std::span<const FooterHint> all_hints, PadStyle style, bool keyboard,
                      ImFont* bold, ImVec2 line_min, float right, bool draw)
{
  std::vector<FooterHint> hints;
  for (const FooterHint& hint : all_hints)
  {
    if (!keyboard || !hint.keys.empty())
      hints.push_back(hint);
  }
  ImFont* font = ImGui::GetFont();
  const float font_size = ImGui::GetFontSize();
  const float glyph = std::floor(ImGui::GetTextLineHeight() * 0.95f);
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const auto key_width = [&](const FooterKey& key) {
    return key.arrow != 0 ? glyph : KeyCapWidth(bold, glyph, key.name);
  };
  const auto glyphs_width = [&](const FooterHint& hint) {
    float width = 0.0f;
    if (keyboard)
    {
      for (std::size_t k = 0; k < hint.keys.size(); ++k)
        width += (k > 0 ? 4.0f : 0.0f) + key_width(hint.keys[k]);
      return width;
    }
    width = PadButtonWidth(bold, glyph, style, hint.button);
    if (hint.second)
      width += 4.0f + PadButtonWidth(bold, glyph, style, *hint.second);
    return width;
  };
  float width = 0.0f;
  for (std::size_t i = 0; i < hints.size(); ++i)
  {
    width += glyphs_width(hints[i]);
    width += gap * 0.6f + ImGui::CalcTextSize(hints[i].text).x;
    if (i + 1 < hints.size())
      width += gap * 2.0f;
  }
  if (!draw)
    return width;
  ImDrawList* list = ImGui::GetWindowDrawList();
  float x = right - width;
  const float glyph_y = line_min.y + std::floor((ImGui::GetTextLineHeight() - glyph) * 0.5f);
  for (std::size_t i = 0; i < hints.size(); ++i)
  {
    if (keyboard)
    {
      for (std::size_t k = 0; k < hints[i].keys.size(); ++k)
      {
        const FooterKey& key = hints[i].keys[k];
        if (k > 0)
          x += 4.0f;
        x += key.arrow != 0 ? DrawArrowKeyCap(list, ImVec2(x, glyph_y), glyph, key.arrow < 0)
                            : DrawKeyCap(list, bold, ImVec2(x, glyph_y), glyph, key.name);
      }
    }
    else
    {
      x += DrawPadButton(list, bold, ImVec2(x, glyph_y), glyph, style, hints[i].button);
      if (hints[i].second)
        x += 4.0f + DrawPadButton(list, bold, ImVec2(x + 4.0f, glyph_y), glyph, style,
                                  *hints[i].second);
    }
    x += gap * 0.6f;
    list->AddText(font, font_size, ImVec2(x, line_min.y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  hints[i].text);
    x += ImGui::CalcTextSize(hints[i].text).x + gap * 2.0f;
  }
  return width;
}

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

using LauncherBanner = LauncherImage;

// largest_width: the most pixels wide it is drawn (LoadLauncherImage).
LauncherBanner LoadLauncherBanner(SDL_Renderer* renderer, const fs::path& release_directory,
                                  float largest_width)
{
  return LoadLauncherImage(renderer, release_directory, "launcher-banner.png", "launcher banner",
                           false, largest_width);
}

// Draws the banner at the cursor, `width` wide, with the panels' rounding.
void DrawLauncherBanner(const LauncherBanner& banner, float width)
{
  const ImVec2 size(width, width * banner.aspect);
  const ImVec2 position = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddImageRounded(
      ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(banner.texture))),
      position, ImVec2(position.x + size.x, position.y + size.y), ImVec2(0.0f, 0.0f),
      ImVec2(1.0f, 1.0f), IM_COL32_WHITE, ImGui::GetStyle().ChildRounding);
  ImGui::Dummy(size);
}

// The system's first preferred language, such as "es" from es_VE.UTF-8, or
// empty when SDL can't tell.
std::string SystemLanguageCode()
{
  int count = 0;
  SDL_Locale** locales = SDL_GetPreferredLocales(&count);
  std::string language =
      locales && count > 0 && locales[0]->language ? locales[0]->language : "";
  SDL_free(locales);
  return language;
}

// The language menu at the bottom of the sidebar. setting is config.ini's
// "language" value; returns true when the player picks another one.
bool DrawLanguageMenu(std::string& setting, std::string_view system_language, float scale)
{
  using moderngekko::frontend::Language;
  using moderngekko::frontend::LanguageName;
  const float height = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeight();
  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - height));
  ImGui::Indent(12.0f * scale);
  // White, so it reads well over the background.
  ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", Tr("LANGUAGE"));
  const std::string automatic = Format(
      Tr("System (%s)"),
      LanguageName(moderngekko::frontend::ResolveLanguage("auto", system_language)));
  const std::array<std::pair<const char*, std::string>, 4> choices = {{
      {"auto", automatic},
      {"en", LanguageName(Language::English)},
      {"es", LanguageName(Language::Spanish)},
      {"ja", LanguageName(Language::Japanese)},
  }};
  const auto current = std::ranges::find_if(
      choices, [&](const auto& choice) { return setting == choice.first; });
  bool changed = false;
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##language",
                        current != choices.end() ? current->second.c_str() : automatic.c_str()))
  {
    for (const auto& [value, label] : choices)
    {
      const bool selected = setting == value;
      if (ImGui::Selectable(label.c_str(), selected) && !selected)
      {
        setting = value;
        changed = true;
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::Unindent(12.0f * scale);
  return changed;
}

// The PLAY page's name for a savestate. The newest one is marked, as
// LauncherSavestateLabel() does, in the interface's language.
std::string SavestateLabel(const fs::path& path, bool latest)
{
  const std::string filename = moderngekko::frontend::PathText(path.filename());
  return latest ? Format(Tr("Latest - %s"), filename.c_str()) : filename;
}

// Without the CMake option (CMakeLists.txt always passes it), savestates stay.
#ifndef MODERNGEKKO_SAVESTATES
#define MODERNGEKKO_SAVESTATES 1
#endif

// The savestates the PLAY page offers to start from. A build without
// savestates (MODERNGEKKO_SAVESTATES OFF) offers none, so the choice never
// shows: its game window has no keys to make them.
std::vector<fs::path> PlaySavestates(const fs::path& user_directory)
{
#if MODERNGEKKO_SAVESTATES
  return moderngekko::frontend::ListLauncherSavestates(user_directory / "StateSaves");
#else
  (void)user_directory;
  return {};
#endif
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
  // A picture before the value: what the player plays with.
  std::optional<DeviceIcon> icon;
};

// What the PLAY page shows below the button, built from plain values so the
// page code only gathers them.
struct PlaySummary
{
  std::uint64_t time_played_ms = 0;
  // The connected gamepad's name, or empty when the profile's pad is missing.
  std::string controller;
  // Its picture (a gamepad of its kind), or the keyboard's.
  std::optional<DeviceIcon> controller_icon;
  bool keyboard_only = false;
  // GameCube mode: the first controller also answers to the keyboard, so the
  // game is playable while the pad is unplugged.
  bool keys_without_pad = false;
  std::string_view aspect_ratio;  // "16:9", "4:3" or "custom"
  std::string_view custom_aspect; // the shape of "custom", such as "21:9"
  bool fullscreen = false;
  int installed_mods = 0;
  std::string mods_folder;
};

std::array<SummaryTile, 4> BuildPlaySummaryTiles(const PlaySummary& summary)
{
  std::array<SummaryTile, 4> tiles;
  tiles[0] = {"##summary_time", Tr("TIME PLAYED"),
              Tr(moderngekko::frontend::FormatTimePlayed(summary.time_played_ms)), false, {},
              false};

  SummaryTile& controls = tiles[1];
  controls = {"##summary_controls", Tr("CONTROLS"), {}, false, Tr("Open Controls"), true};
  controls.icon = summary.controller_icon;
  if (!summary.controller.empty())
    controls.value = summary.controller;
  else if (summary.keyboard_only)
    controls.value = Tr("Keyboard only");
  else if (summary.keys_without_pad)
  {
    controls.value = Tr("Keyboard");
    controls.tooltip = Tr("Your controller isn't connected; the keyboard still works.\n"
                          "Open Controls");
  }
  else
  {
    controls.value = Tr("Not detected");
    controls.warning = true;
  }

  const std::string aspect = summary.aspect_ratio == "custom" ? std::string(summary.custom_aspect)
                             : summary.aspect_ratio.empty()   ? std::string("16:9")
                                                              : std::string(summary.aspect_ratio);
  tiles[2] = {"##summary_display", Tr("DISPLAY"),
              summary.fullscreen ? Format(Tr("%s \xC2\xB7 Fullscreen"), aspect.c_str())
                                 : Format(Tr("%s \xC2\xB7 Window"), aspect.c_str()),
              false, Tr("Open Settings"), true,
              summary.fullscreen ? Format(Tr("%s \xC2\xB7 Full"), aspect.c_str())
                                 : Format(Tr("%s \xC2\xB7 Win"), aspect.c_str())};

  tiles[3] = {"##summary_mods", Tr("MODS"),
              summary.installed_mods == 0   ? std::string(Tr("None"))
              : summary.installed_mods == 1 ? std::string(Tr("1 installed"))
                                            : Format(Tr("%d installed"), summary.installed_mods),
              false,
              Format(Tr("Mods go in %s"), summary.mods_folder.c_str()) + "\n" +
                  Tr("Open Mods & expansions"),
              true};
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
  const ImVec2 padding(10.0f * scale, 9.0f * scale);
  const ImVec2 size(width, SummaryTileHeight(scale));
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 max(min.x + size.x, min.y + size.y);
  // A gamepad or the keyboard can reach the cards that open a page.
  const bool pressed = ImGui::InvisibleButton(
      tile.id, size, tile.clickable ? ImGuiButtonFlags_EnableNav : ImGuiButtonFlags_None);
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
  // The device's picture before the value, as tall as the value's letters.
  float icon_width = 0.0f;
  if (tile.icon)
  {
    const float icon_height = std::floor(value_size * 0.95f);
    icon_width = DrawDeviceIcon(draw_list,
                                ImVec2(min.x + padding.x,
                                       min.y + padding.y + heading_size + 3.0f * scale +
                                           (value_size - icon_height) * 0.5f),
                                icon_height, *tile.icon) +
                 5.0f * scale;
  }
  const float text_width = size.x - padding.x * 2.0f - icon_width;
  // The value at full size, or a little smaller, then its short form the
  // same way, and only then cut with "...": the compact window's cards stay
  // whole for nearly every value.
  std::string shown;
  float shown_size = value_size;
  const auto fit = [&](const std::string& text)
  {
    if (text.empty())
      return false;
    for (const float factor : {1.0f, 0.9f, 0.8f, 0.75f})
    {
      if (bold->CalcTextSizeA(value_size * factor, FLT_MAX, 0.0f, text.c_str()).x <= text_width)
      {
        shown = text;
        shown_size = value_size * factor;
        return true;
      }
    }
    return false;
  };
  if (!fit(tile.value) && !fit(tile.short_value))
  {
    shown_size = value_size * 0.9f;
    shown = FitTextToWidth(bold, shown_size,
                           tile.short_value.empty() ? tile.value : tile.short_value, text_width);
  }
  const ImVec4 value_color =
      tile.warning ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) : style.Colors[ImGuiCol_Text];
  // A smaller value sits a little lower, on about the same baseline.
  draw_list->AddText(bold, shown_size,
                     ImVec2(min.x + padding.x + icon_width,
                            min.y + padding.y + heading_size + 3.0f * scale +
                                (value_size - shown_size) * 0.7f),
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

// A web address drawn as a link. A click opens it in the browser and also
// copies it, since a container such as Distrobox may have no browser to open.
void DrawWebLink(const char* label, const char* url)
{
  static double copied_at = -10.0;
  static const char* copied_url = nullptr;
  ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  if (!ImGui::IsItemHovered())
    return;
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max,
                                      ImGui::GetColorU32(LAUNCHER_ACCENT));
  ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
  {
    SDL_OpenURL(url);
    ImGui::SetClipboardText(url);
    copied_at = ImGui::GetTime();
    copied_url = url;
  }
  const bool just_copied = copied_url == url && ImGui::GetTime() - copied_at < 2.5;
  ImGui::SetTooltip("%s", just_copied ? Tr("Opened in your browser and copied to the clipboard")
                                      : url);
}

// One "name: detail" list of the Credits page, the names in bold.
void DrawCreditsRows(const char* id, std::span<const std::array<const char*, 2>> rows,
                     ImFont* bold)
{
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None))
    return;
  ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("detail", ImGuiTableColumnFlags_WidthStretch);
  for (const auto& [name, detail] : rows)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushFont(bold, 0.0f);
    ImGui::TextUnformatted(Tr(name));
    ImGui::PopFont();
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", Tr(detail));
    ImGui::PopTextWrapPos();
  }
  ImGui::EndTable();
}

// Who made Sonic Riders: Source and what it is built on, credited as each
// project credits itself (the READMEs of SR-Source and ModernGekko): the author and the version
// side by side, then the projects and their licenses. version_section, when
// set, draws the VERSION panel beside CREATED BY (DrawVersionSection).
void DrawCreditsPage(ImFont* bold, const std::function<void()>& version_section = {})
{
  DrawPageHeader(Tr("Credits"),
                 Tr("Sonic Riders: Source is an independent fan project. It is not affiliated "
                    "with or endorsed by SEGA."),
                 bold);
  const bool top_columns = version_section && BeginPageColumns("##credits_top");
  BeginSection("##credits_author", Tr("CREATED BY"));
  // Naizu Akumateki, written in Japanese.
  ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.35f);
  ImGui::TextUnformatted("\xE5\xA5\x88\xE4\xBE\x9D\xE8\x8C\xB2\xE3\x83\xBB\xE6\x82\xAA\xE9\xAD\x94"
                         "\xE7\x9A\x84");
  ImGui::PopFont();
  ImGui::TextDisabled("JP: Naizu Akumateki \xC2\xB7 EN: KnivesDevilish");
  DrawWebLink("github.com/KnivesDevilish/SR-Source",
              "https://github.com/KnivesDevilish/SR-Source");
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", Tr("With the contributors and testers of Sonic Riders: Source, "
                               "for research, development and testing."));
  ImGui::PopTextWrapPos();
  EndSection();
  if (top_columns)
    NextPageColumn();
  if (version_section)
    version_section();
  if (top_columns)
    EndPageColumns();

  static constexpr std::array<std::array<const char*, 2>, 6> BUILT_ON = {{
      {"DolRecomp", MODERNGEKKO_UI_TEXT("Static recompilation framework")},
      {"ModernGekko", MODERNGEKKO_UI_TEXT("Runtime for GameCube/Wii recomps, by Hyperway")},
      {"RecompCore", "SpecialK / aharonahdoot"},
      {MODERNGEKKO_UI_TEXT("Recomp template"),
       MODERNGEKKO_UI_TEXT("Literally God / MrPoloGit, who also added macOS support")},
      {"Dolphin", MODERNGEKKO_UI_TEXT("The Dolphin Team and its contributors")},
      // Netplay's invite codes come from Dolphin's traversal server.
      {MODERNGEKKO_UI_TEXT("Invite codes"),
       MODERNGEKKO_UI_TEXT("Dolphin's public server (stun.dolphin-emu.org)")},
  }};
  // The projects get the wider column: their names come with a line each.
  const bool bottom_columns = BeginPageColumns("##credits_bottom", 0.6f);
  BeginSection("##credits_built_on", Tr("BUILT ON"));
  DrawCreditsRows("##credits_built_on_rows", BUILT_ON, bold);
  EndSection();

  static constexpr std::array<std::array<const char*, 2>, 6> LICENSES = {{
      {"Sonic Riders: Source", "GNU GPL v3"},
      {"ModernGekko", "GNU GPL v3"},
      {"Dolphin", MODERNGEKKO_UI_TEXT("GNU GPL v2 or later")},
      {"SDL 3", MODERNGEKKO_UI_TEXT("zlib License")},
      {"Dear ImGui, pl_mpeg", MODERNGEKKO_UI_TEXT("MIT License")},
      {"Noto Sans CJK, Teko", "SIL Open Font License 1.1"},
  }};
  if (bottom_columns)
    NextPageColumn();
  BeginSection("##credits_licenses", Tr("LICENSES"));
  DrawCreditsRows("##credits_license_rows", LICENSES, bold);
  EndSection();
  if (bottom_columns)
    EndPageColumns();

  ImGui::PushTextWrapPos(0.0f);
  if (moderngekko::frontend::PlaysSeveralGames())
    ImGui::TextDisabled("%s", Tr("Sonic Riders, Sonic Riders: Zero Gravity and their characters "
                                 "are trademarks of SEGA. Play with your own legally obtained "
                                 "copies of the games."));
  else
    ImGui::TextDisabled("%s", Tr("Sonic Riders: Zero Gravity and its characters are trademarks "
                                 "of SEGA. Play with your own legally obtained copy of the game."));
  ImGui::PopTextWrapPos();
}

#if defined(MODERNGEKKO_FRONTEND_VERSION) && defined(MODERNGEKKO_UPDATE_REPOSITORY)
// GitHub's page with the newest release. Opened in the browser and also
// copied, like DrawWebLink's links, since a container such as Distrobox may
// have no browser to open.
const char* DownloadPage()
{
  static const std::string page = moderngekko::frontend::ReleasesPage(MODERNGEKKO_UPDATE_REPOSITORY);
  return page.c_str();
}

// The top bar's notice of a newer version, next to the launcher's name: a lime pill
// that opens the download page.
void DrawNewVersionNotice(const std::string& version, float scale)
{
  static double opened_at = -10.0;
  const std::string text = Format(Tr("NEW VERSION \xC2\xB7 %s"), version.c_str());
  const ImVec2 padding(10.0f * scale, 3.0f * scale);
  const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
  const ImVec2 pill(text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
  // As high as the line, so the CPU core's pill after it stays level.
  const ImVec2 position = ImGui::GetCursorScreenPos();
  const float line_height = std::max(ImGui::GetFrameHeight(), pill.y);
  if (ImGui::InvisibleButton("##new_version", ImVec2(pill.x, line_height),
                             ImGuiButtonFlags_EnableNav))
  {
    SDL_OpenURL(DownloadPage());
    ImGui::SetClipboardText(DownloadPage());
    opened_at = ImGui::GetTime();
  }
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 top_left(position.x, position.y + (line_height - pill.y) * 0.5f);
  const ImVec2 bottom_right(top_left.x + pill.x, top_left.y + pill.y);
  ImVec4 background = LAUNCHER_ACCENT;
  background.w = hovered ? 0.30f : 0.18f;
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(top_left, bottom_right, ImGui::GetColorU32(background), pill.y * 0.5f);
  draw_list->AddRect(top_left, bottom_right, ImGui::GetColorU32(LAUNCHER_ACCENT), pill.y * 0.5f);
  draw_list->AddText(ImVec2(top_left.x + padding.x, top_left.y + padding.y),
                     ImGui::GetColorU32(LAUNCHER_ACCENT), text.c_str());
  if (hovered)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("%s", ImGui::GetTime() - opened_at < 2.5 ?
                                Tr("Opened in your browser and copied to the clipboard") :
                                Tr("Open the download page"));
  }
}

// The Credits page's VERSION panel: this build's version, what GitHub says
// about the newest one, the button that asks again, and whether to ask each
// time the launcher opens. Returns true when that last choice changes
// (config.ini's check_for_updates).
bool DrawVersionSection(moderngekko::frontend::UpdateCheck& updates, bool& check_on_open,
                        ImFont* bold)
{
  using State = moderngekko::frontend::UpdateCheck::State;
  const moderngekko::frontend::UpdateCheck::Status status = updates.Get();
  BeginSection("##credits_version", Tr("VERSION"));
  ImGui::PushFont(bold, 0.0f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(Format(Tr("Version %s"), updates.CurrentVersion().c_str()).c_str());
  ImGui::PopFont();
  // The button at the right end of the same line.
  const char* label = Tr("Check for updates");
  const float button_width =
      ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
  ImGui::SameLine();
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - button_width));
  ImGui::BeginDisabled(status.state == State::Checking);
  if (ImGui::Button(label))
    updates.Start();
  ImGui::EndDisabled();
  ImGui::PushTextWrapPos(0.0f);
  switch (status.state)
  {
  case State::Idle:
    break;
  case State::Checking:
    ImGui::TextDisabled("%s", Tr("Looking on GitHub..."));
    break;
  case State::UpToDate:
    ImGui::TextColored(LAUNCHER_ACCENT, "%s", Tr("You have the latest version."));
    break;
  case State::Available:
    ImGui::TextColored(LAUNCHER_ACCENT, "%s",
                       Format(Tr("A new version is available: %s"), status.latest.c_str()).c_str());
    DrawWebLink(Tr("Open the download page"), DownloadPage());
    break;
  case State::NoReleases:
    ImGui::TextDisabled("%s", Tr("No version has been published on GitHub yet."));
    break;
  case State::Failed:
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                       Tr("Couldn't check. Check your internet connection."));
    break;
  }
  ImGui::PopTextWrapPos();
  const bool toggled = ImGui::Checkbox(Tr("Check for updates when the launcher opens"),
                                       &check_on_open);
  EndSection();
  return toggled;
}
#endif

// Resources/patch-notes.txt, read once when the launcher opens; the PLAY
// page shows the part in its language (see launcher_patch_notes.hpp). No
// file, no panel.
std::string ReadPatchNotes(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  // Notes for players, not a book: the first 64 KB are plenty.
  std::string text(64 * 1024, '\0');
  file.read(text.data(), static_cast<std::streamsize>(text.size()));
  text.resize(static_cast<std::size_t>(file.gcount()));
  return text;
}

// Text at the cursor, wrapped to the room left: one with Japanese by
// BreakLines (launcher_line_break.hpp), as Dear ImGui would cut it anywhere,
// even inside a name; the rest by Dear ImGui. The patch notes' lines, and the
// choices' descriptions in Settings.
void WrappedText(std::string_view text)
{
  if (!moderngekko::frontend::HasJapanese(text))
  {
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    return;
  }
  ImFont* const font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  const std::string lines = moderngekko::frontend::BreakLines(
      text, ImGui::GetContentRegionAvail().x, [&](std::string_view piece) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, piece.data(), piece.data() + piece.size())
            .x;
      });
  ImGui::PushTextWrapPos(-1.0f);
  ImGui::TextUnformatted(lines.c_str());
  ImGui::PopTextWrapPos();
}

// A choice's description in a Settings list: dimmed and wrapped.
void DimWrappedText(std::string_view text)
{
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::PushTextWrapPos(0.0f);
  WrappedText(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

// The PLAY page's PATCH NOTES panel, at the given size: its heading stays put
// and the notes scroll under it when they don't fit.
// pad_scroll scrolls the notes (a gamepad's right stick).
void DrawPatchNotes(const std::vector<moderngekko::frontend::PatchNoteLine>& notes, ImFont* bold,
                    ImVec2 size, float pad_scroll)
{
  using Kind = moderngekko::frontend::PatchNoteLine::Kind;
  ImGui::BeginChild("##patch_notes", size, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::TextDisabled("%s", Tr("PATCH NOTES"));
  ImGui::BeginChild("##patch_notes_text");
  if (pad_scroll != 0.0f)
    ImGui::SetScrollY(ImGui::GetScrollY() + pad_scroll);
  ImGui::PushTextWrapPos(0.0f);
  for (const moderngekko::frontend::PatchNoteLine& line : notes)
  {
    switch (line.kind)
    {
    case Kind::Heading:
      ImGui::PushFont(bold, 0.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
      ImGui::TextUnformatted(line.text.c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      break;
    case Kind::Point:
      // A hanging indent: the wrapped lines start under the text, not the dot.
      ImGui::Bullet();
      ImGui::SameLine();
      WrappedText(line.text);
      break;
    case Kind::Paragraph:
      WrappedText(line.text);
      break;
    case Kind::Gap:
      ImGui::Spacing();
      break;
    }
  }
  ImGui::PopTextWrapPos();
  ImGui::EndChild();
  ImGui::EndChild();
}

// The size the launcher's pages are laid out for, before the display's scale.
// The window is this shape and can't be resized: it opens a third bigger
// (LAUNCHER_ZOOM, 1280x720 at 100%), everything in it scaled alike, or as
// big as fits the screen when that is less (never under this size).
constexpr int LAUNCHER_WIDTH = 960;
constexpr int LAUNCHER_HEIGHT = 540;
constexpr float LAUNCHER_ZOOM = 4.0f / 3.0f;

// How much bigger than LAUNCHER_WIDTH x LAUNCHER_HEIGHT the window opens on
// a display with this content scale and usable area (the screen without its
// panels), leaving room for the window's title bar and borders.
float LauncherZoom(float display_scale, const SDL_Rect& usable)
{
  if (usable.w <= 0 || usable.h <= 0 || display_scale <= 0.0f)
    return 1.0f;
  const float fits_width = (usable.w - 16.0f) / (LAUNCHER_WIDTH * display_scale);
  const float fits_height = (usable.h - 48.0f) / (LAUNCHER_HEIGHT * display_scale);
  return std::clamp(std::min(fits_width, fits_height), 1.0f, LAUNCHER_ZOOM);
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
  constexpr std::array<std::pair<std::string_view, SDL_Scancode>, 32> named_keys = {{
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
      {"ESCAPE", SDL_SCANCODE_ESCAPE},     {"BackSpace", SDL_SCANCODE_BACKSPACE},
      {"BACK", SDL_SCANCODE_BACKSPACE},    {"Backspace", SDL_SCANCODE_BACKSPACE},
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

// Where the SR-Source controller drawing has each of its buttons. These belong
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
  INPUT_SELECT,  // the drawing's − and triangle buttons, kept for mods the
  INPUT_MENU,    // same way: the game itself has no Select or menu button
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
    {"Extra/Select"},
    {"Extra/Menu"},
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
    "A", "B", "X", "Y", "Z", "L", "R", "START", "C", "SELECT", "MENU",
    "",  "",  "",  "",  "",  "",  "",  ""};
constexpr std::array<int, INPUT_COUNT> INPUT_KEY_ARROWS = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                           1, 2, 3, 4, 1, 2, 3, 4};

constexpr std::array<LegendRow, 12> LEGEND_ROWS = {{
    {"A", INPUT_A, INPUT_A},
    {"B", INPUT_B, INPUT_B},
    {"X", INPUT_X, INPUT_X},
    {"Y", INPUT_Y, INPUT_Y},
    {"Z", INPUT_Z, INPUT_Z},
    {"L", INPUT_L, INPUT_L},
    {"R", INPUT_R, INPUT_R},
    {"Start", INPUT_START, INPUT_START},
    {MODERNGEKKO_UI_TEXT("Stick"), INPUT_STICK_UP, INPUT_DPAD_RIGHT},  // stick and D-pad
    {"C", INPUT_C, INPUT_C},
    {"Select", INPUT_SELECT, INPUT_SELECT},
    {MODERNGEKKO_UI_TEXT("Menu"), INPUT_MENU, INPUT_MENU},
}};

// The drawing's six face buttons are A, B, C and X, Y, Z, and its shoulders
// are L and R. GameCube has no C, Select (−) or menu (the triangle) button:
// the profile keeps them apart (Extra/...) for mods. The D-pad arms light for
// the stick as well as for the D-pad.
constexpr std::array<ArtHotspot, 19> CONTROLLER_ART_HOTSPOTS = {{
    {INPUT_A, 1, {{{646, 605}}}, 42},
    {INPUT_B, 1, {{{735, 553}}}, 42},
    {INPUT_X, 1, {{{599, 515}}}, 42},
    {INPUT_Y, 1, {{{688, 461}}}, 42},
    {INPUT_Z, 1, {{{786, 427}}}, 42},
    {INPUT_C, 1, {{{831, 518}}}, 42},
    {INPUT_R, 6, ART_RIGHT_SHOULDER, 0},
    {INPUT_L, 6, ART_LEFT_SHOULDER, 0},
    {INPUT_START, 1, {{{595, 405}}}, 22},   // +
    {INPUT_SELECT, 1, {{{429, 405}}}, 22},  // −
    {INPUT_MENU, 3, {{{512, 538}, {464, 643}, {560, 643}}}, 0},  // the triangle
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
    {MODERNGEKKO_UI_TEXT("D-Pad"), INPUT_UP, INPUT_RIGHT},
    {MODERNGEKKO_UI_TEXT("Shake"), INPUT_SHAKE, INPUT_SHAKE},
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

// Where the SR-Source keyboard drawing (1768 x 608) has the keys the generated
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
constexpr std::array<KeyboardArtKey, 14> KEYBOARD_ART_KEYS = {{
    {SDL_SCANCODE_Q, {226, 156}, {320, 232}},
    {SDL_SCANCODE_W, {330, 156}, {422, 232}},
    {SDL_SCANCODE_E, {432, 156}, {524, 232}},
    {SDL_SCANCODE_A, {246, 259}, {336, 334}},
    {SDL_SCANCODE_S, {351, 259}, {439, 334}},
    {SDL_SCANCODE_D, {455, 259}, {543, 334}},
    {SDL_SCANCODE_LSHIFT, {56, 360}, {278, 433}},
    {SDL_SCANCODE_RETURN, {1403, 260}, {1604, 332}},
    {SDL_SCANCODE_BACKSPACE, {1422, 54}, {1594, 126}},
    {SDL_SCANCODE_M, {928, 357}, {1000, 432}},
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
      return Tr("L trigger");
    if (source.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
      return Tr("R trigger");
    // Left stick (axes 0 and 1) or right stick (2 and 3), then the direction.
    constexpr std::array<std::array<const char*, 4>, 2> directions = {{
        {MODERNGEKKO_UI_TEXT("Left stick left"), MODERNGEKKO_UI_TEXT("Left stick right"),
         MODERNGEKKO_UI_TEXT("Left stick up"), MODERNGEKKO_UI_TEXT("Left stick down")},
        {MODERNGEKKO_UI_TEXT("Right stick left"), MODERNGEKKO_UI_TEXT("Right stick right"),
         MODERNGEKKO_UI_TEXT("Right stick up"), MODERNGEKKO_UI_TEXT("Right stick down")},
    }};
    const bool horizontal = source.axis % 2 == 0;
    const std::size_t direction =
        (horizontal ? 0 : 2) + (source.direction > 0 ? 1 : 0);
    return Tr(directions[source.axis < 2 ? 0 : 1][direction]);
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
      return Tr("Cross");
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
      return Tr("Circle");
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
      return Tr("Square");
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
      return Tr("Triangle");
    default:
      break;
    }
    constexpr std::array<const char*, 4> positions = {
        MODERNGEKKO_UI_TEXT("South button"), MODERNGEKKO_UI_TEXT("East button"),
        MODERNGEKKO_UI_TEXT("West button"), MODERNGEKKO_UI_TEXT("North button")};
    return Tr(positions[static_cast<std::size_t>(source.button)]);
  }
  switch (button)
  {
  case SDL_GAMEPAD_BUTTON_BACK:
    return Tr("Back");
  case SDL_GAMEPAD_BUTTON_GUIDE:
    return "Home";
  case SDL_GAMEPAD_BUTTON_START:
    return "Start";
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
    return Tr("L stick press");
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    return Tr("R stick press");
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    return Tr("L shoulder");
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    return Tr("R shoulder");
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return Tr("D-pad up");
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return Tr("D-pad down");
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return Tr("D-pad left");
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return Tr("D-pad right");
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
      return Tr("Arrow keys");
    return SourceName(gamepad, source);
  }
  if (source.button >= SDL_GAMEPAD_BUTTON_DPAD_UP && source.button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
    return Tr("D-pad");
  if (source.button < 0 && source.axis <= SDL_GAMEPAD_AXIS_LEFTY)
    return Tr("Left stick");
  if (source.button < 0 && source.axis <= SDL_GAMEPAD_AXIS_RIGHTY)
    return Tr("Right stick");
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

// The keyboard drawing, `width` wide and centred. Every key the profile uses
// is outlined and labelled with what it presses, and lights up while held.
void DrawKeyboardArt(const ControllerTester& tester, const LauncherImage& art, float width,
                     ImFont* bold, float scale)
{
  const float available = ImGui::GetContentRegionAvail().x;
  width = std::floor(std::min(width, available));
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

// What one row of the list says: the pad inputs or keys behind that button
// of the emulated controller, or why there are none.
std::string LegendRowText(const ControllerTester& tester, const LegendRow& row, bool keys)
{
  bool custom = false;
  std::vector<std::string> names;
  for (std::size_t input = row.first; input <= row.last; ++input)
  {
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
  if (custom)
    return Tr("edited by hand");
  std::string text;
  for (const std::string& name : names)
    text += (text.empty() ? "" : ", ") + name;
  return text.empty() ? std::string(Tr("not set")) : text;
}

// The width of the list's first column: the emulated controller's buttons.
float LegendLabelWidth(ImFont* bold)
{
  ImGui::PushFont(bold, 0.0f);
  float label_width = 0.0f;
  for (const LegendRow& row : LEGEND_ROWS)
    label_width = std::max(label_width, ImGui::CalcTextSize(Tr(row.label)).x);
  ImGui::PopFont();
  label_width = std::max(label_width, ImGui::CalcTextSize(PROFILE_LEGEND_TITLE).x);
  return label_width + ImGui::GetStyle().ItemSpacing.x * 2.0f;
}

// How wide rows first to first + count - 1 of the list are without wrapping.
float LegendNaturalWidth(const ControllerTester& tester, bool keys, ImFont* bold,
                         std::size_t first, std::size_t count)
{
  float text_width = ImGui::CalcTextSize(keys ? Tr("Key") : Tr("Your controller")).x;
  const std::size_t begin = std::min(first, LEGEND_ROWS.size());
  const std::size_t end = std::min(LEGEND_ROWS.size(), begin + count);
  for (std::size_t row_index = begin; row_index < end; ++row_index)
    text_width = std::max(text_width,
                          ImGui::CalcTextSize(LegendRowText(tester, LEGEND_ROWS[row_index], keys)
                                                  .c_str())
                              .x);
  return LegendLabelWidth(bold) + text_width;
}

// The rows beside the controller drawing (or alone, without one): each
// button of the emulated controller and the pad inputs or keys behind it.
// Rows first to first + count - 1 of LEGEND_ROWS, so the list can be split in
// two columns; the names wrap at `width` (0: at the panel's edge).
void DrawInputLegend(const ControllerTester& tester, bool keys, ImFont* bold,
                     std::size_t first = 0, std::size_t count = LEGEND_ROWS.size(),
                     float width = 0.0f)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  const float wrap_x = width > 0.0f ? ImGui::GetCursorPosX() + width : 0.0f;
  ImGui::BeginGroup();
  const float label_width = LegendLabelWidth(bold);
  // Rows sit close together so the list is about as tall as the artwork.
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
  // Inside a group, SameLine() offsets count from the group's left edge.
  ImGui::TextDisabled("%s", PROFILE_LEGEND_TITLE);
  ImGui::SameLine(label_width);
  ImGui::TextDisabled("%s", keys ? Tr("Key") : Tr("Your controller"));
  const std::size_t begin = std::min(first, LEGEND_ROWS.size());
  const std::size_t end = std::min(LEGEND_ROWS.size(), begin + count);
  for (std::size_t row_index = begin; row_index < end; ++row_index)
  {
    const LegendRow& row = LEGEND_ROWS[row_index];
    bool down = false;
    for (std::size_t input = row.first; input <= row.last; ++input)
      down = down || IsInputDown(tester, input);
    const std::string text = LegendRowText(tester, row, keys);
    ImGui::PushFont(bold, 0.0f);
    if (down)
      ImGui::TextColored(LAUNCHER_ACCENT, "%s", Tr(row.label));
    else
      ImGui::TextUnformatted(Tr(row.label));
    ImGui::PopFont();
    ImGui::SameLine(label_width);
    ImGui::PushTextWrapPos(wrap_x);
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

// Where the Controls page's tester writes its hint: at the right end of the
// BUTTON MAPPING heading's line, in screen coordinates.
struct TesterHintLine
{
  float top = 0.0f;
  float heading_end = 0.0f;  // where the heading's text ends
  float right = 0.0f;        // where the line ends
};

// The tester under the profile line of the Controls page. It starts on that
// line, with the Controller / Keyboard tabs at its right end when the profile
// has both, then shows the matching drawing (or its list of inputs), with how
// to use it on hint_line while the page tests the controller (testing). The
// drawing is as tall as the room left above page_bottom (the bottom of the
// page, in screen coordinates) allows, so the page doesn't scroll.
void DrawControllerTester(ControllerTester& tester, const LauncherImage& controller_art,
                          const LauncherImage& keyboard_art, ImFont* bold, float scale,
                          float page_bottom, const TesterHintLine& hint_line, bool testing)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  if (tester.has_pad_sources && tester.has_key_sources)
  {
    const float tabs_width = ImGui::CalcTextSize(Tr("Controller")).x +
                             ImGui::CalcTextSize(Tr("Keyboard")).x +
                             style.FramePadding.x * 4.0f + style.ItemSpacing.x;
    ImGui::SameLine();
    // On a line of their own when the profile line leaves no room (a
    // language with longer words).
    if (ImGui::GetContentRegionAvail().x < tabs_width)
      ImGui::NewLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, ImGui::GetContentRegionAvail().x - tabs_width));
    if (DrawTesterTab(Tr("Controller"), !tester.show_keyboard))
      tester.show_keyboard = false;
    ImGui::SameLine();
    if (DrawTesterTab(Tr("Keyboard"), tester.show_keyboard))
      tester.show_keyboard = true;
  }
  ImGui::Spacing();
  // The height down to the bottom of the page, less the gap under the
  // drawing, the panel's padding and the gap after the panel. Never smaller
  // than a drawing that can still be read.
  const float art_height =
      std::max(110.0f * scale, page_bottom - ImGui::GetCursorScreenPos().y -
                                   style.WindowPadding.y * 0.75f - style.ItemSpacing.y * 2.0f);
  const float available = ImGui::GetContentRegionAvail().x;

  // How to use the tester, or why it can't light anything up, at the right
  // end of the heading's line ("..." cuts it if the line is too short).
  const auto draw_hint = [&](const std::string& text, ImVec4 color)
  {
    if (!testing)
      return;
    const float room = hint_line.right - hint_line.heading_end - style.ItemSpacing.x * 2.0f;
    const std::string shown = FitTextToWidth(ImGui::GetFont(), ImGui::GetFontSize(), text, room);
    const float width = ImGui::CalcTextSize(shown.c_str()).x;
    ImGui::GetWindowDrawList()->AddText(ImVec2(hint_line.right - width, hint_line.top),
                                        ImGui::GetColorU32(color), shown.c_str());
  };

  if (tester.show_keyboard)
  {
    draw_hint(Tr("Press keys to test them."), LAUNCHER_ACCENT);
    if (keyboard_art.texture)
    {
      DrawKeyboardArt(tester, keyboard_art,
                      std::min(available * 0.9f, art_height * KEYBOARD_ART_SIZE.x /
                                                      KEYBOARD_ART_SIZE.y),
                      bold, scale);
    }
    else
    {
      DrawInputLegend(tester, true, bold);
      EndInputLegend();
    }
    return;
  }

  if (tester.gamepad)
  {
    draw_hint(Tr("Press buttons to test them."), LAUNCHER_ACCENT);
  }
  else if (tester.device.empty())
  {
    draw_hint(Format(Tr("No controller in %s."), std::string(CONTROLLER_PROFILE_NAME).c_str()),
              style.Colors[ImGuiCol_TextDisabled]);
  }
  else
  {
    // "SDL/0/PS3 Controller" -> "PS3 Controller"
    const std::size_t name_start = tester.device.find('/', tester.device.find('/') + 1);
    const std::string name =
        name_start == std::string::npos ? tester.device : tester.device.substr(name_start + 1);
    draw_hint(Format(Tr("Connect %s."), name.c_str()), ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
  }
  if (!controller_art.texture)
  {
    DrawInputLegend(tester, false, bold);
    EndInputLegend();
    return;
  }
  // The drawing and the list beside it, in two columns when they fit, which
  // are shorter than the drawing; in one otherwise.
  const float art_aspect = (CONTROLLER_ART_MAX.x - CONTROLLER_ART_MIN.x) /
                           (CONTROLLER_ART_MAX.y - CONTROLLER_ART_MIN.y);
  const float gap = style.ItemSpacing.x * 2.0f;
  const float width = std::floor(std::min(available * 0.42f, art_height * art_aspect));
  DrawControllerArt(tester, controller_art, width, scale);
  ImGui::SameLine(0.0f, gap);
  const float list_width = available - width - gap;
  if (list_width < 2.0f * 180.0f * scale + gap)
  {
    DrawInputLegend(tester, false, bold);
    EndInputLegend();
    return;
  }
  const std::size_t half = (LEGEND_ROWS.size() + 1) / 2;
  // Each column as wide as its names need when both fit, so that fewer of
  // them wrap; halves otherwise.
  const float first_needs = LegendNaturalWidth(tester, false, bold, 0, half);
  const float second_needs =
      LegendNaturalWidth(tester, false, bold, half, LEGEND_ROWS.size() - half);
  const float first_width = first_needs + second_needs + gap <= list_width ?
                                std::floor(first_needs) :
                                std::floor((list_width - gap) * 0.5f);
  DrawInputLegend(tester, false, bold, 0, half, first_width);
  EndInputLegend();
  ImGui::SameLine(0.0f, gap);
  DrawInputLegend(tester, false, bold, half, LEGEND_ROWS.size() - half,
                  list_width - gap - first_width);
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

  // A renamed project's players keep their folder: the one of the earlier
  // name is moved to the new one the first time.
  const fs::path user_directory = [] {
    const moderngekko::frontend::SettledUserFolder settled =
        moderngekko::frontend::SettleUserFolder(
            DefaultUserDirectory(),
            moderngekko::frontend::SplitFolderNames(MODERNGEKKO_PREVIOUS_USER_DIRECTORY_NAMES));
    if (!settled.moved_from.empty())
      std::cout << "user folder: moved " << settled.moved_from.string() << " to "
                << settled.folder.string() << '\n';
    if (!settled.error.empty())
      std::cerr << "user folder: can't move " << settled.folder.string() << " to its new name ("
                << settled.error << "); using it where it is\n";
    return settled.folder;
  }();
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
  // Interface language: config.ini's choice, or the system's one on "auto".
  const std::string system_language = SystemLanguageCode();
  moderngekko::frontend::ActiveLanguage() =
      moderngekko::frontend::ResolveLanguage(config.language, system_language);

  // One size that can't be changed, so every page is laid out for it: the
  // design's (LAUNCHER_WIDTH x LAUNCHER_HEIGHT) at the display's scale, made
  // a third bigger when the screen has room. `scale` sizes everything drawn.
  const SDL_DisplayID display = SDL_GetPrimaryDisplay();
  const float display_scale = SDL_GetDisplayContentScale(display);
  SDL_Rect usable{};
  if (!SDL_GetDisplayUsableBounds(display, &usable))
    usable = {};
  const float scale = display_scale * LauncherZoom(display_scale, usable);
  SDL_Window* window = SDL_CreateWindow(
      MODERNGEKKO_LAUNCHER_TITLE, static_cast<int>(LAUNCHER_WIDTH * scale),
      static_cast<int>(LAUNCHER_HEIGHT * scale), SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window)
  {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SetWindowIcon(window, release_directory);
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
  // The keyboard and any gamepad move through the launcher: the arrows, the
  // D-pad or the left stick move, Enter or A chooses, and Escape or B goes back
  // to the sidebar; LB and RB change the page (see the main loop).
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ApplyLauncherTheme(ImGui::GetStyle());
  ImGui::GetStyle().ScaleAllSizes(scale);
  ImGui::GetStyle().FontScaleDpi = scale;
  // ImGui sizes a font by its full line height. 18 px gives Noto Sans and Segoe
  // UI a comfortable reading size while the Play page still fits the window.
  const LauncherFonts fonts = LoadInterfaceFont(release_directory, 18.0f);
  // The game selection screen's art and font (Resources/game-select), for a
  // build that plays more than one game; its pictures load the first time
  // PLAY opens it.
  const fs::path game_select_folder = release_directory / "Resources" / "game-select";
  ImFont* const game_select_font = moderngekko::frontend::PlaysSeveralGames() ?
                                       LoadGameSelectFont(game_select_folder) :
                                       nullptr;
  GameSelectScreen game_select;
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll);
  ImGui_ImplSDLRenderer3_Init(renderer);
  // The PLAY page's banner is drawn at most half the window wide, the
  // EXPANSIONS picture 40% of it: bigger pictures are shrunk first.
  int window_pixels_wide = 0;
  if (!SDL_GetWindowSizeInPixels(window, &window_pixels_wide, nullptr))
    window_pixels_wide = 0;
  const LauncherBanner banner =
      LoadLauncherBanner(renderer, release_directory, window_pixels_wide * 0.5f);
  // The same banner in layers, that move (Resources/banner): shown instead
  // when it is there.
  LayeredBanner layered_banner;
  layered_banner.Load(renderer, release_directory / "Resources" / "banner",
                      window_pixels_wide * 0.5f);
  // The PLAY page's banner at the cursor, `width` wide: the one in layers, or
  // else the flat one. False with neither.
  const auto draw_banner = [&](float width) {
    if (layered_banner.Loaded())
      layered_banner.Draw(renderer, width, static_cast<double>(SDL_GetTicksNS()) / 1e9);
    else if (banner.texture)
      DrawLauncherBanner(banner, width);
    else
      return false;
    return true;
  };
  const LauncherImage controller_art = LoadLauncherImage(
      renderer, release_directory, "launcher-controller.png", "launcher controller art");
  const LauncherImage keyboard_art = LoadLauncherImage(
      renderer, release_directory, "launcher-keyboard.png", "launcher keyboard art");
  // The Mods & expansions page's picture of what expansions could be.
  const LauncherImage mods_art =
      LoadLauncherImage(renderer, release_directory, "launcher-mods.png", "launcher mods art",
                        false, window_pixels_wide * 0.4f);
  // The background's floor slides (RenderBackground in launcher_ui.hpp).
  const LauncherImage background = LoadLauncherImage(
      renderer, release_directory, "launcher-background.png", "launcher background", true);
  // The PLAY page's PATCH NOTES (MODERNGEKKO_LAUNCHER_PATCH_NOTES), read in
  // the interface's language again when it changes.
  const std::string patch_notes_text =
      ReadPatchNotes(release_directory / "Resources" / "patch-notes.txt");
  std::vector<moderngekko::frontend::PatchNoteLine> patch_notes;
  std::string patch_notes_language;
  // Resources/launcher-music.mp3 loops while the launcher is open, stops
  // while the game runs and starts over after. Without the file there is no
  // music and no slider for it in Settings → Audio.
  moderngekko::frontend::MusicPlayer music(MODERNGEKKO_FRONTEND_NAME);
  const fs::path music_path = release_directory / "Resources" / "launcher-music.mp3";
  const bool has_music = music.Play(music_path, config.music_volume);
#if defined(MODERNGEKKO_FRONTEND_VERSION) && defined(MODERNGEKKO_UPDATE_REPOSITORY)
  // Check for updates (launcher_update.hpp): GitHub is asked when the
  // launcher opens, unless turned off on the Credits page, and with that
  // page's button.
  moderngekko::frontend::UpdateCheck update_check(MODERNGEKKO_UPDATE_REPOSITORY,
                                                  MODERNGEKKO_FRONTEND_VERSION);
  if (config.check_for_updates)
    update_check.Start();
#endif

  std::vector<fs::path> images = FindDiscImages();
  std::optional<fs::path> selected_image = images.empty() ? std::nullopt : std::optional(images[0]);
  // An extracted game as the launcher sees it: ready, or why not. The
  // bundled DOL patch is the main game's, so only that one gets it.
  const auto inspect_game = [&](const fs::path& root) {
    const auto& games = moderngekko::frontend::KnownGames();
    bool changed = false;
    std::string patch_error;
    const bool main_game = games.size() <= 1 || root.filename() == games.front().disc_id;
    moderngekko::GameInspectResult result =
        !main_game || ApplyBundledDolPatch(root, release_directory, &changed, &patch_error) ?
            InspectForLauncher(root) :
            moderngekko::GameInspectResult{{}, patch_error};
    if (result && !RequiredGameError(*result.metadata).empty())
      result = {{}, RequiredGameError(*result.metadata)};
    return result;
  };
  // Each game of the build (tools/launcher_games.hpp) and its extraction,
  // games/<disc ID>, or the last game played when it is that one. Looked at
  // as the launcher opens and again after an extraction.
  struct GameSlot
  {
    moderngekko::frontend::KnownGame game;
    fs::path root;
    moderngekko::GameInspectResult metadata;
  };
  std::vector<GameSlot> game_slots;
  // The game PLAY and Netplay start: the last one played, else the first
  // one ready. Without one ready, current_metadata says why.
  fs::path current_game = ReadDefaultGame(user_directory, release_directory);
  auto current_metadata = inspect_game(current_game);
  for (const moderngekko::frontend::KnownGame& game : moderngekko::frontend::KnownGames())
  {
    GameSlot slot{game, user_directory / "games" / game.disc_id, {}};
    if (current_metadata && current_metadata.metadata->disc_id == game.disc_id)
    {
      slot.root = current_game;
      slot.metadata = current_metadata;
    }
    else
    {
      slot.metadata = inspect_game(slot.root);
    }
    game_slots.push_back(std::move(slot));
  }
  if (!current_metadata)
  {
    for (const GameSlot& slot : game_slots)
    {
      if (slot.metadata)
      {
        current_game = slot.root;
        current_metadata = slot.metadata;
        break;
      }
    }
  }
  // PLAY or Netplay's game choice: this game is the one they start.
  const auto choose_game = [&](const GameSlot& slot) {
    current_game = slot.root;
    current_metadata = slot.metadata;
  };
  const auto slot_of = [&](std::string_view disc_id) -> const GameSlot* {
    for (const GameSlot& slot : game_slots)
    {
      if (slot.game.disc_id == disc_id)
        return &slot;
    }
    return nullptr;
  };
  // PLAY with more than one game: the selection screen, on the game PLAY
  // would start. False when the build has no art for it (or its art names
  // other games): PLAY asks with plain buttons then.
  const auto open_game_select = [&]() -> bool {
    if (!game_select.Load(renderer, game_select_folder,
                          ImGui::GetMainViewport()->Size.y *
                              ImGui::GetIO().DisplayFramebufferScale.y))
      return false;
    const GameSlot* left = slot_of(game_select.Layout().left_game);
    const GameSlot* right = slot_of(game_select.Layout().right_game);
    if (!left || !right)
      return false;
    const std::string current = current_metadata ? current_metadata.metadata->disc_id : "";
    const GameSelectSide start =
        current == left->game.disc_id && left->metadata     ? GameSelectSide::Left :
        current == right->game.disc_id && right->metadata   ? GameSelectSide::Right :
        left->metadata                                      ? GameSelectSide::Left :
                                                              GameSelectSide::Right;
    game_select.Open(static_cast<bool>(left->metadata), static_cast<bool>(right->metadata), start,
                     static_cast<double>(SDL_GetTicksNS()) / 1e9, left->root, right->root);
    return true;
  };
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
  // The host's address and invite code are hidden on screen until asked, for
  // players who stream.
  bool show_netplay_address = false;
  bool show_netplay_code = false;
  // Friends find the host by invite code (the default) or by its address.
  bool netplay_invite_mode = config.netplay_connection != "ip";
  std::array<char, 32> netplay_code{};
  std::string netplay_join_code;  // the code typed, as the runner takes it
  // "ip:port" pasted whole (as the lobby's Copy button gives it) goes to the
  // address and port fields.
  const auto split_netplay_address = [&]
  {
    if (const auto split = moderngekko::frontend::SplitHostAndPort(netplay_address.data()))
    {
      std::snprintf(netplay_address.data(), netplay_address.size(), "%s", split->host.c_str());
      netplay_port = split->port;
    }
  };
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
  // The custom shape's two numbers as the Settings page edits them; saved
  // once they make a shape config.ini accepts.
  moderngekko::frontend::CustomAspect custom_aspect =
      moderngekko::frontend::ParseCustomAspect(config.custom_aspect)
          .value_or(moderngekko::frontend::CustomAspect{});
  bool custom_aspect_refused = false;
  // The pictures this build can show beside a 4:3 game (Resources/borders,
  // see tools/game_borders.hpp); Settings offers them when there are any.
  const moderngekko::frontend::GameBorderList game_borders =
      moderngekko::frontend::LoadGameBorders(release_directory / "Resources" / "borders");
  // The Screenshots page, made the first time it is shown: the game's
  // screenshots, where Dolphin saves them (ScreenShots/<disc ID>, or every
  // game's without a required disc), and their small pictures in Cache.
  std::optional<moderngekko::frontend::ui::ScreenshotGallery> screenshot_gallery;
  const auto show_screenshots = [&] {
    if (!screenshot_gallery)
    {
#ifdef MODERNGEKKO_REQUIRED_DISC_ID
      if (!moderngekko::frontend::PlaysSeveralGames())
        screenshot_gallery.emplace(user_directory / "ScreenShots" / MODERNGEKKO_REQUIRED_DISC_ID,
                                   user_directory / "Cache" / "ScreenshotThumbnails", false);
      else
#endif
        screenshot_gallery.emplace(user_directory / "ScreenShots",
                                   user_directory / "Cache" / "ScreenshotThumbnails", true);
    }
    screenshot_gallery->Draw(renderer, fonts.bold, scale);
  };
#ifdef MODERNGEKKO_REQUIRED_DISC_ID
  // The complete saves this build brings (Resources/complete-save, see
  // tools/launcher_save.hpp), which Game Files can install over the player's:
  // the main game's in the folder itself, another game's in a folder named
  // after its disc ID. A Wii game's save is a folder of files, a GameCube
  // game's .gci files on its memory card.
  struct CompleteSave
  {
    std::string disc_id;
    std::string title;
    bool gamecube = false;
    fs::path complete_folder;
    fs::path save_folder;  // the Wii save folder, or the memory card's
    fs::path backups;
    // What the page shows, looked at again every second rather than each
    // frame.
    bool game_save_exists = false;
    std::optional<fs::path> latest_backup;
    // What the last install or restore did, kept as its English text and
    // path so it follows the language.
    const char* message = nullptr;
    std::string message_detail;
    bool message_error = false;
  };
  std::vector<CompleteSave> complete_saves;
  {
    const fs::path resources = release_directory / "Resources" / "complete-save";
    std::vector<moderngekko::frontend::KnownGame> games = moderngekko::frontend::KnownGames();
    if (games.empty())
      games.push_back({MODERNGEKKO_REQUIRED_DISC_ID, MODERNGEKKO_REQUIRED_DISC_ID});
    for (std::size_t i = 0; i < games.size(); ++i)
    {
      CompleteSave save;
      save.disc_id = games[i].disc_id;
      save.title = games[i].title;
      save.gamecube = moderngekko::frontend::IsGameCubeDisc(save.disc_id);
      save.complete_folder = i == 0 ? resources : resources / save.disc_id;
      const bool brought =
          save.gamecube ?
              !moderngekko::frontend::CompleteGameCubeSaveFiles(save.complete_folder).empty() :
              !moderngekko::frontend::CompleteSaveFiles(save.complete_folder).empty();
      if (!brought)
        continue;
      save.save_folder =
          save.gamecube ? moderngekko::frontend::GameCubeCardFolder(user_directory, save.disc_id) :
                          moderngekko::frontend::WiiSaveFolder(user_directory, save.disc_id);
      save.backups = moderngekko::frontend::SaveBackupsFolder(user_directory, save.disc_id);
      complete_saves.push_back(std::move(save));
    }
  }
  const bool has_complete_save = !complete_saves.empty();
  Uint64 save_checked_at = 0;
#endif
  // The runner gives MODERNGEKKO_STATICRECOMP priority over config.ini and
  // inherits this process' environment, so say so when it is set.
  const char* cpu_core_environment_value = std::getenv("MODERNGEKKO_STATICRECOMP");
  const std::string cpu_core_override =
      cpu_core_environment_value ? cpu_core_environment_value : "";

  // StateSaves is Dolphin's own state directory, so states written in-game land
  // here without any extra configuration and show up on the next launch.
  //
  // Listed at startup and again when the launcher comes back from the game,
  // rather than every frame, so the dropdown does not hit the filesystem on
  // each redraw. States are written by the running game, not by the launcher.
  std::vector<fs::path> launcher_savestates = PlaySavestates(user_directory);
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
  StatusText controller_status;
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
    controller_status = {CONTROLLER_MAPPED_STATUS};
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
    controller_status = {MODERNGEKKO_UI_TEXT("%s now uses %s with the same buttons"),
                         std::string(CONTROLLER_PROFILE_NAME), controllers[index].label};
    std::cout << "controller: " << controller_status.English() << '\n';
    controller_tester.loaded = false;
    return true;
  };
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  // Without a gamepad the profile is written for the keyboard alone. A gamepad
  // connected later takes the keyboard's place as the profile's device
  // (retarget_controller), and the keys keep working.
  const std::string keyboard_device(moderngekko::frontend::KeyboardDevice());
  const StatusText keyboard_only_status = {
      MODERNGEKKO_UI_TEXT("Keyboard only. A controller you connect is added to the same profile.")};
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
        dialog.error = Format(Tr("%s has no configured controller device"),
                              std::string(CONTROLLER_PROFILE_NAME).c_str());
        return false;
      }
      // The profile's pad isn't connected but another pad is: most likely the
      // same pad under another name (USB / Bluetooth, or Steam's virtual
      // gamepad), or the pad that replaced it. The first one takes its place,
      // as in the game.
      if (FindController(controllers, configured_controllers.front()) < 0 && !controllers.empty())
        return retarget_controller(0);
      selected_controller = configured_controllers.front();
      controller_index = FindController(controllers, selected_controller);
      controller_status = {MODERNGEKKO_UI_TEXT("Using existing %s"),
                           std::string(CONTROLLER_PROFILE_NAME)};
      return true;
    }
    if (controller_index < 0)
    {
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
      return select_keyboard_only();
#else
      std::lock_guard lock(dialog.mutex);
      dialog.error = Tr("Connect an SDL-compatible controller before playing");
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
        controller_status = {MODERNGEKKO_UI_TEXT("No SDL gamepad detected")};
#endif
    }
    else if (controller_index < 0 && !controllers.empty())
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
      controller_status = {MODERNGEKKO_UI_TEXT("Using existing %s"),
                           std::string(CONTROLLER_PROFILE_NAME)};
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
  // The game selection screen chose the game, and ended in black.
  bool launch_from_black = false;
  LauncherPage current_page = LauncherPage::Play;
  // Moving through the launcher with a gamepad.
  // The Controls page tests the controller: while it does, the gamepad's
  // buttons and the keyboard's keys only light the tester up, and neither
  // moves through the launcher. Opening the page starts the test; holding B,
  // or Escape, stops it, and Test buttons starts it again.
  bool controls_testing = true;
  // The page was reached with LB or RB: passing through Controls that way
  // doesn't start the test.
  bool page_by_shoulder = false;
  LauncherPage shown_page = current_page;
  // Next frame, the sidebar's item for the page shown takes the focus (after
  // B, LB or RB).
  bool focus_sidebar = false;
  // At the start and after the game, a gamepad moves nothing until all its
  // buttons are let go: one still held from the game mustn't press PLAY.
  bool pad_waits_for_release = true;
  // What was used last: a gamepad or the keyboard (the footer shows what its
  // buttons or keys do), or the mouse (no hints).
  enum class LastInput
  {
    Mouse,
    Keyboard,
    Pad,
  };
  LastInput last_input = LastInput::Mouse;
  PadStyle pad_style = PadStyle::Xbox;
  const auto read_pad_style = [&] {
    int count = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&count);
    pad_style = pads && count > 0 ? PadStyleOf(SDL_GetGamepadTypeForID(pads[0])) : PadStyle::Xbox;
    SDL_free(pads);
  };
  read_pad_style();
  // Last frame's popups, active item and screenshot viewer: B closes them
  // before it goes back to the sidebar.
  bool popup_was_open = false;
  bool item_was_active = false;
  bool viewer_was_open = false;
  // The focus was on the sidebar last frame: right goes into the page.
  bool sidebar_was_focused = false;
  bool enter_page = false;
  // Which session the Netplay window is about to start (Host or Join).
  LaunchMode netplay_dialog_mode = LaunchMode::Host;
  // Host Netplay's "Allow in the Windows firewall" (Windows only).
  moderngekko::frontend::FirewallRule firewall_rule;
  // PLAY page summary, read again whenever the page is opened: the time
  // played changes while the game runs and mods are added by hand.
  std::uint64_t summary_time_played = 0;
  int summary_installed_mods = 0;
  bool summary_stale = true;
  // The Mods & expansions page's list, read again each time it is opened.
  // The runner loads mods from the same two folders.
  const std::vector<fs::path> mod_folders = {release_directory / "Mods", user_directory / "Mods"};
  std::vector<fs::path> installed_mods;
  const auto open_mods_page = [&] {
    current_page = LauncherPage::Mods;
    installed_mods = moderngekko::frontend::ListInstalledMods(mod_folders);
  };
  int result = 0;
  // Set when the player chose Quit game in the in-game menu: the launcher
  // closes instead of coming back.
  bool quit_from_game = false;
  // The game started by PLAY, Host or Join, while the launcher waits for it:
  // its process, its log, and the file the runner creates once the game can
  // be seen (launcher_handoff.hpp). Until then the launcher shows its loading
  // screen over everything (launcher_transition.hpp); then it hides, where
  // it was, until the game closes.
  struct GameSession
  {
    SDL_Process* process = nullptr;
    LaunchMode mode = LaunchMode::None;
    fs::path log_path;
    bool log_written = false;
    fs::path ready_file;
    bool hidden = false;
    bool placed = false;  // where the launcher was, to come back there
    int x = 0;
    int y = 0;
  };
  GameSession session;
  GameTransition transition;
  // The loading screen's logo: the game's, when the build has the game
  // selection screen's art.
  LoadingLogo loading_logo;
  // The music's volume as it fades out while the game starts.
  int music_level = config.music_volume;
  // Why the game didn't start or failed, shown once the launcher is back.
  std::string session_error_title;
  std::string session_error;
  // Starts the game (PLAY, Host or Join). The launcher goes black, unless it
  // is already (`from_black`: the game selection screen ends in black), and
  // the main loop waits for the game. False, with a message for when the
  // launcher is back, when it can't start.
  const auto start_game = [&](LaunchMode mode, bool from_black) -> bool
  {
    const double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
    transition.Start(now, from_black);
    std::string launch_error;
    if (!WriteDefaultGame(user_directory, release_directory, current_game,
                          &launch_error))
    {
      session_error_title = Tr("Launch failed");
      session_error = launch_error;
      transition.Back(now);
      return false;
    }
    std::vector<std::string> argument_storage = {SiblingRunner(argv[0]).string(), "--game",
                                                 current_game.string(), "--user-dir",
                                                 user_directory.string()};
    // With more than one game, the window takes the name of the one chosen.
    if (moderngekko::frontend::PlaysSeveralGames() && current_metadata)
    {
      if (const moderngekko::frontend::KnownGame* game =
              moderngekko::frontend::FindKnownGame(current_metadata.metadata->disc_id))
      {
        argument_storage.emplace_back("--title");
        argument_storage.emplace_back(game->title);
      }
    }
    // Solo only: see the note by the dropdown.
    if (mode == LaunchMode::Solo && selected_savestate >= 0 &&
        static_cast<std::size_t>(selected_savestate) < launcher_savestates.size())
    {
      argument_storage.emplace_back("--load-state");
      argument_storage.emplace_back(
          moderngekko::frontend::PathText(
              launcher_savestates[static_cast<std::size_t>(selected_savestate)]));
    }
    const bool invite_code = config.netplay_connection == "code";
    if (mode == LaunchMode::Host)
    {
      argument_storage.emplace_back("--netplay-host");
      if (invite_code)
        argument_storage.emplace_back("--invite-code");
    }
    else if (mode == LaunchMode::Join && invite_code)
    {
      argument_storage.emplace_back("--netplay-join-code");
      argument_storage.emplace_back(netplay_join_code);
    }
    else if (mode == LaunchMode::Join)
    {
      argument_storage.emplace_back("--netplay-join");
      argument_storage.emplace_back(config.netplay_address);
    }
    if (mode == LaunchMode::Host || mode == LaunchMode::Join)
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
    // The game's window opens centred on the launcher's (X11, Windows; on
    // Wayland the compositor places windows).
    if (const std::uint64_t launcher_window = NativeWindowId(window))
    {
      argument_storage.emplace_back("--launcher-window");
      argument_storage.emplace_back(std::to_string(launcher_window));
    }
    const std::filesystem::path log_path =
        user_directory / "Logs" / MODERNGEKKO_LOG_FILENAME;
    std::error_code log_error;
    std::filesystem::create_directories(log_path.parent_path(), log_error);
    // Created by the runner when the game can be seen. Without it (no Logs
    // folder), the launcher steps aside as soon as it is black.
    fs::path ready_file;
    if (!log_error)
    {
      ready_file = log_path.parent_path() / "game-shown";
      std::error_code ignored;
      fs::remove(ready_file, ignored);
      argument_storage.emplace_back("--launcher-ready-file");
      argument_storage.emplace_back(moderngekko::frontend::PathText(ready_file));
    }
    std::vector<const char*> arguments;
    arguments.reserve(argument_storage.size() + 1);
    for (const std::string& argument : argument_storage)
      arguments.push_back(argument.c_str());
    arguments.push_back(nullptr);
    SDL_IOStream* log_stream =
        log_error ? nullptr : SDL_IOFromFile(log_path.string().c_str(), "w");
    SDL_Process* process = nullptr;
    std::string process_error;
    if (log_stream)
    {
      // The log's first line says which release and system wrote it, for
      // reports; the runner writes after it.
#if defined(MODERNGEKKO_FRONTEND_VERSION)
      SDL_IOprintf(log_stream, "%s %s (%s)\n", MODERNGEKKO_FRONTEND_NAME,
                   MODERNGEKKO_FRONTEND_VERSION, SDL_GetPlatform());
#else
      SDL_IOprintf(log_stream, "%s (%s)\n", MODERNGEKKO_FRONTEND_NAME, SDL_GetPlatform());
#endif
      SDL_FlushIO(log_stream);
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
      session_error_title = Tr("Launch failed");
      session_error = Format(Tr("Could not start %s: %s"), SiblingRunner(argv[0]).string().c_str(),
                             process_error.c_str());
      transition.Back(now);
      return false;
    }
    session = {process, mode, log_path, !log_error, ready_file};
    if (ready_file.empty())
      transition.GameShown(now);
    // Over the game's window, which opens behind it, until that has its
    // first picture.
    SDL_SetWindowAlwaysOnTop(window, true);
    return true;
  };
  // The game has closed, or can't be followed: the launcher comes back from
  // black, where it was, with a message when the game failed (shown once it
  // is back). Quit game in the in-game menu closes the launcher instead.
  const auto end_game = [&](bool waited, int exit_code)
  {
    const std::string wait_error = waited ? std::string() : std::string(SDL_GetError());
    SDL_DestroyProcess(session.process);
    std::error_code ignored;
    if (!session.ready_file.empty())
      fs::remove(session.ready_file, ignored);
    const GameSession ended = session;
    session = {};
    if (waited && exit_code == moderngekko::frontend::RUNNER_EXIT_QUIT)
    {
      quit_from_game = true;
      result = 0;
      return;
    }
    if (ended.hidden)
    {
      SDL_ShowWindow(window);
      if (ended.placed)
        SDL_SetWindowPosition(window, ended.x, ended.y);
      SDL_RaiseWindow(window);
    }
    SDL_SetWindowAlwaysOnTop(window, false);
    transition.Back(static_cast<double>(SDL_GetTicksNS()) / 1e9);
    result = 0;
    if (waited && exit_code == 0)
      return;
    const LaunchMode mode = ended.mode;
    std::string launch_error;
    if (!waited)
    {
      launch_error = Format(Tr("The game process could not be monitored: %s"), wait_error.c_str());
    }
    else if (mode == LaunchMode::Join)
    {
      switch (static_cast<moderngekko::frontend::NetplayExitCode>(exit_code))
      {
      case moderngekko::frontend::NetplayExitCode::VersionMismatch:
        launch_error = Tr("The host is running an incompatible netplay build. Both "
                          "players must use the same release.");
        break;
      case moderngekko::frontend::NetplayExitCode::CompatibilityMismatch:
        launch_error = Tr("The extracted game or recomp module does not match the "
                          "host. Both players need the same game revision and "
                          "release.");
        break;
      case moderngekko::frontend::NetplayExitCode::RoomFull:
        launch_error = Tr("All four controller slots are already occupied.");
        break;
      case moderngekko::frontend::NetplayExitCode::GameRunning:
        launch_error = Tr("The host has already started the game.");
        break;
      case moderngekko::frontend::NetplayExitCode::ServerFull:
        launch_error = Tr("The netplay server is full.");
        break;
      case moderngekko::frontend::NetplayExitCode::NicknameRejected:
        launch_error = Tr("The nickname was rejected by the host.");
        break;
      case moderngekko::frontend::NetplayExitCode::InviteCodeFailed:
        launch_error = Tr("No room answered to that invite code. Check that it is typed "
                          "right and that the host's room is still open. In the same "
                          "house, use Direct IP with the host's At home address: invite "
                          "codes often fail there. If it keeps failing, your connection "
                          "may not allow invite codes: try Direct IP with Tailscale.");
        break;
      default:
        launch_error = Tr("Could not reach the host. Check the address and the UDP port: "
                          "in the same house, use the address the host's lobby shows "
                          "under At home. If they are right, the host's firewall is "
                          "probably blocking the game: on Windows, the host can allow it "
                          "in Host Netplay; on Linux, the host opens the UDP port (the "
                          "README says how).");
        break;
      }
    }
    else if (mode == LaunchMode::Host)
    {
      launch_error = Tr("Could not create the netplay session. Check the UDP port "
                        "and firewall settings.");
    }
    else if (exit_code == moderngekko::frontend::RUNNER_EXIT_GRAPHICS)
    {
      // The graphics backend couldn't start: another one may.
      launch_error = Format(Tr("The graphics backend (%s) could not start: your graphics "
                               "card or its driver may not support it. Choose another one "
                               "in Settings, under GRAPHICS, and press PLAY again."),
                            graphics_backends[graphics_backend_index].text);
#if defined(_WIN32)
      launch_error += "\n\n";
      launch_error += Tr("On Windows, Direct3D 11 works on almost every PC.");
#endif
    }
    else
    {
      launch_error = Format(Tr("The game process exited with code %d."), exit_code);
    }
    if (ended.log_written)
      launch_error += Format(Tr("\n\nDetails were written to:\n%s"), ended.log_path.string().c_str());
    session_error_title = Tr("Session failed");
    session_error = std::move(launch_error);
    result = 1;
  };
  // Shows a page the way its sidebar item does, with what opening it reads
  // again.
  const auto go_to_page = [&](LauncherPage page) {
    switch (page)
    {
    case LauncherPage::Play:
      summary_stale = true;
      break;
    case LauncherPage::Mods:
      open_mods_page();
      break;
    case LauncherPage::Screenshots:
      if (screenshot_gallery)
        screenshot_gallery->Refresh();
      break;
    default:
      break;
    }
    current_page = page;
  };
  // Back on the PLAY page after the game: what changed while it ran is read
  // again.
  const auto after_game = [&] {
    pad_waits_for_release = true;
    // The music starts over from the beginning.
    music.Stop();
    if (has_music)
      music.Play(music_path, config.music_volume);
    music_level = config.music_volume;
    done = false;
    current_page = LauncherPage::Play;
    // The in-game menu's Quick settings write config.ini while the game
    // runs: what they change is read back, the rest stays as shown here.
    if (const auto saved = moderngekko::frontend::LoadConfig(user_directory, false))
    {
      config.effect = saved.effect;
      config.game_volume = saved.game_volume;
      config.game_border = saved.game_border;
      config.resolution = saved.resolution;
      for (std::size_t i = 0; i < resolutions.size(); ++i)
      {
        if (saved.resolution == resolutions[i].text)
          resolution_index = static_cast<int>(i);
      }
    }
    // Time played and savestates changed while the game ran.
    summary_stale = true;
    launcher_savestates = PlaySavestates(user_directory);
    selected_savestate = -1;
    // Pad and key events from the game session waited in the queue while
    // the launcher was hidden; they are not meant for it.
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    refresh_controllers();
  };
  while (!done)
  {
    bool controllers_changed = false;
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL3_ProcessEvent(&event);
      // While a game runs the launcher waits for it, as it always has.
      if ((event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) &&
          !session.process)
        done = true;
      if (event.type == SDL_EVENT_JOYSTICK_ADDED || event.type == SDL_EVENT_JOYSTICK_REMOVED ||
          event.type == SDL_EVENT_GAMEPAD_REMAPPED)
      {
        controllers_changed = true;
      }
    }
    if (controllers_changed)
    {
      refresh_controllers();
      read_pad_style();
    }
    // A game starting or running (start_game): the launcher shows its loading
    // screen until the game can be seen, then steps aside where it is and
    // waits for the game to close, as it always did.
    if (session.process)
    {
      int exit_code = 1;
      bool waited = true;
      bool ended = SDL_WaitProcess(session.process, false, &exit_code);
      const double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
      std::error_code ignored;
      if (!ended && !transition.IsGameShown() && fs::exists(session.ready_file, ignored))
        transition.GameShown(now);
      if (!ended && transition.TimeToHide(now))
      {
        music.Stop();
        session.placed = SDL_GetWindowPosition(window, &session.x, &session.y);
        SDL_SetWindowAlwaysOnTop(window, false);
        SDL_HideWindow(window);
        session.hidden = true;
        waited = SDL_WaitProcess(session.process, true, &exit_code);
        ended = true;
      }
      else if (!ended)
      {
        // The music fades out meanwhile.
        const int level = static_cast<int>(
            std::lround(config.music_volume * transition.MusicLevel(now)));
        if (level != music_level)
        {
          music.SetVolume(level);
          music_level = level;
        }
      }
      if (ended)
      {
        end_game(waited, exit_code);
        // Quit game in the in-game menu: the launcher closes, still hidden.
        if (quit_from_game)
          break;
        after_game();
      }
    }
    // Why the game failed, once the launcher is back.
    if (!session.process && !session_error.empty() &&
        !transition.Active(static_cast<double>(SDL_GetTicksNS()) / 1e9))
    {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, session_error_title.c_str(),
                               session_error.c_str(), window);
      session_error.clear();
      pad_waits_for_release = true;
    }
    // All black (the loading screen): the pages aren't drawn.
    {
      const double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
      if (transition.CoversLauncher(now))
      {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        DrawLoadingScreen(ImGui::GetForegroundDrawList(), viewport->Pos, viewport->Size,
                          transition, now, loading_logo, scale);
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        continue;
      }
    }
    UpdateControllerTester(controller_tester, current_page == LauncherPage::Controls,
                           controller_profile, controllers);
    // Opening the Controls page starts testing the controller, unless LB or
    // RB only pass through it (Test buttons or Y start it then). While it
    // tests, the keyboard's keys and the gamepad's buttons must not also move
    // ImGui's focus or press the page's buttons.
    if (current_page != shown_page)
    {
      if (current_page == LauncherPage::Controls)
        controls_testing = !page_by_shoulder;
      shown_page = current_page;
    }
    page_by_shoulder = false;
    const bool testing_controls = current_page == LauncherPage::Controls && controls_testing;
    // While the launcher goes to black or comes back from it, nothing in it
    // answers.
    const bool fading = transition.Active(static_cast<double>(SDL_GetTicksNS()) / 1e9);
    if (fading)
      io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else
      io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    // The game selection screen reads the keys and buttons itself.
    if (testing_controls || game_select.IsOpen() || fading)
      io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    else
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (testing_controls || pad_waits_for_release || game_select.IsOpen() || fading)
      io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    else
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

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
        current_metadata = inspect_game(current_game);
        for (GameSlot& slot : game_slots)
        {
          if (current_metadata && current_metadata.metadata->disc_id == slot.game.disc_id)
          {
            slot.root = current_game;
            slot.metadata = current_metadata;
          }
        }
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
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Space) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown))
    {
      std::lock_guard lock(dialog.mutex);
      dialog.error.clear();
    }
    // The gamepad: what was used last, the buttons still held after the
    // game, and B, LB and RB.
    const bool pad_moves = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
    {
      bool pad_pressed = false;
      bool pad_down = false;
      for (int key = ImGuiKey_GamepadStart; key <= ImGuiKey_GamepadRStickDown; ++key)
      {
        pad_pressed = pad_pressed || ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false);
        pad_down = pad_down || ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
      }
      bool key_pressed = false;
      for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart && !key_pressed; ++key)
        key_pressed = ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false);
      if (pad_pressed)
        last_input = LastInput::Pad;
      else if (key_pressed)
        last_input = LastInput::Keyboard;
      else if (ImGui::IsAnyMouseDown() || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
        last_input = LastInput::Mouse;
      if (!pad_down)
        pad_waits_for_release = false;
    }
    // Escape or B in a window of ours (Netplay, the complete save's
    // question) is its Cancel. Not when it ends typing in a text box.
    const bool popup_cancel =
        !item_was_active && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                             (pad_moves && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)));
    if (testing_controls)
    {
      // Holding B (any gamepad's) for a moment, or Escape, stops the test.
      const ImGuiKeyData* back = ImGui::GetKeyData(ImGuiKey_GamepadFaceRight);
      constexpr float HOLD_TO_STOP = 0.8f;
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
          (back->Down && back->DownDuration >= HOLD_TO_STOP &&
           back->DownDurationPrev < HOLD_TO_STOP))
      {
        controls_testing = false;
        focus_sidebar = true;
        // B is still held: it moves nothing until it is let go.
        pad_waits_for_release = true;
      }
    }
    else if (!popup_was_open && !item_was_active && !viewer_was_open && !game_select.IsOpen() &&
             !fading &&
             !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
      // LB and RB, or Q and E on the keyboard: the page before or after this
      // one in the sidebar.
      int step = pad_moves ? static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) -
                                 static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) :
                             0;
      if (step == 0 && !io.WantTextInput)
        step = static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_E, false)) -
               static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_Q, false));
      if (step != 0)
      {
        const auto shown = std::ranges::find(LAUNCHER_PAGES, current_page,
                                             &std::pair<LauncherPage, const char*>::first);
        const auto index = std::distance(LAUNCHER_PAGES.begin(), shown) + step;
        if (index >= 0 && index < static_cast<std::ptrdiff_t>(LAUNCHER_PAGES.size()))
        {
          go_to_page(LAUNCHER_PAGES[static_cast<std::size_t>(index)].first);
          focus_sidebar = true;
          page_by_shoulder = true;
        }
      }
      // Y on the Controls page: test the buttons, as Test buttons does.
      if (pad_moves && step == 0 && current_page == LauncherPage::Controls &&
          controller_profile_exists && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false))
        controls_testing = true;
      // B or Escape: back to the sidebar, on the page shown.
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
          (pad_moves && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
        focus_sidebar = true;
      // Right from the sidebar, or the first move with nothing focused yet:
      // into the page, on its first control.
      const bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
                         (pad_moves && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ||
                                        ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight)));
      bool first_move = false;
      if (GImGui->NavId == 0)
      {
        for (const ImGuiKey key :
             {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow})
          first_move = first_move || ImGui::IsKeyPressed(key, false);
        if (pad_moves)
          for (const ImGuiKey key :
               {ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft,
                ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
                ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight})
            first_move = first_move || ImGui::IsKeyPressed(key, false);
      }
      if ((sidebar_was_focused && right) || first_move)
      {
        ImGui::NavMoveRequestCancel();
        enter_page = true;
      }
    }
    // The right stick scrolls the page (or the patch notes): its texts have
    // nothing for the D-pad to stop on.
    const float pad_scroll =
        pad_moves ? (ImGui::GetKeyData(ImGuiKey_GamepadRStickDown)->AnalogValue -
                     ImGui::GetKeyData(ImGuiKey_GamepadRStickUp)->AnalogValue) *
                        900.0f * scale * io.DeltaTime :
                    0.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    // With a background picture the window itself is see-through (the
    // picture is drawn under it, see RenderBackground); the panels keep
    // their own color, so text stays readable.
    if (background.texture)
      ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin(MODERNGEKKO_FRONTEND_NAME " Launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    // Top bar: the launcher's name on the left ("Launcher" without one; the
    // game's name is already in the banner), the CPU core PLAY will use on
    // the right (MODERNGEKKO_STATICRECOMP wins over config.ini, as in the
    // runner).
    ImGui::PushFont(fonts.bold, 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(MODERNGEKKO_LAUNCHER_HEADING);
    ImGui::PopFont();
    ImGui::SameLine();
#if defined(MODERNGEKKO_FRONTEND_VERSION) && defined(MODERNGEKKO_UPDATE_REPOSITORY)
    {
      const moderngekko::frontend::UpdateCheck::Status update = update_check.Get();
      if (update.state == moderngekko::frontend::UpdateCheck::State::Available)
      {
        DrawNewVersionNotice(update.latest, scale);
        ImGui::SameLine();
      }
    }
#endif
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
    // The sidebar and the page each keep the D-pad to themselves: Dear
    // ImGui's up and down would otherwise jump between them. Right goes into
    // the page, B back to the sidebar (both above).
    ImGui::BeginChild("##navigation", ImVec2(170.0f * scale, body_height));
    ImGui::PopStyleColor();
    bool sidebar_focused = false;
    for (const auto& [page, label] : LAUNCHER_PAGES)
    {
      if (DrawNavigationItem(Tr(label), current_page == page, fonts.bold, scale))
      {
        go_to_page(page);
        // Choosing Controls tests the controller, even when the page is
        // already shown (reached with LB or RB).
        if (page == LauncherPage::Controls)
          controls_testing = true;
      }
      sidebar_focused = sidebar_focused || ImGui::IsItemFocused();
      if (focus_sidebar && current_page == page)
      {
        ImGui::FocusItem();
        ImGui::SetNavCursorVisible(true);
        focus_sidebar = false;
      }
    }
    {
      const std::string previous = config.language;
      if (DrawLanguageMenu(config.language, system_language, scale))
      {
        config.resolution = resolutions[resolution_index].text;
        config.show_fps_in_title = show_fps_in_title;
        config.controller = selected_controller;
        std::string error;
        if (moderngekko::frontend::SaveConfig(user_directory, config, &error))
        {
          moderngekko::frontend::ActiveLanguage() =
              moderngekko::frontend::ResolveLanguage(config.language, system_language);
        }
        else
        {
          config.language = previous;
          std::lock_guard lock(dialog.mutex);
          dialog.error = std::move(error);
        }
      }
    }
    sidebar_focused = sidebar_focused || ImGui::IsItemFocused();
    sidebar_was_focused = sidebar_focused;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##page", ImVec2(0.0f, body_height));
    ImGui::PopStyleColor();
    if (enter_page)
    {
      // The focus goes to the page's first control (PLAY on the PLAY page).
      ImGuiWindow* page_window = ImGui::GetCurrentWindow();
      ImGui::FocusWindow(page_window);
      ImGui::NavInitWindow(page_window, true);
      ImGui::SetNavCursorVisible(true);
      enter_page = false;
    }
    if (current_page != LauncherPage::Play && pad_scroll != 0.0f)
      ImGui::SetScrollY(ImGui::GetScrollY() + pad_scroll);
    {
      if (current_page == LauncherPage::Play)
      {
        if (current_metadata)
        {
          // Two columns over the summary cards when the build has patch notes
          // (and they get at least a few lines): on the left the project
          // banner, the game's state, PLAY, the Netplay links and the
          // savestate choice; on the right the PATCH NOTES panel, from the
          // top of the page down to the cards. Without notes the left column
          // takes the whole width.
          const char* language_code =
              moderngekko::frontend::LanguageCode(moderngekko::frontend::ActiveLanguage());
          if (patch_notes_language != language_code)
          {
            patch_notes = moderngekko::frontend::ParsePatchNotes(patch_notes_text, language_code);
            patch_notes_language = language_code;
          }
          const ImVec2 columns_top = ImGui::GetCursorPos();
          const float row_width = ImGui::GetContentRegionAvail().x;
          const float notes_width = std::floor(row_width * 0.38f);
          // Down to the cards, which keep their usual gap above them.
          const ImVec2 notes_size(
              notes_width, ImGui::GetContentRegionAvail().y - SummaryTileHeight(scale) -
                               ImGui::GetStyle().ItemSpacing.y * 3.0f);
          const bool show_patch_notes =
              !patch_notes.empty() && notes_size.y >= ImGui::GetTextLineHeightWithSpacing() * 3.0f;
          const float column_width = show_patch_notes ?
                                         row_width - notes_width - ImGui::GetStyle().ItemSpacing.x :
                                         row_width;

          // The banner already shows the game's name, so no text title
          // follows it.
          if (draw_banner(column_width))
            ImGui::Spacing();
          if (!moderngekko::frontend::PlaysSeveralGames())
          {
            const std::string ready = Format(Tr("GAME READY \xC2\xB7 %s"),
                                             current_metadata.metadata->disc_id.c_str());
            DrawStatusPill(ready.c_str(), LAUNCHER_ACCENT, scale, false);
          }
          else
          {
            // Each game's state, side by side while they fit the column.
            float pills_x = 0.0f;
            for (std::size_t i = 0; i < game_slots.size(); ++i)
            {
              const GameSlot& slot = game_slots[i];
              const std::string text =
                  slot.metadata ? Format(Tr("GAME READY \xC2\xB7 %s"), slot.game.disc_id.c_str())
                                : Format(Tr("NOT INSTALLED \xC2\xB7 %s"),
                                         slot.game.disc_id.c_str());
              const float pill_width =
                  ImGui::CalcTextSize(text.c_str()).x + 20.0f * scale;
              if (i > 0 && pills_x + ImGui::GetStyle().ItemSpacing.x + pill_width <= column_width)
              {
                ImGui::SameLine();
                pills_x += ImGui::GetStyle().ItemSpacing.x;
              }
              else
              {
                pills_x = 0.0f;
              }
              DrawStatusPill(text.c_str(),
                             slot.metadata ? LAUNCHER_ACCENT : ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                             scale, false);
              ImGui::SetItemTooltip("%s", slot.game.title.c_str());
              pills_x += pill_width;
            }
          }
          ImGui::Spacing();
          ImGui::Spacing();
          // With more than one game the button opens the game selection
          // screen, so it says LAUNCH; the page keeps its PLAY name.
          if (DrawPrimaryButton(moderngekko::frontend::PlaysSeveralGames() ? Tr("LAUNCH") :
                                                                            Tr("PLAY"),
                                fonts.bold, 42 * scale, column_width))
          {
            // With more than one game, PLAY asks which one first.
            if (moderngekko::frontend::PlaysSeveralGames())
            {
              if (!open_game_select())
                ImGui::OpenPopup("##choose_game");
            }
            else if (ensure_controller())
            {
              launch_mode = LaunchMode::Solo;
              done = true;
            }
          }
          // Which game PLAY starts: one button each, the ones not extracted
          // yet greyed out.
          ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                                  ImVec2(0.5f, 0.5f));
          ImGui::SetNextWindowSize(ImVec2(460 * scale, 0.0f));
          if (ImGui::BeginPopupModal("##choose_game", nullptr,
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
          {
            ImGui::PushFont(fonts.bold, 0.0f);
            ImGui::TextUnformatted(Tr("Choose your game"));
            ImGui::PopFont();
            ImGui::Spacing();
            const float choice_width = ImGui::GetContentRegionAvail().x;
            for (const GameSlot& slot : game_slots)
            {
              ImGui::PushID(slot.game.disc_id.c_str());
              ImGui::BeginDisabled(!slot.metadata);
              const bool chosen = ImGui::Button(slot.game.title.c_str(),
                                                ImVec2(choice_width, 46 * scale));
              ImGui::EndDisabled();
              ImGui::PushTextWrapPos(0.0f);
              if (slot.metadata)
                ImGui::TextDisabled("%s", slot.game.disc_id.c_str());
              else
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                                   Tr("Not installed: extract it in Game Files."));
              ImGui::PopTextWrapPos();
              ImGui::Spacing();
              ImGui::PopID();
              if (chosen)
              {
                choose_game(slot);
                summary_stale = true;
                ImGui::CloseCurrentPopup();
                if (ensure_controller())
                {
                  launch_mode = LaunchMode::Solo;
                  done = true;
                }
              }
            }
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button(Tr("Cancel"), ImVec2(choice_width, 34 * scale)) || popup_cancel)
              ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
          }

          // Under PLAY, the Netplay links: side by side when they fit the
          // column, one under the other when a language's names are too long.
          ImGui::Spacing();
          const float link_padding = ImGui::GetStyle().FramePadding.x * 2.0f;
          const float links_width = ImGui::CalcTextSize(Tr("Host Netplay")).x +
                                    ImGui::CalcTextSize(Tr("Join Netplay")).x +
                                    ImGui::CalcTextSize("\xC2\xB7").x + link_padding * 2.0f;
          const bool stacked_links = show_patch_notes && links_width > column_width;

          // Netplay: two text buttons. Each opens a small window with the
          // connection fields and the button that starts the session.
          if (DrawLinkButton(Tr("Host Netplay")))
          {
            netplay_dialog_mode = LaunchMode::Host;
            ImGui::OpenPopup("Netplay##session");
          }
          if (!stacked_links)
          {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted("\xC2\xB7");
            ImGui::SameLine(0.0f, 0.0f);
          }
          if (DrawLinkButton(Tr("Join Netplay")))
          {
            netplay_dialog_mode = LaunchMode::Join;
            ImGui::OpenPopup("Netplay##session");
          }

          // Under the links: which savestate the next PLAY starts from, when
          // there are any.
          if (!launcher_savestates.empty())
          {
            ImGui::Spacing();
            ImGui::TextUnformatted(Tr("Start from savestate"));
            const std::string savestate_preview =
                selected_savestate >= 0 ?
                    SavestateLabel(
                        launcher_savestates[static_cast<std::size_t>(selected_savestate)],
                        selected_savestate == 0) :
                    std::string(Tr("Normal boot"));
            ImGui::SetNextItemWidth(column_width);
            if (ImGui::BeginCombo("##launch_savestate", savestate_preview.c_str()))
            {
              const bool normal_boot_selected = selected_savestate < 0;
              if (ImGui::Selectable(Tr("Normal boot"), normal_boot_selected))
                selected_savestate = -1;
              if (normal_boot_selected)
                ImGui::SetItemDefaultFocus();

              for (std::size_t i = 0; i < launcher_savestates.size(); ++i)
              {
                const std::string label = SavestateLabel(launcher_savestates[i], i == 0);
                const bool selected = selected_savestate == static_cast<int>(i);
                if (ImGui::Selectable(label.c_str(), selected))
                  selected_savestate = static_cast<int>(i);
                if (selected)
                  ImGui::SetItemDefaultFocus();
              }
              ImGui::EndCombo();
            }
            // Netplay starts every player from the same boot, so a state chosen by
            // one side would desync the session immediately. The compact page
            // says so under the pointer.
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("%s", Tr("Savestates are available for solo play only."));
          }

          // Kept in the middle of the window as it grows or shrinks with the
          // choice of Invite code or Direct IP, so its buttons stay in sight.
          ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                                  ImVec2(0.5f, 0.5f));
          ImGui::SetNextWindowSize(ImVec2(560 * scale, 0.0f));
          if (ImGui::BeginPopupModal("Netplay##session", nullptr, ImGuiWindowFlags_NoResize))
          {
            const bool hosting = netplay_dialog_mode == LaunchMode::Host;
            ImGui::PushFont(fonts.bold, 0.0f);
            ImGui::TextUnformatted(hosting ? Tr("Host a session") : Tr("Join a session"));
            ImGui::PopFont();
            ImGui::Spacing();
            // How friends find the host: an invite code from the invite server,
            // or the host's address.
            const float choice_width =
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (DrawChoiceButton(Tr("INVITE CODE"), netplay_invite_mode,
                                 ImVec2(choice_width, 34 * scale), fonts.bold, scale))
              netplay_invite_mode = true;
            ImGui::SameLine();
            if (DrawChoiceButton(Tr("DIRECT IP"), !netplay_invite_mode,
                                 ImVec2(choice_width, 34 * scale), fonts.bold, scale))
              netplay_invite_mode = false;
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            if (netplay_invite_mode)
            {
              ImGui::TextDisabled("%s", hosting ? Tr("When the room opens you get a code. Your "
                                                     "friends type it in Join Netplay, and "
                                                     "nobody sees your IP.")
                                                : Tr("Type the code the host gave you."));
            }
            else
            {
              ImGui::TextDisabled(
                  "%s", hosting ? Tr("Other players connect to this computer's address and "
                                     "UDP port. For the same network or Tailscale.")
                                : Tr("Enter the address and UDP port of the player who hosts. "
                                     "For the same network or Tailscale."));
              ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                                 Tr("Careful: an IP address identifies your connection. Don't "
                                    "show it on stream or share it in public."));
            }
            ImGui::TextDisabled("%s", Tr("Every player needs the same Sonic Riders: Source "
                                         "release and game files. Everyone plays with the "
                                         "host's CPU core."));
            if (moderngekko::frontend::PlaysSeveralGames())
              ImGui::TextDisabled("%s", Tr("Everyone chooses the same game as the host."));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            // A negative width of the widest label leaves room for all of them.
            float netplay_label_width = 0.0f;
            for (const char* label :
                 {Tr("Nickname"), netplay_invite_mode ? Tr("Invite code") : Tr("Host / IP"),
                  Tr("UDP port"), Tr("Buffer frames")})
              netplay_label_width = std::max(netplay_label_width, ImGui::CalcTextSize(label).x);
            if (moderngekko::frontend::PlaysSeveralGames())
              netplay_label_width =
                  std::max(netplay_label_width, ImGui::CalcTextSize(Tr("Game")).x);
            netplay_label_width += ImGui::GetStyle().ItemInnerSpacing.x;
            // The game the session plays: the host's choice, which everyone
            // who joins must also pick.
            if (moderngekko::frontend::PlaysSeveralGames())
            {
              const moderngekko::frontend::KnownGame* shown =
                  moderngekko::frontend::FindKnownGame(current_metadata.metadata->disc_id);
              ImGui::SetNextItemWidth(-netplay_label_width);
              if (ImGui::BeginCombo(Tr("Game"), shown ? shown->title.c_str()
                                                      : current_metadata.metadata->disc_id.c_str()))
              {
                for (const GameSlot& slot : game_slots)
                {
                  if (!slot.metadata)
                    continue;
                  const bool selected =
                      slot.game.disc_id == current_metadata.metadata->disc_id;
                  if (ImGui::Selectable(slot.game.title.c_str(), selected))
                    choose_game(slot);
                  if (selected)
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
              }
            }
            ImGui::SetNextItemWidth(-netplay_label_width);
            ImGui::InputText(Tr("Nickname"), netplay_nickname.data(), netplay_nickname.size());
            if (!hosting && netplay_invite_mode)
            {
              DrawHiddenField("##netplay_code", netplay_code.data(), netplay_code.size(),
                              Tr("Invite code"), netplay_label_width, &show_netplay_code);
            }
            else if (!hosting &&
                     DrawHiddenField("##netplay_address", netplay_address.data(),
                                     netplay_address.size(), Tr("Host / IP"),
                                     netplay_label_width, &show_netplay_address))
            {
              split_netplay_address();
            }
            if (!netplay_invite_mode)
            {
              ImGui::SetNextItemWidth(150 * scale);
              ImGui::InputInt(Tr("UDP port"), &netplay_port);
              netplay_port = std::clamp(netplay_port, 1, 65535);
            }
            ImGui::Checkbox(Tr("Automatic input buffer"), &automatic_buffer);
            if (!automatic_buffer)
            {
              ImGui::SetNextItemWidth(-netplay_label_width);
              ImGui::SliderInt(Tr("Buffer frames"), &manual_buffer, 1, 20);
            }
#if defined(_WIN32)
            // Windows blocks a program that waits for connections until the
            // player allows it, and its question is easy to miss or to answer
            // for home networks only: the host can let the game through here.
            if (hosting)
            {
              using moderngekko::frontend::FirewallResult;
              const FirewallResult firewall = firewall_rule.Result();
              ImGui::Spacing();
              ImGui::PushTextWrapPos(0.0f);
              ImGui::TextDisabled("%s", Tr("If your friends can't connect, the Windows firewall may "
                                           "be blocking the game. Windows asks for permission "
                                           "to change it."));
              ImGui::BeginDisabled(firewall == FirewallResult::Working);
              ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
              const bool allow = ImGui::Button(Tr("Allow the game in the Windows firewall"));
              ImGui::PopStyleColor();
              ImGui::EndDisabled();
              if (allow)
                firewall_rule.Allow(SiblingRunner(argv[0]),
                                    std::string(MODERNGEKKO_FRONTEND_NAME) + " Netplay");
              switch (firewall)
              {
              case FirewallResult::Working:
                ImGui::TextDisabled("%s", Tr("Waiting for Windows..."));
                break;
              case FirewallResult::Allowed:
                ImGui::TextColored(LAUNCHER_ACCENT, "%s",
                                   Tr("Done: Windows lets Netplay connections in."));
                break;
              case FirewallResult::Declined:
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                                   Tr("Windows did not get permission: nothing changed."));
                break;
              case FirewallResult::Failed:
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                                   Tr("Windows could not change its firewall. The README says "
                                      "how to allow the game by hand."));
                break;
              default:
                break;
              }
              ImGui::PopTextWrapPos();
            }
#elif defined(__linux__)
            // Some systems' firewall (firewalld on Fedora, Bazzite...) lets no
            // connection in on the host's port until it is opened.
            if (hosting && !netplay_invite_mode)
            {
              ImGui::Spacing();
              ImGui::PushTextWrapPos(0.0f);
              ImGui::TextDisabled("%s", Format(Tr("If your friends can't connect, your firewall "
                                                  "may be blocking UDP port %d: the README says "
                                                  "how to open it."),
                                               netplay_port)
                                            .c_str());
              ImGui::PopTextWrapPos();
            }
#endif
            // Joining by code needs the whole code before Join does anything.
            const auto typed_code = moderngekko::frontend::NormalizeInviteCode(netplay_code.data());
            const bool code_missing = !hosting && netplay_invite_mode && !typed_code;
            if (code_missing && netplay_code[0] != '\0')
            {
              ImGui::PushTextWrapPos(0.0f);
              ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                                 Tr("An invite code has 8 characters (numbers and letters "
                                    "a-f)."));
              ImGui::PopTextWrapPos();
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
            ImGui::BeginDisabled(code_missing);
            const bool start = ImGui::Button(hosting ? Tr("Host") : Tr("Join"),
                                             ImVec2(button_width, 38 * scale));
            ImGui::EndDisabled();
            ImGui::PopStyleColor(4);
            if (start)
            {
              if (!hosting && !netplay_invite_mode)
                split_netplay_address();
              netplay_join_code = typed_code.value_or("");
              if (ensure_controller())
              {
                config.netplay_nickname = netplay_nickname.data();
                config.netplay_address = netplay_address.data();
                config.netplay_port = static_cast<std::uint16_t>(netplay_port);
                config.netplay_buffer = automatic_buffer ? "auto" : std::to_string(manual_buffer);
                config.netplay_connection = netplay_invite_mode ? "code" : "ip";
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
            if (ImGui::Button(Tr("Cancel"), ImVec2(button_width, 38 * scale)) || popup_cancel)
              ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
          }

          if (show_patch_notes)
          {
            // The cards go under whichever column ends lower.
            const float left_bottom = ImGui::GetCursorPosY();
            ImGui::SetCursorPos(ImVec2(columns_top.x + row_width - notes_width, columns_top.y));
            DrawPatchNotes(patch_notes, fonts.bold, notes_size, pad_scroll);
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), left_bottom));
          }

          // Summary cards along the bottom of the page: how long the game has
          // been played and what the next PLAY will use.
          if (summary_stale)
          {
            summary_time_played = 0;
            if (moderngekko::frontend::PlaysSeveralGames())
            {
              for (const GameSlot& slot : game_slots)
                summary_time_played +=
                    moderngekko::frontend::ReadTimePlayed(user_directory, slot.game.disc_id);
            }
            else
            {
              summary_time_played = moderngekko::frontend::ReadTimePlayed(
                  user_directory, current_metadata.metadata->disc_id);
            }
            summary_installed_mods = moderngekko::frontend::CountInstalledMods(mod_folders);
            summary_stale = false;
          }
          PlaySummary summary;
          summary.time_played_ms = summary_time_played;
          if (controller_index >= 0)
          {
            summary.controller = controllers[controller_index].label;
            summary.controller_icon =
                DeviceIconOf(SDL_GetGamepadTypeForID(controllers[controller_index].joystick_id));
          }
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
          summary.keyboard_only =
              controller_index < 0 && selected_controller == keyboard_device;
          summary.keys_without_pad = true;
          if (controller_index < 0)
            summary.controller_icon = DeviceIcon::Keyboard;
#endif
          summary.aspect_ratio = config.aspect_ratio;
          summary.custom_aspect = config.custom_aspect;
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
          case 3:
            open_mods_page();
            break;
          default:
            break;
          }
        }
        else
        {
          if (draw_banner(ImGui::GetContentRegionAvail().x))
            ImGui::Spacing();
          DrawStatusPill(Tr("GAME NOT INSTALLED"), ImVec4(1.0f, 0.75f, 0.3f, 1.0f), scale, false);
          ImGui::Spacing();
          ImGui::TextUnformatted(Tr("No extracted game is configured yet."));
          ImGui::TextDisabled("%s", Tr("Open Game Files to select and extract your game."));
          ImGui::Spacing();
          if (ImGui::Button(Tr("Go to Game Files")))
            current_page = LauncherPage::GameFiles;
        }
      }
      if (current_page == LauncherPage::Settings)
      {
    DrawPageHeader(Tr("Settings"),
                   Tr("Changes are saved right away and apply the next time you press PLAY."),
                   fonts.bold);
    // Two columns of about the same height, so the page fits the window: the
    // CPU core and the game window on the left, the picture and the sound on
    // the right.
    const bool settings_columns = BeginPageColumns("##settings_columns");
    BeginSection("##settings_cpu", Tr("CPU CORE"));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##cpu_core", Tr(cpu_cores[cpu_core_index].text)))
    {
      for (std::size_t i = 0; i < cpu_cores.size(); ++i)
      {
        const bool selected = cpu_core_index == static_cast<int>(i);
        if (ImGui::Selectable(Tr(cpu_cores[i].text), selected))
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
    // What the cores are, under the pointer, so the page fits the window.
    ImGui::SetItemTooltip("%s\n%s",
                          Tr("JIT64 is the validated full-speed mode. StaticRecomp runs the "
                             "recompiled game code and is still being optimized."),
                          Tr("Netplay: everyone plays with the host's CPU core."));
    if (!cpu_core_override.empty())
    {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                         Format(Tr("MODERNGEKKO_STATICRECOMP=%s is set in the environment and "
                                   "overrides this setting."),
                                cpu_core_override.c_str())
                             .c_str());
      ImGui::PopTextWrapPos();
    }
    EndSection();
    BeginSection("##settings_display", Tr("DISPLAY"));
    // The section's labels share a column, so the lists next to them line up.
    const float display_label_x = ImGui::GetCursorPosX();
    float display_label_width = ImGui::CalcTextSize(Tr("Aspect ratio")).x;
    if (!game_borders.borders.empty())
      display_label_width =
          std::max(display_label_width, ImGui::CalcTextSize(Tr("4:3 borders")).x);
    display_label_width = std::max(display_label_width, ImGui::CalcTextSize(Tr("Window size")).x);
    display_label_width = std::max(display_label_width, ImGui::CalcTextSize(Tr("Shape")).x);
    const float display_list_x = display_label_x + display_label_width + 16.0f * scale;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr("Aspect ratio"));
    ImGui::SameLine(display_list_x);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##aspect_ratio", Tr(aspect_ratios[aspect_ratio_index].text)))
    {
      for (std::size_t i = 0; i < aspect_ratios.size(); ++i)
      {
        const bool selected = aspect_ratio_index == static_cast<int>(i);
        if (ImGui::Selectable(Tr(aspect_ratios[i].text), selected))
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
        DimWrappedText(Tr(aspect_ratios[i].description));
        ImGui::Unindent(12.0f * scale);
      }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s",
                          Tr("Netplay: the host's 16:9 or 4:3 choice applies to every player."));
    // Custom: the shape's width and height, typed or picked from the usual
    // ones. Saved as soon as they make a shape from 1:1 to 6:1.
    if (config.aspect_ratio == "custom")
    {
      const auto save_custom_aspect = [&](moderngekko::frontend::CustomAspect shape) {
        const std::string text = std::to_string(shape.width) + ':' + std::to_string(shape.height);
        custom_aspect_refused = !moderngekko::frontend::ParseCustomAspect(text);
        if (custom_aspect_refused)
          return;
        const std::string previous = config.custom_aspect;
        config.custom_aspect = text;
        config.resolution = resolutions[resolution_index].text;
        config.show_fps_in_title = show_fps_in_title;
        config.controller = selected_controller;
        std::string error;
        if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
        {
          config.custom_aspect = previous;
          std::lock_guard lock(dialog.mutex);
          dialog.error = std::move(error);
        }
      };
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(Tr("Shape"));
      ImGui::SameLine(display_list_x);
      const float number_width = ImGui::CalcTextSize("0000").x + ImGui::GetStyle().FramePadding.x * 2;
      ImGui::SetNextItemWidth(number_width);
      ImGui::InputInt("##custom_aspect_width", &custom_aspect.width, 0, 0);
      const bool width_done = ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SameLine(0.0f, 4.0f * scale);
      ImGui::TextUnformatted(":");
      ImGui::SameLine(0.0f, 4.0f * scale);
      ImGui::SetNextItemWidth(number_width);
      ImGui::InputInt("##custom_aspect_height", &custom_aspect.height, 0, 0);
      if (width_done || ImGui::IsItemDeactivatedAfterEdit())
        save_custom_aspect(custom_aspect);
      // The usual shapes, one click each, under the numbers and lined up with
      // them (on more lines when the column is narrow); the one in use in the
      // accent colour.
      const float presets_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
      bool first_preset = true;
      for (const auto& preset : moderngekko::frontend::CustomAspectPresets())
      {
        const std::string label = std::to_string(preset.width) + ':' + std::to_string(preset.height);
        const float button_width =
            ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (!first_preset)
          ImGui::SameLine();
        if (first_preset || ImGui::GetCursorScreenPos().x + button_width > presets_right)
        {
          if (!first_preset)
            ImGui::NewLine();
          ImGui::SetCursorPosX(display_list_x);
        }
        first_preset = false;
        const bool current =
            custom_aspect.width == preset.width && custom_aspect.height == preset.height;
        if (current)
          ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
        if (ImGui::SmallButton((label + "##custom_aspect_preset").c_str()))
        {
          custom_aspect = preset;
          save_custom_aspect(preset);
        }
        if (current)
          ImGui::PopStyleColor();
      }
      ImGui::PushTextWrapPos(0.0f);
      if (custom_aspect_refused)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                           Tr("Use whole numbers up to 100, from 1:1 to 6:1 (as wide as it is "
                              "tall, up to six times wider). The game keeps the last shape that "
                              "was right."));
      else
        ImGui::TextDisabled("%s", Tr("In a window of another shape, bars fill the rest. In "
                                     "fullscreen, choose your screen's shape."));
      ImGui::PopTextWrapPos();
    }
    if (!game_borders.borders.empty())
    {
      // None, Random (one picked each time the game opens) or one of the
      // build's borders, by the name in the launcher's language. A border
      // config.ini names but the build lacks shows none, and so says None.
      const char* border_language =
          moderngekko::frontend::LanguageCode(moderngekko::frontend::ActiveLanguage());
      const std::string shown_border =
          config.game_border == "random" || game_borders.Find(config.game_border) ?
              config.game_border :
              std::string("none");
      const auto border_name = [&](std::string_view setting) -> std::string {
        if (setting == "random")
          return Tr("Random");
        if (const auto* border = game_borders.Find(setting))
          return border->Name(border_language);
        return Tr("None");
      };
      const auto choose_border = [&](const std::string& setting) {
        const std::string previous = config.game_border;
        config.game_border = setting;
        config.resolution = resolutions[resolution_index].text;
        config.show_fps_in_title = show_fps_in_title;
        config.controller = selected_controller;
        std::string error;
        if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
        {
          config.game_border = previous;
          std::lock_guard lock(dialog.mutex);
          dialog.error = std::move(error);
        }
      };
      const auto describe_border = [&](const char* text) {
        ImGui::Indent(12.0f * scale);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(12.0f * scale);
      };
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(Tr("4:3 borders"));
      ImGui::SameLine(display_list_x);
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##game_border", border_name(shown_border).c_str()))
      {
        if (ImGui::Selectable(Tr("None"), shown_border == "none"))
          choose_border("none");
        if (shown_border == "none")
          ImGui::SetItemDefaultFocus();
        describe_border(Tr("Black bars beside the game."));
        if (ImGui::Selectable(Tr("Random"), shown_border == "random"))
          choose_border("random");
        if (shown_border == "random")
          ImGui::SetItemDefaultFocus();
        describe_border(Tr("One picked at random each time the game opens."));
        for (const auto& border : game_borders.borders)
        {
          const bool selected = shown_border == border.id;
          ImGui::PushID(border.id.c_str());
          if (ImGui::Selectable(border.Name(border_language).c_str(), selected))
            choose_border(border.id);
          ImGui::PopID();
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ImGui::SetItemTooltip("%s", Tr("Pictures beside the game when it is shown in 4:3 on a "
                                     "wider screen. In Netplay too, only you see your choice."));
    }
    {
      // The game window's size out of fullscreen. A size config.ini has
      // that the list lacks shows as written.
      const auto& window_sizes = moderngekko::frontend::SupportedWindowSizes();
      const moderngekko::frontend::WindowSizeOption* shown_size = nullptr;
      for (const auto& option : window_sizes)
      {
        if (config.window_size == option.value)
          shown_size = &option;
      }
      const std::string shown_size_text =
          shown_size ? std::string(Tr(shown_size->text)) :
          moderngekko::frontend::WindowSizeOf(config.window_size).width > 0 ?
                       config.window_size :
                       std::string(Tr("Automatic"));
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(Tr("Window size"));
      ImGui::SameLine(display_list_x);
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##window_size", shown_size_text.c_str()))
      {
        for (const auto& option : window_sizes)
        {
          const bool selected = shown_size == &option;
          if (ImGui::Selectable(Tr(option.text), selected))
          {
            const std::string previous = config.window_size;
            config.window_size = option.value;
            config.resolution = resolutions[resolution_index].text;
            config.show_fps_in_title = show_fps_in_title;
            config.controller = selected_controller;
            std::string error;
            if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
            {
              config.window_size = previous;
              std::lock_guard lock(dialog.mutex);
              dialog.error = std::move(error);
            }
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
          if (*option.description)
          {
            ImGui::Indent(12.0f * scale);
            ImGui::TextDisabled("%s", Tr(option.description));
            ImGui::Unindent(12.0f * scale);
          }
        }
        ImGui::EndCombo();
      }
      ImGui::SetItemTooltip("%s", Tr("The game window's size when it isn't fullscreen. How sharp "
                                     "the game looks is the internal resolution, in Graphics."));
    }
    ImGui::Spacing();
    const bool previous_fullscreen = fullscreen;
    if (ImGui::Checkbox(Tr("Start in fullscreen"), &fullscreen))
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
    // Beside it when both fit the column, under it otherwise.
    {
      const float fps_width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                              ImGui::CalcTextSize(Tr("Show FPS in window title")).x;
      ImGui::SameLine(0.0f, 24.0f * scale);
      if (ImGui::GetContentRegionAvail().x < fps_width)
        ImGui::NewLine();
    }
    const bool previous_show_fps_in_title = show_fps_in_title;
    if (ImGui::Checkbox(Tr("Show FPS in window title"), &show_fps_in_title))
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
    if (settings_columns)
      NextPageColumn();
    BeginSection("##settings_graphics", Tr("GRAPHICS"));
    // Each list beside its label, the lists lined up as in DISPLAY.
    float graphics_label_width = 0.0f;
    for (const char* label : {Tr("Graphics backend"), Tr("Internal resolution"),
                              Tr("Screenshot resolution"), Tr("Visual effect")})
      graphics_label_width = std::max(graphics_label_width, ImGui::CalcTextSize(label).x);
    const float graphics_list_x = ImGui::GetCursorPosX() + graphics_label_width + 16.0f * scale;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr("Graphics backend"));
    ImGui::SameLine(graphics_list_x);
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
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr("Internal resolution"));
    ImGui::SameLine(graphics_list_x);
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
    // What screenshots (F9, or the in-game menu's Take screenshot) are
    // taken at: the game is drawn at it just for them.
    const moderngekko::frontend::ScreenshotResolutionOption& shown_shot =
        moderngekko::frontend::FindScreenshotResolution(config.screenshot_resolution);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr("Screenshot resolution"));
    ImGui::SameLine(graphics_list_x);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##screenshot_resolution", Tr(shown_shot.text)))
    {
      for (const auto& option : moderngekko::frontend::SupportedScreenshotResolutions())
      {
        const bool selected = &shown_shot == &option;
        if (ImGui::Selectable(Tr(option.text), selected))
        {
          const std::string previous = config.screenshot_resolution;
          config.screenshot_resolution = option.value;
          config.resolution = resolutions[resolution_index].text;
          config.show_fps_in_title = show_fps_in_title;
          config.controller = selected_controller;
          std::string error;
          if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
          {
            config.screenshot_resolution = previous;
            std::lock_guard lock(dialog.mutex);
            dialog.error = std::move(error);
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", Tr("F9 or Take screenshot in the in-game menu. The game is drawn "
                                   "at this resolution just for the picture, never below its "
                                   "own."));
    // The picture effect (also in the in-game menu's Quick settings). An
    // effect config.ini names but the build lacks shows as None.
    const auto& effects = moderngekko::frontend::SupportedEffects();
    const moderngekko::frontend::EffectOption* shown_effect =
        moderngekko::frontend::FindEffect(config.effect);
    if (!shown_effect)
      shown_effect = &effects.front();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr("Visual effect"));
    ImGui::SameLine(graphics_list_x);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##effect", Tr(shown_effect->text)))
    {
      for (const auto& effect : effects)
      {
        const bool selected = shown_effect == &effect;
        if (ImGui::Selectable(Tr(effect.text), selected))
        {
          const std::string previous = config.effect;
          config.effect = effect.value;
          config.resolution = resolutions[resolution_index].text;
          config.show_fps_in_title = show_fps_in_title;
          config.controller = selected_controller;
          std::string error;
          if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
          {
            config.effect = previous;
            std::lock_guard lock(dialog.mutex);
            dialog.error = std::move(error);
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
        ImGui::Indent(12.0f * scale);
        DimWrappedText(Tr(effect.description));
        ImGui::Unindent(12.0f * scale);
      }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", Tr("Drawn over the game's picture. Light enough for integrated "
                                   "graphics."));
    EndSection();
    BeginSection("##settings_audio", Tr("AUDIO"));
    // The game's own volume (also in the in-game menu's Quick settings),
    // written when the slider is let go. The launcher's music has its own,
    // below it. Each slider beside its label, lined up as in the sections
    // above.
    float audio_label_width = ImGui::CalcTextSize(Tr("Game volume")).x;
    if (has_music)
      audio_label_width =
          std::max(audio_label_width, ImGui::CalcTextSize(Tr("Launcher and lobby music")).x);
    const float audio_slider_x = ImGui::GetCursorPosX() + audio_label_width + 16.0f * scale;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr("Game volume"));
    ImGui::SameLine(audio_slider_x);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##game_volume", &config.game_volume, 0, 100, "%d %%",
                     ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
      config.resolution = resolutions[resolution_index].text;
      config.show_fps_in_title = show_fps_in_title;
      config.controller = selected_controller;
      std::string error;
      if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
      {
        std::lock_guard lock(dialog.mutex);
        dialog.error = std::move(error);
      }
    }
    ImGui::SetItemTooltip("%s", Tr("Also in the in-game menu's Quick settings."));
    if (has_music)
    {
      // The music of the launcher and the Netplay lobby: heard as it moves,
      // written when the slider is let go.
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(Tr("Launcher and lobby music"));
      ImGui::SameLine(audio_slider_x);
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::SliderInt("##music_volume", &config.music_volume, 0, 100,
                           config.music_volume == 0 ? Tr("Off") : "%d %%",
                           ImGuiSliderFlags_AlwaysClamp))
        music.SetVolume(config.music_volume);
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
        config.resolution = resolutions[resolution_index].text;
        config.show_fps_in_title = show_fps_in_title;
        config.controller = selected_controller;
        std::string error;
        if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
        {
          std::lock_guard lock(dialog.mutex);
          dialog.error = std::move(error);
        }
      }
    }
    EndSection();
    if (settings_columns)
      EndPageColumns();
    }
      if (current_page == LauncherPage::Controls)
      {
    DrawPageHeader(Tr("Controls"),
                   Tr("Choose the controller to play with. The launcher writes its button "
                      "mapping for you."),
                   fonts.bold);
    BeginSection("##controls_controller", Tr("CONTROLLER"));
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
    const bool keyboard_only = controller_index < 0 && selected_controller == keyboard_device;
#else
    const bool keyboard_only = false;
#endif
    // One row: the controller's state, the list and the button that writes a
    // new profile for it (under the list when they don't all fit).
    const float controller_row_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(controller_row_y +
                         std::max(0.0f, (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight() -
                                         6.0f * scale) *
                                            0.5f));
    if (controller_index >= 0)
      DrawStatusPill(Tr("CONNECTED"), LAUNCHER_ACCENT, scale, false);
    else if (keyboard_only)
      DrawStatusPill(Tr("KEYBOARD ONLY"), LAUNCHER_ACCENT, scale, false);
    else
      DrawStatusPill(Tr("NOT DETECTED"), ImVec4(1.0f, 0.75f, 0.3f, 1.0f), scale, false);
    ImGui::SameLine();
    ImGui::SetCursorPosY(controller_row_y);
    const char* replace_label = Tr(REPLACE_PROFILE_BUTTON);
    const float replace_width =
        ImGui::CalcTextSize(replace_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const bool replace_beside = ImGui::GetContentRegionAvail().x - replace_width -
                                    ImGui::GetStyle().ItemSpacing.x >=
                                200.0f * scale;
    ImGui::SetNextItemWidth(replace_beside ? -(replace_width + ImGui::GetStyle().ItemSpacing.x) :
                                             -1.0f);
    const char* controller_preview = controller_index >= 0
                                         ? controllers[controller_index].label.c_str()
                                     : keyboard_only ? Tr("Keyboard only")
                                     : selected_controller.empty() ? Tr("No SDL gamepad detected")
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
            controller_status = {MODERNGEKKO_UI_TEXT("Ready to generate controller profile")};
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (replace_beside)
      ImGui::SameLine();
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
    // Without a gamepad this writes the keyboard-only profile.
    ImGui::BeginDisabled(false);
#else
    ImGui::BeginDisabled(controller_index < 0);
#endif
    if (ImGui::Button(replace_label))
    {
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
      if (controller_index < 0)
        select_keyboard_only();
      else
#endif
        select_controller(controller_index);
    }
    ImGui::EndDisabled();
    // What the last change did is in the footer, always in sight.
    EndSection();
    // The bottom of the page, which the tester's drawing grows down to.
    const float page_bottom = ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y;
    BeginSection("##controls_mapping", Tr("BUTTON MAPPING"));
    // The heading's line, where the tester says how to use it while it tests
    // the controller, and has Test buttons to start that again otherwise.
    const TesterHintLine hint_line{ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x,
                                   ImGui::GetCursorScreenPos().x +
                                       ImGui::GetContentRegionAvail().x};
    if (!controls_testing && controller_profile_exists)
    {
      const ImVec2 cursor = ImGui::GetCursorScreenPos();
      const char* test_label = Tr("Test buttons");
      ImGui::SetCursorScreenPos(ImVec2(hint_line.right - ImGui::CalcTextSize(test_label).x -
                                           ImGui::GetStyle().FramePadding.x * 2.0f,
                                       hint_line.top));
      if (ImGui::SmallButton(test_label))
        controls_testing = true;
      ImGui::SetCursorScreenPos(cursor);
    }
    ImGui::PushTextWrapPos(0.0f);
    if (controller_profile_exists)
      ImGui::TextDisabled("%s", Format(Tr("Using %s from your user folder."),
                                       std::string(CONTROLLER_PROFILE_NAME).c_str())
                                    .c_str());
    else
      ImGui::TextDisabled("%s", Format(Tr("%s is created the first time you play."),
                                       std::string(CONTROLLER_PROFILE_NAME).c_str())
                                    .c_str());
    ImGui::PopTextWrapPos();
    // The tester starts on the line above, where it puts its tabs.
    if (controller_profile_exists)
      DrawControllerTester(controller_tester, controller_art, keyboard_art, fonts.bold, scale,
                           page_bottom, hint_line, controls_testing);
    EndSection();
    }
      if (current_page == LauncherPage::GameFiles)
      {
    DrawPageHeader(Tr("Game Files"),
                   Tr("Use your own copy of the game as an ISO, WBFS or RVZ image. It is "
                      "extracted once and reused afterwards."),
                   fonts.bold);
    // The game and its disc image on the left, the complete save on the
    // right when the build brings one.
#ifdef MODERNGEKKO_REQUIRED_DISC_ID
    const bool files_columns = has_complete_save && BeginPageColumns("##files_columns");
#else
    const bool files_columns = false;
#endif
    if (moderngekko::frontend::PlaysSeveralGames())
    {
      // Each game of the build: ready with its folder, or not extracted yet.
      // The disc image below is either one; the launcher sees which.
      BeginSection("##files_installed", Tr("INSTALLED GAMES"));
      for (std::size_t i = 0; i < game_slots.size(); ++i)
      {
        const GameSlot& slot = game_slots[i];
        if (i > 0)
          ImGui::Spacing();
        ImGui::PushFont(fonts.bold, 0.0f);
        ImGui::TextUnformatted(slot.game.title.c_str());
        ImGui::PopFont();
        const std::string state =
            slot.metadata ? Format(Tr("GAME READY \xC2\xB7 %s"), slot.game.disc_id.c_str())
                          : Format(Tr("NOT INSTALLED \xC2\xB7 %s"), slot.game.disc_id.c_str());
        DrawStatusPill(state.c_str(),
                       slot.metadata ? LAUNCHER_ACCENT : ImVec4(1.0f, 0.75f, 0.3f, 1.0f), scale,
                       false);
        ImGui::PushTextWrapPos(0.0f);
        if (slot.metadata)
          ImGui::TextDisabled("%s", slot.root.string().c_str());
        else
          ImGui::TextDisabled("%s", Tr("Select its disc image below and extract it."));
        ImGui::PopTextWrapPos();
      }
      EndSection();
    }
    else
    {
    BeginSection("##files_installed", Tr("INSTALLED GAME"));
    if (current_metadata)
    {
      const std::string ready = Format(Tr("GAME READY \xC2\xB7 %s"),
                                       current_metadata.metadata->disc_id.c_str());
      DrawStatusPill(ready.c_str(), LAUNCHER_ACCENT, scale, false);
      ImGui::Spacing();
      ImGui::TextUnformatted(current_metadata.metadata->game_name.c_str());
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", current_game.string().c_str());
      ImGui::PopTextWrapPos();
    }
    else
    {
      DrawStatusPill(Tr("NOT INSTALLED"), ImVec4(1.0f, 0.75f, 0.3f, 1.0f), scale, false);
      ImGui::Spacing();
      ImGui::TextDisabled("%s", Tr("Select a disc image below and extract it."));
    }
    EndSection();
    }
    BeginSection("##files_image", Tr("DISC IMAGE"));
    if (selected_image)
      ImGui::TextWrapped("%s", selected_image->string().c_str());
    else
      ImGui::TextDisabled("%s", Tr("No ISO, WBFS, or RVZ selected"));
    ImGui::Spacing();
    if (!extraction.running)
    {
      if (ImGui::Button(Tr("Browse for ISO / WBFS / RVZ")))
      {
        // Static: SDL reads the filters until the dialog closes. Tr() returns
        // texts that live as long as the program.
        static SDL_DialogFileFilter filters[] = {
            {"", "iso;wbfs;rvz"}, {"ISO", "iso"}, {"WBFS", "wbfs"}, {"RVZ", "rvz"}};
        filters[0].name = Tr("Disc images");
        const std::string documents = DocumentsDirectory().string();
        SDL_ShowOpenFileDialog(FileDialogCallback, &dialog, window, filters,
                               static_cast<int>(std::size(filters)), documents.c_str(), false);
      }
      if (selected_image)
      {
        ImGui::SameLine();
        if (ImGui::Button(Tr("Extract Game")))
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
#ifdef MODERNGEKKO_REQUIRED_DISC_ID
    if (has_complete_save)
    {
      if (files_columns)
        NextPageColumn();
      BeginSection("##files_complete_save", Tr("COMPLETE SAVE"));
      if (save_checked_at == 0 || SDL_GetTicks() - save_checked_at >= 1000)
      {
        for (CompleteSave& save : complete_saves)
        {
          save.game_save_exists =
              save.gamecube ?
                  moderngekko::frontend::HasGameCubeSave(save.complete_folder, save.save_folder) :
                  moderngekko::frontend::HasGameSave(save.save_folder);
          save.latest_backup = moderngekko::frontend::LatestSaveBackup(save.backups);
        }
        save_checked_at = std::max<Uint64>(SDL_GetTicks(), 1);
      }
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextUnformatted(Tr("The whole game completed, with everything unlocked. Your own "
                                "save is kept, and Restore my save brings it back."));
      ImGui::PopTextWrapPos();
      // Both ask first: one replaces the player's progress, the other the
      // complete save.
      const auto confirm = [&](const char* id, const char* heading, const char* body,
                               const char* action) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(460 * scale, 0.0f));
        bool confirmed = false;
        if (ImGui::BeginPopupModal(id, nullptr,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        {
          ImGui::PushFont(fonts.bold, 0.0f);
          ImGui::TextUnformatted(heading);
          ImGui::PopFont();
          ImGui::Spacing();
          ImGui::PushTextWrapPos(0.0f);
          ImGui::TextUnformatted(body);
          ImGui::PopTextWrapPos();
          ImGui::Spacing();
          if (ImGui::Button(action))
          {
            confirmed = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button(Tr("Cancel")) || popup_cancel)
            ImGui::CloseCurrentPopup();
          ImGui::EndPopup();
        }
        return confirmed;
      };
      // Each game's, under its name when the build plays more than one.
      for (CompleteSave& save : complete_saves)
      {
        ImGui::PushID(save.disc_id.c_str());
        ImGui::Spacing();
        if (moderngekko::frontend::PlaysSeveralGames())
        {
          ImGui::PushFont(fonts.bold, 0.0f);
          ImGui::TextUnformatted(save.title.c_str());
          ImGui::PopFont();
        }
        if (!save.game_save_exists)
        {
          ImGui::PushTextWrapPos(0.0f);
          ImGui::TextDisabled("%s",
                              Tr("Play once, until the game makes its save, then come back."));
          ImGui::PopTextWrapPos();
        }
        ImGui::BeginDisabled(!save.game_save_exists);
        if (ImGui::Button(Tr("Install complete save")))
          ImGui::OpenPopup("##install_complete_save");
        ImGui::EndDisabled();
        if (save.latest_backup)
        {
          ImGui::SameLine();
          if (ImGui::Button(Tr("Restore my save")))
            ImGui::OpenPopup("##restore_save");
        }
        if (save.message)
        {
          const std::string text = Format(Tr(save.message), save.message_detail.c_str());
          ImGui::PushTextWrapPos(0.0f);
          if (save.message_error)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", text.c_str());
          else
            ImGui::TextDisabled("%s", text.c_str());
          ImGui::PopTextWrapPos();
        }
        if (confirm("##install_complete_save", Tr("Install the complete save?"),
                    Tr("Your progress is replaced by the complete save. A copy of your save is "
                       "kept first, and Restore my save brings it back."),
                    Tr("Install")))
        {
          const SDL_Time now_ns = [] {
            SDL_Time now = 0;
            SDL_GetCurrentTime(&now);
            return now;
          }();
          SDL_DateTime now{};
          SDL_TimeToDateTime(now_ns, &now, true);
          char stamp[32];
          std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d", now.year,
                        now.month, now.day, now.hour, now.minute, now.second);
          const moderngekko::frontend::SaveResult result =
              save.gamecube ?
                  moderngekko::frontend::InstallCompleteGameCubeSave(
                      save.complete_folder, save.save_folder, save.backups, stamp) :
                  moderngekko::frontend::InstallCompleteSave(save.complete_folder,
                                                             save.save_folder, save.backups,
                                                             stamp);
          save.message_error = !result;
          switch (result.error)
          {
          case moderngekko::frontend::SaveError::None:
            save.message =
                MODERNGEKKO_UI_TEXT("Complete save installed. Your own save is kept in %s.");
            save.message_detail = result.backup.string();
            break;
          case moderngekko::frontend::SaveError::Backup:
            save.message =
                MODERNGEKKO_UI_TEXT("Your save couldn't be copied, so nothing changed: %s");
            save.message_detail = result.detail;
            break;
          default:
            save.message = MODERNGEKKO_UI_TEXT("Couldn't write the save: %s");
            save.message_detail = result.detail;
            break;
          }
          save_checked_at = 0;
        }
        if (confirm("##restore_save", Tr("Bring your save back?"),
                    Tr("Your save from before the complete save comes back, in place of the "
                       "current one."),
                    Tr("Restore")) &&
            save.latest_backup)
        {
          const moderngekko::frontend::SaveResult result =
              save.gamecube ?
                  moderngekko::frontend::RestoreGameCubeSaveBackup(*save.latest_backup,
                                                                   save.save_folder) :
                  moderngekko::frontend::RestoreSaveBackup(*save.latest_backup,
                                                           save.save_folder);
          save.message_error = !result;
          save.message = result ? MODERNGEKKO_UI_TEXT("Your save is back.") :
                                  MODERNGEKKO_UI_TEXT("Couldn't write the save: %s");
          save.message_detail = result.detail;
          save_checked_at = 0;
        }
        ImGui::PopID();
      }
      EndSection();
    }
#endif
    if (files_columns)
      EndPageColumns();
    }
      if (current_page == LauncherPage::Mods)
      {
    DrawPageHeader(Tr("Mods & expansions"),
                   Tr("Mods change the game or add to it when it starts, without touching your "
                      "copy of it."),
                   fonts.bold);
    // What is installed on the left, what is planned on the right. A square
    // or upright EXPANSIONS picture goes beside its text, so its column is
    // the wider one.
    const float mods_page_bottom =
        ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y;
    const bool mods_art_upright = mods_art.texture && mods_art.aspect >= 0.75f;
    const bool mods_columns =
        BeginPageColumns("##mods_columns", mods_art_upright ? 0.42f : 0.48f);
    BeginSection("##mods_installed", Tr("INSTALLED MODS"));
    if (installed_mods.empty())
    {
      ImGui::TextUnformatted(Tr("No mods installed yet."));
    }
    else
    {
      for (const fs::path& mod : installed_mods)
      {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(moderngekko::frontend::ModPackageName(mod).c_str());
        ImGui::SetItemTooltip("%s", moderngekko::frontend::PathText(mod).c_str());
      }
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", Tr("Mod packages (.mgm) go in this folder and load when the game "
                                 "starts:"));
    ImGui::TextUnformatted(moderngekko::frontend::PathText(user_directory / "Mods").c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(Tr("Open folder")))
      OpenFolder(user_directory / "Mods");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", Tr("Mod support is experimental: there are no mods made for "
                                 "Sonic Riders: Source yet."));
    ImGui::PopTextWrapPos();
    EndSection();
    if (mods_columns)
      NextPageColumn();
    BeginSection("##mods_expansions", Tr("EXPANSIONS"));
    ImGui::SameLine();
    DrawStatusPill(Tr("PLANNED"), LAUNCHER_ACCENT, scale, true);
    const char* expansions_text = Tr("Story campaigns, characters, tracks and more, made by the "
                                     "community, without changing the original game files.");
    const char* expansions_goal = Tr("A long-term goal of the project.");
    if (mods_art_upright)
    {
      // Beside the text, as tall as the room left on the page allows (so it
      // doesn't scroll) and at most 60% of the column wide.
      const ImGuiStyle& style = ImGui::GetStyle();
      const float width = ImGui::GetContentRegionAvail().x;
      const float room = mods_page_bottom - ImGui::GetCursorScreenPos().y -
                         style.WindowPadding.y * 0.75f - style.ItemSpacing.y * 2.0f;
      const float art_width = std::floor(
          std::clamp(room / mods_art.aspect, 100.0f * scale, width * 0.6f));
      DrawLauncherBanner(mods_art, art_width);
      ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextUnformatted(expansions_text);
      ImGui::Spacing();
      ImGui::TextDisabled("%s", expansions_goal);
      ImGui::PopTextWrapPos();
      ImGui::EndGroup();
    }
    else if (mods_art.texture)
    {
      // As large as the column and the room above the text under it allow,
      // so the page doesn't scroll.
      const ImGuiStyle& style = ImGui::GetStyle();
      const float width = ImGui::GetContentRegionAvail().x;
      const float text_height =
          ImGui::CalcTextSize(expansions_text, nullptr, false, width).y +
          ImGui::CalcTextSize(expansions_goal, nullptr, false, width).y + style.ItemSpacing.y * 2.0f;
      const float room = mods_page_bottom - ImGui::GetCursorScreenPos().y - text_height -
                         style.WindowPadding.y * 0.75f - style.ItemSpacing.y * 3.0f;
      const float art_width = std::floor(std::clamp(room / mods_art.aspect, 120.0f * scale, width));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((width - art_width) * 0.5f));
      DrawLauncherBanner(mods_art, art_width);
      ImGui::Spacing();
    }
    if (!mods_art_upright)
    {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextUnformatted(expansions_text);
      ImGui::TextDisabled("%s", expansions_goal);
      ImGui::PopTextWrapPos();
    }
    EndSection();
    if (mods_columns)
      EndPageColumns();
      }
      if (current_page == LauncherPage::Screenshots)
        show_screenshots();
      if (current_page == LauncherPage::Credits)
      {
#if defined(MODERNGEKKO_FRONTEND_VERSION) && defined(MODERNGEKKO_UPDATE_REPOSITORY)
        DrawCreditsPage(fonts.bold, [&] {
          const bool previous = config.check_for_updates;
          if (!DrawVersionSection(update_check, config.check_for_updates, fonts.bold))
            return;
          config.resolution = resolutions[resolution_index].text;
          config.show_fps_in_title = show_fps_in_title;
          config.controller = selected_controller;
          std::string error;
          if (!moderngekko::frontend::SaveConfig(user_directory, config, &error))
          {
            config.check_for_updates = previous;
            std::lock_guard lock(dialog.mutex);
            dialog.error = std::move(error);
          }
        });
#else
        DrawCreditsPage(fonts.bold);
#endif
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
    // The controller's state, after a picture of what the game reads (a
    // gamepad of its kind, or the keyboard); on the right, while a gamepad or
    // the keyboard is in use, what its buttons or keys do here.
    {
      const std::string status = Format(Tr("Controller: %s"), controller_status.Text().c_str());
      std::optional<DeviceIcon> status_icon;
      if (controller_index >= 0 && controller_index < static_cast<int>(controllers.size()))
        status_icon = DeviceIconOf(SDL_GetGamepadTypeForID(controllers[controller_index].joystick_id));
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
      else if (controller_profile_exists)
        status_icon = DeviceIcon::Keyboard;  // the first controller's keys still play
#endif
      const bool keys_shown = last_input == LastInput::Keyboard;
      std::vector<FooterHint> hints;
      const FooterKey enter{Tr("Enter")};
      const FooterKey escape{"Esc"};
      if (keys_shown ||
          (last_input == LastInput::Pad && (io.BackendFlags & ImGuiBackendFlags_HasGamepad)))
      {
        if (testing_controls)
          hints = keys_shown ? std::vector<FooterHint>{{PadButton::East, std::nullopt,
                                                         Tr("Stop testing"), {escape}}}
                             : std::vector<FooterHint>{{PadButton::East, std::nullopt,
                                                         Tr("Hold to stop testing"), {}}};
        else if (current_page == LauncherPage::Controls && controller_profile_exists &&
                 !popup_was_open)
          hints = {{PadButton::North, std::nullopt, Tr("Test buttons"), {}},
                   {PadButton::South, std::nullopt, Tr("Accept"), {enter}},
                   {PadButton::East, std::nullopt, Tr("Go back"), {escape}},
                   {PadButton::LeftShoulder, PadButton::RightShoulder, Tr("Pages"),
                    {{"Q"}, {"E"}}}};
        else if (viewer_was_open)
          hints = {{PadButton::LeftShoulder, PadButton::RightShoulder, Tr("Newer / older"),
                    {{"", -1}, {"", 1}}},
                   {PadButton::East, std::nullopt, Tr("Go back"), {escape}}};
        else if (popup_was_open)
          hints = {{PadButton::South, std::nullopt, Tr("Accept"), {enter}},
                   {PadButton::East, std::nullopt, Tr("Go back"), {escape}}};
        else
          hints = {{PadButton::South, std::nullopt, Tr("Accept"), {enter}},
                   {PadButton::East, std::nullopt, Tr("Go back"), {escape}},
                   {PadButton::LeftShoulder, PadButton::RightShoulder, Tr("Pages"),
                    {{"Q"}, {"E"}}}};
      }
      if (status_icon)
      {
        const float icon_height = std::floor(ImGui::GetTextLineHeight() * 0.95f);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float icon_width = DrawDeviceIcon(
            ImGui::GetWindowDrawList(),
            ImVec2(at.x, at.y + std::floor((ImGui::GetTextLineHeight() - icon_height) * 0.5f)),
            icon_height, *status_icon);
        ImGui::Dummy(ImVec2(icon_width, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0.0f, 6.0f * scale);
      }
      if (hints.empty())
      {
        ImGui::TextWrapped("%s", status.c_str());
      }
      else
      {
        const ImVec2 line_min = ImGui::GetCursorScreenPos();
        const float right = line_min.x + ImGui::GetContentRegionAvail().x;
        const float hints_width =
            DrawFooterHints(hints, pad_style, keys_shown, fonts.bold, line_min, right, false);
        ImGui::TextUnformatted(FitTextToWidth(ImGui::GetFont(), ImGui::GetFontSize(), status,
                                              ImGui::GetContentRegionAvail().x - hints_width -
                                                  ImGui::GetStyle().ItemSpacing.x * 3.0f)
                                   .c_str());
        DrawFooterHints(hints, pad_style, keys_shown, fonts.bold, line_min, right, true);
      }
    }
    // What B closes next frame, before it goes back to the sidebar.
    popup_was_open =
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    item_was_active = ImGui::IsAnyItemActive();
    viewer_was_open = current_page == LauncherPage::Screenshots && screenshot_gallery &&
                      screenshot_gallery->Viewing();
    ImGui::End();

    // The game selection screen, over the whole window while it is open.
    if (game_select.IsOpen())
    {
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      const double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
      ImGui::SetNextWindowPos(viewport->Pos);
      ImGui::SetNextWindowSize(viewport->Size);
      ImGui::SetNextWindowFocus();
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::Begin("##game_select", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
      // Left and right, the arrow keys or the D-pad or the stick; choose with
      // Enter, Space or A; back with Escape or B. A gamepad still held from
      // before moves nothing.
      const bool pads = !pad_waits_for_release;
      const auto pressed = [&](std::initializer_list<ImGuiKey> keys, bool repeat) {
        for (const ImGuiKey key : keys)
        {
          const bool is_pad = key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown;
          if ((!is_pad || pads) && ImGui::IsKeyPressed(key, repeat))
            return true;
        }
        return false;
      };
      GameSelectInput input;
      input.left = pressed({ImGuiKey_LeftArrow, ImGuiKey_A, ImGuiKey_GamepadDpadLeft,
                            ImGuiKey_GamepadLStickLeft},
                           true);
      input.right = pressed({ImGuiKey_RightArrow, ImGuiKey_D, ImGuiKey_GamepadDpadRight,
                             ImGuiKey_GamepadLStickRight},
                            true);
      input.choose = pressed({ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space,
                              ImGuiKey_GamepadFaceDown},
                             false);
      input.back = pressed({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}, false);
      input.mouse = io.MousePos;
      input.mouse_moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
      input.clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
      game_select.Place(viewport->Pos, viewport->Size);
      const GameSelectEvent event = game_select.Update(input, now);
      for (const moderngekko::frontend::SoundEffect* sound : game_select.TakeSounds())
        music.PlayEffect(*sound);
      GameSelectTexts texts;
      texts.title = Tr("SELECT YOUR GAME");
      texts.switch_games = Tr("Switch");
      texts.choose = Tr("Choose");
      texts.back = Tr("Go back");
      texts.enter_key = Tr("Enter");
      texts.not_installed = Tr("NOT INSTALLED");
      const bool japanese =
          moderngekko::frontend::ActiveLanguage() == moderngekko::frontend::Language::Japanese;
      texts.vertical_title = japanese;
      texts.small_hints = japanese;
      game_select.Draw(ImGui::GetWindowDrawList(), viewport->Pos, viewport->Size, now,
                       game_select_font, fonts.bold, texts, last_input != LastInput::Pad,
                       pad_style);
      ImGui::End();
      ImGui::PopStyleVar(2);
      if (event == GameSelectEvent::Chosen)
      {
        const GameSlot* slot = slot_of(game_select.Chosen() == GameSelectSide::Left ?
                                           game_select.Layout().left_game :
                                           game_select.Layout().right_game);
        if (slot && slot->metadata)
        {
          choose_game(*slot);
          summary_stale = true;
          if (ensure_controller())
          {
            launch_mode = LaunchMode::Solo;
            launch_from_black = true;
            done = true;
          }
        }
      }
      else if (event == GameSelectEvent::Closed)
      {
        // Back on the PLAY page, as it was; a gamepad's B still held moves
        // nothing there.
        pad_waits_for_release = true;
      }
    }

    // The launcher going to black before the game, or coming back after.
    if (fading)
    {
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      DrawLoadingScreen(ImGui::GetForegroundDrawList(), viewport->Pos, viewport->Size, transition,
                        static_cast<double>(SDL_GetTicksNS()) / 1e9, loading_logo, scale);
    }

    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 8, 8, 9, 255);  // ImGuiCol_WindowBg of the theme
    SDL_RenderClear(renderer);
    RenderBackground(renderer, background, static_cast<double>(SDL_GetTicksNS()) / 1e9);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);

    // PLAY, Host or Join was chosen this frame: the game starts, and the
    // launcher waits for it from the next frame (start_game).
    if (launch_mode != LaunchMode::None)
    {
      if (extraction_thread.joinable())
        extraction_thread.join();
      // The loading screen shows the game's logo, as the game selection
      // screen has it.
      loading_logo = {};
      if (moderngekko::frontend::PlaysSeveralGames() && current_metadata &&
          game_select.Load(renderer, game_select_folder,
                           ImGui::GetMainViewport()->Size.y *
                               ImGui::GetIO().DisplayFramebufferScale.y))
      {
        loading_logo = game_select.LogoFor(current_metadata.metadata->disc_id,
                                           ImGui::GetMainViewport()->Size.y);
      }
      if (!start_game(launch_mode, launch_from_black))
        result = 1;
      launch_mode = LaunchMode::None;
      launch_from_black = false;
      done = false;
    }
  }

  if (extraction_thread.joinable())
    extraction_thread.join();

  music.Stop();
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
  layered_banner.Unload();
  if (background.texture)
    SDL_DestroyTexture(background.texture);
  // Its thread stops and its pictures go before the renderer.
  screenshot_gallery.reset();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}
