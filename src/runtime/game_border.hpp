#pragma once

// The pictures beside the game in the black bars, when the game doesn't fill
// the window (a 4:3 game on a wide screen): RuntimeConfig's border_folder and
// border_id, the border in tools/game_borders.hpp's format. They are drawn on
// Dolphin's on-screen UI layer, before each frame is shown, so they cost
// nothing to the game and need no change to the video backends.

#include <filesystem>
#include <memory>
#include <string>

namespace moderngekko::detail
{
class GameBorderOverlay
{
public:
  // Reads the border and its pictures; nullptr, with the reason on stderr,
  // when the folder has no such border or a picture can't be read.
  static std::unique_ptr<GameBorderOverlay> Create(const std::filesystem::path& folder,
                                                   const std::string& id);
  ~GameBorderOverlay();
  GameBorderOverlay(const GameBorderOverlay&) = delete;
  GameBorderOverlay& operator=(const GameBorderOverlay&) = delete;

  // On the video thread, before a frame is presented (VideoEvents'
  // before_present_event): adds the border to that frame's on-screen UI.
  void AddToFrame();

private:
  struct Impl;
  explicit GameBorderOverlay(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};
}  // namespace moderngekko::detail
