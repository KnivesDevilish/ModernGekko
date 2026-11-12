// The intros of the game selection screen (tools/launcher_game_select.hpp):
// parts of an MPEG-1 video in the player's own game files, decoded in the
// background by tools/third_party/pl_mpeg.h. Nothing of the games comes with
// the launcher; a game that isn't extracted has no intro.
//
// A video is a file of the extracted game (a Sofdec .SFD is an MPEG-1 program
// stream), or a file inside a CRI .cvm, which is an ISO 9660 image after a
// header: "files/Image3.cvm:OPENING.SFD".
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <istream>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace moderngekko::frontend
{
// Where a video's bytes are.
struct VideoSource
{
  std::filesystem::path file;
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
};

// A file inside an ISO 9660 image that starts anywhere in the first 256 KiB of
// `image` (a .cvm has its own header first): its offset and length. Names
// are compared without case and without ";1"; folders are searched too.
std::optional<std::pair<std::uint64_t, std::uint64_t>> FindInIso(std::istream& image,
                                                                 std::string_view name);

// layout.txt's video, in the game at `root`; nothing when it isn't there.
std::optional<VideoSource> FindVideoSource(const std::filesystem::path& root,
                                           std::string_view video);

// Decodes the parts of a video, in order, a few frames ahead of where they
// are shown, on a thread of its own.
class PreviewVideo
{
public:
  PreviewVideo() = default;
  PreviewVideo(const PreviewVideo&) = delete;
  PreviewVideo& operator=(const PreviewVideo&) = delete;
  ~PreviewVideo() { Stop(); }

  // Starts over with these parts (first and last frame, counted from 0).
  void Start(const VideoSource& source, std::vector<std::pair<int, int>> parts);
  void Stop();
  bool Running() const { return m_thread.joinable(); }

  // Once its first frame is decoded: its size and frame rate.
  bool Ready() const { return m_ready.load(); }
  // The video can't be read or decoded.
  bool Failed() const { return m_failed.load(); }
  int Width() const { return m_width.load(); }
  int Height() const { return m_height.load(); }
  double FrameRate() const { return m_frame_rate.load(); }

  // The newest decoded frame up to `index` (counted along the parts), as RGBA
  // pixels, into `rgba` (whose old buffer is reused). False when there is no
  // new one since the last call.
  bool Take(int index, std::vector<std::uint8_t>& rgba);

private:
  struct Frame
  {
    int index = 0;
    std::vector<std::uint8_t> rgba;
  };

  void Run(VideoSource source, std::vector<std::pair<int, int>> parts);
  // Waits for room for one more frame; false when stopping.
  bool WaitForRoom();
  void Push(Frame frame);

  static constexpr std::size_t AHEAD = 12;  // frames decoded before they are due

  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_room;
  std::deque<Frame> m_frames;
  std::vector<std::vector<std::uint8_t>> m_spare;  // buffers to reuse
  std::atomic<bool> m_stop{false};
  std::atomic<bool> m_ready{false};
  std::atomic<bool> m_failed{false};
  std::atomic<int> m_width{0};
  std::atomic<int> m_height{0};
  std::atomic<double> m_frame_rate{0.0};
};
}  // namespace moderngekko::frontend
