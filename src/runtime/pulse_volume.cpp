#include "pulse_volume.hpp"

#include <pulse/pulseaudio.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace moderngekko::detail
{
namespace
{
using Clock = std::chrono::steady_clock;

// While the game's stream hasn't shown up (it opens as the game boots), it is
// looked for this often, and for this long after each volume asked for.
constexpr std::chrono::milliseconds RETRY{250};
constexpr std::chrono::seconds GIVE_UP{20};

// Dolphin's PulseAudio backend names its stream so (pa_stream_new).
constexpr std::string_view DOLPHIN_STREAM = "Playback";

void Signal(pa_threaded_mainloop* loop)
{
  pa_threaded_mainloop_signal(loop, 0);
}

// A connection to the sound server, made when first needed and again after
// the server goes away.
class Connection
{
public:
  ~Connection() { Disconnect(); }

  // Sets the volume (0 to 100) of this process's game stream; false when
  // there is no such stream yet or no server.
  bool Apply(int volume)
  {
    if (!Connect())
      return false;
    pa_threaded_mainloop_lock(m_loop);
    Found found{std::to_string(getpid()), m_loop, {}};
    pa_operation* listing =
        pa_context_get_sink_input_info_list(m_context, &Connection::OnSinkInput, &found);
    if (!listing)
    {
      pa_threaded_mainloop_unlock(m_loop);
      Disconnect();
      return false;
    }
    Wait(listing);
    // The same scale as the system's mixer: 100 is PulseAudio's normal volume.
    const auto level = static_cast<pa_volume_t>(
        static_cast<std::uint64_t>(PA_VOLUME_NORM) * static_cast<std::uint64_t>(volume) / 100);
    for (const auto& [index, channels] : found.streams)
    {
      pa_cvolume levels;
      pa_cvolume_set(&levels, channels, level);
      if (pa_operation* setting = pa_context_set_sink_input_volume(
              m_context, index, &levels, &Connection::OnDone, m_loop))
        Wait(setting);
    }
    pa_threaded_mainloop_unlock(m_loop);
    return !found.streams.empty();
  }

private:
  struct Found
  {
    std::string process_id;
    pa_threaded_mainloop* loop;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> streams;
  };

  static void OnState(pa_context*, void* loop)
  {
    Signal(static_cast<pa_threaded_mainloop*>(loop));
  }

  static void OnDone(pa_context*, int, void* loop)
  {
    Signal(static_cast<pa_threaded_mainloop*>(loop));
  }

  static void OnSinkInput(pa_context*, const pa_sink_input_info* info, int end, void* data)
  {
    auto* found = static_cast<Found*>(data);
    if (end != 0 || !info)
    {
      Signal(found->loop);
      return;
    }
    const char* process_id = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_ID);
    if (process_id && found->process_id == process_id && info->name &&
        info->name == DOLPHIN_STREAM && info->channel_map.channels > 0)
      found->streams.emplace_back(info->index, info->channel_map.channels);
  }

  // With the main loop locked: until the operation ends (its callback
  // signals), then lets it go.
  void Wait(pa_operation* operation)
  {
    while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING)
      pa_threaded_mainloop_wait(m_loop);
    pa_operation_unref(operation);
  }

  bool Connect()
  {
    if (m_loop)
    {
      pa_threaded_mainloop_lock(m_loop);
      const bool ready = pa_context_get_state(m_context) == PA_CONTEXT_READY;
      pa_threaded_mainloop_unlock(m_loop);
      if (ready)
        return true;
      Disconnect();
    }
    m_loop = pa_threaded_mainloop_new();
    if (!m_loop)
      return false;
    if (pa_threaded_mainloop_start(m_loop) < 0)
    {
      pa_threaded_mainloop_free(m_loop);
      m_loop = nullptr;
      return false;
    }
    pa_threaded_mainloop_lock(m_loop);
    m_context = pa_context_new(pa_threaded_mainloop_get_api(m_loop), "game volume");
    bool ready = false;
    if (m_context)
    {
      pa_context_set_state_callback(m_context, &Connection::OnState, m_loop);
      if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) >= 0)
      {
        for (;;)
        {
          const pa_context_state_t state = pa_context_get_state(m_context);
          if (state == PA_CONTEXT_READY)
          {
            ready = true;
            break;
          }
          if (!PA_CONTEXT_IS_GOOD(state))
            break;
          pa_threaded_mainloop_wait(m_loop);
        }
      }
    }
    pa_threaded_mainloop_unlock(m_loop);
    if (!ready)
      Disconnect();
    return ready;
  }

  void Disconnect()
  {
    if (!m_loop)
      return;
    pa_threaded_mainloop_lock(m_loop);
    if (m_context)
    {
      pa_context_set_state_callback(m_context, nullptr, nullptr);
      pa_context_disconnect(m_context);
      pa_context_unref(m_context);
      m_context = nullptr;
    }
    pa_threaded_mainloop_unlock(m_loop);
    pa_threaded_mainloop_stop(m_loop);
    pa_threaded_mainloop_free(m_loop);
    m_loop = nullptr;
  }

  pa_threaded_mainloop* m_loop = nullptr;
  pa_context* m_context = nullptr;
};
}  // namespace

PulseStreamVolume::PulseStreamVolume(int volume) : m_volume(volume)
{
  m_thread = std::thread([this] { Run(); });
}

PulseStreamVolume::~PulseStreamVolume()
{
  {
    std::lock_guard lock(m_mutex);
    m_stop = true;
  }
  m_wake.notify_all();
  m_thread.join();
}

void PulseStreamVolume::Set(int volume)
{
  {
    std::lock_guard lock(m_mutex);
    m_volume = volume;
    ++m_requests;
  }
  m_wake.notify_all();
}

void PulseStreamVolume::Run()
{
  Connection pulse;
  unsigned applied = 0;
  unsigned seen = 0;
  Clock::time_point give_up;
  for (;;)
  {
    int volume = 100;
    unsigned request = 0;
    {
      std::unique_lock lock(m_mutex);
      // Asleep until another volume is asked for, or, while the stream
      // hasn't been found, until it is time to look again.
      if (applied == m_requests)
        m_wake.wait(lock, [&] { return m_stop || applied != m_requests; });
      else if (seen == m_requests)
        m_wake.wait_for(lock, RETRY, [&] { return m_stop || seen != m_requests; });
      if (m_stop)
        return;
      volume = m_volume;
      request = m_requests;
    }
    if (request != seen)
    {
      seen = request;
      give_up = Clock::now() + GIVE_UP;
    }
    if (pulse.Apply(volume))
    {
      applied = request;
      std::fprintf(stderr, "game volume: %d%% on the PulseAudio stream\n", volume);
    }
    else if (Clock::now() >= give_up)
    {
      applied = request;
      std::fprintf(stderr, "game volume: no PulseAudio stream of the game found\n");
    }
  }
}
}  // namespace moderngekko::detail
