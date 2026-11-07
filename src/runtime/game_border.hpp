#pragma once

// The pictures beside the game in the black bars, when the game doesn't fill
// the window (a 4:3 game on a wide screen): RuntimeConfig's border_folder and
// border_id, the border in tools/game_borders.hpp's format. They are drawn on
// Dolphin's on-screen UI layer, before each frame is shown, so they cost
// nothing to the game and need no change to the video backends.

#include <filesystem>
#include <memory>
#include <mutex>
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

  // On the video thread, while its backend still exists: frees the pictures'
  // textures, for a border taken off the screen while the game runs. (The
  // destructor can't: when the game ends, the backend goes first.)
  void FreeTextures();

private:
  struct Impl;
  explicit GameBorderOverlay(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};

// The border on the screen, which the in-game menu's Quick settings can switch
// for another while the game runs: the menu's thread hands the new one over,
// and the video thread puts it up at its next frame and frees the old one.
class GameBorderSlot
{
public:
  explicit GameBorderSlot(std::unique_ptr<GameBorderOverlay> border);

  // From any thread: the border to show from the next frame on; none when
  // null.
  void Replace(std::unique_ptr<GameBorderOverlay> border);

  // On the video thread, as GameBorderOverlay::AddToFrame.
  void AddToFrame();

private:
  std::mutex m_mutex;
  // Only the video thread touches m_shown, in AddToFrame.
  std::unique_ptr<GameBorderOverlay> m_shown;
  std::unique_ptr<GameBorderOverlay> m_next;
  bool m_replace = false;
};
}  // namespace moderngekko::detail
