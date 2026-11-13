// The game selection screen: what PLAY opens when the launcher plays more than
// one game. Two halves, one game each, with the build's art
// (Resources/game-select, see launcher_game_select_layout.hpp): the halves
// slide in, the divider drops between them with the title along it, the logos
// pop in. Left and right move between the games, choosing one flashes its
// half and fades to black before the game starts, and back closes the screen.
// A game that isn't extracted shows in grey and can't be chosen.
//
// After a few seconds on an extracted game, its intro plays (layout.txt's
// preview, from the player's own game files, see launcher_preview_video.hpp):
// the other half and the divider slide away, the video comes in over the
// whole canvas with the game's logo still on it, and when it ends, or anything
// is pressed, the halves come back.
//
// The art is a canvas (1920x1333 for SR-Source) fitted to the window's height,
// in its middle; everything here is placed in canvas pixels and drawn with
// Dear ImGui's draw list, the divider's teeth from code so they can slide.
#pragma once

#include "button_glyphs.hpp"
#include "launcher_game_select_layout.hpp"
#include "launcher_loading.hpp"
#include "launcher_look.hpp"
#include "launcher_preview_video.hpp"
#include "launcher_sound.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend::ui
{
enum class GameSelectSide
{
  Left,
  Right,
};

// What was pressed this frame, as the launcher reads its keyboard, gamepads
// and mouse.
struct GameSelectInput
{
  bool left = false;
  bool right = false;
  bool choose = false;
  bool back = false;
  bool mouse_moved = false;
  bool clicked = false;
  ImVec2 mouse{};
};

// What the button hints in the corner show, in the player's language.
struct GameSelectTexts
{
  const char* title = "";          // along the divider
  const char* switch_games = "";
  const char* choose = "";
  const char* back = "";
  const char* enter_key = "Enter";
  const char* not_installed = "";  // over a half whose game isn't extracted
  bool vertical_title = false;     // Japanese: one character under the other
  bool small_hints = false;        // Japanese: its letters run bigger
};

// What the screen plays over the music (layout.txt's sounds).
enum class GameSelectSound
{
  Open,
  Move,
  ChooseLeft,
  ChooseRight,
  Back,
  Denied,
};
inline constexpr std::array<const char*, 6> GAME_SELECT_SOUND_NAMES = {
    "open", "move", "choose-left", "choose-right", "back", "denied"};

enum class GameSelectEvent
{
  None,
  Chosen,  // the chosen game's fade to black has ended: start it
  Closed,  // back: the launcher shows again
};

// The font of the title and the hints (layout.txt's font), with the
// launcher's Japanese merged in; nothing when the build has none. Loaded with
// the launcher's other fonts.
inline ImFont* LoadGameSelectFont(const std::filesystem::path& folder)
{
  std::ifstream file(folder / "layout.txt", std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  const auto layout = ParseGameSelectLayout(text);
  std::error_code ec;
  if (!layout || layout->font.empty() || !std::filesystem::is_regular_file(folder / layout->font, ec))
    return nullptr;
  ImFont* font =
      ImGui::GetIO().Fonts->AddFontFromFileTTF((folder / layout->font).string().c_str(), 32.0f);
  if (font)
    MergeJapaneseFont(true, 32.0f);
  return font;
}

class GameSelectScreen
{
public:
  GameSelectScreen() = default;
  GameSelectScreen(const GameSelectScreen&) = delete;
  GameSelectScreen& operator=(const GameSelectScreen&) = delete;
  ~GameSelectScreen()
  {
    for (Picture* picture : AllPictures())
    {
      if (picture->texture)
        SDL_DestroyTexture(picture->texture);
      if (picture->grey)
        SDL_DestroyTexture(picture->grey);
    }
    m_video.Stop();
    DestroyVideoTextures();
  }

  // Reads Resources/game-select: its layout and pictures. Once; false when
  // something is missing, and the launcher asks with plain buttons instead.
  // The pictures are made smaller as they load when the canvas is much
  // taller than `pixel_height` (the window's height, in screen pixels, with
  // room to zoom): less to draw each frame, and SDL's software renderer
  // can't shrink a picture to under half its size as it draws it.
  bool Load(SDL_Renderer* renderer, const std::filesystem::path& folder, float pixel_height)
  {
    if (m_tried)
      return m_loaded;
    m_tried = true;
    m_renderer = renderer;
    std::ifstream file(folder / "layout.txt", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>()};
    const auto layout = ParseGameSelectLayout(text);
    if (!layout)
    {
      std::cout << "game select: no usable " << (folder / "layout.txt").string() << '\n';
      return false;
    }
    m_layout = *layout;
    const float shrink = std::clamp(pixel_height * 1.5f / m_layout.height, 0.5f, 1.0f);
    const std::array<std::pair<const char*, Picture*>, 7> wanted = {{
        {"frame", &m_frame},
        {"left-background", &m_half[0].background},
        {"left-character", &m_half[0].character},
        {"left-logo", &m_half[0].logo},
        {"right-background", &m_half[1].background},
        {"right-character", &m_half[1].character},
        {"right-logo", &m_half[1].logo},
    }};
    for (const auto& [name, picture] : wanted)
    {
      const GameSelectPicture& place = m_layout.pictures.find(name)->second;
      if (!LoadPicture(renderer, folder / place.file, name != std::string_view("frame"), shrink,
                       *picture))
        return false;
      picture->x = place.x;
      picture->y = place.y;
    }
    // Its sounds, each optional.
    for (std::size_t i = 0; i < GAME_SELECT_SOUND_NAMES.size(); ++i)
    {
      const auto sound = m_layout.sounds.find(GAME_SELECT_SOUND_NAMES[i]);
      if (sound == m_layout.sounds.end())
        continue;
      m_sounds[i] = LoadSoundEffect(folder / sound->second);
      if (!m_sounds[i])
        std::cout << "game select: can't read " << (folder / sound->second).string() << '\n';
    }
    std::cout << "game select: " << folder.string() << '\n';
    m_loaded = true;
    return true;
  }

  const GameSelectLayout& Layout() const { return m_layout; }

  // The sounds to start this frame, in order; the list empties.
  std::vector<const SoundEffect*> TakeSounds()
  {
    std::vector<const SoundEffect*> sounds;
    for (const GameSelectSound sound : m_pending_sounds)
    {
      if (const auto& effect = m_sounds[static_cast<std::size_t>(sound)])
        sounds.push_back(effect.get());
    }
    m_pending_sounds.clear();
    return sounds;
  }
  bool IsOpen() const { return m_open; }
  GameSelectSide Chosen() const { return m_selected; }

  // A game's logo for the loading screen that follows (launcher_loading.hpp),
  // the size it has here in a window `height` tall. None before Load, or for
  // a game this screen doesn't show.
  LoadingLogo LogoFor(std::string_view disc_id, float height) const
  {
    const int side = disc_id == m_layout.left_game ? 0 : disc_id == m_layout.right_game ? 1 : -1;
    if (!m_loaded || side < 0 || m_layout.height <= 0.0f || !m_half[side].logo.texture)
      return {};
    const Picture& logo = m_half[side].logo;
    const float s = height / m_layout.height;
    return {static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(logo.texture)),
            logo.width * s, logo.height * s};
  }

  // Opens the screen on `start`; a half whose game isn't ready is grey. The
  // folders are the games' (games/<disc ID>), where their intros are.
  void Open(bool left_ready, bool right_ready, GameSelectSide start, double now,
            const std::filesystem::path& left_folder = {},
            const std::filesystem::path& right_folder = {})
  {
    m_open = true;
    m_opened_at = now;
    m_last_frame = now;
    m_ready = {left_ready, right_ready};
    m_selected = start;
    m_weight_from = 0.5f;
    m_switched_at = now + 0.9;  // the selection settles as the divider lands
    m_chosen_at.reset();
    m_closing_at.reset();
    m_denied_at.reset();
    m_teeth = 0.0f;
    m_pressed_action = -1;
    m_pending_sounds = {GameSelectSound::Open};
    m_folders = {left_folder, right_folder};
    StopPreview();
    m_still_since = m_switched_at;
    m_preview_broken = {};
  }

  void Close()
  {
    m_open = false;
    StopPreview();
  }

  GameSelectEvent Update(const GameSelectInput& input, double now)
  {
    if (!m_open)
      return GameSelectEvent::None;
    const float t = static_cast<float>(now - m_opened_at);
    const float dt = std::clamp(static_cast<float>(now - m_last_frame), 0.0f, 0.1f);
    m_last_frame = now;
    m_teeth += dt * game_select_motion::TeethSpeed(
                        static_cast<float>(now - m_switched_at),
                        m_chosen_at ? static_cast<float>(now - *m_chosen_at) : -1.0f);

    if (m_chosen_at && now - *m_chosen_at >= CHOSEN_LENGTH)
    {
      Close();
      return GameSelectEvent::Chosen;
    }
    if (m_closing_at && now - *m_closing_at >= CLOSING_LENGTH)
    {
      Close();
      return GameSelectEvent::Closed;
    }
    // Presses count once the halves are in, and until a game is chosen.
    const bool counts = t >= INPUT_AFTER && !m_chosen_at && !m_closing_at;
    if (counts)
      HandleInput(input, now);
    UpdatePreview(input, counts, now);
    return GameSelectEvent::None;
  }

private:
  void HandleInput(const GameSelectInput& input, double now)
  {
    if (input.back)
    {
      m_closing_at = now;
      m_pressed_action = 2;
      m_pressed_at = now;
      m_pending_sounds.push_back(GameSelectSound::Back);
      return;
    }
    const std::optional<GameSelectSide> under_mouse = SideAt(input.mouse);
    if (input.left || input.right)
    {
      Select(input.left ? GameSelectSide::Left : GameSelectSide::Right, now);
      m_pressed_action = 0;
      m_pressed_at = now;
    }
    else if (input.mouse_moved && under_mouse)
    {
      Select(*under_mouse, now);
    }
    if (input.clicked && under_mouse)
      Select(*under_mouse, now);
    if (input.choose || (input.clicked && under_mouse))
    {
      m_pressed_action = 1;
      m_pressed_at = now;
      if (m_ready[Index(m_selected)])
      {
        m_chosen_at = now;
        m_pending_sounds.push_back(m_selected == GameSelectSide::Left ? GameSelectSound::ChooseLeft
                                                                      : GameSelectSound::ChooseRight);
      }
      else
      {
        m_denied_at = now;  // its game isn't extracted: the half shakes
        m_pending_sounds.push_back(GameSelectSound::Denied);
      }
    }
  }

public:
  void Draw(ImDrawList* list, ImVec2 origin, ImVec2 size, double now, ImFont* font,
            ImFont* label_font, const GameSelectTexts& texts, bool keyboard, PadStyle pad_style)
  {
    if (!m_open)
      return;
    using namespace game_select_motion;
    const float t = static_cast<float>(now - m_opened_at);
    const float s = size.y / m_layout.height;  // canvas pixels to screen pixels
    const float ox = origin.x + (size.x - m_layout.width * s) * 0.5f;
    const auto at = [&](float x, float y) { return ImVec2(ox + x * s, origin.y + y * s); };
    // The whole screen fades in as it opens and out when it closes.
    float shown = Clamp01(t / 0.15f);
    if (m_closing_at)
      shown *= 1.0f - EaseOut(static_cast<float>(now - *m_closing_at) / CLOSING_LENGTH);
    const float chosen_for =
        m_chosen_at ? static_cast<float>(now - *m_chosen_at) : -1.0f;  // < 0: not chosen
    const ImU32 black = IM_COL32(0, 0, 0, static_cast<int>(255 * shown));
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), black);
    const ImVec2 canvas_min = at(0.0f, 0.0f);
    const ImVec2 canvas_max = at(m_layout.width, m_layout.height);
    list->PushClipRect(canvas_min, canvas_max, true);

    const float intro = EaseOut(t / 0.9f);
    const float left_weight = LeftWeight(now);
    const float tau = 2.0f * 3.14159265f;
    // An intro playing: `room` is how far the other half and the divider
    // have gone, `video` how far its picture has come in.
    const int previewing = m_preview_at ? m_preview_side : -1;
    const float room = PreviewRoom(now);
    const float video = PreviewShown(now);
    const float away = previewing == 0   ? room * (m_layout.width - m_layout.left_tip + 60.0f)
                       : previewing == 1 ? -room * (m_layout.right_tip + 60.0f)
                                         : 0.0f;
    const auto at_away = [&](float x, float y) { return at(x + away, y); };

    // The two halves: background drifting, character floating (more when
    // selected), brighter when selected; the chosen one zooms and flashes.
    // Under an intro that covers it all, nothing to draw.
    for (int side = 0; side < 2 && video < 0.999f; ++side)
    {
      const Half& half = m_half[side];
      const bool left = side == 0;
      const bool ready = m_ready[side];
      const bool is_selected = Index(m_selected) == side;
      const float weight = left ? left_weight : 1.0f - left_weight;
      float slide = (1.0f - intro) * (left ? -m_layout.left_end : m_layout.width - m_layout.right_start);
      if (previewing >= 0 && side != previewing)
        slide += room * (left ? -(m_layout.left_end + 60.0f)
                              : m_layout.width - m_layout.right_start + 60.0f);
      if (m_denied_at && is_selected)
      {
        const float since = static_cast<float>(now - *m_denied_at);
        if (since < 0.4f)
          slide += 14.0f * std::sin(since * 60.0f) * (1.0f - since / 0.4f);
      }
      const float phase = left ? 0.0f : 1.7f;
      float zoom = 1.03f + 0.015f * std::sin(t * tau / 12.0f + phase);
      const float pan = 6.0f * std::sin(t * tau / 9.0f + phase);
      const float bob = (6.0f + 6.0f * weight) * std::sin(t * tau / 2.6f + phase);
      float grow = 1.0f + 0.035f * weight;
      float brightness = (0.5f + 0.5f * weight) * (ready ? 1.0f : 0.6f);
      if (chosen_for >= 0.0f)
      {
        if (is_selected)
        {
          const float push = EaseOut(chosen_for / 1.0f);
          zoom += 0.05f * push;
          grow += 0.06f * push;
        }
        else
        {
          brightness *= 1.0f - EaseOut(chosen_for / 0.5f);
        }
      }
      const ImVec2 clip_min = left ? ImVec2(canvas_min.x + 1.0f, canvas_min.y)
                                   : at(m_layout.right_start, 0.0f);
      const ImVec2 clip_max = left ? at(m_layout.left_end, m_layout.height)
                                   : ImVec2(canvas_max.x - 1.0f, canvas_max.y);
      list->PushClipRect(clip_min, clip_max, true);
      DrawPicture(list, half.background, !ready, zoom, slide + pan, 0.0f, brightness, shown, at);
      DrawPicture(list, half.character, !ready, grow, slide, bob, brightness, shown, at);
      if (chosen_for >= 0.0f && is_selected)
      {
        const float flash = 0.85f * ChoiceFlash(chosen_for);
        list->AddRectFilled(clip_min, clip_max,
                            IM_COL32(255, 255, 255, static_cast<int>(255 * flash * shown)));
      }
      list->PopClipRect();
    }

    // The divider drops in from above; its teeth slide, the left ones down
    // and the right ones up. The selected side's edge glows green, pulsing.
    const float drop = (1.0f - EaseOut((t - 0.3f) / 0.6f)) * -m_layout.height;
    const float landed = EaseOut((t - 0.9f) / 0.3f);
    const float pulse = 0.65f + 0.35f * std::sin(t * tau / 1.4f);
    const float glow_fade = chosen_for >= 0.0f ? 1.0f - EaseOut(chosen_for / 0.4f) : 1.0f;
    for (int side = 0; side < 2; ++side)
    {
      const float weight = side == 0 ? left_weight : 1.0f - left_weight;
      const float strength = weight * pulse * landed * glow_fade * shown * (1.0f - room);
      if (strength > 0.01f)
        DrawTeethEdge(list, side == 0, drop, strength, at_away, s);
    }
    DrawDivider(list, drop, black, at_away);

    // The title along the divider, once it has landed.
    float title_alpha = EaseOut((t - 1.0f) / 0.4f) * (0.88f + 0.12f * std::sin(t * tau / 2.2f));
    if (chosen_for >= 0.0f)
      title_alpha *= 1.0f - EaseOut(chosen_for / 0.4f);
    title_alpha *= shown * (1.0f - room);
    if (title_alpha > 0.0f && font)
      DrawTitle(list, font, texts, at_away(m_layout.Middle(), m_layout.height * 0.5f), s,
                title_alpha);

    // The intro, over the whole canvas; choosing its game flashes it too.
    if (previewing >= 0 && video > 0.0f)
    {
      DrawPreview(list, canvas_min, canvas_max, now, video * shown);
      if (chosen_for >= 0.0f)
        list->AddRectFilled(canvas_min, canvas_max,
                            IM_COL32(255, 255, 255,
                                     static_cast<int>(255 * 0.7f * ChoiceFlash(chosen_for) *
                                                      video * shown)));
    }

    // The logos pop in; the selected one breathes; a game not extracted
    // shows grey, with a label over its half. During an intro its game's logo
    // stays on it, and the other one goes.
    for (int side = 0; side < 2; ++side)
    {
      const Half& half = m_half[side];
      const bool is_selected = Index(m_selected) == side;
      const float weight = side == 0 ? left_weight : 1.0f - left_weight;
      const Pop pop = LogoPop(t - (side == 0 ? 0.8f : 0.95f));
      const float scale = pop.scale * (1.0f + weight * 0.025f * std::sin(t * tau / 1.6f));
      float dim = 0.6f + 0.4f * weight;
      if (chosen_for >= 0.0f && !is_selected)
        dim *= 1.0f - EaseOut(chosen_for / 0.5f);
      const float leaving = previewing >= 0 && side != previewing ? 1.0f - room : 1.0f;
      DrawPicture(list, half.logo, !m_ready[side], scale, 0.0f, 0.0f, dim,
                  pop.alpha * shown * leaving, at);
      if (!m_ready[side] && label_font)
      {
        const float middle = side == 0 ? m_layout.left_end * 0.5f
                                       : (m_layout.right_start + m_layout.width) * 0.5f;
        DrawLabel(list, label_font, texts.not_installed, at(middle, m_layout.height * 0.07f), s,
                  pop.alpha * shown);
      }
    }

    // The frame over it all, first to come in.
    DrawPicture(list, m_frame, false, 1.0f, 0.0f, 0.0f, 1.0f, std::min(t / 0.4f, 1.0f) * shown, at);

    // The buttons, bottom right, inside the frame's black band.
    float hints_alpha = EaseOut((t - 1.1f) / 0.4f) * shown;
    if (chosen_for >= 0.0f)
      hints_alpha *= 1.0f - EaseOut((chosen_for - 0.2f) / 0.4f);
    if (hints_alpha > 0.0f && font)
      DrawHints(list, font, texts, keyboard, pad_style, now, at, s, hints_alpha);

    list->PopClipRect();
    // Then everything goes to black before the game starts.
    if (chosen_for > 0.8f)
    {
      const float dark = EaseInOut((chosen_for - 0.8f) / 0.6f);
      list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                          IM_COL32(0, 0, 0, static_cast<int>(255 * dark)));
    }
  }

