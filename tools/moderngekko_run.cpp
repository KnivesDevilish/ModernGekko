#include "cache_affinity.hpp"
#include "frontend_config.hpp"
#include "dol_patch.hpp"
#include "game_borders.hpp"
#include "launcher_text.hpp"
#include "moderngekko/game.hpp"
#include "moderngekko/runtime.hpp"
#include "netplay_address.hpp"
#include "netplay_session.hpp"
#include "runner_exit_code.hpp"
#include "ui_text.hpp"

#include "Common/StringUtil.h"

#include <SDL3/SDL.h>

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
// For the affinity/priority setup in main(). WIN32_LEAN_AND_MEAN keeps the
// winsock and GDI surface out of a translation unit that only needs the
// processor-topology and process calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
#ifndef MODERNGEKKO_RUNNER_NAME
#define MODERNGEKKO_RUNNER_NAME "moderngekko-run"
#endif

#ifndef MODERNGEKKO_USER_DIRECTORY_NAME
#define MODERNGEKKO_USER_DIRECTORY_NAME "moderngekko"
#endif

volatile std::sig_atomic_t s_stop_requested = 0;

void HandleStopSignal(int) { s_stop_requested = 1; }

// The system's first preferred language, such as "es" (as in the launcher).
std::string SystemLanguageCode() {
  int count = 0;
  SDL_Locale **locales = SDL_GetPreferredLocales(&count);
  std::string language =
      locales && count > 0 && locales[0]->language ? locales[0]->language : "";
  SDL_free(locales);
  return language;
}

// What config.ini keeps for each of the in-game menu's Quick settings: the
// field and, choice by choice, the value written to it.
struct KeptSetting {
  enum class Field { Effect, Resolution, GameVolume, Border };
  Field field;
  std::vector<std::string> values;
};

