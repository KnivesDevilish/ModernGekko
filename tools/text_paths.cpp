#include "text_paths.hpp"

#include <cwchar>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace moderngekko::text
{
#if defined(_WIN32)
namespace
{
std::string WideToUtf8(const wchar_t* wide)
{
  const int wide_length = static_cast<int>(std::wcslen(wide));
  if (wide_length == 0)
    return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide, wide_length, nullptr, 0, nullptr, nullptr);
  std::string text(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
  if (size > 0)
    WideCharToMultiByte(CP_UTF8, 0, wide, wide_length, text.data(), size, nullptr, nullptr);
  return text;
}
}  // namespace
#endif

std::filesystem::path ExecutablePath()
{
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;)
  {
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
      return {};
    if (length < buffer.size())
    {
      buffer.resize(length);
      return std::filesystem::path(buffer);
    }
    // Cut short: a longer path than MAX_PATH. Try again with more room.
    if (buffer.size() >= 32768)
      return {};
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__linux__)
  std::error_code ec;
  std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::path() : executable;
#else
  return {};
#endif
}

Utf8Arguments::Utf8Arguments(int argc, char** argv)
{
#if defined(_WIN32)
  int count = 0;
  LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
  if (wide && count == argc)
  {
    for (int i = 0; i < count; ++i)
      m_text.push_back(WideToUtf8(wide[i]));
  }
  else
  {
    // The Unicode command line didn't split like argv (it always does for a
    // program started normally): keep argv rather than guess.
    m_text.assign(argv, argv + argc);
  }
  if (wide)
    LocalFree(wide);
#else
  m_text.assign(argv, argv + argc);
#endif
  m_values.reserve(m_text.size() + 1);
  for (std::string& text : m_text)
    m_values.push_back(text.data());
  m_values.push_back(nullptr);
}
}  // namespace moderngekko::text
