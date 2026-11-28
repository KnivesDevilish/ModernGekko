#pragma once

// The launcher's arcade look, over the title scene (launcher_title.hpp): the
// bar of pages along the bottom, its tabs leaning like the logo's letters;
// the PLAY pill and the hints under it on the PLAY page; the pages' title
// badges and their dark glass cards. Drawn with Dear ImGui's draw list, apart
// from SDL, so tools can draw it too.

#include "button_glyphs.hpp"
#include "launcher_look.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend::ui::arcade
{
// The scene's dark ink and the accent's lime.
inline constexpr ImVec4 INK(0.047f, 0.102f, 0.114f, 1.0f);
inline constexpr ImVec4 LIME = LAUNCHER_ACCENT;
// The glass of the cards and of the bar.
inline constexpr ImVec4 GLASS(0.024f, 0.063f, 0.071f, 0.90f);
inline constexpr ImVec4 TAB_TEXT(0.85f, 0.95f, 0.95f, 1.0f);

inline ImU32 Colour(ImVec4 colour, float alpha = 1.0f)
{
  colour.w *= alpha;
  return ImGui::ColorConvertFloat4ToU32(colour);
}

// A label in capitals, as the tabs and badges show it: ASCII and the accented
// Latin letters (á to Á...); other scripts (Japanese) as they are.
inline std::string Capitals(std::string_view text)
{
  std::string out(text);
  for (std::size_t i = 0; i < out.size(); ++i)
  {
    const unsigned char c = static_cast<unsigned char>(out[i]);
    if (c >= 'a' && c <= 'z')
    {
      out[i] = static_cast<char>(c - 'a' + 'A');
    }
    else if (c == 0xC3 && i + 1 < out.size())
    {
      // UTF-8 for U+00E0..U+00FE, except U+00F7 (division sign).
      const unsigned char next = static_cast<unsigned char>(out[i + 1]);
      if (next >= 0xA0 && next <= 0xBE && next != 0xB7)
        out[i + 1] = static_cast<char>(next - 0x20);
      ++i;
    }
  }
  return out;
}

// Text leaning right, like the logo's letters: each point moves right by
// `lean` times its height above the line's bottom.
inline void AddLeaningText(ImDrawList* list, ImFont* font, float size, ImVec2 position,
                           ImU32 colour, const char* text, float lean = 0.18f)
{
  const int first = list->VtxBuffer.Size;
  list->AddText(font, size, position, colour, text);
  const float bottom = position.y + size;
  for (int i = first; i < list->VtxBuffer.Size; ++i)
    list->VtxBuffer[i].pos.x += (bottom - list->VtxBuffer[i].pos.y) * lean;
}

inline ImVec2 TextSize(ImFont* font, float size, const char* text)
{
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

// A band leaning like the text, from min to max.
inline void AddLeaningBand(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 colour,
                           float lean = 0.18f)
{
  const float shift = (max.y - min.y) * lean * 0.5f;
  list->AddQuadFilled(ImVec2(min.x + shift, min.y), ImVec2(max.x + shift, min.y),
                      ImVec2(max.x - shift, max.y), ImVec2(min.x - shift, max.y), colour);
}

inline void AddLeaningOutline(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 colour,
                              float thickness, float lean = 0.18f)
{
  const float shift = (max.y - min.y) * lean * 0.5f;
  list->AddQuad(ImVec2(min.x + shift, min.y), ImVec2(max.x + shift, min.y),
                ImVec2(max.x - shift, max.y), ImVec2(min.x - shift, max.y), colour, thickness);
}

// The bar's glass, from min to max, under its lime line.
inline void DrawBar(ImDrawList* list, ImVec2 min, ImVec2 max, float scale)
{
  list->AddRectFilledMultiColor(min, max, Colour(GLASS, 0.88f), Colour(GLASS, 0.88f),
                                Colour(GLASS, 1.05f), Colour(GLASS, 1.05f));
  const float line = std::max(2.0f, 3.0f * scale);
  list->AddRectFilledMultiColor(ImVec2(min.x, min.y + line), ImVec2(max.x, min.y + line * 4.0f),
                                Colour(LIME, 0.22f), Colour(LIME, 0.22f), Colour(LIME, 0.0f),
                                Colour(LIME, 0.0f));
  list->AddRectFilled(min, ImVec2(max.x, min.y + line), Colour(LIME));
}

// How wide a tab is with its label at this size.
inline float TabWidth(ImFont* bold, float size, const char* label, float scale)
{
  return TextSize(bold, size, Capitals(label).c_str()).x + 26.0f * scale;
}

// One page of the bar, at the cursor, `height` tall: its name in capitals,
// leaning; the page shown on a glowing lime band. Returns true when clicked
// (or chosen with Enter or A).
inline bool Tab(const char* id, const char* label, bool selected, ImFont* bold, float size,
                float height, float scale)
{
  const std::string text = Capitals(label);
  const ImVec2 text_size = TextSize(bold, size, text.c_str());
  const ImVec2 box(text_size.x + 26.0f * scale, height);
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(id, box, ImGuiButtonFlags_EnableNav);
  const bool hovered = ImGui::IsItemHovered();
  const bool focused = ImGui::IsItemFocused();
  ImDrawList* list = ImGui::GetWindowDrawList();
  const ImVec2 max(min.x + box.x, min.y + box.y);
  const ImVec2 band_min(min.x + 3.0f * scale, min.y + height * 0.14f);
  const ImVec2 band_max(max.x - 3.0f * scale, max.y - height * 0.14f);
  if (selected)
  {
    for (int glow = 3; glow >= 1; --glow)
    {
      const float spread = static_cast<float>(glow) * 3.0f * scale;
      AddLeaningBand(list, ImVec2(band_min.x - spread, band_min.y - spread),
                     ImVec2(band_max.x + spread, band_max.y + spread), Colour(LIME, 0.10f));
    }
    AddLeaningBand(list, band_min, band_max, Colour(LIME));
  }
  else if (hovered)
  {
    AddLeaningBand(list, band_min, band_max, Colour(LIME, 0.16f));
  }
  if (focused)
    AddLeaningOutline(list, ImVec2(band_min.x - 2.0f * scale, band_min.y - 2.0f * scale),
                      ImVec2(band_max.x + 2.0f * scale, band_max.y + 2.0f * scale),
                      Colour(selected ? ImVec4(1, 1, 1, 1) : LIME), 2.0f * scale);
  const ImVec2 at(min.x + (box.x - text_size.x) * 0.5f - size * 0.06f,
                  min.y + (box.y - text_size.y) * 0.5f);
  AddLeaningText(list, bold, size, at,
                 selected ? Colour(INK) : Colour(hovered ? ImVec4(1, 1, 1, 1) : TAB_TEXT), text.c_str());
  return pressed;
}

// The PLAY pill: the A button (or Enter on the keyboard) and the label, on a
// dark pill, at the cursor; `height` tall. Its width is PlayPillWidth.
inline float PlayPillWidth(ImFont* bold, float height, const char* label, bool keyboard)
{
  const float glyph = height * 0.62f;
  const float glyph_width =
      keyboard ? KeyCapWidth(bold, glyph, "Enter") : glyph;
  const float text = TextSize(bold, height * 0.52f, Capitals(label).c_str()).x;
  return height * 0.2f + glyph_width + height * 0.28f + text + height * 0.45f;
}

inline bool PlayPill(const char* id, const char* label, ImFont* bold, float height,
                     PadStyle style, bool keyboard)
{
  const float width = PlayPillWidth(bold, height, label, keyboard);
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height), ImGuiButtonFlags_EnableNav);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  const bool focused = ImGui::IsItemFocused();
  ImDrawList* list = ImGui::GetWindowDrawList();
  const ImVec2 max(min.x + width, min.y + height);
  const float round = height * 0.5f;
  // A soft shadow under it, so it stands out of the light scene.
  list->AddRectFilled(ImVec2(min.x + 2.0f, min.y + height * 0.12f),
                      ImVec2(max.x + 2.0f, max.y + height * 0.12f),
                      Colour(ImVec4(0.0f, 0.16f, 0.16f, 1.0f), 0.22f), round);
  if (hovered || focused)
  {
    for (int glow = 3; glow >= 1; --glow)
    {
      const float spread = static_cast<float>(glow) * height * 0.07f;
      list->AddRectFilled(ImVec2(min.x - spread, min.y - spread),
                          ImVec2(max.x + spread, max.y + spread), Colour(LIME, 0.12f),
                          round + spread);
    }
  }
  list->AddRectFilled(min, max, Colour(held ? ImVec4(0.10f, 0.20f, 0.22f, 1.0f) : INK, 0.92f),
                      round);
  if (focused)
    list->AddRect(min, max, Colour(LIME), round, 0, std::max(2.0f, height * 0.05f));
  const float glyph = height * 0.62f;
  const ImVec2 glyph_at(min.x + height * 0.2f, min.y + (height - glyph) * 0.5f);
  const float glyph_width =
      keyboard ? DrawKeyCap(list, bold, glyph_at, glyph, "Enter")
               : DrawPadButton(list, bold, glyph_at, glyph, style, PadButton::South);
  const std::string text = Capitals(label);
  const float text_size = height * 0.52f;
  const ImVec2 text_extent = TextSize(bold, text_size, text.c_str());
  AddLeaningText(list, bold, text_size,
                 ImVec2(glyph_at.x + glyph_width + height * 0.28f,
                        min.y + (height - text_extent.y) * 0.5f),
                 Colour(ImVec4(1, 1, 1, 1)), text.c_str(), 0.14f);
  return pressed;
}

