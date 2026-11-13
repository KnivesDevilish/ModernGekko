// The PLAY page's banner from layers (tools/launcher_banner_layout.hpp): what
// its layout.txt says, and how the layers move. The logos take turns every
// logo-seconds, the next coming in as the last goes; the mouse leans the
// layers, eased, and without it they drift; the backdrop is drawn big enough
// that moving never shows its edges, and the rest stay inside the banner.
#include "launcher_banner_layout.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace
{
using namespace moderngekko::frontend::ui;

bool Near(float value, float expected, float margin = 0.001f)
{
  if (std::abs(value - expected) < margin)
    return true;
  std::printf("got %f, expected %f\n", value, expected);
  return false;
}

std::optional<BannerLayout> Parse(const char* text)
{
  std::istringstream in(text);
  return ParseBannerLayout(in);
}
}  // namespace

int main()
{
  // The layout, back to front; comments and lines it can't read left out.
  {
    const std::optional<BannerLayout> layout = Parse("# The PLAY banner\n"
                                                     "layer background.png 10 wave\n"
                                                     "layer sonic.png 22 float  # Sonic\n"
                                                     "layer broken.png\n"
                                                     "layer too-deep.png 500\n"
                                                     "layer odd.png 5 spin\n"
                                                     "something else\n"
                                                     "logo logo-sonic-riders.png 34\n"
                                                     "logo logo-zero-gravity.png 34\n"
                                                     "logo-seconds 8\n");
    if (!layout || layout->layers.size() != 4 || layout->LogoCount() != 2 ||
        !Near(layout->logo_seconds, 8.0f))
      return 1;
    const BannerLayer& backdrop = layout->layers[0];
    const BannerLayer& sonic = layout->layers[1];
    const BannerLayer& logo = layout->layers[3];
    if (backdrop.picture != "background.png" || backdrop.motion != BannerLayer::Motion::Wave ||
        backdrop.logo || !Near(backdrop.depth, 10.0f))
      return 2;
    if (sonic.motion != BannerLayer::Motion::Float || !Near(sonic.depth, 22.0f))
      return 3;
    if (!logo.logo || logo.picture != "logo-zero-gravity.png" ||
        logo.motion != BannerLayer::Motion::Still)
      return 4;
  }
  // 10 seconds when not given or out of bounds; no layers, no layout.
  {
    const std::optional<BannerLayout> layout = Parse("layer a.png 0\nlogo-seconds 0.2\n");
    if (!layout || !Near(layout->logo_seconds, 10.0f) || layout->LogoCount() != 0)
      return 5;
    if (Parse("# nothing\nlogo-seconds 5\n") || Parse(""))
      return 6;
  }

  // The logos take turns every 10 seconds.
  {
    using banner_motion::LogoAt;
    // The first comes in at the start, alone.
    banner_motion::LogoTurn turn = LogoAt(0.0f, 2, 10.0f);
    if (turn.current != 0 || turn.leaving != -1 || !Near(turn.current_look.alpha, 0.0f))
      return 7;
    // Settled after LOGO_CHANGE.
    turn = LogoAt(5.0f, 2, 10.0f);
    if (turn.current != 0 || turn.leaving != -1 || !Near(turn.current_look.alpha, 1.0f) ||
        !Near(turn.current_look.scale, 1.0f, 0.02f))
      return 8;
    // At 10 s the first goes, and the second comes in once it has mostly
    // gone.
    turn = LogoAt(10.1f, 2, 10.0f);
    if (turn.current != 1 || turn.leaving != 0 || turn.leaving_look.alpha <= 0.0f ||
        turn.leaving_look.alpha >= 1.0f || !Near(turn.current_look.alpha, 0.0f))
      return 9;
    turn = LogoAt(10.0f + banner_motion::LOGO_GAP + 0.05f, 2, 10.0f);
    if (turn.current != 1 || turn.leaving != 0 || turn.leaving_look.alpha >= 0.2f ||
        turn.current_look.alpha <= 0.0f)
      return 10;
    // The first has gone well before the second settles.
    turn = LogoAt(10.0f + banner_motion::LOGO_OUT + 0.01f, 2, 10.0f);
    if (turn.current != 1 || turn.leaving != -1 || turn.current_look.alpha >= 1.0f)
      return 30;
    turn = LogoAt(10.0f + banner_motion::LOGO_GAP + banner_motion::LOGO_CHANGE + 0.01f, 2, 10.0f);
    if (!Near(turn.current_look.alpha, 1.0f) || !Near(turn.current_look.scale, 1.0f, 0.02f))
      return 31;
    // Back to the first at 20 s, the second going.
    turn = LogoAt(20.1f, 2, 10.0f);
    if (turn.current != 0 || turn.leaving != 1)
      return 11;
    // One logo stays, coming in once.
    turn = LogoAt(30.05f, 1, 10.0f);
    if (turn.current != 0 || turn.leaving != -1 || !Near(turn.current_look.alpha, 1.0f))
      return 12;
    // No logos: nothing to show.
    turn = LogoAt(3.0f, 0, 10.0f);
    if (turn.leaving != -1)
      return 13;
  }
  // Coming in, a logo grows a touch past its size and settles; going, it
  // fades.
  {
    using banner_motion::LogoIn;
    using banner_motion::LogoOut;
    using banner_motion::LOGO_CHANGE;
    if (!Near(LogoIn(0.0f).scale, 0.88f) || LogoIn(LOGO_CHANGE * 0.65f).scale <= 1.0f ||
        !Near(LogoIn(LOGO_CHANGE).scale, 1.0f) || !Near(LogoIn(LOGO_CHANGE).alpha, 1.0f))
      return 14;
    using banner_motion::LOGO_OUT;
    if (!Near(LogoOut(0.0f).alpha, 1.0f) || !Near(LogoOut(LOGO_OUT).alpha, 0.0f) ||
        LogoOut(LOGO_OUT * 0.3f).alpha >= 1.0f)
      return 15;
  }

  // The mouse leans it toward the pointer, within -1 to 1; without it, a
  // small drift.
  {
    using banner_motion::Lean;
    const banner_motion::Shift over = Lean(true, 0.5f, -3.0f, 4.0f);
    if (!Near(over.x, 0.5f) || !Near(over.y, -1.0f))
      return 16;
    for (float seconds = 0.0f; seconds < 30.0f; seconds += 0.37f)
    {
      const banner_motion::Shift drift = Lean(false, 0.9f, 0.9f, seconds);
      if (std::abs(drift.x) > 0.3f + 0.001f || std::abs(drift.y) > 0.2f + 0.001f)
        return 17;
    }
  }
  // Easing: nowhere in no time, most of the way after a second.
  {
    using banner_motion::Follow;
    const banner_motion::Shift from{0.0f, 0.0f};
    const banner_motion::Shift to{1.0f, -1.0f};
    if (!Near(Follow(from, to, 0.0f).x, 0.0f) || !Near(Follow(from, to, -1.0f).x, 0.0f))
      return 18;
    const banner_motion::Shift later = Follow(from, to, 1.0f);
    if (later.x < 0.99f || later.y > -0.99f)
      return 19;
    // Many short steps come to the same as one long one.
    banner_motion::Shift stepped = from;
    for (int frame = 0; frame < 30; ++frame)
      stepped = Follow(stepped, to, 1.0f / 60.0f);
    if (!Near(stepped.x, Follow(from, to, 0.5f).x, 0.0005f))
      return 20;
  }

  // The backdrop covers the banner however it leans and waves.
  {
    using namespace banner_motion;
    const float width = 1787.0f;
    const float height = 576.0f;
    const float depth = 10.0f;
    const float zoom = BackdropZoom(width, height, depth, true);
    // Its edges, leaned all the way and moved by the wave at its worst.
    const float reach = depth + WAVE_HEIGHT;
    const float top = height * 0.5f - height * 0.5f * zoom + reach;
    const float left = width * 0.5f - width * 0.5f * zoom + reach;
    if (top > 0.0f || left > 0.0f || zoom > 1.1f)
      return 21;
    if (BackdropZoom(width, height, depth, false) >= zoom)
      return 22;
    // A wave moves a point no more than WAVE_HEIGHT.
    for (float seconds = 0.0f; seconds < 5.0f; seconds += 0.13f)
    {
      const Shift wave = Wave(seconds * 300.0f, 200.0f, seconds);
      if (std::abs(wave.y) > WAVE_HEIGHT + 0.001f || std::abs(wave.x) > WAVE_HEIGHT)
        return 23;
    }
    if (std::abs(Float(1.0f)) > FLOAT_HEIGHT + 0.001f)
      return 24;
  }

  // Moving keeps a layer's part inside the banner: Sonic at the left isn't
  // cut, a logo near the top isn't either, and grown it moves down to stay
  // in; what doesn't fit moves freely.
  {
    using namespace banner_motion;
    const Box sonic{15.0f, 20.0f, 524.0f, 536.0f};
    Shift kept = KeepInside(Shift{-22.0f, 11.0f}, sonic, 1.0f, 0.0f, 0.0f, 1787.0f, 576.0f);
    if (!Near(kept.x, -15.0f) || !Near(kept.y, 11.0f))
      return 25;
    const Box logo{568.0f, 4.0f, 1244.0f, 537.0f};
    kept = KeepInside(Shift{0.0f, -17.0f}, logo, 1.0f, 906.0f, 270.5f, 1787.0f, 576.0f);
    if (!Near(kept.y, -4.0f))
      return 26;
    // Grown, its top would go past the edge: it moves down instead.
    kept = KeepInside(Shift{0.0f, -17.0f}, logo, 1.035f, 906.0f, 270.5f, 1787.0f, 576.0f);
    if (!Near(kept.y, 266.5f * 1.035f - 270.5f))
      return 27;
    kept = KeepInside(Shift{0.0f, 17.0f}, logo, 1.035f, 906.0f, 270.5f, 1787.0f, 576.0f);
    if (!Near(kept.y, 17.0f))
      return 28;
    const Box wide{-10.0f, 0.0f, 1800.0f, 100.0f};
    if (!Near(KeepInside(Shift{30.0f, 0.0f}, wide, 1.0f, 0.0f, 0.0f, 1787.0f, 576.0f).x, 30.0f))
      return 29;
  }

  std::puts("launcher banner: ok");
  return 0;
}
