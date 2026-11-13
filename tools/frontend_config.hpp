#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend {
struct ResolutionOption {
  const char *text;
  int dolphin_scale;
};

struct GraphicsBackendOption {
  const char *text;
  const char *value;
};

struct CpuCoreOption {
  const char *text;
  const char *value;
};

struct AspectRatioOption {
  const char *text;
  const char *value;
  const char *description;
};

// A picture effect: a Dolphin post-processing shader drawn over the game
// (the fork's shaders/, copied to Sys/Shaders/ModernGekko, or Dolphin's
// FXAA). The name and the line saying what it does are English, which the
// launcher and the in-game menu translate; value is config.ini's.
struct EffectOption {
  const char *text;
  const char *value;
  const char *shader;  // empty for none
  const char *description;
};

// A resolution screenshots are taken at (F9, or the in-game menu's Take
// screenshot): the game is drawn at it for the few frames the picture needs.
// value is config.ini's; dolphin_scale 0 takes them at the game's own.
struct ScreenshotResolutionOption {
  const char *text;
  const char *value;
  int dolphin_scale;
};

// A size for the game window (out of fullscreen). value is config.ini's;
// width and height 0 fit it to the screen.
struct WindowSizeOption {
  const char *text;
  const char *value;
  int width;
  int height;
  const char *description;
};

