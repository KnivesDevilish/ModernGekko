#include "screenshot.hpp"

#include "screenshot_steps.hpp"

#include "AudioCommon/AudioCommon.h"
#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Common/Thread.h"
#include "Common/TimeUtil.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "VideoCommon/FrameDumper.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoEvents.h"

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace moderngekko::detail
{
namespace
{
using Clock = std::chrono::steady_clock;

// From F9 until the game is back as it was; past this the screenshot is given
// up and the game put back anyway.
constexpr std::chrono::seconds TIME_LIMIT{15};
// Then how long Dolphin may take to write the PNG (a big picture takes a few
// seconds to compress).
constexpr std::chrono::seconds WRITE_LIMIT{30};
// The sound of a paused game's frames for the picture is still on its way to
// the speakers when it pauses again: it stays silent this much longer.
constexpr std::chrono::milliseconds QUIET_AFTER{700};
// How often the PNG is looked at while it is written.
constexpr std::chrono::milliseconds WRITE_POLL{100};

std::mutex s_active_mutex;
HighResScreenshot* s_active = nullptr;
std::atomic<bool> s_busy{false};
std::atomic<bool> s_blocks_input{false};

// Where Dolphin's own F9 saves (Core/CoreScreenshot.cpp), named the same way:
// ScreenShots/<game id>/<game id>_<date>_<time>.png, numbered after a second
// one in the same second.
std::string NewScreenshotPath()
{
  const std::string game = SConfig::GetInstance().GetGameID();
  std::string folder = File::GetUserPath(D_SCREENSHOTS_IDX) + game + DIR_SEP_CHR;
  if (!File::CreateFullPath(folder))
    folder = File::GetUserPath(D_SCREENSHOTS_IDX);
  const std::optional<std::tm> now = Common::LocalTime(std::time(nullptr));
  const std::string base =
      now ? fmt::format("{}{}_{:%Y-%m-%d_%H-%M-%S}", folder, game, *now) : folder + game;
  std::string path = base + ".png";
  for (unsigned i = 1; File::Exists(path); ++i)
    path = fmt::format("{}_{}.png", base, i);
  return path;
}
}  // namespace

struct HighResScreenshot::Shared
{
  int scale = 0;
  std::function<void(bool)> mute;

  mutable std::mutex mutex;
  std::condition_variable wake;
  bool stop = false;
  bool busy = false;
  ScreenshotStatus status;
  Clock::time_point give_up_at;
  // While the game draws for the picture (the video thread's OnFrame).
  std::optional<ScreenshotFrames> frames;
  std::string path;
  // A Finish job is on its way.
  bool finishing = false;
  // The PNG Dolphin is writing, looked at until it is all there.
  std::string writing;
  Clock::time_point writing_until;
  // When the sound comes back after a paused game's frames.
  std::optional<Clock::time_point> sound_back_at;

  // The host thread's: what Start changed, for Finish to put back.
  bool raised = false;
  std::optional<int> previous_current_scale;
  bool resumed = false;
  bool keep_running = false;
  // The game's sound is silenced (sound_off) for a paused game's frames
  // (shot_muting) and a little after them; mute is only called on this
  // thread.
  bool sound_off = false;
  bool shot_muting = false;

  bool Stopped() const
  {
    std::lock_guard lock(mutex);
    return stop;
  }

  // On the host thread, where Dolphin's settings and pause change (as its
  // own windows change them).
  void Start(Core::System& system)
  {
    if (Stopped())
      return;
    const Core::State state = Core::GetState(system);
    if (state != Core::State::Running && state != Core::State::Paused)
    {
      Finish(system, false);
      return;
    }
    const int game_scale = Config::Get(Config::GFX_EFB_SCALE);
    const int shot_scale = ScreenshotScale(game_scale, scale);
    raised = shot_scale != game_scale;
    if (raised)
    {
      // Over the game's own setting, in the layer Dolphin never saves, so the
      // setting stays as it is.
      previous_current_scale = Config::GetLayer(Config::LayerType::CurrentRun)
                                   ->Get<int>(Config::GFX_EFB_SCALE.GetLocation());
      Config::SetCurrent(Config::GFX_EFB_SCALE, shot_scale);
    }
    // A paused game runs the frames the picture needs, then pauses again.
    resumed = state == Core::State::Paused;
    keep_running = false;
    if (resumed)
    {
      s_blocks_input = true;
      if (mute)
      {
        if (!sound_off)
          mute(true);
        sound_off = true;
        shot_muting = true;
      }
      Core::SetState(system, Core::State::Running, false);
      // Without mute (another sound backend), the stream waits instead; the
      // game's resume starts it again.
      if (!mute)
        AudioCommon::SetSoundStreamRunning(system, false);
    }
    {
      std::lock_guard lock(mutex);
      path = NewScreenshotPath();
      frames.emplace(shot_scale, raised);
    }
    std::fprintf(stderr, "screenshot: drawing at %dx (the game's is %dx)%s\n", shot_scale,
                 game_scale, resumed ? ", running the paused game for it" : "");
  }

  // On the video thread, at the end of each frame the game draws.
  void OnFrame(const std::shared_ptr<Shared>& self)
  {
    ScreenshotFrames::Action action = ScreenshotFrames::Action::Wait;
    std::string request;
    {
      std::lock_guard lock(mutex);
      if (!frames || finishing || stop || !g_frame_dumper)
        return;
      action = frames->OnFrame(g_ActiveConfig.iEFBScale, g_frame_dumper->IsFrameDumping());
      if (action == ScreenshotFrames::Action::Request)
        request = path;
      if (action == ScreenshotFrames::Action::Done || action == ScreenshotFrames::Action::GiveUp)
      {
        frames.reset();
        finishing = true;
      }
    }
    // Dolphin's screenshot lock is held while it writes a PNG: Take waits for
    // the last one to be written, so this doesn't wait.
    if (!request.empty())
      g_frame_dumper->SaveScreenshot(std::move(request));
    if (action != ScreenshotFrames::Action::Done && action != ScreenshotFrames::Action::GiveUp)
      return;
    const bool captured = action == ScreenshotFrames::Action::Done;
    Core::QueueHostJob([self, captured](Core::System& system) { self->Finish(system, captured); });
  }

  // On the host thread: the game back as it was. captured: Dolphin took the
  // picture, and writes it.
  void Finish(Core::System& system, bool captured)
  {
    if (Stopped())
      return;
    if (raised)
    {
      if (previous_current_scale)
        Config::SetCurrent(Config::GFX_EFB_SCALE, *previous_current_scale);
      else
        Config::DeleteKey(Config::LayerType::CurrentRun, Config::GFX_EFB_SCALE);
      raised = false;
      previous_current_scale.reset();
    }
    if (resumed)
    {
      if (!keep_running && Core::GetState(system) == Core::State::Running)
        Core::SetState(system, Core::State::Paused, false);
      resumed = false;
      s_blocks_input = false;
    }
    {
      std::lock_guard lock(mutex);
      frames.reset();
      finishing = false;
      busy = false;
      if (shot_muting)
      {
        sound_back_at = Clock::now() + QUIET_AFTER;
        shot_muting = false;
      }
      if (captured)
      {
        writing = path;
        writing_until = Clock::now() + WRITE_LIMIT;
      }
      else
      {
        status.state = ScreenshotStatus::State::Failed;
      }
    }
    s_busy = false;
    wake.notify_all();
    if (!captured)
      std::fprintf(stderr, "screenshot: not taken\n");
  }

  // On the host thread, a little after a paused game's frames: the sound
  // back, unless another screenshot silenced it again meanwhile.
  void SoundBack()
  {
    if (Stopped() || !sound_off || shot_muting)
      return;
    mute(false);
    sound_off = false;
  }

  // The screenshot's own thread: gives up on one that takes too long, brings
  // the sound back, and waits for the PNG to be written.
  void Watch(const std::shared_ptr<Shared>& self)
  {
    Common::SetCurrentThreadName("Screenshot");
    std::unique_lock lock(mutex);
    while (!stop)
    {
      std::optional<Clock::time_point> next;
      const auto sooner = [&](Clock::time_point when) {
        if (!next || when < *next)
          next = when;
      };
      if (busy && !finishing)
        sooner(give_up_at);
      if (sound_back_at)
        sooner(*sound_back_at);
      if (!writing.empty())
        sooner(Clock::now() + WRITE_POLL);
      if (next)
        wake.wait_until(lock, *next);
      else
        wake.wait(lock);
      if (stop)
        break;
      const Clock::time_point now = Clock::now();
      if (busy && !finishing && now >= give_up_at)
      {
        frames.reset();
        finishing = true;
        Core::QueueHostJob([self](Core::System& system) { self->Finish(system, false); });
      }
      if (sound_back_at && now >= *sound_back_at)
      {
        sound_back_at.reset();
        Core::QueueHostJob([self](Core::System&) { self->SoundBack(); });
      }
      if (!writing.empty())
      {
        const std::string file = writing;
        lock.unlock();
        const std::optional<PngSize> size = FinishedPngSize(file);
        lock.lock();
        if (size)
        {
          status.state = ScreenshotStatus::State::Saved;
          status.width = static_cast<int>(size->width);
          status.height = static_cast<int>(size->height);
          writing.clear();
          std::fprintf(stderr, "screenshot: %ux%u, %s\n", size->width, size->height,
                       file.c_str());
        }
        else if (now >= writing_until)
        {
          status.state = ScreenshotStatus::State::Failed;
          writing.clear();
          std::fprintf(stderr, "screenshot: %s was not written\n", file.c_str());
        }
      }
    }
  }
};

std::unique_ptr<HighResScreenshot> HighResScreenshot::Create(int scale,
                                                             std::function<void(bool)> mute)
{
  std::unique_ptr<HighResScreenshot> screenshot(new HighResScreenshot());
  auto shared = std::make_shared<Shared>();
  shared->scale = scale;
  shared->mute = std::move(mute);
  screenshot->m_shared = shared;
  // After Dolphin's own handlers of the event (registered with the video
  // backend): its settings are applied and its screenshot flushed by then.
  screenshot->m_frame_hook = GetVideoEvents().after_frame_event.Register(
      [shared](Core::System&) { shared->OnFrame(shared); });
  screenshot->m_watcher = std::thread([shared] { shared->Watch(shared); });
  std::lock_guard lock(s_active_mutex);
  s_active = screenshot.get();
  return screenshot;
}

HighResScreenshot::~HighResScreenshot()
{
  {
    std::lock_guard lock(s_active_mutex);
    if (s_active == this)
      s_active = nullptr;
  }
  // Waits for a frame handler running on the video thread.
  m_frame_hook.reset();
  {
    std::lock_guard lock(m_shared->mutex);
    m_shared->stop = true;
  }
  m_shared->wake.notify_all();
  if (m_watcher.joinable())
    m_watcher.join();
  // Jobs still queued find it stopped and leave the game alone (they run on
  // this thread, the host's); the mute callback may point at what is going
  // away with the runtime.
  m_shared->mute = nullptr;
  s_busy = false;
  s_blocks_input = false;
}

bool HighResScreenshot::Take()
{
  {
    std::lock_guard lock(m_shared->mutex);
    // One at a time, the last one written first.
    if (m_shared->busy || m_shared->stop || !m_shared->writing.empty())
      return false;
    m_shared->busy = true;
    m_shared->give_up_at = Clock::now() + TIME_LIMIT;
    m_shared->status.state = ScreenshotStatus::State::Taking;
    ++m_shared->status.count;
    m_shared->status.file.clear();
    m_shared->status.width = 0;
    m_shared->status.height = 0;
    s_busy = true;
  }
  m_shared->wake.notify_all();
  Core::QueueHostJob([shared = m_shared](Core::System& system) { shared->Start(system); });
  return true;
}

ScreenshotStatus HighResScreenshot::Status() const
{
  std::lock_guard lock(m_shared->mutex);
  ScreenshotStatus status = m_shared->status;
  if (status.state == ScreenshotStatus::State::Saved && !m_shared->path.empty())
    status.file = std::filesystem::path(m_shared->path).filename().string();
  return status;
}

void TakeScreenshot()
{
  std::lock_guard lock(s_active_mutex);
  if (s_active)
    s_active->Take();
  else
    Core::SaveScreenShot();
}

bool ScreenshotBusy()
{
  return s_busy;
}

bool ScreenshotBlocksInput()
{
  return s_blocks_input;
}

bool ResumeAfterScreenshot()
{
  std::lock_guard lock(s_active_mutex);
  if (!s_active)
    return false;
  // Host thread, as Start and Finish.
  HighResScreenshot::Shared& shared = *s_active->m_shared;
  if (!shared.resumed)
    return false;
  shared.keep_running = true;
  return true;
}
}  // namespace moderngekko::detail
