#pragma once

// What the game process (moderngekko-run) tells the launcher that started it
// with its exit code, besides 0 (the game closed: the launcher shows itself
// again) and the Netplay failures (NetplayExitCode, netplay_session.hpp).

namespace moderngekko::frontend
{
// The player chose Quit game in the in-game menu: the launcher closes too.
inline constexpr int RUNNER_EXIT_QUIT = 30;
// The graphics backend couldn't start (no driver for it, as Vulkan on many
// older GPUs on Windows): the launcher suggests choosing another.
inline constexpr int RUNNER_EXIT_GRAPHICS = 31;
}  // namespace moderngekko::frontend
