#pragma once

// Pictures of a controller's buttons and of keyboard keys, for the hints on
// screen: the launcher's footer (a gamepad's buttons or the keyboard's keys,
// whichever was used last) and the in-game menu's; and a small picture of
// the device itself. A button looks the way the controller prints it: Xbox's coloured
// A, B, X and Y, PlayStation's cross, circle, square and triangle, Nintendo's
// letters in their own places. Drawn with Dear ImGui's draw list, so no font
// needs the symbols.

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <string>

namespace moderngekko::frontend::ui
{
// How a controller labels its buttons.
enum class PadStyle
{
  Xbox,         // also any controller SDL doesn't know better
  PlayStation,
  Nintendo,
};

// A button by where it is on the controller, as SDL names them (SOUTH is A on
// an Xbox pad, the cross on a PlayStation one and B on a Nintendo one).
enum class PadButton
{
  South,
  East,
  West,
  North,
  LeftShoulder,
  RightShoulder,
};

namespace button_glyphs_detail
{
inline ImU32 Colour(float r, float g, float b, float alpha)
{
  return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, std::clamp(alpha, 0.0f, 1.0f)));
}

inline bool IsFace(PadButton button)
{
  return button != PadButton::LeftShoulder && button != PadButton::RightShoulder;
}

// The letters on the face buttons and shoulders; empty for PlayStation's
// face buttons, which are symbols.
inline const char* Label(PadStyle style, PadButton button)
{
  switch (button)
  {
  case PadButton::South:
    return style == PadStyle::Xbox ? "A" : style == PadStyle::Nintendo ? "B" : "";
  case PadButton::East:
    return style == PadStyle::Xbox ? "B" : style == PadStyle::Nintendo ? "A" : "";
  case PadButton::West:
    return style == PadStyle::Xbox ? "X" : style == PadStyle::Nintendo ? "Y" : "";
  case PadButton::North:
    return style == PadStyle::Xbox ? "Y" : style == PadStyle::Nintendo ? "X" : "";
  case PadButton::LeftShoulder:
    return style == PadStyle::Xbox ? "LB" : style == PadStyle::PlayStation ? "L1" : "L";
  case PadButton::RightShoulder:
    return style == PadStyle::Xbox ? "RB" : style == PadStyle::PlayStation ? "R1" : "R";
  }
  return "";
}

inline void CentredText(ImDrawList* list, ImFont* font, float size, ImVec2 centre, ImU32 colour,
                        const char* text)
{
  const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
  list->AddText(font, size,
                ImVec2(std::floor(centre.x - extent.x * 0.5f),
                       std::floor(centre.y - extent.y * 0.5f)),
                colour, text);
}

// A shoulder or a key: a rounded box around its name.
inline float BoxWidth(ImFont* font, float height, const char* text)
{
  const float text_width = font->CalcTextSizeA(height * 0.58f, FLT_MAX, 0.0f, text).x;
  return std::max(height, std::ceil(text_width + height * 0.6f));
}
}  // namespace button_glyphs_detail

// How wide a button's picture is at the given height.
inline float PadButtonWidth(ImFont* font, float height, PadStyle style, PadButton button)
{
  if (button_glyphs_detail::IsFace(button))
    return height;
  return button_glyphs_detail::BoxWidth(font, height, button_glyphs_detail::Label(style, button));
}

// How wide a pad button drawn by its name is at the given height.
inline float PadLabelWidth(ImFont* font, float height, const char* text)
{
  return button_glyphs_detail::BoxWidth(font, height, text);
}

// Draws a pad button that has no picture of its own (a shoulder, Start, a
// trigger...): a grey rounded box with its name, with its top left corner at
// min. Returns its width.
inline float DrawPadLabel(ImDrawList* list, ImFont* font, ImVec2 min, float height,
                          const char* text, float alpha = 1.0f)
{
  using namespace button_glyphs_detail;
  const float width = BoxWidth(font, height, text);
  const ImVec2 max(min.x + width, min.y + height);
  list->AddRectFilled(min, max, Colour(0.30f, 0.30f, 0.33f, alpha), height * 0.3f);
  CentredText(list, font, height * 0.58f, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
              Colour(0.96f, 0.96f, 0.96f, alpha), text);
  return width;
}

