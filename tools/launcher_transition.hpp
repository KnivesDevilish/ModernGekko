// The way from the launcher to the game and back, as times and amounts (what
// launcher_loading.hpp draws with them). The launcher doesn't jump to another
// window: it goes black (it is already, after the game selection screen) and
// shows its loading screen while the game starts, then steps aside once the
// game's window, opened where the launcher is, has its first picture
// (launcher_handoff.hpp). The music fades out meanwhile. When the game
// closes, the launcher comes back from black. Apart from Dear ImGui and SDL,
// so tests/launcher_transition_test.cpp can check it.
#pragma once

#include "launcher_game_select_layout.hpp"

#include <algorithm>
#include <optional>

namespace moderngekko::frontend::ui
{
class GameTransition
{
public:
  static constexpr double LEAVE_LENGTH = 0.35;   // the launcher fading to black
  static constexpr double ART_DELAY = 0.1;       // then the loading screen comes in
  static constexpr double ART_IN = 0.45;
  static constexpr double ART_OUT = 0.25;        // the game is shown: it goes
  static constexpr double MUSIC_FADE = 1.2;
  static constexpr double WAIT_LIMIT = 40.0;     // no sign of the game: step aside anyway
  static constexpr double RETURN_LENGTH = 0.6;   // back from black

  // The game has started. `from_black`: the launcher is black already;
  // otherwise it fades to black first.
  void Start(double now, bool from_black)
  {
    m_started_at = now;
    m_from_black = from_black;
    m_shown_at.reset();
    m_back_at.reset();
  }

  // The game can be seen (the runner said so).
  void GameShown(double now)
  {
    if (m_started_at && !m_shown_at && !m_back_at)
      m_shown_at = now;
  }
  bool IsGameShown() const { return m_shown_at.has_value(); }

  // Time for the launcher to step aside: black, and the game seen long enough
  // for the loading screen to go, or waited for too long.
  bool TimeToHide(double now) const
  {
    if (!m_started_at || m_back_at || now < BlackAt())
      return false;
    return (m_shown_at && now - *m_shown_at >= ART_OUT) || now - *m_started_at >= WAIT_LIMIT;
  }

  // The game has closed, or didn't start: the launcher comes back from as
  // black as it is.
  void Back(double now)
  {
    if (!m_started_at || m_back_at)
      return;
    m_back_from = Black(now);
    m_art_at_back = ArtAlpha(now);
    m_back_at = now;
  }

  // Something to draw over the launcher, which takes no input meanwhile.
  bool Active(double now) const
  {
    return m_started_at && (!m_back_at || now - *m_back_at < RETURN_LENGTH);
  }
  // All black: the launcher's pages needn't be drawn.
  bool CoversLauncher(double now) const { return Active(now) && Black(now) >= 1.0f; }

  // How black the launcher is, 0 to 1.
  float Black(double now) const
  {
    using namespace game_select_motion;
    if (!m_started_at)
      return 0.0f;
    if (m_back_at)
      return m_back_from * (1.0f - EaseOut(Seconds(now - *m_back_at) / RETURN_LENGTH));
    if (m_from_black)
      return 1.0f;
    return EaseInOut(Seconds(now - *m_started_at) / LEAVE_LENGTH);
  }

  // How much of the loading screen shows, 0 to 1, and how long it has shown.
  float ArtAlpha(double now) const
  {
    using namespace game_select_motion;
    if (!m_started_at)
      return 0.0f;
    if (m_back_at)
      return m_art_at_back * (1.0f - Clamp01(Seconds(now - *m_back_at) / ART_OUT));
    float alpha = Clamp01(Seconds(now - BlackAt() - ART_DELAY) / ART_IN);
    if (m_shown_at)
      alpha *= 1.0f - Clamp01(Seconds(now - *m_shown_at) / ART_OUT);
    return alpha;
  }
  float ArtSeconds(double now) const
  {
    return m_started_at ? Seconds(now - BlackAt() - ART_DELAY) : 0.0f;
  }

  // The launcher's music, 1 (its volume) to 0, while the game starts.
  float MusicLevel(double now) const
  {
    if (!m_started_at || m_back_at)
      return 1.0f;
    return 1.0f - game_select_motion::EaseInOut(Seconds(now - *m_started_at) / MUSIC_FADE);
  }

private:
  static float Seconds(double seconds) { return static_cast<float>(seconds); }
  // When the launcher is all black.
  double BlackAt() const { return *m_started_at + (m_from_black ? 0.0 : LEAVE_LENGTH); }

  std::optional<double> m_started_at;
  bool m_from_black = false;
  std::optional<double> m_shown_at;
  std::optional<double> m_back_at;
  float m_back_from = 1.0f;
  float m_art_at_back = 0.0f;
};
}  // namespace moderngekko::frontend::ui
