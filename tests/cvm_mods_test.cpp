// Files inside a CRI .cvm (src/runtime/cvm_mods.hpp): an archive built here,
// files put into it as a mod would, and the result read back.
#include "cvm_mods.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace cvm = moderngekko::cvm_mods;

namespace
{
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint64_t S = cvm::SECTOR;

void Write(const fs::path& path, const Bytes& data)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc)
      .write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

Bytes Read(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

Bytes Filled(std::size_t size, std::uint8_t value)
{
  return Bytes(size, value);
}

// A directory record, as ISO 9660 writes it.
Bytes Record(const std::string& name, std::uint32_t lba, std::uint32_t size, bool folder)
{
  const std::size_t length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
  Bytes r(length, 0);
  r[0] = static_cast<std::uint8_t>(length);
  cvm::detail::PutBoth32(&r, 2, lba);
  cvm::detail::PutBoth32(&r, 10, size);
  r[25] = folder ? 2 : 0;
  r[28] = 1;
  r[31] = 1;
  r[32] = static_cast<std::uint8_t>(name.size());
  std::copy(name.begin(), name.end(), r.begin() + 33);
  return r;
}

// A .cvm: CVMH and ZONE headers, then an ISO image with these files in its
// root directory (one sector), one after another.
Bytes Archive(const std::vector<std::pair<std::string, Bytes>>& files, std::vector<std::uint32_t>* lbas)
{
  const std::uint64_t iso = 0x1800;
  std::uint32_t next = 19;  // 16 PVD, 17 terminator, 18 root
  Bytes root;
  const Bytes self = Record(std::string(1, '\0'), 18, S, true);
  const Bytes parent = Record(std::string(1, '\1'), 18, S, true);
  root.insert(root.end(), self.begin(), self.end());
  root.insert(root.end(), parent.begin(), parent.end());
  for (const auto& [name, data] : files)
  {
    lbas->push_back(next);
    const Bytes r = Record(name + ";1", next, static_cast<std::uint32_t>(data.size()), false);
    root.insert(root.end(), r.begin(), r.end());
    next += static_cast<std::uint32_t>(cvm::detail::AlignUp(data.size(), S) / S);
  }
  const std::uint64_t total = iso + next * S;
  Bytes out(total, 0);
  std::copy_n("CVMH", 4, out.begin());
  cvm::detail::PutBE(&out, 0x1C, total, 8);
  std::copy_n("ZONE", 4, out.begin() + 0x800);
  cvm::detail::PutBE(&out, 0x804, total - 0x80C, 8);
  cvm::detail::PutBE(&out, 0x830, total - iso, 8);
  Bytes& o = out;
  const std::uint64_t pvd = iso + 16 * S;
  o[pvd] = 1;
  std::copy_n("CD001", 5, o.begin() + static_cast<std::ptrdiff_t>(pvd + 1));
  cvm::detail::PutBoth32(&o, static_cast<std::size_t>(pvd + 80), next);
  o[pvd + 128] = 0x00;
  o[pvd + 129] = 0x08;
  o[pvd + 130] = 0x08;
  o[pvd + 131] = 0x00;
  const Bytes root_record = Record(std::string(1, '\0'), 18, S, true);
  std::copy(root_record.begin(), root_record.end(), o.begin() + static_cast<std::ptrdiff_t>(pvd + 156));
  o[iso + 17 * S] = 255;
  std::copy_n("CD001", 5, o.begin() + static_cast<std::ptrdiff_t>(iso + 17 * S + 1));
  std::copy(root.begin(), root.end(), o.begin() + static_cast<std::ptrdiff_t>(iso + 18 * S));
  for (std::size_t i = 0; i < files.size(); ++i)
    std::copy(files[i].second.begin(), files[i].second.end(),
              o.begin() + static_cast<std::ptrdiff_t>(iso + (*lbas)[i] * S));
  return out;
}

// A file's bytes, as the archive's index says where they are.
Bytes FileIn(const fs::path& archive, const std::string& name)
{
  std::string why;
  const auto index = cvm::ReadIndex(archive, &why);
  if (!index || !index->files.contains(cvm::detail::Lower(name)))
    return {};
  const auto& entry = index->files.at(cvm::detail::Lower(name));
  const Bytes all = Read(archive);
  const std::uint64_t at = index->iso + entry.lba * S;
  return Bytes(all.begin() + static_cast<std::ptrdiff_t>(at),
               all.begin() + static_cast<std::ptrdiff_t>(at + entry.size));
}

bool HasProblem(const cvm::Plan& plan, const std::string& where, const std::string& part)
{
  for (const cvm::Problem& p : plan.problems)
    if (p.where == where && p.why.find(part) != std::string::npos)
      return true;
  return false;
}
}  // namespace

