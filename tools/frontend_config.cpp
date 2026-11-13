#include "frontend_config.hpp"
#include "game_borders.hpp"
#include "ui_text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace moderngekko::frontend {
namespace {
std::string Trim(std::string value) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

std::string Lower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool ValidNetplayAddress(std::string_view value) {
  if (value.empty() || value.size() > 253)
    return false;
  return std::ranges::all_of(value, [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '-' || c == '_';
  });
}

// "stun.dolphin-emu.org" or "203.0.113.5:6262".
bool ValidInviteServer(std::string_view value) {
  const std::size_t colon = value.find(':');
  if (colon != std::string_view::npos) {
    const std::string_view port_text = value.substr(colon + 1);
    unsigned int port = 0;
    const auto parsed = std::from_chars(
        port_text.data(), port_text.data() + port_text.size(), port);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != port_text.data() + port_text.size() || port == 0 ||
        port > 65535)
      return false;
    value = value.substr(0, colon);
  }
  return ValidNetplayAddress(value);
}

std::string NormalizeGraphicsBackend(std::string value) {
  const std::string lower = Lower(Trim(std::move(value)));
  if (lower == "vulkan")
    return "Vulkan";
  if (lower == "opengl" || lower == "ogl")
    return "OGL";
  // Direct3D, on Windows only; the runtime names them D3D and D3D12. A
  // config.ini brought from Windows plays with Vulkan elsewhere.
  const bool d3d11 =
      lower == "d3d" || lower == "d3d11" || lower == "direct3d 11" || lower == "direct3d11";
  const bool d3d12 = lower == "d3d12" || lower == "direct3d 12" || lower == "direct3d12";
#if defined(_WIN32)
  if (d3d11)
    return "D3D";
  if (d3d12)
    return "D3D12";
#else
  if (d3d11 || d3d12)
    return "Vulkan";
#endif
  return {};
}

std::string NormalizeCpuCore(std::string value) {
  const std::string lower = Lower(Trim(std::move(value)));
  if (lower == "jit64" || lower == "jit")
    return "jit64";
  if (lower == "staticrecomp" || lower == "static_recomp" || lower == "static")
    return "staticrecomp";
  return {};
}

std::string NormalizeAspectRatio(std::string value) {
  const std::string lower = Lower(Trim(std::move(value)));
  if (lower == "16:9" || lower == "widescreen" || lower == "wide")
    return "16:9";
  if (lower == "4:3" || lower == "standard")
    return "4:3";
  if (lower == "stretch" || lower == "stretched")
    return "stretch";
  if (lower == "ultrawide")
    return "ultrawide";
  if (lower == "custom")
    return "custom";
  return {};
}

std::string NormalizeLanguage(std::string value) {
  const std::string lower = Lower(Trim(std::move(value)));
  if (lower == "auto" || lower == "system")
    return "auto";
  if (lower == "en" || lower == "english")
    return "en";
  if (lower == "es" || lower == "spanish")
    return "es";
  if (lower == "ja" || lower == "japanese")
    return "ja";
  return {};
}

bool ParseBoolean(const std::string &value, bool *result) {
  if (value == "true" || value == "1" || value == "yes" || value == "on") {
    *result = true;
    return true;
  }
  if (value == "false" || value == "0" || value == "no" || value == "off") {
    *result = false;
    return true;
  }
  return false;
}

fs::path ControllerConfigPath(const fs::path &user_directory) {
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  return user_directory / "Config" / "GCPadNew.ini";
#else
  return user_directory / "Config" / "WiimoteNew.ini";
#endif
}

std::string_view ControllerSectionPrefix() {
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  return "[GCPad";
#else
  return "[Wiimote";
#endif
}

#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
// The first GameCube controller also answers to the keyboard: S, D, A for
// A, B, X, W for Y, Q for Z, left Shift and E for L and R, Enter for Start and
// the arrows for the control stick, plus Backspace and M for the Select and
// Menu buttons kept for mods. Device and key names are the ones
// Dolphin's keyboard backend uses on each platform; on Linux that is the X11
// backend, which the runner uses unless started with --wayland.
#if defined(_WIN32)
constexpr std::string_view KEYBOARD_DEVICE = "DInput/0/Keyboard Mouse";
constexpr std::string_view KEY_START = "RETURN";
constexpr std::string_view KEY_BACKSPACE = "BACK";
constexpr std::string_view KEY_LEFT_SHIFT = "LSHIFT";
constexpr std::array<std::string_view, 4> KEY_ARROWS = {"UP", "DOWN", "LEFT", "RIGHT"};
#elif defined(__APPLE__)
constexpr std::string_view KEYBOARD_DEVICE = "Quartz/0/Keyboard & Mouse";
constexpr std::string_view KEY_START = "Return";
constexpr std::string_view KEY_BACKSPACE = "Backspace";
constexpr std::string_view KEY_LEFT_SHIFT = "Left Shift";
constexpr std::array<std::string_view, 4> KEY_ARROWS = {"Up Arrow", "Down Arrow", "Left Arrow",
                                                        "Right Arrow"};
#else
constexpr std::string_view KEYBOARD_DEVICE = "XInput2/0/Virtual core pointer";
constexpr std::string_view KEY_START = "Return";
constexpr std::string_view KEY_BACKSPACE = "BackSpace";
constexpr std::string_view KEY_LEFT_SHIFT = "Shift_L";
constexpr std::array<std::string_view, 4> KEY_ARROWS = {"Up", "Down", "Left", "Right"};
#endif