// The in-game menu's Quick settings, as config.ini has them now: the picture
// effect, the internal resolution, the game's volume and, for a 4:3 game,
// the border. A change is written back to config.ini, so the launcher shows
// it and the next game starts with it.
void AddQuickSettings(moderngekko::RuntimeConfig *config,
                      const moderngekko::frontend::ConfigResult &frontend_config,
                      const moderngekko::frontend::GameBorderList &border_list) {
  using moderngekko::QuickSetting;
  std::vector<KeptSetting> kept;

  QuickSetting effect{QuickSetting::Kind::Effect, {}, 0};
  KeptSetting effect_kept{KeptSetting::Field::Effect, {}};
  for (const auto &option : moderngekko::frontend::SupportedEffects()) {
    if (frontend_config.effect == option.value)
      effect.selected = effect.choices.size();
    effect.choices.push_back({option.text, option.description, option.shader});
    effect_kept.values.emplace_back(option.value);
  }
  config->menu.quick_settings.push_back(std::move(effect));
  kept.push_back(std::move(effect_kept));

  // The launcher's sizes; a scale config.ini names without one of them is
  // added, so it can be chosen again.
  QuickSetting resolution{QuickSetting::Kind::InternalResolution, {}, 0};
  KeptSetting resolution_kept{KeptSetting::Field::Resolution, {}};
  bool listed = false;
  for (const auto &option : moderngekko::frontend::SupportedResolutions()) {
    if (option.dolphin_scale == frontend_config.dolphin_scale) {
      resolution.selected = resolution.choices.size();
      listed = true;
    }
    resolution.choices.push_back({option.text, {}, std::to_string(option.dolphin_scale)});
    resolution_kept.values.emplace_back(option.text);
  }
  if (!listed && frontend_config.dolphin_scale > 0) {
    resolution.selected = resolution.choices.size();
    resolution.choices.push_back(
        {frontend_config.resolution, {}, std::to_string(frontend_config.dolphin_scale)});
    resolution_kept.values.push_back(frontend_config.resolution);
  }
  config->menu.quick_settings.push_back(std::move(resolution));
  kept.push_back(std::move(resolution_kept));

  // In steps of 10; a volume between two steps shows as the nearest.
  QuickSetting volume{QuickSetting::Kind::GameVolume, {}, 0};
  KeptSetting volume_kept{KeptSetting::Field::GameVolume, {}};
  for (int step = 0; step <= 100; step += 10) {
    volume.choices.push_back({std::to_string(step) + " %", {}, std::to_string(step)});
    volume_kept.values.push_back(std::to_string(step));
  }
  volume.selected = static_cast<std::size_t>((frontend_config.game_volume + 5) / 10);
  config->menu.quick_settings.push_back(std::move(volume));
  kept.push_back(std::move(volume_kept));

  // The border only matters when the game is 4:3. Random shows the border
  // picked when the game opened.
  if (frontend_config.aspect_ratio == "4:3" && !border_list.borders.empty()) {
    const char *language =
        moderngekko::frontend::LanguageCode(moderngekko::frontend::ActiveLanguage());
    QuickSetting border{QuickSetting::Kind::Border, {}, 0};
    KeptSetting border_kept{KeptSetting::Field::Border, {}};
    border.choices.push_back(
        {MODERNGEKKO_UI_TEXT("None"), MODERNGEKKO_UI_TEXT("Black bars beside the game."), {}});
    border_kept.values.emplace_back("none");
    if (frontend_config.game_border == "random")
      border.selected = border.choices.size();
    border.choices.push_back({MODERNGEKKO_UI_TEXT("Random"),
                              MODERNGEKKO_UI_TEXT("One picked at random each time the game opens."),
                              config->graphics.border_id});
    border_kept.values.emplace_back("random");
    for (const auto &item : border_list.borders) {
      if (frontend_config.game_border == item.id)
        border.selected = border.choices.size();
      border.choices.push_back({item.Name(language), {}, item.id});
      border_kept.values.push_back(item.id);
    }
    config->menu.quick_settings.push_back(std::move(border));
    kept.push_back(std::move(border_kept));
  }

  config->menu.keep_quick_setting = [user_directory = config->user_directory,
                                     kept = std::move(kept)](std::size_t row,
                                                             std::size_t choice) {
    if (row >= kept.size() || choice >= kept[row].values.size())
      return;
    // Read again: only the one setting changes in config.ini.
    auto saved = moderngekko::frontend::LoadConfig(user_directory, false);
    if (!saved) {
      std::cerr << "quick setting not kept: config.ini: " << saved.error << '\n';
      return;
    }
    const std::string &value = kept[row].values[choice];
    switch (kept[row].field) {
    case KeptSetting::Field::Effect:
      saved.effect = value;
      break;
    case KeptSetting::Field::Resolution:
      saved.resolution = value;
      break;
    case KeptSetting::Field::GameVolume:
      saved.game_volume = std::stoi(value);
      break;
    case KeptSetting::Field::Border:
      saved.game_border = value;
      break;
    }
    std::string error;
    if (!moderngekko::frontend::SaveConfig(user_directory, saved, &error))
      std::cerr << "quick setting not kept: " << error << '\n';
  };
}

void Usage() {
  std::cerr << "usage: " MODERNGEKKO_RUNNER_NAME
               " [--game <extracted-root>] [--module <path>]\n"
               "       [--user-dir <path>] [--title <text>] [--load-state <path>]\n"
               "       [--graphics <backend>] [--audio <backend>]\n"
               "       [--mods <directory>] [--no-mods]\n"
               "       [--wayland] [-X11] [--headless] [--allow-interpreter]\n"
               "       [--cache-affinity | --no-cache-affinity]\n"
               "       [--netplay-host [--invite-code] | --netplay-join <host> |\n"
               "        --netplay-join-code <code>] [--netplay-port <port>]\n"
               "       [--invite-server <host[:port]>]\n"
               "       [--nickname <name>] [--buffer <auto|1-20>] "
               "[--controller <device>]...\n"
               "       With no --game, boots the path in "
               "<user-dir>/default-game.txt.\n";
}

