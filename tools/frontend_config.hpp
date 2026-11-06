#pragma once

#include <cstdint>
#include <filesystem>
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
  // "16:9", "4:3" or "stretch" (see SupportedAspectRatios).
  std::string aspect_ratio = "16:9";
  // The launcher's language: "auto" (the system's), "en" or "es".
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
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
// Dolphin's name for the keyboard on this platform. Without a gamepad the
// profile is written for this device: the first GameCube controller reads
// its keys either way, and a gamepad connected later takes the device's place.
std::string_view KeyboardDevice();
#endif
} // namespace moderngekko::frontend