// The game is played with the GameCube controller, so the emulated Wii Remote
// is left disconnected (Source = 0). Without that, a missing or empty
// WiimoteNew.ini makes Dolphin bind its default keys to the Wii Remote, and
// several overlap the GameCube keys: Enter would also press HOME, Q minus and
// E plus. The slot keeps a key because Dolphin rewrites the file when the
// game closes and drops sections that have none. A file that already sets up
// [Wiimote1] is the player's and is left alone.
void DisconnectWiiRemotes(const fs::path &config_directory) {
  const fs::path path = config_directory / "WiimoteNew.ini";
  std::error_code ec;
  if (fs::exists(path, ec)) {
    std::ifstream input(path);
    std::string line;
    bool in_first_slot = false;
    while (std::getline(input, line)) {
      const std::string trimmed = Trim(line);
      if (trimmed.starts_with('[')) {
        in_first_slot = trimmed == "[Wiimote1]";
        continue;
      }
      if (in_first_slot && !trimmed.empty())
        return;
    }
  }
  fs::create_directories(config_directory, ec);
  std::ofstream output(path, std::ios::trunc);
  output << "[Wiimote1]\nSource = 0\n";
}
#endif

} // namespace

const std::vector<ResolutionOption> &SupportedResolutions() {
  // These are the output-resolution labels used by Dolphin's integer EFB
  // scales.
  static const std::vector<ResolutionOption> resolutions = {
      {"640x528", 1},   {"1280x720", 2},  {"1920x1080", 3},  {"2560x1440", 4},
      {"3840x2160", 6}, {"5120x2880", 8}, {"7680x4320", 12},
  };
  return resolutions;
}

const std::vector<GraphicsBackendOption> &SupportedGraphicsBackends() {
  static const std::vector<GraphicsBackendOption> backends = {
      {"Vulkan", "Vulkan"},
      {"OpenGL", "OGL"},
#if defined(_WIN32)
      // For a graphics card or driver without Vulkan, as many older ones on
      // Windows.
      {"Direct3D 11", "D3D"},
      {"Direct3D 12", "D3D12"},
#endif
  };
  return backends;
}

const std::vector<CpuCoreOption> &SupportedCpuCores() {
  // The first entry is the default used when config.ini has no cpu_core line.
  static const std::vector<CpuCoreOption> cores = {
      {MODERNGEKKO_UI_TEXT("JIT64 (recommended)"), "jit64"},
      {MODERNGEKKO_UI_TEXT("StaticRecomp (experimental)"), "staticrecomp"},
  };
  return cores;
}

const std::vector<EffectOption> &SupportedEffects() {
  static const std::vector<EffectOption> effects = {
      {MODERNGEKKO_UI_TEXT("None"), "none", "",
       MODERNGEKKO_UI_TEXT("The game's picture as it is.")},
      {MODERNGEKKO_UI_TEXT("Remaster"), "remaster", "ModernGekko/remaster",
       MODERNGEKKO_UI_TEXT("Crisper detail, richer colors, glowing lights and "
                           "soft shadows in the corners, like a remaster.")},
      {MODERNGEKKO_UI_TEXT("Anime outline"), "outline", "ModernGekko/outline",
       MODERNGEKKO_UI_TEXT("Clean ink lines and flat, bright colors, like an "
                           "anime. The lines go round everything on screen, "
                           "not only the characters.")},
      {MODERNGEKKO_UI_TEXT("CRT"), "crt", "ModernGekko/crt",
       MODERNGEKKO_UI_TEXT("Scanlines and darker corners, like an old TV.")},
  };
  return effects;
}

// Effects earlier versions had: Remaster has what bloom, vivid and sharpen
// did, so they become it; the others become none.
std::string_view CurrentEffect(std::string_view value) {
  static constexpr std::array<std::array<std::string_view, 2>, 5> RETIRED = {{
      {"bloom", "remaster"},
      {"vivid", "remaster"},
      {"sharpen", "remaster"},
      {"chromatic", "none"},
      {"fxaa", "none"},
  }};
  for (const auto &[before, now] : RETIRED) {
    if (value == before)
      return now;
  }
  return value;
}

const EffectOption *FindEffect(std::string_view value) {
  for (const EffectOption &effect : SupportedEffects()) {
    if (value == effect.value)
      return &effect;
  }
  return nullptr;
}

const std::vector<WindowSizeOption> &SupportedWindowSizes() {
  static const std::vector<WindowSizeOption> sizes = {
      {MODERNGEKKO_UI_TEXT("Automatic"), "auto", 0, 0,
       MODERNGEKKO_UI_TEXT("Fits your screen.")},
      {"960x540", "960x540", 960, 540, ""},
      {"1280x720", "1280x720", 1280, 720, ""},
      {"1600x900", "1600x900", 1600, 900, ""},
      {"1920x1080", "1920x1080", 1920, 1080, ""},
      {"2560x1440", "2560x1440", 2560, 1440, ""},
  };
  return sizes;
}

WindowDimensions WindowSizeOf(std::string_view value) {
  for (const WindowSizeOption &option : SupportedWindowSizes()) {
    if (value == option.value)
      return {option.width, option.height};
  }
  // Another size written by hand: digits, "x", digits.
  const std::size_t x = value.find('x');
  if (x == std::string_view::npos)
    return {};
  const auto number = [](std::string_view digits, int *out) {
    if (digits.empty() || digits.size() > 5)
      return false;
    int result = 0;
    for (const char c : digits) {
      if (c < '0' || c > '9')
        return false;
      result = result * 10 + (c - '0');
    }
    *out = result;
    return true;
  };
  WindowDimensions size;
  if (!number(value.substr(0, x), &size.width) ||
      !number(value.substr(x + 1), &size.height) || size.width < 320 ||
      size.height < 240 || size.width > 7680 || size.height > 4320)
    return {};
  return size;
}

