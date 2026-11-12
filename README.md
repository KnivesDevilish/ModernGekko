# ModernGekko

A runtime for GameCube/Wii recomps.

## About this fork

This is a modified fork of [ExpansionPak/ModernGekko](https://github.com/ExpansionPak/ModernGekko) by Hyperway, maintained by KnivesDevilish for [SR-ZGS](https://github.com/KnivesDevilish/SR-ZGS), the static recompilation of *Sonic Riders: Zero Gravity* (Wii). SR-ZGS uses it as its `external/ModernGekko` submodule.

The changes live in the `knives/srzgs-launcher` branch, which starts from the original `master` (commit `a2e4e2b`, September 2026). They add:

- A redesigned launcher in a 1280x720 window (its 960x540 layout a third bigger, or as big as fits the screen): PLAY, Settings, Controls, Game Files, Mods & expansions, Screenshots and Credits pages, a sidebar with the language (the music volume is in Settings → Audio), a PATCH NOTES panel and a summary of the time played, the controller, the display settings and the installed mods. Most pages put their panels in two columns (`BeginPageColumns` in `tools/launcher_ui.hpp`) so they fit the window without scrolling.
- Gamepad navigation in the launcher and the Netplay lobby (Dear ImGui's, with any connected gamepad): the D-pad or left stick moves, A chooses, B goes back to the sidebar, LB and RB change the page and the right stick scrolls. The footer shows the buttons as the controller prints them (Xbox, PlayStation or Nintendo, drawn by `tools/button_glyphs.hpp`); after a key press it shows the keyboard's instead (Enter, Esc, and Q and E, which change the page like LB and RB). The footer and the PLAY page's controller tile also draw the device the game reads, a gamepad laid out like its maker's or the keyboard (`DrawDeviceIcon`, from SDL's gamepad type). Choosing the Controls page starts testing the controller, so its buttons only light the tester up until B is held (or Escape pressed); LB and RB pass through the page without testing, and Y or Test buttons starts it there. After the game, a gamepad moves nothing until its buttons are let go.
- A Mods & expansions page: the code mod packages the runner will load (see [Code mods](#code-mods)), the folder they go in, and a panel about planned expansions with an optional picture (`MODERNGEKKO_LAUNCHER_MODS_ART`).
- Launcher languages: English, Spanish and Japanese (with an embedded subset of Noto Sans CJK JP).
- A CPU core selector (JIT64 by default, StaticRecomp experimental) and an aspect ratio setting.
- GameCube controller profiles with keyboard keys and three extra buttons, a controller tester, a keyboard-only profile when there is no gamepad, and following a gamepad whose name changes.
- A Netplay lobby with invite codes or direct IP, GameCube ports for every seat, and the host's CPU core for every player.
- Music for the launcher and the lobby (cubeb, or PulseAudio on Linux), background pictures and an optional check for new releases on GitHub.
- 4:3 borders: pictures in the bars beside a 4:3 game on a wide screen, chosen in the launcher's Settings (one of them, none, or a random one each time the game opens) and drawn by the runtime on Dolphin's on-screen UI layer. A grid in them can slide, as in the launcher's background.
- An in-game menu, opened with M or a pad's Guide button (the first controller's profile): Continue, Quick settings, See controls, Take screenshot, Back to the launcher and Quit game, the last two after a question. In solo play it pauses the game; in Netplay the match goes on behind it. After it closes, the game comes back once the button that closed it (accept, back or the menu button) is let go, so the game doesn't get that press; a stick or D-pad direction still held goes to the game at once. Quit game closes the launcher too. The Quick settings (picture effect, internal resolution, the game's volume and, for a 4:3 game, the border) apply at once, even paused, and are written to `config.ini`. See controls lists what presses each button of the emulated controller, read from the player's profile. The hints at the bottom show the profile's accept and back buttons as the pad prints them (Xbox, PlayStation or Nintendo) or as keys, following whichever of the pad and the keyboard was used last.
- A Screenshots page in the launcher (`tools/launcher_gallery.hpp`): the game's screenshots, newest first, as small pictures read and shrunk on a thread of their own and kept in the user directory's `Cache/ScreenshotThumbnails`; one chosen is shown large in the launcher itself, with the next and previous a key or a click away, and can be deleted.
- A complete save the Game Files page can install (`MODERNGEKKO_COMPLETE_SAVE`, `tools/launcher_save.hpp`): a Wii game's save files without the save's `banner.bin`, copied over the save the game made on its first start after a copy of it is kept in `SaveBackups/<disc ID>/`, which Restore puts back.
- A window size setting: the game window opens at the size chosen in the launcher, or, automatically, at 16:9 and 85% of the primary monitor (XRandR), in its middle (`src/runtime/window_size.hpp`, read by the fork's copies of Dolphin's windows in `src/platform/`). Dolphin's own window opens at its saved size, 1556x934 unless changed in Dolphin.ini.
- Screenshots (F9, or Take screenshot) at the launcher's screenshot resolution (3840x2160 by default), whatever the internal resolution: the game is drawn at it for the few frames Dolphin's screenshot needs, in the configuration layer Dolphin never saves, then goes back (`src/runtime/screenshot.cpp`). They are named `<MODERNGEKKO_SCREENSHOT_NAME>_0001.png` and on (the disc ID without it). A paused game runs those frames muted and without input, and pauses again. A line at the bottom of the screen says when the PNG is written, and its size.
- Picture effects, chosen in the launcher's Settings or the Quick settings: bloom, chromatic aberration, vivid colours, sharpen, outline and CRT, the fork's own one-pass post-processing shaders in `shaders/` (copied to `Sys/Shaders/ModernGekko`) made to run on integrated graphics, and Dolphin's FXAA. The launcher's Settings also have the game's volume; on Linux, where Dolphin plays through its PulseAudio backend, which ignores Dolphin's volume, the runtime sets the volume of the game's stream on the sound server instead (`src/runtime/pulse_volume.cpp`, with libpulse).
- The Windows game window (`src/platform/PlatformWin32.cpp`) works as the Linux ones: F9 takes a screenshot, F10 pauses (waiting while a screenshot is taken), Alt+Enter or F11 switch to fullscreen, Ctrl+Esc closes the game and Escape alone no longer does; it opens at the launcher's window size, in the middle of the primary monitor's work area, and fullscreen when the launcher says so; it knows when it has the focus. It has no sunken edge and a black background (no white line around the picture), and its File and View menu bar only with `MODERNGEKKO_WINDOW_MENU` (ON by default). On Windows the launcher and the runner are windowed programs, with no console window beside them.
- The program's icon: `MODERNGEKKO_APP_ICON` (a square PNG, `Resources/app-icon.png`) on the launcher's, the lobby's and the X11 game window's title bars and taskbar entries, and `MODERNGEKKO_APP_ICON_ICO` built into both Windows executables (Explorer, the taskbar and the game window).
- Graphics: on Windows the launcher also offers Direct3D 11 and Direct3D 12. When the backend can't start ("Failed to initialize video backend!"), the runner exits with `RUNNER_EXIT_GRAPHICS` and the launcher suggests choosing another; the runtime's alerts about it aren't shown as message boxes (every alert is written to the log). An experimental `ultrawide` aspect ratio widens the 3D view to the window's shape (Dolphin's widescreen hack, stretched to fill). An experimental `custom` one takes any shape from 1:1 to 6:1 (`custom_aspect=W:H` in `config.ini`, 21:9 by default; Dolphin's Custom aspect mode, with the widescreen hack for every shape but 16:9).
- Input: the in-game menu also opens with `Extra/Select` (Back), as Steam, the Xbox Game Bar and the Steam Deck often take Guide first. Inside gamescope (the Steam Deck's Game Mode) controllers are read with or without the window's focus. The log lists the input devices the game sees whenever they change, and whether the profile's device is among them.
- A GameCube port whose profile names a gamepad that isn't connected (or the keyboard) reads the first connected gamepad that no other port reads, as the game starts and whenever controllers come and go, and the in-game menu reads it too (`EffectivePadDevices` in `src/runtime/game_menu_controls.hpp`, applied by the runtime's `PadDeviceWatch`). The choice is written to the log, and to `GCPadNew.ini` while the game runs (Dolphin loads it again as the game starts); the profile gets its own devices back when the runtime ends. On a Steam Deck the launcher sees Steam's virtual gamepad and the game the Deck's own controller, under another name. The launcher also moves the profile to the first gamepad when its own isn't connected, whatever the number of gamepads.
- Netplay diagnostics: the log keeps why a connection failed (Dolphin's alerts are written there while the joining player connects, instead of being hidden), whether the invite server answered, how long the connection was tried and the host's addresses (a public one is only mentioned). The host's lobby lists first the home address the computer reaches the internet from, and leaves out more virtual adapters (Waydroid, LXC, Bluetooth). On Windows, Host Netplay can let the runner through Windows Defender Firewall, on every network profile, with one administrator prompt (`tools/windows_firewall.cpp`); on Linux it says that a firewall may need the UDP port opened. The launcher writes the release and system as the log's first line.
- Without a StaticRecomp module (`g<disc ID>_recomp`, beside the runner or in the user directory's `StaticRecompModules`), the runner plays with JIT64 even when StaticRecomp is chosen, instead of interpreting the game's code, and starts the runtime without a module (as `--allow-interpreter` does), which JIT64 doesn't need.
- Small runtime changes: the runtime's on-screen messages are hidden, the game files are not hashed twice when the runner boots a game, and the launcher starts faster.

New CMake options. All of them are optional: when one is left empty, the launcher works without that picture, music, panel or version check.

| Option | What it sets |
| --- | --- |
| `MODERNGEKKO_LAUNCHER_BANNER` | PNG at the top of the PLAY page |
| `MODERNGEKKO_LAUNCHER_BACKGROUND` | Picture behind the launcher's pages |
| `MODERNGEKKO_LAUNCHER_CONTROLLER_ART`, `MODERNGEKKO_LAUNCHER_KEYBOARD_ART` | Drawings for the Controls page tester |
| `MODERNGEKKO_LAUNCHER_MODS_ART` | Picture of the EXPANSIONS panel on the Mods & expansions page |
| `MODERNGEKKO_LAUNCHER_MUSIC`, `MODERNGEKKO_LOBBY_MUSIC` | MP3 looped in the launcher and in the Netplay lobby |
| `MODERNGEKKO_LOBBY_ART`, `MODERNGEKKO_LOBBY_BACKGROUND` | Portrait and background picture of the Netplay lobby |
| `MODERNGEKKO_LAUNCHER_PATCH_NOTES` | UTF-8 text for the PATCH NOTES panel, with optional `[en]`, `[es]` and `[ja]` sections |
| `MODERNGEKKO_FRONTEND_VERSION`, `MODERNGEKKO_UPDATE_REPOSITORY` | Version shown by the launcher and the GitHub repository whose releases it checks |
| `MODERNGEKKO_GAME_BORDERS` | Folder with the 4:3 borders' pictures and their `borders.txt` (format in `tools/game_borders.hpp`) |

`MODERNGEKKO_SAVESTATES` (ON by default) can take savestates away from the player: with it OFF, the game window has no savestate keys (the fork's copies of Dolphin's game windows are in `src/platform/`) and the PLAY page doesn't offer to start from a savestate. The runner's `--load-state` and the automation keep them for testing.

SR-ZGS passes all of them from `tools/build_tools.sh`, and `MODERNGEKKO_SAVESTATES=OFF`.

This fork is distributed under the same license as the original, the GNU GPL v3 (see `LICENSE`). As the license asks of modified versions: this is a modified version of ModernGekko, changed by KnivesDevilish starting on November 3, 2026; the commit history of `knives/srzgs-launcher` lists every change. The rest of this README is the original one.

## Code mods

The runner loads code mods from `Mods` beside the executable and from `<user-dir>/Mods` by default. It accepts package directories named `<id>.mgm` containing `mod.so`, `mod.dll`, or `mod.dylib`, and development libraries named `<id>.mgm.so`, `<id>.mgm.dll`, or `<id>.mgm.dylib`. Use `--mods <directory>` for another location or `--no-mods` to disable loading.

The mod ABI supports dependency ordering, minimum versions, optional dependencies, imports and exports, entry and return hooks, normal and forced function patches, events and callbacks, load callbacks, and exact disc/CPU ABI validation. Netplay fingerprints include every loaded package binary and its filename.

DolRecomp's optional MAP input emits named address constants for code mods. Literal addresses remain supported when a game has no MAP file. See `mod-template` for a minimal package.

## Credits

SpecialK / aharonahdoot - RecompCore (referenced heavily)

The Dolphin Team - Foundation of this repo

Literally God / MrPoloGit - Making the Recomp template and adding MacOS support

Please contact me if your name is missing and you contributed something!

## Hall of Fame
binsento - Super Mario Sunshine & Super Smash Bros. Brawl recomp

MOOMAN - 007 AUF

me (Hyperway) Luigi's Mansion & Kirby Wii

Literally God / MrPoloGit - Super Smash Bros. Melee

Contact me to be added to the Hall of Fame
