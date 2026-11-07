#pragma once

// The Game Files page can put a complete save (the game finished at 100%, as
// the game's author left it) in place of the player's, and bring the
// player's back later. Only the game's own save files come with a build
// (MODERNGEKKO_COMPLETE_SAVE, copied to Resources/complete-save): a Wii
// save's banner.bin, the picture and icon the Wii shows for it, is the game's
// art, so the save the game makes on its first start keeps its own and the
// complete one goes over the rest. Apart from SDL and ImGui, so
// tests/launcher_save_test.cpp can check it.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace moderngekko::frontend
{
namespace save_files
{
namespace fs = std::filesystem;

// The save's picture and icon, which never come with a build.
constexpr std::string_view BANNER = "banner.bin";

// A save folder's files, by name.
inline std::vector<fs::path> FilesIn(const fs::path& folder)
{
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
  {
    if (it->is_regular_file(ec))
      files.push_back(it->path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

// Copies a file over another through a temporary one next to it, so a copy
// cut short never leaves half a save.
inline bool CopyOver(const fs::path& from, const fs::path& to, std::string* error)
{
  std::error_code ec;
  const fs::path temporary = to.parent_path() / (to.filename().string() + ".part");
  fs::copy_file(from, temporary, fs::copy_options::overwrite_existing, ec);
  if (!ec)
    fs::rename(temporary, to, ec);
  if (ec)
  {
    fs::remove(temporary, ec);
    if (error)
      *error = to.filename().string() + ": " + ec.message();
    return false;
  }
  return true;
}
}  // namespace save_files

// Where Dolphin keeps a Wii game's save inside the user directory:
// Wii/title/00010000/<the disc ID's first four letters, in hex>/data.
inline std::filesystem::path WiiSaveFolder(const std::filesystem::path& user_directory,
                                           std::string_view disc_id)
{
  char title[9] = "00000000";
  for (std::size_t i = 0; i < 4 && i < disc_id.size(); ++i)
    std::snprintf(title + i * 2, 3, "%02x", static_cast<unsigned char>(disc_id[i]));
  return user_directory / "Wii" / "title" / "00010000" / title / "data";
}

// Where copies of the player's save are kept, one folder each, named by when
// they were made: SaveBackups/<disc ID>/<date>_<time>.
inline std::filesystem::path SaveBackupsFolder(const std::filesystem::path& user_directory,
                                               std::string_view disc_id)
{
  return user_directory / "SaveBackups" / std::string(disc_id);
}

// The complete save's files: what the build brings, without a banner.bin or
// hidden files (such as a .gitkeep).
inline std::vector<std::filesystem::path>
CompleteSaveFiles(const std::filesystem::path& complete_folder)
{
  std::vector<std::filesystem::path> files = save_files::FilesIn(complete_folder);
  std::erase_if(files, [](const std::filesystem::path& file) {
    const std::string name = file.filename().string();
    return name == save_files::BANNER || name.starts_with('.');
  });
  return files;
}

// Whether the game made its save: its banner.bin is there.
inline bool HasGameSave(const std::filesystem::path& save_folder)
{
  std::error_code ec;
  return std::filesystem::is_regular_file(save_folder / save_files::BANNER, ec);
}

// The newest copy of the player's save; nothing when there is none. The
// folders' names sort by time.
inline std::optional<std::filesystem::path> LatestSaveBackup(const std::filesystem::path& backups)
{
  std::optional<std::filesystem::path> latest;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(backups, ec), end; !ec && it != end;
       it.increment(ec))
  {
    if (it->is_directory(ec) && HasGameSave(it->path()) &&
        (!latest || it->path().filename() > latest->filename()))
      latest = it->path();
  }
  return latest;
}

enum class SaveError
{
  None,
  NoCompleteSave,  // the build brings none
  NoGameSave,      // the game hasn't made its save yet
  NoBackup,
  Backup,          // the player's save couldn't be copied: nothing changed
  Copy,
};

struct SaveResult
{
  SaveError error = SaveError::None;
  std::filesystem::path backup;  // where the player's save went
  std::string detail;            // the file and the system's reason

  explicit operator bool() const { return error == SaveError::None; }
};

inline SaveResult SaveOutcome(SaveError error, std::filesystem::path backup = {},
                              std::string detail = {})
{
  SaveResult result;
  result.error = error;
  result.backup = std::move(backup);
  result.detail = std::move(detail);
  return result;
}

// Copies the player's save into backups/<stamp> (stamp: the date and time,
// such as 2026-11-07_09-30-00), then the complete save's files over it.
inline SaveResult InstallCompleteSave(const std::filesystem::path& complete_folder,
                                      const std::filesystem::path& save_folder,
                                      const std::filesystem::path& backups,
                                      std::string_view stamp)
{
  namespace fs = std::filesystem;
  const std::vector<fs::path> complete = CompleteSaveFiles(complete_folder);
  if (complete.empty())
    return SaveOutcome(SaveError::NoCompleteSave);
  if (!HasGameSave(save_folder))
    return SaveOutcome(SaveError::NoGameSave);
  // A second one in the same second gets a number.
  fs::path backup = backups / std::string(stamp);
  std::error_code ec;
  for (int i = 2; fs::exists(backup, ec); ++i)
    backup = backups / (std::string(stamp) + "_" + std::to_string(i));
  fs::create_directories(backup, ec);
  if (ec)
    return SaveOutcome(SaveError::Backup, {}, backup.string() + ": " + ec.message());
  for (const fs::path& file : save_files::FilesIn(save_folder))
  {
    std::string error;
    if (!save_files::CopyOver(file, backup / file.filename(), &error))
    {
      fs::remove_all(backup, ec);
      return SaveOutcome(SaveError::Backup, {}, error);
    }
  }
  for (const fs::path& file : complete)
  {
    std::string error;
    if (!save_files::CopyOver(file, save_folder / file.filename(), &error))
      return SaveOutcome(SaveError::Copy, backup, error);
  }
  return SaveOutcome(SaveError::None, backup);
}

// Puts a copy of the player's save back as it was, files the complete save
// added taken out, and the copy, now used, removed.
inline SaveResult RestoreSaveBackup(const std::filesystem::path& backup,
                                    const std::filesystem::path& save_folder)
{
  namespace fs = std::filesystem;
  if (!HasGameSave(backup))
    return SaveOutcome(SaveError::NoBackup);
  const std::vector<fs::path> saved = save_files::FilesIn(backup);
  std::error_code ec;
  fs::create_directories(save_folder, ec);
  for (const fs::path& file : saved)
  {
    std::string error;
    if (!save_files::CopyOver(file, save_folder / file.filename(), &error))
      return SaveOutcome(SaveError::Copy, backup, error);
  }
  for (const fs::path& file : save_files::FilesIn(save_folder))
  {
    const bool kept = std::any_of(saved.begin(), saved.end(), [&](const fs::path& other) {
      return other.filename() == file.filename();
    });
    if (!kept)
      fs::remove(file, ec);
  }
  fs::remove_all(backup, ec);
  return SaveOutcome(SaveError::None, backup);
}
}  // namespace moderngekko::frontend
