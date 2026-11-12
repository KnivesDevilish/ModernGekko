#pragma once

// The Game Files page can put a complete save (the game finished at 100%, as
// the game's author left it) in place of the player's, and bring the
// player's back later. Only the game's own save files come with a build
// (MODERNGEKKO_COMPLETE_SAVE, copied to Resources/complete-save): the save's
// picture and icon, which the console shows for it, are the game's art, so
// the save the game makes on its first start keeps its own and the complete
// one goes over the rest.
//  - A Wii game: the save folder's files, without banner.bin (the picture and
//    icon), in Resources/complete-save itself (the build's main game).
//  - A GameCube game: its .gci files (the memory card's saves, as Dolphin
//    keeps them), with the picture and icon bytes blanked, in a folder named
//    after its disc ID (Resources/complete-save/GXEE8P). Installing puts the
//    player's own picture and icon back in.
// Apart from SDL and ImGui, so tests/launcher_save_test.cpp can check it.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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
// A GameCube save as Dolphin keeps it on a memory card folder: its 64-byte
// directory entry, then its blocks.
constexpr std::string_view GCI = ".gci";
constexpr std::size_t GCI_ENTRY_SIZE = 0x40;
constexpr std::size_t GCI_BLOCK_SIZE = 0x2000;

inline bool IsGci(const fs::path& file) { return file.extension() == GCI; }

inline std::optional<std::vector<unsigned char>> ReadBytes(const fs::path& file)
{
  std::ifstream in(file, std::ios::binary);
  if (!in)
    return std::nullopt;
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>());
}

// Writes a file through a temporary one next to it, as CopyOver does.
inline bool WriteBytes(const fs::path& to, const std::vector<unsigned char>& bytes,
                       std::string* error)
{
  std::error_code ec;
  const fs::path temporary = to.parent_path() / (to.filename().string() + ".part");
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out)
      ec = std::make_error_code(std::errc::io_error);
  }
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

// Where Dolphin keeps a GameCube game's saves inside the user directory: the
// memory card in slot A, a folder of .gci files for the disc's region
// (GC/USA/Card A, GC/EUR/Card A or GC/JPN/Card A, from the disc ID's fourth
// letter).
inline std::filesystem::path GameCubeCardFolder(const std::filesystem::path& user_directory,
                                                std::string_view disc_id)
{
  const char region = disc_id.size() > 3 ? disc_id[3] : 'E';
  const char* folder = region == 'E' ? "USA" : region == 'J' || region == 'K' ? "JPN" : "EUR";
  return user_directory / "GC" / folder / "Card A";
}

// A GameCube disc ID (G for games, D for demos), as opposed to a Wii one.
inline bool IsGameCubeDisc(std::string_view disc_id)
{
  return !disc_id.empty() && (disc_id[0] == 'G' || disc_id[0] == 'D');
}

// Where a .gci file keeps the save's picture and icon (its banner and the
// frames of its icon, as its directory entry describes them, Dolphin's
// GCMemcard reading): bytes [first, first + count) of the file. count is 0
// when it has neither; nothing when they run past the file.
struct ByteRange
{
  std::size_t first = 0;
  std::size_t count = 0;
};