int main()
{
  const fs::path base = fs::temp_directory_path() /
                        ("moderngekko-cvm-mods-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::vector<std::uint32_t> lbas;
  // Names of odd and even length, so records start at all kinds of places.
  const Bytes original = Archive({{"B10_SELECT.ADX", Filled(5000, 0x10)},
                                  {"B11_OPTION.ADX", Filled(3000, 0x11)},
                                  {"E2.AFS", Filled(100, 0x12)},
                                  {"OTHER.BIN", Filled(10, 0x13)}},
                                 &lbas);
  const fs::path archive = base / "game" / "Image2.cvm";
  Write(archive, original);

  std::string why;
  const auto index = cvm::ReadIndex(archive, &why);
  if (!index || index->files.size() != 4 || index->iso != 0x1800 ||
      index->files.at("b11_option.adx").lba != lbas[1] || index->files.at("b11_option.adx").size != 3000)
    return 1;

  // B10_SELECT: 6100 bytes fit in the 3 sectors its 5000 had: in place (the
  // later mod's file wins). B11_OPTION and E2.AFS don't fit: at the end. A
  // file the archive hasn't is left out.
  Write(base / "a" / "B10_SELECT.ADX", Filled(6000, 0xA0));
  Write(base / "b" / "B10_SELECT.ADX", Filled(6100, 0xB0));
  Write(base / "a" / "b11_option.adx", Filled(9000, 0xA1));
  Write(base / "a" / "E2.AFS", Filled(4999, 0xA2));
  Write(base / "a" / "NEW.BIN", Filled(10, 0xA3));
  const cvm::Plan plan = cvm::PlanReplacements(archive, "Image2.cvm",
                                               {{"B10_SELECT.ADX", base / "a" / "B10_SELECT.ADX", "a"},
                                                {"b11_option.adx", base / "a" / "b11_option.adx", "a"},
                                                {"E2.AFS", base / "a" / "E2.AFS", "a"},
                                                {"NEW.BIN", base / "a" / "NEW.BIN", "a"},
                                                {"b10_select.adx", base / "b" / "B10_SELECT.ADX", "b"}});
  if (plan.in_place != 1 || plan.at_end != 2 || plan.problems.size() != 1 ||
      !HasProblem(plan, "a: Image2.cvm/NEW.BIN", "has no file"))
    return 2;
  for (const cvm::Write& w : plan.writes)
    if (w.offset % 4 != 0)
      return 3;

  Bytes patched = original;
  cvm::ApplyTo(&patched, plan.writes);
  const fs::path result = base / "result" / "Image2.cvm";
  Write(result, patched);
  // The archive reads back as one: the header and volume sizes match it.
  const auto after = cvm::ReadIndex(result, &why);
  if (!after || after->file_size != plan.size || plan.size % S != 0 || plan.size <= original.size())
    return 4;
  if (FileIn(result, "B10_SELECT.ADX") != Filled(6100, 0xB0) ||
      FileIn(result, "B11_OPTION.ADX") != Filled(9000, 0xA1) ||
      FileIn(result, "E2.AFS") != Filled(4999, 0xA2) || FileIn(result, "OTHER.BIN") != Filled(10, 0x13))
    return 5;
  // In place: B10_SELECT kept its sector. The others went past the old end.
  if (after->files.at("b10_select.adx").lba != lbas[0] ||
      after->files.at("e2.afs").lba * S + 0x1800 < original.size() ||
      after->files.at("b11_option.adx").lba * S + 0x1800 < original.size() ||
      after->files.at("other.bin").lba != lbas[3])
    return 6;

  // Everything fits: nothing at the end, the header left as it is.
  Write(base / "c" / "OTHER.BIN", Filled(2048, 0xC3));
  const cvm::Plan small = cvm::PlanReplacements(archive, "Image2.cvm",
                                                {{"OTHER.BIN", base / "c" / "OTHER.BIN", "c"}});
  Bytes small_patched = original;
  cvm::ApplyTo(&small_patched, small.writes);
  Write(result, small_patched);
  if (small.at_end != 0 || small.size != original.size() || small_patched.size() != original.size() ||
      FileIn(result, "OTHER.BIN") != Filled(2048, 0xC3))
    return 7;

  // Not a .cvm laid out as expected: nothing is changed, and each file says why.
  Bytes odd = original;
  cvm::detail::PutBE(&odd, 0x1C, 1234, 8);
  Write(base / "odd" / "Image1.cvm", odd);
  const cvm::Plan refused = cvm::PlanReplacements(base / "odd" / "Image1.cvm", "Image1.cvm",
                                                  {{"OTHER.BIN", base / "c" / "OTHER.BIN", "c"}});
  if (!refused.writes.empty() || !HasProblem(refused, "c: Image1.cvm/OTHER.BIN", "header"))
    return 8;

  // The game's archive is never touched.
  if (Read(archive) != original)
    return 9;
  fs::remove_all(base);
  return 0;
}