std::filesystem::path
ReadDefaultGame(const std::filesystem::path &user_directory,
                const std::filesystem::path &executable_directory) {
#ifdef MODERNGEKKO_PORTABLE_DEFAULT_GAME
  const std::filesystem::path default_file =
      executable_directory / "default-game.txt";
#else
  const std::filesystem::path default_file =
      user_directory / "default-game.txt";
#endif
  std::ifstream file(default_file);
  std::string path;
  std::getline(file, path);
  if (!path.empty() && path.back() == '\r')
    path.pop_back();
  std::filesystem::path result(path);
  if (result.is_relative())
    result = executable_directory / result;
  return result;
}

std::filesystem::path DefaultUserDirectory() {
#ifdef MODERNGEKKO_USER_DIRECTORY_IN_DOCUMENTS
#if defined(_WIN32)
  if (const char *user_profile = std::getenv("USERPROFILE"))
    return std::filesystem::path(user_profile) / "Documents" /
           MODERNGEKKO_USER_DIRECTORY_NAME;
#endif
  if (const char *home = std::getenv("HOME"))
    return std::filesystem::path(home) / "Documents" /
           MODERNGEKKO_USER_DIRECTORY_NAME;
#endif
#if defined(_WIN32)
  if (const char *local_app_data = std::getenv("LOCALAPPDATA"))
    return std::filesystem::path(local_app_data) /
           MODERNGEKKO_USER_DIRECTORY_NAME;
#endif
  if (const char *xdg = std::getenv("XDG_DATA_HOME"))
    return std::filesystem::path(xdg) / MODERNGEKKO_USER_DIRECTORY_NAME;
  if (const char *home = std::getenv("HOME"))
    return std::filesystem::path(home) / ".local" / "share" /
           MODERNGEKKO_USER_DIRECTORY_NAME;
  return std::string(MODERNGEKKO_USER_DIRECTORY_NAME) + "-user";
}

std::string LibrarySuffix() {
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

std::filesystem::path ExecutableDirectory(const char *argv0) {
  std::error_code ec;
#if defined(__linux__)
  const std::filesystem::path proc_executable =
      std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec)
    return proc_executable.parent_path();
  ec.clear();
#endif
  const std::filesystem::path executable =
      std::filesystem::weakly_canonical(argv0, ec);
  return ec ? std::filesystem::current_path() : executable.parent_path();
}
} // namespace

