#pragma once

// The launcher's look over the title scene (launcher_title.hpp): frosted
// glass with the scene blurred behind it, in the logo's greens; the carousel
// of pages at the bottom; the LAUNCH pill on the PLAY page; the pages' titles
// and their sub-pages (LT and RT) instead of scrolling; switches and blocks in
// place of check boxes and slider bars; and the old-TV switch-on of a page.
// Drawn with Dear ImGui's draw list, apart from SDL, so tools can draw it too.

#include "button_glyphs.hpp"
#include "launcher_look.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend::ui::arcade
{
// The logo's colours: its green-yellow, its green and its dark green; white.
inline constexpr ImVec4 CHART = LAUNCHER_ACCENT;
inline constexpr ImVec4 GREEN(0.31f, 0.60f, 0.14f, 1.0f);
inline constexpr ImVec4 DEEP(0.02f, 0.10f, 0.04f, 1.0f);
inline constexpr ImVec4 WHITE(1.0f, 1.0f, 1.0f, 1.0f);
inline constexpr ImVec4 DIM(0.74f, 0.84f, 0.76f, 1.0f);

inline ImU32 Colour(ImVec4 colour, float alpha = 1.0f)
{
  colour.w *= alpha;
  return ImGui::ColorConvertFloat4ToU32(colour);
}

// What the glass shows through: the scene, blurred (TitleScene::Blurred), and
// the window's size; without it the glass is a plain dark tint.
struct Backdrop
{
  ImTextureID blurred = ImTextureID_Invalid;
  ImVec2 origin{0.0f, 0.0f};
  ImVec2 size{0.0f, 0.0f};
};

inline Backdrop& GlassBackdrop()
{
  static Backdrop backdrop;
  return backdrop;
}

// A label in capitals, as the carousel and titles show it: ASCII and the
// accented Latin letters (á to Á...); other scripts (Japanese) as they are.
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

inline ImVec2 TextSize(ImFont* font, float size, const char* text)
{
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

// Text leaning right, like the logo's letters: each point moves right by
// `lean` times its height above the line's bottom.
inline void AddLeaningText(ImDrawList* list, ImFont* font, float size, ImVec2 position,
                           ImU32 colour, const char* text, float lean = 0.16f)
{
  const int first = list->VtxBuffer.Size;
  list->AddText(font, size, position, colour, text);
  const float bottom = position.y + size;
  for (int i = first; i < list->VtxBuffer.Size; ++i)
    list->VtxBuffer[i].pos.x += (bottom - list->VtxBuffer[i].pos.y) * lean;
}

// A band leaning like the text, from min to max.
inline void AddLeaningBand(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 colour,
                           float lean = 0.3f)
{
  const float shift = (max.y - min.y) * lean * 0.5f;
  list->AddQuadFilled(ImVec2(min.x + shift, min.y), ImVec2(max.x + shift, min.y),
                      ImVec2(max.x - shift, max.y), ImVec2(min.x - shift, max.y), colour);
}

// The colour of the vertices drawn since `first`, from `top` at y0 to
// `bottom` at y1.
inline void ShadeY(ImDrawList* list, int first, float y0, float y1, ImVec4 top, ImVec4 bottom)
{
  for (int i = first; i < list->VtxBuffer.Size; ++i)
  {
    ImDrawVert& vertex = list->VtxBuffer[i];
    const float t = std::clamp((vertex.pos.y - y0) / std::max(1.0f, y1 - y0), 0.0f, 1.0f);
    vertex.col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(top.x + (bottom.x - top.x) * t, top.y + (bottom.y - top.y) * t,
               top.z + (bottom.z - top.z) * t, top.w + (bottom.w - top.w) * t));
  }
}

// A piece of frosted glass from a to b: a soft shadow under it, the scene
// blurred through it, tinted dark green (`tint`: how dark), a sheen on its
// upper half, light along its top edge and a darker line along its bottom.
inline void Glass(ImDrawList* list, ImVec2 a, ImVec2 b, float rounding, float scale,
                  float tint = 0.46f)
{
  for (int i = 5; i >= 1; --i)
  {
    const float spread = static_cast<float>(i) * 3.2f * scale;
    list->AddRectFilled(ImVec2(a.x - spread * 0.4f, a.y + spread * 0.8f),
                        ImVec2(b.x + spread * 0.4f, b.y + spread * 1.4f), IM_COL32(0, 12, 4, 14),
                        rounding + spread);
  }
  const Backdrop& backdrop = GlassBackdrop();
  const bool blurred = backdrop.blurred != ImTextureID_Invalid && backdrop.size.x > 0.0f &&
                       backdrop.size.y > 0.0f;
  if (blurred)
  {
    const ImVec2 uv0((a.x - backdrop.origin.x) / backdrop.size.x,
                     (a.y - backdrop.origin.y) / backdrop.size.y);
    const ImVec2 uv1((b.x - backdrop.origin.x) / backdrop.size.x,
                     (b.y - backdrop.origin.y) / backdrop.size.y);
    list->AddImageRounded(ImTextureRef(backdrop.blurred), a, b, uv0, uv1, IM_COL32_WHITE,
                          rounding);
  }
  // Without the blurred scene behind it, darker, so text still reads.
  const float darkness = blurred ? tint : std::min(0.9f, tint + 0.35f);
  int first = list->VtxBuffer.Size;
  list->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
  ShadeY(list, first, a.y, b.y, ImVec4(0.03f, 0.10f, 0.05f, darkness - 0.06f),
         ImVec4(0.01f, 0.04f, 0.02f, std::min(1.0f, darkness + 0.10f)));
  first = list->VtxBuffer.Size;
  const float middle = a.y + (b.y - a.y) * 0.5f;
  list->AddRectFilled(a, ImVec2(b.x, middle), IM_COL32_WHITE, rounding,
                      ImDrawFlags_RoundCornersTop);
  ShadeY(list, first, a.y, middle, ImVec4(1.0f, 1.0f, 1.0f, 0.10f),
         ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
  list->AddRect(a, b, Colour(WHITE, 0.12f), rounding, 0, 1.0f);
  const float inset = std::min(rounding * 0.8f, (b.x - a.x) * 0.5f);
  list->AddLine(ImVec2(a.x + inset, a.y + 0.5f), ImVec2(b.x - inset, a.y + 0.5f),
                Colour(WHITE, 0.45f), 1.2f);
  list->AddLine(ImVec2(a.x + inset, b.y - 0.5f), ImVec2(b.x - inset, b.y - 0.5f),
                Colour(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.35f), 1.0f);
}

// How a piece of glass switches on, like an old TV: a bright line across its
// middle, that opens to the whole glass with a flash, then scan lines that
// fade. Start() when it appears; Draw() over it each frame (on the
// foreground draw list) while Active().
class TvSwitchOn
{
public:
  static constexpr double LINE = 0.10;
  static constexpr double OPEN = 0.16;
  static constexpr double LINES = 0.40;

  void Start(double now) { m_started = now; }
  bool Active(double now) const { return m_started >= 0.0 && now < m_started + LINE + OPEN + LINES; }

  void Draw(ImDrawList* list, ImVec2 a, ImVec2 b, float rounding, double now, float scale) const
  {
    if (!Active(now))
      return;
    const float t = static_cast<float>(now - m_started);
    const float width = b.x - a.x;
    const float height = b.y - a.y;
    const float middle = (a.y + b.y) * 0.5f;
    const ImU32 screen = Colour(ImVec4(0.01f, 0.04f, 0.02f, 1.0f), 0.96f);
    const auto smooth = [](float x) {
      x = std::clamp(x, 0.0f, 1.0f);
      return x * x * (3.0f - 2.0f * x);
    };
    const float line = static_cast<float>(LINE);
    const float open_time = static_cast<float>(OPEN);
    if (t < line)
    {
      // The screen still off, the line growing across it from the middle.
      list->AddRectFilled(a, b, screen, rounding);
      const float grow = smooth(t / line);
      const float half = width * 0.5f * grow;
      const float cx = (a.x + b.x) * 0.5f;
      list->AddRectFilled(ImVec2(cx - half, middle - 1.5f * scale),
                          ImVec2(cx + half, middle + 1.5f * scale), Colour(WHITE, 0.95f));
      list->AddRectFilled(ImVec2(cx - half, middle - 5.0f * scale),
                          ImVec2(cx + half, middle + 5.0f * scale), Colour(CHART, 0.30f));
      return;
    }
    if (t < line + open_time)
    {
      // The picture opening from the line, top and bottom still dark.
      const float open = smooth((t - line) / open_time);
      const float half = height * 0.5f * open;
      list->PushClipRect(a, ImVec2(b.x, middle - half), true);
      list->AddRectFilled(a, b, screen, rounding);
      list->PopClipRect();
      list->PushClipRect(ImVec2(a.x, middle + half), b, true);
      list->AddRectFilled(a, b, screen, rounding);
      list->PopClipRect();
      list->AddRectFilled(ImVec2(a.x, middle - half - 1.5f * scale),
                          ImVec2(b.x, middle - half + 1.5f * scale), Colour(WHITE, 0.8f));
      list->AddRectFilled(ImVec2(a.x, middle + half - 1.5f * scale),
                          ImVec2(b.x, middle + half + 1.5f * scale), Colour(WHITE, 0.8f));
      list->AddRectFilled(ImVec2(a.x, middle - half), ImVec2(b.x, middle + half),
                          Colour(WHITE, 0.35f * (1.0f - open)), rounding);
    }
    // The flash, and the scan lines fading.
    const float after = std::max(0.0f, t - line - open_time * 0.5f);
    const float fade = 1.0f - smooth(after / static_cast<float>(LINES + OPEN * 0.5));
    if (fade <= 0.0f)
      return;
    list->AddRectFilled(a, b, Colour(CHART, 0.10f * fade), rounding);
    list->PushClipRect(a, b, true);
    const float step = std::max(2.0f, 3.0f * scale);
    for (float y = a.y; y < b.y; y += step)
      list->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), Colour(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.28f * fade));
    // A brighter band rolling down once.
    const float roll = a.y + height * smooth(after / static_cast<float>(LINES));
    list->AddRectFilledMultiColor(ImVec2(a.x, roll - 18.0f * scale), ImVec2(b.x, roll),
                                  Colour(WHITE, 0.0f), Colour(WHITE, 0.0f),
                                  Colour(WHITE, 0.10f * fade), Colour(WHITE, 0.10f * fade));
    list->PopClipRect();
  }

private:
  double m_started = -1.0;
};

