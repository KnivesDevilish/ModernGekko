// The launcher's title screen (launcher_title.hpp draws it): what its
// layout.txt says, when each part of the opening happens, where the camera
// is, where each layer goes and the lines of its moving grids, as plain
// numbers. Apart from SDL and Dear ImGui, so tests/launcher_title_test.cpp
// can check it.
//
// layout.txt, in Resources/title/, one line each, back to front ("#" starts a
// comment). The pictures are all the same size (the first sets the scene's);
// positions are in that picture's pixels.
//   layer <picture> [zoom <z>] [parallax <p>] [bob <b> <seconds>] [appear <from> <to>]
//       zoom: how much it follows the camera, 0 (not at all, like the far
//         wall) to 1 (fully), up to 1.5 (nearer than the characters).
//       parallax: how far it leans with the mouse, in % of the window's width.
//       bob: it moves up and down by b % of the window's height, once every
//         `seconds` (a negative b starts the other way).
//       appear: it fades in between these points of the camera's move, 0 to 1.
//   grid <across> <along> <x0> <y0> <x1> <y1> <x2> <y2> <x3> <y3> [move <seconds>]
//        [colour <r> <g> <b> <a>] [width <w>] [fade <start> <end>]
//        [blend normal|add|light]
//       A grid of lines drawn with the layer before it, on a surface seen in
//       perspective: <across> by <along> cells whose corners are x0 y0 (the
//       start, left), x1 y1 (start, right), x2 y2 (end, right), x3 y3 (end,
//       left). The lines across it go along it, one cell every `seconds`
//       (toward the end; negative, toward the start), and round again, like
//       a treadmill. colour: 0 to 255; width: of a line, in cells (never
//       under a pixel); fade: how strong the lines are at the start and at
//       the end, 0 to 1; light: they light up what is under them.
//   camera <at> <x> <y> <zoom>
//       Where the camera is at this point of its move (0 to 1): the point of
//       the picture in the middle of the window (0 to 1 across and down), and
//       how close (1 to 4). It goes smoothly through them; it starts at the
//       focus (below) unless a point at 0 is given, and always ends on the
//       whole picture.
//   size <w> <h>        the pictures' size, that grids are placed in (the
//                       first layer's when not given)
//   move-seconds <s>    how long the camera's move takes (1 to 10)
//   title <picture>     the logo of the white title screen
//   focus <x> <y>       where the camera starts, 0 to 1 across and down
//   start-zoom <z>      how close it starts (1 to 4)
//   sound <file.wav>    played when a button is pressed on the title screen
// A line it can't read is left out. Without any layer, or without the title
// logo, the launcher opens as it did before.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <istream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace moderngekko::frontend::ui
{
struct TitleLayer
{
  std::string picture;
  float zoom = 1.0f;
  float parallax = 0.0f;
  float bob = 0.0f;
  float bob_seconds = 0.0f;
  float appear_from = -1.0f;  // -1: always there
  float appear_to = -1.0f;
};

enum class TitleBlend
{
  Normal,
  Add,
  Light,
};

struct TitleGrid
{
  std::size_t layer = 0;  // drawn right after it, moving with it
  float across = 1.0f;
  float along = 1.0f;
  std::array<float, 8> corners{};
  float seconds_per_cell = 0.0f;  // 0: still
  std::array<float, 4> colour{1.0f, 1.0f, 1.0f, 1.0f};
  float width = 0.02f;
  float fade_start = 1.0f;
  float fade_end = 1.0f;
  TitleBlend blend = TitleBlend::Normal;
};

struct TitleCameraKey
{
  float at = 0.0f;
  float x = 0.5f;
  float y = 0.5f;
  float zoom = 1.0f;
};

struct TitleLayout
{
  std::vector<TitleLayer> layers;
  std::vector<TitleGrid> grids;
  std::vector<TitleCameraKey> camera;
  std::string title;
  std::string sound;
  float focus_x = 0.5f;
  float focus_y = 0.5f;
  float start_zoom = 2.0f;
  float move_seconds = 0.0f;  // 0: TitleIntro's own
  float picture_width = 0.0f;  // 0: the first layer's
  float picture_height = 0.0f;
};

namespace title_detail
{
inline bool Number(std::istream& in, float& value, float lowest, float highest)
{
  return static_cast<bool>(in >> value) && std::isfinite(value) && value >= lowest &&
         value <= highest;
}

inline std::optional<TitleGrid> ParseGrid(std::istringstream& words, std::size_t layer)
{
  TitleGrid grid;
  grid.layer = layer;
  if (!Number(words, grid.across, 0.01f, 200.0f) || !Number(words, grid.along, 0.01f, 200.0f))
    return std::nullopt;
  for (float& corner : grid.corners)
  {
    if (!Number(words, corner, -100000.0f, 100000.0f))
      return std::nullopt;
  }
  std::string key;
  while (words >> key)
  {
    bool good = false;
    if (key == "move")
    {
      good = Number(words, grid.seconds_per_cell, -600.0f, 600.0f);
    }
    else if (key == "colour" || key == "color")
    {
      good = true;
      for (float& channel : grid.colour)
      {
        good = good && Number(words, channel, 0.0f, 255.0f);
        channel /= 255.0f;
      }
    }
    else if (key == "width")
    {
      good = Number(words, grid.width, 0.0f, 0.5f);
    }
    else if (key == "fade")
    {
      good = Number(words, grid.fade_start, 0.0f, 1.0f) && Number(words, grid.fade_end, 0.0f, 1.0f);
    }
    else if (key == "blend")
    {
      std::string blend;
      good = static_cast<bool>(words >> blend);
      if (blend == "normal")
        grid.blend = TitleBlend::Normal;
      else if (blend == "add")
        grid.blend = TitleBlend::Add;
      else if (blend == "light")
        grid.blend = TitleBlend::Light;
      else
        good = false;
    }
    if (!good)
      return std::nullopt;
  }
  return grid;
}
}  // namespace title_detail

inline std::optional<TitleLayout> ParseTitleLayout(std::istream& in)
{
  using title_detail::Number;
  TitleLayout layout;
  std::string line;
  while (std::getline(in, line))
  {
    if (const std::size_t hash = line.find('#'); hash != std::string::npos)
      line.erase(hash);
    std::istringstream words(line);
    std::string kind;
    if (!(words >> kind))
      continue;
    if (kind == "title" || kind == "sound")
    {
      std::string name;
      if (words >> name)
        (kind == "title" ? layout.title : layout.sound) = name;
      continue;
    }
    if (kind == "focus")
    {
      float x = 0.0f;
      float y = 0.0f;
      if (Number(words, x, 0.0f, 1.0f) && Number(words, y, 0.0f, 1.0f))
      {
        layout.focus_x = x;
        layout.focus_y = y;
      }
      continue;
    }
    if (kind == "start-zoom")
    {
      float zoom = 0.0f;
      if (Number(words, zoom, 1.0f, 4.0f))
        layout.start_zoom = zoom;
      continue;
    }
    if (kind == "size")
    {
      float w = 0.0f;
      float h = 0.0f;
      if (Number(words, w, 1.0f, 100000.0f) && Number(words, h, 1.0f, 100000.0f))
      {
        layout.picture_width = w;
        layout.picture_height = h;
      }
      continue;
    }
    if (kind == "move-seconds")
    {
      float seconds = 0.0f;
      if (Number(words, seconds, 1.0f, 10.0f))
        layout.move_seconds = seconds;
      continue;
    }
    if (kind == "camera")
    {
      TitleCameraKey key;
      if (Number(words, key.at, 0.0f, 1.0f) && Number(words, key.x, 0.0f, 1.0f) &&
          Number(words, key.y, 0.0f, 1.0f) && Number(words, key.zoom, 1.0f, 4.0f))
        layout.camera.push_back(key);
      continue;
    }
    if (kind == "grid")
    {
      if (layout.layers.empty())
        continue;
      if (std::optional<TitleGrid> grid = title_detail::ParseGrid(words, layout.layers.size() - 1))
        layout.grids.push_back(*grid);
      continue;
    }
    if (kind != "layer")
      continue;
    TitleLayer layer;
    if (!(words >> layer.picture))
      continue;
    bool good = true;
    std::string key;
    while (good && words >> key)
    {
      if (key == "zoom")
        good = Number(words, layer.zoom, 0.0f, 1.5f);
      else if (key == "parallax")
        good = Number(words, layer.parallax, 0.0f, 10.0f);
      else if (key == "bob")
        good = Number(words, layer.bob, -10.0f, 10.0f) &&
               Number(words, layer.bob_seconds, 1.0f, 120.0f);
      else if (key == "appear")
        good = Number(words, layer.appear_from, 0.0f, 1.0f) &&
               Number(words, layer.appear_to, 0.0f, 1.0f) && layer.appear_from < layer.appear_to;
      else
        good = false;
    }
    if (good)
      layout.layers.push_back(layer);
  }
  if (layout.layers.empty() || layout.title.empty())
    return std::nullopt;
  std::stable_sort(layout.camera.begin(), layout.camera.end(),
                   [](const TitleCameraKey& a, const TitleCameraKey& b) { return a.at < b.at; });
  return layout;
}

namespace title_motion
{
inline float Clamp01(float value)
{
  return std::clamp(value, 0.0f, 1.0f);
}

inline float SmoothStep(float t)
{
  t = Clamp01(t);
  return t * t * (3.0f - 2.0f * t);
}

// How much of a layer shows at this point of the camera's move.
inline float LayerAlpha(const TitleLayer& layer, float move)
{
  if (layer.appear_from < 0.0f)
    return 1.0f;
  return SmoothStep((move - layer.appear_from) / (layer.appear_to - layer.appear_from));
}

// How far a layer is up or down at this time, -1 to 1 of its bob.
inline float Bob(const TitleLayer& layer, double seconds)
{
  if (layer.bob_seconds <= 0.0f)
    return 0.0f;
  constexpr double TAU = 6.283185307179586;
  return static_cast<float>(std::sin(seconds / layer.bob_seconds * TAU));
}

// The camera's points: the ones in layout.txt, from the focus at 0 unless one
// is there, to the whole picture at 1 unless one is there.
inline std::vector<TitleCameraKey> CameraKeys(const TitleLayout& layout)
{
  std::vector<TitleCameraKey> keys = layout.camera;
  if (keys.empty() || keys.front().at > 0.0f)
    keys.insert(keys.begin(), TitleCameraKey{0.0f, layout.focus_x, layout.focus_y, layout.start_zoom});
  if (keys.back().at < 1.0f)
    keys.push_back(TitleCameraKey{1.0f, 0.5f, 0.5f, 1.0f});
  return keys;
}

// Where the camera is at this point of its move, 0 to 1: through each point,
// smoothly (a Catmull-Rom curve; the zoom goes evenly in steps of its own).
inline TitleCameraKey CameraAt(const TitleLayout& layout, float move)
{
  const std::vector<TitleCameraKey> keys = CameraKeys(layout);
  move = Clamp01(move);
  std::size_t i = 0;
  while (i + 2 < keys.size() && move > keys[i + 1].at)
    ++i;
  const TitleCameraKey& p1 = keys[i];
  const TitleCameraKey& p2 = keys[i + 1];
  const TitleCameraKey& p0 = i > 0 ? keys[i - 1] : p1;
  const TitleCameraKey& p3 = i + 2 < keys.size() ? keys[i + 2] : p2;
  const float span = p2.at - p1.at;
  const float s = span > 0.0f ? Clamp01((move - p1.at) / span) : 1.0f;
  const auto curve = [s](float a, float b, float c, float d) {
    return 0.5f * (2.0f * b + (c - a) * s + (2.0f * a - 5.0f * b + 4.0f * c - d) * s * s +
                   (3.0f * b - a - 3.0f * c + d) * s * s * s);
  };
  TitleCameraKey camera;
  camera.at = move;
  camera.x = curve(p0.x, p1.x, p2.x, p3.x);
  camera.y = curve(p0.y, p1.y, p2.y, p3.y);
  camera.zoom = std::exp(curve(std::log(p0.zoom), std::log(p1.zoom), std::log(p2.zoom),
                               std::log(p3.zoom)));
  camera.zoom = std::clamp(camera.zoom, 1.0f, 4.0f);
  return camera;
}

struct Rect
{
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

// Where a layer is drawn, in window pixels (width x height). The scene's
// pictures (picture_width x picture_height) cover the window, centred, at the
// end of the move; before, the camera's point is in the middle of the window,
// and each layer follows the camera by its zoom. mouse: -1 to 1 across and
// down. A layer is drawn big enough, and kept far enough in, that leaning and
// bobbing never show its edges: it always covers the window.
inline Rect LayerRect(const TitleLayout& layout, const TitleLayer& layer, float width,
                      float height, float picture_width, float picture_height, float move,
                      float mouse_x, float mouse_y, double seconds)
{
  if (width <= 0.0f || height <= 0.0f || picture_width <= 0.0f || picture_height <= 0.0f)
    return {};
  const TitleCameraKey camera = CameraAt(layout, move);
  const float cover = std::max(width / picture_width, height / picture_height);
  const float lean_x = layer.parallax * 0.01f * width;
  const float lean_y = lean_x * 0.5f;
  const float bob = layer.bob * 0.01f * height;
  const float margin_x = lean_x;
  const float margin_y = lean_y + std::abs(bob);
  const float grow =
      std::max((width + margin_x * 2.0f) / width, (height + margin_y * 2.0f) / height);
  const float zoom = std::max(1.0f + (camera.zoom - 1.0f) * layer.zoom, 1.0f) * grow;
  // The point of the picture in the middle of the window, for this layer.
  const float centre_x = 0.5f + (camera.x - 0.5f) * layer.zoom;
  const float centre_y = 0.5f + (camera.y - 0.5f) * layer.zoom;
  Rect rect;
  rect.w = picture_width * cover * zoom;
  rect.h = picture_height * cover * zoom;
  rect.x = width * 0.5f - centre_x * rect.w;
  rect.y = height * 0.5f - centre_y * rect.h;
  // Never an edge in the window, even close up.
  rect.x = std::clamp(rect.x, width + margin_x - rect.w, -margin_x);
  rect.y = std::clamp(rect.y, height + margin_y - rect.h, -margin_y);
  rect.x -= std::clamp(mouse_x, -1.0f, 1.0f) * lean_x;
  rect.y -= std::clamp(mouse_y, -1.0f, 1.0f) * lean_y;
  rect.y += bob * Bob(layer, seconds);
  return rect;
}

// A grid's surface: from its cells (u across, v along) to the picture's
// pixels, in perspective (the square-to-quad mapping).
class GridSurface
{
public:
  explicit GridSurface(const TitleGrid& grid) : m_across(grid.across), m_along(grid.along)
  {
    const auto& c = grid.corners;
    const float sx = c[0] - c[2] + c[4] - c[6];
    const float sy = c[1] - c[3] + c[5] - c[7];
    const float dx1 = c[2] - c[4];
    const float dx2 = c[6] - c[4];
    const float dy1 = c[3] - c[5];
    const float dy2 = c[7] - c[5];
    const float den = dx1 * dy2 - dx2 * dy1;
    if (std::abs(den) > 1e-6f)
    {
      m_g = (sx * dy2 - dx2 * sy) / den;
      m_h = (dx1 * sy - sx * dy1) / den;
    }
    m_a = c[2] - c[0] + m_g * c[2];
    m_b = c[6] - c[0] + m_h * c[6];
    m_c = c[0];
    m_d = c[3] - c[1] + m_g * c[3];
    m_e = c[7] - c[1] + m_h * c[7];
    m_f = c[1];
  }

  std::array<float, 2> At(float u, float v) const
  {
    const float s = u / m_across;
    const float t = v / m_along;
    float w = m_g * s + m_h * t + 1.0f;
    if (std::abs(w) < 1e-6f)
      w = 1e-6f;
    return {(m_a * s + m_b * t + m_c) / w, (m_d * s + m_e * t + m_f) / w};
  }

private:
  float m_across;
  float m_along;
  float m_a = 0.0f, m_b = 0.0f, m_c = 0.0f, m_d = 0.0f, m_e = 0.0f, m_f = 0.0f;
  float m_g = 0.0f, m_h = 0.0f;
};

struct Vertex
{
  float x = 0.0f;
  float y = 0.0f;
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 0.0f;
};

// Where the moving lines are at this time: 0 to 1 of a cell, always forward.
inline float GridPhase(const TitleGrid& grid, double seconds)
{
  if (grid.seconds_per_cell == 0.0f)
    return 0.0f;
  const double cells = seconds / static_cast<double>(grid.seconds_per_cell);
  return static_cast<float>(cells - std::floor(cells));
}

// The triangles of a grid's lines, in window pixels, added to `out`: drawn
// over its layer (place: where the layer is; picture_width: its picture's
// width), at this time, with the layer's alpha. Each line is a strip, its
// edges fading to nothing so it looks smooth, as wide as its width on the
// surface (at least a pixel), fainter toward its fade and at the grid's
// start and end. Segments outside the window (width x height) are left out.
inline void GridMesh(const TitleGrid& grid, const Rect& place, float picture_width, float width,
                     float height, double seconds, float alpha, std::vector<Vertex>& out)
{
  if (alpha <= 0.0f || picture_width <= 0.0f || grid.colour[3] <= 0.0f)
    return;
  const GridSurface surface(grid);
  const float scale = place.w / picture_width;
  const auto window_at = [&](float u, float v) {
    const std::array<float, 2> p = surface.At(u, v);
    return std::array<float, 2>{place.x + p[0] * scale, place.y + p[1] * scale};
  };
  const float soft = std::min(0.6f, grid.along * 0.25f);
  const auto strength = [&](float v) {
    const float t = Clamp01(v / grid.along);
    const float fade = grid.fade_start + (grid.fade_end - grid.fade_start) * t;
    const float ends = SmoothStep(v / soft) * SmoothStep((grid.along - v) / soft);
    return grid.colour[3] * alpha * fade * ends;
  };
  constexpr int SEGMENTS = 12;
  constexpr float FEATHER = 0.8f;  // pixels of fading edge
  const auto add_line = [&](float u0, float v0, float u1, float v1, bool along_line) {
    std::array<std::array<float, 2>, SEGMENTS + 1> points{};
    std::array<float, SEGMENTS + 1> half{};
    std::array<float, SEGMENTS + 1> strong{};
    for (int i = 0; i <= SEGMENTS; ++i)
    {
      const float t = static_cast<float>(i) / SEGMENTS;
      const float u = u0 + (u1 - u0) * t;
      const float v = v0 + (v1 - v0) * t;
      points[i] = window_at(u, v);
      // How wide the line is here: half its width on the surface, across it.
      const std::array<float, 2> side =
          along_line ? window_at(u + grid.width * 0.5f, v) : window_at(u, v + grid.width * 0.5f);
      half[i] = std::hypot(side[0] - points[i][0], side[1] - points[i][1]);
      strong[i] = strength(v);
    }
    for (int i = 0; i < SEGMENTS; ++i)
    {
      const std::array<float, 2>& p = points[i];
      const std::array<float, 2>& q = points[i + 1];
      const float dx = q[0] - p[0];
      const float dy = q[1] - p[1];
      const float length = std::hypot(dx, dy);
      if (length < 1e-4f || (strong[i] <= 0.0f && strong[i + 1] <= 0.0f))
        continue;
      const float reach = std::max(half[i], half[i + 1]) + FEATHER + 1.0f;
      if (std::max(p[0], q[0]) < -reach || std::min(p[0], q[0]) > width + reach ||
          std::max(p[1], q[1]) < -reach || std::min(p[1], q[1]) > height + reach)
        continue;
      const float nx = -dy / length;
      const float ny = dx / length;
      // Rows across the strip: edge, core, core, edge.
      const auto rows = [&](int at, std::array<Vertex, 4>& row) {
        // Thinner than a pixel: a pixel wide, as faint as it would cover.
        const float core = std::max(half[at], 0.5f);
        const float faint = std::min(1.0f, half[at] / 0.5f);
        const float offsets[4] = {-(core + FEATHER), -core, core, core + FEATHER};
        const float alphas[4] = {0.0f, strong[at] * faint, strong[at] * faint, 0.0f};
        for (int k = 0; k < 4; ++k)
        {
          row[k] = Vertex{points[at][0] + nx * offsets[k], points[at][1] + ny * offsets[k],
                          grid.colour[0],                  grid.colour[1],
                          grid.colour[2],                  alphas[k]};
        }
      };
      std::array<Vertex, 4> first{};
      std::array<Vertex, 4> second{};
      rows(i, first);
      rows(i + 1, second);
      for (int k = 0; k < 3; ++k)
      {
        out.push_back(first[k]);
        out.push_back(first[k + 1]);
        out.push_back(second[k]);
        out.push_back(first[k + 1]);
        out.push_back(second[k + 1]);
        out.push_back(second[k]);
      }
    }
  };
  // The lines that go along (still), one at each step across.
  for (float u = 0.0f; u <= grid.across + 1e-3f; u += 1.0f)
    add_line(u, 0.0f, u, grid.along, true);
  // The lines across (moving along).
  const float phase = GridPhase(grid, seconds);
  for (float v = phase; v <= grid.along; v += 1.0f)
    add_line(0.0f, v, grid.across, v, false);
}
}  // namespace title_motion

// The opening, in time: the window fades from black to the white title
// screen (the logo, and "PRESS ANY BUTTON" breathing under it); a press
// fades the logo, the white gives way to the scene and the music starts, and
// the camera goes along its points to the whole picture, slowly at first and
// at the end; then the launcher's pages come in. A second press while the
// camera moves takes it to the end at once.
class TitleIntro
{
public:
  static constexpr double FADE_IN = 0.9;
  static constexpr double LOGO_OUT = 0.35;
  static constexpr double WHITE_OUT_AT = 0.15;
  static constexpr double WHITE_OUT = 0.55;
  static constexpr double MOVE_AT = 0.45;
  static constexpr double MOVE = 4.2;
  static constexpr double PAGES_IN = 0.5;
  static constexpr double PROMPT_PERIOD = 1.6;

  // move_seconds: how long the camera's move takes (layout.txt's
  // move-seconds); 0 for MOVE.
  void Start(double now, double move_seconds = 0.0)
  {
    m_started = now;
    m_has_started = true;
    m_has_pressed = false;
    m_move = move_seconds > 0.0 ? move_seconds : MOVE;
  }

  bool Started() const { return m_has_started; }
  // From the press to the end of the camera's move.
  double End() const { return MOVE_AT + m_move; }
  // The title screen and the camera's move: the pages aren't drawn.
  bool Active(double now) const
  {
    return Started() && (!m_has_pressed || now < m_pressed + End());
  }
  bool Waiting() const { return Started() && !m_has_pressed; }

  // A key, click or button: the first starts the move, another during it
  // ends it.
  void Press(double now)
  {
    if (!Started())
      return;
    if (!m_has_pressed)
    {
      m_pressed = now;
      m_has_pressed = true;
    }
    else if (now < m_pressed + End())
      m_pressed = now - End();
  }

  // Black over everything at the very start, 1 to 0.
  float Black(double now) const
  {
    return 1.0f - title_motion::Clamp01(Seconds(now - m_started) / Seconds(FADE_IN));
  }
  // The white of the title screen, 1 until the press.
  float White(double now) const
  {
    if (!m_has_pressed)
      return 1.0f;
    return 1.0f - title_motion::SmoothStep(Seconds(now - m_pressed - WHITE_OUT_AT) /
                                           Seconds(WHITE_OUT));
  }
  float Logo(double now) const
  {
    if (!m_has_pressed)
      return 1.0f;
    return 1.0f - title_motion::Clamp01(Seconds(now - m_pressed) / Seconds(LOGO_OUT));
  }
  // The music starts as the scene shows, not on the title screen.
  bool Music(double now) const
  {
    return !Started() || (m_has_pressed && now >= m_pressed + WHITE_OUT_AT);
  }
  // "PRESS ANY BUTTON", breathing while it waits.
  float Prompt(double now) const
  {
    if (m_has_pressed)
      return 0.0f;
    constexpr double TAU = 6.283185307179586;
    const float breath =
        0.6f + 0.4f * static_cast<float>(std::cos((now - m_started) / PROMPT_PERIOD * TAU));
    return breath * title_motion::Clamp01(Seconds(now - m_started - FADE_IN * 0.5) /
                                          Seconds(FADE_IN));
  }
  // The camera's move, 0 (its first point) to 1 (the whole picture).
  float Move(double now) const
  {
    if (!Started())
      return 1.0f;
    if (!m_has_pressed)
      return 0.0f;
    return title_motion::SmoothStep(Seconds(now - m_pressed - MOVE_AT) / Seconds(m_move));
  }
  // The launcher's pages, 0 to 1, as the move ends. 1 without a title screen.
  float Pages(double now) const
  {
    if (!Started())
      return 1.0f;
    if (!m_has_pressed)
      return 0.0f;
    return title_motion::Clamp01(Seconds(now - m_pressed - End()) / Seconds(PAGES_IN));
  }

private:
  static float Seconds(double seconds) { return static_cast<float>(seconds); }

  double m_started = 0.0;
  double m_pressed = 0.0;
  bool m_has_started = false;
  bool m_has_pressed = false;
  double m_move = MOVE;
};
}  // namespace moderngekko::frontend::ui
