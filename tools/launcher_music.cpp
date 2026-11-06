#include "launcher_music.hpp"

#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_mp3.h"

#if defined(MODERNGEKKO_HAS_CUBEB)
#include "AudioCommon/CubebUtils.h"

#include <cubeb/cubeb.h>
#elif defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
#include <pulse/error.h>
#include <pulse/simple.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#endif

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <utility>

namespace moderngekko::frontend
{
struct MusicPlayer::Decoder
{
  drmp3 mp3{};
};

#if defined(MODERNGEKKO_HAS_CUBEB)
struct MusicPlayer::Output
{
  // On Windows, cubeb's WASAPI backend must be used from a thread where COM
  // is set up; Dolphin's helper runs every cubeb call on such a thread.
  // Elsewhere it runs them on the calling thread.
  std::unique_ptr<CubebUtils::CoInitSyncWorker> worker;
  cubeb* context = nullptr;
  cubeb_stream* stream = nullptr;
};
#elif defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
// Linux, where ModernGekko builds Dolphin without cubeb: the music goes to
// the sound server (PulseAudio, or PipeWire standing in for it) from a
// thread of its own, which decodes a little and hands it over, again and
// again. The server waits when it has enough, which paces the thread.
struct MusicPlayer::Output
{
  std::string name;
  pa_sample_spec format{};
  std::thread thread;
  std::mutex mutex;
  std::condition_variable wake;
  bool paused = false;
  bool stopping = false;
  // Counts Resume() calls: after the sound server failed, the next resume
  // (when the game closes) tries again.
  std::uint64_t resumes = 0;
  std::atomic<float> gain{1.0f};
};
#else
struct MusicPlayer::Output
{
};
#endif

namespace
{
#if defined(MODERNGEKKO_HAS_CUBEB) || defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
// The slider's 0 to 100 as a gain: squared, so the low half of the slider is
// as useful as the top half to the ear.
float Gain(int volume)
{
  const float level = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
  return level * level;
}

// The next frames of music, starting over at the end of the file so it
// loops. A file that can't be decoded any further gives silence.
void ReadMusic(MusicPlayer::Decoder& decoder, drmp3_int16* samples, long frames)
{
  const long channels = static_cast<long>(decoder.mp3.channels);
  long written = 0;
  bool restarted = false;
  while (written < frames)
  {
    const drmp3_uint64 read = drmp3_read_pcm_frames_s16(
        &decoder.mp3, static_cast<drmp3_uint64>(frames - written), samples + written * channels);
    if (read == 0)
    {
      // The end: back to the start once. Nothing again right after means
      // the file can't be decoded; the rest is silence.
      if (restarted || !drmp3_seek_to_pcm_frame(&decoder.mp3, 0))
      {
        std::fill(samples + written * channels, samples + frames * channels, drmp3_int16{0});
        return;
      }
      restarted = true;
      continue;
    }
    restarted = false;
    written += static_cast<long>(read);
  }
}
#endif

#if defined(MODERNGEKKO_HAS_CUBEB)
// The audio device asks for more sound. Runs on cubeb's audio thread.
long FeedMusic(cubeb_stream*, void* user, const void*, void* output, long frames)
{
  ReadMusic(*static_cast<MusicPlayer::Decoder*>(user), static_cast<drmp3_int16*>(output),
            frames);
  return frames;
}

void MusicStateChanged(cubeb_stream*, void*, cubeb_state state)
{
  if (state == CUBEB_STATE_ERROR)
    std::cerr << "music: the audio device stopped with an error\n";
}
#elif defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
// Frames decoded and handed over at a time: about a fortieth of a second.
constexpr long BLOCK_FRAMES = 1024;

pa_simple* ConnectMusic(const MusicPlayer::Output& output)
{
  // A tenth of a second waiting at the server: a volume change is heard
  // that soon, and it is plenty to ride out a busy moment.
  pa_buffer_attr buffering;
  buffering.maxlength = static_cast<std::uint32_t>(-1);
  buffering.tlength = static_cast<std::uint32_t>(pa_usec_to_bytes(100000, &output.format));
  buffering.prebuf = static_cast<std::uint32_t>(-1);
  buffering.minreq = static_cast<std::uint32_t>(-1);
  buffering.fragsize = static_cast<std::uint32_t>(-1);
  int error = 0;
  // The name is how the system's volume mixer lists the music.
  pa_simple* connection =
      pa_simple_new(nullptr, output.name.c_str(), PA_STREAM_PLAYBACK, nullptr,
                    output.name.c_str(), &output.format, nullptr, &buffering, &error);
  if (!connection)
    std::cerr << "music: no audio device (" << pa_strerror(error) << ")\n";
  return connection;
}

// Drops what is still waiting to play, and the music's place in the mixer.
void Disconnect(pa_simple*& connection)
{
  if (!connection)
    return;
  int error = 0;
  pa_simple_flush(connection, &error);
  pa_simple_free(connection);
  connection = nullptr;
}

// The music thread. connection is open when it starts.
void PlayMusic(MusicPlayer::Output& output, MusicPlayer::Decoder& decoder, pa_simple* connection)
{
  const std::size_t channels = decoder.mp3.channels;
  std::vector<drmp3_int16> block(static_cast<std::size_t>(BLOCK_FRAMES) * channels);
  // While paused: until the player resumes or stops.
  const auto wait_while_paused = [&] {
    std::unique_lock lock(output.mutex);
    output.wake.wait(lock, [&] { return output.stopping || !output.paused; });
  };
  // After the sound server failed: until the next resume, or a stop.
  const auto wait_for_next_resume = [&] {
    std::unique_lock lock(output.mutex);
    const std::uint64_t seen = output.resumes;
    output.wake.wait(lock, [&] { return output.stopping || output.resumes != seen; });
  };
  for (;;)
  {
    bool paused = false;
    {
      std::lock_guard lock(output.mutex);
      if (output.stopping)
        break;
      paused = output.paused;
    }
    if (paused)
    {
      // While the game runs nothing of the music stays queued, and the
      // music leaves the mixer.
      Disconnect(connection);
      wait_while_paused();
      continue;
    }
    if (!connection)
    {
      connection = ConnectMusic(output);
      if (!connection)
      {
        wait_for_next_resume();
        continue;
      }
    }
    ReadMusic(decoder, block.data(), BLOCK_FRAMES);
    const float gain = output.gain.load();
    if (gain < 1.0f)
    {
      for (drmp3_int16& sample : block)
        sample = static_cast<drmp3_int16>(static_cast<float>(sample) * gain);
    }
    int error = 0;
    if (pa_simple_write(connection, block.data(), block.size() * sizeof(drmp3_int16), &error) <
        0)
    {
      std::cerr << "music: the audio device stopped (" << pa_strerror(error) << ")\n";
      Disconnect(connection);
      wait_for_next_resume();
    }
  }
  Disconnect(connection);
}
#endif
}  // namespace

MusicPlayer::MusicPlayer(std::string name) : m_name(std::move(name))
{
}

MusicPlayer::~MusicPlayer()
{
  Stop();
}

bool MusicPlayer::Play(const std::filesystem::path& path, int volume)
{
  Stop();
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec))
  {
    std::cerr << "music: no " << path.string() << '\n';
    return false;
  }
