#pragma once

// What the in-game menu's Controls page lists: each button of the emulated
// controller and the gamepad inputs and keyboard keys that press it, read from
// the player's profile (GCPadNew.ini or WiimoteNew.ini) the way the launcher's
// tester reads it. Apart from Dolphin, so tests/game_menu_controls_test.cpp
// can check it. The names are English; the menu translates them as it draws.
// The hints at the bottom of the menu come from here too: a binding split
// into its gamepad and keyboard inputs, and the button each hint draws.

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

// A Nintendo pad prints its letters in other places: B at the bottom, A on
// the right, Y on the left and X at the top.
inline bool IsNintendoPad(std::string_view device)
{
  constexpr std::array<std::string_view, 4> names = {"Nintendo", "Switch", "Pro Controller",
                                                     "Joy-Con"};
  return std::ranges::any_of(names, [&](std::string_view name) {
    return device.find(name) != std::string_view::npos;
  });
}

// The face buttons as Dolphin's SDL backend names them, by place: bottom,
// right, left, top. Its XInput-style names (the generated profile's) and its
// SDL position names.
inline constexpr std::array<std::array<std::string_view, 2>, 4> FACE_BUTTONS = {{
    {"Button A", "Button S"},
    {"Button B", "Button E"},
    {"Button X", "Button W"},
    {"Button Y", "Button N"},
}};

