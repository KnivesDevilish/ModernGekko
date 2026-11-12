#pragma once

// Background music for the launcher and the Netplay lobby: an MP3 file that
// loops, decoded a little at a time (dr_mp3) as the sound goes out, so a few
// minutes of music never sits whole in memory. The SDL that Dolphin builds
// has no audio, so the sound goes out through cubeb, Dolphin's own audio
// library, where the build has it (Windows, macOS), and on Linux, where
// ModernGekko builds Dolphin without cubeb, straight to the sound server
// (PulseAudio, or PipeWire in its place). Short sounds (the game selection
// screen's) play over the music, mixed into it as it goes out.

#include "launcher_sound.hpp"

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
  // The music's volume also sets the sounds': at 0 neither is heard.
  void SetVolume(int volume);
  // Plays a short sound over the music, once, from now. Nothing while the
  // music isn't playing (no file, no audio device, paused for the game).
  void PlayEffect(const SoundEffect& sound);
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