int RunMain(int argc, char **argv) {
#if defined(_WIN32)
  if (!std::getenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD"))
    _putenv_s("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1");
#else
  setenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", 0);
#endif
  moderngekko::RuntimeConfig config;
  config.user_directory = DefaultUserDirectory();
  const std::filesystem::path executable_directory =
      ExecutableDirectory(argv[0]);
#ifdef MODERNGEKKO_DEFAULT_WINDOW_TITLE
  config.window_title = MODERNGEKKO_DEFAULT_WINDOW_TITLE;
#endif
  std::filesystem::path module_path;
  bool use_default_mods = true;
  std::optional<moderngekko::frontend::NetplayRole> netplay_role;
  std::string netplay_address;
  std::optional<std::uint16_t> netplay_port;
  std::string netplay_nickname;
  std::string netplay_buffer;
  std::vector<std::string> netplay_controllers;
  bool netplay_invite_code = false;
  std::string netplay_invite_server;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&](const char *option) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << option << " requires a value\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--game")
      config.game_root = value("--game");
    else if (arg == "--module")
      module_path = value("--module");
    else if (arg == "--user-dir")
      config.user_directory = value("--user-dir");
    else if (arg == "--title")
      config.window_title = value("--title");
    else if (arg == "--load-state")
    {
      // Checked here rather than left to the boot path: a state that is not
      // there would otherwise boot to the title screen and look like it worked.
      std::filesystem::path state = StringToPath(value("--load-state"));
      if (!std::filesystem::is_regular_file(state))
      {
        std::cerr << "savestate not found: " << PathToString(state) << '\n';
        return 2;
      }
      config.load_state_path = std::move(state);
    }
    else if (arg == "--graphics")
      config.graphics.backend = value("--graphics");
    else if (arg == "--audio")
      config.audio.backend = value("--audio");
    else if (arg == "--mods")
      config.mod_directories.emplace_back(value("--mods"));
    else if (arg == "--no-mods")
      use_default_mods = false;
    else if (arg == "-X11" || arg == "--x11")
      config.window_system = moderngekko::WindowSystem::X11;
    else if (arg == "--wayland")
      config.window_system = moderngekko::WindowSystem::Wayland;
    else if (arg == "--mute")
      config.audio.mute = true;
    else if (arg == "--automation-dir")
      config.automation.directory = value("--automation-dir");
    else if (arg == "--headless")
      config.headless = true;
    else if (arg == "--allow-interpreter")
      config.allow_interpreter = true;
    else if (arg == "--cache-affinity" || arg == "--no-cache-affinity")
      // Already handled before the run started, in PinProcessToLargestCache().
      // Accepted here so the parse does not reject it, and on every platform so
      // a shared script can pass it without knowing the host.
      ;
    else if (arg == "--netplay-host")
      netplay_role = moderngekko::frontend::NetplayRole::Host;
    else if (arg == "--netplay-join") {
      netplay_role = moderngekko::frontend::NetplayRole::Join;
      netplay_address = value("--netplay-join");
    } else if (arg == "--invite-code")
      netplay_invite_code = true;
    else if (arg == "--netplay-join-code") {
      netplay_role = moderngekko::frontend::NetplayRole::Join;
      netplay_invite_code = true;
      const auto code =
          moderngekko::frontend::NormalizeInviteCode(value("--netplay-join-code"));
      if (!code) {
        std::cerr << "--netplay-join-code takes the host's 8-character code\n";
        return 2;
      }
      netplay_address = *code;
    } else if (arg == "--invite-server") {
      netplay_invite_server = value("--invite-server");
    } else if (arg == "--netplay-port") {
      const std::string port_value = value("--netplay-port");
      unsigned int port = 0;
      const auto parsed = std::from_chars(
          port_value.data(), port_value.data() + port_value.size(), port);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != port_value.data() + port_value.size() || port == 0 ||
          port > 65535) {
        std::cerr << "--netplay-port must be between 1 and 65535\n";
        return 2;
      }
      netplay_port = static_cast<std::uint16_t>(port);
    } else if (arg == "--nickname")
      netplay_nickname = value("--nickname");
    else if (arg == "--buffer")
      netplay_buffer = value("--buffer");
    else if (arg == "--controller")
      netplay_controllers.emplace_back(value("--controller"));
    else if (arg == "--help" || arg == "-h") {
      Usage();
      return 0;
    } else {
      std::cerr << "unknown option: " << arg << '\n';
      Usage();
      return 2;
    }
  }
  if (config.game_root.empty())
    config.game_root =
        ReadDefaultGame(config.user_directory, executable_directory);
  if (config.game_root.empty()) {
    std::cerr << "no game configured; use --game once or create "
              << (config.user_directory / "default-game.txt") << '\n';
    Usage();
    return 2;
  }

  auto frontend_config =
      moderngekko::frontend::LoadConfig(config.user_directory, true);
  if (!frontend_config) {
    std::cerr << "invalid config.ini: " << frontend_config.error << '\n';
    return 2;
  }
  config.graphics.internal_resolution_scale = frontend_config.dolphin_scale;
  if (config.graphics.backend.empty())
    config.graphics.backend = frontend_config.graphics_backend;
  config.fullscreen = frontend_config.fullscreen;
  config.show_fps_in_title = frontend_config.show_fps_in_title;
  if (frontend_config.aspect_ratio == "4:3")
    config.graphics.aspect_ratio = moderngekko::AspectRatio::Standard;
  else if (frontend_config.aspect_ratio == "stretch")
    config.graphics.aspect_ratio = moderngekko::AspectRatio::Stretch;
  else
    config.graphics.aspect_ratio = moderngekko::AspectRatio::Widescreen;

  // The pictures beside a game that doesn't fill the window: the one
  // config.ini names, none, or for "random" one picked now, so each time the
  // game opens it may be another. Resources/borders is the build's
  // (MODERNGEKKO_GAME_BORDERS); the runtime reports problems with it. The
  // folder is given even without a border, for the in-game menu to show one.
  const std::filesystem::path borders = executable_directory / "Resources" / "borders";
  const moderngekko::frontend::GameBorderList border_list =
      moderngekko::frontend::LoadGameBorders(borders);
  if (!border_list.borders.empty())
    config.graphics.border_folder = borders;
  {
    const std::uint64_t random_number =
        (static_cast<std::uint64_t>(std::random_device{}()) << 32) ^
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    if (const auto *border = moderngekko::frontend::ChooseGameBorder(
            border_list, frontend_config.game_border, random_number))
      config.graphics.border_id = border->id;
  }
  // The picture effect and the game's volume. An effect this build lacks is
  // none.
  if (const auto *effect = moderngekko::frontend::FindEffect(frontend_config.effect))
    config.graphics.post_processing_shader = effect->shader;
  config.audio.volume = frontend_config.game_volume;
  // Screenshots at the chosen resolution, whatever the game is shown at.
  config.graphics.screenshot_scale =
      moderngekko::frontend::FindScreenshotResolution(frontend_config.screenshot_resolution)
          .dolphin_scale;

  // The menu over the game (M, or Guide on a pad), in the launcher's language:
  // config.ini's, or the system's. It answers to the first controller.
  moderngekko::frontend::ActiveLanguage() = moderngekko::frontend::ResolveLanguage(
      frontend_config.language, SystemLanguageCode());
  {
    moderngekko::frontend::GameMenuKeys keys =
        moderngekko::frontend::GameMenuProfileKeys();
    config.menu.enabled = true;
    config.menu.profile_file = std::move(keys.profile_file);
    config.menu.profile_section = std::move(keys.profile_section);
    config.menu.open_keys = std::move(keys.open);
    config.menu.accept_keys = std::move(keys.accept);
    config.menu.back_keys = std::move(keys.back);
    config.menu.up_keys = std::move(keys.up);
    config.menu.down_keys = std::move(keys.down);
    config.menu.left_keys = std::move(keys.left);
    config.menu.right_keys = std::move(keys.right);
    config.menu.open_fallback = std::move(keys.open_fallback);
    config.menu.release_directory = executable_directory;
    AddQuickSettings(&config, frontend_config, border_list);
    moderngekko::frontend::GameMenuControlsLegend legend =
        moderngekko::frontend::GameMenuControls();
    config.menu.controls_title = std::move(legend.title);
    for (auto &row : legend.rows)
      config.menu.controls.push_back({row.label, std::move(row.keys)});
  }