inline std::optional<ByteRange> GciImageBytes(const std::vector<unsigned char>& gci)
{
  using save_files::GCI_ENTRY_SIZE;
  if (gci.size() < GCI_ENTRY_SIZE)
    return std::nullopt;
  const auto be16 = [&](std::size_t at) {
    return static_cast<std::size_t>(gci[at] << 8 | gci[at + 1]);
  };
  const std::size_t image_offset = static_cast<std::size_t>(gci[0x2C]) << 24 |
                                   static_cast<std::size_t>(gci[0x2D]) << 16 | be16(0x2E);
  if (image_offset == 0xFFFFFFFF)
    return ByteRange{GCI_ENTRY_SIZE, 0};
  constexpr std::size_t PALETTE = 256 * 2;
  constexpr std::size_t BANNER_PIXELS = 96 * 32;
  constexpr std::size_t ICON_PIXELS = 32 * 32;
  std::size_t length = 0;
  const unsigned banner_format = gci[0x07] & 0b11;
  if (banner_format == 1)  // CI8, with its palette
    length += BANNER_PIXELS + PALETTE;
  else if (banner_format == 2)  // RGB5A3
    length += BANNER_PIXELS * 2;
  // Up to eight icon frames, until one with no delay; a shared palette after
  // them when one of them uses it.
  const std::size_t icon_format = be16(0x30);
  const std::size_t icon_speed = be16(0x32);
  bool shared_palette = false;
  if ((icon_format & 0b11) != 0)
  {
    for (int frame = 0; frame < 8; ++frame)
    {
      if (((icon_speed >> (2 * frame)) & 0b11) == 0)
        break;
      const std::size_t format = (icon_format >> (2 * frame)) & 0b11;
      if (format == 1)
      {
        length += ICON_PIXELS;
        shared_palette = true;
      }
      else if (format == 2)
      {
        length += ICON_PIXELS * 2;
      }
      else if (format == 3)
      {
        length += ICON_PIXELS + PALETTE;
      }
    }
  }
  if (shared_palette)
    length += PALETTE;
  const ByteRange range{GCI_ENTRY_SIZE + image_offset, length};
  if (range.first > gci.size() || range.count > gci.size() - range.first)
    return std::nullopt;
  return range;
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

// A copy of a GameCube save: .gci files.
inline bool HasGciFiles(const std::filesystem::path& folder)
{
  const std::vector<std::filesystem::path> files = save_files::FilesIn(folder);
  return std::any_of(files.begin(), files.end(), save_files::IsGci);
}

// The newest copy of the player's save (a Wii save's files or GameCube .gci
// files); nothing when there is none. The folders' names sort by time.
inline std::optional<std::filesystem::path> LatestSaveBackup(const std::filesystem::path& backups)
{
  std::optional<std::filesystem::path> latest;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(backups, ec), end; !ec && it != end;
       it.increment(ec))
  {
    if (it->is_directory(ec) && (HasGameSave(it->path()) || HasGciFiles(it->path())) &&
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

// GameCube: the complete save's .gci files (in complete_folder) and whether
// the game made its own, the same files on its memory card.
inline std::vector<std::filesystem::path>
CompleteGameCubeSaveFiles(const std::filesystem::path& complete_folder)
{
  std::vector<std::filesystem::path> files = CompleteSaveFiles(complete_folder);
  std::erase_if(files, [](const std::filesystem::path& file) { return !save_files::IsGci(file); });
  return files;
}

inline bool HasGameCubeSave(const std::filesystem::path& complete_folder,
                            const std::filesystem::path& card_folder)
{
  const std::vector<std::filesystem::path> complete = CompleteGameCubeSaveFiles(complete_folder);
  std::error_code ec;
  return !complete.empty() &&
         std::all_of(complete.begin(), complete.end(), [&](const std::filesystem::path& file) {
           return std::filesystem::is_regular_file(card_folder / file.filename(), ec);
         });
}

// Copies the player's .gci files that the complete save replaces into
// backups/<stamp>, then writes each complete one over its own, with the
// picture and icon bytes taken from the player's file.
inline SaveResult InstallCompleteGameCubeSave(const std::filesystem::path& complete_folder,
                                              const std::filesystem::path& card_folder,
                                              const std::filesystem::path& backups,
                                              std::string_view stamp)
{
  namespace fs = std::filesystem;
  const std::vector<fs::path> complete = CompleteGameCubeSaveFiles(complete_folder);
  if (complete.empty())
    return SaveOutcome(SaveError::NoCompleteSave);
  if (!HasGameCubeSave(complete_folder, card_folder))
    return SaveOutcome(SaveError::NoGameSave);
  // Each complete file with the player's picture and icon, made before
  // anything is written, so a mismatch changes nothing.
  std::vector<std::vector<unsigned char>> merged;
  for (const fs::path& file : complete)
  {
    const fs::path own = card_folder / file.filename();
    auto bytes = save_files::ReadBytes(file);
    const auto own_bytes = save_files::ReadBytes(own);
    const auto range = bytes ? GciImageBytes(*bytes) : std::nullopt;
    const auto own_range = own_bytes ? GciImageBytes(*own_bytes) : std::nullopt;
    if (!range || !own_range || range->count != own_range->count)
      return SaveOutcome(SaveError::Copy, {},
                         file.filename().string() + ": not the same game's save");
    std::copy_n(own_bytes->begin() + static_cast<std::ptrdiff_t>(own_range->first),
                own_range->count, bytes->begin() + static_cast<std::ptrdiff_t>(range->first));
    merged.push_back(std::move(*bytes));
  }
  fs::path backup = backups / std::string(stamp);
  std::error_code ec;
  for (int i = 2; fs::exists(backup, ec); ++i)
    backup = backups / (std::string(stamp) + "_" + std::to_string(i));
  fs::create_directories(backup, ec);
  if (ec)
    return SaveOutcome(SaveError::Backup, {}, backup.string() + ": " + ec.message());
  for (const fs::path& file : complete)
  {
    std::string error;
    if (!save_files::CopyOver(card_folder / file.filename(), backup / file.filename(), &error))
    {
      fs::remove_all(backup, ec);
      return SaveOutcome(SaveError::Backup, {}, error);
    }
  }
  for (std::size_t i = 0; i < complete.size(); ++i)
  {
    std::string error;
    if (!save_files::WriteBytes(card_folder / complete[i].filename(), merged[i], &error))
      return SaveOutcome(SaveError::Copy, backup, error);
  }
  return SaveOutcome(SaveError::None, backup);
}

// Puts the player's .gci files back from a copy, and removes the copy. The
// memory card's other saves stay as they are.
inline SaveResult RestoreGameCubeSaveBackup(const std::filesystem::path& backup,
                                            const std::filesystem::path& card_folder)
{
  namespace fs = std::filesystem;
  if (!HasGciFiles(backup))
    return SaveOutcome(SaveError::NoBackup);
  std::error_code ec;
  fs::create_directories(card_folder, ec);
  for (const fs::path& file : save_files::FilesIn(backup))
  {
    std::string error;
    if (save_files::IsGci(file) &&
        !save_files::CopyOver(file, card_folder / file.filename(), &error))
      return SaveOutcome(SaveError::Copy, backup, error);
  }
  fs::remove_all(backup, ec);
  return SaveOutcome(SaveError::None, backup);
}
}  // namespace moderngekko::frontend