// The carousel of pages, centred on centre_x with its bottom at `bottom`:
// the page shown in the middle, big, in the logo's green-yellow; the ones
// before and after it small at its sides (wrapping around); LB and RB at its
// ends (Q and E on the keyboard); a dot for each page under it. Returns the
// page clicked or chosen, or -1. focus_selected: the page shown takes the
// keyboard and gamepad focus; focused: whether one of its names has it.
inline float CarouselHeight(float scale)
{
  return 44.0f * scale + 16.0f * scale;
}

inline int Carousel(const std::vector<const char*>& labels, int selected, ImFont* bold,
                    float centre_x, float bottom, float scale, PadStyle style, bool keyboard,
                    bool focus_selected, bool* focused)
{
  const int count = static_cast<int>(labels.size());
  if (count == 0)
    return -1;
  const int previous = (selected + count - 1) % count;
  const int next = (selected + 1) % count;
  const std::string current = Capitals(labels[static_cast<std::size_t>(selected)]);
  const std::string before = Capitals(labels[static_cast<std::size_t>(previous)]);
  const std::string after = Capitals(labels[static_cast<std::size_t>(next)]);
  const float height = 44.0f * scale;
  const float big = 21.0f * scale;
  const float small = 13.0f * scale;
  const float glyph = 18.0f * scale;
  const float left_glyph = keyboard ? KeyCapWidth(bold, glyph, "Q")
                                    : PadButtonWidth(bold, glyph, style, PadButton::LeftShoulder);
  const float right_glyph = keyboard ? KeyCapWidth(bold, glyph, "E")
                                     : PadButtonWidth(bold, glyph, style, PadButton::RightShoulder);
  const float pad = 14.0f * scale;
  const float gap = 22.0f * scale;
  const float current_width = TextSize(bold, big, current.c_str()).x + 16.0f * scale;
  const float before_width = TextSize(bold, small, before.c_str()).x;
  const float after_width = TextSize(bold, small, after.c_str()).x;
  // The sides as wide as the wider one, so the page shown stays in the middle.
  const float side = std::max(before_width, after_width);
  const float width = pad + left_glyph + gap + side + gap + current_width + gap + side + gap +
                      right_glyph + pad;
  const ImVec2 a(std::round(centre_x - width * 0.5f), std::round(bottom - 16.0f * scale - height));
  const ImVec2 b(a.x + width, a.y + height);
  ImDrawList* list = ImGui::GetWindowDrawList();
  Glass(list, a, b, height * 0.5f, scale, 0.5f);
  const float cy = a.y + height * 0.5f;
  float x = a.x + pad;
  if (keyboard)
    DrawKeyCap(list, bold, ImVec2(x, cy - glyph * 0.5f), glyph, "Q");
  else
    DrawPadButton(list, bold, ImVec2(x, cy - glyph * 0.5f), glyph, style, PadButton::LeftShoulder,
                  0.85f);
  x += left_glyph + gap;

  int clicked = -1;
  bool any_focused = false;
  // One name: an invisible button with its text drawn over it.
  const auto name = [&](const char* id, const std::string& text, float size, float box_width,
                        float box_x, int page, bool is_current) {
    ImGui::SetCursorScreenPos(ImVec2(box_x, a.y + 4.0f * scale));
    if (ImGui::InvisibleButton(id, ImVec2(box_width, height - 8.0f * scale),
                               ImGuiButtonFlags_EnableNav))
      clicked = page;
    const bool hovered = ImGui::IsItemHovered();
    const bool item_focused = ImGui::IsItemFocused();
    if (is_current && focus_selected)
    {
      ImGui::FocusItem();
      ImGui::SetNavCursorVisible(true);
    }
    any_focused = any_focused || item_focused;
    const ImVec2 extent = TextSize(bold, size, text.c_str());
    const ImVec2 at(box_x + (box_width - extent.x) * 0.5f - size * 0.06f, cy - extent.y * 0.5f);
    if (is_current)
    {
      if (item_focused)
        list->AddRectFilled(ImVec2(box_x - 6.0f * scale, a.y + 6.0f * scale),
                            ImVec2(box_x + box_width + 6.0f * scale, b.y - 6.0f * scale),
                            Colour(CHART, 0.16f), (height - 12.0f * scale) * 0.5f);
      AddLeaningText(list, bold, size, at, Colour(CHART), text.c_str());
    }
    else
    {
      list->AddText(bold, size, at, Colour(WHITE, hovered || item_focused ? 0.95f : 0.5f),
                    text.c_str());
    }
  };
  name("##before", before, small, side, x, previous, false);
  x += side + gap;
  name("##current", current, big, current_width, x, selected, true);
  x += current_width + gap;
  name("##after", after, small, side, x, next, false);
  x += side + gap;
  if (keyboard)
    DrawKeyCap(list, bold, ImVec2(x, cy - glyph * 0.5f), glyph, "E");
  else
    DrawPadButton(list, bold, ImVec2(x, cy - glyph * 0.5f), glyph, style,
                  PadButton::RightShoulder, 0.85f);
  // The dots: one for each page, the one shown long.
  const float dots_y = b.y + 9.0f * scale;
  const float spacing = 14.0f * scale;
  for (int i = 0; i < count; ++i)
  {
    const float dot_x = centre_x + (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) *
                                       spacing;
    if (i == selected)
      list->AddRectFilled(ImVec2(dot_x - 6.0f * scale, dots_y - 2.5f * scale),
                          ImVec2(dot_x + 6.0f * scale, dots_y + 2.5f * scale), Colour(CHART),
                          2.5f * scale);
    else
      list->AddCircleFilled(ImVec2(dot_x, dots_y), 2.5f * scale, Colour(WHITE, 0.6f));
  }
  if (focused)
    *focused = any_focused;
  return clicked;
}

