// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// This fork's copy of vendor/dolphin/Source/Core/DolphinNoGUI/PlatformX11.cpp
// (RecompCore cf8ceb8). The changes: the savestate keys (F1-F8 and
// Shift+F1-F8, F11, F12) exist only when the build keeps savestates
// (MODERNGEKKO_SAVESTATES, CMakeLists.txt); F9 takes the runtime's
// screenshot, at the resolution chosen for screenshots
// (src/runtime/screenshot_keys.hpp), and F10 (pause) waits while one is taken;
// the window opens at the size chosen in the launcher, or one that fits the
// primary monitor, in its middle (src/runtime/window_size.hpp), or in the
// middle of the launcher's window, on its monitor, when the launcher names it
// (RuntimeConfig::centre_on_window), with the program's icon
// (Resources/app-icon.png, MODERNGEKKO_APP_ICON), and asks for the focus once
// the launcher has stepped aside. Alt+Enter (fullscreen) and Ctrl+Esc (quit)
// stay.

// Without the CMake option (CMakeLists.txt always passes it), the window keeps
// its savestate keys, as before.
#ifndef MODERNGEKKO_SAVESTATES
#define MODERNGEKKO_SAVESTATES 1
#endif

#include <unistd.h>

// X.h defines None to be 0L, but other parts of Dolphin undef that so that
// None can be used in enums.  Work around that here by copying the definition
// before it is undefined.
#include <X11/X.h>
static constexpr auto X_None = None;

#include "DolphinNoGUI/Platform.h"

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Image.h"
#include "Common/MsgHandler.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#if MODERNGEKKO_SAVESTATES
#include "Core/State.h"
#endif
#include "Core/System.h"
#include "../runtime/screenshot_keys.hpp"
#include "../runtime/window_size.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "UICommon/UICommon.h"
#include "UICommon/X11Utils.h"
#include "VideoCommon/Present.h"

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#endif

namespace
{
// The program's icon, Resources/app-icon.png beside the executable, as the
// window's _NET_WM_ICON (its icon in the taskbar and the window list), at
// 128, 64 and 32 pixels. Nothing without the file.
void SetWindowIcon(Display* display, Window window)
{
  std::string file;
  if (!File::ReadFileToString(File::GetExeDirectory() + "/Resources/app-icon.png", file))
    return;
  Common::UniqueBuffer<u8> rgba;
  u32 width = 0;
  u32 height = 0;
  if (!Common::LoadPNG(std::span(reinterpret_cast<const u8*>(file.data()), file.size()), &rgba,
                       &width, &height) ||
      width == 0 || height == 0)
    return;
  // Each size: width, height, then a row-major ARGB pixel per long, each the
  // average of the picture's pixels it covers.
  std::vector<unsigned long> icon;
  for (const u32 size : {128u, 64u, 32u})
  {
    icon.push_back(size);
    icon.push_back(size);
    for (u32 y = 0; y < size; ++y)
    {
      const u32 y0 = y * height / size;
      const u32 y1 = std::max(y0 + 1, (y + 1) * height / size);
      for (u32 x = 0; x < size; ++x)
      {
        const u32 x0 = x * width / size;
        const u32 x1 = std::max(x0 + 1, (x + 1) * width / size);
        unsigned long sum[4] = {};
        for (u32 sy = y0; sy < y1; ++sy)
        {
          for (u32 sx = x0; sx < x1; ++sx)
          {
            const u8* pixel = rgba.data() + (static_cast<std::size_t>(sy) * width + sx) * 4;
            for (int channel = 0; channel < 4; ++channel)
              sum[channel] += pixel[channel];
          }
        }
        const unsigned long count = static_cast<unsigned long>(y1 - y0) * (x1 - x0);
        icon.push_back(((sum[3] / count) << 24) | ((sum[0] / count) << 16) |
                       ((sum[1] / count) << 8) | (sum[2] / count));
      }
    }
  }
  XChangeProperty(display, window, XInternAtom(display, "_NET_WM_ICON", False), XA_CARDINAL, 32,
                  PropModeReplace, reinterpret_cast<unsigned char*>(icon.data()),
                  static_cast<int>(icon.size()));
}

class PlatformX11 : public Platform
{
public:
  ~PlatformX11() override;

  bool Init() override;
  void SetTitle(const std::string& string) override;
  void MainLoop() override;

  WindowSystemInfo GetWindowSystemInfo() const override;

private:
  void CloseDisplay();
  void UpdateWindowPosition();
  void ProcessEvents();
  void FocusAfterLauncher();

