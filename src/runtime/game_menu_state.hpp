#pragma once

// What the in-game menu shows and how its buttons move through it, apart from
// Dolphin, so tests/game_menu_state_test.cpp can check it. The menu itself
// (reading the controller, pausing the game) is game_menu.cpp, and its look
// game_menu_draw.hpp.

#include <chrono>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace moderngekko::detail
{
// The main page, the Quick settings, or the question asked before leaving
// the game, which loses whatever the game hasn't saved.
enum class MenuPage
{
  Main,
  QuickSettings,
  ConfirmLeave,  // Back to the launcher (Back to the lobby in Netplay)
  ConfirmQuit,   // Quit game
};

// The rows of the pages, top to bottom. On the Quick settings page every row
// is a Setting.
enum class MenuItem
{
  Continue,
  QuickSettings,
  Leave,
  Quit,
  No,
  Yes,
  Setting,
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
  bool left = false;
  bool right = false;

  bool Any() const { return open || accept || back || up || down || left || right; }
};

// What a press did, for the caller to act on.
enum class MenuEvent
{
  None,
  Opened,
  Closed,          // Continue, B on the main page or the menu button again
  Leave,           // Yes to Back to the launcher
  Quit,            // Yes to Quit game
  SettingChanged,  // a quick setting took another choice: ChangedRow()
};

class MenuState
{
public:
  // choice_counts: how many choices each quick setting has, and chosen the
  // one each starts on. Without any, the main page has no Quick settings.
  explicit MenuState(std::vector<std::size_t> choice_counts = {},
                     std::vector<std::size_t> chosen = {})
      : m_choice_counts(std::move(choice_counts)), m_chosen(std::move(chosen))
  {
    m_chosen.resize(m_choice_counts.size(), 0);
    for (std::size_t row = 0; row < m_chosen.size(); ++row)
    {
      // A setting always has its one choice at least.
      if (m_choice_counts[row] == 0)
        m_choice_counts[row] = 1;
      if (m_chosen[row] >= m_choice_counts[row])
        m_chosen[row] = 0;
    }
    m_setting_items.assign(m_choice_counts.size(), MenuItem::Setting);
  }

  // The presses of one poll. Only one counts, the first of: the menu button,
  // B, A, up, down, left, right.
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
      switch (m_page)
      {
      case MenuPage::Main:
        return Close();
      case MenuPage::QuickSettings:
        ShowMainAt(MenuItem::QuickSettings);
        break;
      case MenuPage::ConfirmLeave:
        ShowMainAt(MenuItem::Leave);
        break;
      case MenuPage::ConfirmQuit:
        ShowMainAt(MenuItem::Quit);
        break;
      }
      return MenuEvent::None;
    }
    if (pressed.accept)
    {
      switch (Items()[m_selected])
      {
      case MenuItem::Continue:
        return Close();
      case MenuItem::QuickSettings:
        Show(MenuPage::QuickSettings, 0);
        return MenuEvent::None;
      case MenuItem::Leave:
        Show(MenuPage::ConfirmLeave, 0);
        return MenuEvent::None;
      case MenuItem::Quit:
        Show(MenuPage::ConfirmQuit, 0);
        return MenuEvent::None;
      case MenuItem::No:
        ShowMainAt(m_page == MenuPage::ConfirmLeave ? MenuItem::Leave : MenuItem::Quit);
        return MenuEvent::None;
      case MenuItem::Yes:
      {
        const MenuEvent event =
            m_page == MenuPage::ConfirmLeave ? MenuEvent::Leave : MenuEvent::Quit;
        Close();
        return event;
      }
      case MenuItem::Setting:
        // A goes round the choices, back to the first after the last.
        return Choose((m_chosen[m_selected] + 1) % m_choice_counts[m_selected]);
      }
    }
    const std::size_t count = Items().size();
    if (pressed.up)
    {
      Show(m_page, (m_selected + count - 1) % count);
      return MenuEvent::None;
    }
    if (pressed.down)
    {
      Show(m_page, (m_selected + 1) % count);
      return MenuEvent::None;
    }
    // Left and right step through a setting's choices and stop at its ends,
    // as a volume would.
    if (m_page == MenuPage::QuickSettings && (pressed.left || pressed.right))
    {
      const std::size_t chosen = m_chosen[m_selected];
      if (pressed.left && chosen > 0)
        return Choose(chosen - 1);
      if (pressed.right && chosen + 1 < m_choice_counts[m_selected])
        return Choose(chosen + 1);
    }
    return MenuEvent::None;
  }

  bool IsOpen() const { return m_open; }
  MenuPage Page() const { return m_page; }
  std::size_t Selected() const { return m_selected; }

  std::span<const MenuItem> Items() const
  {
    switch (m_page)
    {
    case MenuPage::Main:
      if (m_choice_counts.empty())
        return MAIN_ITEMS;
      return MAIN_ITEMS_WITH_SETTINGS;
    case MenuPage::QuickSettings:
      return m_setting_items;
    case MenuPage::ConfirmLeave:
    case MenuPage::ConfirmQuit:
      break;
    }
    return CONFIRM_ITEMS;
  }

  // The quick settings: how many there are, and the choice each is on.
  std::size_t SettingCount() const { return m_choice_counts.size(); }
  std::size_t Chosen(std::size_t row) const { return m_chosen[row]; }
  // The quick setting the last SettingChanged was about.
  std::size_t ChangedRow() const { return m_changed_row; }

  // Changes whenever what the menu shows changes, so a paused game is drawn
  // again only then.
  unsigned Version() const { return m_version; }

private:
  static constexpr MenuItem MAIN_ITEMS[] = {MenuItem::Continue, MenuItem::Leave, MenuItem::Quit};
  static constexpr MenuItem MAIN_ITEMS_WITH_SETTINGS[] = {
      MenuItem::Continue, MenuItem::QuickSettings, MenuItem::Leave, MenuItem::Quit};
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

  // Back to the main page, on the row that led away from it.
  void ShowMainAt(MenuItem item)
  {
    const auto items = m_choice_counts.empty() ? std::span<const MenuItem>(MAIN_ITEMS) :
                                                 std::span<const MenuItem>(MAIN_ITEMS_WITH_SETTINGS);
    std::size_t row = 0;
    while (row + 1 < items.size() && items[row] != item)
      ++row;
    Show(MenuPage::Main, row);
  }

  MenuEvent Choose(std::size_t choice)
  {
    if (choice == m_chosen[m_selected])
      return MenuEvent::None;
    m_chosen[m_selected] = choice;
    m_changed_row = m_selected;
    ++m_version;
    return MenuEvent::SettingChanged;
  }

  bool m_open = false;
  MenuPage m_page = MenuPage::Main;
  std::size_t m_selected = 0;
  unsigned m_version = 0;
  std::vector<std::size_t> m_choice_counts;
  std::vector<std::size_t> m_chosen;
  std::vector<MenuItem> m_setting_items;
  std::size_t m_changed_row = 0;
};

// Turns the buttons held at each poll into presses: a button counts once when
// it goes down, and the directions, held, count again after REPEAT_DELAY and
// then every REPEAT_INTERVAL, to run through rows and choices.
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
    pressed.left = Repeat(held.left, m_held.left, &m_left_next, now);
    pressed.right = Repeat(held.right, m_held.right, &m_right_next, now);
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
  Clock::time_point m_left_next;
  Clock::time_point m_right_next;
};
}  // namespace moderngekko::detail