const std::vector<ScreenshotResolutionOption> &SupportedScreenshotResolutions() {
  // Up to 8x: the picture is drawn at it and then copied twice (to scale it
  // to the screen's shape and to read it back), which at 12x asks for more
  // video memory than many laptops have. A game set to 12x still takes them
  // at 12x.
  static const std::vector<ScreenshotResolutionOption> resolutions = {
      {MODERNGEKKO_UI_TEXT("Same as the game"), "game", 0},
      {"1920x1080", "1920x1080", 3},
      {"2560x1440", "2560x1440", 4},
      {"3840x2160", "3840x2160", 6},
      {"5120x2880", "5120x2880", 8},
  };
  return resolutions;
}

const ScreenshotResolutionOption &FindScreenshotResolution(std::string_view value) {
  const auto &resolutions = SupportedScreenshotResolutions();
  const std::string_view fallback = ConfigResult{}.screenshot_resolution;
  const ScreenshotResolutionOption *found = nullptr;
  for (const ScreenshotResolutionOption &option : resolutions) {
    if (value == option.value)
      return option;
    if (fallback == option.value)
      found = &option;
  }
  return found ? *found : resolutions.front();
}

const std::vector<AspectRatioOption> &SupportedAspectRatios() {
  // The first entry is the default used when config.ini has no aspect_ratio.
  // The texts are English; the launcher translates them when it shows them.
  static const std::vector<AspectRatioOption> ratios = {
      {MODERNGEKKO_UI_TEXT("16:9 (Widescreen)"), "16:9",
       MODERNGEKKO_UI_TEXT(
           "The game draws in widescreen. Best for most monitors and TVs.")},
      {"4:3", "4:3",
       MODERNGEKKO_UI_TEXT("The original 4:3 picture, with bars on the sides "
                           "of a wide window.")},
      {MODERNGEKKO_UI_TEXT("Stretch"), "stretch",
       MODERNGEKKO_UI_TEXT("The widescreen picture stretched to fill the "
                           "window, whatever its shape. On a 16:9 screen it "
                           "looks the same as 16:9.")},
      {MODERNGEKKO_UI_TEXT("Ultrawide (experimental)"), "ultrawide",
       MODERNGEKKO_UI_TEXT("For 21:9 and wider screens: the race is shown wider "
                           "instead of stretched. Menus and the HUD may still "
                           "look stretched.")},
      {MODERNGEKKO_UI_TEXT("Custom (experimental)"), "custom",
       MODERNGEKKO_UI_TEXT("The shape you choose, such as 16:10, 21:9 or 32:9. "
                           "The race keeps its proportions; menus and the HUD "
                           "may look stretched.")},
  };
  return ratios;
}

std::optional<CustomAspect> ParseCustomAspect(std::string_view value) {
  const std::string text = Trim(std::string(value));
  const std::size_t colon = text.find(':');
  if (colon == std::string::npos)
    return std::nullopt;
  int width = 0;
  int height = 0;
  const std::string_view w = std::string_view(text).substr(0, colon);
  const std::string_view h = std::string_view(text).substr(colon + 1);
  const auto parsed_w = std::from_chars(w.data(), w.data() + w.size(), width);
  const auto parsed_h = std::from_chars(h.data(), h.data() + h.size(), height);
  if (parsed_w.ec != std::errc{} || parsed_w.ptr != w.data() + w.size() ||
      parsed_h.ec != std::errc{} || parsed_h.ptr != h.data() + h.size())
    return std::nullopt;
  if (width < 1 || width > 100 || height < 1 || height > 100 || width < height ||
      width > height * 6)
    return std::nullopt;
  return CustomAspect{width, height};
}

const std::vector<CustomAspect> &CustomAspectPresets() {
  static const std::vector<CustomAspect> presets = {
      {16, 10}, {21, 9}, {32, 9}, {5, 4}, {3, 2}};
  return presets;
}

