#pragma once

// Background music for the launcher and the Netplay lobby: an MP3 file that
// loops, decoded a little at a time (dr_mp3) as the sound goes out, so a few
// minutes of music never sits whole in memory. The SDL that Dolphin builds
// has no audio, so the sound goes out through cubeb, Dolphin's own audio
// library, where the build has it (Windows, macOS), and on Linux, where
// ModernGekko builds Dolphin without cubeb, straight to the sound server
// (PulseAudio, or PipeWire in its place).

#include <filesystem>
#include <memory>
#include <string>

namespace moderngekko::frontend
{
class MusicPlayer
{
public:
  // name is how the system's volume mixer lists the music.
  explicit MusicPlayer(std::string name);
  ~MusicPlayer();
  MusicPlayer(const MusicPlayer&) = delete;
  MusicPlayer& operator=(const MusicPlayer&) = delete;

  // Starts path looping at volume, 0 (silent) to 100, as config.ini's
  // music_volume. Returns false, and stays silent, when the file is missing
  // or unreadable or no audio device opens: music is optional.
  bool Play(const std::filesystem::path& path, int volume);
  void SetVolume(int volume);
  // The music stops where it is and picks up from there. (The launcher
  // starts its music over after the game instead: Stop, then Play.)
  void Pause();
  void Resume();
  void Stop();

  struct Decoder;
  struct Output;

private:
  std::string m_name;
  std::unique_ptr<Decoder> m_decoder;
  std::unique_ptr<Output> m_output;
};
}  // namespace moderngekko::frontend
