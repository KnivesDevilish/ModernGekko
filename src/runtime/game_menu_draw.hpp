#pragma once

// How the in-game menu looks: a panel in the launcher's colours over the
// darkened game, drawn with an ImGui draw list so it needs nothing from
// Dolphin's on-screen interface (game_menu.cpp draws it on the game's frames).
// The texts are the launcher's translations (tools/launcher_text.hpp).

#include "button_glyphs.hpp"
#include "game_menu_controls.hpp"
#include "game_menu_state.hpp"
#include "launcher_look.hpp"
#include "launcher_text.hpp"
#include "moderngekko/runtime.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace moderngekko::detail
{
struct GameMenuFonts
{
  ImFont* regular = nullptr;  // ImGui's current font when null
  ImFont* bold = nullptr;     // the regular font when null
};

// The buttons the hints at the bottom of the menu show for accepting and
// going back: the profile's, as the controller prints them or as keyboard
// keys, whichever of the two was used last.
struct GameMenuHints
{
  bool keys = false;  // the keyboard was used last
  frontend::ui::PadStyle style = frontend::ui::PadStyle::Xbox;
  // Nothing when the profile gives the pad (or the keyboard) none; the
  // other's is drawn then, or the controller's usual A and B.
  std::optional<PadHint> accept_pad;
  std::optional<PadHint> back_pad;
  std::string accept_key;  // English, as KeyName names it: "S", "Enter"
  std::string back_key;
};

// What the pages show besides their rows.
struct GameMenuContent
{
  // The Quick settings' rows, in the state's order.
  std::span<const QuickSetting> settings;
  // The Controls page: the emulated controller's name (its first column's
  // title), its rows, and the pad's own name (empty for a keyboard).
  std::string_view controls_title;
  std::span<const ControlsRow> controls;
  std::string_view controller_name;
  // In Netplay the game keeps running behind the menu, and leaving goes back
  // to the lobby instead of the launcher.
  bool netplay = false;
  GameMenuHints hints;
};

// A line at the bottom of the screen saying how a screenshot went, shown
// whether the menu is open or not.
struct GameMenuNotice
{
  enum class Kind
  {
    Working,
    Done,
    Failed,
  };
  std::string text;
  Kind kind = Kind::Working;
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
  case MenuItem::QuickSettings:
    return Tr("Quick settings");
  case MenuItem::Controls:
    return Tr("See controls");
  case MenuItem::Screenshot:
    return Tr("Take screenshot");
  case MenuItem::Leave:
    return netplay ? Tr("Back to the lobby") : Tr("Back to the launcher");
  case MenuItem::Quit:
    return Tr("Quit game");
  case MenuItem::No:
    return Tr("No");
  case MenuItem::Yes:
    return Tr("Yes");
  case MenuItem::Setting:
    break;
  }
  return "";
}

// The name of a quick setting's row.
inline const char* SettingText(QuickSetting::Kind kind)
{
  using moderngekko::frontend::Tr;
  switch (kind)
  {
  case QuickSetting::Kind::Effect:
    return Tr("Visual effect");
  case QuickSetting::Kind::InternalResolution:
    return Tr("Internal resolution");
  case QuickSetting::Kind::GameVolume:
    return Tr("Game volume");
  case QuickSetting::Kind::Border:
    return Tr("4:3 borders");
  }
  return "";
}

// The line under the Quick settings rows for the chosen row: what its choice
// does, or what the row is about.
inline const char* SettingHint(const QuickSetting& setting, std::size_t chosen)
{
  using moderngekko::frontend::Tr;
  if (chosen < setting.choices.size() && !setting.choices[chosen].description.empty())
    return Tr(setting.choices[chosen].description.c_str());
  switch (setting.kind)
  {
  case QuickSetting::Kind::InternalResolution:
    return Tr("Higher looks sharper and needs a faster graphics card.");
  case QuickSetting::Kind::Border:
    return Tr("Pictures beside the game when it is shown in 4:3 on a wider screen. In Netplay "
              "too, only you see your choice.");
  case QuickSetting::Kind::Effect:
  case QuickSetting::Kind::GameVolume:
    break;
  }
  return nullptr;
}

