// Copyright 2019 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// This fork's copy of vendor/dolphin/Source/Core/DolphinNoGUI/PlatformWin32.cpp
// (RecompCore cf8ceb8). The changes: the savestate key (F1) and the File
// menu's Save State and Load State exist only when the build keeps savestates
// (MODERNGEKKO_SAVESTATES, CMakeLists.txt), and so does holding Space to run
// the game faster; resource.h is included by its path under
// vendor/dolphin/Source/Core. And the keys and the window work as in the X11
// and Wayland copies: F9 takes the runtime's screenshot, at the resolution
// chosen for screenshots (src/runtime/screenshot_keys.hpp); F10 (pause), as
// the File menu's Pause, waits while one is taken; Alt+Enter and F11 switch
// to fullscreen and back (Escape leaves it); Ctrl+Escape closes the game,
// Escape alone no longer does. The window opens at the size chosen in the
// launcher, or one that fits the primary monitor, in its middle
// (src/runtime/window_size.hpp), or in the middle of the launcher's window, on
// its monitor, when the launcher names it (RuntimeConfig::centre_on_window),
// and fullscreen when the launcher says so.
// Whether it has the focus is known, so the game reads the controllers only
// then, unless background input is on. The window has no sunken edge and a
// black background (no white line around the picture, in a window or in
// fullscreen), the program's icon (icon 101, MODERNGEKKO_APP_ICON_ICO), and
// its File and View menu bar only with MODERNGEKKO_WINDOW_MENU.

// Without the CMake option (CMakeLists.txt always passes it), the window keeps
// its savestate key and menu entries, as before.
#ifndef MODERNGEKKO_SAVESTATES
#define MODERNGEKKO_SAVESTATES 1
#endif

#include "DolphinNoGUI/Platform.h"

#include "Core/Config/MainSettings.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Core.h"
#if MODERNGEKKO_SAVESTATES
#include "Core/SavestateLayout.h"
#include "Core/State.h"
#endif
#include "Core/System.h"

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "../runtime/screenshot_keys.hpp"
#include "../runtime/window_size.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>
#include <climits>
#include <dwmapi.h>
#include <thread>

#include "VideoCommon/Present.h"
#include "DolphinNoGUI/resource.h"

namespace
{
// Menu command ids. Load State entries are allocated a contiguous range,
// since the list is rebuilt from disk each time the menu opens.
#if MODERNGEKKO_SAVESTATES
constexpr UINT ID_SAVE_STATE = 41001;
#endif
constexpr UINT ID_PAUSE = 41002;
constexpr UINT ID_MUTE = 41003;
constexpr UINT ID_FULLSCREEN = 41004;
#if MODERNGEKKO_SAVESTATES
constexpr UINT ID_LOAD_STATE_FIRST = 41100;
constexpr UINT ID_LOAD_STATE_LAST = 41199;

// Hold-to-fast-forward target. 2x is fast enough to skip a cutscene without
// outrunning what most hosts can actually emulate.
constexpr float FAST_FORWARD_SPEED = 2.0f;
#endif

// The window's style, as CreateRenderWindow makes it: no sunken edge
// (WS_EX_CLIENTEDGE), which drew a light line around the game's picture.
constexpr DWORD WINDOW_STYLE = WS_OVERLAPPEDWINDOW;
constexpr DWORD WINDOW_EX_STYLE = 0;
constexpr BOOL WINDOW_HAS_MENU = MODERNGEKKO_WINDOW_MENU ? TRUE : FALSE;

// Bit 30 of a key message's lParam: the key was already down, so this is the
// keyboard's auto-repeat.
bool IsRepeat(LPARAM lParam)
{
  return (static_cast<ULONG_PTR>(lParam) & (1u << 30)) != 0;
}

// Pauses the game, or lets it run again. Not while a screenshot is taken: a
// paused game may be running a few frames for it.
void TogglePause()
{
  if (moderngekko::detail::ScreenshotBusy())
    return;
  auto& system = Core::System::GetInstance();
  const bool paused = Core::GetState(system) == Core::State::Paused;
  Core::SetState(system, paused ? Core::State::Running : Core::State::Paused);
}

class PlatformWin32 final : public Platform
{
public:
  ~PlatformWin32() override;

  bool Init() override;
  void SetTitle(const std::string& string) override;
  void MainLoop() override;

