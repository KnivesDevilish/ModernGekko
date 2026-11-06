// Finding the floor of a background picture that slides: a grid seen in
// perspective (as in the launcher's and the lobby's pictures) is found with
// its repeat measured to a small fraction of a pixel, plain stripes too, and
// pictures with nothing regular in them stay still.
#include "moving_background.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace
{
constexpr int WIDTH = 871;
constexpr int HEIGHT = 490;

struct Picture
{
  std::vector<std::uint8_t> rgba = std::vector<std::uint8_t>(WIDTH * HEIGHT * 4, 0);

  void Set(int x, int y, double green)
  {
    std::uint8_t* pixel = &rgba[(static_cast<std::size_t>(y) * WIDTH + x) * 4];
    const auto level = static_cast<std::uint8_t>(std::clamp(green, 0.0, 255.0));
    pixel[0] = level / 8;
    pixel[1] = level;
    pixel[2] = level / 6;
    pixel[3] = 255;
  }

  moderngekko::frontend::ScrollingFloor Floor() const
  {
    return moderngekko::frontend::FindScrollingFloor(rgba.data(), WIDTH, HEIGHT, WIDTH * 4);
  }
};

// How much of a line about two pixels wide covers a pixel. position counts
// cells, with a line at each whole number, and a cell is cell_pixels wide.
double LineCover(double position, double cell_pixels)
{
  const double distance = std::abs(position - std::round(position)) * cell_pixels;
  return std::clamp(1.5 - distance, 0.0, 1.0);
}

// A dark sky over a green grid floor that starts at row 276, with its
// vanishing point at row -52 and cells 0.0487 x (row + 52) pixels wide.
constexpr int HORIZON = 276;
constexpr double VANISHING_ROW = -52.0;
constexpr double CELL_PER_ROW = 0.0487;
constexpr double DEPTH = 2400.0;

Picture Grid()
{
  Picture picture;
  for (int y = 0; y < HEIGHT; ++y)
  {
    for (int x = 0; x < WIDTH; ++x)
    {
      if (y < HORIZON)
      {
        picture.Set(x, y, 12.0 * y / HORIZON);
        continue;
      }
      // The grid's lines going away from the viewer meet at the vanishing
      // point; the ones across get further apart toward the viewer.
      const double below = y - VANISHING_ROW;
      const double cell = CELL_PER_ROW * below;
      const double away = LineCover((x - 401.3) / cell, cell);
      const double across = LineCover(DEPTH / below, below * below / DEPTH);
      picture.Set(x, y, 30.0 + 140.0 * std::max(away, across));
    }
  }
  return picture;
}
}  // namespace

int main()
{
  {
    const auto floor = Grid().Floor();
    if (!floor.found)
      return 1;
    // From just under the horizon to the bottom of the picture.
    if (floor.top < HORIZON || floor.top > HORIZON + 4 || floor.bottom != HEIGHT - 1)
      return 2;
    // The repeat, row by row, within a twentieth of a pixel.
    for (int y = floor.top; y <= floor.bottom; ++y)
    {
      if (std::abs(floor.Period(y) - CELL_PER_ROW * (y - VANISHING_ROW)) > 0.05)
        return 3;
    }
  }

  // Plain upright stripes repeat by the same amount on every row.
  {
    Picture picture;
    for (int y = 0; y < HEIGHT; ++y)
    {
      for (int x = 0; x < WIDTH; ++x)
        picture.Set(x, y, y < HEIGHT / 2 ? 10.0 : 20.0 + 120.0 * LineCover(x / 37.25, 37.25));
    }
    const auto floor = picture.Floor();
    if (!floor.found || floor.top != HEIGHT / 2 || floor.bottom != HEIGHT - 1 ||
        std::abs(floor.Period(floor.top) - 37.25) > 0.05 ||
        std::abs(floor.Period(floor.bottom) - 37.25) > 0.05)
      return 4;
  }

  // One colour, or noise: nothing slides.
  {
    Picture picture;
    for (int y = 0; y < HEIGHT; ++y)
    {
      for (int x = 0; x < WIDTH; ++x)
        picture.Set(x, y, 90.0);
    }
    if (picture.Floor().found)
      return 5;
    std::mt19937 random(7);
    std::uniform_real_distribution<double> level(0.0, 255.0);
    for (int y = 0; y < HEIGHT; ++y)
    {
      for (int x = 0; x < WIDTH; ++x)
        picture.Set(x, y, level(random));
    }
    if (picture.Floor().found)
      return 6;
  }

  // Stripes whose repeat jumps around from row to row can't wrap without a
  // seam: they stay still too.
  {
    Picture picture;
    std::mt19937 random(11);
    std::uniform_real_distribution<double> repeat(20.0, 120.0);
    double period = repeat(random);
    for (int y = 0; y < HEIGHT; ++y)
    {
      if (y % 6 == 0)
        period = repeat(random);
      for (int x = 0; x < WIDTH; ++x)
        picture.Set(x, y, 20.0 + 120.0 * LineCover(x / period, period));
    }
    if (picture.Floor().found)
      return 7;
  }

  // Too small to tell, or no pixels at all.
  if (moderngekko::frontend::FindScrollingFloor(nullptr, WIDTH, HEIGHT, WIDTH * 4).found)
    return 8;
  {
    const std::vector<std::uint8_t> tiny(32 * 8 * 4, 255);
    if (moderngekko::frontend::FindScrollingFloor(tiny.data(), 32, 8, 32 * 4).found)
      return 9;
  }
  return 0;
}
