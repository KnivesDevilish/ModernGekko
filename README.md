# ModernGekko

A runtime for GameCube/Wii recomps.

## About this fork

This is a modified fork of [ExpansionPak/ModernGekko](https://github.com/ExpansionPak/ModernGekko) by Hyperway, maintained by KnivesDevilish for [SR-ZGS](https://github.com/KnivesDevilish/SR-ZGS), the static recompilation of *Sonic Riders: Zero Gravity* (Wii). SR-ZGS uses it as its `external/ModernGekko` submodule.

The changes live in the `knives/srzgs-launcher` branch, which starts from the original `master` (commit `a2e4e2b`, September 2026). They add:

- A redesigned launcher: PLAY, Settings, Controls, Game Files and Credits pages, a sidebar with music volume and language, a PATCH NOTES panel and a summary of the time played, the controller and the display settings.
- Launcher languages: English, Spanish and Japanese (with an embedded subset of Noto Sans CJK JP).
- A CPU core selector (JIT64 by default, StaticRecomp experimental) and an aspect ratio setting.
- GameCube controller profiles with keyboard keys and three extra buttons, a controller tester, a keyboard-only profile when there is no gamepad, and following a gamepad whose name changes.
- A Netplay lobby with invite codes or direct IP, GameCube ports for every seat, and the host's CPU core for every player.
- Music for the launcher and the lobby (cubeb, or PulseAudio on Linux), background pictures and an optional check for new releases on GitHub.
- 4:3 borders: pictures in the bars beside a 4:3 game on a wide screen, chosen in the launcher's Settings (one of them, none, or a random one each time the game opens) and drawn by the runtime on Dolphin's on-screen UI layer. A grid in them can slide, as in the launcher's background.
- An in-game menu, opened with M or a pad's Guide button (the first controller's profile): Continue, Quick settings, See controls, Take screenshot, Back to the launcher and Quit game, the last two after a question. In solo play it pauses the game; in Netplay the match goes on behind it. Quit game closes the launcher too. The Quick settings (picture effect, internal resolution, the game's volume and, for a 4:3 game, the border) apply at once, even paused, and are written to `config.ini`. See controls lists what presses each button of the emulated controller, read from the player's profile.
- A complete save the Game Files page can install (`MODERNGEKKO_COMPLETE_SAVE`, `tools/launcher_save.hpp`): a Wii game's save files without the save's `banner.bin`, copied over the save the game made on its first start after a copy of it is kept in `SaveBackups/<disc ID>/`, which Restore puts back.
- A window size setting: the game window opens at the size chosen in the launcher, or, automatically, at 16:9 and 85% of the primary monitor (XRandR), in its middle (`src/runtime/window_size.hpp`, read by the fork's copies of Dolphin's windows in `src/platform/`). Dolphin's own window opens at its saved size, 1556x934 unless changed in Dolphin.ini.
- Screenshots (F9, or Take screenshot) at the launcher's screenshot resolution (3840x2160 by default), whatever the internal resolution: the game is drawn at it for the few frames Dolphin's screenshot needs, in the configuration layer Dolphin never saves, then goes back (`src/runtime/screenshot.cpp`). A paused game runs those frames muted and without input, and pauses again. A line at the bottom of the screen says when the PNG is written, and its size.
- Picture effects, chosen in the launcher's Settings or the Quick settings: bloom, chromatic aberration, vivid colours, sharpen, outline and CRT, the fork's own one-pass post-processing shaders in `shaders/` (copied to `Sys/Shaders/ModernGekko`) made to run on integrated graphics, and Dolphin's FXAA. The launcher's Settings also have the game's volume; on Linux, where Dolphin plays through its PulseAudio backend, which ignores Dolphin's volume, the runtime sets the volume of the game's stream on the sound server instead (`src/runtime/pulse_volume.cpp`, with libpulse).
- Small runtime changes: the runtime's on-screen messages are hidden, the game files are not hashed twice when the runner boots a game, and the launcher starts faster.

New CMake options. All of them are optional: when one is left empty, the launcher works without that picture, music, panel or version check.

| Option | What it sets |
| --- | --- |
| `MODERNGEKKO_LAUNCHER_BANNER` | PNG at the top of the PLAY page |
| `MODERNGEKKO_LAUNCHER_BACKGROUND` | Picture behind the launcher's pages |
| `MODERNGEKKO_LAUNCHER_CONTROLLER_ART`, `MODERNGEKKO_LAUNCHER_KEYBOARD_ART` | Drawings for the Controls page tester |
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