  WindowSystemInfo GetWindowSystemInfo() const override;

private:
  static constexpr TCHAR WINDOW_CLASS_NAME[] = _T("DolphinNoGUI");

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  static bool RegisterRenderWindowClass();
  bool CreateRenderWindow();
  bool CreateMenus();
  void RefreshMenu(HMENU menu);
#if MODERNGEKKO_SAVESTATES
  void SaveStateToStatesDirectory();
#endif
  void ToggleFullscreen();
  void UpdateWindowPosition();
  void ProcessEvents();

  HWND m_hwnd{};
  HMENU m_menu{};
  HMENU m_file_menu{};
#if MODERNGEKKO_SAVESTATES
  HMENU m_load_menu{};
#endif
  HMENU m_view_menu{};
#if MODERNGEKKO_SAVESTATES
  // Parallel to the Load State menu entries, rebuilt whenever it opens.
  std::vector<std::string> m_load_state_paths;
  std::time_t m_last_save_time{};
  std::size_t m_save_sequence{};
#endif
  bool m_fullscreen = false;
  LONG m_windowed_style = 0;
  RECT m_windowed_rect{};

  int m_window_x = Config::Get(Config::MAIN_RENDER_WINDOW_XPOS);
  int m_window_y = Config::Get(Config::MAIN_RENDER_WINDOW_YPOS);
  int m_window_width = Config::Get(Config::MAIN_RENDER_WINDOW_WIDTH);
  int m_window_height = Config::Get(Config::MAIN_RENDER_WINDOW_HEIGHT);
};

PlatformWin32::~PlatformWin32()
{
  if (m_hwnd)
    DestroyWindow(m_hwnd);
}

bool PlatformWin32::RegisterRenderWindowClass()
{
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = 0;
  wc.lpfnWndProc = WndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = GetModuleHandle(nullptr);
  // The executable's own icon; nothing (the system's) when it has none.
  wc.hIcon = LoadIcon(wc.hInstance, IDI_ICON1);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // Black, as the game around its picture: the window is never white while
  // it opens or changes size.
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = WINDOW_CLASS_NAME;
  wc.hIconSm = static_cast<HICON>(LoadImage(wc.hInstance, IDI_ICON1, IMAGE_ICON,
                                            GetSystemMetrics(SM_CXSMICON),
                                            GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

  if (!RegisterClassEx(&wc))
  {
    MessageBox(nullptr, _T("Window registration failed."), _T("Error"), MB_ICONERROR | MB_OK);
    return false;
  }

  return true;
}

bool PlatformWin32::CreateRenderWindow()
{
  // The size chosen in the launcher, or one that fits the screen, in the
  // middle of the launcher's window, on its monitor's work area (the desktop
  // without its taskbar), or of the primary monitor's work area. The size is
  // the game's picture: the frame (and the menu bar, if any) goes around it,
  // and the whole window is centred. The launcher's window is asked here
  // rather than told by the launcher: Windows gives its place as this program
  // sees the desktop, which differs from the launcher's view when one of the
  // two scales for the screen's DPI and the other doesn't.
  const HWND launcher = reinterpret_cast<HWND>(
      static_cast<std::uintptr_t>(moderngekko::detail::WindowToCentreOn()));
  RECT launcher_rect{};
  const bool near_launcher =
      launcher && IsWindow(launcher) && GetWindowRect(launcher, &launcher_rect) &&
      launcher_rect.right > launcher_rect.left && launcher_rect.bottom > launcher_rect.top;
  moderngekko::detail::ScreenArea screen;
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (GetMonitorInfo(near_launcher ? MonitorFromWindow(launcher, MONITOR_DEFAULTTOPRIMARY) :
                                     MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY),
                     &monitor))
  {
    screen = {monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right - monitor.rcWork.left,
              monitor.rcWork.bottom - monitor.rcWork.top};
  }
  const moderngekko::detail::WindowPlace place =
      moderngekko::detail::PlaceWindow(moderngekko::detail::RequestedWindowSize(), screen);
  RECT outer{0, 0, place.width, place.height};
  AdjustWindowRectEx(&outer, WINDOW_STYLE, WINDOW_HAS_MENU, WINDOW_EX_STYLE);
  m_window_width = outer.right - outer.left;
  m_window_height = outer.bottom - outer.top;
  m_window_x = CW_USEDEFAULT;
  m_window_y = CW_USEDEFAULT;
  if (screen.width > 0 && screen.height > 0 && near_launcher)
  {
    m_window_x = moderngekko::detail::CentredOn((launcher_rect.left + launcher_rect.right) / 2,
                                                m_window_width, screen.x, screen.width);
    m_window_y = moderngekko::detail::CentredOn((launcher_rect.top + launcher_rect.bottom) / 2,
                                                m_window_height, screen.y, screen.height);
  }
  else if (screen.width > 0 && screen.height > 0)
  {
    m_window_x = screen.x + std::max(0, (screen.width - m_window_width) / 2);
    m_window_y = screen.y + std::max(0, (screen.height - m_window_height) / 2);
  }

  // The title is the game's as soon as it starts (SetTitle).
  m_hwnd = CreateWindowEx(WINDOW_EX_STYLE, WINDOW_CLASS_NAME, _T(""), WINDOW_STYLE, m_window_x,
                          m_window_y, m_window_width, m_window_height, nullptr, nullptr,
                          GetModuleHandle(nullptr), this);
  if (!m_hwnd)
  {
    MessageBox(nullptr, _T("CreateWindowEx failed."), _T("Error"), MB_ICONERROR | MB_OK);
    return false;
  }

  ShowWindow(m_hwnd, SW_SHOW);
  UpdateWindow(m_hwnd);
  return true;
}

bool PlatformWin32::CreateMenus()
{
  m_menu = CreateMenu();
  m_file_menu = CreatePopupMenu();
  m_view_menu = CreatePopupMenu();
  if (!m_menu || !m_file_menu || !m_view_menu)
    return false;

#if MODERNGEKKO_SAVESTATES
  m_load_menu = CreatePopupMenu();
  if (!m_load_menu)
    return false;
  AppendMenuW(m_file_menu, MF_STRING, ID_SAVE_STATE, L"&Save State\tF1");
  AppendMenuW(m_file_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_load_menu), L"&Load State");
  AppendMenuW(m_file_menu, MF_SEPARATOR, 0, nullptr);
#endif
  // Checked state is refreshed from the core when the menu opens, so it cannot
  // drift out of step with an emulation that was paused some other way.
  AppendMenuW(m_file_menu, MF_STRING, ID_PAUSE, L"&Pause\tF10");

  AppendMenuW(m_view_menu, MF_STRING, ID_FULLSCREEN, L"&Fullscreen\tAlt+Enter");
  AppendMenuW(m_view_menu, MF_STRING, ID_MUTE, L"&Mute Audio");

  AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_file_menu), L"&File");
  AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_view_menu), L"&View");
  return SetMenu(m_hwnd, m_menu) != FALSE;
}

