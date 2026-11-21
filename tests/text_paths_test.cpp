// Folders whose names aren't plain ASCII (GitHub issue #5: on Windows, a
// user folder with an "a" with a tilde broke the extraction and the game's
// start). Checks what tools/text_paths.hpp promises, where it matters:
//
//   moderngekko_text_paths_test [--work <folder>] [--runner <moderngekko-run>] [--keep]
//
// In <folder> (a new one in the system's temporary folder when not given) it
// makes a folder named with letters from several languages and checks that:
//  1. its name becomes UTF-8 text and back (PathText, TextPath);
//  2. what is made through the path is found through SDL with that text, and
//     the other way;
//  3. EnvironmentPath reads a variable with that folder in it;
//  4. a program started with SDL_CreateProcess and that folder in its
//     arguments, as the launcher starts the game, gets it whole
//     (Utf8Arguments);
//  5. on Windows, a text in the system's code page, as earlier versions wrote
//     default-game.txt, still names its folder;
//  6. with --runner, the real runner, started as the launcher starts it with
//     its user folder in there, makes that folder and its config.ini and gets
//     as far as looking at the game (there is none: it stops there);
//  7. nothing was made under a wrong name (with '?' or U+FFFD in it), the
//     folder the bug made.
// It ends with 0 when everything passed. The release workflow runs it on
// Windows and Linux (SR-Source's tools/check_unicode_paths.sh).

#include "text_paths.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using moderngekko::text::EnvironmentPath;
using moderngekko::text::GenericPathText;
using moderngekko::text::IsUtf8;
using moderngekko::text::PathText;
using moderngekko::text::TextPath;

int g_failures = 0;

void Check(bool passed, const std::string& what)
{
  std::cout << (passed ? "ok      " : "FAILED  ") << what << std::endl;
  if (!passed)
    ++g_failures;
}

std::string Text(std::u8string_view text)
{
  return std::string(text.begin(), text.end());
}

// "Pão ñ 日本 ☃": letters of Western code pages, two Japanese ones and a
// snowman that no code page of Windows has (.string() can't write it).
std::string Name()
{
  return Text(u8"Pão ñ 日本 ☃");
}

