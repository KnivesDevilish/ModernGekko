#pragma once

// The game the launcher starts ends with the launcher, even when the launcher
// is closed from the Task Manager or crashes: a game left loading (the
// Netplay lobby's first check of the game files can take minutes) would
// otherwise go on alone in the background, unseen.
//
// On Windows the game goes in a job object that Windows closes, ending the
// game, when the launcher's last handle to it goes. On Linux the runner asks
// for the same itself (PR_SET_PDEATHSIG in moderngekko_run.cpp); elsewhere
// nothing happens.

#include <cstdint>

namespace moderngekko::frontend
{
// The process with this ID ends when the launcher does. Called once per game,
// right after starting it.
void EndWithLauncher(std::int64_t process_id);
}  // namespace moderngekko::frontend