#if defined(__linux__)
  // The system's volume mixer lists the game under the name its audio
  // library gives: Dolphin's ("Dolphin Emulator"). PulseAudio, and PipeWire's
  // PulseAudio layer, take a replacement from the environment, so the game
  // shows up under its window title instead. A value already set in the
  // shell is kept.
  if (config.window_title && !config.window_title->empty())
    setenv("PULSE_PROP_OVERRIDE_application.name", config.window_title->c_str(), 0);
#endif

  // CPU core: config.ini decides unless MODERNGEKKO_STATICRECOMP is already set,
  // so the shell can still force either core for testing. The runtime reads
  // the variable once, in Runtime::Create, so it must be set before that call
  // (solo and netplay both reach it below).
  {
    const char *forced = std::getenv("MODERNGEKKO_STATICRECOMP");
    const bool forced_by_environment = forced && *forced;
    if (!forced_by_environment) {
      const char *value =
          frontend_config.cpu_core == "staticrecomp" ? "1" : "0";
#if defined(_WIN32)
      _putenv_s("MODERNGEKKO_STATICRECOMP", value);
#else
      setenv("MODERNGEKKO_STATICRECOMP", value, 1);
#endif
    }
    // Same rule as SelectCPUCore() in the runtime: only a leading '0' is JIT.
    const char *effective = std::getenv("MODERNGEKKO_STATICRECOMP");
    const bool static_recomp = !effective || !*effective || *effective != '0';
#ifdef _M_ARM_64
    const char *jit_name = "JITARM64";
#else
    const char *jit_name = "JIT64";
#endif
    std::cout << "cpu core: " << (static_recomp ? "StaticRecomp" : jit_name)
              << (forced_by_environment ? " (from MODERNGEKKO_STATICRECOMP)"
                                        : " (from config.ini)")
              << '\n';
  }

  if (use_default_mods) {
    config.mod_directories.push_back(executable_directory / "Mods");
    config.mod_directories.push_back(config.user_directory / "Mods");
  }

  std::string solo_controller = frontend_config.controller;
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  // Without a configured gamepad the profile is written for the keyboard, as
  // the launcher does; a gamepad chosen later takes its place.
  if (solo_controller.empty())
    solo_controller = moderngekko::frontend::KeyboardDevice();
