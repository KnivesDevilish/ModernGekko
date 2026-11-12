// The way from the launcher to the game and back
// (tools/launcher_transition.hpp): from a page it fades to black first, from
// the game selection screen it is black at once; the loading screen comes in
// once it is black and goes when the game can be seen, and only then does the
// launcher step aside, or after waiting too long. The music fades out
// meanwhile. When the game closes the launcher comes back from as black as
// it was, and takes input again after.
#include "launcher_transition.hpp"

#include <cmath>
#include <cstdio>

namespace
{
using moderngekko::frontend::ui::GameTransition;

bool Near(float value, float expected)
{
  if (std::abs(value - expected) < 0.001f)
    return true;
  std::printf("got %f, expected %f\n", value, expected);
  return false;
}
}  // namespace

int main()
{
  // Nothing before a game starts.
  {
    const GameTransition idle;
    if (idle.Active(5.0) || idle.CoversLauncher(5.0) || !Near(idle.Black(5.0), 0.0f) ||
        !Near(idle.MusicLevel(5.0), 1.0f) || idle.TimeToHide(5.0))
      return 1;
  }

  // From a page: black after LEAVE_LENGTH, then the loading screen.
  GameTransition page;
  page.Start(10.0, false);
  if (!page.Active(10.0) || page.CoversLauncher(10.1) || !Near(page.Black(10.0), 0.0f) ||
      !Near(page.Black(10.0 + GameTransition::LEAVE_LENGTH / 2), 0.5f) ||
      !Near(page.Black(10.0 + GameTransition::LEAVE_LENGTH), 1.0f) ||
      !page.CoversLauncher(10.0 + GameTransition::LEAVE_LENGTH))
    return 2;
  const double black_at = 10.0 + GameTransition::LEAVE_LENGTH;
  const double art_full = black_at + GameTransition::ART_DELAY + GameTransition::ART_IN;
  if (!Near(page.ArtAlpha(black_at), 0.0f) || !Near(page.ArtAlpha(art_full), 1.0f) ||
      !Near(page.ArtSeconds(black_at + GameTransition::ART_DELAY), 0.0f))
    return 3;
  // The music: all of it at first, none after MUSIC_FADE.
  if (!Near(page.MusicLevel(10.0), 1.0f) ||
      !Near(page.MusicLevel(10.0 + GameTransition::MUSIC_FADE / 2), 0.5f) ||
      !Near(page.MusicLevel(10.0 + GameTransition::MUSIC_FADE), 0.0f))
    return 4;

  // The game seen while still going black: the launcher stays until it is
  // black and the loading screen has gone (ART_OUT after).
  GameTransition quick;
  quick.Start(0.0, false);
  quick.GameShown(0.05);
  if (!quick.IsGameShown() || quick.TimeToHide(0.05 + GameTransition::ART_OUT) ||
      quick.TimeToHide(GameTransition::LEAVE_LENGTH - 0.01) ||
      !quick.TimeToHide(GameTransition::LEAVE_LENGTH))
    return 5;

  // From the selection screen: black at once. Seen at 3 s: the loading
  // screen fades out, and the launcher steps aside ART_OUT later.
  GameTransition chosen;
  chosen.Start(0.0, true);
  if (!chosen.CoversLauncher(0.0) || !Near(chosen.Black(0.0), 1.0f) || chosen.TimeToHide(2.9))
    return 6;
  chosen.GameShown(3.0);
  if (!Near(chosen.ArtAlpha(3.0), 1.0f) ||
      !Near(chosen.ArtAlpha(3.0 + GameTransition::ART_OUT / 2), 0.5f) ||
      chosen.TimeToHide(3.0 + GameTransition::ART_OUT - 0.01) ||
      !chosen.TimeToHide(3.0 + GameTransition::ART_OUT))
    return 7;
  // Seen again later changes nothing.
  chosen.GameShown(4.0);
  if (!chosen.TimeToHide(3.0 + GameTransition::ART_OUT))
    return 8;

  // No sign of the game: the launcher steps aside after WAIT_LIMIT.
  GameTransition slow;
  slow.Start(0.0, true);
  if (slow.TimeToHide(GameTransition::WAIT_LIMIT - 0.1) || !slow.TimeToHide(GameTransition::WAIT_LIMIT))
    return 9;

  // The game closed while the launcher was hidden: back from all black,
  // input again after RETURN_LENGTH.
  chosen.Back(60.0);
  if (!chosen.Active(60.0) || !Near(chosen.Black(60.0), 1.0f) || chosen.TimeToHide(60.0) ||
      chosen.Black(60.0 + GameTransition::RETURN_LENGTH / 2) >= 1.0f ||
      !Near(chosen.Black(60.0 + GameTransition::RETURN_LENGTH), 0.0f) ||
      chosen.Active(60.0 + GameTransition::RETURN_LENGTH) ||
      !Near(chosen.MusicLevel(60.0), 1.0f) || !Near(chosen.ArtAlpha(60.0), 0.0f))
    return 10;
  // A second Back, or the game seen after it, changes nothing.
  chosen.Back(61.0);
  chosen.GameShown(61.0);
  if (chosen.Active(60.0 + GameTransition::RETURN_LENGTH))
    return 11;

  // Failed while going black, halfway: it comes back from halfway, and the
  // loading screen it had (none yet) stays gone.
  GameTransition failed;
  failed.Start(0.0, false);
  failed.Back(GameTransition::LEAVE_LENGTH / 2);
  if (!Near(failed.Black(GameTransition::LEAVE_LENGTH / 2), 0.5f) ||
      failed.CoversLauncher(GameTransition::LEAVE_LENGTH / 2) ||
      !Near(failed.ArtAlpha(1.0), 0.0f))
    return 12;
  // Failed during the loading screen: it fades out with the black.
  GameTransition loading;
  loading.Start(0.0, true);
  loading.Back(2.0);
  if (!Near(loading.ArtAlpha(2.0), 1.0f) ||
      !Near(loading.ArtAlpha(2.0 + GameTransition::ART_OUT), 0.0f))
    return 13;

  // A new game starts over.
  chosen.Start(100.0, false);
  if (chosen.IsGameShown() || !Near(chosen.Black(100.0), 0.0f) || !chosen.Active(100.0))
    return 14;

  std::puts("launcher transition: ok");
  return 0;
}
