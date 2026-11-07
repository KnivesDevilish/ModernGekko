#pragma once

// Pictures of a controller's buttons and of keyboard keys, for the hints on
// screen: the launcher's footer while a gamepad is in use, and the in-game
// menu's. A button looks the way the controller prints it: Xbox's coloured
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
}  // namespace moderngekko::frontend::ui