#endif
  if (!netplay_role && !solo_controller.empty()) {
    std::string controller_message;
    if (!moderngekko::frontend::EnsureControllerConfig(
            config.user_directory, solo_controller,
            &controller_message)) {
      std::cerr << "controller configuration: " << controller_message << '\n';
      return 2;
    }
    std::cout << "controller configuration: " << controller_message << '\n';
  }

#ifdef MODERNGEKKO_DOL_PATCH_MANIFEST
  bool dol_changed = false;
  std::string dol_patch_error;
  if (!moderngekko::frontend::ApplyDolPatchManifest(
          config.game_root / "sys" / "main.dol",
          executable_directory / MODERNGEKKO_DOL_PATCH_MANIFEST, &dol_changed,
          &dol_patch_error)) {
    std::cerr << "DOL patching failed: " << dol_patch_error << '\n';
    return 2;
  }
  if (dol_changed)
    std::cout << "Applied native DOL patches\n";
#endif
  // Hashing every file under files/ takes tens of seconds on a multi-GB game.
  // The runner only needs that hash for a pinned asset check, and the runtime
  // never needs it to boot. Netplay is unaffected: RunNetplayLobby inspects the
  // game again with the full hash for its compatibility fingerprint.
  moderngekko::GameInspectOptions inspect_options;
#ifndef MODERNGEKKO_REQUIRED_ASSETS_SHA256
  inspect_options.hash_assets = false;
#endif
  config.hash_game_assets = false;
  const auto inspected =
      moderngekko::InspectGame(config.game_root, inspect_options);
  if (!inspected) {
    std::cerr << "invalid game: " << inspected.error << '\n';
    return 2;
  }

#ifdef MODERNGEKKO_REQUIRED_DISC_ID
  if (inspected.metadata->disc_id != MODERNGEKKO_REQUIRED_DISC_ID) {
    std::cerr << "unsupported disc ID: expected "
              << MODERNGEKKO_REQUIRED_DISC_ID << ", got "
              << inspected.metadata->disc_id << '\n';
    return 2;
  }
#endif
#ifdef MODERNGEKKO_REQUIRED_DOL_SHA256
  if (inspected.metadata->dol_sha256 != MODERNGEKKO_REQUIRED_DOL_SHA256) {
    std::cerr << "unsupported main DOL: this release requires its pinned game build\n";
    return 2;
  }
#endif
#ifdef MODERNGEKKO_REQUIRED_REL_SHA256
  if (inspected.metadata->rel_sha256 != MODERNGEKKO_REQUIRED_REL_SHA256) {
    std::cerr << "unsupported _Main.rel: this release requires its pinned game build\n";
    return 2;
  }
#endif
#ifdef MODERNGEKKO_REQUIRED_ASSETS_SHA256
  if (inspected.metadata->assets_sha256 !=
      MODERNGEKKO_REQUIRED_ASSETS_SHA256) {
    std::cerr << "unsupported game assets: this release requires its pinned game build\n";
    return 2;
  }
