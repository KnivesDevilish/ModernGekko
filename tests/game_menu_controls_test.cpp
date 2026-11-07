// The in-game menu's Controls page (src/runtime/game_menu_controls.hpp): the
// profile's bindings become readable names, the gamepad's apart from the
// keyboard's; rows of several directions name whole sticks; a PlayStation
// pad's face buttons are its symbols; bindings written by hand beyond "|"
// are reported as such.
#include "runtime/game_menu_controls.hpp"

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace
{
using moderngekko::detail::ControlsRow;
using moderngekko::detail::PadDeviceName;
using moderngekko::detail::ReadControls;

struct Control
{
  std::string label;
  std::vector<std::string> keys;
};

constexpr const char* KEYBOARD = "XInput2/0/Virtual core pointer";

std::vector<ControlsRow> Read(const std::map<std::string, std::string>& profile,
                              const std::vector<Control>& controls, const std::string& device)
{
  return ReadControls(
      controls,
      [&](const std::string& key) -> std::optional<std::string> {
        const auto found = profile.find(key);
        if (found == profile.end())
          return std::nullopt;
        return found->second;
      },
      device);
}

bool Is(const std::vector<std::string>& names, const std::vector<std::string>& expected)
{
  return names == expected;
}
}  // namespace

int main()
{
  const std::string k = std::string("`") + KEYBOARD + ":";
  // As GenerateControllerConfig writes the first GameCube controller.
  const std::map<std::string, std::string> profile = {
      {"Buttons/A", "`Button A`" " | " + k + "S`"},
      {"Buttons/Z", "`Trigger R` | " + k + "Q`"},
      {"Buttons/Start", "Start | " + k + "Return`"},
      {"Main Stick/Up", "`Left Y+` | " + k + "Up`"},
      {"Main Stick/Down", "`Left Y-` | " + k + "Down`"},
      {"Main Stick/Left", "`Left X-` | " + k + "Left`"},
      {"Main Stick/Right", "`Left X+` | " + k + "Right`"},
      {"D-Pad/Up", "`Pad N`"},
      {"D-Pad/Down", "`Pad S`"},
      {"Triggers/L", "`Shoulder L` | " + k + "Shift_L`"},
      {"Triggers/L-Analog", "`Shoulder L` | " + k + "Shift_L`"},
      {"Extra/Menu", "Guide | " + k + "M`"},
  };
  const std::vector<Control> controls = {
      {"A", {"Buttons/A"}},
      {"Z", {"Buttons/Z"}},
      {"Start", {"Buttons/Start"}},
      {"Stick",
       {"Main Stick/Up", "Main Stick/Down", "Main Stick/Left", "Main Stick/Right", "D-Pad/Up",
        "D-Pad/Down"}},
      {"L", {"Triggers/L", "Triggers/L-Analog"}},
      {"Menu", {"Extra/Menu"}},
      {"Y", {"Buttons/Y"}},
  };
  {
    const auto rows = Read(profile, controls, "SDL/0/Xbox Series X Controller");
    if (rows.size() != 7 || rows[0].label != "A" || !Is(rows[0].pad, {"A"}) ||
        !Is(rows[0].keys, {"S"}) || rows[0].custom)
      return 63;
    if (!Is(rows[1].pad, {"R trigger"}) || !Is(rows[1].keys, {"Q"}) ||
        !Is(rows[2].pad, {"Start"}) || !Is(rows[2].keys, {"Enter"}))
      return 64;
    // The stick and the D-pad in one row: named once each.
    if (!Is(rows[3].pad, {"Left stick", "D-pad"}) || !Is(rows[3].keys, {"Arrow keys"}))
      return 65;
    // L and its analog side share their inputs.
    if (!Is(rows[4].pad, {"L shoulder"}) || !Is(rows[4].keys, {"Left Shift"}) ||
        !Is(rows[5].pad, {"Home"}) || !Is(rows[5].keys, {"M"}))
      return 66;
    // A key the profile doesn't have: nothing, not custom.
    if (!rows[6].pad.empty() || !rows[6].keys.empty() || rows[6].custom)
      return 67;
  }
  {
    // A PlayStation pad's face buttons; a single direction keeps its name.
    const std::map<std::string, std::string> ps = {
        {"Buttons/A", "`Button A`"}, {"Buttons/B", "`Button E`"}, {"D-Pad/Up", "`Pad N`"},
        {"Main Stick/Up", "`Right Y-` | " + k + "Up`"}};
    const auto rows = Read(ps,
                           {{"A", {"Buttons/A"}},
                            {"B", {"Buttons/B"}},
                            {"Up", {"D-Pad/Up"}},
                            {"Stick up", {"Main Stick/Up"}}},
                           "SDL/1/PS5 Controller");
    if (!Is(rows[0].pad, {"Cross"}) || !Is(rows[1].pad, {"Circle"}) ||
        !Is(rows[2].pad, {"D-pad up"}) || !Is(rows[3].pad, {"Right stick down"}) ||
        !Is(rows[3].keys, {"Up arrow"}))
      return 68;
  }
  {
    // Without a pad, the profile's device is the keyboard: the pad names the
    // generated profile still has are no keys.
    const auto rows = Read(profile, controls, KEYBOARD);
    if (!rows[0].pad.empty() || !Is(rows[0].keys, {"S"}) || !rows[2].pad.empty() ||
        !Is(rows[2].keys, {"Enter"}) || !Is(rows[3].keys, {"Arrow keys"}) ||
        !rows[3].pad.empty())
      return 69;
  }
  {
    // Written by hand: "&", "!" or functions are custom; plain names a
    // backend doesn't spell the way the page knows stay as they are.
    const std::map<std::string, std::string> edited = {
        {"Buttons/A", "`Button A` & `Button B`"},
        {"Buttons/B", "!`Button B`"},
        {"Buttons/X", "`Paddle 1` | `Some Axis` | " + k + "F1` | `DInput/0/Keyboard Mouse:BACK`"},
        {"Buttons/Y", "`Button Y"},
        {"Buttons/Z", "  "}};
    const auto rows = Read(edited,
                           {{"A", {"Buttons/A"}},
                            {"B", {"Buttons/B"}},
                            {"X", {"Buttons/X"}},
                            {"Y", {"Buttons/Y"}},
                            {"Z", {"Buttons/Z"}}},
                           "SDL/0/Xbox Controller");
    if (!rows[0].custom || !rows[1].custom || rows[2].custom || !rows[3].custom ||
        rows[4].custom || !rows[4].pad.empty() || !rows[4].keys.empty())
      return 70;
    if (!Is(rows[2].pad, {"Paddle 1", "Some Axis"}) || !Is(rows[2].keys, {"F1", "Backspace"}))
      return 71;
  }
  if (PadDeviceName("SDL/0/Xbox Series X Controller") != "Xbox Series X Controller" ||
      PadDeviceName(" SDL/12/8BitDo Pro 2 ") != "8BitDo Pro 2" ||
      !PadDeviceName(KEYBOARD).empty() || !PadDeviceName("").empty())
    return 72;
  std::puts("game menu controls: ok");
  return 0;
}
