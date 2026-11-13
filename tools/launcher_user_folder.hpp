// The user folder of a renamed project (settings, saves, extracted games,
// screenshots...): the first time the launcher opens under the new name, it
// moves the folder of an earlier name (MODERNGEKKO_PREVIOUS_USER_DIRECTORY_NAMES)
// to its own, so nobody loses anything, and points default-game.txt, which
// names the last game played by its full path, at the new place. When the
// folder can't be moved (another program holding it, say), the launcher uses
// it where it is this time and tries again the next. Apart from the launcher,
// so tests/launcher_user_folder_test.cpp can check it.
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace moderngekko::frontend
{
// "sr-zgs|old" as {"sr-zgs", "old"}; empty names left out.
inline std::vector<std::string> SplitFolderNames(std::string_view names)
{
  std::vector<std::string> split;
  while (!names.empty())
  {
    const std::size_t bar = names.find('|');
    const std::string_view name = names.substr(0, bar);
    if (!name.empty())
      split.emplace_back(name);
    if (bar == std::string_view::npos)
      break;
    names.remove_prefix(bar + 1);
  }
  return split;
}

// default-game.txt in `folder`, moved there from `earlier`: a game inside the
// earlier folder is named inside the new one.
inline void PointDefaultGameAt(const std::filesystem::path& folder,
                               const std::filesystem::path& earlier)
{
  const std::filesystem::path file = folder / "default-game.txt";
  std::string line;
  {
    std::ifstream in(file);
    if (!std::getline(in, line))
      return;
  }
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  const std::filesystem::path game(line);
  const auto [in_earlier, rest] =
      std::mismatch(earlier.begin(), earlier.end(), game.begin(), game.end());
  if (in_earlier != earlier.end())
    return;
  std::filesystem::path moved = folder;
  for (auto part = rest; part != game.end(); ++part)
    moved /= *part;
  std::ofstream(file, std::ios::trunc) << moved.string() << '\n';
}

struct SettledUserFolder
{
  std::filesystem::path folder;      // the folder to use
  std::filesystem::path moved_from;  // the earlier folder moved to it, if any
  std::string error;                 // why the earlier one couldn't be moved
};

// The folder to use: `folder`, after moving there the first of the earlier
// names' folders (beside it) that is there, when `folder` isn't there yet;
// or that earlier folder as it is, when it can't be moved.
inline SettledUserFolder SettleUserFolder(const std::filesystem::path& folder,
                                          const std::vector<std::string>& earlier_names)
{
  SettledUserFolder settled{folder, {}, {}};
  std::error_code error;
  if (std::filesystem::exists(folder, error) || error)
    return settled;
  for (const std::string& name : earlier_names)
  {
    const std::filesystem::path earlier = folder.parent_path() / name;
    if (earlier == folder || !std::filesystem::is_directory(earlier, error))
      continue;
    std::filesystem::rename(earlier, folder, error);
    if (error)
    {
      settled.folder = earlier;
      settled.error = error.message();
      return settled;
    }
    settled.moved_from = earlier;
    PointDefaultGameAt(folder, earlier);
    return settled;
  }
  return settled;
}
}  // namespace moderngekko::frontend
