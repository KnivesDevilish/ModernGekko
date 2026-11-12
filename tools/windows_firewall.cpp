#include "windows_firewall.hpp"

#include <algorithm>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#endif

namespace moderngekko::frontend
{
namespace
{
#if defined(_WIN32)
std::wstring WideText(const std::string& text)
{
  if (text.empty())
    return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                       nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(std::max(size, 0)), L'\0');
  if (size > 0)
  {
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                        size);
  }
  return wide;
}

// One elevated cmd.exe runs both netsh commands, so Windows asks once. The
// first deletes the program's inbound rules (none is fine: the second still
// runs), the second adds one that allows it on every profile; cmd.exe exits
// with the second's code, 0 when it worked.
FirewallResult AddRule(const std::filesystem::path& program, const std::string& rule_name)
{
  // ShellExecuteEx may hand the work to the shell, which needs COM.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  struct ComScope
  {
    bool initialized;
    ~ComScope()
    {
      if (initialized)
        CoUninitialize();
    }
  } com_scope{SUCCEEDED(com)};
  const std::wstring path = program.wstring();
  const std::wstring parameters =
      L"/d /c netsh advfirewall firewall delete rule name=all dir=in program=\"" + path +
      L"\" >nul & netsh advfirewall firewall add rule name=\"" + WideText(rule_name) +
      L"\" dir=in action=allow program=\"" + path + L"\" enable=yes profile=any";
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  info.lpVerb = L"runas";
  info.lpFile = L"cmd.exe";
  info.lpParameters = parameters.c_str();
  info.nShow = SW_HIDE;
  if (!ShellExecuteExW(&info))
    return GetLastError() == ERROR_CANCELLED ? FirewallResult::Declined : FirewallResult::Failed;
  if (!info.hProcess)
    return FirewallResult::Failed;
  const DWORD waited = WaitForSingleObject(info.hProcess, 60000);
  DWORD exit_code = 1;
  if (waited != WAIT_OBJECT_0 || !GetExitCodeProcess(info.hProcess, &exit_code))
    exit_code = 1;
  CloseHandle(info.hProcess);
  return exit_code == 0 ? FirewallResult::Allowed : FirewallResult::Failed;
}
#else
FirewallResult AddRule(const std::filesystem::path&, const std::string&)
{
  return FirewallResult::Failed;
}
#endif
}  // namespace

void FirewallRule::Allow(std::filesystem::path program, std::string rule_name)
{
  if (m_result.load() == FirewallResult::Working)
    return;
  if (m_thread.joinable())
    m_thread.join();
  m_result = FirewallResult::Working;
  m_thread = std::jthread([this, program = std::move(program), rule_name = std::move(rule_name)] {
    m_result = AddRule(program, rule_name);
  });
}
}  // namespace moderngekko::frontend
