#pragma once

// What the in-game menu's Controls page lists: each button of the emulated
// controller and the gamepad inputs and keyboard keys that press it, read from
// the player's profile (GCPadNew.ini or WiimoteNew.ini) the way the launcher's
// tester reads it. Apart from Dolphin, so tests/game_menu_controls_test.cpp
// can check it. The names are English; the menu translates them as it draws.

#include "ui_text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace moderngekko::detail
{
// One row of the Controls page.
struct ControlsRow
{
  std::string label;  // the emulated controller's button, as the frontend names it
  std::vector<std::string> pad;
  std::vector<std::string> keys;
  // Written by hand with more than inputs joined by "|" (Dolphin's "&", "!",
  // functions...): the page says so instead of naming its inputs.
  bool custom = false;
};

namespace game_menu_controls
{
inline std::string_view Trim(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return text;
}

// Dolphin's SDL backend names pads "SDL/<n>/<name>"; anything else (the
// keyboard's "XInput2/0/Virtual core pointer", "DInput/0/Keyboard Mouse"...)
// is a keyboard. The launcher's tester tells them apart the same way.
inline bool IsPadDevice(std::string_view device)
{
  device = Trim(device);
  return device.empty() || device.starts_with("SDL/");
}

// A PlayStation pad's face buttons have symbols, not letters.
inline bool HasPlayStationFaces(std::string_view device)
{
  constexpr std::array<std::string_view, 7> names = {
      "PS3", "PS4", "PS5", "DualShock", "DualSense", "PlayStation", "Sony"};
  return std::ranges::any_of(names, [&](std::string_view name) {
    return device.find(name) != std::string_view::npos;
  });
}

// A gamepad input as Dolphin's SDL backend names it; nothing for a name that
// isn't one. whole: the row covers several directions, so a stick or the
// D-pad is named once ("Left stick") instead of by direction.
inline std::optional<std::string> PadName(std::string_view name, bool whole, bool playstation)
{
  // Face buttons: Dolphin's XInput-style names (the generated profile's) and
  // its SDL position names.
  constexpr std::array<std::array<std::string_view, 2>, 4> faces = {{
      {"Button A", "Button S"},
      {"Button B", "Button E"},
      {"Button X", "Button W"},
      {"Button Y", "Button N"},
  }};
  constexpr std::array<const char*, 4> letters = {"A", "B", "X", "Y"};
  constexpr std::array<const char*, 4> symbols = {
      MODERNGEKKO_UI_TEXT("Cross"), MODERNGEKKO_UI_TEXT("Circle"), MODERNGEKKO_UI_TEXT("Square"),
      MODERNGEKKO_UI_TEXT("Triangle")};
  for (std::size_t i = 0; i < faces.size(); ++i)
  {
    if (name == faces[i][0] || name == faces[i][1])
      return playstation ? symbols[i] : letters[i];
  }
  constexpr std::array<std::pair<std::string_view, const char*>, 9> buttons = {{
      {"Trigger L", MODERNGEKKO_UI_TEXT("L trigger")},
      {"Trigger R", MODERNGEKKO_UI_TEXT("R trigger")},
      {"Shoulder L", MODERNGEKKO_UI_TEXT("L shoulder")},
      {"Shoulder R", MODERNGEKKO_UI_TEXT("R shoulder")},
      {"Thumb L", MODERNGEKKO_UI_TEXT("L stick press")},
      {"Thumb R", MODERNGEKKO_UI_TEXT("R stick press")},
      {"Back", MODERNGEKKO_UI_TEXT("Back")},
      {"Guide", "Home"},
      {"Start", "Start"},
  }};
  for (const auto& [dolphin, shown] : buttons)
  {
    if (name == dolphin)
      return shown;
  }
  constexpr std::array<std::pair<std::string_view, const char*>, 4> dpad = {{
      {"Pad N", MODERNGEKKO_UI_TEXT("D-pad up")},
      {"Pad S", MODERNGEKKO_UI_TEXT("D-pad down")},
      {"Pad W", MODERNGEKKO_UI_TEXT("D-pad left")},
      {"Pad E", MODERNGEKKO_UI_TEXT("D-pad right")},
  }};
  for (const auto& [dolphin, shown] : dpad)
  {
    if (name == dolphin)
      return whole ? MODERNGEKKO_UI_TEXT("D-pad") : shown;
  }
  // "Left X-" ... "Right Y+": Dolphin's Y+ is up.
  constexpr std::array<std::pair<std::string_view, std::array<const char*, 5>>, 2> sticks = {{
      {"Left ",
       {MODERNGEKKO_UI_TEXT("Left stick"), MODERNGEKKO_UI_TEXT("Left stick left"),
        MODERNGEKKO_UI_TEXT("Left stick right"), MODERNGEKKO_UI_TEXT("Left stick up"),
        MODERNGEKKO_UI_TEXT("Left stick down")}},
      {"Right ",
       {MODERNGEKKO_UI_TEXT("Right stick"), MODERNGEKKO_UI_TEXT("Right stick left"),
        MODERNGEKKO_UI_TEXT("Right stick right"), MODERNGEKKO_UI_TEXT("Right stick up"),
        MODERNGEKKO_UI_TEXT("Right stick down")}},
  }};
  for (const auto& [side, shown] : sticks)
  {
    if (name.size() != side.size() + 2 || !name.starts_with(side))
      continue;
    const char axis = name[side.size()];
    const char sign = name[side.size() + 1];
    if ((axis != 'X' && axis != 'Y') || (sign != '+' && sign != '-'))
      continue;
    if (whole)
      return shown[0];
    if (axis == 'X')
      return sign == '-' ? shown[1] : shown[2];
    return sign == '+' ? shown[3] : shown[4];
  }
  constexpr std::array<std::string_view, 6> others = {"Misc 1",   "Paddle 1", "Paddle 2",
                                                      "Paddle 3", "Paddle 4", "Touchpad"};
  if (std::ranges::contains(others, name))
    return std::string(name);
  return std::nullopt;
}

// A keyboard key as Dolphin's keyboard backends name it: X11, DirectInput and
// macOS spell some differently.
inline std::string KeyName(std::string_view name, bool whole)
{
  if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])))
    return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
  constexpr std::array<std::pair<std::string_view, const char*>, 12> arrows = {{
      {"Up", MODERNGEKKO_UI_TEXT("Up arrow")},
      {"UP", MODERNGEKKO_UI_TEXT("Up arrow")},
      {"Up Arrow", MODERNGEKKO_UI_TEXT("Up arrow")},
      {"Down", MODERNGEKKO_UI_TEXT("Down arrow")},
      {"DOWN", MODERNGEKKO_UI_TEXT("Down arrow")},
      {"Down Arrow", MODERNGEKKO_UI_TEXT("Down arrow")},
      {"Left", MODERNGEKKO_UI_TEXT("Left arrow")},
      {"LEFT", MODERNGEKKO_UI_TEXT("Left arrow")},
      {"Left Arrow", MODERNGEKKO_UI_TEXT("Left arrow")},
      {"Right", MODERNGEKKO_UI_TEXT("Right arrow")},
      {"RIGHT", MODERNGEKKO_UI_TEXT("Right arrow")},
      {"Right Arrow", MODERNGEKKO_UI_TEXT("Right arrow")},
  }};
  for (const auto& [dolphin, shown] : arrows)
  {
    if (name == dolphin)
      return whole ? MODERNGEKKO_UI_TEXT("Arrow keys") : shown;
  }
  constexpr std::array<std::pair<std::string_view, const char*>, 20> keys = {{
      {"Return", MODERNGEKKO_UI_TEXT("Enter")},
      {"RETURN", MODERNGEKKO_UI_TEXT("Enter")},
      {"Shift_L", MODERNGEKKO_UI_TEXT("Left Shift")},
      {"LSHIFT", MODERNGEKKO_UI_TEXT("Left Shift")},
      {"Left Shift", MODERNGEKKO_UI_TEXT("Left Shift")},
      {"Shift_R", MODERNGEKKO_UI_TEXT("Right Shift")},
      {"RSHIFT", MODERNGEKKO_UI_TEXT("Right Shift")},
      {"Right Shift", MODERNGEKKO_UI_TEXT("Right Shift")},
      {"space", MODERNGEKKO_UI_TEXT("Space")},
      {"SPACE", MODERNGEKKO_UI_TEXT("Space")},
      {"Space", MODERNGEKKO_UI_TEXT("Space")},
      {"Tab", "Tab"},
      {"TAB", "Tab"},
      {"Control_L", MODERNGEKKO_UI_TEXT("Left Ctrl")},
      {"LCONTROL", MODERNGEKKO_UI_TEXT("Left Ctrl")},
      {"Escape", "Esc"},
      {"ESCAPE", "Esc"},
      {"BackSpace", MODERNGEKKO_UI_TEXT("Backspace")},
      {"BACK", MODERNGEKKO_UI_TEXT("Backspace")},
      {"Backspace", MODERNGEKKO_UI_TEXT("Backspace")},
  }};
  for (const auto& [dolphin, shown] : keys)
  {
    if (name == dolphin)
      return shown;
  }
  return std::string(name);
}