#if !defined(MODERNGEKKO_HAS_CUBEB) && !defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
  (void)volume;
  std::cerr << "music: this build has no audio output\n";
  return false;
#else
  m_decoder = std::make_unique<Decoder>();
#ifdef _WIN32
  const bool opened = drmp3_init_file_w(&m_decoder->mp3, path.c_str(), nullptr);
#else
  const bool opened = drmp3_init_file(&m_decoder->mp3, path.c_str(), nullptr);
#endif
  if (!opened || m_decoder->mp3.channels == 0 || m_decoder->mp3.channels > 2 ||
      m_decoder->mp3.sampleRate == 0)
  {
    std::cerr << "music: can't decode " << path.string() << '\n';
    if (opened)
      drmp3_uninit(&m_decoder->mp3);
    m_decoder.reset();
    return false;
  }

  m_output = std::make_unique<Output>();
  bool started = false;
#if defined(MODERNGEKKO_HAS_CUBEB)
  m_output->worker = std::make_unique<CubebUtils::CoInitSyncWorker>("Music");
  Output& output = *m_output;
  Decoder& decoder = *m_decoder;
  const std::string name = m_name;
  m_output->worker->Execute([&] {
    if (cubeb_init(&output.context, name.c_str(), nullptr) != CUBEB_OK)
    {
      output.context = nullptr;
      return;
    }
    cubeb_stream_params params{};
    params.format = CUBEB_SAMPLE_S16NE;
    params.rate = decoder.mp3.sampleRate;
    params.channels = decoder.mp3.channels;
    params.layout = decoder.mp3.channels == 1 ? CUBEB_LAYOUT_MONO : CUBEB_LAYOUT_STEREO;
    params.prefs = CUBEB_STREAM_PREF_NONE;
    // A tenth of a second of buffering: plenty for music, light on the CPU.
    std::uint32_t latency = params.rate / 10;
    std::uint32_t minimum = 0;
    if (cubeb_get_min_latency(output.context, &params, &minimum) == CUBEB_OK)
      latency = std::max(latency, minimum);
    if (cubeb_stream_init(output.context, &output.stream, name.c_str(), nullptr, nullptr,
                          nullptr, &params, latency, FeedMusic, MusicStateChanged,
                          &decoder) != CUBEB_OK)
    {
      output.stream = nullptr;
      return;
    }
    cubeb_stream_set_volume(output.stream, Gain(volume));
    started = cubeb_stream_start(output.stream) == CUBEB_OK;
  });
  if (!started)
    std::cerr << "music: no audio device\n";