// Draws a hint's button with its top left corner at min: the key when the
// keyboard was used last (or the pad has none), else the pad's button as the
// controller prints it, else fallback. Returns its width.
inline float DrawHintButton(ImDrawList* list, ImFont* font, ImVec2 min, float height,
                            const GameMenuHints& hints, const std::optional<PadHint>& pad,
                            const std::string& key, frontend::ui::PadButton fallback, float alpha)
{
  using moderngekko::frontend::Tr;
  namespace ui = frontend::ui;
  if (!key.empty() && (hints.keys || !pad))
    return ui::DrawKeyCap(list, font, min, height, Tr(key), alpha);
  if (pad && pad->kind == PadHint::Kind::Name)
    return ui::DrawPadLabel(list, font, min, height, Tr(pad->name.c_str()), alpha);
  ui::PadButton button = fallback;
  if (pad && pad->kind == PadHint::Kind::Face)
  {
    constexpr std::array<ui::PadButton, 4> faces = {ui::PadButton::South, ui::PadButton::East,
                                                    ui::PadButton::West, ui::PadButton::North};
    button = faces[static_cast<std::size_t>(std::clamp(pad->place, 0, 3))];
  }
  else if (pad)
  {
    button = pad->place == 0 ? ui::PadButton::LeftShoulder : ui::PadButton::RightShoulder;
  }
  return ui::DrawPadButton(list, font, min, height, hints.style, button, alpha);
}

// The names of a Controls row's inputs, translated and joined; what the page
// says instead when there are none or the profile was edited by hand.
inline std::string ControlNames(const std::vector<std::string>& names, bool custom, bool* none)
{
  using moderngekko::frontend::Tr;
  *none = false;
  if (custom)
    return Tr("edited by hand");
  std::string joined;
  for (const std::string& name : names)
  {
    if (!joined.empty())
      joined += ", ";
    joined += Tr(name.c_str());
  }
  if (joined.empty())
  {
    *none = true;
    return Tr("not set");
  }
  return joined;
}

// text cut to fit width, ending in "..." when it doesn't.
inline std::string FitText(ImFont* font, float size, std::string text, float width)
{
  if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= width)
    return text;
  while (!text.empty())
  {
    // Back by one UTF-8 character.
    std::size_t end = text.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
      --end;
    text.resize(end);
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, (text + "...").c_str()).x <= width)
      return text + "...";
  }
  return text;
}

// A small arrow pointing left or right, centred on a point.
inline void DrawArrow(ImDrawList* list, ImVec2 centre, float size, bool right, ImU32 colour)
{
  const float direction = right ? 1.0f : -1.0f;
  list->AddTriangleFilled(ImVec2(centre.x + direction * size * 0.5f, centre.y),
                          ImVec2(centre.x - direction * size * 0.5f, centre.y - size * 0.6f),
                          ImVec2(centre.x - direction * size * 0.5f, centre.y + size * 0.6f),
                          colour);
}
}  // namespace game_menu_draw