#endif

  // Compatibility discovery belongs to the runner, never the runtime library.
  if (module_path.empty()) {
    if (const char *env = std::getenv("STATICRECOMP_MODULE"))
      module_path = env;
    else {
      const std::string module_name =
          "g" + inspected.metadata->disc_id + "_recomp" + LibrarySuffix();
      const auto bundled = executable_directory / module_name;
      const auto user_module =
          config.user_directory / "StaticRecompModules" / module_name;
      if (std::filesystem::is_regular_file(bundled))
        module_path = bundled;
      else if (std::filesystem::is_regular_file(user_module))
        module_path = user_module;
    }
  }
  if (!module_path.empty())
    config.module =
        moderngekko::ModuleSource::DynamicPath(std::move(module_path));

#if defined(__linux__) || defined(_WIN32)
  if (!config.headless && config.graphics.backend.empty())
    config.graphics.backend = "Vulkan";
#endif

  if (netplay_role) {
    moderngekko::frontend::NetplayOptions options;
    options.role = *netplay_role;
    options.address = netplay_address.empty() ? frontend_config.netplay_address
                                              : netplay_address;
    // With an invite code the host is found through the invite server; a
    // joining player's address is then the host's code.
    options.invite_code = netplay_invite_code;
    options.invite_server = netplay_invite_server.empty()
                                ? frontend_config.invite_server
                                : netplay_invite_server;
    options.port = netplay_port.value_or(frontend_config.netplay_port);
    options.nickname = netplay_nickname.empty()
                           ? frontend_config.netplay_nickname
                           : netplay_nickname;
    options.buffer = netplay_buffer.empty() ? frontend_config.netplay_buffer
                                            : netplay_buffer;
    const std::vector<std::string> configured_controllers =
        moderngekko::frontend::ReadConfiguredControllers(config.user_directory);
    options.controllers =
        netplay_controllers.empty()
            ? (configured_controllers.empty() ? frontend_config.controllers
                                              : configured_controllers)
            : netplay_controllers;
    if (options.controllers.empty() && !frontend_config.controller.empty())
      options.controllers.push_back(frontend_config.controller);
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
    // As in solo play: with nothing configured the player uses the keyboard.
    if (options.controllers.empty())
      options.controllers.emplace_back(moderngekko::frontend::KeyboardDevice());
#endif
    if (options.controllers.empty()) {
      std::cerr << "netplay requires at least one selected controller\n";
      return 2;
    }
    if (!options.invite_code)
      frontend_config.netplay_address = options.address;
    frontend_config.netplay_connection = options.invite_code ? "code" : "ip";
    frontend_config.invite_server = options.invite_server;
    frontend_config.netplay_port = options.port;
    frontend_config.netplay_nickname = options.nickname;
    frontend_config.netplay_buffer = options.buffer;
    frontend_config.controllers = options.controllers;
    frontend_config.controller = options.controllers.front();
    std::string controller_message;
    if (!moderngekko::frontend::EnsureControllerConfig(
            config.user_directory, options.controllers, &controller_message)) {
      std::cerr << "controller configuration: " << controller_message << '\n';
      return 2;
    }
    std::string save_error;
    if (!moderngekko::frontend::SaveConfig(config.user_directory,
                                           frontend_config, &save_error)) {
      std::cerr << "configuration: " << save_error << '\n';
      return 2;
    }
    std::cerr << "netplay: runner started\n";
    return moderngekko::frontend::RunNetplayLobby(
        std::move(config), std::move(frontend_config), std::move(options));
  }

  auto created = moderngekko::Runtime::Create(std::move(config));
  if (!created) {
    std::cerr << "initialization failed: " << created.error->message << '\n';
    return 1;
  }
  std::cout << "audio backend: " << created.runtime->GetConfig().audio.backend
            << '\n';

  std::signal(SIGINT, HandleStopSignal);
  std::signal(SIGTERM, HandleStopSignal);
  std::jthread signal_watcher([&](std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
      if (s_stop_requested) {
        s_stop_requested = 0;
        created.runtime->RequestStop();
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });
  const moderngekko::RuntimeRunResult result = created.runtime->Run();
  signal_watcher.request_stop();
  if (result.error) {
    std::cerr << "runtime failed: " << result.error->message << '\n';
    return 1;
  }
  if (result.reason == moderngekko::RuntimeExitReason::Quit)
    return moderngekko::frontend::RUNNER_EXIT_QUIT;
  return 0;
}