// The LAUNCH pill of the PLAY page: the A button (or Enter) and the label in
// the logo's green-yellow, on glass, at the cursor; `height` tall.
inline float LaunchPillWidth(ImFont* bold, float height, const char* label, bool keyboard)
{
  const float glyph = height * 0.6f;
  const float glyph_width = keyboard ? KeyCapWidth(bold, glyph, "Enter") : glyph;
  return height * 0.22f + glyph_width + height * 0.3f +
         TextSize(bold, height * 0.5f, Capitals(label).c_str()).x + height * 0.55f;
}

inline bool LaunchPill(const char* id, const char* label, ImFont* bold, float height,
                       float scale, PadStyle style, bool keyboard)
{
  const float width = LaunchPillWidth(bold, height, label, keyboard);
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const bool pressed =
      ImGui::InvisibleButton(id, ImVec2(width, height), ImGuiButtonFlags_EnableNav);
  const bool lit = ImGui::IsItemHovered() || ImGui::IsItemFocused();
  const ImVec2 b(a.x + width, a.y + height);
  ImDrawList* list = ImGui::GetWindowDrawList();
  const float rounding = height * 0.5f;
  for (int glow = 3; glow >= 1; --glow)
  {
    const float spread = static_cast<float>(glow) * 3.0f * scale;
    list->AddRectFilled(ImVec2(a.x - spread, a.y - spread), ImVec2(b.x + spread, b.y + spread),
                        Colour(CHART, lit ? 0.16f : 0.08f), rounding + spread);
  }
  Glass(list, a, b, rounding, scale, ImGui::IsItemActive() ? 0.75f : 0.62f);
  list->AddRect(a, b, Colour(CHART, lit ? 1.0f : 0.8f), rounding, 0,
                (lit ? 2.2f : 1.6f) * scale);
  const float glyph = height * 0.6f;
  const ImVec2 glyph_at(a.x + height * 0.22f, a.y + (height - glyph) * 0.5f);
  const float glyph_width = keyboard ? DrawKeyCap(list, bold, glyph_at, glyph, "Enter")
                                     : DrawPadButton(list, bold, glyph_at, glyph, style,
                                                     PadButton::South);
  const std::string text = Capitals(label);
  const float size = height * 0.5f;
  const ImVec2 extent = TextSize(bold, size, text.c_str());
  AddLeaningText(list, bold, size,
                 ImVec2(glyph_at.x + glyph_width + height * 0.3f, a.y + (height - extent.y) * 0.5f),
                 Colour(CHART), text.c_str(), 0.14f);
  return pressed;
}

