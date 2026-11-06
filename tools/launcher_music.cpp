#include "launcher_music.hpp"

#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_mp3.h"

#ifdef MODERNGEKKO_HAS_CUBEB
#include "AudioCommon/CubebUtils.h"

#include <cubeb/cubeb.h>
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

#ifdef MODERNGEKKO_HAS_CUBEB
struct MusicPlayer::Output
{
  // On Windows, cubeb's WASAPI backend must be used from a thread where COM
  // is set up; Dolphin's helper runs every cubeb call on such a thread.
  // Elsewhere it runs them on the calling thread.
  std::unique_ptr<CubebUtils::CoInitSyncWorker> worker;
  cubeb* context = nullptr;
  cubeb_stream* stream = nullptr;
};
#else
struct MusicPlayer::Output
{
};
#endif

namespace
{
#ifdef MODERNGEKKO_HAS_CUBEB
// The slider's 0 to 100 as a gain: squared, so the low half of the slider is
// as useful as the top half to the ear.
float Gain(int volume)
{
  const float level = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
  return level * level;
}

// The audio device asks for more sound: decode it, and start over at the end
// of the file so the music loops. Runs on cubeb's audio thread.
long FeedMusic(cubeb_stream*, void* user, const void*, void* output, long frames)
{
  auto& decoder = *static_cast<MusicPlayer::Decoder*>(user);
  auto* samples = static_cast<drmp3_int16*>(output);
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
        return frames;
      }
      restarted = true;
      continue;
    }
    restarted = false;
    written += static_cast<long>(read);
  }
  return frames;
}

void MusicStateChanged(cubeb_stream*, void*, cubeb_state state)
{
  if (state == CUBEB_STATE_ERROR)
    std::cerr << "music: the audio device stopped with an error\n";
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
    return false;
#ifndef MODERNGEKKO_HAS_CUBEB
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
  m_output->worker = std::make_unique<CubebUtils::CoInitSyncWorker>("Music");
  bool started = false;
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
  {
    std::cerr << "music: no audio device\n";
    Stop();
    return false;
  }
  std::cerr << "music: playing " << path.filename().string() << '\n';
  return true;
#endif
}

void MusicPlayer::SetVolume(int volume)
{
#ifdef MODERNGEKKO_HAS_CUBEB
  if (m_output && m_output->stream)
  {
    cubeb_stream* stream = m_output->stream;
    m_output->worker->Execute([stream, volume] { cubeb_stream_set_volume(stream, Gain(volume)); });
  }
#else
  (void)volume;
#endif
}

void MusicPlayer::Pause()
{
#ifdef MODERNGEKKO_HAS_CUBEB
  if (m_output && m_output->stream)
  {
    cubeb_stream* stream = m_output->stream;
    m_output->worker->Execute([stream] { cubeb_stream_stop(stream); });
  }
#endif
}

void MusicPlayer::Resume()
{
#ifdef MODERNGEKKO_HAS_CUBEB
  if (m_output && m_output->stream)
  {
    cubeb_stream* stream = m_output->stream;
    m_output->worker->Execute([stream] { cubeb_stream_start(stream); });
  }
#endif
}

void MusicPlayer::Stop()
{
#ifdef MODERNGEKKO_HAS_CUBEB
  // The stream first: once it is gone, the audio thread no longer reads the
  // decoder.
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
#endif
  if (m_decoder)
  {
    drmp3_uninit(&m_decoder->mp3);
    m_decoder.reset();
  }
}
}  // namespace moderngekko::frontend
