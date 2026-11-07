// The in-game menu's pages and buttons (src/runtime/game_menu_state.hpp):
// it opens on the main page, leaving the game always asks first with No
// selected, B goes back the way it came, the rows wrap, and held buttons
// count once except up and down, which repeat.
#include "runtime/game_menu_state.hpp"

#include <chrono>
#include <cstdio>

namespace
{
using moderngekko::detail::MenuButtonRepeat;
using moderngekko::detail::MenuButtons;
using moderngekko::detail::MenuEvent;
using moderngekko::detail::MenuItem;
using moderngekko::detail::MenuPage;
using moderngekko::detail::MenuState;

MenuButtons Open()
{
  MenuButtons b;
  b.open = true;
  return b;
}
MenuButtons Accept()
{
  MenuButtons b;
  b.accept = true;
  return b;
}
MenuButtons Back()
{
  MenuButtons b;
  b.back = true;
  return b;
}
MenuButtons Up()
{
  MenuButtons b;
  b.up = true;
  return b;
}
MenuButtons Down()
{
  MenuButtons b;
  b.down = true;
  return b;
}

MenuItem SelectedItem(const MenuState& state)
{
  return state.Items()[state.Selected()];
}
}  // namespace

int main()
{
  {
    MenuState state;
    // Closed, only the menu button does anything.
    if (state.IsOpen() || state.Press(Accept()) != MenuEvent::None ||
        state.Press(Down()) != MenuEvent::None || state.IsOpen())
      return 1;
    const unsigned closed_version = state.Version();
    if (state.Press(Open()) != MenuEvent::Opened || !state.IsOpen() ||
        state.Page() != MenuPage::Main || SelectedItem(state) != MenuItem::Continue ||
        state.Items().size() != 3 || state.Version() == closed_version)
      return 2;
    // Continue closes it; so do B and the menu button.
    if (state.Press(Accept()) != MenuEvent::Closed || state.IsOpen())
      return 3;
    state.Press(Open());
    if (state.Press(Back()) != MenuEvent::Closed || state.IsOpen())
      return 4;
    state.Press(Open());
    if (state.Press(Open()) != MenuEvent::Closed || state.IsOpen())
      return 5;
  }
  {
    MenuState state;
    state.Press(Open());
    // The rows wrap both ways.
    state.Press(Up());
    if (SelectedItem(state) != MenuItem::Quit)
      return 6;
    state.Press(Down());
    if (SelectedItem(state) != MenuItem::Continue)
      return 7;
    state.Press(Down());
    if (SelectedItem(state) != MenuItem::Leave)
      return 8;
    // Back to the launcher asks first, with No selected; No and B come back
    // to the row that asked.
    if (state.Press(Accept()) != MenuEvent::None || state.Page() != MenuPage::ConfirmLeave ||
        SelectedItem(state) != MenuItem::No || state.Items().size() != 2)
      return 9;
    if (state.Press(Accept()) != MenuEvent::None || state.Page() != MenuPage::Main ||
        SelectedItem(state) != MenuItem::Leave || !state.IsOpen())
      return 10;
    state.Press(Accept());
    if (state.Press(Back()) != MenuEvent::None || state.Page() != MenuPage::Main ||
        SelectedItem(state) != MenuItem::Leave)
      return 11;
    // Yes leaves, and the menu is closed after.
    state.Press(Accept());
    state.Press(Down());
    if (SelectedItem(state) != MenuItem::Yes || state.Press(Accept()) != MenuEvent::Leave ||
        state.IsOpen())
      return 12;
  }
  {
    MenuState state;
    state.Press(Open());
    state.Press(Up());
    if (state.Press(Accept()) != MenuEvent::None || state.Page() != MenuPage::ConfirmQuit)
      return 13;
    state.Press(Back());
    if (state.Page() != MenuPage::Main || SelectedItem(state) != MenuItem::Quit)
      return 14;
    state.Press(Accept());
    state.Press(Up());
    if (SelectedItem(state) != MenuItem::Yes || state.Press(Accept()) != MenuEvent::Quit ||
        state.IsOpen())
      return 15;
    // Opened again, it starts over on the main page.
    if (state.Press(Open()) != MenuEvent::Opened || state.Page() != MenuPage::Main ||
        SelectedItem(state) != MenuItem::Continue)
      return 16;
    // The menu button closes it from a question too.
    state.Press(Down());
    state.Press(Accept());
    if (state.Press(Open()) != MenuEvent::Closed || state.IsOpen())
      return 17;
  }
  {
    // One press counts per poll: the menu button before B before A.
    MenuState state;
    state.Press(Open());
    MenuButtons both = Accept();
    both.back = true;
    if (state.Press(both) != MenuEvent::Closed)
      return 18;
  }
  {
    using Clock = MenuButtonRepeat::Clock;
    MenuButtonRepeat repeat;
    const Clock::time_point start{};
    // A held counts once.
    if (!repeat.Update(Accept(), start).accept ||
        repeat.Update(Accept(), start + std::chrono::seconds(2)).accept)
      return 19;
    repeat.Update({}, start + std::chrono::seconds(3));
    if (!repeat.Update(Accept(), start + std::chrono::seconds(4)).accept)
      return 20;
    // Down held: once, then after the delay, then every interval.
    const Clock::time_point down = start + std::chrono::seconds(10);
    if (!repeat.Update(Down(), down).down)
      return 21;
    if (repeat.Update(Down(), down + MenuButtonRepeat::REPEAT_DELAY / 2).down)
      return 22;
    if (!repeat.Update(Down(), down + MenuButtonRepeat::REPEAT_DELAY).down)
      return 23;
    if (repeat.Update(Down(), down + MenuButtonRepeat::REPEAT_DELAY +
                                  MenuButtonRepeat::REPEAT_INTERVAL / 2)
            .down)
      return 24;
    if (!repeat.Update(Down(), down + MenuButtonRepeat::REPEAT_DELAY +
                                   MenuButtonRepeat::REPEAT_INTERVAL)
             .down)
      return 25;
    // Let go and pressed again: at once.
    repeat.Update({}, down + std::chrono::seconds(5));
    if (!repeat.Update(Down(), down + std::chrono::seconds(5) + std::chrono::milliseconds(1)).down)
      return 26;
    MenuButtons held = Up();
    if (!held.Any() || MenuButtons{}.Any())
      return 27;
  }
  std::puts("game menu state: ok");
  return 0;
}