ConfigResult LoadConfig(const fs::path &user_directory,
                        bool create_if_missing) {
  const fs::path path = user_directory / "config.ini";
  if (!fs::exists(path) && create_if_missing) {
    std::string error;
    if (!SaveConfig(user_directory, "1920x1080", true, {}, &error))
      return {.error = std::move(error)};
  }

  std::ifstream file(path);
  if (!file)
    return {.error = "can't open " + path.string()};

  ConfigResult config;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' ||
        trimmed[0] == '[')
      continue;
    const std::size_t separator = trimmed.find('=');
    if (separator == std::string::npos)
      return {.error = "invalid config.ini line: " + trimmed};
    const std::string key = Lower(Trim(trimmed.substr(0, separator)));
    const std::string raw_value = Trim(trimmed.substr(separator + 1));
    const std::string value = Lower(raw_value);
    if (key == "resolution")
      config.resolution = value;
    else if (key == "backend" || key == "graphics_backend") {
      config.graphics_backend = NormalizeGraphicsBackend(raw_value);
      if (config.graphics_backend.empty())
#if defined(_WIN32)
        return {.error = "graphics backend must be Vulkan, OpenGL, Direct3D 11 or Direct3D 12"};
#else
        return {.error = "graphics backend must be Vulkan or OpenGL"};
#endif
    }
    else if (key == "cpu_core") {
      config.cpu_core = NormalizeCpuCore(raw_value);
      if (config.cpu_core.empty())
        return {.error = "cpu_core must be jit64 or staticrecomp"};
    }
    else if (key == "controller")
      config.controller = raw_value;
    else if (key.starts_with("controller") && key.size() == 11 &&
             key.back() >= '1' && key.back() <= '4') {
      const std::size_t index = static_cast<std::size_t>(key.back() - '1');
      if (config.controllers.size() <= index)
        config.controllers.resize(index + 1);
      config.controllers[index] = raw_value;
    } else if (key == "show_fps_in_title") {
      if (!ParseBoolean(value, &config.show_fps_in_title))
        return {.error = "show_fps_in_title must be true or false"};
    } else if (key == "fullscreen") {
      if (!ParseBoolean(value, &config.fullscreen))
        return {.error = "fullscreen must be true or false"};
    } else if (key == "aspect_ratio") {
      config.aspect_ratio = NormalizeAspectRatio(raw_value);
      if (config.aspect_ratio.empty())
        return {.error = "aspect_ratio must be 16:9, 4:3, stretch, ultrawide or custom"};
    } else if (key == "custom_aspect") {
      const auto custom = ParseCustomAspect(raw_value);
      if (!custom)
        return {.error = "custom_aspect must be width:height, such as 21:9"};
      config.custom_aspect =
          std::to_string(custom->width) + ':' + std::to_string(custom->height);
    } else if (key == "border") {
      // Any id is kept: a border the build doesn't have just shows none.
      config.game_border = NormalizeBorderSetting(raw_value);
      if (config.game_border.empty())
        return {.error = "border must be none, random or a border's id"};
    } else if (key == "language") {
      config.language = NormalizeLanguage(raw_value);
      if (config.language.empty())
        return {.error = "language must be auto, en, es or ja"};
    } else if (key == "effect") {
      // Kept as written (an effect this build lacks is shown as none), but
      // for those earlier versions had.
      config.effect = CurrentEffect(value);
      if (config.effect.empty() ||
          config.effect.find_first_of(" \t\r\n=") != std::string::npos)
        return {.error = "effect must be none or the name of an effect"};
    } else if (key == "window_size") {
      // Kept as written: a size this build doesn't list still works, and
      // anything else fits the screen.
      config.window_size = value;
      if (config.window_size.empty() ||
          config.window_size.find_first_of(" \t\r\n=") != std::string::npos)
        return {.error = "window_size must be auto or a size such as 1280x720"};
    } else if (key == "screenshot_resolution") {
      // Kept as written, as effect is: one this build doesn't list takes
      // the default.
      config.screenshot_resolution = value;
      if (config.screenshot_resolution.empty() ||
          config.screenshot_resolution.find_first_of(" \t\r\n=") != std::string::npos)
        return {.error = "screenshot_resolution must be game or a resolution"};
    } else if (key == "game_volume") {
      int volume = -1;
      const auto parsed = std::from_chars(
          raw_value.data(), raw_value.data() + raw_value.size(), volume);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != raw_value.data() + raw_value.size() || volume < 0 ||
          volume > 100)
        return {.error = "game_volume must be a number from 0 to 100"};
      config.game_volume = volume;
    } else if (key == "music_volume") {
      int volume = -1;
      const auto parsed = std::from_chars(
          raw_value.data(), raw_value.data() + raw_value.size(), volume);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != raw_value.data() + raw_value.size() || volume < 0 ||
          volume > 100)
        return {.error = "music_volume must be a number from 0 to 100"};
      config.music_volume = volume;
    } else if (key == "check_for_updates") {
      if (!ParseBoolean(value, &config.check_for_updates))
        return {.error = "check_for_updates must be true or false"};
    } else if (key == "nickname")
      config.netplay_nickname = raw_value;
    else if (key == "address")
      config.netplay_address = raw_value;
    else if (key == "port") {
      unsigned int port = 0;
      const auto parsed = std::from_chars(
          raw_value.data(), raw_value.data() + raw_value.size(), port);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != raw_value.data() + raw_value.size() || port == 0 ||
          port > 65535)
        return {.error = "netplay port must be between 1 and 65535"};
      config.netplay_port = static_cast<std::uint16_t>(port);
    } else if (key == "buffer") {
      if (value != "auto") {
        unsigned int frames = 0;
        const auto parsed =
            std::from_chars(value.data(), value.data() + value.size(), frames);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size() || frames < 1 ||
            frames > 20)
          return {.error =
                      "netplay buffer must be auto or a value from 1 to 20"};
      }
      config.netplay_buffer = value;
    } else if (key == "connection") {
      if (value != "code" && value != "ip")
        return {.error = "netplay connection must be code or ip"};
      config.netplay_connection = value;
    } else if (key == "invite_server") {
      if (!ValidInviteServer(raw_value))
        return {.error = "invite_server must be a host name, optionally "
                         "followed by :port"};
      config.invite_server = raw_value;
    }
  }
  if (config.resolution.empty())
    return {.error = "config.ini is missing resolution=<width>x<height>"};

  std::erase(config.controllers, std::string{});
  if (config.controllers.empty() && !config.controller.empty())
    config.controllers.push_back(config.controller);
  if (config.controller.empty() && !config.controllers.empty())
    config.controller = config.controllers.front();
  if (config.netplay_nickname.empty())
    return {.error = "netplay nickname cannot be empty"};
  if (config.netplay_nickname.size() > 30)
    return {.error = "netplay nickname cannot exceed 30 characters"};
  if (!ValidNetplayAddress(config.netplay_address))
    return {.error = "netplay address must be an IPv4 address or hostname"};

  for (const ResolutionOption &option : SupportedResolutions()) {
    if (config.resolution == option.text) {
      config.dolphin_scale = option.dolphin_scale;
      return config;
    }
  }

  // Dolphin also accepts exact raw EFB multiples even when they do not have a
  // common display label.
  for (int scale = 1; scale <= 12; ++scale) {
    const std::string raw =
        std::to_string(640 * scale) + "x" + std::to_string(528 * scale);
    if (config.resolution == raw) {
      config.dolphin_scale = scale;
      return config;
    }
  }

  return {.error = "unsupported internal resolution '" +
                   config.resolution +
                   "'; use a listed display resolution or an exact 640x528 "
                   "multiple up to 12x"};
}

