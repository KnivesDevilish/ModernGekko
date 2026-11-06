#pragma once

// The floor of a background picture that slides sideways, for the launcher
// and the Netplay lobby. A grid seen in perspective repeats along each of
// its rows: every few pixels near the horizon, every few more toward the
// bottom. Shifting each row to the left by a fraction of its own repeat, and
// wrapping it around, moves the whole floor one cell at a time with no seam.
// This file finds that floor in the picture's pixels when it is loaded;
// launcher_ui.hpp draws it.
//
// Nothing here is tied to one picture. A background whose rows don't repeat,
// or repeat unevenly, has no floor and simply stays still.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace moderngekko::frontend
{
struct ScrollingFloor
{
  bool found = false;
  // The floor's first and last rows of the picture; the rows above and
  // below stay still.
  int top = 0;
  int bottom = 0;
  // Row y repeats every period_at_zero + period_per_row * y pixels: the
  // cells of a grid in perspective widen steadily toward the viewer.
  double period_at_zero = 0.0;
  double period_per_row = 0.0;

  double Period(double row) const { return period_at_zero + period_per_row * row; }
};

namespace moving_background_detail
{
// Rows sampled while looking for the floor: every fourth one.
inline constexpr int ROW_STEP = 4;
// A row whose brightness varies less than this (a variance, in 0-255
// levels) is one colour, and has nothing to repeat.
inline constexpr double FLAT_ROW = 4.0;
// How alike a row must be to itself moved by its repeat (1 is identical).
inline constexpr double REPEATS = 0.6;
// Sampled rows a floor needs, so a few rows that happen to look regular
// don't make one.
inline constexpr std::size_t MIN_ROWS = 20;
// How far the measured repeats may stray from a straight line, in pixels:
// a quarter pixel or so stays invisible where a row wraps around.
inline constexpr double MAX_STRAY = 0.35;

// One row's brightness with its mean taken out, and running sums of its
// squares (squares[i] adds up the first i values squared).
struct Row
{
  std::vector<double> values;
  std::vector<double> squares;
  double variance = 0.0;
};

inline void LoadRow(const std::uint8_t* rgba, int width, int pitch, int y, Row& row)
{
  const std::size_t count = static_cast<std::size_t>(width);
  row.values.resize(count);
  row.squares.resize(count + 1);
  const std::uint8_t* pixel = rgba + static_cast<std::ptrdiff_t>(y) * pitch;
  double sum = 0.0;
  for (std::size_t x = 0; x < count; ++x, pixel += 4)
  {
    row.values[x] = 0.299 * pixel[0] + 0.587 * pixel[1] + 0.114 * pixel[2];
    sum += row.values[x];
  }
  const double mean = sum / width;
  row.squares[0] = 0.0;
  for (std::size_t x = 0; x < count; ++x)
  {
    row.values[x] -= mean;
    row.squares[x + 1] = row.squares[x] + row.values[x] * row.values[x];
  }
  row.variance = row.squares[count] / width;
}

// How alike the row is to itself moved lag pixels: 1 when it repeats
// exactly every lag pixels, around 0 when the two don't line up at all.
inline double Correlation(const Row& row, int lag)
{
  const int width = static_cast<int>(row.values.size());
  if (lag <= 0 || lag >= width)
    return 0.0;
  const int count = width - lag;
  const double* a = row.values.data();
  const double* b = a + lag;
  double dot = 0.0;
  for (int x = 0; x < count; ++x)
    dot += a[x] * b[x];
  const double left = row.squares[static_cast<std::size_t>(count)];
  const double right = row.squares[static_cast<std::size_t>(width)] -
                       row.squares[static_cast<std::size_t>(lag)];
  return left > 0.0 && right > 0.0 ? dot / std::sqrt(left * right) : 0.0;
}

// The same for a lag between whole pixels, reading the row between them.
inline double CorrelationAt(const Row& row, double lag)
{
  const int width = static_cast<int>(row.values.size());
  const int whole = static_cast<int>(std::floor(lag));
  if (whole < 1 || whole + 1 >= width)
    return 0.0;
  const double part = lag - whole;
  const int count = width - whole - 1;
  double dot = 0.0;
  double left = 0.0;
  double right = 0.0;
  for (int x = 0; x < count; ++x)
  {
    const double a = row.values[static_cast<std::size_t>(x)];
    const double b = (1.0 - part) * row.values[static_cast<std::size_t>(x + whole)] +
                     part * row.values[static_cast<std::size_t>(x + whole + 1)];
    dot += a * b;
    left += a * a;
    right += b * b;
  }
  return left > 0.0 && right > 0.0 ? dot / std::sqrt(left * right) : 0.0;
}

// The highest correlation within two pixels of near, placed between pixels
// by a parabola through it and its neighbours. correlation holds the lags
// from min_lag - 1 to max_lag + 1.
inline double PeakNear(const std::vector<double>& correlation, double near, int min_lag,
                       int max_lag)
{
  const int center = std::clamp(static_cast<int>(std::lround(near)), min_lag, max_lag);
  int peak = center;
  for (int lag = std::max(min_lag, center - 2); lag <= std::min(max_lag, center + 2); ++lag)
  {
    if (correlation[static_cast<std::size_t>(lag)] > correlation[static_cast<std::size_t>(peak)])
      peak = lag;
  }
  const double before = correlation[static_cast<std::size_t>(peak - 1)];
  const double at = correlation[static_cast<std::size_t>(peak)];
  const double after = correlation[static_cast<std::size_t>(peak + 1)];
  const double curve = before - 2.0 * at + after;
  return curve < 0.0 ? peak + 0.5 * (before - after) / curve : peak;
}
}  // namespace moving_background_detail

// rgba: the picture's pixels, four bytes each (red, green, blue, alpha), with
// rows pitch bytes apart.
inline ScrollingFloor FindScrollingFloor(const std::uint8_t* rgba, int width, int height,
                                         int pitch)
{
  using namespace moving_background_detail;
  ScrollingFloor floor;
  if (!rgba || width < 64 || height < 16 || pitch < width * 4)
    return floor;
  // A repeat between a hundredth and a sixth of the width: at least six
  // repeats across, so a picture that merely looks alike far apart is not
  // taken for a pattern.
  const int min_lag = std::max(4, width / 100);
  const int max_lag = width / 6;

  struct Sample
  {
    int y;
    double period;
  };
  std::vector<Sample> samples;
  std::vector<double> correlation(static_cast<std::size_t>(max_lag) + 2);
  Row row;
  // From the bottom up to a third of the way down: a floor is under the
  // horizon.
  for (int y = height - 1; y >= height / 3; y -= ROW_STEP)
  {
    LoadRow(rgba, width, pitch, y, row);
    if (row.variance < FLAT_ROW)
      continue;
    double best = 0.0;
    for (int lag = min_lag - 1; lag <= max_lag + 1; ++lag)
    {
      const double value = Correlation(row, lag);
      correlation[static_cast<std::size_t>(lag)] = value;
      if (lag >= min_lag && lag <= max_lag)
        best = std::max(best, value);
    }
    if (best < REPEATS)
      continue;
    // The shortest repeat is the first peak about as high as the highest
    // one (which may be two or three repeats long).
    int repeat = 0;
    for (int lag = min_lag; lag <= max_lag && repeat == 0; ++lag)
    {
      const double value = correlation[static_cast<std::size_t>(lag)];
      if (value >= 0.85 * best && value >= correlation[static_cast<std::size_t>(lag - 1)] &&
          value >= correlation[static_cast<std::size_t>(lag + 1)])
        repeat = lag;
    }
    if (repeat == 0)
      continue;
    // Measured again over as many repeats as fit, which pins it down to a
    // small fraction of a pixel.
    const double first = PeakNear(correlation, repeat, min_lag, max_lag);
    const int repeats = std::max(1, static_cast<int>(max_lag / first));
    const double period =
        repeats > 1 ? PeakNear(correlation, repeats * first, min_lag, max_lag) / repeats : first;
    samples.push_back({y, period});
  }
  if (samples.size() < MIN_ROWS)
    return floor;

  // A straight line through the repeats (least squares), fitted again
  // without the rows far from it.
  double mean_period = 0.0;
  for (const Sample& sample : samples)
    mean_period += sample.period;
  mean_period /= static_cast<double>(samples.size());
  const double tolerance = std::max(1.0, 0.02 * mean_period);
  std::vector<bool> kept(samples.size(), true);
  double at_zero = 0.0;
  double per_row = 0.0;
  for (int pass = 0; pass < 3; ++pass)
  {
    double n = 0.0, sum_y = 0.0, sum_p = 0.0, sum_yy = 0.0, sum_yp = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
      if (!kept[i])
        continue;
      const double y = samples[i].y;
      const double p = samples[i].period;
      n += 1.0;
      sum_y += y;
      sum_p += p;
      sum_yy += y * y;
      sum_yp += y * p;
    }
    const double determinant = n * sum_yy - sum_y * sum_y;
    if (n < static_cast<double>(MIN_ROWS) || determinant <= 0.0)
      return floor;
    per_row = (n * sum_yp - sum_y * sum_p) / determinant;
    at_zero = (sum_p - per_row * sum_y) / n;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
      kept[i] =
          std::abs(samples[i].period - (at_zero + per_row * samples[i].y)) <= tolerance;
    }
  }
  // Most rows must sit on the line, and close to it.
  double strays = 0.0;
  std::size_t count = 0;
  for (std::size_t i = 0; i < samples.size(); ++i)
  {
    if (!kept[i])
      continue;
    const double stray = samples[i].period - (at_zero + per_row * samples[i].y);
    strays += stray * stray;
    ++count;
  }
  if (count < MIN_ROWS || count * 4 < samples.size() * 3 ||
      std::sqrt(strays / static_cast<double>(count)) > MAX_STRAY)
    return floor;

  // The floor is the run of such rows up from the lowest one, through gaps
  // of a few rows (a horizontal line of the grid repeats nothing).
  int top = -1;
  int bottom = -1;
  for (std::size_t i = 0; i < samples.size(); ++i)
  {
    if (!kept[i])
      continue;
    if (bottom < 0)
    {
      top = bottom = samples[i].y;
      continue;
    }
    if (top - samples[i].y > 8 * ROW_STEP)
      break;
    top = samples[i].y;
  }
  const auto period = [&](int y) { return at_zero + per_row * y; };
  // Then row by row: up while rows still repeat (the sky above stays), and
  // down while they repeat or are one colour, which looks the same moved.
  const auto part_of_floor = [&](int y, bool one_colour_counts) {
    LoadRow(rgba, width, pitch, y, row);
    if (row.variance < FLAT_ROW)
      return one_colour_counts;
    const double p = period(y);
    return p >= min_lag && p <= max_lag && CorrelationAt(row, p) >= REPEATS;
  };
  while (top > 0 && part_of_floor(top - 1, false))
    --top;
  while (bottom + 1 < height && part_of_floor(bottom + 1, true))
    ++bottom;
  if (period(top) < 2.0 || period(bottom) < 2.0 || period(top) > width / 2.0 ||
      period(bottom) > width / 2.0)
    return floor;

  floor.found = true;
  floor.top = top;
  floor.bottom = bottom;
  floor.period_at_zero = at_zero;
  floor.period_per_row = per_row;
  return floor;
}
}  // namespace moderngekko::frontend
