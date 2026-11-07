#pragma once

// The game's volume when Dolphin plays it through its PulseAudio backend (the
// one used on Linux, where this build has no cubeb). That backend plays at
// full volume whatever Dolphin's volume setting says; only cubeb, WASAPI and
// OpenAL apply it. PulseAudio, and PipeWire's PulseAudio layer, keep a volume
// for each stream, as the system's mixer shows: this sets the one of the
// game's stream, Dolphin's "Playback" in this process, on the same scale.

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace moderngekko::detail
{
class PulseStreamVolume
{
public:
  // From the start, the game's stream gets volume (0 to 100) as soon as it
  // exists, and again whenever Set asks for another.
  explicit PulseStreamVolume(int volume);
  ~PulseStreamVolume();
  PulseStreamVolume(const PulseStreamVolume&) = delete;
  PulseStreamVolume& operator=(const PulseStreamVolume&) = delete;

  // From any thread; the stream gets it shortly after.
  void Set(int volume);

private:
  void Run();

  std::mutex m_mutex;
  std::condition_variable m_wake;
  int m_volume = 100;
  // Counts the volumes asked for, so a new one is applied even if the last
  // one never found the stream.
  unsigned m_requests = 1;
  bool m_stop = false;
  std::thread m_thread;
};
}  // namespace moderngekko::detail
