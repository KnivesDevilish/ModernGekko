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

SR-ZGS passes all of them from `tools/build_tools.sh`.

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
