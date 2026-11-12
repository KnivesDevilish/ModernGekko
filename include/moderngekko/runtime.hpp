#pragma once

#include "moderngekko/game.hpp"
#include "moderngekko/module_abi.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace moderngekko
{
struct ModuleSource
{
  enum class Kind
  {
    None,
    DynamicPath,
    AttachedDescriptor,
  };

  static ModuleSource DynamicPath(std::filesystem::path path);
  static ModuleSource AttachedDescriptor(const ModernGekkoModuleDesc* descriptor);

  Kind kind = Kind::None;
  std::filesystem::path path;
  const ModernGekkoModuleDesc* descriptor = nullptr;
};

// How the picture fills the window. Widescreen and Standard also tell the Wii
// which shape the game should draw (the system's 16:9 setting); Dolphin then
// follows the game, so the image is never squashed.
enum class AspectRatio
{
  Widescreen, // the game draws 16:9
  Standard,   // the game draws 4:3, with bars on a wide window
  Stretch,    // the game draws 16:9 stretched to fill the window
  // Experimental: the game's 3D view widened to the window's shape (21:9 on
  // an ultrawide screen) instead of stretched; its 2D parts still stretch.
  Ultrawide,
  // Experimental: the picture takes GraphicsSettings' custom shape (bars
  // fill the rest of the window), its 3D view widened or narrowed to keep
  // the proportions; its 2D parts still stretch.
  Custom,
};

struct GraphicsSettings
{
  std::string backend;
  std::optional<int> internal_resolution_scale;
  AspectRatio aspect_ratio = AspectRatio::Widescreen;
  // The shape AspectRatio::Custom gives the picture, width:height.
  int custom_aspect_width = 21;
  int custom_aspect_height = 9;
  // Pictures in the black bars beside the game when it doesn't fill the
  // window, as a 4:3 game on a wide screen (src/runtime/game_border.hpp): the
  // folder with borders.txt and the id of the border to show. No border when
  // either is empty.
  std::filesystem::path border_folder;
  std::string border_id;
  // A Dolphin post-processing shader drawn over the picture, by its name in
  // Sys/Shaders (such as "ModernGekko/bloom"); none when empty.
  std::string post_processing_shader;
  // Screenshots (F9, or the in-game menu's Take screenshot) are taken at
  // least at this internal resolution scale: the game is drawn at it for the
  // few frames the picture needs, then goes back to its own
  // (src/runtime/screenshot.hpp). 0, or a scale no higher than the game's,
  // takes them at the game's.
  int screenshot_scale = 0;
  // What screenshots' file names start with, before their number, such as
  // "SR-ZGS" for SR-ZGS_0001.png; the game's disc ID when empty.
  std::string screenshot_name;
};

struct AudioSettings
{
  std::string backend;
  bool mute = false;
  // The game's volume, 0 to 100. mute silences it whatever this says.
  int volume = 100;
};

// One row of the in-game menu's Quick settings: the choices it steps
// through, the one chosen, and what it changes in the running game.
struct QuickSetting
{
  enum class Kind
  {
    // value: a post-processing shader, as GraphicsSettings has it.
    Effect,
    // value: Dolphin's internal resolution scale, such as "2".
    InternalResolution,
    // value: the game's volume, "0" to "100".
    GameVolume,
    // value: a border's id in GraphicsSettings::border_folder; none when
    // empty.
    Border,
  };
  struct Choice
  {
    // Shown in the row: English, which the menu translates, or a name kept
    // as it is (a resolution, a border's name).
    std::string label;
    // A line shown under the rows while this choice is chosen; may be empty.
    std::string description;
    std::string value;
  };
  Kind kind = Kind::Effect;
  std::vector<Choice> choices;
  std::size_t selected = 0;
};

// One row of the in-game menu's Controls page: a button of the emulated
// controller, named in English (the menu translates it), and the profile keys
// whose inputs press it, such as {"Stick", {"Main Stick/Up", ...}}. A row of
// several keys names whole sticks ("Left stick") rather than directions.
struct GameMenuControl
{
  std::string label;
  std::vector<std::string> keys;
};

struct InputSettings
{
  bool background_input = false;
};

struct AutomationSettings
{
  std::filesystem::path directory;
};

// The menu the player opens over the game with the first controller's menu
// button (src/runtime/game_menu.hpp). It answers to that controller as the
// player has it set up: the profile in the user's Config folder (profile_file,
// such as GCPadNew.ini, and its profile_section, such as GCPad1) and, for each
// of its actions, the profile keys whose inputs do it (such as "Buttons/A").
// open_fallback is the menu button for a profile that has none of open_keys.
struct GameMenuSettings
{
  bool enabled = false;
  std::string profile_file;
  std::string profile_section;
  std::vector<std::string> open_keys;
  std::vector<std::string> accept_keys;
  std::vector<std::string> back_keys;
  std::vector<std::string> up_keys;
  std::vector<std::string> down_keys;
  std::vector<std::string> left_keys;
  std::vector<std::string> right_keys;
  std::string open_fallback;
  // The interface font is looked for after the system's fonts in
  // <release_directory>/Sys/Resources (tools/launcher_look.hpp).
  std::filesystem::path release_directory;
  // The Quick settings page; the main page has no Quick settings without
  // any. A change applies to the game at once, then keep_quick_setting is
  // called with the row and the choice (on the menu's own thread) so the
  // frontend can keep it for next time.
  std::vector<QuickSetting> quick_settings;
  std::function<void(std::size_t row, std::size_t choice)> keep_quick_setting;
  // The Controls page, read from the profile when the game starts: the name
  // of the emulated controller (its first column's title, such as
  // "GameCube") and its rows. No Controls page without rows.
  std::string controls_title;
  std::vector<GameMenuControl> controls;
};

enum class WindowSystem
{
  Default,
  Wayland,
  X11,
};

struct RuntimeConfig
{
  std::filesystem::path game_root;
  std::filesystem::path user_directory;
  ModuleSource module;
  std::vector<std::filesystem::path> mod_directories;
  GraphicsSettings graphics;
  AudioSettings audio;
  InputSettings input;
  WindowSystem window_system = WindowSystem::Default;
  bool headless = false;
  bool fullscreen = false;
  // The game window's size when it opens (out of fullscreen); 0 fits it to
  // the screen (src/runtime/window_size.hpp).
  int window_width = 0;
  int window_height = 0;
  // A window the game window opens centred on, on its monitor, as its
  // window system names it (an X11 window ID, a Windows HWND): the
  // launcher's. 0, or a window that has gone: the middle of the primary
  // monitor.
  std::uint64_t centre_on_window = 0;
  // Called once, from the video thread, when the game window shows its
  // first picture: the launcher waits for it before stepping aside.
  std::function<void()> first_picture_shown;
  bool allow_interpreter = false;
  bool show_fps_in_title = true;
  std::optional<std::string> window_title;
  // Boot straight into a savestate instead of from the title screen.
  std::optional<std::filesystem::path> load_state_path;
  AutomationSettings automation;
  GameMenuSettings menu;
  // Hash the whole files/ tree when Create() inspects the game. That hash only
  // ends up in GetGameMetadata().assets_sha256; when false it is left empty
  // and the boot skips tens of seconds of hashing on a multi-GB game.
  bool hash_game_assets = true;
};

enum class RuntimeErrorCode
{
  AlreadyActive,
  InvalidGame,
  ModuleRequired,
  ModuleRejected,
  PlatformUnavailable,
  InitializationFailed,
  BootFailed,
  InvalidState,
};

struct RuntimeError
{
  RuntimeErrorCode code = RuntimeErrorCode::InitializationFailed;
  std::string message;
};

enum class RuntimeExitReason
{
  Stopped,
  BootFailed,
  // The player chose Quit game in the in-game menu: a launcher waiting for
  // the game closes too.
  Quit,
};

struct RuntimeRunResult
{
  RuntimeExitReason reason = RuntimeExitReason::Stopped;
  std::optional<RuntimeError> error;
};

class Runtime;

struct RuntimeCreateResult
{
  std::unique_ptr<Runtime> runtime;
  std::optional<RuntimeError> error;

  explicit operator bool() const { return runtime != nullptr; }
};

class Runtime final
{
public:
  static RuntimeCreateResult Create(RuntimeConfig config);

  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) = delete;
  Runtime& operator=(Runtime&&) = delete;

  RuntimeRunResult Run();
  void RequestStop();
  std::optional<RuntimeError> Pause();
  std::optional<RuntimeError> Resume();

  const RuntimeConfig& GetConfig() const;
  const GameMetadata& GetGameMetadata() const;
  const std::string& GetWindowTitle() const;

private:
  struct Impl;
  explicit Runtime(std::unique_ptr<Impl> impl);
  void StopAutomation();
  std::unique_ptr<Impl> m_impl;
};
}  // namespace moderngekko

namespace ModernGekko = moderngekko;
