#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <utility>

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

namespace runner_alerts_detail {
struct LastQuietAlert {
  std::mutex mutex;
  std::string text;
};
inline LastQuietAlert &Last() {
  static LastQuietAlert last;
  return last;
}
} // namespace runner_alerts_detail

// The alert handler keeps the text of the last alert it kept quiet, so the
// Netplay connection attempt can tell why it failed ("Invalid host": no room
// has that invite code).
inline void RememberQuietAlert(std::string text) {
  auto &last = runner_alerts_detail::Last();
  std::lock_guard lock(last.mutex);
  last.text = std::move(text);
}

// That text, forgotten as it is read; empty when no alert was kept quiet.
inline std::string TakeQuietAlert() {
  auto &last = runner_alerts_detail::Last();
  std::lock_guard lock(last.mutex);
  return std::exchange(last.text, std::string());
}
} // namespace moderngekko::frontend