// Draws the open menu over a window of display pixels. fade goes from 0 to 1
// as the menu opens.
inline void DrawGameMenu(ImDrawList* list, ImVec2 display, const GameMenuFonts& fonts,
                         const MenuState& state, const GameMenuContent& content, float fade)
{
  using moderngekko::frontend::Format;
  using moderngekko::frontend::Tr;
  using namespace game_menu_draw;
  if (!state.IsOpen() || display.x <= 0.0f || display.y <= 0.0f)
    return;
  ImFont* regular = fonts.regular ? fonts.regular : ImGui::GetFont();
  ImFont* bold = fonts.bold ? fonts.bold : regular;
  fade = std::clamp(fade, 0.0f, 1.0f);
  const std::span<const QuickSetting> settings = content.settings;
  const bool netplay = content.netplay;
  const bool settings_page = state.Page() == MenuPage::QuickSettings;
  const bool controls_page = state.Page() == MenuPage::Controls;

  // Sized for 1280x720 and scaled to the window, never so small it can't be read.
  const float s = std::max(0.7f, std::min(display.x / 1280.0f, display.y / 720.0f));
  const ImVec4 accent = moderngekko::frontend::ui::LAUNCHER_ACCENT;
  const ImU32 accent_colour = Colour(accent.x, accent.y, accent.z, fade);
  const ImU32 text_colour = Colour(0.94f, 0.94f, 0.94f, fade);
  const ImU32 dim_text_colour = Colour(0.62f, 0.62f, 0.65f, fade);

  // What the page says: its title, a line under it, its rows.
  const char* title = "";
  const char* body = nullptr;
  std::string controller_line;
  switch (state.Page())
  {
  case MenuPage::Main:
    title = netplay ? Tr("Menu") : Tr("Paused");
    if (netplay)
      body = Tr("The match keeps going while this menu is open.");
    break;
  case MenuPage::QuickSettings:
    title = Tr("Quick settings");
    body = Tr("Changes apply at once and are kept for next time.");
    break;
  case MenuPage::Controls:
    title = Tr("Controls");
    if (content.controller_name.empty())
    {
      body = Tr("Keyboard only");
    }
    else
    {
      controller_line = Format(Tr("Controller: %s"), std::string(content.controller_name).c_str());
      body = controller_line.c_str();
    }
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
  // The settings' rows hold a name and a choice side by side, and the
  // controls' a button and two columns of inputs.
  const float wanted_width = settings_page ? 640.0f : controls_page ? 720.0f : 470.0f;
  const float width = std::min(wanted_width * s, display.x - 32.0f * s);
  const float inner = width - padding * 2.0f;
  const float title_size = 31.0f * s;
  const float body_size = 17.0f * s;
  const float row_size = 21.0f * s;
  const float row_height = 46.0f * s;
  const float row_gap = 6.0f * s;
  const float hint_size = 15.0f * s;
  const float badge_radius = 10.5f * s;
  const auto items = state.Items();

  // Under the settings' rows, room for two lines saying what the chosen one
  // does, so the panel keeps its size from row to row.
  const char* setting_hint = nullptr;
  if (settings_page && state.Selected() < settings.size())
    setting_hint = SettingHint(settings[state.Selected()], state.Chosen(state.Selected()));
  const float setting_hint_height =
      settings_page ? 16.0f * s + regular->CalcTextSizeA(body_size, FLT_MAX, 0.0f, "A").y * 2.0f :
                      0.0f;

  const ImVec2 title_extent = bold->CalcTextSizeA(title_size, FLT_MAX, inner, title);
  const ImVec2 body_extent =
      body ? regular->CalcTextSizeA(body_size, FLT_MAX, inner, body) : ImVec2(0.0f, 0.0f);
  // The Controls page: a line of column titles, then a row per button. A
  // column with nothing in any row is left out.
  const float table_row_height = 30.0f * s;
  const float table_size = 18.0f * s;
  bool any_pad = false;
  bool any_keys = false;
  for (const ControlsRow& row : content.controls)
  {
    any_pad = any_pad || !row.pad.empty() || row.custom;
    any_keys = any_keys || !row.keys.empty();
  }
  if (!any_pad && !any_keys)
    any_pad = true;
  const float table_height =
      controls_page ? table_row_height * static_cast<float>(content.controls.size() + 1) : 0.0f;
  const float rows_height =
      controls_page ? table_height :
                      static_cast<float>(items.size()) * (row_height + row_gap) - row_gap;
  const float height = padding + title_extent.y + (body ? 10.0f * s + body_extent.y : 0.0f) +
                       22.0f * s + rows_height + setting_hint_height + 24.0f * s + 1.0f +
                       16.0f * s + badge_radius * 2.0f + padding;

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

  if (controls_page)
  {
    float label_width = bold->CalcTextSizeA(hint_size, FLT_MAX, 0.0f,
                                            std::string(content.controls_title).c_str())
                            .x;
    for (const ControlsRow& row : content.controls)
      label_width = std::max(
          label_width, bold->CalcTextSizeA(table_size, FLT_MAX, 0.0f, Tr(row.label.c_str())).x);
    label_width += 24.0f * s;
    const int columns = (any_pad ? 1 : 0) + (any_keys ? 1 : 0);
    const float column_gap = 16.0f * s;
    const float column_width =
        (inner - label_width - column_gap * static_cast<float>(columns - 1)) /
        static_cast<float>(columns);
    const float pad_x = x + label_width;
    const float keys_x = any_pad ? pad_x + column_width + column_gap : pad_x;
    const auto middle = [&](ImFont* font, float size, const char* text, float row_y) {
      return row_y + (table_row_height - font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).y) * 0.5f;
    };
    // The column titles, quieter than the rows.
    const std::string first_title(content.controls_title);
    list->AddText(bold, hint_size, ImVec2(x, middle(bold, hint_size, first_title.c_str(), y)),
                  dim_text_colour, first_title.c_str());
    if (any_pad)
      list->AddText(bold, hint_size,
                    ImVec2(pad_x, middle(bold, hint_size, Tr("Controller"), y)),
                    dim_text_colour, Tr("Controller"));
    if (any_keys)
      list->AddText(bold, hint_size, ImVec2(keys_x, middle(bold, hint_size, Tr("Keyboard"), y)),
                    dim_text_colour, Tr("Keyboard"));
    y += table_row_height;
    for (std::size_t i = 0; i < content.controls.size(); ++i)
    {
      const ControlsRow& row = content.controls[i];
      // Every other row a little lighter, to follow a line across.
      if (i % 2 == 0)
        list->AddRectFilled(ImVec2(x - 12.0f * s, y), ImVec2(x + inner + 12.0f * s,
                                                              y + table_row_height),
                            Colour(1.0f, 1.0f, 1.0f, 0.035f * fade), 6.0f * s);
      const char* label = Tr(row.label.c_str());
      list->AddText(bold, table_size, ImVec2(x, middle(bold, table_size, label, y)), accent_colour,
                    label);
      const auto cell = [&](float cell_x, const std::vector<std::string>& names, bool custom) {
        bool none = false;
        const std::string text = FitText(regular, table_size,
                                         ControlNames(names, custom, &none), column_width);
        list->AddText(regular, table_size,
                      ImVec2(cell_x, middle(regular, table_size, text.c_str(), y)),
                      none ? dim_text_colour : text_colour, text.c_str());
      };
      if (any_pad)
        cell(pad_x, row.pad, row.custom);
      if (any_keys)
        cell(keys_x, row.keys, row.custom);
      y += table_row_height;
    }
    y += row_gap;
  }

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
    const bool setting = items[i] == MenuItem::Setting && i < settings.size();
    const char* text = setting ? SettingText(settings[i].kind) : ItemText(items[i], netplay);
    ImFont* font = selected ? bold : regular;
    const ImVec2 extent = font->CalcTextSizeA(row_size, FLT_MAX, 0.0f, text);
    const float text_y = y + (row_height - extent.y) * 0.5f;
    list->AddText(font, row_size, ImVec2(x + 8.0f * s, text_y),
                  selected ? text_colour : Colour(0.80f, 0.80f, 0.82f, fade), text);
    if (setting)
    {
      // The choice at the right end of the row, between arrows on the chosen
      // row; an arrow fades where the choices end.
      const QuickSetting& row = settings[i];
      const std::size_t chosen = state.Chosen(i);
      const char* value =
          chosen < row.choices.size() ? Tr(row.choices[chosen].label.c_str()) : "";
      const ImVec2 value_extent = font->CalcTextSizeA(row_size, FLT_MAX, 0.0f, value);
      const float arrow = 9.0f * s;
      const float right_edge = x + inner - 4.0f * s;
      const float value_x = right_edge - (selected ? arrow + 12.0f * s : 0.0f) - value_extent.x;
      list->AddText(font, row_size, ImVec2(value_x, text_y),
                    selected ? accent_colour : dim_text_colour, value);
      if (selected)
      {
        const float middle = y + row_height * 0.5f;
        const ImU32 faded = Colour(accent.x, accent.y, accent.z, 0.25f * fade);
        DrawArrow(list, ImVec2(value_x - 12.0f * s - arrow * 0.5f, middle), arrow, false,
                  chosen > 0 ? accent_colour : faded);
        DrawArrow(list, ImVec2(right_edge - arrow * 0.5f, middle), arrow, true,
                  chosen + 1 < row.choices.size() ? accent_colour : faded);
      }
    }
    y += row_height + row_gap;
  }
  y -= row_gap;
  if (settings_page)
  {
    y += 16.0f * s;
    if (setting_hint)
      list->AddText(regular, body_size, ImVec2(x, y), dim_text_colour, setting_hint, nullptr,
                    inner);
    y += setting_hint_height - 16.0f * s;
  }
  y += 24.0f * s;

  // The buttons that work here: accepting and going back with the profile's
  // buttons, as the controller prints them (Xbox's green A, PlayStation's
  // cross...) or as keyboard keys, whichever was used last; the D-pad's left
  // and right on the settings.
  list->AddLine(ImVec2(x, y), ImVec2(x + inner, y), Colour(0.157f, 0.157f, 0.169f, fade),
                std::max(1.0f, s));
  y += 1.0f + 16.0f * s;
  float hint_x = x;
  const auto hint_text = [&](const char* text) {
    const ImVec2 extent = regular->CalcTextSizeA(hint_size, FLT_MAX, 0.0f, text);
    list->AddText(regular, hint_size, ImVec2(hint_x, y + badge_radius - extent.y * 0.5f),
                  dim_text_colour, text);
    hint_x += extent.x + 22.0f * s;
  };
  const auto hint = [&](const std::optional<PadHint>& pad, const std::string& key,
                        frontend::ui::PadButton fallback, const char* text) {
    hint_x += DrawHintButton(list, bold, ImVec2(hint_x, y), badge_radius * 2.0f, content.hints,
                             pad, key, fallback, fade) +
              8.0f * s;
    hint_text(text);
  };
  if (settings_page)
  {
    const ImU32 grey = Colour(0.80f, 0.80f, 0.82f, fade);
    const float arrow = badge_radius * 0.9f;
    DrawArrow(list, ImVec2(hint_x + arrow * 0.5f, y + badge_radius), arrow, false, grey);
    DrawArrow(list, ImVec2(hint_x + arrow * 2.0f, y + badge_radius), arrow, true, grey);
    hint_x += arrow * 2.5f + 8.0f * s;
    hint_text(Tr("Change"));
  }
  else if (!controls_page)
  {
    hint(content.hints.accept_pad, content.hints.accept_key, frontend::ui::PadButton::South,
         Tr("Accept"));
  }
  hint(content.hints.back_pad, content.hints.back_key, frontend::ui::PadButton::East,
       Tr("Go back"));
}
}  // namespace moderngekko::detail