  Display* m_display = nullptr;
  Window m_window = {};
  Cursor m_blank_cursor = X_None;
#ifdef HAVE_XRANDR
  X11Utils::XRRConfiguration* m_xrr_config = nullptr;
#endif
  int m_window_x = Config::Get(Config::MAIN_RENDER_WINDOW_XPOS);
  int m_window_y = Config::Get(Config::MAIN_RENDER_WINDOW_YPOS);
  unsigned int m_window_width = Config::Get(Config::MAIN_RENDER_WINDOW_WIDTH);
  unsigned int m_window_height = Config::Get(Config::MAIN_RENDER_WINDOW_HEIGHT);
  // Waiting for the launcher to step aside (FocusAfterLauncher), checked
  // every so often until then.
  bool m_focus_after_launcher = false;
  std::chrono::steady_clock::time_point m_opened_at;
  std::chrono::steady_clock::time_point m_next_launcher_check;
};

PlatformX11::~PlatformX11()
{
#ifdef HAVE_XRANDR
  delete m_xrr_config;
#endif

  if (m_display)
  {
    if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never)
      XFreeCursor(m_display, m_blank_cursor);

    XCloseDisplay(m_display);
  }
}

namespace
{
// The monitor with `point` on it, else the primary monitor (the first monitor
// when none is primary): its place on the desktop, or the whole screen
// without XRandR 1.5.
moderngekko::detail::ScreenArea MonitorAt(Display* display,
                                          std::optional<std::pair<int, int>> point)
{
#ifdef HAVE_XRANDR
  int event_base = 0;
  int error_base = 0;
  int major = 0;
  int minor = 0;
  if (XRRQueryExtension(display, &event_base, &error_base) &&
      XRRQueryVersion(display, &major, &minor) && (major > 1 || (major == 1 && minor >= 5)))
  {
    moderngekko::detail::ScreenArea area;
    int count = 0;
    if (XRRMonitorInfo* monitors =
            XRRGetMonitors(display, DefaultRootWindow(display), True, &count))
    {
      bool primary_found = false;
      for (int i = 0; i < count; ++i)
      {
        const XRRMonitorInfo& monitor = monitors[i];
        if (point && point->first >= monitor.x && point->first < monitor.x + monitor.width &&
            point->second >= monitor.y && point->second < monitor.y + monitor.height)
        {
          area = {monitor.x, monitor.y, monitor.width, monitor.height};
          break;
        }
        if (!primary_found && (i == 0 || monitor.primary))
        {
          area = {monitor.x, monitor.y, monitor.width, monitor.height};
          primary_found = monitor.primary;
        }
      }
      XRRFreeMonitors(monitors);
    }
    if (area.width > 0 && area.height > 0)
      return area;
  }
#endif
  const int screen = DefaultScreen(display);
  return {0, 0, DisplayWidth(display, screen), DisplayHeight(display, screen)};
}

// Asked of a window that may have gone: an X error then, which by default
// ends the program, so errors are ignored while it is asked.
int IgnoreXError(Display*, XErrorEvent*)
{
  return 0;
}

// The middle of the window RuntimeConfig::centre_on_window names (the
// launcher's), on the desktop; nothing without one, or when it has gone.
std::optional<std::pair<int, int>> CentreOfWindowToCentreOn(Display* display)
{
  const std::uint64_t id = moderngekko::detail::WindowToCentreOn();
  if (id == 0)
    return std::nullopt;
  const Window window = static_cast<Window>(id);
  XSync(display, False);
  const XErrorHandler previous = XSetErrorHandler(IgnoreXError);
  XWindowAttributes attributes{};
  int x = 0;
  int y = 0;
  Window child = 0;
  const bool found = XGetWindowAttributes(display, window, &attributes) != 0 &&
                     XTranslateCoordinates(display, window, DefaultRootWindow(display), 0, 0,
                                           &x, &y, &child) != 0;
  XSync(display, False);
  XSetErrorHandler(previous);
  if (!found || attributes.width <= 0 || attributes.height <= 0)
    return std::nullopt;
  return std::pair{x + attributes.width / 2, y + attributes.height / 2};
}
}  // namespace