std::string ReadAll(const fs::path& file)
{
  std::ifstream input(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// Waits up to two minutes: a program that hangs (a message box nobody closes)
// fails the check instead of keeping the release workflow waiting.
bool WaitFor(SDL_Process* process, int* exit_code)
{
  if (!process)
    return false;
  bool waited = false;
  for (int tenth = 0; tenth < 1200 && !waited; ++tenth)
  {
    waited = SDL_WaitProcess(process, false, exit_code);
    if (!waited)
      SDL_Delay(100);
  }
  if (!waited)
  {
    std::cout << "        (it didn't end in two minutes: stopped)\n";
    SDL_KillProcess(process, true);
    SDL_WaitProcess(process, true, exit_code);
  }
  SDL_DestroyProcess(process);
  return waited;
}

// Started by the test with the folder and a file in it, as the launcher starts
// the game with its folders.
int Child(const std::string& folder_text, const std::string& file_text)
{
  const fs::path folder = TextPath(folder_text);
  std::error_code ec;
  if (!fs::is_directory(folder, ec) || PathText(folder.filename()) != Name())
  {
    std::cerr << "child: got the folder as " << folder_text << '\n';
    return 3;
  }
  std::ofstream(TextPath(file_text)) << "child\n";
  return fs::is_regular_file(TextPath(file_text), ec) ? 0 : 4;
}

void CheckText()
{
  const std::string name = Name();
  Check(IsUtf8(name) && name.find("\xC3\xA3") != std::string::npos,
        "the name is UTF-8 text (its a-tilde is C3 A3)");
  Check(PathText(TextPath(name)) == name, "the name becomes a path and the same text again");
  Check(GenericPathText(TextPath("a/" + name)) == "a/" + name, "and with '/' between folders");
  Check(!IsUtf8("P\xE3o"), "a code page's byte alone is not UTF-8");
  Check(!IsUtf8("\xC0\xAF") && !IsUtf8("\xED\xA0\x80") && !IsUtf8("\xF4\x90\x80\x80") &&
            !IsUtf8("\xE6\x97"),
        "nor are overlong forms, surrogates, past U+10FFFF and cut letters");
  Check(IsUtf8("") && IsUtf8("abc") && IsUtf8("\xF0\x9F\x98\x80"), "but ASCII and 4-byte letters are");
}

void CheckFolders(const fs::path& folder)
{
  std::error_code ec;
  fs::create_directories(folder, ec);
  Check(!ec && fs::is_directory(folder, ec), "a folder named " + Name() + " is made");

  // SDL takes UTF-8 (so does Dolphin): a folder SDL makes with the text is the path's.
  Check(SDL_CreateDirectory(PathText(folder / "sdl").c_str()) && fs::is_directory(folder / "sdl", ec),
        "SDL makes a folder with the path's text, and it is that path");

  const fs::path through_sdl = folder / TextPath(Name() + ".txt");
  if (SDL_IOStream* out = SDL_IOFromFile(PathText(through_sdl).c_str(), "w"))
  {
    SDL_WriteIO(out, "hola", 4);
    SDL_CloseIO(out);
  }
  Check(ReadAll(through_sdl) == "hola", "a file SDL writes with the text is read through the path");

  std::ofstream(folder / "written.txt", std::ios::binary) << "adios";
  std::size_t size = 0;
  void* loaded = SDL_LoadFile(PathText(folder / "written.txt").c_str(), &size);
  Check(loaded && std::string(static_cast<const char*>(loaded), size) == "adios",
        "a file written through the path is read by SDL with the text");
  SDL_free(loaded);
}

void CheckEnvironment(const fs::path& folder)
{
#if defined(_WIN32)
  _wputenv_s(L"MODERNGEKKO_TEXT_PATHS_TEST", folder.c_str());
#else
  setenv("MODERNGEKKO_TEXT_PATHS_TEST", folder.c_str(), 1);
#endif
  const auto read = EnvironmentPath("MODERNGEKKO_TEXT_PATHS_TEST");
  Check(read && *read == folder, "an environment variable with the folder in it is read whole");
  Check(!EnvironmentPath("MODERNGEKKO_TEXT_PATHS_TEST_NOT_SET"), "and one not set is not there");
}

void CheckChild(const fs::path& self, const fs::path& folder)
{
  Check(!self.empty() && fs::is_regular_file(self), "the program knows its own file: " + PathText(self));
  const fs::path file = folder / "from-child.txt";
  const std::vector<std::string> storage = {PathText(self), "--child", PathText(folder),
                                            PathText(file)};
  std::vector<const char*> arguments;
  for (const std::string& argument : storage)
    arguments.push_back(argument.c_str());
  arguments.push_back(nullptr);
  int exit_code = -1;
  const bool waited = WaitFor(SDL_CreateProcess(arguments.data(), false), &exit_code);
  std::error_code ec;
  Check(waited && exit_code == 0 && fs::is_regular_file(file, ec),
        "a program started with the folder in its arguments gets it whole (exit " +
            std::to_string(exit_code) + ")");
}

void CheckCodePage(const fs::path& folder)
{
#if defined(_WIN32)
  // Earlier versions wrote default-game.txt with .string(), in the system's code page.
  const fs::path accented(L"Pão");
  std::string legacy;
  try
  {
    legacy = accented.string();
  }
  catch (const std::exception&)
  {
  }
  if (!legacy.empty() && !IsUtf8(legacy))
    Check(TextPath(legacy) == accented, "a text in the system's code page still names its folder");
  else
    std::cout << "skipped: this system's code page has no a-tilde, or it is UTF-8\n";
  bool threw = false;
  try
  {
    (void)moderngekko::text::SystemPathText(folder);
  }
  catch (...)
  {
    threw = true;
  }
  Check(!threw, "SystemPathText doesn't stop the program for a letter the code page lacks");
#else
  (void)folder;
#endif
}

// As the launcher starts the game (moderngekko_launcher.cpp): its output to a
// log SDL opens, the arguments in UTF-8.
void CheckRunner(const fs::path& runner, const fs::path& folder)
{
  const fs::path user = folder / "sr-source";
  const fs::path game = folder / "no game here";
  const fs::path log = folder / "runner.log";
  const std::vector<std::string> storage = {PathText(runner), "--user-dir", PathText(user),
                                            "--game", PathText(game)};
  std::vector<const char*> arguments;
  for (const std::string& argument : storage)
    arguments.push_back(argument.c_str());
  arguments.push_back(nullptr);

  SDL_Process* process = nullptr;
  if (SDL_IOStream* log_stream = SDL_IOFromFile(PathText(log).c_str(), "w"))
  {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments.data());
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                          SDL_PROCESS_STDIO_REDIRECT);
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log_stream);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    SDL_CloseIO(log_stream);
  }
  int exit_code = -1;
  const bool waited = WaitFor(process, &exit_code);
  const std::string output = ReadAll(log);
  std::error_code ec;
  Check(waited, "the runner starts: " + PathText(runner) + (waited ? "" : std::string(" (") + SDL_GetError() + ")"));
  Check(fs::is_regular_file(user / "config.ini", ec),
        "the runner makes its user folder, " + PathText(user) + ", with config.ini in it");
  Check(output.find("can't create user directory") == std::string::npos &&
            output.find("invalid config.ini") == std::string::npos,
        "and says nothing against it");
  Check(exit_code == 2 && output.find("invalid game") != std::string::npos,
        "it gets as far as the game, which isn't there (exit " + std::to_string(exit_code) + ")");
  if (g_failures)
    std::cout << "--- the runner's output:\n" << output << "---\n";
}

