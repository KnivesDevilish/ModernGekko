#pragma once

#include "moderngekko/game.hpp"
#include "moderngekko/module_abi.h"

#include <filesystem>
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
};

struct GraphicsSettings
{
  std::string backend;
  std::optional<int> internal_resolution_scale;
  AspectRatio aspect_ratio = AspectRatio::Widescreen;
  // Pictures in the black bars beside the game when it doesn't fill the
  // window, as a 4:3 game on a wide screen (src/runtime/game_border.hpp): the
  // folder with borders.txt and the id of the border to show. No border when
  // either is empty.
  std::filesystem::path border_folder;
  std::string border_id;
};

struct AudioSettings
{
  std::string backend;
  bool mute = false;
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
  std::string open_fallback;
  // The interface font is looked for after the system's fonts in
  // <release_directory>/Sys/Resources (tools/launcher_look.hpp).
  std::filesystem::path release_directory;
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
