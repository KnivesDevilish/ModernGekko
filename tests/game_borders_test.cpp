// borders.txt (tools/game_borders.hpp): borders, their pictures and parts,
// grid motion, fit and backdrops, broken entries left out, choosing one, and
// where a side goes in its bar.
#include "game_borders.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
using moderngekko::frontend::BorderFit;
using moderngekko::frontend::BorderMotion;
using moderngekko::frontend::BorderRect;
using moderngekko::frontend::ChooseGameBorder;
using moderngekko::frontend::LoadGameBorders;
using moderngekko::frontend::NormalizeBorderSetting;
using moderngekko::frontend::ParseGameBorders;
using moderngekko::frontend::PlaceBorder;

bool Near(double a, double b)
{
  return std::abs(a - b) < 1e-4;
}

bool SameRect(const BorderRect& rect, float x0, float y0, float x1, float y1)
{
  return Near(rect.x0, x0) && Near(rect.y0, y0) && Near(rect.x1, x1) && Near(rect.y1, y1);
}
}  // namespace

int main()
{
  // Written on Windows: a byte order mark, \r\n, comments and spaces.
  const auto list = ParseGameBorders("\xEF\xBB\xBF# 4:3 borders\r\n"
                                     "\r\n"
                                     "[sonic]\r\n"
                                     "name = Sonic, Knuckles & Tails\r\n"
                                     "name.es = Sonic, Knuckles y Tails\r\n"
                                     "left = Sonic Left.png\r\n"
                                     "right = Sonic Right.png\r\n"
                                     "[Green-Grid]\n"
                                     "name = Green grid\n"
                                     "name.ja = \xE7\xB7\x91\n"
                                     "left = Green Grid.png 176 88 355 894\n"
                                     "right =   Green Grid.png   1113 88.5 1292 894  \n"
                                     "left_motion = up 176 355 95.3 -0.0567\n"
                                     "right_motion = Up 1113 1292 11.93 0.0567\n");
  if (list.borders.size() != 2 || !list.problems.empty())
    return 1;
  const auto& sonic = list.borders[0];
  if (sonic.id != "sonic" || sonic.left.picture != "Sonic Left.png" || sonic.left.part ||
      sonic.right.picture != "Sonic Right.png" || sonic.left.motion.Slides())
    return 2;
  if (sonic.Name("es") != "Sonic, Knuckles y Tails" ||
      sonic.Name("en") != "Sonic, Knuckles & Tails" || sonic.Name("ja") != sonic.name)
    return 3;
  const auto& grid = list.borders[1];
  if (grid.id != "green-grid" || grid.left.picture != "Green Grid.png" || !grid.left.part ||
      !SameRect(*grid.left.part, 176, 88, 355, 894) || grid.right.picture != "Green Grid.png" ||
      !grid.right.part || !SameRect(*grid.right.part, 1113, 88.5f, 1292, 894) ||
      grid.Name("ja") != "\xE7\xB7\x91")
    return 4;
  const BorderMotion& motion = grid.left.motion;
  if (motion.direction != BorderMotion::Direction::Up || motion.Sideways() ||
      motion.first != 176 || motion.last != 355 || !Near(motion.a, 95.3) ||
      !Near(motion.b, -0.0567) || !Near(motion.Period(100), 95.3 - 5.67))
    return 5;
  if (grid.right.motion.direction != BorderMotion::Direction::Up || !Near(grid.right.motion.b, 0.0567))
    return 6;

  // A broken border is left out with a problem; the rest still load. A
  // border's name defaults to its id.
  const auto broken = ParseGameBorders("[only-left]\n"
                                       "left = a.png\n"
                                       "[bad part]\n"
                                       "left = a.png\n"
                                       "right = b.png\n"
                                       "[none]\n"
                                       "left = a.png\n"
                                       "right = b.png\n"
                                       "[inverted]\n"
                                       "left = a.png 10 0 5 10\n"
                                       "right = b.png\n"
                                       "[outside]\n"
                                       "left = ../a.png\n"
                                       "right = b.png\n"
                                       "[slow]\n"
                                       "left = a.png\n"
                                       "right = b.png\n"
                                       "left_motion = sideways 0 10 5 0\n"
                                       "[tiny]\n"
                                       "left = a.png\n"
                                       "right = b.png\n"
                                       "right_motion = left 0 10 1 0\n"
                                       "[comma]\n"
                                       "left = a.png\n"
                                       "right = b.png\n"
                                       "left_motion = left 0 10 5,5 0\n"
                                       "[extra]\n"
                                       "left = a.png\n"
                                       "right = b.png\n"
                                       "colour = red\n"
                                       "[good]\n"
                                       "left = a b.png 1 2\n"
                                       "right = b.png\n"
                                       "[good]\n"
                                       "left = c.png\n"
                                       "right = d.png\n"
                                       "stray line\n");
  if (broken.borders.size() != 1 || broken.borders[0].id != "good" ||
      broken.borders[0].name != "good" || broken.borders[0].left.picture != "a b.png 1 2" ||
      broken.problems.size() != 11)
    return 7;
  if (!ParseGameBorders("").borders.empty() || !ParseGameBorders("\n# nothing\n").problems.empty())
    return 8;

  // Choosing: none, an id, a missing id (none), random (any number picks one,
  // the same number the same one).
  if (ChooseGameBorder(list, "none", 0) || ChooseGameBorder(list, "missing", 0) ||
      ChooseGameBorder(list, "green-grid", 0) != &list.borders[1] ||
      ChooseGameBorder(list, "random", 4) != &list.borders[0] ||
      ChooseGameBorder(list, "random", 7) != &list.borders[1] ||
      ChooseGameBorder(broken, "random", 12345) != &broken.borders[0] ||
      ChooseGameBorder(ParseGameBorders(""), "random", 3))
    return 9;
  if (NormalizeBorderSetting(" Random ") != "random" || NormalizeBorderSetting("Green-Grid") !=
                                                           "green-grid" ||
      !NormalizeBorderSetting("two words").empty() || !NormalizeBorderSetting("").empty())
    return 10;

  // Placing. 1920x1080 with a 4:3 game: 240-pixel bars, which a 2:9 part
  // fills exactly.
  const BorderRect strip{0, 0, 240, 1080};
  const auto exact = PlaceBorder({0, 0, 240, 1080}, strip, true);
  if (!exact || !SameRect(exact->screen, 0, 0, 240, 1080) ||
      !SameRect(exact->picture, 0, 0, 240, 1080) || !Near(exact->Scale(), 1.0))
    return 11;
  // 1366x768: the bars are 171 pixels, a third of a pixel wider than the
  // part at that height. The whole part, smaller, fills the bar.
  const auto small = PlaceBorder({1195, 0, 1366, 768}, strip, false);
  if (!small || !Near(small->Scale(), 768.0 / 1080.0) ||
      !SameRect(small->picture, 0, 0, 240, 1080) || !SameRect(small->screen, 1195, 0, 1366, 768))
    return 12;
  // 1680x1050, 16:10: 140-pixel bars, narrower than the part. The side next
  // to the game stays; the left bar loses the part's left, the right bar its
  // right.
  const BorderRect grid_part{176, 88, 355, 894};
  const auto left = PlaceBorder({0, 0, 140, 1050}, grid_part, true);
  const float shown = 140.0f * 806.0f / 1050.0f;
  if (!left || !SameRect(left->picture, 355 - shown, 88, 355, 894))
    return 13;
  const auto right = PlaceBorder({1540, 0, 1680, 1050}, grid_part, false);
  if (!right || !SameRect(right->picture, 176, 88, 176 + shown, 894) ||
      !SameRect(right->screen, 1540, 0, 1680, 1050))
    return 14;
  // 2560x1080, ultrawide: 560-pixel bars, wider than the part. It fills the
  // width and loses as much of its top as of its bottom.
  const auto wide = PlaceBorder({0, 0, 560, 1080}, strip, true);
  const float tall = 1080.0f * 240.0f / 560.0f;
  if (!wide || !SameRect(wide->picture, 0, 540 - tall / 2, 240, 540 + tall / 2) ||
      !Near(wide->Scale(), 560.0 / 240.0))
    return 15;
  // No bar: nothing to draw.
  if (PlaceBorder({0, 0, 0.5f, 1080}, strip, true) || PlaceBorder({0, 0, 240, 1080}, {}, true))
    return 16;

  // Loading from a folder: a border whose picture or backdrop is missing is
  // left out.
  const auto folder = std::filesystem::temp_directory_path() / "moderngekko_game_borders_test";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder);
  std::ofstream(folder / "borders.txt") << "[a]\nleft = a.png\nright = a.png\n"
                                           "[b]\nleft = a.png\nright = missing.png\n"
                                           "[c]\nleft = a.png\nright = a.png\n"
                                           "fit = contain\nright_backdrop = gone.png\n";
  std::ofstream(folder / "a.png") << "png";
  const auto loaded = LoadGameBorders(folder);
  const bool loaded_ok = loaded.borders.size() == 1 && loaded.borders[0].id == "a" &&
                         loaded.problems.size() == 2 &&
                         LoadGameBorders(folder / "nothing").borders.empty();
  std::filesystem::remove_all(folder);
  if (!loaded_ok)
    return 17;

  // fit and backdrops: cover unless the border says contain; a backdrop with
  // or without a part; any other fit is a problem.
  const auto fitted = ParseGameBorders("[sonic]\n"
                                       "left = Sonic.png 0 0 240 1080\n"
                                       "right = Sonic.png 240 0 480 1080\n"
                                       "fit = Contain\n"
                                       "left_backdrop = Sonic Backdrop.png 0 0 120 540\n"
                                       "right_backdrop = Sonic Backdrop.png\n"
                                       "[bad-fit]\n"
                                       "left = a.png\n"
                                       "right = b.png\n"
                                       "fit = stretch\n");
  if (fitted.borders.size() != 1 || fitted.problems.size() != 1 ||
      fitted.borders[0].fit != BorderFit::Contain || list.borders[0].fit != BorderFit::Cover)
    return 18;
  const auto& fitted_left = fitted.borders[0].left;
  const auto& fitted_right = fitted.borders[0].right;
  if (fitted_left.backdrop != "Sonic Backdrop.png" || !fitted_left.backdrop_part ||
      !SameRect(*fitted_left.backdrop_part, 0, 0, 120, 540) ||
      fitted_right.backdrop != "Sonic Backdrop.png" || fitted_right.backdrop_part ||
      !list.borders[0].left.backdrop.empty())
    return 19;

  // contain: the whole part, against the game, centred up and down.
  // A 16:9 screen and a game of exactly 4:3: the bar itself.
  const auto contain_exact = PlaceBorder({0, 0, 240, 1080}, strip, true, BorderFit::Contain);
  if (!contain_exact || !SameRect(contain_exact->screen, 0, 0, 240, 1080) ||
      !SameRect(contain_exact->picture, 0, 0, 240, 1080))
    return 20;
  // A game a little narrower than 4:3 leaves 250-pixel bars: the part keeps
  // its size next to the game and the outer 10 pixels are the backdrop's.
  const auto narrow_left = PlaceBorder({0, 0, 250, 1080}, strip, true, BorderFit::Contain);
  const auto narrow_right = PlaceBorder({1670, 0, 1920, 1080}, strip, false, BorderFit::Contain);
  if (!narrow_left || !SameRect(narrow_left->screen, 10, 0, 250, 1080) ||
      !SameRect(narrow_left->picture, 0, 0, 240, 1080) || !narrow_right ||
      !SameRect(narrow_right->screen, 1670, 0, 1910, 1080))
    return 21;
  // A little wider than 4:3, 230-pixel bars: smaller, with even bands above
  // and below.
  const auto wide_game = PlaceBorder({0, 0, 230, 1080}, strip, true, BorderFit::Contain);
  if (!wide_game || !SameRect(wide_game->screen, 0, 22.5f, 230, 1057.5f) ||
      !SameRect(wide_game->picture, 0, 0, 240, 1080))
    return 22;
  // 16:10 (140 x 1050 bars) and ultrawide (560 x 1080): never cut.
  const auto sixteen_ten = PlaceBorder({0, 0, 140, 1050}, strip, true, BorderFit::Contain);
  const auto ultrawide = PlaceBorder({2000, 0, 2560, 1080}, strip, false, BorderFit::Contain);
  if (!sixteen_ten || !SameRect(sixteen_ten->screen, 0, 210, 140, 840) || !ultrawide ||
      !SameRect(ultrawide->screen, 2000, 0, 2240, 1080) ||
      !SameRect(ultrawide->picture, 0, 0, 240, 1080))
    return 23;
  // 1366x768: a third of a pixel short of the bar still fills it.
  const auto laptop = PlaceBorder({1195, 0, 1366, 768}, strip, false, BorderFit::Contain);
  if (!laptop || !SameRect(laptop->screen, 1195, 0, 1366, 768))
    return 24;
  return 0;
}