void CheckNoWrongNames(const fs::path& work)
{
  std::error_code ec;
  std::vector<std::string> wrong;
  for (fs::recursive_directory_iterator it(work, ec), end; !ec && it != end; it.increment(ec))
  {
    const std::string name = PathText(it->path().filename());
    if (name.find('?') != std::string::npos || name.find("\xEF\xBF\xBD") != std::string::npos)
      wrong.push_back(PathText(it->path()));
  }
  Check(!ec && wrong.empty(), "nothing was made under a wrong name");
  for (const std::string& path : wrong)
    std::cout << "        " << path << '\n';
}
}  // namespace

int main(int argc, char** argv)
{
  moderngekko::text::Utf8Arguments arguments(argc, argv);
  argc = arguments.Count();
  argv = arguments.Values();
  if (argc == 4 && std::string_view(argv[1]) == "--child")
    return Child(argv[2], argv[3]);

  fs::path work;
  fs::path runner;
  bool keep = false;
  for (int i = 1; i < argc; ++i)
  {
    const std::string_view argument = argv[i];
    if (argument == "--work" && i + 1 < argc)
      work = TextPath(argv[++i]);
    else if (argument == "--runner" && i + 1 < argc)
      runner = TextPath(argv[++i]);
    else if (argument == "--keep")
      keep = true;
    else
    {
      std::cerr << "usage: moderngekko_text_paths_test [--work <folder>] "
                   "[--runner <moderngekko-run>] [--keep]\n";
      return 2;
    }
  }
  std::error_code ec;
  if (work.empty())
    work = fs::temp_directory_path(ec) / ("moderngekko-text-paths-" + std::to_string(SDL_GetTicksNS()));
  fs::remove_all(work, ec);
  fs::create_directories(work, ec);
  work = fs::weakly_canonical(work, ec);
  const fs::path folder = work / TextPath(Name());
  std::cout << "working in " << PathText(folder) << '\n';

  CheckText();
  CheckFolders(folder);
  CheckEnvironment(folder);
  CheckChild(moderngekko::text::ExecutablePath(), folder);
  CheckCodePage(folder);
  if (!runner.empty())
    CheckRunner(runner, folder);
  CheckNoWrongNames(work);

  if (!keep)
    fs::remove_all(work, ec);
  std::cout << (g_failures ? std::to_string(g_failures) + " FAILED" : std::string("text paths: ok"))
            << std::endl;
  return g_failures ? 1 : 0;
}