inline void AddOnce(std::vector<std::string>& names, std::string name)
{
  if (!name.empty() && std::ranges::find(names, name) == names.end())
    names.push_back(std::move(name));
}
}  // namespace game_menu_controls

// Adds the inputs of one binding of the profile to row: input names joined by
// "|", such as "`Button A` | `XInput2/0/Virtual core pointer:S`". A name may
// carry its device; names without one belong to the profile's device.
// Anything else marks the row custom.
inline void AddBinding(std::string_view expression, std::string_view device, bool whole,
                       ControlsRow& row)
{
  using namespace game_menu_controls;
  std::vector<std::string_view> terms;
  bool quoted = false;
  std::size_t start = 0;
  for (std::size_t i = 0; i < expression.size(); ++i)
  {
    const char c = expression[i];
    if (c == '`')
    {
      quoted = !quoted;
      continue;
    }
    if (quoted || std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '\t')
      continue;
    if (c == '|')
    {
      terms.push_back(expression.substr(start, i - start));
      start = i + 1;
      continue;
    }
    row.custom = true;
    return;
  }
  if (quoted)
  {
    row.custom = true;
    return;
  }
  terms.push_back(expression.substr(start));
  for (std::string_view term : terms)
  {
    std::string name;
    for (const char c : term)
    {
      if (c != '`')
        name += c;
    }
    std::string_view input = Trim(name);
    if (input.empty())
      continue;
    std::string_view input_device = device;
    if (const std::size_t colon = input.rfind(':'); colon != std::string_view::npos)
    {
      input_device = Trim(input.substr(0, colon));
      input = Trim(input.substr(colon + 1));
    }
    const std::optional<std::string> pad_name =
        PadName(input, whole, HasPlayStationFaces(input_device));
    if (IsPadDevice(input_device))
      AddOnce(row.pad, pad_name.value_or(std::string(input)));
    // The generated profile also names the pad's buttons on a profile whose
    // device is the keyboard (no pad was connected): those are no keys.
    else if (!pad_name)
      AddOnce(row.keys, KeyName(input, whole));
  }
}

// The rows of the page: for each button the frontend lists (its label and
// the profile keys behind it, such as {"Stick", {"Main Stick/Up", ...}}),
// what the profile binds to it. value gives a profile key's binding, if any;
// device is the profile's Device line.
template <typename Control>
std::vector<ControlsRow>
ReadControls(const std::vector<Control>& controls,
             const std::function<std::optional<std::string>(const std::string&)>& value,
             std::string_view device)
{
  std::vector<ControlsRow> rows;
  for (const Control& control : controls)
  {
    ControlsRow row;
    row.label = control.label;
    const bool whole = control.keys.size() > 1;
    for (const std::string& key : control.keys)
    {
      if (const std::optional<std::string> binding = value(key))
        AddBinding(*binding, device, whole, row);
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

// The pad's own name for the page's subtitle: "SDL/0/Xbox Series X
// Controller" is "Xbox Series X Controller". Empty for a keyboard.
inline std::string PadDeviceName(std::string_view device)
{
  device = game_menu_controls::Trim(device);
  if (!device.starts_with("SDL/"))
    return {};
  device.remove_prefix(4);
  const std::size_t slash = device.find('/');
  return std::string(slash == std::string_view::npos ? device : device.substr(slash + 1));
}
}  // namespace moderngekko::detail