// A small round glass button with a pad button's picture (or a key's) in
// it, at the cursor; `caption` shows under it while it is pointed at or
// focused. Returns true when clicked or chosen.
inline bool RoundButton(const char* id, PadButton button, const char* key, const char* caption,
                        ImFont* bold, float diameter, float scale, PadStyle style, bool keyboard)
{
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const bool pressed =
      ImGui::InvisibleButton(id, ImVec2(diameter, diameter), ImGuiButtonFlags_EnableNav);
  const bool lit = ImGui::IsItemHovered() || ImGui::IsItemFocused();
  const ImVec2 b(a.x + diameter, a.y + diameter);
  ImDrawList* list = ImGui::GetWindowDrawList();
  Glass(list, a, b, diameter * 0.5f, scale, 0.55f);
  if (lit)
    list->AddCircle(ImVec2(a.x + diameter * 0.5f, a.y + diameter * 0.5f), diameter * 0.5f,
                    Colour(CHART), 32, 1.8f * scale);
  const float glyph = diameter * 0.56f;
  const ImVec2 glyph_at(a.x + (diameter - glyph) * 0.5f, a.y + (diameter - glyph) * 0.5f);
  if (keyboard && key)
  {
    const float key_width = KeyCapWidth(bold, glyph * 0.8f, key);
    DrawKeyCap(list, bold, ImVec2(a.x + (diameter - key_width) * 0.5f, a.y + (diameter - glyph * 0.8f) * 0.5f),
               glyph * 0.8f, key);
  }
  else
  {
    DrawPadButton(list, bold, glyph_at, glyph, style, button);
  }
  if (lit && caption)
  {
    const float size = 13.0f * scale;
    const ImVec2 extent = TextSize(bold, size, caption);
    const ImVec2 at(a.x + (diameter - extent.x) * 0.5f - 8.0f * scale, b.y + 6.0f * scale);
    Glass(list, at, ImVec2(at.x + extent.x + 16.0f * scale, at.y + extent.y + 6.0f * scale),
          (extent.y + 6.0f * scale) * 0.5f, scale, 0.6f);
    list->AddText(bold, size, ImVec2(at.x + 8.0f * scale, at.y + 3.0f * scale), Colour(WHITE),
                  caption);
  }
  return pressed;
}

