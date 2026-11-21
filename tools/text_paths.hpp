#pragma once

// Paths as text, and text as paths.
//
// Every text this program handles is UTF-8: what SDL takes and gives, the
// interface, the game's core (Dolphin's File and DiscIO), config.ini, the
// logs, the runner's arguments. On Linux a path is its bytes, so these
// functions change nothing there. On Windows a path is UTF-16, and the
// std::filesystem calls that take or give a std::string (path(std::string),
// path / std::string, .string(), .generic_string()) use the system's code
// page instead (windows-1252, 932...). That broke every folder with a letter
// written differently in the two: in a user folder named P, a with tilde
// (U+00E3), o, the letter reached SDL as the byte 0xE3, which isn't UTF-8, and
// became U+FFFD; the runner then got "P?o" in its arguments, a name no folder
// can have (GitHub issue #5). And .string() throws for a letter the code page
// doesn't have at all (Japanese on an English Windows).
//
// So paths become text only through PathText()/GenericPathText(), text
// becomes a path only through TextPath(), the folders the system names come
// from EnvironmentPath(), and main() reads its arguments with Utf8Arguments.

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::text
{
// Whether `text` is well-formed UTF-8 (no overlong forms, no surrogates,
// nothing past U+10FFFF).
inline bool IsUtf8(std::string_view text)
{
  std::size_t i = 0;
  while (i < text.size())
  {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80)
    {
      ++i;
      continue;
    }
    std::size_t length = 0;
    unsigned char low = 0x80, high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF)
      length = 2;
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
      length = 3;
      if (lead == 0xE0)
        low = 0xA0;
      else if (lead == 0xED)
        high = 0x9F;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
      length = 4;
      if (lead == 0xF0)
        low = 0x90;
      else if (lead == 0xF4)
        high = 0x8F;
    }
    else
      return false;
    if (i + length > text.size())
      return false;
    for (std::size_t k = 1; k < length; ++k)
    {
      const unsigned char next = static_cast<unsigned char>(text[i + k]);
      if (next < (k == 1 ? low : 0x80) || next > (k == 1 ? high : 0xBF))
        return false;
    }
    i += length;
  }
  return true;
}

// The path as UTF-8 text: for SDL, the interface, Dolphin, files and logs.
inline std::string PathText(const std::filesystem::path& path)
{
#if defined(_WIN32)
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
#else
  return path.native();
#endif
}

// The same with '/' between folders on every system.
inline std::string GenericPathText(const std::filesystem::path& path)
{
#if defined(_WIN32)
  const std::u8string text = path.generic_u8string();
  return std::string(text.begin(), text.end());
#else
  return path.generic_string();
#endif
}

// The path a UTF-8 text names. On Windows, a text that isn't UTF-8 was
// written by an earlier version in the system's code page (a
// default-game.txt, say) and is read that way.
inline std::filesystem::path TextPath(std::string_view text)
{
#if defined(_WIN32)
  if (IsUtf8(text))
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
  try
  {
    return std::filesystem::path(std::string(text));
  }
  catch (const std::exception&)
  {
    // Neither UTF-8 nor the code page: '?' can't be in a file name, so the
    // path fails where it's used instead of naming another folder.
    std::u8string kept;
    for (const char c : text)
      kept.push_back(static_cast<unsigned char>(c) < 0x80 ? static_cast<char8_t>(c) : u8'?');
    return std::filesystem::path(kept);
  }
#else
  return std::filesystem::path(std::string(text));
#endif
}

// A file name or relative path written in UTF-8 below `folder`.
inline std::filesystem::path TextPathIn(const std::filesystem::path& folder, std::string_view name)
{
  return folder / TextPath(name);
}

// The path for Dolphin's DynamicLibrary, which opens a library with
// LoadLibraryA on Windows: that one reads the system's code page. A path the
// code page can't write isn't opened (LoadLibraryA can't name it), with the
// usual error, instead of stopping the program.
inline std::string SystemPathText(const std::filesystem::path& path)
{
#if defined(_WIN32)
  try
  {
    return path.string();
  }
  catch (const std::exception&)
  {
    return PathText(path);
  }
#else
  return path.native();
#endif
}

// An environment variable that names a folder (LOCALAPPDATA, USERPROFILE,
// HOME...), read in Unicode on Windows. Set but empty is still a value, as
// std::getenv gives it.
inline std::optional<std::filesystem::path> EnvironmentPath(const char* name)
{
#if defined(_WIN32)
  std::wstring wide_name;
  for (const char* c = name; *c; ++c)
    wide_name.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*c)));
  const wchar_t* value = _wgetenv(wide_name.c_str());
  if (!value)
    return std::nullopt;
  return std::filesystem::path(value);
#else
  const char* value = std::getenv(name);
  if (!value)
    return std::nullopt;
  return std::filesystem::path(value);
#endif
}

// This program's file, as the system knows it (not argv[0], which may be a
// bare name or relative to another folder). Empty if the system won't say.
std::filesystem::path ExecutablePath();

// main()'s arguments as UTF-8. On Windows, main() gets them in the system's
// code page, which can't hold every letter: they are read again from the
// Unicode command line. Elsewhere they are kept as they are.
//
//   moderngekko::text::Utf8Arguments arguments(argc, argv);
//   argc = arguments.Count();
//   argv = arguments.Values();
class Utf8Arguments
{
public:
  Utf8Arguments(int argc, char** argv);
  Utf8Arguments(const Utf8Arguments&) = delete;
  Utf8Arguments& operator=(const Utf8Arguments&) = delete;

  int Count() const { return static_cast<int>(m_text.size()); }
  char** Values() { return m_values.data(); }

private:
  std::vector<std::string> m_text;
  std::vector<char*> m_values;
};
}  // namespace moderngekko::text
