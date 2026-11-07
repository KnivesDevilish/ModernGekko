#pragma once

// The menu the player opens over the game with the menu button (M on the
// keyboard, Guide on a pad, as the first controller's profile has them):
// Continue, Back to the launcher and Quit game, the last two after a question.
// In solo play it pauses the game, which is drawn again under the menu while
// it stays open; in Netplay the match goes on behind it. While it is open the
// game gets no input from the controllers (Host_UIBlocksControllerState).
//
// What the menu shows and how it moves is game_menu_state.hpp, and its look
// game_menu_draw.hpp.

#include "moderngekko/runtime.hpp"

#include <functional>
#include <memory>

namespace moderngekko::detail
{
// How the player left the game from the menu.
enum class GameMenuChoice
{
  None,      // they didn't: the game closed some other way
  Launcher,  // Back to the launcher (to the lobby in Netplay)
  Quit,      // Quit game: the launcher closes too
};

struct GameMenuHooks
{
  // Draws the rest of Dolphin's on-screen UI layer (the 4:3 border), on the
  // video thread, when the menu draws a paused game again.
  std::function<void()> draw_others;
  // Ends the game, as closing its window does. From the menu's own thread.
  std::function<void()> stop_game;
};

class GameMenu
{
public:
  // After the game has booted, and destroyed before Dolphin stops: until then
  // a thread reads the controller. nullptr when settings.enabled is false.
  static std::unique_ptr<GameMenu> Create(const GameMenuSettings& settings, GameMenuHooks hooks);
  ~GameMenu();
  GameMenu(const GameMenu&) = delete;
  GameMenu& operator=(const GameMenu&) = delete;

  // On the video thread, before a frame is presented (VideoEvents'
  // before_present_event): adds the open menu to that frame's on-screen UI.
  void AddToFrame();

  GameMenuChoice Choice() const;

private:
  struct Impl;
  explicit GameMenu(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};

// While the menu is open, and until its buttons are let go after it closes,
// the game reads no controller input: what Host_UIBlocksControllerState says.
bool GameMenuBlocksInput();
}  // namespace moderngekko::detail
