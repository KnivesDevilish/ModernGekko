#pragma once

// Screenshots at a higher internal resolution than the game is drawn at
// (GraphicsSettings::screenshot_scale), taken with F9 in the game window or
// the in-game menu's Take screenshot. Dolphin saves the picture the game
// copied out for the screen, at the resolution it was drawn at, without the
// picture effect, the borders or the menu. So the game is drawn at the higher
// resolution for the few frames that takes (screenshot_steps.hpp), then goes
// back to its own; the setting is never changed. A paused game runs those
// frames without sound or input and pauses again, so the picture is of a
// moment a few frames later. The PNG goes where Dolphin puts its own,
// ScreenShots/<game id>/ in the user folder, with a short name: SR-Source_0001.png
// and on.

#include "screenshot_keys.hpp"

#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace Common
{
struct HookBase;
}

namespace moderngekko::detail
{
// How the last screenshot went, for the in-game menu to say.
struct ScreenshotStatus
{
  enum class State
  {
    Idle,
    Taking,  // from the request until the PNG is written
    Saved,
    Failed,
  };
  State state = State::Idle;
  // Goes up with every screenshot, so a new one is told from the last.
  unsigned count = 0;
  std::string file;  // the PNG's name, without its folder
  int width = 0;
  int height = 0;
};

class HighResScreenshot
{
public:
  // scale: GraphicsSettings::screenshot_scale; name: what the files' names
  // start with (GraphicsSettings::screenshot_name). mute silences the game's sound
  // (true) and brings it back (false) around a paused game's frames for the
  // picture; without it the sound stream is stopped for them instead. Until
  // it is destroyed, F9 takes its screenshots (TakeScreenshot).
  static std::unique_ptr<HighResScreenshot> Create(int scale, std::string name,
                                                   std::function<void(bool muted)> mute);
  ~HighResScreenshot();
  HighResScreenshot(const HighResScreenshot&) = delete;
  HighResScreenshot& operator=(const HighResScreenshot&) = delete;

  // From any thread. False while one is being taken or written.
  bool Take();
  ScreenshotStatus Status() const;

private:
  struct Shared;
  friend bool ResumeAfterScreenshot();

  HighResScreenshot() = default;

  std::shared_ptr<Shared> m_shared;
  // Dolphin's end-of-frame event, on the video thread.
  std::unique_ptr<Common::HookBase> m_frame_hook;
  std::thread m_watcher;
};

// TakeScreenshot and ScreenshotBusy, for the game window's keys, are in
// screenshot_keys.hpp.
// While a paused game runs for a screenshot, the controllers' input stays out
// of it (Host_UIBlocksControllerState).
bool ScreenshotBlocksInput();
// The in-game menu resuming a game it paused, on the host thread. A paused
// game running for a screenshot then keeps running after it instead of
// pausing again: true when that is so, and the menu has nothing to do.
bool ResumeAfterScreenshot();
}  // namespace moderngekko::detail