// Optionally confine the process to the cores that share the largest L3.
//
// The emulated CPU is a single serial instruction stream, so nothing here is
// about parallelism -- core usage stays at 1.00 either way. It is about cache
// residency. A recompiled module is one dispatch switch spanning the whole
// game, which lives or dies on staying in L3, and on a chip with 3D V-Cache on
// one CCD only, Windows migrating the thread between dies makes it repeatedly
// lose its working set. Measured on a 9950X3D: 55.41 -> 70.42 fps, +27%.
//
// This applies to every Dolphin thread, not only the emulated CPU thread, so it
// stays opt-in: enable it with --cache-affinity, or MODERNGEKKO_CACHE_AFFINITY=1
// for callers that cannot add an argument. --no-cache-affinity forces it off
// even when the environment asks for it, which is what an A/B harness wants.
//
// Measured across four titles on a 9950X3D (96 MB L3 on one CCD), C backend,
// three interleaved rounds per scene, same module and savestate throughout:
//
//   Pokemon Colosseum        +20.6%    Mario Kart: Double Dash!!   +32.9%
//   Luigi's Mansion          +11.7%    Skyward Sword               +35.6%
//
// Twelve of twelve scenes improved, +5% to +43%, mean +25.2%. Pinning also cuts
// run-to-run spread sharply -- one Colosseum scene went from +-0.297 to +-0.015
// standard error over three samples -- so it makes benchmarking cheaper as well
// as faster. All of that is one machine and one topology, which is why the flag
// stays default-off: a uniform-L3 part has nothing to gain, and a caller who
// pins the wrong way can only lose.
//
// argv is scanned directly because pinning has to happen before the run starts,
// which is well before the normal argument parse in RunMain().
void PinProcessToLargestCache(int argc, char **argv) {
  bool enabled = moderngekko::frontend::AffinityEnabled(
      std::getenv("MODERNGEKKO_CACHE_AFFINITY"));
  for (int i = 1; i < argc; i++) {
    const std::string_view arg(argv[i]);
    if (arg == "--cache-affinity")
      enabled = true;
    else if (arg == "--no-cache-affinity")
      enabled = false;
  }
  if (!enabled)
    return;

#if defined(_WIN32)

  DWORD length = 0;
  GetLogicalProcessorInformationEx(RelationCache, nullptr, &length);
  if (length == 0)
    return;
  std::vector<char> buffer(length);
  if (!GetLogicalProcessorInformationEx(
          RelationCache,
          reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length))
    return;

  const moderngekko::frontend::CacheDomain domain =
      moderngekko::frontend::LargestSharedCache(buffer.data(), length);
  if (!domain)
    return;

  if (moderngekko::frontend::ApplyCacheDomain(GetCurrentProcess(), domain).affinity_set) {
    std::cout << "[perf] pinned to the cores sharing the largest L3 (" << (domain.size >> 20)
              << " MB), mask 0x" << std::hex << static_cast<unsigned long long>(domain.mask)
              << std::dec << '\n';
  }

#elif defined(__linux__)
  const moderngekko::frontend::CacheDomain domain =
      moderngekko::frontend::LargestSharedCache();
  if (!domain)
    return;

  // 0, not getpid(): this has to cover the threads Dolphin spawns after it, and
  // a pid argument is documented to move the calling thread only.
  if (moderngekko::frontend::ApplyCacheDomain(0, domain).affinity_set) {
    std::cout << "[perf] pinned to the " << domain.cpus.size()
              << " CPUs sharing the largest L3 (" << (domain.size >> 20) << " MB)"
              << '\n';
  }
#endif
}

int main(int argc, char **argv) {
  try {
    PinProcessToLargestCache(argc, argv);
    return RunMain(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "fatal error: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "fatal error: unknown exception\n";
  }
  return 1;
}