// A page's title, big and leaning, with a short stroke of the logo's
// green-yellow under it; takes its room at the cursor.
inline void PageTitle(const char* title, ImFont* bold, float scale)
{
  const std::string text = Capitals(title);
  const float size = 28.0f * scale;
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImDrawList* list = ImGui::GetWindowDrawList();
  AddLeaningText(list, bold, size, at, Colour(WHITE), text.c_str());
  AddLeaningBand(list, ImVec2(at.x + 3.0f * scale, at.y + size + 8.0f * scale),
                 ImVec2(at.x + 52.0f * scale, at.y + size + 12.0f * scale), Colour(CHART), 0.4f);
  ImGui::Dummy(ImVec2(TextSize(bold, size, text.c_str()).x + size * 0.2f, size + 16.0f * scale));
}

// The sub-pages of a page, on the title's line after it (from left): their
// names, the one shown on a soft glass pill. LT and RT go to the one before
// or after (PageDots shows them at the bottom). `shown` is kept in ImGui's
// storage under id. Returns the sub-page shown; changed: whether it changed
// this frame.
inline int SubPages(const char* id, const std::vector<const char*>& names, ImFont* bold,
                    float left, float top, float scale, bool triggers, bool* changed = nullptr)
{
  ImGuiStorage* storage = ImGui::GetStateStorage();
  const ImGuiID key = ImGui::GetID(id);
  const int count = static_cast<int>(names.size());
  const int before = std::clamp(storage->GetInt(key, 0), 0, std::max(0, count - 1));
  int shown = before;
  if (changed)
    *changed = false;
  if (count <= 1)
    return 0;
  if (triggers)
  {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL2, false))
      shown = std::max(0, shown - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR2, false))
      shown = std::min(count - 1, shown + 1);
  }
  const float size = 15.0f * scale;
  const float gap = 30.0f * scale;
  float x = left;
  ImDrawList* list = ImGui::GetWindowDrawList();
  const float line = top + 2.0f * scale;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  for (int i = 0; i < count; ++i)
  {
    const float text_width = TextSize(bold, size, names[static_cast<std::size_t>(i)]).x;
    ImGui::SetCursorScreenPos(ImVec2(x - 14.0f * scale, line));
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##sub", ImVec2(text_width + 28.0f * scale, 30.0f * scale),
                               ImGuiButtonFlags_EnableNav))
      shown = i;
    const bool lit = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImGui::PopID();
    if (i == shown)
      list->AddRectFilled(ImVec2(x - 14.0f * scale, line),
                          ImVec2(x + text_width + 14.0f * scale, line + 30.0f * scale),
                          Colour(WHITE, 0.10f), 15.0f * scale);
    list->AddText(bold, size, ImVec2(x, line + 6.0f * scale),
                  i == shown ? Colour(CHART) : Colour(WHITE, lit ? 0.95f : 0.6f),
                  names[static_cast<std::size_t>(i)]);
    x += text_width + gap;
  }
  ImGui::SetCursorScreenPos(cursor);
  storage->SetInt(key, shown);
  if (changed)
    *changed = shown != before;
  return shown;
}

inline void RowLine();
struct RowState;
inline RowState& Rows();
inline bool InRows();