struct ConfigResult {
  int dolphin_scale = 0;
  std::string resolution;
  std::string graphics_backend = "Vulkan";
  // "jit64" or "staticrecomp". JIT64 is the default because it is the mode
  // validated at full speed; MODERNGEKKO_STATICRECOMP still overrides it.
  std::string cpu_core = "jit64";
  std::string controller;
  std::vector<std::string> controllers;
  bool show_fps_in_title = true;
  bool fullscreen = false;
  // "16:9", "4:3", "stretch", "ultrawide" or "custom" (see
  // SupportedAspectRatios).
  std::string aspect_ratio = "16:9";
  // The shape "custom" gives the picture, "<width>:<height>" (see
  // ParseCustomAspect).
  std::string custom_aspect = "21:9";
  // The pictures beside a 4:3 game on a wide screen: "none", "random" (one is
  // picked each time the game opens) or the id of a border in
  // Resources/borders/borders.txt (tools/game_borders.hpp).
  std::string game_border = "random";
  // The picture effect: one of SupportedEffects()' values.
  std::string effect = "none";
  // The game's volume, 0 (silent) to 100.
  int game_volume = 100;
  // The game window's size: "auto" (fits the screen), one of
  // SupportedWindowSizes()' values, or another "<width>x<height>".
  std::string window_size = "auto";
  // What screenshots are taken at: one of SupportedScreenshotResolutions()'
  // values. Never below the game's own resolution.
  std::string screenshot_resolution = "3840x2160";
  // The launcher's language: "auto" (the system's), "en", "es" or "ja".
  std::string language = "auto";
  // Volume of the launcher's and the Netplay lobby's music, 0 (off) to 100.
  int music_volume = 70;
  // Whether the launcher looks on GitHub for a newer version when it opens
  // (only in builds that name a version and a repository).
  bool check_for_updates = true;
  std::string netplay_nickname = "Player";
  std::string netplay_address = "127.0.0.1";
  std::uint16_t netplay_port = 2626;
  std::string netplay_buffer = "auto";
  // How friends find the host: "code" (an invite code handed out by the
  // invite server) or "ip" (the host's address and UDP port).
  std::string netplay_connection = "code";
  // The invite server, "host" or "host:port": Dolphin's public traversal
  // server unless config.ini names another one.
  std::string invite_server = "stun.dolphin-emu.org";
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

const std::vector<ResolutionOption> &SupportedResolutions();
const std::vector<GraphicsBackendOption> &SupportedGraphicsBackends();
const std::vector<CpuCoreOption> &SupportedCpuCores();
const std::vector<AspectRatioOption> &SupportedAspectRatios();
// A custom shape, "<width>:<height>" such as "21:9": whole numbers from 1 to
// 100, as wide as 1:1 up to 6:1. Nothing for anything else.
struct CustomAspect {
  int width = 21;
  int height = 9;
};
std::optional<CustomAspect> ParseCustomAspect(std::string_view value);
// Ready-made shapes the launcher offers for "custom".
const std::vector<CustomAspect> &CustomAspectPresets();
// The first entry, none, is the default.
const std::vector<EffectOption> &SupportedEffects();
// The effect of config.ini's value; nullptr for one this build doesn't have.
const EffectOption *FindEffect(std::string_view value);
// An effect value config.ini may have from an earlier version, as this one
// calls it: bloom, vivid and sharpen are remaster now, chromatic and fxaa
// none. Any other value comes back as it is.
std::string_view CurrentEffect(std::string_view value);
// Automatic first, then sizes from small to large.
const std::vector<WindowSizeOption> &SupportedWindowSizes();
// The width and height of config.ini's value; 0 and 0 (fit the screen) for
// "auto" or a value that isn't a size from 320x240 to 7680x4320.
struct WindowDimensions {
  int width = 0;
  int height = 0;
};
WindowDimensions WindowSizeOf(std::string_view value);
// From the game's own resolution up; ConfigResult's default among them.
const std::vector<ScreenshotResolutionOption> &SupportedScreenshotResolutions();
// The option of config.ini's value; the default's for one that isn't listed.
const ScreenshotResolutionOption &FindScreenshotResolution(std::string_view value);
ConfigResult LoadConfig(const std::filesystem::path &user_directory,
                        bool create_if_missing);
bool SaveConfig(const std::filesystem::path &user_directory,
                const ConfigResult &config, std::string *error);
bool SaveConfig(const std::filesystem::path &user_directory,
                std::string_view resolution, bool show_fps_in_title,
                std::string_view controller, std::string *error);
std::string
ReadConfiguredController(const std::filesystem::path &user_directory);
std::vector<std::string>
ReadConfiguredControllers(const std::filesystem::path &user_directory);
bool ControllerConfigExists(const std::filesystem::path &user_directory);
bool GenerateControllerConfig(const std::filesystem::path &user_directory,
                              std::span<const std::string> controllers,
                              std::string *message);
bool GenerateControllerConfig(const std::filesystem::path &user_directory,
                              std::string_view controller,
                              std::string *message);
bool EnsureControllerConfig(const std::filesystem::path &user_directory,
                            std::span<const std::string> controllers,
                            std::string *message);
bool EnsureControllerConfig(const std::filesystem::path &user_directory,
                            std::string_view controller, std::string *message);
// What the in-game menu answers to: the first controller's profile, as
// GenerateControllerConfig writes it (a file in the user's Config folder and
// its section), and the profile keys of each of the menu's buttons. Goes into
// the runtime's GameMenuSettings.
struct GameMenuKeys {
  std::string profile_file;
  std::string profile_section;
  std::vector<std::string> open;
  std::vector<std::string> accept;
  std::vector<std::string> back;
  std::vector<std::string> up;
  std::vector<std::string> down;
  std::vector<std::string> left;
  std::vector<std::string> right;
  // The menu button of a profile written before it had one.
  std::string open_fallback;
};
GameMenuKeys GameMenuProfileKeys();
// What the in-game menu's Controls page lists: the emulated controller's
// name and, for each of its buttons the game uses, the profile keys behind it
// (as the launcher's tester groups them). Labels are English, for the menu to
// translate.
struct GameMenuControlRow {
  const char *label;
  std::vector<std::string> keys;
};
struct GameMenuControlsLegend {
  std::string title;
  std::vector<GameMenuControlRow> rows;
};
GameMenuControlsLegend GameMenuControls();
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
// Dolphin's name for the keyboard on this platform. Without a gamepad the
// profile is written for this device: the first GameCube controller reads
// its keys either way, and a gamepad connected later takes the device's place.
std::string_view KeyboardDevice();
#endif
} // namespace moderngekko::frontend
