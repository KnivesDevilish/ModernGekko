#pragma once

// What the game window's keys (src/platform/) ask of the screenshots
// (screenshot.hpp), in a header of its own: X11's headers, which those
// windows include, define names such as None and Status.

namespace moderngekko::detail
{
// F9: the running game's screenshot at the resolution chosen for them, or
// Dolphin's own when there is none. On the host thread.
void TakeScreenshot();
// From a screenshot's start until the game is back as it was: the window's
// pause key waits, and the in-game menu takes no presses.
bool ScreenshotBusy();
}  // namespace moderngekko::detail