// A switch in place of a check box: a track with a round knob, in the logo's
// green-yellow when on, then the label; in a section's rows, the label on the
// left and the switch at the right end. Same use as ImGui::Checkbox.
inline bool Switch(const char* label, bool* value)
{
  const bool row = InRows();
  if (row)
    RowLine();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float height = std::round(ImGui::GetFrameHeight() * 0.8f);
  const float width = std::round(height * 1.9f);
  const char* label_end = ImGui::FindRenderedTextEnd(label);
  const ImVec2 text = ImGui::CalcTextSize(label, label_end);
  const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float frame = ImGui::GetFrameHeight();
  const float row_width = ImGui::GetContentRegionAvail().x;
  const bool pressed = ImGui::InvisibleButton(
      label,
      ImVec2(row ? row_width : width + (text.x > 0.0f ? spacing + text.x : 0.0f), frame),
      ImGuiButtonFlags_EnableNav);
  if (pressed)
    *value = !*value;
  const bool lit = ImGui::IsItemHovered() || ImGui::IsItemFocused();
  ImDrawList* list = ImGui::GetWindowDrawList();
  const ImVec2 track(row ? a.x + row_width - width : a.x, a.y + (frame - height) * 0.5f);
  const ImVec2 track_end(track.x + width, track.y + height);
  // Its knob slides over a few frames.
  ImGuiStorage* storage = ImGui::GetStateStorage();
  const ImGuiID id = ImGui::GetItemID();
  float slide = storage->GetFloat(id, *value ? 1.0f : 0.0f);
  slide += ((*value ? 1.0f : 0.0f) - slide) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
  storage->SetFloat(id, slide);
  if (slide > 0.01f)
  {
    const int first = list->VtxBuffer.Size;
    list->AddRectFilled(track, track_end, IM_COL32_WHITE, height * 0.5f);
    ShadeY(list, first, track.y, track_end.y, ImVec4(CHART.x, CHART.y, CHART.z, slide),
           ImVec4(0.55f, 0.80f, 0.04f, slide));
  }
  if (slide < 0.99f)
  {
    list->AddRectFilled(track, track_end, Colour(WHITE, 0.14f * (1.0f - slide)), height * 0.5f);
    list->AddRect(track, track_end, Colour(WHITE, 0.22f * (1.0f - slide)), height * 0.5f);
  }
  if (lit)
    list->AddRect(ImVec2(track.x - 2.0f * scale, track.y - 2.0f * scale),
                  ImVec2(track_end.x + 2.0f * scale, track_end.y + 2.0f * scale),
                  Colour(CHART, 0.8f), height * 0.5f + 2.0f * scale, 0, 1.5f * scale);
  const float knob_x = track.x + height * 0.5f + (width - height) * slide;
  const ImVec4 knob(0.75f + (DEEP.x - 0.75f) * slide, 0.75f + (DEEP.y - 0.75f) * slide,
                    0.75f + (DEEP.z - 0.75f) * slide, 1.0f);
  list->AddCircleFilled(ImVec2(knob_x, track.y + height * 0.5f), height * 0.37f,
                        ImGui::ColorConvertFloat4ToU32(knob), 24);
  if (text.x > 0.0f)
    list->AddText(ImVec2(row ? a.x : track_end.x + spacing, a.y + (frame - text.y) * 0.5f),
                  ImGui::GetColorU32(ImGuiCol_Text), label, label_end);
  return pressed;
}

