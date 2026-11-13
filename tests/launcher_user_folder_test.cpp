// A renamed project's user folder (tools/launcher_user_folder.hpp): the
// earlier folder is moved to the new name with all it holds, and
// default-game.txt follows it; nothing happens when the new folder is there
// already, or there is no earlier one.
#include "launcher_user_folder.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using moderngekko::frontend::SettleUserFolder;
using moderngekko::frontend::SplitFolderNames;

namespace
{
std::string FirstLine(const fs::path& file)
{
  std::ifstream in(file);
  std::string line;
  std::getline(in, line);
  return line;
}

void Write(const fs::path& file, const std::string& text)
{
  fs::create_directories(file.parent_path());
  std::ofstream(file) << text;
}
}  // namespace

int main()
{
  if (SplitFolderNames("sr-zgs") != std::vector<std::string>{"sr-zgs"} ||
      SplitFolderNames("a||b|") != std::vector<std::string>{"a", "b"} ||
      !SplitFolderNames("").empty())
    return 1;

  const fs::path base = fs::temp_directory_path() / "moderngekko_user_folder_test";
  fs::remove_all(base);
  const fs::path earlier = base / "sr-zgs";
  const fs::path folder = base / "sr-source";
  Write(earlier / "config.ini", "[launcher]\n");
  Write(earlier / "games" / "RS9E8P" / "sys" / "main.dol", "dol");
  Write(earlier / "default-game.txt", (earlier / "games" / "RS9E8P").string() + "\n");

  // Nothing earlier by these names: nothing moves.
  const auto none = SettleUserFolder(folder, {"other"});
  if (none.folder != folder || !none.moved_from.empty() || fs::exists(folder))
    return 2;

  // Moved, with everything in it, and the last game played follows.
  const auto moved = SettleUserFolder(folder, {"missing", "sr-zgs"});
  if (moved.folder != folder || moved.moved_from != earlier || !moved.error.empty() ||
      fs::exists(earlier) || !fs::exists(folder / "config.ini") ||
      FirstLine(folder / "games" / "RS9E8P" / "sys" / "main.dol") != "dol" ||
      FirstLine(folder / "default-game.txt") != (folder / "games" / "RS9E8P").string())
    return 3;

  // The new folder is there: an earlier one that shows up again stays where
  // it is (nothing is merged).
  Write(earlier / "config.ini", "[launcher]\n");
  const auto again = SettleUserFolder(folder, {"sr-zgs"});
  if (again.folder != folder || !again.moved_from.empty() || !fs::exists(earlier))
    return 4;

  // A last game outside the folder (a portable release's) is left as it was.
  fs::remove_all(base);
  Write(earlier / "default-game.txt", "/somewhere/else/RS9E8P\n");
  if (SettleUserFolder(folder, {"sr-zgs"}).moved_from != earlier ||
      FirstLine(folder / "default-game.txt") != "/somewhere/else/RS9E8P")
    return 5;

  // An earlier name that is the folder's own is no earlier folder.
  fs::remove_all(base);
  fs::create_directories(base);
  if (!SettleUserFolder(folder, {"sr-source"}).moved_from.empty() || fs::exists(folder))
    return 6;

  fs::remove_all(base);
  std::puts("launcher user folder: ok");
  return 0;
}