// Draws a button with its top left corner at min, `height` tall. alpha fades
// it. Returns its width.
inline float DrawPadButton(ImDrawList* list, ImFont* font, ImVec2 min, float height,
                           PadStyle style, PadButton button, float alpha = 1.0f)
{
  using namespace button_glyphs_detail;
  const char* label = Label(style, button);
  if (!IsFace(button))
    return DrawPadLabel(list, font, min, height, label, alpha);

  const float radius = height * 0.5f;
  const ImVec2 centre(min.x + radius, min.y + radius);
  if (style == PadStyle::PlayStation)
  {
    // A dark button with its symbol in the symbol's colour.
    list->AddCircleFilled(centre, radius, Colour(0.17f, 0.17f, 0.19f, alpha), 24);
    const float r = radius * 0.46f;
    const float thickness = std::max(1.5f, height * 0.09f);
    switch (button)
    {
    case PadButton::South:
    {
      const ImU32 blue = Colour(0.50f, 0.68f, 1.0f, alpha);
      list->AddLine(ImVec2(centre.x - r, centre.y - r), ImVec2(centre.x + r, centre.y + r), blue,
                    thickness);
      list->AddLine(ImVec2(centre.x - r, centre.y + r), ImVec2(centre.x + r, centre.y - r), blue,
                    thickness);
      break;
    }
    case PadButton::East:
      list->AddCircle(centre, r * 1.05f, Colour(1.0f, 0.42f, 0.42f, alpha), 20, thickness);
      break;
    case PadButton::West:
      list->AddRect(ImVec2(centre.x - r * 0.9f, centre.y - r * 0.9f),
                    ImVec2(centre.x + r * 0.9f, centre.y + r * 0.9f),
                    Colour(0.93f, 0.56f, 0.85f, alpha), 0.0f, 0, thickness);
      break;
    default:
      list->AddTriangle(ImVec2(centre.x, centre.y - r * 1.05f),
                        ImVec2(centre.x + r * 1.05f, centre.y + r * 0.75f),
                        ImVec2(centre.x - r * 1.05f, centre.y + r * 0.75f),
                        Colour(0.36f, 0.86f, 0.70f, alpha), thickness);
      break;
    }
    return height;
  }

  ImU32 fill = Colour(0.30f, 0.30f, 0.33f, alpha);
  ImU32 letter = Colour(0.96f, 0.96f, 0.96f, alpha);
  if (style == PadStyle::Xbox)
  {
    // The colours Xbox prints its letters in.
    switch (button)
    {
    case PadButton::South:
      fill = Colour(0.16f, 0.72f, 0.45f, alpha);
      letter = Colour(0.03f, 0.08f, 0.05f, alpha);
      break;
    case PadButton::East:
      fill = Colour(0.86f, 0.25f, 0.24f, alpha);
      break;
    case PadButton::West:
      fill = Colour(0.22f, 0.47f, 0.92f, alpha);
      break;
    default:
      fill = Colour(0.96f, 0.76f, 0.18f, alpha);
      letter = Colour(0.10f, 0.08f, 0.02f, alpha);
      break;
    }
  }
  list->AddCircleFilled(centre, radius, fill, 24);
  CentredText(list, font, height * 0.62f, centre, letter, label);
  return height;
}

// How wide a key's picture is at the given height.
inline float KeyCapWidth(ImFont* font, float height, const std::string& name)
{
  return button_glyphs_detail::BoxWidth(font, height, name.c_str());
}