// Blocks in place of a slider bar: ten leaning blocks, as many lit as the
// value; dragged or clicked like a slider (it is one, unseen, under them),
// and with a gamepad or the arrows like one. Same use as ImGui::SliderInt.
inline bool BlocksSlider(const char* label, int* value, int lowest, int highest,
                         const char* format = "%d", ImGuiSliderFlags flags = 0)
{
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float frame_width = ImGui::CalcItemWidth();
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
  const bool changed = ImGui::SliderInt(label, value, lowest, highest, format, flags);
  ImGui::PopStyleColor(6);
  const ImVec2 a = ImGui::GetItemRectMin();
  const ImVec2 b(a.x + frame_width, ImGui::GetItemRectMax().y);
  const bool lit = ImGui::IsItemHovered() || ImGui::IsItemFocused() || ImGui::IsItemActive();
  ImDrawList* list = ImGui::GetWindowDrawList();
  // Its label, which the unseen slider can't show.
  const char* label_end = ImGui::FindRenderedTextEnd(label);
  if (label_end != label)
    list->AddText(ImVec2(b.x + ImGui::GetStyle().ItemInnerSpacing.x,
                         a.y + ImGui::GetStyle().FramePadding.y),
                  ImGui::GetColorU32(ImGuiCol_Text), label, label_end);
  char text[32];
  std::snprintf(text, sizeof(text), format, *value);
  const float text_width = ImGui::CalcTextSize(text).x + 10.0f * scale;
  const float blocks_width = std::max(1.0f, (b.x - a.x) - text_width);
  constexpr int BLOCKS = 10;
  const float gap = 4.0f * scale;
  const float block = (blocks_width - gap * (BLOCKS - 1)) / BLOCKS;
  const float filled = highest > lowest ? static_cast<float>(*value - lowest) /
                                              static_cast<float>(highest - lowest) *
                                              BLOCKS :
                                          0.0f;
  float x = a.x;
  for (int i = 0; i < BLOCKS; ++i)
  {
    const ImVec2 min(x, a.y + (b.y - a.y) * 0.18f);
    const ImVec2 max(x + block, b.y - (b.y - a.y) * 0.18f);
    const float lit_part = std::clamp(filled - static_cast<float>(i), 0.0f, 1.0f);
    AddLeaningBand(list, min, max, Colour(WHITE, 0.12f), 0.3f);
    if (lit_part > 0.0f)
      AddLeaningBand(list, min, ImVec2(min.x + block * lit_part, max.y),
                     Colour(CHART, 0.55f + 0.045f * static_cast<float>(i)), 0.3f);
    x += block + gap;
  }
  if (lit)
    list->AddRect(ImVec2(a.x - 4.0f * scale, a.y), ImVec2(b.x, b.y), Colour(CHART, 0.6f),
                  6.0f * scale, 0, 1.2f * scale);
  list->AddText(ImVec2(b.x - text_width + 6.0f * scale, a.y + (b.y - a.y - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), text);
  return changed;
}

// Rows, as in a game's options: in a section (BeginSection in
// launcher_ui.hpp) each row has its name on the left and its control on the
// right, a thin line between rows.
struct RowState
{
  bool active = false;  // in a section
  int rows = 0;         // rows drawn in it so far
};

inline RowState& Rows()
{
  static RowState rows;
  return rows;
}

// How wide a row's control is: about half the column, within reason.
inline float ControlWidth()
{
  const float scale = ImGui::GetStyle().FontScaleDpi;
  return std::clamp(ImGui::GetContentRegionAvail().x * 0.5f, 150.0f * scale, 300.0f * scale);
}

// Where a row's control starts (window x, as SameLine() takes it).
inline float ControlX()
{
  return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ControlWidth();
}

inline bool InRows()
{
  return Rows().active;
}

// Before a row: the line under the row before it (none before the first).
inline void RowLine()
{
  RowState& rows = Rows();
  if (!rows.active)
    return;
  if (rows.rows > 0)
  {
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float y = at.y - ImGui::GetStyle().ItemSpacing.y * 0.5f - 1.0f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, y),
                                        ImVec2(at.x + ImGui::GetContentRegionAvail().x, y),
                                        Colour(WHITE, 0.07f), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
  }
  ++rows.rows;
}

// The dots of a page's sub-pages, with LT and RT, centred on centre_x at y.
inline void PageDots(int count, int shown, ImFont* bold, float centre_x, float y, float scale)
{
  if (count <= 1)
    return;
  ImDrawList* list = ImGui::GetWindowDrawList();
  const float spacing = 18.0f * scale;
  const float half = static_cast<float>(count - 1) * spacing * 0.5f;
  const float glyph = 17.0f * scale;
  DrawPadLabel(list, bold, ImVec2(centre_x - half - 26.0f * scale - PadLabelWidth(bold, glyph, "LT"),
                                  y - glyph * 0.5f),
               glyph, "LT", 0.8f);
  for (int i = 0; i < count; ++i)
  {
    const float x = centre_x - half + static_cast<float>(i) * spacing;
    if (i == shown)
      list->AddRectFilled(ImVec2(x - 7.0f * scale, y - 3.0f * scale),
                          ImVec2(x + 7.0f * scale, y + 3.0f * scale), Colour(CHART), 3.0f * scale);
    else
      list->AddCircleFilled(ImVec2(x, y), 3.0f * scale, Colour(WHITE, 0.35f));
  }
  DrawPadLabel(list, bold, ImVec2(centre_x + half + 26.0f * scale, y - glyph * 0.5f), glyph, "RT",
               0.8f);
}

// A selector in place of a drop-down list: ◀ value ▶. Left and right (the
// arrows, the D-pad or the stick, or a click on ◀ or ▶) choose the one before
// or after; A, Enter or a click in the middle opens the whole list. Used like
// a combo: BeginSelector() instead of ImGui::BeginCombo(), SelectorItem()
// instead of ImGui::Selectable() for its choices, EndSelector() instead of
// ImGui::EndCombo(), only when BeginSelector() returned true. To go to the one
// before or after, it opens the list unseen for two frames: the first finds
// the choice now selected, the second chooses its neighbour (as a click on it
// would).
struct SelectorState
{
  ImGuiID id = 0;
  int step = 0;      // -1 or 1 while stepping
  int phase = 0;     // 1: finding the selected one; 2: choosing
  int selected = -1;
  int target = -1;
  int counter = 0;
  bool hidden = false;
};

inline SelectorState& CurrentSelector()
{
  static SelectorState state;
  return state;
}

