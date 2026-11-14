#pragma once

// Files inside a CRI .cvm archive (Sonic Riders: Zero Gravity keeps almost
// everything in Image1/2/3.cvm). A file mod's files/<name>.cvm/ folder holds
// files that take the place of the ones with the same names inside that
// archive:
//
//   files/Image2.cvm/B11_OPTION.ADX
//
// A .cvm is a CRI header (CVMH, then ZONE) followed by an ISO 9660 image
// whose directory says where each file is. The archive is never copied (they
// are over a gigabyte): the plan below is a list of writes into it, made as
// the game starts, in memory, like any file mod. A new file no bigger than
// the room its original had is written there; a bigger one goes at the end
// of the archive. Either way the file's directory record gets its new place
// and size, and when the archive grows, the sizes the CRI header and the
// ISO 9660 volume keep are made the new one.
//
// The writes start on multiples of 4 bytes, as the disc patches that make
// them need: a record's fields are rewritten with the bytes around them.
//
// Apart from the standard library, so tests/cvm_mods_test.cpp can check it.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace moderngekko::cvm_mods
{
inline constexpr std::uint64_t SECTOR = 0x800;

// One file inside an archive, from a mod.
struct Replacement
{
  std::string inner;               // its path inside the archive, '/' between parts
  std::filesystem::path source;    // the mod's file
  std::string mod;                 // the mod's folder name, for messages
};

// Bytes to write at `offset` in the archive: the whole of `file`, or `bytes`.
struct Write
{
  std::uint64_t offset = 0;
  std::filesystem::path file;
  std::vector<std::uint8_t> bytes;
};

struct Problem
{
  std::string where;
  std::string why;
};

struct Plan
{
  std::vector<Write> writes;
  std::uint64_t size = 0;          // the archive's size after the writes
  std::size_t in_place = 0;        // files written where the original was
  std::size_t at_end = 0;          // files added at the end
  std::vector<Problem> problems;   // "<mod>: <archive>/<file>", why
};

namespace detail
{
inline std::string Lower(std::string_view text)
{
  std::string out(text);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

inline std::uint64_t AlignUp(std::uint64_t value, std::uint64_t to)
{
  return (value + to - 1) / to * to;
}

inline std::uint64_t ReadBE(const std::vector<std::uint8_t>& data, std::size_t at, int size)
{
  std::uint64_t value = 0;
  for (int i = 0; i < size; ++i)
    value = value << 8 | data[at + i];
  return value;
}

inline std::uint32_t ReadLE32(const std::vector<std::uint8_t>& data, std::size_t at)
{
  return static_cast<std::uint32_t>(data[at]) | static_cast<std::uint32_t>(data[at + 1]) << 8 |
         static_cast<std::uint32_t>(data[at + 2]) << 16 | static_cast<std::uint32_t>(data[at + 3]) << 24;
}

inline void PutBE(std::vector<std::uint8_t>* data, std::size_t at, std::uint64_t value, int size)
{
  for (int i = size - 1; i >= 0; --i, value >>= 8)
    (*data)[at + i] = static_cast<std::uint8_t>(value);
}

inline void PutLE32(std::vector<std::uint8_t>* data, std::size_t at, std::uint32_t value)
{
  for (int i = 0; i < 4; ++i, value >>= 8)
    (*data)[at + i] = static_cast<std::uint8_t>(value);
}

// ISO 9660's both-endian 32-bit field: little-endian, then big-endian.
inline void PutBoth32(std::vector<std::uint8_t>* data, std::size_t at, std::uint32_t value)
{
  PutLE32(data, at, value);
  PutBE(data, at + 4, value, 4);
}

inline std::vector<std::uint8_t> ReadAt(std::ifstream& file, std::uint64_t offset, std::size_t size)
{
  std::vector<std::uint8_t> data(size);
  file.clear();
  file.seekg(static_cast<std::streamoff>(offset));
  file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
  data.resize(static_cast<std::size_t>(std::max<std::streamsize>(file.gcount(), 0)));
  return data;
}

struct Entry
{
  std::uint64_t record = 0;  // where its directory record is, in the archive
  std::uint32_t lba = 0;
  std::uint32_t size = 0;
};
}  // namespace detail

// Where the archive's files are: lowercase path inside it -> entry. Empty,
// with why, when the archive isn't laid out as expected.
struct Index
{
  std::uint64_t file_size = 0;
  std::uint64_t iso = 0;   // where the ISO 9660 image starts
  std::uint64_t pvd = 0;   // its primary volume descriptor
  std::map<std::string, detail::Entry> files;
};

inline std::optional<Index> ReadIndex(const std::filesystem::path& cvm, std::string* why)
{
  using namespace detail;
  std::error_code ec;
  Index index;
  index.file_size = std::filesystem::file_size(cvm, ec);
  std::ifstream file(cvm, std::ios::binary);
  if (ec || !file)
  {
    *why = "can't read it";
    return std::nullopt;
  }
  const std::vector<std::uint8_t> head = ReadAt(file, 0, 0x1800);
  // CVMH: its size at 0x1C. ZONE: what follows it at 0x804, the ISO image's
  // size at 0x830. The image starts at 0x1800.
  if (head.size() < 0x1800 || std::string(head.begin(), head.begin() + 4) != "CVMH" ||
      std::string(head.begin() + 0x800, head.begin() + 0x804) != "ZONE")
  {
    *why = "it doesn't start like a CRI .cvm";
    return std::nullopt;
  }
  index.iso = 0x1800;
  if (ReadBE(head, 0x1C, 8) != index.file_size || ReadBE(head, 0x804, 8) != index.file_size - 0x80C ||
      ReadBE(head, 0x830, 8) != index.file_size - index.iso)
  {
    *why = "its header doesn't keep the sizes where expected";
    return std::nullopt;
  }
  index.pvd = index.iso + 16 * SECTOR;
  const std::vector<std::uint8_t> pvd = ReadAt(file, index.pvd, SECTOR);
  if (pvd.size() < SECTOR || pvd[0] != 1 || std::string(pvd.begin() + 1, pvd.begin() + 6) != "CD001" ||
      pvd[128] != 0x00 || pvd[129] != 0x08 ||  // 2048-byte blocks
      static_cast<std::uint64_t>(ReadLE32(pvd, 80)) * SECTOR + index.iso != index.file_size)
  {
    *why = "its ISO 9660 image isn't as expected";
    return std::nullopt;
  }

  // The directories, from the root.
  struct Pending
  {
    std::uint32_t lba, size;
    std::string path;
    int depth;
  };
  std::vector<Pending> pending = {{ReadLE32(pvd, 156 + 2), ReadLE32(pvd, 156 + 10), "", 0}};
  std::vector<std::uint32_t> seen;
  while (!pending.empty())
  {
    const Pending dir = pending.back();
    pending.pop_back();
    if (dir.depth > 12 || std::ranges::find(seen, dir.lba) != seen.end() || dir.size > (1u << 24))
      continue;
    seen.push_back(dir.lba);
    const std::uint64_t start = index.iso + static_cast<std::uint64_t>(dir.lba) * SECTOR;
    const std::vector<std::uint8_t> data = ReadAt(file, start, dir.size);
    std::size_t at = 0;
    while (at < data.size())
    {
      const std::uint8_t length = data[at];
      if (length == 0)
      {
        at = static_cast<std::size_t>(AlignUp(at + 1, SECTOR));
        continue;
      }
      if (length < 34 || at + length > data.size())
        break;
      const std::uint8_t name_length = data[at + 32];
      std::string name(reinterpret_cast<const char*>(&data[at + 33]),
                       std::min<std::size_t>(name_length, length - 33));
      const std::uint32_t lba = ReadLE32(data, at + 2);
      const std::uint32_t size = ReadLE32(data, at + 10);
      const bool folder = data[at + 25] & 0x02;
      if (name != std::string(1, '\0') && name != std::string(1, '\1'))
      {
        if (const std::size_t version = name.find(';'); version != std::string::npos)
          name.resize(version);
        const std::string path = dir.path.empty() ? name : dir.path + "/" + name;
        if (folder)
          pending.push_back({lba, size, path, dir.depth + 1});
        else
          index.files[Lower(path)] = {start + at, lba, size};
      }
      at += length;
    }
  }
  return index;
}

// The writes that put `replacements` (in the order the mods apply: a later
// one for the same file wins) into the archive at `cvm`, named `name` in
// messages.
inline Plan PlanReplacements(const std::filesystem::path& cvm, std::string_view name,
                             const std::vector<Replacement>& replacements)
{
  namespace fs = std::filesystem;
  using namespace detail;
  Plan plan;
  std::string why;
  const auto index = ReadIndex(cvm, &why);
  if (!index)
  {
    for (const Replacement& r : replacements)
      plan.problems.push_back({r.mod + ": " + std::string(name) + "/" + r.inner, std::string(name) + ": " + why});
    return plan;
  }
  plan.size = index->file_size;

  // The last mod's file for each one.
  std::map<std::string, const Replacement*> chosen;
  std::vector<std::string> order;
  for (const Replacement& r : replacements)
  {
    const std::string key = Lower(r.inner);
    if (!chosen.contains(key))
      order.push_back(key);
    chosen[key] = &r;
  }

  std::ifstream file(cvm, std::ios::binary);
  std::uint64_t end = index->file_size;  // whole sectors already
  std::uint64_t data_end = end;          // where the last added file's bytes end
  for (const std::string& key : order)
  {
    const Replacement& r = *chosen[key];
    const std::string where = r.mod + ": " + std::string(name) + "/" + r.inner;
    const auto entry = index->files.find(key);
    if (entry == index->files.end())
    {
      plan.problems.push_back({where, std::string(name) + " has no file " + r.inner});
      continue;
    }
    std::error_code ec;
    const std::uint64_t size = fs::file_size(r.source, ec);
    if (ec || size > 0xFFFFFFFFull)
    {
      plan.problems.push_back({where, ec ? "can't read it" : "it is too big"});
      continue;
    }
    std::uint64_t lba = entry->second.lba;
    const std::uint64_t room = AlignUp(entry->second.size, SECTOR);
    if (size <= room)
    {
      plan.writes.push_back({index->iso + lba * SECTOR, r.source, {}});
      ++plan.in_place;
    }
    else
    {
      if (AlignUp(end + size, SECTOR) > 0xFFFFFFFFull)
      {
        plan.problems.push_back({where, std::string(name) + " would grow past 4 GB"});
        continue;
      }
      lba = (end - index->iso) / SECTOR;
      plan.writes.push_back({end, r.source, {}});
      data_end = end + size;
      end = AlignUp(data_end, SECTOR);
      ++plan.at_end;
    }
    // The record's place (at +2) and size (at +10), both-endian, with the
    // bytes around them up to multiples of 4.
    const std::uint64_t record = entry->second.record;
    const std::uint64_t from = (record + 2) & ~std::uint64_t{3};
    const std::uint64_t to = AlignUp(record + 18, 4);
    std::vector<std::uint8_t> bytes = ReadAt(file, from, static_cast<std::size_t>(to - from));
    if (bytes.size() != to - from)
    {
      plan.problems.push_back({where, "can't read its directory record"});
      continue;
    }
    PutBoth32(&bytes, static_cast<std::size_t>(record + 2 - from), static_cast<std::uint32_t>(lba));
    PutBoth32(&bytes, static_cast<std::size_t>(record + 10 - from), static_cast<std::uint32_t>(size));
    plan.writes.push_back({from, {}, std::move(bytes)});
  }

  if (end != index->file_size)
  {
    // The last file's sector filled up, and the sizes kept in the header and
    // the volume made the archive's new one.
    const std::uint64_t pad_from = AlignUp(data_end, 4);
    if (pad_from < end)
      plan.writes.push_back({pad_from, {}, std::vector<std::uint8_t>(end - pad_from, 0)});
    std::vector<std::uint8_t> total(8);
    PutBE(&total, 0, end, 8);
    plan.writes.push_back({0x1C, {}, total});
    std::vector<std::uint8_t> zone(8);
    PutBE(&zone, 0, end - 0x80C, 8);
    plan.writes.push_back({0x804, {}, zone});
    std::vector<std::uint8_t> iso(8);
    PutBE(&iso, 0, end - index->iso, 8);
    plan.writes.push_back({0x830, {}, iso});
    std::vector<std::uint8_t> volume(8);
    PutBoth32(&volume, 0, static_cast<std::uint32_t>((end - index->iso) / SECTOR));
    plan.writes.push_back({index->pvd + 80, {}, volume});
    plan.size = end;
  }
  return plan;
}

// `writes` done on a copy of the archive's bytes (for tests: the runtime has
// the disc do them).
inline void ApplyTo(std::vector<std::uint8_t>* archive, const std::vector<Write>& writes)
{
  for (const Write& w : writes)
  {
    std::vector<std::uint8_t> bytes = w.bytes;
    if (!w.file.empty())
    {
      std::ifstream file(w.file, std::ios::binary);
      bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    if (archive->size() < w.offset + bytes.size())
      archive->resize(static_cast<std::size_t>(w.offset + bytes.size()), 0);
    std::ranges::copy(bytes, archive->begin() + static_cast<std::ptrdiff_t>(w.offset));
  }
}
}  // namespace moderngekko::cvm_mods
