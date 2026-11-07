#pragma once

// How the in-game menu looks: a panel in the launcher's colours over the
// darkened game, drawn with an ImGui draw list so it needs nothing from
// Dolphin's on-screen interface (game_menu.cpp draws it on the game's frames).
// The texts are the launcher's translations (tools/launcher_text.hpp).

#include "game_menu_state.hpp"
#include "launcher_look.hpp"
#include "launcher_text.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>

namespace moderngekko::detail
{
struct GameMenuFonts
{
  ImFont* regular = nullptr;  // ImGui's current font when null
  ImFont* bold = nullptr;     // the regular font when null
};

namespace game_menu_draw
{
inline ImU32 Colour(float r, float g, float b, float a)
{
  return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, std::clamp(a, 0.0f, 1.0f)));
}

inline const char* ItemText(MenuItem item, bool netplay)
{
  using moderngekko::frontend::Tr;
  switch (item)
  {
  case MenuItem::Continue:
    return Tr("Continue");
  case MenuItem::Leave:
    return netplay ? Tr("Back to the lobby") : Tr("Back to the launcher");
  case MenuItem::Quit:
    return Tr("Quit game");
  case MenuItem::No:
    return Tr("No");
  case MenuItem::Yes:
    return Tr("Yes");
  }
  return "";
}

// A round button face, as on the controller, with its letter in the middle.
inline void DrawBadge(ImDrawList* list, ImFont* font, ImVec2 centre, float radius,
                      const char* letter, ImU32 fill, ImU32 text)
{
  list->AddCircleFilled(centre, radius, fill, 24);
  const float size = radius * 1.25f;
  const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, letter);
  list->AddText(font, size, ImVec2(centre.x - extent.x * 0.5f, centre.y - extent.y * 0.5f), text,
                letter);
}
}  // namespace game_menu_draw