bool PlatformX11::Init()
{
  XInitThreads();
  m_display = XOpenDisplay(nullptr);
  if (!m_display)
  {
    PanicAlertFmt("No X11 display found");
    return false;
  }

  // The size chosen in the launcher, or one that fits the screen, in the
  // middle of the launcher's window, on its monitor, or of the primary
  // monitor.
  const std::optional<std::pair<int, int>> centre = CentreOfWindowToCentreOn(m_display);
  const moderngekko::detail::ScreenArea screen = MonitorAt(m_display, centre);
  const moderngekko::detail::WindowPlace place =
      centre ? moderngekko::detail::PlaceWindowAt(moderngekko::detail::RequestedWindowSize(),
                                                  screen, centre->first, centre->second) :
               moderngekko::detail::PlaceWindow(moderngekko::detail::RequestedWindowSize(), screen);
  m_window_x = place.x;
  m_window_y = place.y;
  m_window_width = static_cast<unsigned int>(place.width);
  m_window_height = static_cast<unsigned int>(place.height);

  m_window = XCreateSimpleWindow(m_display, DefaultRootWindow(m_display), m_window_x, m_window_y,
                                 m_window_width, m_window_height, 0, 0, BlackPixel(m_display, 0));
  // Window managers place windows as they like, unless told the program
  // asked for this place and size.
  if (XSizeHints* hints = XAllocSizeHints())
  {
    hints->flags = PPosition | PSize;
    hints->x = m_window_x;
    hints->y = m_window_y;
    hints->width = static_cast<int>(m_window_width);
    hints->height = static_cast<int>(m_window_height);
    XSetWMNormalHints(m_display, m_window, hints);
    XFree(hints);
  }
  XSelectInput(m_display, m_window, StructureNotifyMask | KeyPressMask | FocusChangeMask);
  Atom wmProtocols[1];
  wmProtocols[0] = XInternAtom(m_display, "WM_DELETE_WINDOW", True);
  XSetWMProtocols(m_display, m_window, wmProtocols, 1);
  pid_t pid = getpid();
  XChangeProperty(m_display, m_window, XInternAtom(m_display, "_NET_WM_PID", False), XA_CARDINAL,
                  32, PropModeReplace, reinterpret_cast<unsigned char*>(&pid), 1);
  char host_name[HOST_NAME_MAX] = "";
  if (!gethostname(host_name, sizeof(host_name)))
  {
    XTextProperty wmClientMachine = {reinterpret_cast<unsigned char*>(host_name), XA_STRING, 8,
                                     strlen(host_name)};
    XSetWMClientMachine(m_display, m_window, &wmClientMachine);
  }
  SetWindowIcon(m_display, m_window);
  XMapRaised(m_display, m_window);
  m_focus_after_launcher = moderngekko::detail::WindowToCentreOn() != 0;
  m_opened_at = std::chrono::steady_clock::now();
  XFlush(m_display);
  XSync(m_display, True);
  ProcessEvents();

  if (Config::Get(Config::MAIN_DISABLE_SCREENSAVER))
    UICommon::InhibitScreenSaver(true);

#ifdef HAVE_XRANDR
  m_xrr_config = new X11Utils::XRRConfiguration(m_display, m_window);
#endif

  if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never)
  {
    // make a blank cursor
    Pixmap Blank;
    XColor DummyColor;
    char ZeroData[1] = {0};
    Blank = XCreateBitmapFromData(m_display, m_window, ZeroData, 1, 1);
    m_blank_cursor = XCreatePixmapCursor(m_display, Blank, Blank, &DummyColor, &DummyColor, 0, 0);
    XFreePixmap(m_display, Blank);
    XDefineCursor(m_display, m_window, m_blank_cursor);
  }

  // Enter fullscreen if enabled.
  if (Config::Get(Config::MAIN_FULLSCREEN))
  {
    m_window_fullscreen = X11Utils::ToggleFullscreen(m_display, m_window);
#ifdef HAVE_XRANDR
    m_xrr_config->ToggleDisplayMode(True);
#endif
    ProcessEvents();
  }

  UpdateWindowPosition();
  return true;
}

void PlatformX11::SetTitle(const std::string& string)
{
  XStoreName(m_display, m_window, string.c_str());
}