// A gamepad input as Dolphin's SDL backend names it; nothing for a name that
// isn't one. whole: the row covers several directions, so a stick or the
// D-pad is named once ("Left stick") instead of by direction. playstation and
// nintendo: the pad's face buttons are named as that pad prints them.
inline std::optional<std::string> PadName(std::string_view name, bool whole, bool playstation,
                                          bool nintendo = false)
{
  constexpr std::array<const char*, 4> letters = {"A", "B", "X", "Y"};
  constexpr std::array<const char*, 4> nintendo_letters = {"B", "A", "Y", "X"};
  constexpr std::array<const char*, 4> symbols = {
      MODERNGEKKO_UI_TEXT("Cross"), MODERNGEKKO_UI_TEXT("Circle"), MODERNGEKKO_UI_TEXT("Square"),
      MODERNGEKKO_UI_TEXT("Triangle")};
  for (std::size_t i = 0; i < FACE_BUTTONS.size(); ++i)
  {
    if (name == FACE_BUTTONS[i][0] || name == FACE_BUTTONS[i][1])
      return playstation ? symbols[i] : nintendo ? nintendo_letters[i] : letters[i];
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

// The terms of a binding joined by "|", such as "`Button A`" and
// "`XInput2/0/Virtual core pointer:S`"; nothing for one written by hand with
// more than "|" (Dolphin's "&", "!", functions...).
inline std::optional<std::vector<std::string_view>> BindingTerms(std::string_view expression)
{
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
    if (c != '|')
      return std::nullopt;
    terms.push_back(expression.substr(start, i - start));
    start = i + 1;
  }
  if (quoted)
    return std::nullopt;
  terms.push_back(expression.substr(start));
  return terms;
}

// A term's input and its device: the device it names before ":", or the
// profile's.
struct TermInput
{
  std::string input;
  std::string device;
};

inline TermInput InputOfTerm(std::string_view term, std::string_view profile_device)
{
  std::string name;
  for (const char c : term)
  {
    if (c != '`')
      name += c;
  }
  std::string_view input = Trim(name);
  std::string_view device = profile_device;
  if (const std::size_t colon = input.rfind(':'); colon != std::string_view::npos)
  {
    device = Trim(input.substr(0, colon));
    input = Trim(input.substr(colon + 1));
  }
  return {std::string(input), std::string(Trim(device))};
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
  const std::optional<std::vector<std::string_view>> terms = BindingTerms(expression);
  if (!terms)
  {
    row.custom = true;
    return;
  }
  for (std::string_view term : *terms)
  {
    const TermInput input = InputOfTerm(term, device);
    if (input.input.empty())
      continue;
    const std::optional<std::string> pad_name =
        PadName(input.input, whole, HasPlayStationFaces(input.device),
                IsNintendoPad(input.device));
    if (IsPadDevice(input.device))
      AddOnce(row.pad, pad_name.value_or(input.input));
    // The generated profile also names the pad's buttons on a profile whose
    // device is the keyboard (no pad was connected): those are no keys.
    else if (!pad_name)
      AddOnce(row.keys, KeyName(input.input, whole));
  }
}

// A binding of the profile split by device: its gamepad terms and its
// keyboard terms, each joined again by "|" so the menu can tell which of the
// two pressed one of its buttons, and the first input of each, for the hints
// at the bottom of the menu. All empty for a binding written by hand with
// more than "|".
struct DeviceBinding
{
  std::string pad_expression;
  std::string keys_expression;
  std::string first_pad;   // as Dolphin names it: "Button A"
  std::string pad_device;  // first_pad's: "SDL/0/PS4 Controller"
  std::string first_key;   // as the menu names it: "S", "Enter"
};

inline DeviceBinding SplitBinding(std::string_view expression, std::string_view device)
{
  using namespace game_menu_controls;
  DeviceBinding split;
  const std::optional<std::vector<std::string_view>> terms = BindingTerms(expression);
  if (!terms)
    return split;
  const auto add = [](std::string& joined, std::string_view term) {
    if (!joined.empty())
      joined += " | ";
    joined += Trim(term);
  };
  for (std::string_view term : *terms)
  {
    const TermInput input = InputOfTerm(term, device);
    if (input.input.empty())
      continue;
    if (IsPadDevice(input.device))
    {
      add(split.pad_expression, term);
      if (split.first_pad.empty())
      {
        split.first_pad = input.input;
        split.pad_device = input.device;
      }
    }
    else if (!PadName(input.input, false, false))
    {
      add(split.keys_expression, term);
      if (split.first_key.empty())
        split.first_key = KeyName(input.input, false);
    }
  }
  return split;
}

// What the menu's hints draw for a gamepad input: a face button or a
// shoulder by its place, or anything else (Start, a trigger...) by its name.
struct PadHint
{
  enum class Kind
  {
    Face,
    Shoulder,
    Name,
  };
  Kind kind = Kind::Face;
  // Face: 0 bottom, 1 right, 2 left, 3 top. Shoulder: 0 left, 1 right.
  int place = 0;
  std::string name;  // Kind::Name: English, translated when drawn
};

inline PadHint PadHintOf(std::string_view input)
{
  using namespace game_menu_controls;
  for (std::size_t i = 0; i < FACE_BUTTONS.size(); ++i)
  {
    if (input == FACE_BUTTONS[i][0] || input == FACE_BUTTONS[i][1])
      return {PadHint::Kind::Face, static_cast<int>(i), {}};
  }
  if (input == "Shoulder L" || input == "Shoulder R")
    return {PadHint::Kind::Shoulder, input == "Shoulder L" ? 0 : 1, {}};
  return {PadHint::Kind::Name, 0, PadName(input, false, false).value_or(std::string(input))};
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

// The device each GameCube port reads, from its profile's (profile[0] is
// GCPad1's). A gamepad that isn't connected, or the keyboard, gives way to
// the first connected gamepad that no other port reads: the launcher and the
// game may see the same pad under different names (on a Steam Deck, "Steam
// Virtual Gamepad" in the launcher and "Steam Deck Controller" in the game),
// and a profile made while no pad was plugged in names the keyboard. The
// generated profile's pad inputs carry no device, so they follow it, while
// its keys name the keyboard and still work. A port with no device, a
// connected gamepad or any other kind of device keeps its profile's; so does
// a port when no gamepad is free.
inline std::vector<std::string> EffectivePadDevices(const std::vector<std::string>& profile,
                                                    const std::vector<std::string>& connected)
{
  const auto is_pad = [](std::string_view device) { return device.starts_with("SDL/"); };
  const auto is_keyboard = [](std::string_view device) {
    return device.starts_with("XInput2/") || device.starts_with("DInput/") ||
           device.starts_with("Quartz/");
  };
  std::vector<std::string> result = profile;
  for (std::string& device : result)
  {
    const bool missing_pad = is_pad(device) && !std::ranges::contains(connected, device);
    if (!missing_pad && !is_keyboard(device))
      continue;
    const auto free_pad = std::ranges::find_if(connected, [&](const std::string& candidate) {
      return is_pad(candidate) && !std::ranges::contains(result, candidate);
    });
    if (free_pad != connected.end())
      device = *free_pad;
  }
  return result;
}
}  // namespace moderngekko::detail