inline bool BeginSelector(const char* id, const char* preview, ImGuiComboFlags flags = 0)
{
  (void)flags;
  SelectorState& state = CurrentSelector();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float width = ImGui::CalcItemWidth();
  const float height = ImGui::GetFrameHeight();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const ImVec2 b(a.x + width, a.y + height);
  const ImGuiID popup_id = ImGui::GetID(id) + 1;
  const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height), ImGuiButtonFlags_EnableNav);
  const ImGuiID item_id = ImGui::GetItemID();
  const bool hovered = ImGui::IsItemHovered();
  const bool focused = ImGui::IsItemFocused();
  const float arrow_zone = height * 1.1f;
  int step = 0;
  bool open_list = false;
  if (pressed)
  {
    const float mouse_x = ImGui::GetIO().MousePos.x;
    if (ImGui::GetIO().MouseClicked[0] || ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
      if (mouse_x < a.x + arrow_zone)
        step = -1;
      else if (mouse_x > b.x - arrow_zone)
        step = 1;
      else
        open_list = true;
    }
    else
    {
      open_list = true;
    }
  }
  if (focused && !ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None))
  {
    const bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ||
                      ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) ||
                      ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft);
    const bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
                       ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ||
                       ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight);
    if (left || right)
    {
      // The arrows change the choice here instead of moving the focus.
      ImGui::NavMoveRequestCancel();
      step = left ? -1 : 1;
    }
  }
  const bool stepping_here = state.id == item_id && state.phase != 0;
  if (step != 0 && !stepping_here)
  {
    state = SelectorState{};
    state.id = item_id;
    state.step = step;
    state.phase = 1;
    ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);
  }
  else if (open_list)
  {
    state = SelectorState{};
    ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);
  }

  // The look: an outlined pill, ◀ and ▶ at its ends, the value in the middle.
  const bool lit = hovered || focused;
  ImDrawList* list = ImGui::GetWindowDrawList();
  const float rounding = height * 0.5f;
  if (lit)
    list->AddRectFilled(a, b, Colour(CHART, 0.10f), rounding);
  list->AddRect(a, b, lit ? Colour(CHART, 0.85f) : Colour(WHITE, 0.16f), rounding, 0,
                (lit ? 1.6f : 1.0f) * scale);
  const float cy = a.y + height * 0.5f;
  const float t = height * 0.17f;
  const ImU32 arrow = lit ? Colour(CHART) : Colour(WHITE, 0.7f);
  const float lx = a.x + height * 0.45f;
  const float rx = b.x - height * 0.45f;
  list->AddTriangleFilled(ImVec2(lx, cy), ImVec2(lx + t * 1.4f, cy - t), ImVec2(lx + t * 1.4f, cy + t),
                          arrow);
  list->AddTriangleFilled(ImVec2(rx, cy), ImVec2(rx - t * 1.4f, cy - t), ImVec2(rx - t * 1.4f, cy + t),
                          arrow);
  if (preview)
  {
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float room = std::max(1.0f, width - arrow_zone * 2.0f);
    std::string text(preview);
    const auto fits = [&](const std::string& candidate) {
      return font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x <= room;
    };
    if (!fits(text))
    {
      // Shortened from its end, whole UTF-8 characters at a time, with "...".
      while (text.size() > 1 && !fits(text + "..."))
      {
        std::size_t cut = text.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
          --cut;
        text.erase(cut);
      }
      text += "...";
    }
    const float text_width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
    list->AddText(font, size, ImVec2(a.x + (width - text_width) * 0.5f, cy - size * 0.5f),
                  ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
  }

  if (!ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None))
  {
    if (state.id == item_id)
      state = SelectorState{};
    return false;
  }
  const bool hidden = state.id == item_id && state.phase != 0;
  state.hidden = hidden;
  state.counter = 0;
  if (hidden)
  {
    // Stepping: the list opens unseen, away from the window.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
    ImGui::SetNextWindowPos(ImVec2(-10000.0f, -10000.0f));
  }
  const bool open = ImGui::BeginComboPopup(popup_id, ImRect(a, b), ImGuiComboFlags_None);
  if (!open && hidden)
  {
    ImGui::PopStyleVar();
    state = SelectorState{};
  }
  return open;
}

// One choice of a selector's list. Same use as ImGui::Selectable().
inline bool SelectorItem(const char* label, bool selected)
{
  SelectorState& state = CurrentSelector();
  if (!state.hidden)
    return ImGui::Selectable(label, selected);
  const int index = state.counter++;
  if (state.phase == 1)
  {
    if (selected)
      state.selected = index;
    return false;
  }
  return index == state.target;
}

inline void EndSelector()
{
  SelectorState& state = CurrentSelector();
  const bool hidden = state.hidden;
  if (hidden)
  {
    if (state.phase == 1)
    {
      // Found: next frame, its neighbour (none past the ends).
      const int count = state.counter;
      const int target = state.selected < 0 ? 0 : state.selected + state.step;
      if (count == 0 || target < 0 || target >= count)
      {
        ImGui::CloseCurrentPopup();
        state = SelectorState{};
      }
      else
      {
        state.target = target;
        state.phase = 2;
      }
    }
    else
    {
      ImGui::CloseCurrentPopup();
      state = SelectorState{};
    }
    state.hidden = false;
  }
  ImGui::EndCombo();
  if (hidden)
    ImGui::PopStyleVar();
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