bool SaveConfig(const fs::path &user_directory, const ConfigResult &config,
                std::string *error) {
  const std::string graphics_backend =
      NormalizeGraphicsBackend(config.graphics_backend);
  if (config.resolution.empty() || graphics_backend.empty() ||
      config.netplay_nickname.empty() ||
      config.netplay_nickname.size() > 30 ||
      config.netplay_nickname.find_first_of("\r\n") != std::string::npos ||
      !ValidNetplayAddress(config.netplay_address) ||
      config.netplay_address.find_first_of("\r\n") != std::string::npos ||
      config.netplay_port == 0 ||
      (config.netplay_connection != "code" &&
       config.netplay_connection != "ip") ||
      !ValidInviteServer(config.invite_server)) {
    if (error)
      *error = "invalid frontend settings";
    return false;
  }
  if (config.netplay_buffer != "auto") {
    unsigned int frames = 0;
    const auto parsed = std::from_chars(
        config.netplay_buffer.data(),
        config.netplay_buffer.data() + config.netplay_buffer.size(), frames);
    if (parsed.ec != std::errc{} ||
        parsed.ptr !=
            config.netplay_buffer.data() + config.netplay_buffer.size() ||
        frames < 1 || frames > 20) {
      if (error)
        *error = "netplay buffer must be auto or a value from 1 to 20";
      return false;
    }
  }
  const std::string cpu_core = NormalizeCpuCore(config.cpu_core);
  if (cpu_core.empty()) {
    if (error)
      *error = "cpu_core must be jit64 or staticrecomp";
    return false;
  }
  const std::string aspect_ratio = NormalizeAspectRatio(config.aspect_ratio);
  if (aspect_ratio.empty()) {
    if (error)
      *error = "aspect_ratio must be 16:9, 4:3, stretch, ultrawide or custom";
    return false;
  }
  const auto custom_aspect = ParseCustomAspect(config.custom_aspect);
  if (!custom_aspect) {
    if (error)
      *error = "custom_aspect must be width:height, such as 21:9";
    return false;
  }
  const std::string game_border = NormalizeBorderSetting(config.game_border);
  if (game_border.empty()) {
    if (error)
      *error = "border must be none, random or a border's id";
    return false;
  }
  const std::string language = NormalizeLanguage(config.language);
  if (language.empty()) {
    if (error)
      *error = "language must be auto, en, es or ja";
    return false;
  }
  if (config.music_volume < 0 || config.music_volume > 100) {
    if (error)
      *error = "music_volume must be a number from 0 to 100";
    return false;
  }
  if (config.game_volume < 0 || config.game_volume > 100) {
    if (error)
      *error = "game_volume must be a number from 0 to 100";
    return false;
  }
  if (config.effect.empty() ||
      config.effect.find_first_of(" \t\r\n=") != std::string::npos) {
    if (error)
      *error = "effect must be none or the name of an effect";
    return false;
  }
  if (config.screenshot_resolution.empty() ||
      config.screenshot_resolution.find_first_of(" \t\r\n=") != std::string::npos) {
    if (error)
      *error = "screenshot_resolution must be game or a resolution";
    return false;
  }
  if (config.window_size.empty() ||
      config.window_size.find_first_of(" \t\r\n=") != std::string::npos) {
    if (error)
      *error = "window_size must be auto or a size such as 1280x720";
    return false;
  }
  std::error_code ec;
  fs::create_directories(user_directory, ec);
  if (ec) {
    if (error)
      *error = "can't create user directory: " + ec.message();
    return false;
  }
  std::ofstream file(user_directory / "config.ini", std::ios::trunc);
  if (!file) {
    if (error)
      *error = "can't write " + (user_directory / "config.ini").string();
    return false;
  }
  file << "# ModernGekko frontend settings\n"
          "[Launcher]\n"
          "# auto (the system's language), en, es or ja\n"
          "language="
       << language << '\n'
       << "# Music in the launcher and the Netplay lobby: 0 (off) to 100\n"
          "music_volume="
       << config.music_volume << '\n'
       << "# Look on GitHub for a newer version when the launcher opens\n"
          "check_for_updates="
       << (config.check_for_updates ? "true" : "false") << '\n'
       << "# This is the game's internal render size, not the window size.\n"
          "[Video]\n"
          "resolution="
       << config.resolution << '\n'
       << "backend=" << graphics_backend << '\n'
       << "fullscreen=" << (config.fullscreen ? "true" : "false") << '\n'
       << "# The game window out of fullscreen: auto (fits the screen) or a\n"
       << "# size such as 1280x720\n"
       << "window_size=" << config.window_size << '\n'
       << "# 16:9, 4:3, stretch, ultrawide or custom\n"
       << "aspect_ratio=" << aspect_ratio << '\n'
       << "# The shape for custom, width:height (from 1:1 to 6:1), such as 21:9\n"
       << "custom_aspect=" << custom_aspect->width << ':' << custom_aspect->height << '\n'
       << "# Pictures beside a 4:3 game: none, random or a border's id\n"
       << "border=" << game_border << '\n'
       << "# Picture effect: none, remaster, outline (anime) or crt\n"
       << "effect=" << config.effect << '\n'
       << "# Screenshots (F9 or the in-game menu): game, 1920x1080, 2560x1440,\n"
       << "# 3840x2160 or 5120x2880, never below the game's own resolution\n"
       << "screenshot_resolution=" << config.screenshot_resolution << '\n'
       << "show_fps_in_title=" << (config.show_fps_in_title ? "true" : "false")
       << '\n'
       << "[Audio]\n"
       << "# The game's volume: 0 (silent) to 100\n"
       << "game_volume=" << config.game_volume << '\n'
       << "[Core]\n"
       << "# jit64 or staticrecomp. MODERNGEKKO_STATICRECOMP overrides it.\n"
       << "cpu_core=" << cpu_core << '\n'
       << "[Input]\n";
  for (std::size_t i = 0; i < config.controllers.size() && i < 4; ++i) {
    if (config.controllers[i].find_first_of("\r\n") != std::string::npos) {
      if (error)
        *error = "controller device cannot contain a newline";
      return false;
    }
    file << "controller" << i + 1 << '=' << config.controllers[i] << '\n';
  }
  file << "[Netplay]\n"
       << "nickname=" << config.netplay_nickname << '\n'
       << "address=" << config.netplay_address << '\n'
       << "port=" << config.netplay_port << '\n'
       << "buffer=" << config.netplay_buffer << '\n'
       << "# code (an invite code) or ip (the host's address)\n"
       << "connection=" << config.netplay_connection << '\n'
       << "# The server that hands out invite codes: host or host:port\n"
       << "invite_server=" << config.invite_server << '\n';
  return true;
}