void PlatformX11::MainLoop()
{
  while (IsRunning())
  {
    UpdateRunningFlag();
    Core::HostDispatchJobs(Core::System::GetInstance());
    ProcessEvents();
    UpdateWindowPosition();
    FocusAfterLauncher();

    // TODO: Is this sleep appropriate?
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

WindowSystemInfo PlatformX11::GetWindowSystemInfo() const
{
  WindowSystemInfo wsi;
  wsi.type = WindowSystemType::X11;
  wsi.display_connection = static_cast<void*>(m_display);
  wsi.render_window = reinterpret_cast<void*>(m_window);
  wsi.render_surface = reinterpret_cast<void*>(m_window);
  return wsi;
}

// The launcher stays over this window, with its loading screen, until the
// game's first picture, and then hides. The window manager may not have given
// this window the focus meanwhile, the launcher being the window in use, and
// without the focus the game reads no controller: once the launcher is gone,
// this window asks for it, as a task bar would when it is clicked.
void PlatformX11::FocusAfterLauncher()
{
  if (!m_focus_after_launcher)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < m_next_launcher_check)
    return;
  m_next_launcher_check = now + std::chrono::milliseconds(100);
  // The launcher steps aside 40 seconds at most after the game starts.
  if (now - m_opened_at > std::chrono::minutes(2))
  {
    m_focus_after_launcher = false;
    return;
  }
  const Window launcher = static_cast<Window>(moderngekko::detail::WindowToCentreOn());
  XSync(m_display, False);
  const XErrorHandler previous = XSetErrorHandler(IgnoreXError);
  XWindowAttributes attributes{};
  const bool shown = XGetWindowAttributes(m_display, launcher, &attributes) != 0 &&
                     attributes.map_state == IsViewable;
  XSync(m_display, False);
  XSetErrorHandler(previous);
  if (shown)
    return;
  m_focus_after_launcher = false;
  Window focused = 0;
  int revert_to = 0;
  XGetInputFocus(m_display, &focused, &revert_to);
  if (focused == m_window)
    return;
  XEvent event{};
  event.xclient.type = ClientMessage;
  event.xclient.window = m_window;
  event.xclient.message_type = XInternAtom(m_display, "_NET_ACTIVE_WINDOW", False);
  event.xclient.format = 32;
  event.xclient.data.l[0] = 2;  // from a task bar or a pager
  event.xclient.data.l[1] = CurrentTime;
  XSendEvent(m_display, DefaultRootWindow(m_display), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &event);
  XFlush(m_display);
}

void PlatformX11::UpdateWindowPosition()
{
  if (m_window_fullscreen)
    return;

  Window winDummy;
  unsigned int borderDummy, depthDummy;
  XGetGeometry(m_display, m_window, &winDummy, &m_window_x, &m_window_y, &m_window_width,
               &m_window_height, &borderDummy, &depthDummy);
}

void PlatformX11::ProcessEvents()
{
  XEvent event;
  KeySym key;
  for (int num_events = XPending(m_display); num_events > 0; num_events--)
  {
    XNextEvent(m_display, &event);
    switch (event.type)
    {
    case KeyPress:
      key = XLookupKeysym((XKeyEvent*)&event, 0);
      if (key == XK_Escape && (event.xkey.state & ControlMask))
      {
        RequestShutdown();
      }
      // A paused game may be running a few frames for a screenshot.
      else if (key == XK_F10 && !moderngekko::detail::ScreenshotBusy())
      {
        if (Core::GetState(Core::System::GetInstance()) == Core::State::Running)
        {
          if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never)
            XUndefineCursor(m_display, m_window);
          Core::SetState(Core::System::GetInstance(), Core::State::Paused);
        }
        else
        {
          if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never)
            XDefineCursor(m_display, m_window, m_blank_cursor);
          Core::SetState(Core::System::GetInstance(), Core::State::Running);
        }
      }
      else if ((key == XK_Return) && (event.xkey.state & Mod1Mask))
      {
        m_window_fullscreen = !m_window_fullscreen;
        X11Utils::ToggleFullscreen(m_display, m_window);
#ifdef HAVE_XRANDR
        m_xrr_config->ToggleDisplayMode(m_window_fullscreen);
#endif
        UpdateWindowPosition();
      }
#if MODERNGEKKO_SAVESTATES
      else if (key >= XK_F1 && key <= XK_F8)
      {
        int slot_number = key - XK_F1 + 1;
        if (event.xkey.state & ShiftMask)
          State::Save(Core::System::GetInstance(), slot_number);
        else
          State::Load(Core::System::GetInstance(), slot_number);
      }
#endif
      else if (key == XK_F9)
        moderngekko::detail::TakeScreenshot();
#if MODERNGEKKO_SAVESTATES
      else if (key == XK_F11)
        State::LoadLastSaved(Core::System::GetInstance());
      else if (key == XK_F12)
      {
        if (event.xkey.state & ShiftMask)
          State::UndoLoadState(Core::System::GetInstance());
        else
          State::UndoSaveState(Core::System::GetInstance());
      }
#endif
      break;
    case FocusIn:
    {
      m_window_focus = true;
      if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never &&
          Core::GetState(Core::System::GetInstance()) != Core::State::Paused)
      {
        XDefineCursor(m_display, m_window, m_blank_cursor);
      }
    }
    break;
    case FocusOut:
    {
      m_window_focus = false;
      if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never)
        XUndefineCursor(m_display, m_window);
    }
    break;
    case ClientMessage:
    {
      if ((unsigned long)event.xclient.data.l[0] ==
          XInternAtom(m_display, "WM_DELETE_WINDOW", False))
        Stop();
    }
    break;
    case ConfigureNotify:
    {
      if (g_presenter)
        g_presenter->ResizeSurface();
    }
    break;
    }
  }
}
}  // namespace

std::unique_ptr<Platform> Platform::CreateX11Platform()
{
  return std::make_unique<PlatformX11>();
}
