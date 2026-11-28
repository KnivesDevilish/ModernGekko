// The launcher's title screen (tools/launcher_title_layout.hpp): what its
// layout.txt says; the opening in time (black, the white title, a press, the
// music, the camera's move, the pages); the camera going through its points;
// where each layer is drawn, never with an edge in the window, however the
// camera, the mouse and the bobbing move it; and the moving grids, whose
// lines go along their surface and round again.
#include "launcher_title_layout.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace
{
using namespace moderngekko::frontend::ui;

int failures = 0;

void Check(bool condition, const char* what)
{
  if (!condition)
  {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

bool Near(float value, float expected, float margin = 0.001f)
{
  if (std::abs(value - expected) < margin)
    return true;
  std::printf("got %f, expected %f\n", value, expected);
  return false;
}

std::optional<TitleLayout> Parse(const char* text)
{
  std::istringstream in(text);
  return ParseTitleLayout(in);
}

constexpr const char* SCENE =
    "# The title scene\n"
    "size 2560 1440\n"
    "layer room.png zoom 0.35 parallax 0.4\n"
    "grid 29 11.3 -1490.7 -251 4073.7 -251 2444.7 232 138.3 232 move 1.6 colour 40 170 175 115 "
    "width 0.025 fade 1 0\n"
    "layer walls.png zoom 0.6 parallax 1 bob 1.5 11  # green\n"
    "grid 7 16 -557.2 -487.9 354.7 -147.7 588.3 1633.3 -1100.9 2618 move -1.8 colour 77 102 "
    "82 255 width 0.035 blend light\n"
    "layer characters.png zoom 1 parallax 1.4 bob 0.3 3.4\n"
    "layer front.png zoom 1.4 parallax 2\n"
    "layer logo.png zoom 0.3 parallax 0.5 appear 0.6 1\n"
    "title logo-design.png\n"
    "sound press.wav\n"
    "move-seconds 4.2\n"
    "camera 0.75 0.56 0.6 1.5\n"
    "camera 0 0.2 0.95 2.6\n"
    "camera 0.45 0.72 0.95 2.4\n";

// The lowest and highest x and y of a grid's lines at this time.
struct Box
{
  float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
};
Box Bounds(const std::vector<title_motion::Vertex>& mesh)
{
  Box box;
  for (const title_motion::Vertex& v : mesh)
  {
    box.left = std::min(box.left, v.x);
    box.right = std::max(box.right, v.x);
    box.top = std::min(box.top, v.y);
    box.bottom = std::max(box.bottom, v.y);
  }
  return box;
}
}  // namespace

int main()
{
  // The layout, back to front; lines it can't read left out.
  {
    const std::optional<TitleLayout> layout = Parse(SCENE);
    Check(layout.has_value(), "the scene is read");
    if (!layout)
      return 1;
    Check(layout->layers.size() == 5, "five layers");
    Check(layout->layers[0].picture == "room.png", "room first");
    Check(Near(layout->layers[0].zoom, 0.35f) && Near(layout->layers[0].parallax, 0.4f),
          "room's zoom and parallax");
    Check(layout->layers[1].picture == "walls.png" && Near(layout->layers[1].bob, 1.5f) &&
              Near(layout->layers[1].bob_seconds, 11.0f),
          "a comment after a layer");
    Check(Near(layout->layers[4].appear_from, 0.6f) && Near(layout->layers[4].appear_to, 1.0f),
          "logo appears");
    Check(layout->title == "logo-design.png" && layout->sound == "press.wav", "title and sound");
    Check(Near(layout->picture_width, 2560.0f) && Near(layout->picture_height, 1440.0f), "size");
    Check(Near(layout->move_seconds, 4.2f), "move seconds");
    Check(layout->grids.size() == 2, "two grids");
    const TitleGrid& ceiling = layout->grids[0];
    Check(ceiling.layer == 0 && Near(ceiling.across, 29.0f) && Near(ceiling.along, 11.3f),
          "the ceiling's grid, on the room");
    Check(Near(ceiling.corners[0], -1490.7f, 0.01f) && Near(ceiling.corners[7], 232.0f),
          "its corners");
    Check(Near(ceiling.seconds_per_cell, 1.6f) && Near(ceiling.colour[1], 170.0f / 255.0f) &&
              Near(ceiling.width, 0.025f) && Near(ceiling.fade_start, 1.0f) &&
              Near(ceiling.fade_end, 0.0f) && ceiling.blend == TitleBlend::Normal,
          "its movement and look");
    const TitleGrid& wall = layout->grids[1];
    Check(wall.layer == 1 && Near(wall.seconds_per_cell, -1.8f) && wall.blend == TitleBlend::Light,
          "the wall's grid, going the other way, lighting up");
    Check(layout->camera.size() == 3 && Near(layout->camera[0].at, 0.0f) &&
              Near(layout->camera[2].at, 0.75f),
          "the camera's points, in order");

    const std::optional<TitleLayout> odd = Parse("grid 1 1 0 0 1 0 1 1 0 1\n"
                                                 "layer a.png\n"
                                                 "layer b.png zoom 3\n"
                                                 "layer c.png parallax 40\n"
                                                 "layer d.png bob 1\n"
                                                 "layer e.png appear 0.8 0.2\n"
                                                 "layer f.png spin 2\n"
                                                 "layer\n"
                                                 "grid 2 2 0 0 1 0 1 1\n"
                                                 "grid 2 2 0 0 1 0 1 1 0 1 colour 300 0 0 0\n"
                                                 "grid 2 2 0 0 1 0 1 1 0 1 blend glow\n"
                                                 "grid 2 2 0 0 1 0 1 1 0 1 sparkle\n"
                                                 "camera 0.5 0.5 0.5 9\n"
                                                 "camera 0.2 0.5 0.5 0.5\n"
                                                 "camera 2 0.5 0.5 2\n"
                                                 "move-seconds 60\n"
                                                 "title t.png\n"
                                                 "focus 2 0.5\n"
                                                 "start-zoom 9\n"
                                                 "colour red\n");
    Check(odd && odd->layers.size() == 1 && odd->layers[0].picture == "a.png",
          "only the layer it can read");
    Check(odd && odd->grids.empty(), "no grid it can't read, nor one before any layer");
    Check(odd && odd->camera.empty() && Near(odd->move_seconds, 0.0f),
          "no camera point or move out of range");
    Check(odd && Near(odd->focus_x, 0.5f) && Near(odd->start_zoom, 2.0f),
          "focus and zoom out of range left as they were");
    Check(!Parse("title t.png\n"), "no layer: nothing to show");
    Check(!Parse("layer a.png\n"), "no title logo: nothing to show");
    Check(!Parse(""), "empty");
  }

  // The opening, in time.
  {
    TitleIntro intro;
    Check(!intro.Started() && !intro.Active(5.0), "not started: no title screen");
    Check(Near(intro.Pages(5.0), 1.0f) && Near(intro.Move(5.0), 1.0f) && intro.Music(5.0),
          "not started: pages, the whole picture, the music");
    intro.Press(5.0);
    Check(!intro.Started(), "a press before it starts does nothing");

    intro.Start(10.0, 4.0);
    const double end = intro.End();
    Check(Near(static_cast<float>(end), static_cast<float>(TitleIntro::MOVE_AT + 4.0)),
          "the layout's move seconds");
    Check(intro.Active(10.0) && intro.Waiting(), "starts waiting");
    Check(!intro.Music(30.0), "no music on the title screen");
    Check(Near(intro.Black(10.0), 1.0f), "black at first");
    Check(Near(intro.Black(10.0 + TitleIntro::FADE_IN), 0.0f), "then no black");
    Check(Near(intro.White(30.0), 1.0f) && Near(intro.Logo(30.0), 1.0f),
          "white and the logo while it waits");
    Check(Near(intro.Move(30.0), 0.0f) && Near(intro.Pages(30.0), 0.0f),
          "the first point, no pages while it waits");
    Check(Near(intro.Prompt(10.0), 0.0f), "the prompt comes after the black");
    float lowest = 1.0f;
    float highest = 0.0f;
    for (double t = 12.0; t < 16.0; t += 0.05)
    {
      lowest = std::min(lowest, intro.Prompt(t));
      highest = std::max(highest, intro.Prompt(t));
    }
    Check(lowest > 0.15f && lowest < 0.3f && highest > 0.95f, "the prompt breathes");
    Check(intro.Active(1000.0), "it waits as long as it takes");

    intro.Press(40.0);
    Check(!intro.Waiting() && intro.Active(40.0), "a press starts the move");
    Check(Near(intro.Prompt(40.0), 0.0f), "the prompt goes at once");
    Check(!intro.Music(40.0) && intro.Music(40.0 + TitleIntro::WHITE_OUT_AT),
          "the music starts as the scene shows");
    Check(Near(intro.Logo(40.0 + TitleIntro::LOGO_OUT), 0.0f), "the logo fades");
    Check(Near(intro.White(40.0 + TitleIntro::WHITE_OUT_AT), 1.0f), "the white stays a moment");
    Check(Near(intro.White(40.0 + TitleIntro::WHITE_OUT_AT + TitleIntro::WHITE_OUT), 0.0f),
          "then gives way to the scene");
    Check(intro.Move(40.0 + TitleIntro::WHITE_OUT_AT + TitleIntro::WHITE_OUT) < 0.05f,
          "the scene shows at the first point");
    Check(intro.Move(41.5) > 0.0f && intro.Move(41.5) < intro.Move(43.0) &&
              intro.Move(43.0) < 1.0f,
          "then the camera goes");
    Check(Near(intro.Move(40.0 + end), 1.0f), "to the whole picture");
    Check(!intro.Active(40.0 + end), "then the title screen is over");
    Check(Near(intro.Pages(40.0 + end), 0.0f) &&
              Near(intro.Pages(40.0 + end + TitleIntro::PAGES_IN), 1.0f),
          "and the pages come in");

    // A second press during the move ends it.
    TitleIntro hurried;
    hurried.Start(1.0);
    Check(Near(static_cast<float>(hurried.End()),
               static_cast<float>(TitleIntro::MOVE_AT + TitleIntro::MOVE)),
          "its own move without the layout's");
    hurried.Press(3.0);
    hurried.Press(3.5);
    Check(!hurried.Active(3.5) && Near(hurried.Move(3.5), 1.0f), "a second press ends the move");
    Check(Near(hurried.Logo(3.5), 0.0f) && Near(hurried.White(3.5), 0.0f) && hurried.Music(3.5),
          "with no logo or white left, and the music");
  }

  // Layers fading in during the move, and bobbing.
  {
    TitleLayer logo;
    logo.appear_from = 0.55f;
    logo.appear_to = 1.0f;
    Check(Near(title_motion::LayerAlpha(logo, 0.3f), 0.0f) &&
              Near(title_motion::LayerAlpha(logo, 1.0f), 1.0f) &&
              title_motion::LayerAlpha(logo, 0.8f) > 0.0f &&
              title_motion::LayerAlpha(logo, 0.8f) < 1.0f,
          "appears between its points");
    Check(Near(title_motion::LayerAlpha(TitleLayer{}, 0.0f), 1.0f), "the others always there");
    TitleLayer still;
    Check(Near(title_motion::Bob(still, 3.0), 0.0f), "no bob without one");
  }

  // The camera: through its points, smoothly, to the whole picture.
  {
    const std::optional<TitleLayout> layout = Parse(SCENE);
    const TitleCameraKey start = title_motion::CameraAt(*layout, 0.0f);
    Check(Near(start.x, 0.2f) && Near(start.y, 0.95f) && Near(start.zoom, 2.6f),
          "starts at its first point");
    const TitleCameraKey middle = title_motion::CameraAt(*layout, 0.45f);
    Check(Near(middle.x, 0.72f) && Near(middle.zoom, 2.4f, 0.01f), "goes through the next");
    const TitleCameraKey end = title_motion::CameraAt(*layout, 1.0f);
    Check(Near(end.x, 0.5f) && Near(end.y, 0.5f) && Near(end.zoom, 1.0f),
          "ends on the whole picture");
    float largest_step = 0.0f;
    TitleCameraKey before = start;
    for (float m = 0.01f; m <= 1.0001f; m += 0.01f)
    {
      const TitleCameraKey now = title_motion::CameraAt(*layout, m);
      largest_step = std::max(largest_step, std::abs(now.x - before.x) + std::abs(now.y - before.y));
      Check(now.zoom >= 1.0f && now.zoom <= 8.0f, "zoom in range");
      before = now;
    }
    Check(largest_step < 0.05f, "no jumps");
    // One point, straight to the whole picture: close at the feet between
    // Amy and Eggman, kept at the picture's bottom, zooming out evenly.
    const std::optional<TitleLayout> straight = Parse("layer a.png\ntitle t.png\n"
                                                      "camera 0 0.516 0.96 6\n");
    Check(straight && straight->camera.size() == 1 && Near(straight->camera[0].zoom, 6.0f),
          "up to 8 times closer");
    const TitleCameraKey a = title_motion::CameraAt(*straight, 0.0f);
    const TitleCameraKey halfway = title_motion::CameraAt(*straight, 0.5f);
    Check(Near(a.zoom, 6.0f) && Near(halfway.zoom, std::sqrt(6.0f), 0.01f) &&
              Near(halfway.x, (0.516f + 0.5f) * 0.5f, 0.001f),
          "point A to B: straight, the zoom evenly");
    TitleLayer flat_layer;
    const title_motion::Rect at_a = title_motion::LayerRect(*straight, flat_layer, 1280.0f, 720.0f,
                                                            2560.0f, 1440.0f, 0.0f, 0.0f, 0.0f, 0.0);
    Check(Near(at_a.w, 1280.0f * 6.0f, 0.5f) && Near(at_a.y + at_a.h, 720.0f, 0.5f) &&
              Near(at_a.x + 0.516f * at_a.w, 640.0f, 0.5f),
          "point A: the feet in the middle, at the bottom");
    // Without camera points: from the focus, as before.
    const std::optional<TitleLayout> plain = Parse("layer a.png\ntitle t.png\nfocus 0.3 0.7\n"
                                                   "start-zoom 2.2\n");
    const TitleCameraKey focus = title_motion::CameraAt(*plain, 0.0f);
    Check(Near(focus.x, 0.3f) && Near(focus.y, 0.7f) && Near(focus.zoom, 2.2f),
          "the focus without camera points");
    Check(Near(title_motion::CameraAt(*plain, 1.0f).zoom, 1.0f), "and the whole picture");
  }

  // Where the layers go: always covering the window, in every window shape,
  // at every point of the move, leaning and bobbing.
  {
    const std::optional<TitleLayout> layout = Parse(SCENE);
    const float windows[][2] = {{1280.0f, 720.0f}, {1920.0f, 1080.0f}, {1280.0f, 800.0f},
                                {1024.0f, 768.0f}, {2560.0f, 1080.0f}, {800.0f, 1200.0f}};
    bool covers = true;
    for (const auto& window : windows)
    {
      for (const TitleLayer& layer : layout->layers)
      {
        for (float move = 0.0f; move <= 1.0001f; move += 0.05f)
        {
          for (float mouse = -1.0f; mouse <= 1.0001f; mouse += 0.5f)
          {
            for (double seconds = 0.0; seconds < 12.0; seconds += 0.37)
            {
              const title_motion::Rect rect =
                  title_motion::LayerRect(*layout, layer, window[0], window[1], 2560.0f, 1440.0f,
                                          move, mouse, -mouse, seconds);
              if (rect.x > 0.01f || rect.y > 0.01f || rect.x + rect.w < window[0] - 0.01f ||
                  rect.y + rect.h < window[1] - 0.01f)
              {
                if (covers)
                  std::printf("%s in %gx%g at move %g: %g,%g %gx%g\n", layer.picture.c_str(),
                              window[0], window[1], move, rect.x, rect.y, rect.w, rect.h);
                covers = false;
              }
            }
          }
        }
      }
    }
    Check(covers, "every layer always covers the window");

    // At the end, with the mouse in the middle and no bob, the picture is
    // centred; a layer without parallax or bob is the picture covering the
    // window exactly.
    TitleLayer flat;
    const title_motion::Rect whole = title_motion::LayerRect(*layout, flat, 1280.0f, 720.0f,
                                                             2560.0f, 1440.0f, 1.0f, 0.0f, 0.0f,
                                                             0.0);
    Check(Near(whole.x, 0.0f) && Near(whole.y, 0.0f) && Near(whole.w, 1280.0f) &&
              Near(whole.h, 720.0f),
          "the whole picture at the end");
    const title_motion::Rect tall = title_motion::LayerRect(*layout, flat, 1280.0f, 800.0f,
                                                            2560.0f, 1440.0f, 1.0f, 0.0f, 0.0f, 0.0);
    Check(Near(tall.h, 800.0f) && Near(tall.x, (1280.0f - tall.w) * 0.5f),
          "a taller window: covered, centred");
    const title_motion::Rect walls = title_motion::LayerRect(
        *layout, layout->layers[1], 1280.0f, 720.0f, 2560.0f, 1440.0f, 1.0f, 0.0f, 0.0f, 0.0);
    Check(Near(walls.x + walls.w * 0.5f, 640.0f) && Near(walls.y + walls.h * 0.5f, 360.0f) &&
              walls.w > 1280.0f,
          "a layer that moves is a little bigger, centred");

    // Close up at the start: the camera's point in the middle where the
    // picture allows it, the characters bigger than the far wall.
    const std::optional<TitleLayout> centred = Parse("layer a.png\ntitle t.png\nfocus 0.5 0.5\n"
                                                     "start-zoom 2.2\n");
    const title_motion::Rect close = title_motion::LayerRect(*centred, flat, 1280.0f, 720.0f,
                                                             2560.0f, 1440.0f, 0.0f, 0.0f, 0.0f,
                                                             0.0);
    Check(Near(close.w, 1280.0f * 2.2f, 0.5f) && Near(close.x + close.w * 0.5f, 640.0f, 0.5f),
          "close up around the focus");
    const title_motion::Rect characters_start = title_motion::LayerRect(
        *layout, layout->layers[2], 1280.0f, 720.0f, 2560.0f, 1440.0f, 0.0f, 0.0f, 0.0f, 0.0);
    const title_motion::Rect room_start = title_motion::LayerRect(
        *layout, layout->layers[0], 1280.0f, 720.0f, 2560.0f, 1440.0f, 0.0f, 0.0f, 0.0f, 0.0);
    Check(characters_start.w > room_start.w * 1.5f, "the characters nearer than the far wall");
    // Low at the start: the picture's bottom stays at the window's, the left
    // part of it (Sonic's side) in view.
    Check(characters_start.y + characters_start.h >= 720.0f &&
              characters_start.y + characters_start.h < 760.0f,
          "kept down at the bottom, not past it");
    Check(characters_start.x > -0.2f * characters_start.w, "starting on the left");
  }

  // The grids: their corners where layout.txt says, lines going along and
  // round again, fading, and nothing drawn without strength.
  {
    const std::optional<TitleLayout> layout = Parse(SCENE);
    const TitleGrid& ceiling = layout->grids[0];
    const title_motion::GridSurface surface(ceiling);
    const std::array<float, 2> far_right = surface.At(ceiling.across, ceiling.along);
    const std::array<float, 2> near_left = surface.At(0.0f, 0.0f);
    Check(Near(near_left[0], -1490.7f, 0.05f) && Near(near_left[1], -251.0f, 0.05f) &&
              Near(far_right[0], 2444.7f, 0.05f) && Near(far_right[1], 232.0f, 0.05f),
          "the surface on its corners");
    // In perspective: halfway along is nearer the start on the picture.
    Check(surface.At(ceiling.across * 0.5f, ceiling.along * 0.5f)[1] > (-251.0f + 232.0f) * 0.5f,
          "farther cells smaller");

    const title_motion::Rect whole{0.0f, 0.0f, 1280.0f, 720.0f};
    std::vector<title_motion::Vertex> mesh;
    title_motion::GridMesh(ceiling, whole, 2560.0f, 1280.0f, 720.0f, 0.0, 1.0f, mesh);
    Check(!mesh.empty() && mesh.size() % 3 == 0, "triangles");
    bool finite = true;
    for (const title_motion::Vertex& v : mesh)
      finite = finite && std::isfinite(v.x) && std::isfinite(v.y) && v.a >= 0.0f && v.a <= 1.0f;
    Check(finite, "every vertex a number, its alpha 0 to 1");
    const Box box = Bounds(mesh);
    Check(box.bottom < 125.0f, "the ceiling's lines stay on the ceiling");
    if (box.bottom >= 125.0f)
      std::printf("ceiling lines: %g to %g\n", box.top, box.bottom);

    // Round again: a cell later it is the same; half a cell later it isn't.
    std::vector<title_motion::Vertex> later;
    title_motion::GridMesh(ceiling, whole, 2560.0f, 1280.0f, 720.0f, ceiling.seconds_per_cell,
                           1.0f, later);
    bool same = later.size() == mesh.size();
    for (std::size_t k = 0; same && k < mesh.size(); ++k)
      same = std::abs(later[k].x - mesh[k].x) < 0.05f && std::abs(later[k].y - mesh[k].y) < 0.05f;
    Check(same, "a cell later, the same");
    Check(Near(title_motion::GridPhase(ceiling, 0.4), 0.25f) &&
              Near(title_motion::GridPhase(ceiling, 1.6 * 7 + 0.4), 0.25f),
          "the lines go a cell every 1.6 seconds, forever");
    const TitleGrid& wall = layout->grids[1];
    Check(Near(title_motion::GridPhase(wall, 0.45), 0.75f), "the other way: backward");

    // The wall's lines go up: along it is downward on the picture, and its
    // lines go back along it.
    const title_motion::GridSurface wall_surface(wall);
    Check(wall_surface.At(3.0f, 8.0f)[1] > wall_surface.At(3.0f, 7.0f)[1] &&
              title_motion::GridPhase(wall, 0.3) < title_motion::GridPhase(wall, 0.2),
          "the left wall's lines go up");
    std::vector<title_motion::Vertex> wall_mesh;
    title_motion::GridMesh(wall, whole, 2560.0f, 1280.0f, 720.0f, 0.0, 1.0f, wall_mesh);
    const Box wall_box = Bounds(wall_mesh);
    Check(!wall_mesh.empty() && wall_box.right < 310.0f, "the wall's lines stay on the wall");

    // Fading: at no strength, nothing; the far end of the ceiling, faint.
    std::vector<title_motion::Vertex> none;
    title_motion::GridMesh(ceiling, whole, 2560.0f, 1280.0f, 720.0f, 0.0, 0.0f, none);
    Check(none.empty(), "an unseen layer's grid isn't drawn");
    float far_alpha = 0.0f;
    float near_alpha = 0.0f;
    for (const title_motion::Vertex& v : mesh)
    {
      if (v.y > 100.0f)
        far_alpha = std::max(far_alpha, v.a);
      if (v.y < 20.0f)
        near_alpha = std::max(near_alpha, v.a);
    }
    Check(near_alpha > far_alpha * 2.0f, "fainter toward the back");
    // Out of the window: left out.
    std::vector<title_motion::Vertex> away;
    title_motion::GridMesh(ceiling, title_motion::Rect{5000.0f, 5000.0f, 1280.0f, 720.0f}, 2560.0f,
                           1280.0f, 720.0f, 0.0, 1.0f, away);
    Check(away.empty(), "nothing out of the window");
  }

  if (failures == 0)
    std::printf("launcher title: all good\n");
  return failures == 0 ? 0 : 1;
}