// One hint under PLAY, at the cursor: a button's picture (or a key's, or
// none) and what it does, in the scene's dark ink. Returns true when clicked
// or chosen. Its width is HintWidth.
struct Hint
{
  PadButton button = PadButton::South;
  const char* key = nullptr;  // on the keyboard: its key, or none
  const char* text = "";
};

inline float HintWidth(const Hint& hint, ImFont* bold, float height, bool keyboard)
{
  const float glyph = height * 0.82f;
  float width = TextSize(bold, height * 0.72f, hint.text).x + height * 0.3f;
  if (!keyboard)
    width += glyph + height * 0.3f;
  else if (hint.key)
    width += KeyCapWidth(bold, glyph, hint.key) + height * 0.3f;
  return width;
}

inline bool HintButton(const char* id, const Hint& hint, ImFont* bold, float height,
                       PadStyle style, bool keyboard)
{
  const float width = HintWidth(hint, bold, height, keyboard);
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height), ImGuiButtonFlags_EnableNav);
  const bool hovered = ImGui::IsItemHovered();
  const bool focused = ImGui::IsItemFocused();
  ImDrawList* list = ImGui::GetWindowDrawList();
  const ImVec2 max(min.x + width, min.y + height);
  if (hovered || focused)
    list->AddRectFilled(ImVec2(min.x - height * 0.15f, min.y), ImVec2(max.x, max.y),
                        Colour(LIME, focused ? 0.75f : 0.45f), height * 0.25f);
  const float glyph = height * 0.82f;
  float x = min.x;
  const float glyph_y = min.y + (height - glyph) * 0.5f;
  if (!keyboard)
    x += DrawPadButton(list, bold, ImVec2(x, glyph_y), glyph, style, hint.button) + height * 0.3f;
  else if (hint.key)
    x += DrawKeyCap(list, bold, ImVec2(x, glyph_y), glyph, hint.key) + height * 0.3f;
  const float text_size = height * 0.72f;
  list->AddText(bold, text_size, ImVec2(x, min.y + (height - text_size) * 0.5f), Colour(INK),
                hint.text);
  return pressed;
}

