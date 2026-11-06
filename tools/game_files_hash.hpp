#pragma once

// The Netplay lobby makes sure every player has the same game files by hashing
// all of files/ (moderngekko::HashDirectorySha256): several GB, which takes
// many seconds every time the lobby opens. CachedDirectorySha256 gives exactly
// the same result, but remembers each file's hash in a cache file together
// with the file's size and modification time, so only new or changed files are
// read again.

#include "moderngekko/game.hpp"
#include "moderngekko/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace moderngekko::frontend
{
namespace game_files_hash_detail
{
inline constexpr std::string_view FILES_HASH_CACHE_HEADER = "moderngekko-files-sha256 1";

struct CachedFileHash
{
  std::uintmax_t size = 0;
  long long modified = 0;
  std::string sha256;
};
}  // namespace game_files_hash_detail

// The hash of every file under root, as HashDirectorySha256(root) computes it.
// cache_path is where the per-file hashes are kept between runs; it is created
// or rewritten as needed, and a missing, foreign or damaged cache only means
// the files are read again. rehashed, when given, receives how many files had
// to be read.
inline std::optional<std::string> CachedDirectorySha256(const std::filesystem::path& root,
                                                         const std::filesystem::path& cache_path,
                                                         std::size_t* rehashed = nullptr)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(root, ec) || ec)
    return std::nullopt;

  struct Entry
  {
    fs::path path;
    std::string relative;
    std::uintmax_t size = 0;
    long long modified = 0;
  };
  std::vector<Entry> files;
  fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied,
                                            ec);
  const fs::recursive_directory_iterator end;
  while (!ec && iterator != end)
  {
    if (iterator->is_regular_file(ec) && !ec)
    {
      Entry entry;
      entry.path = iterator->path();
      entry.relative = fs::relative(entry.path, root, ec).generic_string();
      if (!ec)
        entry.size = fs::file_size(entry.path, ec);
      if (!ec)
        entry.modified = static_cast<long long>(
            fs::last_write_time(entry.path, ec).time_since_epoch().count());
      if (ec)
        return std::nullopt;
      files.push_back(std::move(entry));
    }
    iterator.increment(ec);
  }
  if (ec)
    return std::nullopt;
  std::ranges::sort(files, [](const Entry& a, const Entry& b) { return a.relative < b.relative; });

  // "<size> <modified> <sha256> <relative path>" per line, after a header and
  // the folder the hashes belong to.
  std::unordered_map<std::string, game_files_hash_detail::CachedFileHash> cache;
  {
    std::ifstream input(cache_path);
    std::string line;
    if (input && std::getline(input, line) &&
        line == game_files_hash_detail::FILES_HASH_CACHE_HEADER && std::getline(input, line) &&
        line == root.generic_string())
    {
      while (std::getline(input, line))
      {
        std::istringstream fields(line);
        game_files_hash_detail::CachedFileHash cached;
        if (!(fields >> cached.size >> cached.modified >> cached.sha256) ||
            fields.get() != ' ' || cached.sha256.size() != 64)
          continue;
        std::string relative;
        std::getline(fields, relative);
        if (!relative.empty())
          cache[relative] = std::move(cached);
      }
    }
  }

  std::size_t read_again = 0;
  std::vector<std::uint8_t> manifest;
  std::ostringstream updated;
  updated << game_files_hash_detail::FILES_HASH_CACHE_HEADER << '\n'
          << root.generic_string() << '\n';
  for (const Entry& file : files)
  {
    std::string hash;
    const auto cached = cache.find(file.relative);
    if (cached != cache.end() && cached->second.size == file.size &&
        cached->second.modified == file.modified)
    {
      hash = cached->second.sha256;
    }
    else
    {
      const auto computed = HashFileSha256(file.path);
      if (!computed)
        return std::nullopt;
      hash = *computed;
      ++read_again;
    }
    // The same manifest HashDirectorySha256 builds.
    const auto append_u64 = [&](std::uint64_t value) {
      for (int shift = 56; shift >= 0; shift -= 8)
        manifest.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    append_u64(file.relative.size());
    manifest.insert(manifest.end(), file.relative.begin(), file.relative.end());
    append_u64(file.size);
    manifest.insert(manifest.end(), hash.begin(), hash.end());
    // A name with a line break can't be kept on one line; it is read again.
    if (file.relative.find_first_of("\r\n") == std::string::npos)
      updated << file.size << ' ' << file.modified << ' ' << hash << ' ' << file.relative << '\n';
  }
  if (rehashed)
    *rehashed = read_again;

  // Rewritten whenever something was read again or a file went away. The
  // cache is only a shortcut: failing to write it changes nothing else.
  if (read_again != 0 || cache.size() != files.size())
  {
    fs::create_directories(cache_path.parent_path(), ec);
    fs::path temporary = cache_path;
    temporary += ".tmp";
    {
      std::ofstream output(temporary, std::ios::trunc);
      output << updated.str();
    }
    fs::rename(temporary, cache_path, ec);
    if (ec)
      fs::remove(temporary, ec);
  }
  return Sha256(std::move(manifest));
}
}  // namespace moderngekko::frontend