bool SaveConfig(const fs::path &user_directory, std::string_view resolution,
                bool show_fps_in_title, std::string_view controller,
                std::string *error) {
  ConfigResult config = LoadConfig(user_directory, false);
  if (!config)
    config = {};
  config.resolution = resolution;
  config.show_fps_in_title = show_fps_in_title;
  config.controller = controller;
  config.controllers.clear();
  if (!controller.empty())
    config.controllers.emplace_back(controller);
  return SaveConfig(user_directory, config, error);
}

std::string ReadConfiguredController(const fs::path &user_directory) {
  const std::vector<std::string> controllers =
      ReadConfiguredControllers(user_directory);
  return controllers.empty() ? std::string{} : controllers.front();
}

std::vector<std::string>
ReadConfiguredControllers(const fs::path &user_directory) {
  std::ifstream input(ControllerConfigPath(user_directory));
  std::vector<std::string> controllers;
  std::string line;
  std::size_t controller_index = 4;
  const std::string_view section_prefix = ControllerSectionPrefix();
  while (std::getline(input, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.starts_with('[') && trimmed.ends_with(']')) {
      controller_index = 4;
      if (trimmed.size() == section_prefix.size() + 2 &&
          trimmed.starts_with(section_prefix) &&
          trimmed[section_prefix.size()] >= '1' &&
          trimmed[section_prefix.size()] <= '4')
        controller_index = static_cast<std::size_t>(
            trimmed[section_prefix.size()] - '1');
      continue;
    }
    if (controller_index >= 4)
      continue;
    const std::size_t separator = trimmed.find('=');
    if (separator != std::string::npos &&
        Trim(trimmed.substr(0, separator)) == "Device") {
      const std::string device = Trim(trimmed.substr(separator + 1));
      if (!device.empty()) {
        if (controllers.size() <= controller_index)
          controllers.resize(controller_index + 1);
        controllers[controller_index] = device;
      }
    }
  }
  std::erase(controllers, std::string{});
  return controllers;
}

bool ControllerConfigExists(const fs::path &user_directory) {
  std::error_code ec;
  return fs::is_regular_file(ControllerConfigPath(user_directory), ec);
}