// The tabs of the bar, centred between left and right (screen x) on the line
// at top, `height` tall, with LB and RB at their ends (Q and E on the
// keyboard). Their text shrinks to fit. focus_selected: the page shown takes
// the keyboard and gamepad focus. Returns the tab clicked or chosen, or -1;
// focused: whether a tab has the focus.
inline int Tabs(const std::vector<const char*>& labels, int selected, ImFont* bold, float left,
                float right, float top, float height, float scale, PadStyle style, bool keyboard,
                bool focus_selected, bool* focused)
{
  ImDrawList* list = ImGui::GetWindowDrawList();
  const float glyph = height * 0.5f;
  const float ends_gap = 12.0f * scale;
  const float left_glyph = keyboard ? KeyCapWidth(bold, glyph, "Q")
                                    : PadButtonWidth(bold, glyph, style, PadButton::LeftShoulder);
  const float right_glyph = keyboard ? KeyCapWidth(bold, glyph, "E")
                                     : PadButtonWidth(bold, glyph, style, PadButton::RightShoulder);
  float size = height * 0.56f;
  const float room = std::max(1.0f, right - left - left_glyph - right_glyph - ends_gap * 2.0f);
  float tabs_width = 0.0f;
  for (const char* label : labels)
    tabs_width += TabWidth(bold, size, label, scale);
  if (tabs_width > room)
  {
    // Smaller text, down to 70%; the padding stays.
    const float padding = 26.0f * scale * static_cast<float>(labels.size());
    const float fit = std::max(0.7f, (room - padding) / std::max(1.0f, tabs_width - padding));
    size *= fit;
    tabs_width = 0.0f;
    for (const char* label : labels)
      tabs_width += TabWidth(bold, size, label, scale);
  }
  const float total = left_glyph + ends_gap + tabs_width + ends_gap + right_glyph;
  float x = std::max(left, (left + right - total) * 0.5f);
  const float glyph_y = top + (height - glyph) * 0.5f;
  if (keyboard)
    DrawKeyCap(list, bold, ImVec2(x, glyph_y), glyph, "Q");
  else
    DrawPadButton(list, bold, ImVec2(x, glyph_y), glyph, style, PadButton::LeftShoulder);
  x += left_glyph + ends_gap;
  int clicked = -1;
  bool any_focused = false;
  for (std::size_t i = 0; i < labels.size(); ++i)
  {
    ImGui::SetCursorScreenPos(ImVec2(x, top));
    ImGui::PushID(static_cast<int>(i));
    const bool is_selected = static_cast<int>(i) == selected;
    if (Tab("##tab", labels[i], is_selected, bold, size, height, scale))
      clicked = static_cast<int>(i);
    if (is_selected && focus_selected)
    {
      ImGui::FocusItem();
      ImGui::SetNavCursorVisible(true);
    }
    any_focused = any_focused || ImGui::IsItemFocused();
    ImGui::PopID();
    x += TabWidth(bold, size, labels[i], scale);
  }
  x += ends_gap;
  if (keyboard)
    DrawKeyCap(list, bold, ImVec2(x, glyph_y), glyph, "E");
  else
    DrawPadButton(list, bold, ImVec2(x, glyph_y), glyph, style, PadButton::RightShoulder);
  if (focused)
    *focused = any_focused;
  return clicked;
}