#else
  m_output->name = m_name;
  m_output->format.format = PA_SAMPLE_S16NE;
  m_output->format.rate = m_decoder->mp3.sampleRate;
  m_output->format.channels = static_cast<std::uint8_t>(m_decoder->mp3.channels);
  m_output->gain = Gain(volume);
  // The first connection here, so Play can tell at once whether there is a
  // sound server at all.
  if (pa_simple* connection = ConnectMusic(*m_output))
  {
    m_output->thread =
        std::thread(PlayMusic, std::ref(*m_output), std::ref(*m_decoder), connection);
    started = true;
  }
#endif
  if (!started)
  {
    Stop();
    return false;
  }
  std::cerr << "music: playing " << path.filename().string() << '\n';
  return true;
#endif
}

void MusicPlayer::SetVolume(int volume)
{
#if defined(MODERNGEKKO_HAS_CUBEB)
  if (m_output && m_output->stream)
  {
    cubeb_stream* stream = m_output->stream;
    m_output->worker->Execute([stream, volume] { cubeb_stream_set_volume(stream, Gain(volume)); });
  }
#elif defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
  if (m_output)
    m_output->gain = Gain(volume);
#else
  (void)volume;
#endif
}

void MusicPlayer::Pause()
{
#if defined(MODERNGEKKO_HAS_CUBEB)
  if (m_output && m_output->stream)
  {
    cubeb_stream* stream = m_output->stream;
    m_output->worker->Execute([stream] { cubeb_stream_stop(stream); });
  }
#elif defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
  if (m_output)
  {
    std::lock_guard lock(m_output->mutex);
    m_output->paused = true;
    m_output->wake.notify_all();
  }
#endif
}

void MusicPlayer::Resume()
{
#if defined(MODERNGEKKO_HAS_CUBEB)
  if (m_output && m_output->stream)
  {
    cubeb_stream* stream = m_output->stream;
    m_output->worker->Execute([stream] { cubeb_stream_start(stream); });
  }
#elif defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
  if (m_output)
  {
    std::lock_guard lock(m_output->mutex);
    m_output->paused = false;
    ++m_output->resumes;
    m_output->wake.notify_all();
  }
#endif
}

void MusicPlayer::Stop()
{
  // The output first: once it is gone, nothing reads the decoder any more.
#if defined(MODERNGEKKO_HAS_CUBEB)
  if (m_output)
  {
    Output& output = *m_output;
    output.worker->Execute([&output] {
      if (output.stream)
      {
        cubeb_stream_stop(output.stream);
        cubeb_stream_destroy(output.stream);
        output.stream = nullptr;
      }
      if (output.context)
      {
        cubeb_destroy(output.context);
        output.context = nullptr;
      }
    });
    m_output.reset();
  }
#elif defined(MODERNGEKKO_HAS_PULSE_SIMPLE)
  if (m_output)
  {
    {
      std::lock_guard lock(m_output->mutex);
      m_output->stopping = true;
      m_output->wake.notify_all();
    }
    if (m_output->thread.joinable())
      m_output->thread.join();
    m_output.reset();
  }
#endif
  if (m_decoder)
  {
    drmp3_uninit(&m_decoder->mp3);
    m_decoder.reset();
  }
}
}  // namespace moderngekko::frontend