bool GenerateControllerConfig(const fs::path &user_directory,
                              std::span<const std::string> controllers,
                              std::string *message) {
  if (controllers.empty() || controllers.size() > 4) {
    if (message)
      *message = "select between one and four connected SDL gamepads";
    return false;
  }
  for (const std::string &controller : controllers) {
    if (controller.empty() ||
        controller.find_first_of("\r\n") != std::string_view::npos) {
      if (message)
        *message = "select connected SDL gamepads";
      return false;
    }
  }

  const fs::path destination = ControllerConfigPath(user_directory);
  std::error_code ec;
  fs::create_directories(destination.parent_path(), ec);
  if (ec) {
    if (message)
      *message = "can't create controller config directory: " + ec.message();
    return false;
  }
  std::ofstream output(destination, std::ios::trunc);
  if (!output) {
    if (message)
      *message = "can't write " + destination.string();
    return false;
  }
  for (std::size_t i = 0; i < 4; ++i) {
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
    output << "[GCPad" << i + 1 << "]\n";
    if (i >= controllers.size())
      continue;
    // " | `<keyboard>:<key>`" on the first controller, nothing on the others.
    const auto key = [i](std::string_view name) {
      if (i != 0)
        return std::string();
      return " | `" + std::string(KEYBOARD_DEVICE) + ':' + std::string(name) +
             '`';
    };
    output << "Device = " << controllers[i] << '\n';
    // The first controller also answers to the keyboard, so it stays connected
    // when the gamepad isn't; Dolphin otherwise reports it unplugged.
    if (i == 0)
      output << "Options/Always Connected = True\n";
    output << "Buttons/A = `Button A`" << key("S") << '\n'
           << "Buttons/B = `Button B`" << key("D") << '\n'
           << "Buttons/X = `Button X`" << key("A") << '\n'
           << "Buttons/Y = `Button Y`" << key("W") << '\n'
           << "Buttons/Z = `Trigger R`" << key("Q") << '\n'
           << "Buttons/Start = Start" << key(KEY_START) << '\n'
           << "Main Stick/Up = `Left Y+`" << key(KEY_ARROWS[0]) << '\n'
           << "Main Stick/Down = `Left Y-`" << key(KEY_ARROWS[1]) << '\n'
           << "Main Stick/Left = `Left X-`" << key(KEY_ARROWS[2]) << '\n'
           << "Main Stick/Right = `Left X+`" << key(KEY_ARROWS[3]) << '\n'
           << "Main Stick/Calibration = 100.00\n"
           << "C-Stick/Up = `Right Y+`\n"
              "C-Stick/Down = `Right Y-`\n"
              "C-Stick/Left = `Right X-`\n"
              "C-Stick/Right = `Right X+`\n"
              "C-Stick/Calibration = 100.00\n"
           // L and R sit on the shoulder buttons. They are digital, so the
           // analog value is all or nothing, as on a fully pressed trigger.
           << "Triggers/L = `Shoulder L`" << key(KEY_LEFT_SHIFT) << '\n'
           << "Triggers/R = `Shoulder R`" << key("E") << '\n'
           << "Triggers/L-Analog = `Shoulder L`" << key(KEY_LEFT_SHIFT) << '\n'
           << "Triggers/R-Analog = `Shoulder R`" << key("E") << '\n'
           << "D-Pad/Up = `Pad N`\n"
              "D-Pad/Down = `Pad S`\n"
              "D-Pad/Left = `Pad W`\n"
              "D-Pad/Right = `Pad E`\n"
              "Rumble/Motor = `Motor L` | `Motor R`\n"
              // Not GameCube inputs, and Dolphin ignores them: the left
              // trigger is kept as the extra C button of the SR-Source controller
              // drawing, and Back and Guide (Select and Home on most pads) as
              // its − and triangle buttons, for mods. The launcher's tester
              // shows them.
              "Extra/C = `Trigger L`\n"
           << "Extra/Select = Back" << key(KEY_BACKSPACE) << '\n'
           << "Extra/Menu = Guide" << key("M") << '\n';
#else
    output << "[Wiimote" << i + 1 << "]\n";
    if (i >= controllers.size())
      continue;
    output << "Device = " << controllers[i] << '\n'
           << "Buttons/A = `Shoulder L`\n"
              "Buttons/B = `Shoulder R`\n"
              "Buttons/1 = `Button W`\n"
              "Buttons/2 = `Button S`\n"
              "Buttons/- = Back\n"
              "Buttons/+ = Start\n"
              "Buttons/Home = Guide\n"
              "D-Pad/Up = `Pad N` | `Left Y+`\n"
              "D-Pad/Down = `Pad S` | `Left Y-`\n"
              "D-Pad/Left = `Pad W` | `Left X-`\n"
              "D-Pad/Right = `Pad E` | `Left X+`\n"
              "IR/Up = `Cursor Y-`\n"
              "IR/Down = `Cursor Y+`\n"
              "IR/Left = `Cursor X-`\n"
              "IR/Right = `Cursor X+`\n"
              "Shake/X = `Trigger L`\n"
              "Shake/Y = `Trigger R`\n"
              "Shake/Z = `Trigger L`\n"
              "IRPassthrough/Object 1 X = `IR Object 1 X`\n"
              "IRPassthrough/Object 1 Y = `IR Object 1 Y`\n"
              "IRPassthrough/Object 1 Size = `IR Object 1 Size`\n"
              "IRPassthrough/Object 2 X = `IR Object 2 X`\n"
              "IRPassthrough/Object 2 Y = `IR Object 2 Y`\n"
              "IRPassthrough/Object 2 Size = `IR Object 2 Size`\n"
              "IRPassthrough/Object 3 X = `IR Object 3 X`\n"
              "IRPassthrough/Object 3 Y = `IR Object 3 Y`\n"
              "IRPassthrough/Object 3 Size = `IR Object 3 Size`\n"
              "IRPassthrough/Object 4 X = `IR Object 4 X`\n"
              "IRPassthrough/Object 4 Y = `IR Object 4 Y`\n"
              "IRPassthrough/Object 4 Size = `IR Object 4 Size`\n"
              "IMUAccelerometer/Up = `Accel Up`\n"
              "IMUAccelerometer/Down = `Accel Down`\n"
              "IMUAccelerometer/Left = `Accel Left`\n"
              "IMUAccelerometer/Right = `Accel Right`\n"
              "IMUAccelerometer/Forward = `Accel Forward`\n"
              "IMUAccelerometer/Backward = `Accel Backward`\n"
              "IMUGyroscope/Pitch Up = `Gyro Pitch Up`\n"
              "IMUGyroscope/Pitch Down = `Gyro Pitch Down`\n"
              "IMUGyroscope/Roll Left = `Gyro Roll Left`\n"
              "IMUGyroscope/Roll Right = `Gyro Roll Right`\n"
              "IMUGyroscope/Yaw Left = `Gyro Yaw Left`\n"
              "IMUGyroscope/Yaw Right = `Gyro Yaw Right`\n"
              "Rumble/Motor = Motor\n"
              "Extension = None\n"
              "Options/Sideways Wiimote = True\n";
#endif
  }
#ifndef MODERNGEKKO_GAMECUBE_CONTROLLERS
  output << "[BalanceBoard]\n";
#endif
  if (!output) {
    if (message)
      *message = "can't write " + destination.string();
    return false;
  }
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  DisconnectWiiRemotes(destination.parent_path());
#endif
  if (message)
    *message = std::to_string(controllers.size()) +
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
               " GameCube controller" +
#else
               " sideways Wii Remote" +
#endif
               (controllers.size() == 1 ? " mapped" : "s mapped");
  return true;
}