namespace moderngekko::detail
{
// The notice line at the bottom of the screen, over the game or the menu.
// alpha fades it out.
inline void DrawGameMenuNotice(ImDrawList* list, ImVec2 display, const GameMenuFonts& fonts,
                               const GameMenuNotice& notice, float alpha)
{
  using namespace game_menu_draw;
  if (notice.text.empty() || display.x <= 0.0f || display.y <= 0.0f)
    return;
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  ImFont* font = fonts.regular ? fonts.regular : ImGui::GetFont();
  const float s = std::max(0.7f, std::min(display.x / 1280.0f, display.y / 720.0f));
  const float size = 17.0f * s;
  const float dot = 5.0f * s;
  const float padding_x = 18.0f * s;
  const float height = 40.0f * s;
  const std::string text = FitText(font, size, notice.text,
                                   display.x - 32.0f * s - padding_x * 2.0f - dot * 4.0f);
  const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
  const float width = padding_x * 2.0f + dot * 2.0f + 12.0f * s + extent.x;
  const ImVec2 min(std::floor((display.x - width) * 0.5f),
                   std::floor(display.y - 36.0f * s - height));
  const ImVec2 max(min.x + width, min.y + height);
  list->AddRectFilled(min, max, Colour(0.031f, 0.031f, 0.035f, 0.94f * alpha), height * 0.5f);
  list->AddRect(min, max, Colour(0.157f, 0.157f, 0.169f, alpha), height * 0.5f, 0,
                std::max(1.0f, s));
  ImVec4 colour = moderngekko::frontend::ui::LAUNCHER_ACCENT;
  if (notice.kind == GameMenuNotice::Kind::Done)
    colour = ImVec4(0.16f, 0.72f, 0.45f, 1.0f);
  else if (notice.kind == GameMenuNotice::Kind::Failed)
    colour = ImVec4(0.86f, 0.25f, 0.24f, 1.0f);
  list->AddCircleFilled(ImVec2(min.x + padding_x + dot, min.y + height * 0.5f), dot,
                        Colour(colour.x, colour.y, colour.z, alpha), 16);
  list->AddText(font, size,
                ImVec2(min.x + padding_x + dot * 2.0f + 12.0f * s,
                       min.y + (height - extent.y) * 0.5f),
                Colour(0.94f, 0.94f, 0.94f, alpha), text.c_str());
}
}  // namespace moderngekko::detail