// Draws the open menu over a window of display pixels. fade goes from 0 to 1
// as the menu opens. In Netplay the game keeps running behind it, and leaving
// goes back to the lobby instead of the launcher.
inline void DrawGameMenu(ImDrawList* list, ImVec2 display, const GameMenuFonts& fonts,
                         const MenuState& state, bool netplay, float fade)
{
  using moderngekko::frontend::Tr;
  using namespace game_menu_draw;
  if (!state.IsOpen() || display.x <= 0.0f || display.y <= 0.0f)
    return;
  ImFont* regular = fonts.regular ? fonts.regular : ImGui::GetFont();
  ImFont* bold = fonts.bold ? fonts.bold : regular;
  fade = std::clamp(fade, 0.0f, 1.0f);

  // Sized for 1280x720 and scaled to the window, never so small it can't be read.
  const float s = std::max(0.7f, std::min(display.x / 1280.0f, display.y / 720.0f));
  const ImVec4 accent = moderngekko::frontend::ui::LAUNCHER_ACCENT;
  const ImU32 accent_colour = Colour(accent.x, accent.y, accent.z, fade);
  const ImU32 text_colour = Colour(0.94f, 0.94f, 0.94f, fade);
  const ImU32 dim_text_colour = Colour(0.62f, 0.62f, 0.65f, fade);

  // What the page says: its title, a line under it, its rows.
  const char* title = "";
  const char* body = nullptr;
  switch (state.Page())
  {
  case MenuPage::Main:
    title = netplay ? Tr("Menu") : Tr("Paused");
    if (netplay)
      body = Tr("The match keeps going while this menu is open.");
    break;
  case MenuPage::ConfirmLeave:
    title = netplay ? Tr("Back to the lobby?") : Tr("Back to the launcher?");
    body = netplay ? Tr("The match ends for every player.") :
                     Tr("Progress the game hasn't saved will be lost.");
    break;
  case MenuPage::ConfirmQuit:
    title = Tr("Quit the game?");
    body = netplay ? Tr("The match ends for every player.") :
                     Tr("Progress the game hasn't saved will be lost.");
    break;
  }

  const float padding = 30.0f * s;
  const float width = std::min(470.0f * s, display.x - 32.0f * s);
  const float inner = width - padding * 2.0f;
  const float title_size = 31.0f * s;
  const float body_size = 17.0f * s;
  const float row_size = 21.0f * s;
  const float row_height = 46.0f * s;
  const float row_gap = 6.0f * s;
  const float hint_size = 15.0f * s;
  const float badge_radius = 10.5f * s;
  const auto items = state.Items();

  const ImVec2 title_extent = bold->CalcTextSizeA(title_size, FLT_MAX, inner, title);
  const ImVec2 body_extent =
      body ? regular->CalcTextSizeA(body_size, FLT_MAX, inner, body) : ImVec2(0.0f, 0.0f);
  const float rows_height =
      static_cast<float>(items.size()) * (row_height + row_gap) - row_gap;
  const float height = padding + title_extent.y + (body ? 10.0f * s + body_extent.y : 0.0f) +
                       22.0f * s + rows_height + 24.0f * s + 1.0f + 16.0f * s +
                       badge_radius * 2.0f + padding;

  // The game darkens and the panel rises a little into place as it fades in.
  list->AddRectFilled(ImVec2(0.0f, 0.0f), display, Colour(0.0f, 0.0f, 0.0f, 0.58f * fade));
  const float rise = (1.0f - fade) * 14.0f * s;
  const ImVec2 min(std::floor((display.x - width) * 0.5f),
                   std::floor((display.y - height) * 0.5f + rise));
  const ImVec2 max(min.x + width, min.y + height);
  const float rounding = 14.0f * s;
  list->AddRectFilled(min, max, Colour(0.031f, 0.031f, 0.035f, 0.97f * fade), rounding);
  list->AddRect(min, max, Colour(0.157f, 0.157f, 0.169f, fade), rounding, 0, std::max(1.0f, s));
  // The accent line along the top, like the launcher's selected tab.
  list->AddRectFilled(ImVec2(min.x + rounding, min.y), ImVec2(max.x - rounding, min.y + 3.0f * s),
                      accent_colour, 1.5f * s);

  float y = min.y + padding;
  const float x = min.x + padding;
  list->AddText(bold, title_size, ImVec2(x, y), accent_colour, title, nullptr, inner);
  y += title_extent.y;
  if (body)
  {
    y += 10.0f * s;
    list->AddText(regular, body_size, ImVec2(x, y), dim_text_colour, body, nullptr, inner);
    y += body_extent.y;
  }
  y += 22.0f * s;

  for (std::size_t i = 0; i < items.size(); ++i)
  {
    const bool selected = i == state.Selected();
    const ImVec2 row_min(x - 12.0f * s, y);
    const ImVec2 row_max(x + inner + 12.0f * s, y + row_height);
    if (selected)
    {
      list->AddRectFilled(row_min, row_max, Colour(accent.x, accent.y, accent.z, 0.16f * fade),
                          8.0f * s);
      list->AddRectFilled(ImVec2(row_min.x, row_min.y + 9.0f * s),
                          ImVec2(row_min.x + 4.0f * s, row_max.y - 9.0f * s), accent_colour,
                          2.0f * s);
    }
    const char* text = ItemText(items[i], netplay);
    ImFont* font = selected ? bold : regular;
    const ImVec2 extent = font->CalcTextSizeA(row_size, FLT_MAX, 0.0f, text);
    list->AddText(font, row_size, ImVec2(x + 8.0f * s, y + (row_height - extent.y) * 0.5f),
                  selected ? text_colour : Colour(0.80f, 0.80f, 0.82f, fade), text);
    y += row_height + row_gap;
  }
  y += 24.0f * s - row_gap;

  // The buttons that work here, as the controller draws them: A green, B red.
  list->AddLine(ImVec2(x, y), ImVec2(x + inner, y), Colour(0.157f, 0.157f, 0.169f, fade),
                std::max(1.0f, s));
  y += 1.0f + 16.0f * s;
  float hint_x = x;
  const auto hint = [&](const char* letter, ImU32 fill, ImU32 letter_colour, const char* text) {
    DrawBadge(list, bold, ImVec2(hint_x + badge_radius, y + badge_radius), badge_radius, letter,
              fill, letter_colour);
    hint_x += badge_radius * 2.0f + 8.0f * s;
    const ImVec2 extent = regular->CalcTextSizeA(hint_size, FLT_MAX, 0.0f, text);
    list->AddText(regular, hint_size, ImVec2(hint_x, y + badge_radius - extent.y * 0.5f),
                  dim_text_colour, text);
    hint_x += extent.x + 22.0f * s;
  };
  hint("A", Colour(0.16f, 0.72f, 0.45f, fade), Colour(0.03f, 0.08f, 0.05f, fade), Tr("Accept"));
  hint("B", Colour(0.86f, 0.25f, 0.24f, fade), Colour(1.0f, 1.0f, 1.0f, fade), Tr("Go back"));
}
}  // namespace moderngekko::detail
