// The game window's size and place (src/runtime/window_size.hpp): automatic
// is 16:9 at 85% of the screen, centred, also on a second monitor or one
// turned upright; a chosen size is kept when it fits, and the automatic one
// used when it doesn't; with no screen known, 1280x720 or the chosen size.
#include "runtime/window_size.hpp"

#include <cstdio>

namespace
{
using moderngekko::detail::PlaceWindow;
using moderngekko::detail::ScreenArea;
using moderngekko::detail::WindowPlace;
using moderngekko::detail::WindowSize;

bool Is(WindowPlace place, int x, int y, int width, int height)
{
  if (place.x == x && place.y == y && place.width == width && place.height == height)
    return true;
  std::printf("got %d,%d %dx%d\n", place.x, place.y, place.width, place.height);
  return false;
}
}  // namespace

int main()
{
  const WindowSize automatic;
  // A laptop's 1366x768 and a 1920x1080 monitor.
  if (!Is(PlaceWindow(automatic, {0, 0, 1366, 768}), 104, 58, 1158, 652))
    return 95;
  if (!Is(PlaceWindow(automatic, {0, 0, 1920, 1080}), 144, 81, 1632, 918))
    return 96;
  // The second of two monitors, an ultrawide, and one turned upright.
  if (!Is(PlaceWindow(automatic, {1920, 0, 1920, 1080}), 2064, 81, 1632, 918) ||
      !Is(PlaceWindow(automatic, {0, 0, 3440, 1440}), 632, 108, 2176, 1224) ||
      !Is(PlaceWindow(automatic, {0, 0, 1080, 1920}), 81, 702, 918, 516))
    return 97;
  // A chosen size that fits stays; one that doesn't becomes the automatic.
  if (!Is(PlaceWindow({1280, 720}, {0, 0, 1920, 1080}), 320, 180, 1280, 720) ||
      !Is(PlaceWindow({1920, 1080}, {0, 0, 1920, 1080}), 0, 0, 1920, 1080) ||
      !Is(PlaceWindow({1920, 1080}, {0, 0, 1366, 768}), 104, 58, 1158, 652) ||
      !Is(PlaceWindow({2560, 1440}, {0, 0, 1920, 1080}), 144, 81, 1632, 918))
    return 98;
  // No screen known: 1280x720, or the chosen size, where the system puts it.
  if (!Is(PlaceWindow(automatic, {}), 0, 0, 1280, 720) ||
      !Is(PlaceWindow({1600, 900}, {}), 0, 0, 1600, 900) ||
      !Is(PlaceWindow({0, 900}, {}), 0, 0, 1280, 720))
    return 99;
  if (!WindowSize{}.Automatic() || !WindowSize{0, 720}.Automatic() ||
      WindowSize{1280, 720}.Automatic())
    return 100;
  std::puts("window size: ok");
  return 0;
}