// Draws a keyboard key, a key cap with its name ("S", "Esc"), with its top
// left corner at min. Returns its width.
inline float DrawKeyCap(ImDrawList* list, ImFont* font, ImVec2 min, float height,
                        const std::string& name, float alpha = 1.0f)
{
  using namespace button_glyphs_detail;
  const float width = BoxWidth(font, height, name.c_str());
  const ImVec2 max(min.x + width, min.y + height);
  const float rounding = height * 0.22f;
  list->AddRectFilled(min, max, Colour(0.13f, 0.13f, 0.15f, alpha), rounding);
  // An outline lighter than the key, so it reads as a key.
  list->AddRect(min, max, Colour(0.62f, 0.62f, 0.66f, alpha), rounding, 0,
                std::max(1.0f, height * 0.06f));
  CentredText(list, font, height * 0.58f, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
              Colour(0.96f, 0.96f, 0.96f, alpha), name.c_str());
  return width;
}

// Draws an arrow key (left or right), a square key cap with the arrow drawn
// in it, so no font needs the arrow. Returns its width.
inline float DrawArrowKeyCap(ImDrawList* list, ImVec2 min, float height, bool left,
                             float alpha = 1.0f)
{
  using namespace button_glyphs_detail;
  const ImVec2 max(min.x + height, min.y + height);
  const float rounding = height * 0.22f;
  list->AddRectFilled(min, max, Colour(0.13f, 0.13f, 0.15f, alpha), rounding);
  list->AddRect(min, max, Colour(0.62f, 0.62f, 0.66f, alpha), rounding, 0,
                std::max(1.0f, height * 0.06f));
  const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const float r = height * 0.2f;
  const ImU32 white = Colour(0.96f, 0.96f, 0.96f, alpha);
  if (left)
    list->AddTriangleFilled(ImVec2(c.x - r, c.y), ImVec2(c.x + r * 0.8f, c.y - r),
                            ImVec2(c.x + r * 0.8f, c.y + r), white);
  else
    list->AddTriangleFilled(ImVec2(c.x + r, c.y), ImVec2(c.x - r * 0.8f, c.y + r),
                            ImVec2(c.x - r * 0.8f, c.y - r), white);
  return height;
}

// What the player plays with, as a small picture: a gamepad in its maker's
// colours (Xbox, PlayStation, Nintendo, or any other), or a keyboard.
enum class DeviceIcon
{
  Xbox,
  PlayStation,
  Nintendo,
  Gamepad,
  Keyboard,
};

inline float DeviceIconWidth(float height, DeviceIcon icon)
{
  return std::floor(height * (icon == DeviceIcon::Keyboard ? 1.7f : 1.6f));
}

