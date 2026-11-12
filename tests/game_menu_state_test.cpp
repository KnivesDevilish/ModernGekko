// The in-game menu's pages and buttons (src/runtime/game_menu_state.hpp):
// it opens on the main page, leaving the game always asks first with No
// selected, B goes back the way it came, the rows wrap, quick settings step
// with left and right and go round with A, the Controls page has nothing to
// choose, Take screenshot leaves the menu as it is, and held buttons count
// once except the directions, which repeat.
#include "runtime/game_menu_state.hpp"

#include <algorithm>
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

MenuButtons Left()
{
  MenuButtons b;
  b.left = true;
  return b;
}
MenuButtons Right()
{
  MenuButtons b;
  b.right = true;
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
    // Only the buttons keep a closed menu's game waiting, not the stick.
    MenuButtons steering = Left();
    steering.up = true;
    if (steering.AnyButton() || !Back().AnyButton() || !Open().AnyButton() ||
        !Accept().AnyButton())
      return 63;
  }
  {
    // With quick settings, the main page has a Quick settings row second.
    MenuState state({3, 11}, {1, 10});
    state.Press(Open());
    if (state.Items().size() != 4 || state.Items()[1] != MenuItem::QuickSettings ||
        state.SettingCount() != 2 || state.Chosen(0) != 1 || state.Chosen(1) != 10)
      return 28;
    state.Press(Down());
    if (state.Press(Accept()) != MenuEvent::None || state.Page() != MenuPage::QuickSettings ||
        state.Items().size() != 2 || SelectedItem(state) != MenuItem::Setting ||
        state.Selected() != 0)
      return 29;
    // Right and left step and stop at the ends; A goes round.
    if (state.Press(Right()) != MenuEvent::SettingChanged || state.Chosen(0) != 2 ||
        state.ChangedRow() != 0)
      return 30;
    if (state.Press(Right()) != MenuEvent::None || state.Chosen(0) != 2)
      return 31;
    if (state.Press(Accept()) != MenuEvent::SettingChanged || state.Chosen(0) != 0)
      return 32;
    if (state.Press(Left()) != MenuEvent::None || state.Chosen(0) != 0)
      return 33;
    state.Press(Down());
    const unsigned before = state.Version();
    if (state.Press(Left()) != MenuEvent::SettingChanged || state.Chosen(1) != 9 ||
        state.ChangedRow() != 1 || state.Version() == before)
      return 34;
    // B goes back to the main page, on Quick settings.
    if (state.Press(Back()) != MenuEvent::None || state.Page() != MenuPage::Main ||
        SelectedItem(state) != MenuItem::QuickSettings || !state.IsOpen())
      return 35;
    // Left and right do nothing elsewhere; the confirm pages still return to
    // their rows.
    if (state.Press(Right()) != MenuEvent::None || state.Page() != MenuPage::Main)
      return 36;
    state.Press(Down());
    state.Press(Accept());
    if (state.Page() != MenuPage::ConfirmLeave)
      return 37;
    state.Press(Back());
    if (SelectedItem(state) != MenuItem::Leave)
      return 38;
    state.Press(Down());
    state.Press(Accept());
    state.Press(Accept());
    if (state.Page() != MenuPage::Main || SelectedItem(state) != MenuItem::Quit)
      return 39;
    // The choices made stay when the menu opens again.
    state.Press(Open());
    state.Press(Open());
    if (state.Chosen(0) != 0 || state.Chosen(1) != 9)
      return 40;
    // A chosen past the end, or a setting without choices, starts at 0.
    MenuState odd({2, 0}, {5});
    if (odd.Chosen(0) != 0 || odd.Chosen(1) != 0)
      return 41;
  }
  {
    // Left and right repeat like up and down.
    using Clock = MenuButtonRepeat::Clock;
    MenuButtonRepeat repeat;
    const Clock::time_point start{};
    if (!repeat.Update(Right(), start).right ||
        repeat.Update(Right(), start + MenuButtonRepeat::REPEAT_DELAY / 2).right ||
        !repeat.Update(Right(), start + MenuButtonRepeat::REPEAT_DELAY).right ||
        !repeat.Update(Left(), start + std::chrono::seconds(1)).left || !Left().Any())
      return 42;
  }
  {
    // See controls and Take screenshot come after Quick settings, before
    // leaving; each only when the menu has it.
    using moderngekko::detail::MenuRows;
    const MenuItem all[] = {MenuItem::Continue, MenuItem::QuickSettings, MenuItem::Controls,
                            MenuItem::Screenshot, MenuItem::Leave,       MenuItem::Quit};
    MenuState state({3}, {0}, MenuRows{.controls = true, .screenshot = true});
    state.Press(Open());
    if (!std::ranges::equal(state.Items(), all))
      return 55;
    const MenuItem some[] = {MenuItem::Continue, MenuItem::Screenshot, MenuItem::Leave,
                             MenuItem::Quit};
    MenuState without({}, {}, MenuRows{.screenshot = true});
    without.Press(Open());
    if (!std::ranges::equal(without.Items(), some))
      return 56;

    // The Controls page has nothing to choose: up and down do nothing, and A
    // and B both come back to its row.
    state.Press(Down());
    state.Press(Down());
    if (SelectedItem(state) != MenuItem::Controls ||
        state.Press(Accept()) != MenuEvent::None || state.Page() != MenuPage::Controls ||
        !state.Items().empty())
      return 57;
    const unsigned controls_version = state.Version();
    if (state.Press(Up()) != MenuEvent::None || state.Press(Down()) != MenuEvent::None ||
        state.Press(Left()) != MenuEvent::None || state.Press(Right()) != MenuEvent::None ||
        state.Version() != controls_version || state.Page() != MenuPage::Controls)
      return 58;
    if (state.Press(Back()) != MenuEvent::None || state.Page() != MenuPage::Main ||
        SelectedItem(state) != MenuItem::Controls)
      return 59;
    state.Press(Accept());
    if (state.Press(Accept()) != MenuEvent::None || state.Page() != MenuPage::Main ||
        SelectedItem(state) != MenuItem::Controls || !state.IsOpen())
      return 60;

    // Take screenshot leaves the menu open where it is, and only tells.
    state.Press(Down());
    const unsigned shot_version = state.Version();
    if (SelectedItem(state) != MenuItem::Screenshot ||
        state.Press(Accept()) != MenuEvent::Screenshot || !state.IsOpen() ||
        state.Page() != MenuPage::Main || SelectedItem(state) != MenuItem::Screenshot ||
        state.Version() != shot_version)
      return 61;
    // Leaving still asks first, from its new row.
    state.Press(Down());
    state.Press(Accept());
    state.Press(Back());
    if (state.Page() != MenuPage::Main || SelectedItem(state) != MenuItem::Leave)
      return 62;
  }
  std::puts("game menu state: ok");
  return 0;
}
