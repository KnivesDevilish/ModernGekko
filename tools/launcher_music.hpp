#pragma once

// Background music for the launcher and the Netplay lobby: an MP3 file that
// loops, decoded a little at a time on SDL's audio thread (dr_mp3), so a
// few minutes of music never sits whole in memory.

#include <filesystem>
#include <memory>

struct SDL_AudioStream;

namespace moderngekko::frontend
{
class MusicPlayer
{
public:
  MusicPlayer();
  ~MusicPlayer();
  MusicPlayer(const MusicPlayer&) = delete;
  MusicPlayer& operator=(const MusicPlayer&) = delete;

  // Starts path looping at volume, 0 (silent) to 100, as config.ini's
  // music_volume. Returns false, and stays silent, when the file is missing
  // or unreadable or no audio device opens: music is optional.
  bool Play(const std::filesystem::path& path, int volume);
  void SetVolume(int volume);
  // While the game runs, for the launcher: the music stops where it is and
  // picks up from there.
  void Pause();
  void Resume();
  void Stop();

  struct Decoder;

private:
  std::unique_ptr<Decoder> m_decoder;
  SDL_AudioStream* m_stream = nullptr;
  bool m_audio_started = false;
};
}  // namespace moderngekko::frontend