private:
  static constexpr float INPUT_AFTER = 0.5f;
  static constexpr double CHOSEN_LENGTH = 1.4;   // flash, then the fade to black
  static constexpr double CLOSING_LENGTH = 0.25;
  static constexpr double PREVIEW_PREPARE = 0.6;  // still this long: its video gets ready
  static constexpr double PREVIEW_AFTER = 5.0;    // still this long: its intro plays
  static constexpr double PREVIEW_CLOSE = 0.7;    // the halves coming back
  static constexpr int BLUR_WIDTH = 16;           // the blurred copy behind the video
  static constexpr int BLUR_HEIGHT = 12;

  // The intros: getting the selected game's video ready while the player
  // stays on it, playing it, and closing it when it ends or anything moves.
  void UpdatePreview(const GameSelectInput& input, bool counts, double now)
  {
    const bool moved = input.left || input.right || input.back || input.mouse_moved;
    if (counts && (moved || input.choose || input.clicked))
    {
      m_still_since = now;
      if (moved)
        ClosePreview(now);  // choosing leaves it playing under the flash
    }
    if (m_chosen_at || m_closing_at)
      return;
    if (m_preview_closing_at)
    {
      if (now - *m_preview_closing_at < PREVIEW_CLOSE)
        return;
      StopPreview();
      m_still_since = now;  // and another one after a while
    }
    if (m_preview_at)
    {
      const GameSelectPreview& preview = *m_layout.previews[m_preview_side];
      if (game_select_preview::MomentAt(preview, m_video.FrameRate(), VideoSeconds(now)).ended)
        ClosePreview(now);
      return;
    }
    const int side = Index(m_selected);
    const auto& preview = m_layout.previews[side];
    if (!preview || !m_ready[side] || m_preview_broken[side] || m_folders[side].empty())
      return;
    const char* name = side == 0 ? "left" : "right";
    if (m_video_side != side && now - m_still_since >= PREVIEW_PREPARE)
    {
      m_video.Stop();
      m_video_side = -1;
      const auto source = FindVideoSource(m_folders[side], preview->video);
      if (!source)
      {
        std::cout << "game select: no intro on the " << name << ", "
                  << (m_folders[side] / preview->video.substr(0, preview->video.find(':'))).string()
                  << " has no " << preview->video << '\n';
        m_preview_broken[side] = true;
        return;
      }
      m_video.Start(*source, preview->parts);
      m_video_side = side;
    }
    if (m_video_side != side)
      return;
    if (m_video.Failed())
    {
      std::cout << "game select: can't play " << preview->video << " (the intro on the " << name
                << ")\n";
      m_preview_broken[side] = true;
      m_video.Stop();
      m_video_side = -1;
      return;
    }
    if (m_video.Ready() && now - m_still_since >= PREVIEW_AFTER)
    {
      m_preview_at = now;
      m_preview_side = side;
    }
  }

  void ClosePreview(double now)
  {
    if (!m_preview_at || m_preview_closing_at)
      return;
    m_preview_room_from = PreviewRoom(now);
    m_preview_shown_from = PreviewShown(now);
    m_preview_closing_at = now;
  }

  void StopPreview()
  {
    m_video.Stop();
    m_video_side = -1;
    m_preview_at.reset();
    m_preview_closing_at.reset();
  }

  // How far the other half and the divider have gone for the intro, 0..1.
  float PreviewRoom(double now) const
  {
    using game_select_motion::EaseInOut;
    if (!m_preview_at)
      return 0.0f;
    if (m_preview_closing_at)
      return m_preview_room_from *
             (1.0f - EaseInOut(static_cast<float>(now - *m_preview_closing_at) / 0.55f));
    return EaseInOut(static_cast<float>(now - *m_preview_at) / 0.4f);
  }

  // How far the intro's picture has come in, 0..1.
  float PreviewShown(double now) const
  {
    using game_select_motion::EaseInOut;
    if (!m_preview_at)
      return 0.0f;
    if (m_preview_closing_at)
      return m_preview_shown_from *
             (1.0f - EaseInOut(static_cast<float>(now - *m_preview_closing_at) / PREVIEW_CLOSE));
    return EaseInOut(static_cast<float>(now - *m_preview_at - 0.1) / 0.45f);
  }

  // Where in its video the intro is: it starts as its picture comes in.
  double VideoSeconds(double now) const
  {
    return m_preview_at ? std::max(0.0, now - *m_preview_at - 0.1) : 0.0;
  }

  // The intro: the frame due now, as large as fits in the canvas, over a
  // blurred, darker copy of itself that fills it; its fades over both.
  void DrawPreview(ImDrawList* list, ImVec2 canvas_min, ImVec2 canvas_max, double now, float alpha)
  {
    const GameSelectPreview& preview = *m_layout.previews[m_preview_side];
    const auto moment =
        game_select_preview::MomentAt(preview, m_video.FrameRate(), VideoSeconds(now));
    if (m_video.Take(moment.frame, m_rgba))
      UploadFrame();
    if (!m_video_texture || alpha <= 0.0f)
      return;
    const auto image = [&](SDL_Texture* texture, ImVec2 min, ImVec2 max, float brightness) {
      list->AddImage(
          ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture))), min,
          max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
          ImGui::ColorConvertFloat4ToU32(ImVec4(brightness, brightness, brightness, alpha)));
    };
    if (m_blur_texture)
      image(m_blur_texture, canvas_min, canvas_max, 0.4f);
    else
      list->AddRectFilled(canvas_min, canvas_max, IM_COL32(0, 0, 0, static_cast<int>(255 * alpha)));
    const float width = canvas_max.x - canvas_min.x;
    const float height = canvas_max.y - canvas_min.y;
    float w = width;
    float h = width / preview.shape;
    if (h > height)
    {
      h = height;
      w = height * preview.shape;
    }
    const ImVec2 min(canvas_min.x + (width - w) * 0.5f, canvas_min.y + (height - h) * 0.5f);
    image(m_video_texture, min, ImVec2(min.x + w, min.y + h), 1.0f);
    if (moment.black > 0.0f)
      list->AddRectFilled(canvas_min, canvas_max,
                          IM_COL32(0, 0, 0, static_cast<int>(255 * alpha * moment.black)));
    if (moment.colour > 0.0f)
    {
      const auto& c = preview.end_colour;
      list->AddRectFilled(canvas_min, canvas_max,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], alpha * moment.colour)));
    }
  }

  // The frame just taken into the video's texture, and a tiny copy of it into
  // the blurred one (drawn stretched, it blurs).
  void UploadFrame()
  {
    const int w = m_video.Width();
    const int h = m_video.Height();
    if (w <= 0 || h <= 0 || m_rgba.size() < static_cast<std::size_t>(w) * h * 4 || !m_renderer)
      return;
    if (!m_video_texture || m_texture_width != w || m_texture_height != h)
    {
      DestroyVideoTextures();
      // pl_mpeg leaves the fourth byte alone: RGBX.
      m_video_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBX32,
                                          SDL_TEXTUREACCESS_STREAMING, w, h);
      m_blur_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBX32,
                                         SDL_TEXTUREACCESS_STREAMING, BLUR_WIDTH, BLUR_HEIGHT);
      // Without an alpha channel SDL would draw them opaque, over the fade.
      for (SDL_Texture* texture : {m_video_texture, m_blur_texture})
      {
        if (!texture)
          continue;
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
      }
      m_texture_width = w;
      m_texture_height = h;
    }
    if (!m_video_texture)
      return;
    SDL_UpdateTexture(m_video_texture, nullptr, m_rgba.data(), w * 4);
    if (!m_blur_texture)
      return;
    m_blur.assign(static_cast<std::size_t>(BLUR_WIDTH) * BLUR_HEIGHT * 4, 255);
    for (int by = 0; by < BLUR_HEIGHT; ++by)
    {
      for (int bx = 0; bx < BLUR_WIDTH; ++bx)
      {
        std::array<unsigned, 3> sum{};
        unsigned count = 0;
        for (int y = by * h / BLUR_HEIGHT; y < (by + 1) * h / BLUR_HEIGHT; y += 2)
        {
          const std::uint8_t* row = m_rgba.data() + static_cast<std::size_t>(y) * w * 4;
          for (int x = bx * w / BLUR_WIDTH; x < (bx + 1) * w / BLUR_WIDTH; x += 2)
          {
            sum[0] += row[x * 4];
            sum[1] += row[x * 4 + 1];
            sum[2] += row[x * 4 + 2];
            ++count;
          }
        }
        std::uint8_t* out = m_blur.data() + (static_cast<std::size_t>(by) * BLUR_WIDTH + bx) * 4;
        for (int c = 0; c < 3; ++c)
          out[c] = static_cast<std::uint8_t>(count ? sum[c] / count : 0);
      }
    }
    SDL_UpdateTexture(m_blur_texture, nullptr, m_blur.data(), BLUR_WIDTH * 4);
  }

  void DestroyVideoTextures()
  {
    if (m_video_texture)
      SDL_DestroyTexture(m_video_texture);
    if (m_blur_texture)
      SDL_DestroyTexture(m_blur_texture);
    m_video_texture = nullptr;
    m_blur_texture = nullptr;
    m_texture_width = 0;
    m_texture_height = 0;
  }

  struct Picture
  {
    SDL_Texture* texture = nullptr;
    SDL_Texture* grey = nullptr;  // for a game not extracted
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
  };
  struct Half
  {
    Picture background;
    Picture character;
    Picture logo;
  };

  static int Index(GameSelectSide side) { return side == GameSelectSide::Left ? 0 : 1; }

  std::array<Picture*, 7> AllPictures()
  {
    return {&m_frame,           &m_half[0].background, &m_half[0].character, &m_half[0].logo,
            &m_half[1].background, &m_half[1].character, &m_half[1].logo};
  }

  static bool LoadPicture(SDL_Renderer* renderer, const std::filesystem::path& path,
                          bool with_grey, float shrink, Picture& picture)
  {
    SDL_Surface* loaded = SDL_LoadPNG(path.string().c_str());
    if (!loaded)
    {
      std::cout << "game select: can't load " << path.string() << ": " << SDL_GetError() << '\n';
      return false;
    }
    SDL_Surface* pixels = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!pixels)
      return false;
    // Its size on the canvas stays the file's, whatever the texture's.
    picture.width = static_cast<float>(pixels->w);
    picture.height = static_cast<float>(pixels->h);
    if (shrink < 0.99f)
    {
      SDL_Surface* smaller = SDL_ScaleSurface(
          pixels, std::max(1, static_cast<int>(std::lround(pixels->w * shrink))),
          std::max(1, static_cast<int>(std::lround(pixels->h * shrink))), SDL_SCALEMODE_LINEAR);
      if (smaller)
      {
        SDL_DestroySurface(pixels);
        pixels = smaller;
      }
    }
    picture.texture = SDL_CreateTextureFromSurface(renderer, pixels);
    if (picture.texture && with_grey && SDL_LockSurface(pixels))
    {
      // The grey copy: each pixel's brightness, a little darker.
      for (int y = 0; y < pixels->h; ++y)
      {
        auto* row = static_cast<std::uint8_t*>(pixels->pixels) + y * pixels->pitch;
        for (int x = 0; x < pixels->w; ++x)
        {
          std::uint8_t* p = row + x * 4;
          const int grey = (p[0] * 54 + p[1] * 183 + p[2] * 19) >> 8;
          p[0] = p[1] = p[2] = static_cast<std::uint8_t>(grey * 0.8f);
        }
      }
      SDL_UnlockSurface(pixels);
      picture.grey = SDL_CreateTextureFromSurface(renderer, pixels);
    }
    SDL_DestroySurface(pixels);
    return picture.texture != nullptr;
  }

  // A picture scaled by `zoom` about its middle, moved by (dx, dy) canvas
  // pixels, darkened to `brightness`.
  template <typename At>
  static void DrawPicture(ImDrawList* list, const Picture& picture, bool grey, float zoom,
                          float dx, float dy, float brightness, float alpha, const At& at)
  {
    SDL_Texture* texture = grey && picture.grey ? picture.grey : picture.texture;
    if (!texture || alpha <= 0.0f)
      return;
    const float cx = picture.x + picture.width * 0.5f;
    const float cy = picture.y + picture.height * 0.5f;
    const float half_w = picture.width * 0.5f * zoom;
    const float half_h = picture.height * 0.5f * zoom;
    const float b = std::clamp(brightness, 0.0f, 1.0f);
    list->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture))),
                   at(cx - half_w + dx, cy - half_h + dy), at(cx + half_w + dx, cy + half_h + dy),
                   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                   ImGui::ColorConvertFloat4ToU32(ImVec4(b, b, b, std::clamp(alpha, 0.0f, 1.0f))));
  }

  // 1 when the left half is selected, 0 for the right, eased between.
  float LeftWeight(double now) const
  {
    const float target = m_selected == GameSelectSide::Left ? 1.0f : 0.0f;
    const float x = game_select_motion::EaseInOut(static_cast<float>(now - m_switched_at) / 0.3f);
    return m_weight_from + (target - m_weight_from) * x;
  }

  void Select(GameSelectSide side, double now)
  {
    if (side == m_selected)
      return;
    m_weight_from = LeftWeight(now);
    m_selected = side;
    m_switched_at = now;
    m_denied_at.reset();
    m_pending_sounds.push_back(GameSelectSound::Move);
  }

  // Which half the mouse is over, in screen pixels (as last drawn).
  std::optional<GameSelectSide> SideAt(ImVec2 mouse) const
  {
    if (m_drawn_scale <= 0.0f || mouse.y < m_drawn_origin_y ||
        mouse.y > m_drawn_origin_y + m_layout.height * m_drawn_scale)
      return std::nullopt;
    const float x = (mouse.x - m_drawn_left) / m_drawn_scale;
    if (x >= 0.0f && x < m_layout.left_tip)
      return GameSelectSide::Left;
    if (x > m_layout.right_tip && x <= m_layout.width)
      return GameSelectSide::Right;
    return std::nullopt;
  }