// Draws the picture with its top left corner at min, `height` tall. Returns
// its width.
inline float DrawDeviceIcon(ImDrawList* list, ImVec2 min, float height, DeviceIcon icon,
                            float alpha = 1.0f)
{
  using namespace button_glyphs_detail;
  const float w = DeviceIconWidth(height, icon);
  const float h = height;
  const ImU32 body = Colour(0.86f, 0.87f, 0.90f, alpha);
  const ImU32 dark = Colour(0.12f, 0.12f, 0.14f, alpha);
  if (icon == DeviceIcon::Keyboard)
  {
    // A keyboard: the case, two rows of keys and the space bar.
    const ImVec2 a(min.x + w * 0.02f, min.y + h * 0.18f);
    const ImVec2 b(min.x + w * 0.98f, min.y + h * 0.86f);
    list->AddRectFilled(a, b, body, h * 0.14f);
    const float key = h * 0.13f;
    const float gap = (b.x - a.x - key * 6.0f) / 7.0f;
    for (int row = 0; row < 2; ++row)
    {
      const float y = a.y + h * 0.1f + row * (key + h * 0.06f);
      for (int i = 0; i < 6; ++i)
      {
        const float x = a.x + gap + i * (key + gap) + (row == 1 ? gap * 0.5f : 0.0f);
        if (row == 1 && i == 5)
          continue;
        list->AddRectFilled(ImVec2(x, y), ImVec2(x + key, y + key), dark, key * 0.25f);
      }
    }
    const float bar_y = b.y - h * 0.1f - key;
    list->AddRectFilled(ImVec2(a.x + (b.x - a.x) * 0.28f, bar_y),
                        ImVec2(a.x + (b.x - a.x) * 0.72f, bar_y + key), dark, key * 0.25f);
    return w;
  }

  // A gamepad, laid out like its maker's: the body and two grips, the sticks,
  // the D-pad and the four face buttons where that pad has them.
  //   Xbox: the left stick high and the D-pad low, the face buttons in their
  //         four colours and the Xbox button in green.
  //   PlayStation: the D-pad and the face buttons high, both sticks low in
  //         the middle, the touchpad between them with its blue light, the
  //         face buttons' shapes once there is room for them.
  //   Nintendo: the Joy-Con grip, a blue left and a red right on a dark body.
  //   Any other: the PlayStation layout, all in one colour.
  const auto at = [&](float x, float y) { return ImVec2(min.x + w * x, min.y + h * y); };
  const bool joycon = icon == DeviceIcon::Nintendo;
  const ImU32 centre = joycon ? Colour(0.40f, 0.41f, 0.45f, alpha) : body;
  const ImU32 left_grip = joycon ? Colour(0.10f, 0.72f, 0.95f, alpha) : body;
  const ImU32 right_grip = joycon ? Colour(1.0f, 0.33f, 0.30f, alpha) : body;
  const ImU32 part = joycon ? Colour(0.08f, 0.08f, 0.10f, alpha) : dark;
  // Each grip runs from the body down and out, a stroke with round ends.
  const auto grip = [&](ImVec2 from, ImVec2 to, ImU32 colour) {
    const float radius = h * 0.18f;
    list->AddLine(from, to, colour, radius * 2.0f);
    list->AddCircleFilled(from, radius, colour, 16);
    list->AddCircleFilled(to, radius, colour, 16);
  };
  grip(at(0.30f, 0.42f), at(0.19f, 0.78f), left_grip);
  grip(at(0.70f, 0.42f), at(0.81f, 0.78f), right_grip);
  if (joycon)
  {
    // The two Joy-Con on the grip's sides, the dark grip between them.
    list->AddRectFilled(at(0.12f, 0.10f), at(0.36f, 0.64f), left_grip, h * 0.2f);
    list->AddRectFilled(at(0.64f, 0.10f), at(0.88f, 0.64f), right_grip, h * 0.2f);
    list->AddRectFilled(at(0.32f, 0.10f), at(0.68f, 0.62f), centre, h * 0.06f);
  }
  else
  {
    list->AddRectFilled(at(0.12f, 0.10f), at(0.88f, 0.64f), body, h * 0.24f);
  }

  const auto stick = [&](ImVec2 c) { list->AddCircleFilled(c, h * 0.085f, part, 14); };
  const auto dpad = [&](ImVec2 c) {
    const float arm = h * 0.095f;
    const float half = h * 0.032f;
    list->AddRectFilled(ImVec2(c.x - arm, c.y - half), ImVec2(c.x + arm, c.y + half), part);
    list->AddRectFilled(ImVec2(c.x - half, c.y - arm), ImVec2(c.x + half, c.y + arm), part);
  };
  if (joycon)
  {
    // The left Joy-Con's stick above its buttons; the right one's stick below.
    stick(at(0.24f, 0.29f));
    dpad(at(0.24f, 0.50f));
    stick(at(0.76f, 0.52f));
  }
  else if (icon == DeviceIcon::Xbox)
  {
    stick(at(0.26f, 0.31f));
    dpad(at(0.37f, 0.50f));
    stick(at(0.62f, 0.50f));
  }
  else
  {
    dpad(at(0.26f, 0.32f));
    stick(at(0.39f, 0.51f));
    stick(at(0.61f, 0.51f));
  }

  ImU32 north = part, east = part, south = part, west = part;
  if (icon == DeviceIcon::Xbox)
  {
    north = Colour(0.96f, 0.76f, 0.18f, alpha);
    east = Colour(0.90f, 0.26f, 0.24f, alpha);
    south = Colour(0.16f, 0.72f, 0.45f, alpha);
    west = Colour(0.22f, 0.47f, 0.92f, alpha);
    // The Xbox button, in its green, a little bigger than the others.
    list->AddCircleFilled(at(0.5f, 0.27f), h * 0.075f, Colour(0.16f, 0.72f, 0.45f, alpha), 14);
  }
  else if (icon == DeviceIcon::PlayStation)
  {
    north = Colour(0.20f, 0.72f, 0.58f, alpha);
    east = Colour(0.90f, 0.28f, 0.30f, alpha);
    south = Colour(0.30f, 0.50f, 0.95f, alpha);
    west = Colour(0.85f, 0.40f, 0.75f, alpha);
    // The touchpad, with the light bar's blue along its top.
    list->AddRectFilled(at(0.39f, 0.10f), at(0.61f, 0.36f), Colour(0.30f, 0.31f, 0.35f, alpha),
                        h * 0.05f);
    list->AddRectFilled(at(0.39f, 0.10f), ImVec2(min.x + w * 0.61f, min.y + h * 0.10f +
                                                                       std::max(1.0f, h * 0.05f)),
                        Colour(0.30f, 0.62f, 1.0f, alpha), h * 0.02f);
  }

  const ImVec2 face = joycon ? at(0.76f, 0.28f) : at(0.74f, 0.32f);
  const float step = joycon ? h * 0.095f : h * 0.11f;
  const float dot = h * 0.055f;
  if (icon == DeviceIcon::PlayStation && h >= 28.0f)
  {
    // Big enough for the shapes: the triangle, the circle, the cross and the
    // square, each in its colour.
    const float r = h * 0.05f;
    const float line = std::max(1.0f, h * 0.022f);
    list->AddCircleFilled(ImVec2(face.x, face.y - step), dot * 1.25f, dark, 12);
    list->AddCircleFilled(ImVec2(face.x + step, face.y), dot * 1.25f, dark, 12);
    list->AddCircleFilled(ImVec2(face.x, face.y + step), dot * 1.25f, dark, 12);
    list->AddCircleFilled(ImVec2(face.x - step, face.y), dot * 1.25f, dark, 12);
    const ImVec2 n(face.x, face.y - step);
    list->AddTriangle(ImVec2(n.x, n.y - r), ImVec2(n.x + r * 0.9f, n.y + r * 0.6f),
                      ImVec2(n.x - r * 0.9f, n.y + r * 0.6f), north, line);
    list->AddCircle(ImVec2(face.x + step, face.y), r * 0.85f, east, 12, line);
    const ImVec2 sc(face.x, face.y + step);
    list->AddLine(ImVec2(sc.x - r * 0.8f, sc.y - r * 0.8f), ImVec2(sc.x + r * 0.8f, sc.y + r * 0.8f),
                  south, line);
    list->AddLine(ImVec2(sc.x + r * 0.8f, sc.y - r * 0.8f), ImVec2(sc.x - r * 0.8f, sc.y + r * 0.8f),
                  south, line);
    const ImVec2 wc(face.x - step, face.y);
    list->AddRect(ImVec2(wc.x - r * 0.75f, wc.y - r * 0.75f), ImVec2(wc.x + r * 0.75f, wc.y + r * 0.75f),
                  west, 0.0f, 0, line);
    return w;
  }
  list->AddCircleFilled(ImVec2(face.x, face.y - step), dot, north, 10);
  list->AddCircleFilled(ImVec2(face.x + step, face.y), dot, east, 10);
  list->AddCircleFilled(ImVec2(face.x, face.y + step), dot, south, 10);
  list->AddCircleFilled(ImVec2(face.x - step, face.y), dot, west, 10);
  return w;
}
}  // namespace moderngekko::frontend::ui
