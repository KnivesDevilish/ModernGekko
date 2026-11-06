#include "launcher_music.hpp"

#include <SDL3/SDL.h>

#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_mp3.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace moderngekko::frontend
{
struct MusicPlayer::Decoder
{
  drmp3 mp3{};
  std::vector<drmp3_int16> buffer;
};

namespace
{
// SDL's audio thread asks for more sound: decode it, and start over at the
// end of the file so the music loops.
void SDLCALL FeedMusic(void* userdata, SDL_AudioStream* stream, int additional_amount, int)
{
  auto& decoder = *static_cast<MusicPlayer::Decoder*>(userdata);
  const int frame_bytes = static_cast<int>(decoder.mp3.channels * sizeof(drmp3_int16));
  int frames_wanted = std::max(additional_amount, 0) / frame_bytes;
  bool restarted = false;
  while (frames_wanted > 0)
  {
    const drmp3_uint64 chunk =
        std::min<drmp3_uint64>(frames_wanted, decoder.buffer.size() / decoder.mp3.channels);
    const drmp3_uint64 read = drmp3_read_pcm_frames_s16(&decoder.mp3, chunk, decoder.buffer.data());
    if (read == 0)
    {
      // The end: back to the start once. Nothing again right after means
      // the file can't be decoded; the rest stays silent.
      if (restarted || !drmp3_seek_to_pcm_frame(&decoder.mp3, 0))
        return;
      restarted = true;
      continue;
    }
    restarted = false;
    SDL_PutAudioStreamData(stream, decoder.buffer.data(),
                           static_cast<int>(read) * frame_bytes);
    frames_wanted -= static_cast<int>(read);
  }
}

// The slider's 0 to 100 as a gain: squared, so the low half of the slider is
// as useful as the top half to the ear.
float Gain(int volume)
{
  const float level = std::clamp(volume, 0, 100) / 100.0f;
  return level * level;
}
}  // namespace

MusicPlayer::MusicPlayer() = default;

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
  auto decoder = std::make_unique<Decoder>();
#ifdef _WIN32
  const bool opened = drmp3_init_file_w(&decoder->mp3, path.c_str(), nullptr);
#else
  const bool opened = drmp3_init_file(&decoder->mp3, path.c_str(), nullptr);
#endif
  if (!opened)
  {
    std::cerr << "music: can't decode " << path.string() << '\n';
    return false;
  }
  if (decoder->mp3.channels == 0 || decoder->mp3.sampleRate == 0)
  {
    drmp3_uninit(&decoder->mp3);
    return false;
  }
  decoder->buffer.resize(4096 * decoder->mp3.channels);

  if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
  {
    std::cerr << "music: no audio: " << SDL_GetError() << '\n';
    drmp3_uninit(&decoder->mp3);
    return false;
  }
  m_audio_started = true;
  const SDL_AudioSpec spec{SDL_AUDIO_S16, static_cast<int>(decoder->mp3.channels),
                           static_cast<int>(decoder->mp3.sampleRate)};
  m_decoder = std::move(decoder);
  m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FeedMusic,
                                       m_decoder.get());
  if (!m_stream)
  {
    std::cerr << "music: can't open the audio device: " << SDL_GetError() << '\n';
    Stop();
    return false;
  }
  SDL_SetAudioStreamGain(m_stream, Gain(volume));
  SDL_ResumeAudioStreamDevice(m_stream);
  std::cerr << "music: playing " << path.filename().string() << '\n';
  return true;
}

void MusicPlayer::SetVolume(int volume)
{
  if (m_stream)
    SDL_SetAudioStreamGain(m_stream, Gain(volume));
}

void MusicPlayer::Pause()
{
  if (m_stream)
    SDL_PauseAudioStreamDevice(m_stream);
}

void MusicPlayer::Resume()
{
  if (m_stream)
    SDL_ResumeAudioStreamDevice(m_stream);
}

void MusicPlayer::Stop()
{
  // The stream first: once it is gone, the audio thread no longer reads the
  // decoder.
  if (m_stream)
  {
    SDL_DestroyAudioStream(m_stream);
    m_stream = nullptr;
  }
  if (m_decoder)
  {
    drmp3_uninit(&m_decoder->mp3);
    m_decoder.reset();
  }
  if (m_audio_started)
  {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_audio_started = false;
  }
}
}  // namespace moderngekko::frontend