// Rebuilt on open rather than cached: states are written by this process while
// the menu is closed, and by the launcher between sessions.
void PlatformWin32::RefreshMenu(const HMENU menu)
{
  if (menu == m_file_menu)
  {
    auto& system = Core::System::GetInstance();
    const bool paused = Core::GetState(system) == Core::State::Paused;
    CheckMenuItem(m_file_menu, ID_PAUSE, MF_BYCOMMAND | (paused ? MF_CHECKED : MF_UNCHECKED));
    return;
  }

  if (menu == m_view_menu)
  {
    CheckMenuItem(m_view_menu, ID_MUTE,
                  MF_BYCOMMAND |
                      (Config::Get(Config::MAIN_AUDIO_MUTED) ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m_view_menu, ID_FULLSCREEN,
                  MF_BYCOMMAND | (m_fullscreen ? MF_CHECKED : MF_UNCHECKED));
    return;
  }

#if MODERNGEKKO_SAVESTATES
  if (menu != m_load_menu)
    return;

  while (DeleteMenu(m_load_menu, 0, MF_BYPOSITION))
  {
  }
  m_load_state_paths.clear();

  // Location, extension and order all come from State::Layout, so this menu and
  // any frontend listing the same directory cannot disagree.
  const std::vector<std::filesystem::path> states =
      State::Layout::List(StringToPath(File::GetUserPath(D_STATESAVES_IDX)));

  if (states.empty())
  {
    AppendMenuW(m_load_menu, MF_STRING | MF_GRAYED, 0, L"(no savestates)");
    return;
  }

  const std::size_t limit = std::min<std::size_t>(
      states.size(), ID_LOAD_STATE_LAST - ID_LOAD_STATE_FIRST + 1);
  for (std::size_t i = 0; i < limit; ++i)
  {
    AppendMenuW(m_load_menu, MF_STRING, ID_LOAD_STATE_FIRST + i,
                states[i].filename().wstring().c_str());
    m_load_state_paths.push_back(PathToString(states[i]));
  }
#endif
}

#if MODERNGEKKO_SAVESTATES
void PlatformWin32::SaveStateToStatesDirectory()
{
  const std::string directory = File::GetUserPath(D_STATESAVES_IDX);
  File::CreateFullPath(directory);
  const std::time_t now = std::time(nullptr);
  if (now == m_last_save_time)
    ++m_save_sequence;
  else
  {
    m_last_save_time = now;
    m_save_sequence = 0;
  }

  const std::filesystem::path path =
      StringToPath(directory) /
      State::Layout::TimestampedName(now, m_save_sequence, State::Layout::MANUAL_PREFIX);
  State::SaveAs(Core::System::GetInstance(), PathToString(path));
}
#endif

void PlatformWin32::ToggleFullscreen()
{
  if (!m_fullscreen)
  {
    GetWindowRect(m_hwnd, &m_windowed_rect);
    m_windowed_style = GetWindowLong(m_hwnd, GWL_STYLE);

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
      return;

    SetMenu(m_hwnd, nullptr);
    SetWindowLong(m_hwnd, GWL_STYLE, m_windowed_style & ~WS_OVERLAPPEDWINDOW);
    SetWindowPos(m_hwnd, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                 monitor.rcMonitor.right - monitor.rcMonitor.left,
                 monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    m_fullscreen = true;
    m_window_fullscreen = true;
    return;
  }

  SetWindowLong(m_hwnd, GWL_STYLE, m_windowed_style);
  SetMenu(m_hwnd, m_menu);
  SetWindowPos(m_hwnd, nullptr, m_windowed_rect.left, m_windowed_rect.top,
               m_windowed_rect.right - m_windowed_rect.left,
               m_windowed_rect.bottom - m_windowed_rect.top,
               SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOZORDER);
  m_fullscreen = false;
  m_window_fullscreen = false;
}

bool PlatformWin32::Init()
{
  if (!RegisterRenderWindowClass() || !CreateRenderWindow())
    return false;
#if MODERNGEKKO_WINDOW_MENU
  if (!CreateMenus())
    return false;
#endif

  // Fullscreen from the start when the launcher says so.
  if (Config::Get(Config::MAIN_FULLSCREEN))
  {
    ToggleFullscreen();
    ProcessEvents();
  }

  if (Config::Get(Config::MAIN_DISABLE_SCREENSAVER))
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);

  UpdateWindowPosition();
  return true;
}

void PlatformWin32::SetTitle(const std::string& string)
{
  SetWindowTextW(m_hwnd, UTF8ToWString(string).c_str());
}

void PlatformWin32::MainLoop()
{
  while (IsRunning())
  {
    UpdateRunningFlag();
    Core::HostDispatchJobs(Core::System::GetInstance());
    ProcessEvents();
    UpdateWindowPosition();

    // TODO: Is this sleep appropriate?
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

WindowSystemInfo PlatformWin32::GetWindowSystemInfo() const
{
  WindowSystemInfo wsi;
  wsi.type = WindowSystemType::Windows;
  wsi.render_window = reinterpret_cast<void*>(m_hwnd);
  wsi.render_surface = reinterpret_cast<void*>(m_hwnd);
  return wsi;
}

void PlatformWin32::UpdateWindowPosition()
{
  if (m_window_fullscreen)
    return;

  RECT rc = {};
  if (!GetWindowRect(m_hwnd, &rc))
    return;

  m_window_x = rc.left;
  m_window_y = rc.top;
  m_window_width = rc.right - rc.left;
  m_window_height = rc.bottom - rc.top;
}

void PlatformWin32::ProcessEvents()
{
  MSG msg;
  while (PeekMessage(&msg, m_hwnd, 0, 0, PM_REMOVE))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

LRESULT PlatformWin32::WndProc(const HWND hwnd, const UINT msg, const WPARAM wParam,
                               const LPARAM lParam)
{
  PlatformWin32* platform = reinterpret_cast<PlatformWin32*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  switch (msg)
  {
  case WM_NCCREATE:
  {
    platform = static_cast<PlatformWin32*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(platform));
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  case WM_CREATE:
  {
    if (hwnd)
    {
      // Remove rounded corners from the render window on Windows 11
      constexpr DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_DONOTROUND;
      DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_preference,
                            sizeof(corner_preference));
    }
  }
  break;

  case WM_SIZE:
  {
    if (g_presenter)
      g_presenter->ResizeSurface();
  }
  break;

  case WM_KEYDOWN:
#if MODERNGEKKO_SAVESTATES
    // Ignore auto-repeat, or holding F1 down would write a state every few
    // milliseconds.
    if (wParam == VK_F1 && !IsRepeat(lParam))
    {
      platform->SaveStateToStatesDirectory();
      return 0;
    }
    if (wParam == VK_SPACE)
    {
      Config::SetCurrent(Config::MAIN_EMULATION_SPEED, FAST_FORWARD_SPEED);
      return 0;
    }
#endif
    if (wParam == VK_F9)
    {
      // One screenshot per press, not one per auto-repeat.
      if (!IsRepeat(lParam))
        moderngekko::detail::TakeScreenshot();
      return 0;
    }
    else if (wParam == VK_F11)
    {
      platform->ToggleFullscreen();
      return 0;
    }
    else if (wParam == VK_ESCAPE && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
      platform->RequestShutdown();
      return 0;
    }
    else if (wParam == VK_ESCAPE && platform->m_fullscreen)
    {
      platform->ToggleFullscreen();
      return 0;
    }
    break;

#if MODERNGEKKO_SAVESTATES
  case WM_KEYUP:
    if (wParam == VK_SPACE)
    {
      Config::SetCurrent(Config::MAIN_EMULATION_SPEED, 1.0f);
      return 0;
    }
    break;
#endif

  case WM_SETFOCUS:
    if (platform)
      platform->m_window_focus = true;
    break;

  case WM_KILLFOCUS:
    if (platform)
      platform->m_window_focus = false;
#if MODERNGEKKO_SAVESTATES
    // Never leave emulation running fast because Space was released while
    // another window had focus and the key-up went elsewhere.
    Config::SetCurrent(Config::MAIN_EMULATION_SPEED, 1.0f);
#endif
    break;

  case WM_SYSKEYDOWN:
    if (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000) != 0)
    {
      platform->ToggleFullscreen();
      return 0;
    }
    // F10 is a system key on Windows (it opens the menu bar): here it pauses,
    // as on Linux.
    if (wParam == VK_F10)
    {
      if (!IsRepeat(lParam))
        TogglePause();
      return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);

  case WM_INITMENUPOPUP:
    if (platform)
      platform->RefreshMenu(reinterpret_cast<HMENU>(wParam));
    break;

  case WM_COMMAND:
  {
    if (!platform)
      break;
    const UINT command = LOWORD(wParam);
#if MODERNGEKKO_SAVESTATES
    if (command == ID_SAVE_STATE)
    {
      platform->SaveStateToStatesDirectory();
    }
#endif
    if (command == ID_PAUSE)
    {
      TogglePause();
    }
    else if (command == ID_MUTE)
    {
      Config::SetCurrent(Config::MAIN_AUDIO_MUTED, !Config::Get(Config::MAIN_AUDIO_MUTED));
    }
    else if (command == ID_FULLSCREEN)
    {
      platform->ToggleFullscreen();
    }
#if MODERNGEKKO_SAVESTATES
    else if (command >= ID_LOAD_STATE_FIRST && command <= ID_LOAD_STATE_LAST)
    {
      const std::size_t index = command - ID_LOAD_STATE_FIRST;
      if (index < platform->m_load_state_paths.size())
        State::LoadAs(Core::System::GetInstance(), platform->m_load_state_paths[index]);
    }
#endif
    break;
  }

  case WM_CLOSE:
    platform->RequestShutdown();
    break;

  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  return 0;
}
}  // namespace

std::unique_ptr<Platform> Platform::CreateWin32Platform()
{
  return std::make_unique<PlatformWin32>();
}
