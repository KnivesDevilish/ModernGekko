#pragma once

// What the in-game menu shows and how its buttons move through it, apart from
// Dolphin, so tests/game_menu_state_test.cpp can check it. The menu itself
// (reading the controller, pausing the game) is game_menu.cpp, and its look
// game_menu_draw.hpp.

#include <chrono>
#include <cstddef>
#include <span>

namespace moderngekko::detail
{
// The main page, or the question asked before leaving the game, which loses
// whatever the game hasn't saved.
enum class MenuPage
{
  Main,
  ConfirmLeave,  // Back to the launcher (Back to the lobby in Netplay)
  ConfirmQuit,   // Quit game
};

// The rows of the pages, top to bottom.
enum class MenuItem
{
  Continue,
  Leave,
  Quit,
  No,
  Yes,
};

// The menu's buttons: held on the controller right now, or the ones that count
// as pressed this time (MenuButtonRepeat::Update).
struct MenuButtons
{
  bool open = false;    // the menu button: M on the keyboard, Guide on a pad
  bool accept = false;  // A
  bool back = false;    // B
  bool up = false;      // D-pad or stick
  bool down = false;

  bool Any() const { return open || accept || back || up || down; }
};

// What a press did, for the caller to act on.
enum class MenuEvent
{
  None,
  Opened,
  Closed,  // Continue, B on the main page or the menu button again
  Leave,   // Yes to Back to the launcher
  Quit,    // Yes to Quit game
};

class MenuState
{
public:
  // The presses of one poll. Only one counts, the first of: the menu button,
  // B, A, up, down.
  MenuEvent Press(const MenuButtons& pressed)
  {
    if (!m_open)
    {
      if (!pressed.open)
        return MenuEvent::None;
      m_open = true;
      Show(MenuPage::Main, 0);
      return MenuEvent::Opened;
    }
    if (pressed.open)
      return Close();
    if (pressed.back)
    {
      if (m_page == MenuPage::Main)
        return Close();
      ShowMainAtQuestion();
      return MenuEvent::None;
    }
    if (pressed.accept)
    {
      switch (Items()[m_selected])
      {
      case MenuItem::Continue:
        return Close();
      case MenuItem::Leave:
        Show(MenuPage::ConfirmLeave, 0);
        return MenuEvent::None;
      case MenuItem::Quit:
        Show(MenuPage::ConfirmQuit, 0);
        return MenuEvent::None;
      case MenuItem::No:
        ShowMainAtQuestion();
        return MenuEvent::None;
      case MenuItem::Yes:
      {
        const MenuEvent event =
            m_page == MenuPage::ConfirmLeave ? MenuEvent::Leave : MenuEvent::Quit;
        Close();
        return event;
      }
      }
    }
    const std::size_t count = Items().size();
    if (pressed.up)
      Show(m_page, (m_selected + count - 1) % count);
    else if (pressed.down)
      Show(m_page, (m_selected + 1) % count);
    return MenuEvent::None;
  }

  bool IsOpen() const { return m_open; }
  MenuPage Page() const { return m_page; }
  std::size_t Selected() const { return m_selected; }

  std::span<const MenuItem> Items() const
  {
    if (m_page == MenuPage::Main)
      return MAIN_ITEMS;
    return CONFIRM_ITEMS;
  }

  // Changes whenever what the menu shows changes, so a paused game is drawn
  // again only then.
  unsigned Version() const { return m_version; }

private:
  static constexpr MenuItem MAIN_ITEMS[] = {MenuItem::Continue, MenuItem::Leave, MenuItem::Quit};
  // No first, so a stray A doesn't leave the game.
  static constexpr MenuItem CONFIRM_ITEMS[] = {MenuItem::No, MenuItem::Yes};

  MenuEvent Close()
  {
    m_open = false;
    ++m_version;
    return MenuEvent::Closed;
  }

  void Show(MenuPage page, std::size_t selected)
  {
    m_page = page;
    m_selected = selected;
    ++m_version;
  }

  // Back from a question to the main page, on the row that asked it.
  void ShowMainAtQuestion()
  {
    const MenuItem asked = m_page == MenuPage::ConfirmLeave ? MenuItem::Leave : MenuItem::Quit;
    std::size_t row = 0;
    while (MAIN_ITEMS[row] != asked)
      ++row;
    Show(MenuPage::Main, row);
  }

  bool m_open = false;
  MenuPage m_page = MenuPage::Main;
  std::size_t m_selected = 0;
  unsigned m_version = 0;
};

// Turns the buttons held at each poll into presses: a button counts once when
// it goes down, and up and down, held, count again after REPEAT_DELAY and then
// every REPEAT_INTERVAL, to run through the rows.
class MenuButtonRepeat
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds REPEAT_DELAY{400};
  static constexpr std::chrono::milliseconds REPEAT_INTERVAL{120};

  MenuButtons Update(const MenuButtons& held, Clock::time_point now)
  {
    MenuButtons pressed;
    pressed.open = held.open && !m_held.open;
    pressed.accept = held.accept && !m_held.accept;
    pressed.back = held.back && !m_held.back;
    pressed.up = Repeat(held.up, m_held.up, &m_up_next, now);
    pressed.down = Repeat(held.down, m_held.down, &m_down_next, now);
    m_held = held;
    return pressed;
  }

private:
  static bool Repeat(bool held, bool was_held, Clock::time_point* next, Clock::time_point now)
  {
    if (!held)
      return false;
    if (!was_held)
    {
      *next = now + REPEAT_DELAY;
      return true;
    }
    if (now < *next)
      return false;
    *next = now + REPEAT_INTERVAL;
    return true;
  }

  MenuButtons m_held;
  Clock::time_point m_up_next;
  Clock::time_point m_down_next;
};
}  // namespace moderngekko::detail
