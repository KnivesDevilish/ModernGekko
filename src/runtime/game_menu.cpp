#include "game_menu.hpp"

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Common/IniFile.h"
#include "Common/Thread.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/NetPlay/NetPlayProto.h"
#include "Core/System.h"
#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerEmu/Control/Control.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "VideoCommon/AsyncRequests.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoConfig.h"
#include "game_menu_controls.hpp"
#include "game_menu_draw.hpp"
#include "game_menu_state.hpp"
#include "launcher_look.hpp"
#include "launcher_text.hpp"
#include "screenshot.hpp"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace moderngekko::detail
{
namespace
{
using Clock = std::chrono::steady_clock;

// How long the menu takes to fade in.
constexpr std::chrono::milliseconds FADE_TIME{140};
// How often the controller is read: closed, only the menu button matters.
constexpr std::chrono::milliseconds POLL_OPEN{8};
constexpr std::chrono::milliseconds POLL_CLOSED{16};
// A paused game under the open menu is drawn again when the menu changes, and
// this often anyway, so it follows a resized window.
constexpr std::chrono::milliseconds REDRAW_INTERVAL{100};
// How long the line saying a screenshot was saved stays, and how long it
// takes to fade out at the end.
constexpr std::chrono::milliseconds NOTICE_TIME{4000};
constexpr std::chrono::milliseconds NOTICE_FADE{400};

std::atomic<bool> s_blocks_input{false};

// The controls the menu reads, made from the profile keys it is given: a
// group per profile group ("Buttons", "D-Pad", "Extra"...) with the controls
// those keys name, so EmulatedController loads their inputs from the player's
// profile the way Dolphin loads the game's controller. Dolphin's own GameCube
// controller has no "Extra" group and skips those lines.
class MenuController final : public ControllerEmu::EmulatedController
{
public:
  std::string GetName() const override { return "GameMenu"; }
  InputConfig* GetConfig() const override { return nullptr; }

  // The control of a profile key such as "Buttons/A"; nullptr for a key
  // without a group.
  ControllerEmu::Control* ControlOf(std::string_view key)
  {
    const std::size_t slash = key.rfind('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 == key.size())
      return nullptr;
    return ControlOf(key.substr(0, slash), key.substr(slash + 1));
  }

  ControllerEmu::Control* ControlOf(std::string_view group_name, std::string_view control_name)
  {
    ControllerEmu::ControlGroup* group = nullptr;
    for (const auto& existing : groups)
    {
      if (existing->name == group_name)
        group = existing.get();
    }
    if (!group)
    {
      groups.push_back(std::make_unique<ControllerEmu::ControlGroup>(
          std::string(group_name), ControllerEmu::GroupType::Buttons));
      group = groups.back().get();
    }
    for (const auto& control : group->controls)
    {
      if (control->name == control_name)
        return control.get();
    }
    group->AddInput(ControllerEmu::Translatability::DoNotTranslate, std::string(control_name));
    return group->controls.back().get();
  }
};

std::vector<ControllerEmu::Control*> Controls(MenuController* controller,
                                              const std::vector<std::string>& keys)
{
  std::vector<ControllerEmu::Control*> controls;
  for (const std::string& key : keys)
  {
    if (ControllerEmu::Control* control = controller->ControlOf(key))
      controls.push_back(control);
  }
  return controls;
}

bool AnyHeld(const std::vector<ControllerEmu::Control*>& controls)
{
  return std::ranges::any_of(controls, [](const ControllerEmu::Control* control) {
    return control->GetState<bool>();
  });
}

bool HasInput(const std::vector<ControllerEmu::Control*>& controls)
{
  return std::ranges::any_of(controls, [](const ControllerEmu::Control* control) {
    return !control->control_ref->GetExpression().empty();
  });
}

// What the menu thread hands the video thread to draw.
struct View
{
  MenuState state;
  Clock::time_point opened_at;
  // How the last screenshot went: shown while it is taken (lasting), then
  // until notice_until.
  GameMenuNotice notice;
  bool notice_lasting = false;
  Clock::time_point notice_until;

  bool NoticeShown(Clock::time_point now) const
  {
    return !notice.text.empty() && (notice_lasting || now < notice_until);
  }
};

GameMenuNotice NoticeOf(const ScreenshotStatus& status)
{
  using moderngekko::frontend::Format;
  using moderngekko::frontend::Tr;
  GameMenuNotice notice;
  switch (status.state)
  {
  case ScreenshotStatus::State::Idle:
    break;
  case ScreenshotStatus::State::Taking:
    notice.text = Tr("Taking screenshot...");
    break;
  case ScreenshotStatus::State::Saved:
    notice.kind = GameMenuNotice::Kind::Done;
    notice.text = Format(Tr("Screenshot saved \xC2\xB7 %d x %d"), status.width, status.height);
    break;
  case ScreenshotStatus::State::Failed:
    notice.kind = GameMenuNotice::Kind::Failed;
    notice.text = Tr("Couldn't take the screenshot");
    break;
  }
  return notice;
}
}  // namespace

struct GameMenu::Impl
{
  // Shared with the jobs the menu hands to Dolphin's threads (Pause, Resume,
  // RedrawPaused), which can run after the menu is gone; menu is then null.
  struct Shared
  {
    std::mutex mutex;
    Impl* menu = nullptr;
    std::function<void()> draw_others;
    std::atomic<bool> redraw_queued{false};
    // Set by the job that paused the game, so the menu resumes only a game
    // it paused itself, not one already paused with F10.
    std::atomic<bool> paused_by_menu{false};

    // On the video thread: the paused game's last frame, the border and the
    // menu, shown again.
    void DrawPausedFrame()
    {
      {
        std::lock_guard lock(mutex);
        if (menu && g_presenter)
        {
          // What a running game does after each frame: a quick setting
          // changed while paused (an effect, the resolution) shows at once.
          CheckForConfigChanges();
          if (draw_others)
            draw_others();
          menu->AddToFrame();
          g_presenter->Present();
        }
      }
      redraw_queued = false;
    }
  };

  GameMenuSettings settings;
  GameMenuHooks hooks;
  bool netplay = false;
  std::shared_ptr<Shared> shared = std::make_shared<Shared>();

  // The Controls page, read from the profile with the controls (then only
  // read, by the video thread too).
  std::vector<ControlsRow> controls;
  std::string controller_name;

  // The menu thread's, apart from view.
  MenuController controller;
  std::vector<ControllerEmu::Control*> open_controls;
  std::vector<ControllerEmu::Control*> accept_controls;
  std::vector<ControllerEmu::Control*> back_controls;
  std::vector<ControllerEmu::Control*> up_controls;
  std::vector<ControllerEmu::Control*> down_controls;
  std::vector<ControllerEmu::Control*> left_controls;
  std::vector<ControllerEmu::Control*> right_controls;
  Common::EventHook devices_hook;
  MenuButtonRepeat repeat;
  bool resume_pending = false;
  bool leaving = false;
  unsigned drawn_version = 0;
  Clock::time_point last_redraw;
  ScreenshotStatus last_status;
  bool notice_was_shown = false;
  std::atomic<GameMenuChoice> choice{GameMenuChoice::None};

  std::mutex view_mutex;
  View view;

  // The video thread's.
  GameMenuFonts fonts;
  bool fonts_loaded = false;
  int last_frame = -1;

  std::jthread thread;

  void LoadControls()
  {
    open_controls = Controls(&controller, settings.open_keys);
    accept_controls = Controls(&controller, settings.accept_keys);
    back_controls = Controls(&controller, settings.back_keys);
    up_controls = Controls(&controller, settings.up_keys);
    down_controls = Controls(&controller, settings.down_keys);
    left_controls = Controls(&controller, settings.left_keys);
    right_controls = Controls(&controller, settings.right_keys);

    Common::IniFile profile;
    profile.Load(File::GetUserPath(D_CONFIG_IDX) + settings.profile_file);
    Common::IniFile::Section* section = profile.GetOrCreateSection(settings.profile_section);
    controller.LoadConfig(section);
    // The Controls page shows the profile as it is now.
    std::string device;
    section->Get("Device", &device);
    controls = ReadControls(
        settings.controls,
        [section](const std::string& key) -> std::optional<std::string> {
          std::string value;
          if (!section->Get(key, &value))
            return std::nullopt;
          return value;
        },
        device);
    controller_name = PadDeviceName(device);
    // An older profile may have no menu button: the fallback's, then.
    if (!HasInput(open_controls) && !settings.open_fallback.empty())
    {
      ControllerEmu::Control* fallback = controller.ControlOf("Fallback", "Menu");
      fallback->control_ref->SetExpression(settings.open_fallback);
      open_controls.push_back(fallback);
    }
    controller.UpdateReferences(g_controller_interface);
    // A pad plugged in or out later is found again, as for the game.
    devices_hook = g_controller_interface.RegisterDevicesChangedCallback(
        [this] { controller.UpdateReferences(g_controller_interface); });
  }

  MenuButtons Held()
  {
    MenuButtons held;
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    held.open = AnyHeld(open_controls);
    held.accept = AnyHeld(accept_controls);
    held.back = AnyHeld(back_controls);
    held.up = AnyHeld(up_controls);
    held.down = AnyHeld(down_controls);
    held.left = AnyHeld(left_controls);
    held.right = AnyHeld(right_controls);
    return held;
  }

  // Core::SetState is called on the host thread (the game window's loop), as
  // Dolphin's own pause key does.
  void Pause()
  {
    Core::QueueHostJob([shared = shared](Core::System& system) {
      if (Core::GetState(system) != Core::State::Running)
        return;
      Core::SetState(system, Core::State::Paused);
      shared->paused_by_menu = true;
    });
  }

  void Resume()
  {
    Core::QueueHostJob([shared = shared](Core::System& system) {
      if (!shared->paused_by_menu.exchange(false))
        return;
      // A paused game running for a screenshot just keeps running after it.
      if (ResumeAfterScreenshot())
        return;
      if (Core::GetState(system) == Core::State::Paused)
        Core::SetState(system, Core::State::Running);
    });
  }

  // A paused game presents no frames, so the menu has its last one shown again
  // with the menu over it: the job goes to the emulated CPU's thread (which
  // runs jobs while paused) and from there to the video thread, the way
  // Dolphin's CPU thread hands it work.
  void RedrawPaused()
  {
    if (shared->redraw_queued.exchange(true))
      return;
    Core::QueueHostJob([shared = shared](Core::System& system) {
      if (Core::GetState(system) != Core::State::Paused)
      {
        shared->redraw_queued = false;
        return;
      }
      Core::RunOnCPUThread(system, [shared] {
        AsyncRequests::GetInstance()->PushEvent([shared] { shared->DrawPausedFrame(); });
      });
    });
  }

  // A quick setting took another choice: into the game at once, then kept
  // by the frontend for next time.
  void ChangeSetting(std::size_t row, std::size_t choice)
  {
    if (row >= settings.quick_settings.size())
      return;
    const QuickSetting& setting = settings.quick_settings[row];
    if (choice >= setting.choices.size())
      return;
    if (hooks.apply_setting)
      hooks.apply_setting(setting, choice);
    if (settings.keep_quick_setting)
      settings.keep_quick_setting(row, choice);
  }

  void Leave(GameMenuChoice how)
  {
    choice = how;
    leaving = true;
    if (hooks.stop_game)
      hooks.stop_game();
  }

  void Run(std::stop_token stop)
  {
    Common::SetCurrentThreadName("Game menu");
    // The host's channel, as Dolphin's hotkeys read the controllers: the
    // game's own reads keep theirs.
    ControllerInterface::SetCurrentInputChannel(ciface::InputChannel::Host);
    auto& system = Core::System::GetInstance();
    while (!stop.stop_requested())
    {
      // Like the game: no input while another window has the focus, unless
      // background input is on.
      ControlReference::SetInputGate(Config::Get(Config::MAIN_INPUT_BACKGROUND_INPUT) ||
                                     Host_RendererHasFocus());
      g_controller_interface.UpdateInput();
      const MenuButtons held = Held();
      const Clock::time_point now = Clock::now();
      const MenuButtons pressed = repeat.Update(held, now);
      const Core::State core_state = Core::GetState(system);
      const bool in_game = core_state == Core::State::Running || core_state == Core::State::Paused;
      // While a screenshot is taken (a paused game may be running for it),
      // the menu takes no presses.
      const bool screenshot_busy = ScreenshotBusy();
      std::optional<GameMenuNotice> new_notice;
      if (hooks.screenshot_status)
      {
        const ScreenshotStatus status = hooks.screenshot_status();
        if (status.state != last_status.state || status.count != last_status.count)
        {
          last_status = status;
          new_notice = NoticeOf(status);
        }
      }

      MenuEvent event = MenuEvent::None;
      bool open = false;
      unsigned version = 0;
      Clock::time_point opened_at;
      std::size_t changed_row = 0;
      std::size_t changed_choice = 0;
      bool notice_shown = false;
      {
        std::lock_guard lock(view_mutex);
        if (in_game && !leaving && !screenshot_busy)
        {
          event = view.state.Press(pressed);
          if (event == MenuEvent::Opened)
            view.opened_at = now;
        }
        if (new_notice)
        {
          view.notice = std::move(*new_notice);
          view.notice_lasting = last_status.state == ScreenshotStatus::State::Taking;
          view.notice_until = now + NOTICE_TIME;
        }
        notice_shown = view.NoticeShown(now);
        open = view.state.IsOpen();
        version = view.state.Version();
        opened_at = view.opened_at;
        changed_row = view.state.ChangedRow();
        if (changed_row < view.state.SettingCount())
          changed_choice = view.state.Chosen(changed_row);
      }

      switch (event)
      {
      case MenuEvent::Opened:
        s_blocks_input = true;
        resume_pending = false;
        // Netplay can't pause one player: the match goes on behind the menu.
        if (!netplay)
          Pause();
        break;
      case MenuEvent::Closed:
        resume_pending = true;
        break;
      case MenuEvent::Leave:
        Leave(GameMenuChoice::Launcher);
        break;
      case MenuEvent::Quit:
        Leave(GameMenuChoice::Quit);
        break;
      case MenuEvent::SettingChanged:
        ChangeSetting(changed_row, changed_choice);
        break;
      case MenuEvent::Screenshot:
        if (hooks.take_screenshot)
          hooks.take_screenshot();
        break;
      case MenuEvent::None:
        break;
      }
      // The game comes back once the button that closed the menu is let go,
      // so it doesn't get that press.
      if (resume_pending && !held.Any())
      {
        resume_pending = false;
        if (!netplay)
          Resume();
        s_blocks_input = false;
      }

      // The notice comes, fades and goes on a paused game too.
      const bool notice_redraw = new_notice.has_value() || notice_shown != notice_was_shown ||
                                 (notice_shown && now - last_redraw >= REDRAW_INTERVAL);
      notice_was_shown = notice_shown;
      if (core_state == Core::State::Paused && !leaving &&
          (version != drawn_version || notice_redraw ||
           (open && (now - opened_at < FADE_TIME || now - last_redraw >= REDRAW_INTERVAL))))
      {
        drawn_version = version;
        last_redraw = now;
        RedrawPaused();
      }
      std::this_thread::sleep_for(open || notice_shown ? POLL_OPEN : POLL_CLOSED);
    }
  }

  // The interface font of the launcher, with its Japanese characters, added
  // to Dolphin's on-screen UI the first time the menu is drawn. ImGui makes
  // each size it is drawn at when it is needed.
  void LoadFonts()
  {
    if (fonts_loaded)
      return;
    fonts_loaded = true;
    constexpr float SIZE = 32.0f;
    ImGuiIO& io = ImGui::GetIO();
    for (const frontend::ui::InterfaceFontFiles& candidate :
         frontend::ui::InterfaceFontCandidates(settings.release_directory))
    {
      fonts.regular = io.Fonts->AddFontFromFileTTF(candidate.regular.string().c_str(), SIZE);
      if (!fonts.regular)
        continue;
      frontend::ui::MergeJapaneseFont(false, SIZE);
      if (!candidate.bold.empty())
        fonts.bold = io.Fonts->AddFontFromFileTTF(candidate.bold.string().c_str(), SIZE);
      if (fonts.bold)
        frontend::ui::MergeJapaneseFont(true, SIZE);
      std::fprintf(stderr, "game menu font: %s\n", candidate.regular.string().c_str());
      return;
    }
    fonts.regular = io.Fonts->AddFontDefault();
    frontend::ui::MergeJapaneseFont(false, SIZE);
    std::fprintf(stderr, "game menu font: ImGui default\n");
  }

  void AddToFrame()
  {
    if (ImGui::GetFrameCount() == last_frame)
      return;
    last_frame = ImGui::GetFrameCount();
    View shown;
    {
      std::lock_guard lock(view_mutex);
      shown = view;
    }
    const Clock::time_point now = Clock::now();
    const bool notice = shown.NoticeShown(now);
    if (!shown.state.IsOpen() && !notice)
      return;
    LoadFonts();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (shown.state.IsOpen())
    {
      const float opening = std::chrono::duration<float>(now - shown.opened_at) /
                            std::chrono::duration<float>(FADE_TIME);
      const float eased = 1.0f - std::pow(1.0f - std::clamp(opening, 0.0f, 1.0f), 3.0f);
      GameMenuContent content;
      content.settings = settings.quick_settings;
      content.controls_title = settings.controls_title;
      content.controls = controls;
      content.controller_name = controller_name;
      content.netplay = netplay;
      DrawGameMenu(list, display, fonts, shown.state, content, eased);
    }
    if (notice)
    {
      const float alpha =
          shown.notice_lasting ?
              1.0f :
              std::chrono::duration<float>(shown.notice_until - now) /
                  std::chrono::duration<float>(NOTICE_FADE);
      DrawGameMenuNotice(list, display, fonts, shown.notice, alpha);
    }
  }
};

GameMenu::GameMenu(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

std::unique_ptr<GameMenu> GameMenu::Create(const GameMenuSettings& settings, GameMenuHooks hooks)
{
  if (!settings.enabled)
    return nullptr;
  auto impl = std::make_unique<Impl>();
  impl->settings = settings;
  impl->hooks = std::move(hooks);
  impl->netplay = NetPlay::IsNetPlayRunning();
  // The rows keep the frontend's order: keep_quick_setting is told rows by
  // their place in settings.quick_settings.
  std::vector<std::size_t> choice_counts;
  std::vector<std::size_t> chosen;
  for (const QuickSetting& setting : impl->settings.quick_settings)
  {
    choice_counts.push_back(setting.choices.size());
    chosen.push_back(setting.selected);
  }
  impl->view.state =
      MenuState(std::move(choice_counts), std::move(chosen),
                MenuRows{.controls = !impl->settings.controls.empty(),
                         .screenshot = static_cast<bool>(impl->hooks.take_screenshot)});
  impl->shared->menu = impl.get();
  impl->shared->draw_others = impl->hooks.draw_others;
  impl->LoadControls();
  if (!HasInput(impl->open_controls))
  {
    std::fprintf(stderr, "game menu: %s [%s] has no menu button; the menu is off\n",
                 settings.profile_file.c_str(), settings.profile_section.c_str());
    return nullptr;
  }
  Impl* raw = impl.get();
  impl->thread = std::jthread([raw](std::stop_token stop) { raw->Run(std::move(stop)); });
  return std::unique_ptr<GameMenu>(new GameMenu(std::move(impl)));
}

GameMenu::~GameMenu()
{
  m_impl->thread = {};
  m_impl->devices_hook = {};
  {
    std::lock_guard lock(m_impl->shared->mutex);
    m_impl->shared->menu = nullptr;
  }
  s_blocks_input = false;
}

void GameMenu::AddToFrame()
{
  m_impl->AddToFrame();
}

GameMenuChoice GameMenu::Choice() const
{
  return m_impl->choice;
}

bool GameMenuBlocksInput()
{
  return s_blocks_input;
}
}  // namespace moderngekko::detail