public:
  // Where the canvas went on screen, for SideAt: called by the launcher with
  // the same origin and size as Draw.
  void Place(ImVec2 origin, ImVec2 size)
  {
    m_drawn_scale = size.y / m_layout.height;
    m_drawn_left = origin.x + (size.x - m_layout.width * m_drawn_scale) * 0.5f;
    m_drawn_origin_y = origin.y;
  }

private:
  // One side's teeth as triangles: the left ones point left, the right ones
  // right, repeating every period; `shift` moves them along.
  template <typename Each>
  void ForEachTooth(bool left, float drop, Each&& each) const
  {
    const float period = m_layout.period;
    const float offset = left ? m_teeth : -m_teeth;
    const float first = -2.0f * period + std::fmod(std::fmod(offset, period) + period, period);
    const int count = static_cast<int>((m_layout.height + 4.0f * period) / period) + 1;
    const float tip = left ? m_layout.left_tip : m_layout.right_tip;
    const float valley = left ? m_layout.left_valley : m_layout.right_valley;
    for (int k = 0; k < count; ++k)
    {
      const float y = first + static_cast<float>(k) * period + drop;
      each(valley, y, tip, y + period * 0.5f, valley, y + period);
    }
  }

  template <typename At>
  void DrawDivider(ImDrawList* list, float drop, ImU32 black, const At& at) const
  {
    for (const bool left : {true, false})
    {
      ForEachTooth(left, drop, [&](float x0, float y0, float x1, float y1, float x2, float y2) {
        list->AddTriangleFilled(at(x0, y0), at(x1, y1), at(x2, y2), black);
      });
    }
    list->AddRectFilled(at(m_layout.left_valley - 0.5f, drop - 2.0f * m_layout.period),
                        at(m_layout.right_valley + 0.5f,
                           drop + m_layout.height + 2.0f * m_layout.period),
                        black);
  }

  // The glow along one side's teeth: a green line and a soft wider one; the
  // black teeth drawn after cover their inner half.
  template <typename At>
  void DrawTeethEdge(ImDrawList* list, bool left, float drop, float strength, const At& at,
                     float scale) const
  {
    const ImVec4 accent = LAUNCHER_ACCENT;
    const auto colour = [&](float alpha) {
      return ImGui::ColorConvertFloat4ToU32(
          ImVec4(accent.x, accent.y, accent.z, std::clamp(alpha, 0.0f, 1.0f)));
    };
    std::vector<ImVec2> edge;
    ForEachTooth(left, drop, [&](float x0, float y0, float x1, float y1, float, float) {
      edge.push_back(at(x0, y0));
      edge.push_back(at(x1, y1));
    });
    if (edge.empty())
      return;
    list->AddPolyline(edge.data(), static_cast<int>(edge.size()), colour(strength * 0.28f),
                      ImDrawFlags_None, std::max(4.0f, 26.0f * scale));
    list->AddPolyline(edge.data(), static_cast<int>(edge.size()), colour(strength * 0.5f),
                      ImDrawFlags_None, std::max(3.0f, 14.0f * scale));
    list->AddPolyline(edge.data(), static_cast<int>(edge.size()), colour(strength),
                      ImDrawFlags_None, std::max(2.0f, 7.0f * scale));
  }

  // Turns the vertices drawn since `first` by a quarter turn clockwise about
  // `pivot`: text that reads from top to bottom.
  static void QuarterTurn(ImDrawList* list, int first, ImVec2 pivot)
  {
    for (int i = first; i < list->VtxBuffer.Size; ++i)
    {
      ImVec2& p = list->VtxBuffer[i].pos;
      const ImVec2 d(p.x - pivot.x, p.y - pivot.y);
      p = ImVec2(pivot.x - d.y, pivot.y + d.x);
    }
  }

  static std::vector<std::string> Characters(std::string_view text)
  {
    std::vector<std::string> characters;
    for (std::size_t i = 0; i < text.size();)
    {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      const std::size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
      characters.emplace_back(text.substr(i, length));
      i += length;
    }
    return characters;
  }

  static void DrawTitle(ImDrawList* list, ImFont* font, const GameSelectTexts& texts,
                        ImVec2 centre, float scale, float alpha)
  {
    const ImU32 colour = IM_COL32(245, 245, 245, static_cast<int>(255 * std::clamp(alpha, 0.0f, 1.0f)));
    const std::vector<std::string> characters = Characters(texts.title);
    if (texts.vertical_title)
    {
      // One character under the other; the long vowel mark turns upright.
      const float size = 50.0f * scale;
      const float step = size * 1.08f;
      float y = centre.y - step * static_cast<float>(characters.size()) * 0.5f;
      for (const std::string& character : characters)
      {
        const ImVec2 glyph = font->CalcTextSizeA(size, FLT_MAX, 0.0f, character.c_str());
        const ImVec2 middle(centre.x, y + step * 0.5f);
        const int first = list->VtxBuffer.Size;
        list->AddText(font, size, ImVec2(middle.x - glyph.x * 0.5f, middle.y - glyph.y * 0.5f),
                      colour, character.c_str());
        if (character == "\xE3\x83\xBC")  // ー
          QuarterTurn(list, first, middle);
        y += step;
      }
      return;
    }
    // Spaced letters on one line, turned to read from top to bottom.
    const float size = 74.0f * scale;
    const float spacing = size * 0.07f;
    float width = 0.0f;
    for (const std::string& character : characters)
      width += font->CalcTextSizeA(size, FLT_MAX, 0.0f, character.c_str()).x + spacing;
    width -= spacing;
    const float height = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "A").y;
    const int first = list->VtxBuffer.Size;
    float x = centre.x - width * 0.5f;
    for (const std::string& character : characters)
    {
      list->AddText(font, size, ImVec2(x, centre.y - height * 0.5f), colour, character.c_str());
      x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, character.c_str()).x + spacing;
    }
    QuarterTurn(list, first, centre);
  }

  static void DrawLabel(ImDrawList* list, ImFont* font, const char* text, ImVec2 centre,
                        float scale, float alpha)
  {
    const float size = std::max(12.0f, 34.0f * scale);
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 padding(size * 0.6f, size * 0.2f);
    const ImVec2 min(centre.x - text_size.x * 0.5f - padding.x, centre.y - text_size.y * 0.5f - padding.y);
    const ImVec2 max(centre.x + text_size.x * 0.5f + padding.x, centre.y + text_size.y * 0.5f + padding.y);
    const int a = static_cast<int>(255 * std::clamp(alpha, 0.0f, 1.0f));
    list->AddRectFilled(min, max, IM_COL32(0, 0, 0, a * 3 / 4), (max.y - min.y) * 0.5f);
    list->AddText(font, size, ImVec2(min.x + padding.x, min.y + padding.y),
                  IM_COL32(255, 191, 77, a), text);
  }

  template <typename At>
  void DrawHints(ImDrawList* list, ImFont* font, const GameSelectTexts& texts, bool keyboard,
                 PadStyle pad_style, double now, const At& at, float scale, float alpha) const
  {
    const float band = (m_layout.band_bottom - m_layout.band_top) * scale;
    const float glyph = std::max(8.0f, std::round(band * 0.74f));
    const float text_size = band * (texts.small_hints ? 0.62f : 1.05f);
    const float middle = at(0.0f, (m_layout.band_top + m_layout.band_bottom) * 0.5f).y;
    float x = at(m_layout.hints_right, 0.0f).x;
    const ImU32 text_colour = IM_COL32(235, 235, 238, static_cast<int>(255 * alpha));
    // From the right: Back, Choose, Switch.
    for (int action = 2; action >= 0; --action)
    {
      const char* label = action == 0 ? texts.switch_games : action == 1 ? texts.choose : texts.back;
      const ImVec2 label_size = font->CalcTextSizeA(text_size, FLT_MAX, 0.0f, label);
      x -= label_size.x;
      list->AddText(font, text_size, ImVec2(x, middle - label_size.y * 0.5f + band * 0.04f),
                    text_colour, label);
      x -= glyph * 0.3f;
      // The button just pressed grows for a moment.
      float grow = 1.0f;
      if (m_pressed_action == action && now - m_pressed_at < 0.25)
        grow = 1.0f + 0.3f * (1.0f - static_cast<float>(now - m_pressed_at) / 0.25f);
      const float h = glyph * grow;
      const auto top = [&](float height) { return middle - height * 0.5f; };
      if (action == 0)
      {
        for (const bool left : {false, true})
        {
          x -= h;
          if (keyboard)
            DrawArrowKeyCap(list, ImVec2(x, top(h)), h, left, alpha);
          else
            DrawDpadArrow(list, ImVec2(x, top(h)), h, left, alpha);
          x -= glyph * 0.15f;
        }
      }
      else if (keyboard)
      {
        const std::string key = action == 1 ? texts.enter_key : "Esc";
        x -= KeyCapWidth(font, h, key);
        DrawKeyCap(list, font, ImVec2(x, top(h)), h, key, alpha);
      }
      else
      {
        const PadButton button = action == 1 ? PadButton::South : PadButton::East;
        x -= PadButtonWidth(font, h, pad_style, button);
        DrawPadButton(list, font, ImVec2(x, top(h)), h, pad_style, button, alpha);
      }
      x -= glyph * 0.9f;  // the space between two hints
    }
  }

  bool m_tried = false;
  bool m_loaded = false;
  GameSelectLayout m_layout;
  Picture m_frame;
  std::array<Half, 2> m_half;

  bool m_open = false;
  double m_opened_at = 0.0;
  double m_last_frame = 0.0;
  std::array<bool, 2> m_ready{};
  GameSelectSide m_selected = GameSelectSide::Right;
  float m_weight_from = 0.5f;
  double m_switched_at = 0.0;
  std::optional<double> m_chosen_at;
  std::optional<double> m_closing_at;
  std::optional<double> m_denied_at;
  float m_teeth = 0.0f;
  int m_pressed_action = -1;  // 0 switch, 1 choose, 2 back
  double m_pressed_at = 0.0;
  std::array<std::shared_ptr<const SoundEffect>, GAME_SELECT_SOUND_NAMES.size()> m_sounds;
  std::vector<GameSelectSound> m_pending_sounds;
  float m_drawn_scale = 0.0f;
  float m_drawn_left = 0.0f;
  float m_drawn_origin_y = 0.0f;

  // The intros.
  SDL_Renderer* m_renderer = nullptr;
  std::array<std::filesystem::path, 2> m_folders;
  std::array<bool, 2> m_preview_broken{};  // its video can't be played: no intro
  double m_still_since = 0.0;              // the last press, or the last selection
  PreviewVideo m_video;
  int m_video_side = -1;                   // whose video m_video decodes
  std::optional<double> m_preview_at;      // when the intro began
  std::optional<double> m_preview_closing_at;
  int m_preview_side = 0;
  float m_preview_room_from = 0.0f;
  float m_preview_shown_from = 0.0f;
  SDL_Texture* m_video_texture = nullptr;
  SDL_Texture* m_blur_texture = nullptr;
  int m_texture_width = 0;
  int m_texture_height = 0;
  std::vector<std::uint8_t> m_rgba;
  std::vector<std::uint8_t> m_blur;
};
}  // namespace moderngekko::frontend::ui
