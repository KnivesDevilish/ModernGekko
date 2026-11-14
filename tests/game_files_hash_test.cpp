// The Netplay lobby's cached hash of the game files must always equal the full
// hash (moderngekko::HashDirectorySha256), and read only files that changed.
#include "game_files_hash.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
void Write(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}
}  // namespace

int main()
{
  using moderngekko::HashDirectorySha256;
  using moderngekko::frontend::CachedDirectorySha256;

  const fs::path base = fs::temp_directory_path() / "moderngekko_game_files_hash_test";
  fs::remove_all(base);
  const fs::path files = base / "files";
  const fs::path cache = base / "user" / "Cache" / "game-files.sha256";
  Write(files / "_Main.rel", "rel");
  Write(files / "Sound" / "bgm 01.brstm", std::string(100000, 'a'));
  Write(files / "Movie" / "intro.thp", "movie");
  Write(files / "zz.bin", "last");

  // First run: everything is read, and the result is the full hash.
  std::size_t rehashed = 99;
  const auto first = CachedDirectorySha256(files, cache, &rehashed);
  if (!first || *first != HashDirectorySha256(files) || rehashed != 4 || !fs::exists(cache))
    return 1;

  // Nothing changed: nothing is read, same result.
  const auto second = CachedDirectorySha256(files, cache, &rehashed);
  if (!second || *second != *first || rehashed != 0)
    return 2;

  // A file changed size: only that one is read.
  Write(files / "Movie" / "intro.thp", "a longer movie");
  const auto third = CachedDirectorySha256(files, cache, &rehashed);
  if (!third || *third != HashDirectorySha256(files) || *third == *first || rehashed != 1)
    return 3;

  // Same size, other content, newer time: read again too.
  Write(files / "zz.bin", "LAST");
  fs::last_write_time(files / "zz.bin",
                      fs::last_write_time(files / "zz.bin") + std::chrono::hours(1));
  const auto fourth = CachedDirectorySha256(files, cache, &rehashed);
  if (!fourth || *fourth != HashDirectorySha256(files) || rehashed != 1)
    return 4;

  // A file removed and one added.
  fs::remove(files / "_Main.rel");
  Write(files / "New" / "extra.dat", "extra");
  const auto fifth = CachedDirectorySha256(files, cache, &rehashed);
  if (!fifth || *fifth != HashDirectorySha256(files) || rehashed != 1)
    return 5;

  // A damaged cache, or one written for another folder, is ignored.
  Write(cache, "garbage\nnot a cache\n");
  const auto sixth = CachedDirectorySha256(files, cache, &rehashed);
  if (!sixth || *sixth != *fifth || rehashed != 4)
    return 6;
  const fs::path copy = base / "copy";
  fs::copy(files, copy, fs::copy_options::recursive);
  const auto other = CachedDirectorySha256(copy, cache, &rehashed);
  if (!other || *other != *fifth || rehashed != 4)
    return 7;

  // No folder, no hash.
  if (CachedDirectorySha256(base / "missing", cache))
    return 8;

  // Progress: from 0 to every byte that has to be read, never backwards.
  std::uintmax_t all_bytes = 0;
  for (const auto& entry : fs::recursive_directory_iterator(files))
  {
    if (entry.is_regular_file())
      all_bytes += entry.file_size();
  }
  fs::remove(cache);
  std::vector<std::pair<std::uintmax_t, std::uintmax_t>> reports;
  const auto record = [&](std::uintmax_t read, std::uintmax_t to_read) {
    reports.emplace_back(read, to_read);
    return true;
  };
  const auto seventh = CachedDirectorySha256(files, cache, &rehashed, record);
  if (!seventh || *seventh != *fifth || rehashed != 4 || reports.size() != 5 ||
      reports.front() != std::pair<std::uintmax_t, std::uintmax_t>(0, all_bytes) ||
      reports.back() != std::pair<std::uintmax_t, std::uintmax_t>(all_bytes, all_bytes))
    return 9;
  for (std::size_t i = 1; i < reports.size(); ++i)
  {
    if (reports[i].first < reports[i - 1].first || reports[i].second != all_bytes)
      return 10;
  }

  // Nothing to read: no progress at all.
  reports.clear();
  const auto eighth = CachedDirectorySha256(files, cache, &rehashed, record);
  if (!eighth || *eighth != *fifth || rehashed != 0 || !reports.empty())
    return 11;

  // Stopped after the first file read: no hash, but that file is kept in the
  // cache, and the next check reads only the others.
  fs::remove(cache);
  const auto stopped = CachedDirectorySha256(
      files, cache, &rehashed, [](std::uintmax_t read, std::uintmax_t) { return read == 0; });
  if (stopped || rehashed != 1 || !fs::exists(cache))
    return 12;
  const auto resumed = CachedDirectorySha256(files, cache, &rehashed);
  if (!resumed || *resumed != *fifth || rehashed != 3)
    return 13;

  // Stopped before reading anything: no hash, nothing read.
  fs::remove(cache);
  if (CachedDirectorySha256(files, cache, &rehashed,
                            [](std::uintmax_t, std::uintmax_t) { return false; }) ||
      rehashed != 0)
    return 14;

  fs::remove_all(base);
  return 0;
}
