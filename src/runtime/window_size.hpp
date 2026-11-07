#pragma once

// The game window's size and place when it opens: the size chosen in the
// launcher (RuntimeConfig::window_width/height), or one that fits the screen.
// The runtime sets it before the window opens, and the fork's copies of
// Dolphin's windows (src/platform/) read it. Apart from Dolphin and the
// window systems, so tests/window_size_test.cpp can check it; X11's headers,
// which those windows include, define names such as None and Status, so none
// are used here.

#include <algorithm>

namespace moderngekko::detail
{
// Width and height in pixels; 0 for "fit the screen".
struct WindowSize
{
  int width = 0;
  int height = 0;

  bool Automatic() const { return width <= 0 || height <= 0; }
};

// A monitor's place on the desktop, or an empty one when it isn't known.
struct ScreenArea
{
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct WindowPlace
{
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// An automatic window takes this much of the screen, at 16:9, leaving room
// for its title bar and the desktop's panel.
constexpr int AUTOMATIC_PERCENT = 85;
// Without a screen to fit (Wayland, where the compositor places windows).
constexpr WindowSize FALLBACK_WINDOW{1280, 720};

// The window's size and place on screen: the size asked for, centred, or
// the automatic one when it was asked for or doesn't fit the screen.
inline WindowPlace PlaceWindow(WindowSize asked, ScreenArea screen)
{
  WindowSize size = asked;
  const bool known = screen.width > 0 && screen.height > 0;
  if (known && (size.Automatic() || size.width > screen.width || size.height > screen.height))
  {
    int height = screen.height * AUTOMATIC_PERCENT / 100;
    int width = height * 16 / 9;
    if (width > screen.width * AUTOMATIC_PERCENT / 100)
    {
      width = screen.width * AUTOMATIC_PERCENT / 100;
      height = width * 9 / 16;
    }
    // Even sizes scale the game's picture more evenly.
    size = {std::max(2, width & ~1), std::max(2, height & ~1)};
  }
  else if (!known && size.Automatic())
  {
    size = FALLBACK_WINDOW;
  }
  WindowPlace place{0, 0, size.width, size.height};
  if (known)
  {
    place.x = screen.x + (screen.width - size.width) / 2;
    place.y = screen.y + (screen.height - size.height) / 2;
  }
  return place;
}

// Set by the runtime before the window opens (one thread, so no lock).
inline WindowSize& RequestedWindowSize()
{
  static WindowSize size;
  return size;
}
}  // namespace moderngekko::detail
