#include "frontend_config.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
constexpr const char *CONTROLLER_CONFIG_NAME = "GCPadNew.ini";
#else
constexpr const char *CONTROLLER_CONFIG_NAME = "WiimoteNew.ini";
#endif

int main() {
  namespace fs = std::filesystem;
  const fs::path directory =
      fs::temp_directory_path() /
      ("moderngekko-frontend-config-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));

  std::string error;
  const std::string controller = "SDL/0/Test Controller";
  if (!moderngekko::frontend::SaveConfig(directory, "1920x1080", false,
                                         controller, &error))
    return 1;

  const auto loaded = moderngekko::frontend::LoadConfig(directory, false);
  if (!loaded || loaded.dolphin_scale != 3 || loaded.show_fps_in_title ||
      loaded.controller != controller ||
      loaded.graphics_backend != "Vulkan" || loaded.aspect_ratio != "16:9") {
    return 2;
  }

  moderngekko::frontend::ConfigResult netplay_config = loaded;
  netplay_config.graphics_backend = "OpenGL";
  netplay_config.fullscreen = true;
  netplay_config.controllers = {controller, "SDL/1/Second Controller"};
  netplay_config.controller = controller;
  netplay_config.netplay_nickname = "Kirby";
  netplay_config.netplay_address = "192.168.1.50";
  netplay_config.netplay_port = 34567;
  netplay_config.netplay_buffer = "auto";
  // Invite codes by default, from Dolphin's public server.
  if (netplay_config.netplay_connection != "code" ||
      netplay_config.invite_server != "stun.dolphin-emu.org")
    return 20;
  netplay_config.netplay_connection = "ip";
  netplay_config.invite_server = "203.0.113.5:6262";
  if (!moderngekko::frontend::SaveConfig(directory, netplay_config, &error))
    return 6;
  const auto netplay_loaded =
      moderngekko::frontend::LoadConfig(directory, false);
  if (!netplay_loaded ||
      netplay_loaded.controllers != netplay_config.controllers ||
      netplay_loaded.netplay_nickname != "Kirby" ||
      netplay_loaded.netplay_address != "192.168.1.50" ||
      netplay_loaded.netplay_port != 34567 ||
      netplay_loaded.netplay_buffer != "auto" ||
      netplay_loaded.netplay_connection != "ip" ||
      netplay_loaded.invite_server != "203.0.113.5:6262" ||
      netplay_loaded.graphics_backend != "OGL" ||
      !netplay_loaded.fullscreen) {
    return 7;
  }
  auto invalid_invite = netplay_config;
  invalid_invite.netplay_connection = "carrier pigeon";
  if (moderngekko::frontend::SaveConfig(directory, invalid_invite, &error))
    return 21;
  invalid_invite = netplay_config;
  invalid_invite.invite_server = "stun.example.org:99999";
  if (moderngekko::frontend::SaveConfig(directory, invalid_invite, &error))
    return 22;

  auto invalid_netplay = netplay_config;
  invalid_netplay.netplay_address = "not a host";
  if (moderngekko::frontend::SaveConfig(directory, invalid_netplay, &error))
    return 8;
  invalid_netplay = netplay_config;
  invalid_netplay.netplay_nickname = std::string(31, 'K');
  if (moderngekko::frontend::SaveConfig(directory, invalid_netplay, &error))
    return 9;
  invalid_netplay = netplay_config;
  invalid_netplay.graphics_backend = "Direct3D 9";
  if (moderngekko::frontend::SaveConfig(directory, invalid_netplay, &error))
    return 13;
  if (!moderngekko::frontend::GenerateControllerConfig(
          directory, netplay_config.controllers, &error))
    return 3;
  if (moderngekko::frontend::ReadConfiguredController(directory) != controller)
    return 4;
  if (moderngekko::frontend::ReadConfiguredControllers(directory) !=
      netplay_config.controllers)
    return 10;

  // Scoped so the handle is closed before the cleanup below. Windows refuses
  // to delete a file that is still open, where POSIX allows it, so leaving
  // these open makes remove_all throw there and only there.
  std::string generated;
  {
    std::ifstream input(directory / "Config" / CONTROLLER_CONFIG_NAME);
    generated.assign(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
  }
#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  if (!generated.contains("Buttons/A = `Button A`\n") ||
      !generated.contains("Buttons/Z = `Trigger R`\n") ||
      !generated.contains("Main Stick/Up = `Left Y+`\n") ||
      !generated.contains("C-Stick/Up = `Right Y+`\n") ||
      !generated.contains("Triggers/L-Analog = `Shoulder L`\n") ||
      !generated.contains("Extra/C = `Trigger L`\n") ||
      // Select and Menu, kept for mods: Back and Guide, and on the first
      // controller Backspace and M as well.
      !generated.contains("Extra/Select = Back | `") ||
      !generated.contains(":M`\n") ||
      !generated.contains("Extra/Select = Back\nExtra/Menu = Guide\n") ||
      !generated.contains("Rumble/Motor = `Motor L` | `Motor R`\n") ||
      !generated.contains("[GCPad2]\nDevice = SDL/1/Second Controller\n") ||
      // Only the first controller also answers to the keyboard, and so stays
      // connected without its gamepad.
      !generated.contains("[GCPad1]\nDevice = SDL/0/Test Controller\n"
                          "Options/Always Connected = True\n"
                          "Buttons/A = `Button A` | `") ||
      !generated.contains("Device = SDL/1/Second Controller\n"
                          "Buttons/A = `Button A`\n") ||
      // Its R trigger is also the E key; the D-pad has no keys.
      !generated.contains(":E`\nTriggers/L-Analog") ||
      generated.contains("D-Pad/Up = `Pad N` | `") ||
      generated.contains("[Wiimote") || generated.contains("[BalanceBoard]")) {
    return 5;
  }
  // The Wii Remote is left disconnected.
  const auto read_wii_remotes = [&] {
    std::ifstream input(directory / "Config" / "WiimoteNew.ini");
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
  };
  if (read_wii_remotes() != "[Wiimote1]\nSource = 0\n")
    return 14;
#else
  if (!generated.contains("Buttons/A = `Shoulder L`\n") ||
      !generated.contains("Buttons/1 = `Button W`\n") ||
      !generated.contains("Buttons/2 = `Button S`\n") ||
      !generated.contains("Shake/X = `Trigger L`\n") ||
      !generated.contains("D-Pad/Up = `Pad N` | `Left Y+`\n") ||
      !generated.contains("D-Pad/Right = `Pad E` | `Left X+`\n") ||
      !generated.contains("Extension = None\n") ||
      !generated.contains("Options/Sideways Wiimote = True\n") ||
      !generated.contains("[Wiimote2]\nDevice = SDL/1/Second Controller\n") ||
      generated.contains("Nunchuk/")) {
    return 5;
  }
#endif

  // The in-game menu reads the first controller of this profile: every key it
  // names is in that controller's section, and it has a menu button.
  {
    const moderngekko::frontend::GameMenuKeys menu =
        moderngekko::frontend::GameMenuProfileKeys();
    const std::string header = "[" + menu.profile_section + "]\n";
    const std::size_t start = generated.find(header);
    if (menu.profile_file != CONTROLLER_CONFIG_NAME || start == std::string::npos ||
        menu.open.empty() || menu.accept.empty() || menu.back.empty() || menu.up.empty() ||
        menu.down.empty() || menu.left.empty() || menu.right.empty() ||
        menu.open_fallback.empty())
      return 43;
    const std::size_t end = generated.find("\n[", start);
    const std::string section =
        "\n" + generated.substr(start, end == std::string::npos ? std::string::npos
                                                                : end + 1 - start);
    for (const auto *keys : {&menu.open, &menu.accept, &menu.back, &menu.up, &menu.down,
                             &menu.left, &menu.right}) {
      for (const std::string &key : *keys) {
        if (!section.contains("\n" + key + " = "))
          return 44;
      }
    }
  }

#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  const std::string custom =
      "[GCPad1]\nDevice = SDL/9/Custom Controller\nButtons/A = Custom\n";
#else
  const std::string custom =
      "[Wiimote1]\nDevice = SDL/9/Custom Controller\nButtons/1 = Custom\n";
#endif
  {
    std::ofstream output(directory / "Config" / CONTROLLER_CONFIG_NAME,
                         std::ios::trunc);
    output << custom;
  }
  if (!moderngekko::frontend::EnsureControllerConfig(
          directory, netplay_config.controllers, &error))
    return 11;
  std::string preserved;
  {
    std::ifstream custom_input(directory / "Config" / CONTROLLER_CONFIG_NAME);
    preserved.assign(std::istreambuf_iterator<char>(custom_input),
                     std::istreambuf_iterator<char>());
  }
  if (preserved != custom || moderngekko::frontend::ReadConfiguredController(
                                 directory) != "SDL/9/Custom Controller")
    return 12;

#ifdef MODERNGEKKO_GAMECUBE_CONTROLLERS
  // Dolphin empties WiimoteNew.ini on exit when its sections have no keys;
  // the next start disconnects the Wii Remote again. A player's own Wii Remote
  // setup is left alone.
  for (const std::string& emptied : {std::string{}, std::string("[Wiimote1]\n[Wiimote2]\n")}) {
    std::ofstream(directory / "Config" / "WiimoteNew.ini", std::ios::trunc) << emptied;
    if (!moderngekko::frontend::EnsureControllerConfig(
            directory, netplay_config.controllers, &error) ||
        read_wii_remotes() != "[Wiimote1]\nSource = 0\n")
      return 15;
  }
  const std::string own = "[Wiimote1]\nDevice = SDL/0/Test Controller\n";
  std::ofstream(directory / "Config" / "WiimoteNew.ini", std::ios::trunc) << own;
  if (!moderngekko::frontend::EnsureControllerConfig(
          directory, netplay_config.controllers, &error) ||
      read_wii_remotes() != own)
    return 16;

  // Without a gamepad the profile is written for the keyboard: it stays
  // connected and reads the same keys.
  {
    const fs::path keyboard_only = directory / "keyboard-only";
    const std::string keyboard(moderngekko::frontend::KeyboardDevice());
    if (keyboard.empty() ||
        !moderngekko::frontend::GenerateControllerConfig(keyboard_only, keyboard, &error))
      return 17;
    std::ifstream input(keyboard_only / "Config" / CONTROLLER_CONFIG_NAME);
    const std::string profile((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    if (!profile.starts_with("[GCPad1]\nDevice = " + keyboard +
                             "\nOptions/Always Connected = True\n"
                             "Buttons/A = `Button A` | `" + keyboard + ":") ||
        moderngekko::frontend::ReadConfiguredController(keyboard_only) != keyboard)
      return 18;
  }
#endif

  // Aspect ratio: saved, kept by the short SaveConfig the Settings page uses,
  // read in any case, and an unknown shape is refused.
  {
    const fs::path aspect = directory / "aspect";
    auto aspect_config = netplay_config;
    aspect_config.aspect_ratio = "4:3";
    if (!moderngekko::frontend::SaveConfig(aspect, aspect_config, &error) ||
        moderngekko::frontend::LoadConfig(aspect, false).aspect_ratio != "4:3")
      return 19;
    if (!moderngekko::frontend::SaveConfig(aspect, "1920x1080", true, controller,
                                           &error) ||
        moderngekko::frontend::LoadConfig(aspect, false).aspect_ratio != "4:3")
      return 20;
    aspect_config.aspect_ratio = "21:9";
    if (moderngekko::frontend::SaveConfig(aspect, aspect_config, &error))
      return 21;
    {
      std::ofstream file(aspect / "config.ini", std::ios::trunc);
      file << "[Video]\nresolution=1920x1080\naspect_ratio = Stretch\n";
    }
    if (moderngekko::frontend::LoadConfig(aspect, false).aspect_ratio != "stretch")
      return 22;
    {
      std::ofstream file(aspect / "config.ini", std::ios::trunc);
      file << "[Video]\nresolution=1920x1080\naspect_ratio=5:4\n";
    }
    if (moderngekko::frontend::LoadConfig(aspect, false))
      return 23;
    if (moderngekko::frontend::SupportedAspectRatios().front().value !=
        std::string("16:9"))
      return 24;
  }

  // Language: "auto" until chosen, saved, kept by the short SaveConfig, read
  // in other spellings, and an unknown language is refused.
  {
    const fs::path language = directory / "language";
    if (moderngekko::frontend::LoadConfig(language, true).language != "auto")
      return 25;
    auto language_config = moderngekko::frontend::LoadConfig(language, false);
    language_config.language = "es";
    if (!moderngekko::frontend::SaveConfig(language, language_config, &error) ||
        !moderngekko::frontend::SaveConfig(language, "1920x1080", true, controller,
                                           &error) ||
        moderngekko::frontend::LoadConfig(language, false).language != "es")
      return 26;
    language_config.language = "fr";
    if (moderngekko::frontend::SaveConfig(language, language_config, &error))
      return 27;
    {
      std::ofstream file(language / "config.ini", std::ios::trunc);
      file << "[Launcher]\nlanguage = English\n[Video]\nresolution=1920x1080\n";
    }
    if (moderngekko::frontend::LoadConfig(language, false).language != "en")
      return 28;
    // Japanese, as "ja" or "Japanese".
    language_config.language = "ja";
    if (!moderngekko::frontend::SaveConfig(language, language_config, &error) ||
        moderngekko::frontend::LoadConfig(language, false).language != "ja")
      return 36;
    {
      std::ofstream file(language / "config.ini", std::ios::trunc);
      file << "[Launcher]\nlanguage=Japanese\n[Video]\nresolution=1920x1080\n";
    }
    if (moderngekko::frontend::LoadConfig(language, false).language != "ja")
      return 37;
  }

  // Music volume: 70 until changed, saved, kept by the short SaveConfig, and
  // only 0 to 100.
  {
    const fs::path music = directory / "music";
    auto music_config = moderngekko::frontend::LoadConfig(music, true);
    if (music_config.music_volume != 70)
      return 29;
    music_config.music_volume = 0;
    if (!moderngekko::frontend::SaveConfig(music, music_config, &error) ||
        !moderngekko::frontend::SaveConfig(music, "1920x1080", true, controller, &error) ||
        moderngekko::frontend::LoadConfig(music, false).music_volume != 0)
      return 30;
    music_config.music_volume = 101;
    if (moderngekko::frontend::SaveConfig(music, music_config, &error))
      return 31;
    {
      std::ofstream file(music / "config.ini", std::ios::trunc);
      file << "[Launcher]\nmusic_volume=loud\n[Video]\nresolution=1920x1080\n";
    }
    if (moderngekko::frontend::LoadConfig(music, false))
      return 32;
  }

  // Looking for updates: on until turned off, then kept off, also by the
  // short SaveConfig; only true or false.
  {
    const fs::path updates = directory / "updates";
    auto updates_config = moderngekko::frontend::LoadConfig(updates, true);
    if (!updates_config.check_for_updates)
      return 33;
    updates_config.check_for_updates = false;
    if (!moderngekko::frontend::SaveConfig(updates, updates_config, &error) ||
        !moderngekko::frontend::SaveConfig(updates, "1920x1080", true, controller, &error) ||
        moderngekko::frontend::LoadConfig(updates, false).check_for_updates)
      return 34;
    {
      std::ofstream file(updates / "config.ini", std::ios::trunc);
      file << "[Launcher]\ncheck_for_updates=sometimes\n[Video]\nresolution=1920x1080\n";
    }
    if (moderngekko::frontend::LoadConfig(updates, false))
      return 35;
  }

  // 4:3 border: random until chosen, then saved, kept by the short
  // SaveConfig, read in any case; any id is kept, but not two words.
  {
    const fs::path border = directory / "border";
    auto border_config = moderngekko::frontend::LoadConfig(border, true);
    if (border_config.game_border != "random")
      return 38;
    border_config.game_border = "green-grid";
    if (!moderngekko::frontend::SaveConfig(border, border_config, &error) ||
        !moderngekko::frontend::SaveConfig(border, "1920x1080", true, controller, &error) ||
        moderngekko::frontend::LoadConfig(border, false).game_border != "green-grid")
      return 39;
    border_config.game_border = "two words";
    if (moderngekko::frontend::SaveConfig(border, border_config, &error))
      return 40;
    {
      std::ofstream file(border / "config.ini", std::ios::trunc);
      file << "[Video]\nresolution=1920x1080\nborder = None\n";
    }
    if (moderngekko::frontend::LoadConfig(border, false).game_border != "none")
      return 41;
    {
      std::ofstream file(border / "config.ini", std::ios::trunc);
      file << "[Video]\nresolution=1920x1080\nborder=../sonic\n";
    }
    if (moderngekko::frontend::LoadConfig(border, false))
      return 42;
  }

  // The picture effect and the game's volume: none and 100 by default, kept,
  // and an impossible value refused. The effects have distinct values, none
  // first, and each one with a shader but none.
  {
    const fs::path effect = directory / "effect";
    auto effect_config = netplay_config;
    const auto defaults = moderngekko::frontend::LoadConfig(effect, true);
    if (!defaults || defaults.effect != "none" || defaults.game_volume != 100)
      return 45;
    effect_config.effect = "bloom-chromatic";
    effect_config.game_volume = 35;
    if (!moderngekko::frontend::SaveConfig(effect, effect_config, &error))
      return 46;
    const auto loaded = moderngekko::frontend::LoadConfig(effect, false);
    if (loaded.effect != "bloom-chromatic" || loaded.game_volume != 35)
      return 47;
    // The short SaveConfig the Settings page uses keeps them.
    if (!moderngekko::frontend::SaveConfig(effect, "1920x1080", true, controller, &error) ||
        moderngekko::frontend::LoadConfig(effect, false).effect != "bloom-chromatic" ||
        moderngekko::frontend::LoadConfig(effect, false).game_volume != 35)
      return 48;
    effect_config.game_volume = 101;
    if (moderngekko::frontend::SaveConfig(effect, effect_config, &error))
      return 49;
    effect_config.game_volume = 50;
    effect_config.effect = "two words";
    if (moderngekko::frontend::SaveConfig(effect, effect_config, &error))
      return 50;
    for (const char *bad : {"game_volume=-1", "game_volume=loud", "effect=a b"}) {
      std::ofstream(effect / "config.ini", std::ios::trunc)
          << "[Video]\nresolution=1920x1080\n" << bad << '\n';
      if (moderngekko::frontend::LoadConfig(effect, false))
        return 51;
    }
    const auto &effects = moderngekko::frontend::SupportedEffects();
    if (effects.empty() || std::string(effects.front().value) != "none" ||
        *effects.front().shader != '\0')
      return 52;
    for (std::size_t i = 0; i < effects.size(); ++i) {
      if (moderngekko::frontend::FindEffect(effects[i].value) != &effects[i] ||
          (i > 0 && *effects[i].shader == '\0'))
        return 53;
    }
    if (moderngekko::frontend::FindEffect("sepia"))
      return 54;
  }

  fs::remove_all(directory);
  return 0;
}
