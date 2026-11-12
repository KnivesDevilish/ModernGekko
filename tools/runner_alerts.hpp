#pragma once

#include <atomic>

namespace moderngekko::frontend {
// While set, the runner's alert handler (moderngekko_run.cpp) only writes
// Dolphin's alerts to the log, with no message box. A Netplay connection
// attempt sets it: the launcher explains a failure in the player's language,
// and the log keeps Dolphin's reason ("Traversal server timed out connecting
// to the host"...).
inline std::atomic<bool> &QuietAlerts() {
  static std::atomic<bool> quiet{false};
  return quiet;
}
} // namespace moderngekko::frontend