bool GenerateControllerConfig(const fs::path &user_directory,
                              std::string_view controller,
                              std::string *message) {
  const std::string value(controller);
  return GenerateControllerConfig(
      user_directory, std::span<const std::string>(&value, 1), message);
}

bool EnsureControllerConfig(const fs::path &user_directory,
                            std::span<const std::string> controllers,
                            std::string *message) {
  if (ControllerConfigExists(user_directory)) {
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
    // Checked on every start: Dolphin may have emptied the file on exit.
    DisconnectWiiRemotes(ControllerConfigPath(user_directory).parent_path());
#endif
    if (message)
      *message = "using existing controller profile";
    return true;
  }
  return GenerateControllerConfig(user_directory, controllers, message);
}

bool EnsureControllerConfig(const fs::path &user_directory,
                            std::string_view controller, std::string *message) {
  const std::string value(controller);
  return EnsureControllerConfig(
      user_directory, std::span<const std::string>(&value, 1), message);
}
GameMenuKeys GameMenuProfileKeys() {
  GameMenuKeys keys;
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  // Extra/Menu is Guide, and M on the keyboard; Extra/Select (Back: Select,
  // Share, View, − or the Steam Deck's ⧉; Backspace on the keyboard) opens it
  // too, as Guide is often taken before the game sees it (Steam, the Xbox Game
  // Bar, the Steam Deck). A accepts, B goes back, and the D-pad or the
  // control stick moves (the arrows on the keyboard).
  keys.profile_file = "GCPadNew.ini";
  keys.profile_section = "GCPad1";
  keys.open = {"Extra/Menu", "Extra/Select"};
  keys.accept = {"Buttons/A"};
  keys.back = {"Buttons/B"};
  keys.up = {"D-Pad/Up", "Main Stick/Up"};
  keys.down = {"D-Pad/Down", "Main Stick/Down"};
  keys.left = {"D-Pad/Left", "Main Stick/Left"};
  keys.right = {"D-Pad/Right", "Main Stick/Right"};
  keys.open_fallback = "Guide | Back | `" + std::string(KEYBOARD_DEVICE) + ":M`";
#else
  // HOME is Guide. The sideways Wii Remote's 2 and 1 sit where a pad's south
  // and west buttons are, and its D-pad also follows the left stick.
  keys.profile_file = "WiimoteNew.ini";
  keys.profile_section = "Wiimote1";
  keys.open = {"Buttons/Home"};
  keys.accept = {"Buttons/2", "Buttons/A"};
  keys.back = {"Buttons/1", "Buttons/B"};
  keys.up = {"D-Pad/Up"};
  keys.down = {"D-Pad/Down"};
  keys.left = {"D-Pad/Left"};
  keys.right = {"D-Pad/Right"};
  keys.open_fallback = "Guide";
#endif
  return keys;
}

GameMenuControlsLegend GameMenuControls() {
  GameMenuControlsLegend legend;
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  // The launcher tester's rows, but for the extra C and Select buttons,
  // which only mods use; the stick row also has the D-pad.
  legend.title = "GameCube";
  legend.rows = {
      {"A", {"Buttons/A"}},
      {"B", {"Buttons/B"}},
      {"X", {"Buttons/X"}},
      {"Y", {"Buttons/Y"}},
      {"Z", {"Buttons/Z"}},
      {"L", {"Triggers/L", "Triggers/L-Analog"}},
      {"R", {"Triggers/R", "Triggers/R-Analog"}},
      {"Start", {"Buttons/Start"}},
      {MODERNGEKKO_UI_TEXT("Stick"),
       {"Main Stick/Up", "Main Stick/Down", "Main Stick/Left",
        "Main Stick/Right", "D-Pad/Up", "D-Pad/Down", "D-Pad/Left",
        "D-Pad/Right"}},
      {MODERNGEKKO_UI_TEXT("Menu"), {"Extra/Menu", "Extra/Select"}},
  };
#else
  legend.title = "Wii Remote";
  legend.rows = {
      {"A", {"Buttons/A"}},
      {"B", {"Buttons/B"}},
      {"1", {"Buttons/1"}},
      {"2", {"Buttons/2"}},
      {"\xE2\x88\x92", {"Buttons/-"}},
      {"+", {"Buttons/+"}},
      {"HOME", {"Buttons/Home"}},
      {MODERNGEKKO_UI_TEXT("D-Pad"),
       {"D-Pad/Up", "D-Pad/Down", "D-Pad/Left", "D-Pad/Right"}},
      {MODERNGEKKO_UI_TEXT("Shake"), {"Shake/X", "Shake/Y", "Shake/Z"}},
  };
#endif
  return legend;
}

#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
std::string_view KeyboardDevice() { return KEYBOARD_DEVICE; }
#endif
} // namespace moderngekko::frontend