// The hints under PLAY, centred on centre_x on the line at top, `height`
// tall, with a dot between them. Returns the hint clicked or chosen, or -1.
inline int HintRow(const std::vector<Hint>& hints, ImFont* bold, float centre_x, float top,
                   float height, PadStyle style, bool keyboard)
{
  const char* dot = "\xC2\xB7";
  const float dot_width = TextSize(bold, height * 0.72f, dot).x + height * 0.8f;
  float total = 0.0f;
  for (std::size_t i = 0; i < hints.size(); ++i)
    total += HintWidth(hints[i], bold, height, keyboard) + (i > 0 ? dot_width : 0.0f);
  float x = centre_x - total * 0.5f;
  int clicked = -1;
  ImDrawList* list = ImGui::GetWindowDrawList();
  for (std::size_t i = 0; i < hints.size(); ++i)
  {
    if (i > 0)
    {
      list->AddText(bold, height * 0.72f, ImVec2(x + height * 0.4f, top + height * 0.14f),
                    Colour(INK, 0.7f), dot);
      x += dot_width;
    }
    ImGui::SetCursorScreenPos(ImVec2(x, top));
    ImGui::PushID(static_cast<int>(i));
    if (HintButton("##hint", hints[i], bold, height, style, keyboard))
      clicked = static_cast<int>(i);
    ImGui::PopID();
    x += HintWidth(hints[i], bold, height, keyboard);
  }
  return clicked;
}

// A page's title: a lime badge with its name in capitals, leaning, and the
// line under it; takes its room at the cursor.
inline void TitleBadge(const char* title, ImFont* bold, float size, float scale)
{
  const std::string text = Capitals(title);
  const ImVec2 text_size = TextSize(bold, size, text.c_str());
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 box(text_size.x + 30.0f * scale, text_size.y + 8.0f * scale);
  ImDrawList* list = ImGui::GetWindowDrawList();
  AddLeaningBand(list, ImVec2(min.x + 6.0f * scale, min.y), ImVec2(min.x + box.x, min.y + box.y),
                 Colour(LIME));
  AddLeaningText(list, bold, size,
                 ImVec2(min.x + 15.0f * scale, min.y + (box.y - text_size.y) * 0.5f),
                 Colour(INK), text.c_str());
  ImGui::Dummy(box);
}

// Where a point of the title scene's picture (0 to 1 across and down) is in
// a window of this size, with the picture covering it, centred (the scene at
// rest: launcher_title_layout.hpp).
inline ImVec2 ScenePoint(ImVec2 point, ImVec2 window, float picture_aspect)
{
  if (picture_aspect <= 0.0f || window.x <= 0.0f || window.y <= 0.0f)
    return ImVec2(window.x * point.x, window.y * point.y);
  const float width = std::max(window.x, window.y / picture_aspect);
  const float height = width * picture_aspect;
  return ImVec2((window.x - width) * 0.5f + point.x * width,
                (window.y - height) * 0.5f + point.y * height);
}
}  // namespace moderngekko::frontend::ui::arcade
