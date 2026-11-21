// Data patches in mods (src/runtime/data_patches.hpp): reading patches.ini
// and making the patched copies of the game's files.
#include "data_patches.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace dp = moderngekko::data_patches;
namespace cvm = moderngekko::cvm_mods;

namespace
{
using Bytes = std::vector<std::uint8_t>;

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

bool HasProblem(const std::vector<dp::Problem>& problems, const std::string& where,
                const std::string& part)
{
  for (const dp::Problem& problem : problems)
    if (problem.where == where && problem.why.find(part) != std::string::npos)
      return true;
  return false;
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
  r[32] = static_cast<std::uint8_t>(name.size());
  std::copy(name.begin(), name.end(), r.begin() + 33);
  return r;
}

// A .cvm (as tests/cvm_mods_test.cpp makes them): CVMH and ZONE headers, then
// an ISO image with these files in its root directory.
Bytes Archive(const std::vector<std::pair<std::string, Bytes>>& files)
{
  constexpr std::uint64_t S = cvm::SECTOR;
  const std::uint64_t iso = 0x1800;
  std::uint32_t next = 19;  // 16 PVD, 17 terminator, 18 root
  std::vector<std::uint32_t> lbas;
  Bytes root;
  for (const Bytes& r : {Record(std::string(1, '\0'), 18, S, true), Record(std::string(1, '\1'), 18, S, true)})
    root.insert(root.end(), r.begin(), r.end());
  for (const auto& [name, data] : files)
  {
    lbas.push_back(next);
    const Bytes r = Record(name + ";1", next, static_cast<std::uint32_t>(data.size()), false);
    root.insert(root.end(), r.begin(), r.end());
    next += static_cast<std::uint32_t>(cvm::detail::AlignUp(data.size(), S) / S);
  }
  const std::uint64_t total = iso + next * S;
  Bytes o(total, 0);
  std::copy_n("CVMH", 4, o.begin());
  cvm::detail::PutBE(&o, 0x1C, total, 8);
  std::copy_n("ZONE", 4, o.begin() + 0x800);
  cvm::detail::PutBE(&o, 0x804, total - 0x80C, 8);
  cvm::detail::PutBE(&o, 0x830, total - iso, 8);
  const std::uint64_t pvd = iso + 16 * S;
  o[pvd] = 1;
  std::copy_n("CD001", 5, o.begin() + static_cast<std::ptrdiff_t>(pvd + 1));
  cvm::detail::PutBoth32(&o, static_cast<std::size_t>(pvd + 80), next);
  o[pvd + 129] = 0x08;
  const Bytes root_record = Record(std::string(1, '\0'), 18, S, true);
  std::copy(root_record.begin(), root_record.end(), o.begin() + static_cast<std::ptrdiff_t>(pvd + 156));
  std::copy(root.begin(), root.end(), o.begin() + static_cast<std::ptrdiff_t>(iso + 18 * S));
  for (std::size_t i = 0; i < files.size(); ++i)
    std::copy(files[i].second.begin(), files[i].second.end(),
              o.begin() + static_cast<std::ptrdiff_t>(iso + lbas[i] * S));
  return o;
}

// A REL module: 2 sections, code at 0x100-0x200 and data at 0x200-0x400.
Bytes FakeRel()
{
  Bytes rel(0x400, 0);
  const auto put = [&](std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
      rel[at + i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
  };
  put(0x0C, 2);
  put(0x10, 0x4C);
  put(0x4C, 0x100 | 1);
  put(0x50, 0x100);
  put(0x54, 0x200);
  put(0x58, 0x200);
  return rel;
}
}  // namespace

int main()
{
  // Keys: the place in the file, the type, km/h, comments and sections.
  std::vector<dp::Problem> problems;
  auto patches = dp::Parse("# comment\n[Values]\n"
                           "_Main.rel@0x1018:f32 = 216 km/h\r\n"
                           "_MAIN.REL@0X100C:F32 = 0.5\n"
                           "_Main.rel@0x1020:s32 = 250000\n"
                           "_Main.rel@0x1006:u16 = 0x10\n"
                           "_Main.rel@0x10BF:s8 = -2\n",
                           "GXEE8P", &problems);
  if (!problems.empty() || patches.size() != 5)
    return 2;
  // 216 km/h is 1.0f.
  if (patches[0].file != "_Main.rel" || patches[0].offset != 0x1018 ||
      patches[0].bytes != Bytes{0x3F, 0x80, 0x00, 0x00})
    return 3;
  if (patches[1].offset != 0x100C || patches[1].bytes != Bytes{0x3F, 0x00, 0x00, 0x00})
    return 4;
  // 250000 = 0x0003D090.
  if (patches[2].offset != 0x1020 || patches[2].bytes != Bytes{0x00, 0x03, 0xD0, 0x90})
    return 5;
  if (patches[3].offset != 0x1006 || patches[3].bytes != Bytes{0x00, 0x10} ||
      patches[4].offset != 0x10BF || patches[4].bytes != Bytes{0xFE})
    return 6;

  // Raw keys, and lines that can't be used.
  problems.clear();
  patches = dp::Parse("_Main.rel@0x10:u32 = 0xDEADBEEF\n"
                      "/sound/x.bin@16:bytes = 3f 80 00 00\n"
                      "_Main.rel@0x20:s16 = -1\n"
                      "_Main.rel@0x30:f32 = 1.5\n"
                      "no equals sign\n"
                      "gear.Default.topSpeed = 1\n"
                      "_Main.rel:f32 = 1\n"
                      "_Main.rel@0x10 = 1\n"
                      "../secret@0:u8 = 1\n"
                      "_Main.rel@zz:u8 = 1\n"
                      "_Main.rel@0:u64 = 1\n"
                      "_Main.rel@0:u8 = 256\n"
                      "_Main.rel@0:bytes = 3f8\n"
                      "_Main.rel@0:f32 = fast\n"
                      "_Main.rel@0:u8 =\n",
                      "GXEE8P", &problems);
  if (patches.size() != 4 || patches[0].bytes != Bytes{0xDE, 0xAD, 0xBE, 0xEF} ||
      patches[1].file != "sound/x.bin" || patches[1].offset != 16 ||
      patches[1].bytes != Bytes{0x3F, 0x80, 0x00, 0x00} || patches[2].bytes != Bytes{0xFF, 0xFF} ||
      patches[3].bytes != Bytes{0x3F, 0xC0, 0x00, 0x00})
    return 7;
  if (problems.size() != 11 || !HasProblem(problems, "patches.ini line 5", "=") ||
      !HasProblem(problems, "patches.ini line 6", "<file>@<offset>:<type>") ||
      !HasProblem(problems, "patches.ini line 7", "<file>@<offset>:<type>") ||
      !HasProblem(problems, "patches.ini line 8", "<file>@<offset>:<type>") ||
      !HasProblem(problems, "patches.ini line 9", "path under files/") ||
      !HasProblem(problems, "patches.ini line 10", "number") ||
      !HasProblem(problems, "patches.ini line 11", "type") ||
      !HasProblem(problems, "patches.ini line 12", "0 to 255") ||
      !HasProblem(problems, "patches.ini line 13", "hex") ||
      !HasProblem(problems, "patches.ini line 14", "number") ||
      !HasProblem(problems, "patches.ini line 15", "no value"))
    return 8;

  // "+=" and "*=".
  problems.clear();
  patches = dp::Parse("_Main.rel@0x1010:f32 += 80 km/h\n"
                      "_Main.rel@0x1068:s32 *= 1.5\n"
                      "_Main.rel@0x10:s16 += -3\n"
                      "_Main.rel@0x10:bytes += 1\n"
                      "_Main.rel@0x1010:f32 *= 2 km/h\n"
                      "gear.*.topSpeed += 1\n",
                      "GXEE8P", &problems);
  if (patches.size() != 3 || problems.size() != 3 ||
      !HasProblem(problems, "patches.ini line 4", "number type") ||
      !HasProblem(problems, "patches.ini line 5", "number") ||
      !HasProblem(problems, "patches.ini line 6", "<file>@<offset>:<type>"))
    return 17;
  if (patches[0].op != dp::Op::Add || patches[0].offset != 0x1010 || patches[0].bytes.size() != 4 ||
      patches[0].operand != 80.0 / 216.0)
    return 18;
  if (patches[1].op != dp::Op::Multiply || patches[1].offset != 0x1068 ||
      patches[1].operand != 1.5 || patches[2].bytes.size() != 2 || patches[2].operand != -3)
    return 19;

  // Making the copies.
  const fs::path base = fs::temp_directory_path() /
                        ("moderngekko-data-patches-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path game = base / "game" / "files";
  const fs::path out = base / "Cache" / "patched" / "GXEE8P";
  const Bytes rel = FakeRel();
  Write(game / "_Main.rel", rel);
  Write(game / "Sound" / "Voice.bin", Bytes(8, 0x11));
  // A file mod that replaces Voice.bin: the patches start from its copy.
  Write(base / "Mods" / "voices" / "files" / "Sound" / "Voice.bin", Bytes(8, 0x22));

  dp::ModPatches first{"first", base / "Mods" / "first", {}};
  dp::ModPatches voices{"voices", base / "Mods" / "voices", {}};
  dp::ModPatches second{"second", base / "Mods" / "second", {}};
  problems.clear();
  first.patches = dp::Parse("_main.REL@0x210:u8 = 1\n"   // data: applies (any capitals)
                            "_Main.rel@0x220:u8 = 1\n"   // the later mod changes it again
                            "_Main.rel@0x120:u8 = 7\n"   // code
                            "_Main.rel@0x3FE:u32 = 1\n"  // past the end
                            "Gone.bin@0:u8 = 1\n",       // no such file
                            "GXEE8P", &problems);
  second.patches = dp::Parse("_Main.rel@0x220:u8 = 2\nsound/voice.bin@2:u8 = 0x33\n", "GXEE8P", &problems);
  if (!problems.empty())
    return 10;

  dp::Built built = dp::BuildPatchedFiles({first, voices, second}, game, out, false);
  Bytes expected = rel;
  expected[0x210] = 1;
  expected[0x220] = 2;
  expected[0x120] = 7;
  Bytes voice(8, 0x22);
  voice[2] = 0x33;
  if (built.applied != 5 || built.applied_by_mod != std::vector<std::size_t>{3, 0, 2} || built.files != std::vector<std::string>{"_Main.rel", "Sound/Voice.bin"} ||
      Read(out / "_Main.rel") != expected || Read(out / "Sound" / "Voice.bin") != voice ||
      built.problems.size() != 2 || !HasProblem(built.problems, "first: patches.ini line 4", "past the end") ||
      !HasProblem(built.problems, "first: patches.ini line 5", "no file"))
    return 11;

  // Under StaticRecomp a module's code isn't changed, and a mod's copy of a
  // module isn't the starting point (file mods leave those out too).
  Write(base / "Mods" / "voices" / "files" / "_Main.rel", Bytes(0x400, 0x55));
  built = dp::BuildPatchedFiles({first, voices, second}, game, out, true);
  expected[0x120] = 0;
  if (Read(out / "_Main.rel") != expected ||
      !HasProblem(built.problems, "first: patches.ini line 3", "StaticRecomp"))
    return 12;
  // Without StaticRecomp, the mod's module is the starting point.
  built = dp::BuildPatchedFiles({first, voices, second}, game, out, false);
  if (Read(out / "_Main.rel")[0x10] != 0x55 || Read(out / "_Main.rel")[0x220] != 2)
    return 13;

  // "+=" and "*=" on what the file has, after the changes before them.
  {
    Bytes values = FakeRel();
    values[0x200] = 0x3F;  // f32 1.0
    values[0x200 + 1] = 0x80;
    values[0x204] = 0x00;  // s32 100000
    values[0x205] = 0x01;
    values[0x206] = 0x86;
    values[0x207] = 0xA0;
    values[0x208] = 200;   // u8
    values[0x20A] = 0x00;  // s16 2
    values[0x20B] = 0x02;
    Write(game / "values.bin", values);
    problems.clear();
    dp::ModPatches sets{"sets", base / "Mods" / "sets", dp::Parse("values.bin@0x200:f32 = 2\n", "GXEE8P", &problems)};
    dp::ModPatches changes{"changes", base / "Mods" / "changes",
                           dp::Parse("values.bin@0x200:f32 += 216 km/h\n"
                                     "values.bin@0x204:s32 *= 1.5\n"
                                     "values.bin@0x208:u8 += 100\n"
                                     "values.bin@0x20A:s16 += -5\n",
                                     "GXEE8P", &problems)};
    if (!problems.empty())
      return 20;
    built = dp::BuildPatchedFiles({sets, changes}, game, out, false);
    const Bytes result = Read(out / "values.bin");
    // 2 (set) + 1 (216 km/h) = 3.0f; 150000; 200 + 100 doesn't fit a u8; 2 - 5 = -3.
    if (result.size() != values.size() || result[0x200] != 0x40 || result[0x201] != 0x40 ||
        result[0x202] != 0 || result[0x203] != 0 || result[0x204] != 0x00 || result[0x205] != 0x02 ||
        result[0x206] != 0x49 || result[0x207] != 0xF0 || result[0x208] != 200 ||
        result[0x20A] != 0xFF || result[0x20B] != 0xFD ||
        !HasProblem(built.problems, "changes: patches.ini line 3", "doesn't fit"))
      return 21;
  }

  // A file too big to copy (a whole .cvm) isn't patched.
  {
    std::ofstream big(game / "Image2.cvm", std::ios::binary);
    big.seekp(static_cast<std::streamoff>(dp::MAX_FILE));
    big.put('\0');
  }
  dp::ModPatches huge{"huge", base / "Mods" / "huge", dp::Parse("Image2.cvm@0x10:u8 = 1\n", "RS9E8P", &problems)};
  built = dp::BuildPatchedFiles({huge}, game, out, false);
  if (!built.files.empty() || !HasProblem(built.problems, "huge: patches.ini line 1", "too big"))
    return 16;

  // Files inside a .cvm (Zero Gravity): from the archive, or from a mod's
  // files/<archive>.cvm/ copy; the patched copy goes in like a mod's file.
  {
    const fs::path zg = base / "zg" / "files";
    const fs::path zg_out = base / "Cache" / "patched" / "RS9E8P";
    Bytes param(0x40, 0);
    param[0x10] = 0x3F;  // 161 km/h: 0x3F3ED098
    param[0x11] = 0x3E;
    param[0x12] = 0xD0;
    param[0x13] = 0x98;
    Bytes zg_rel = FakeRel();
    zg_rel[0x210] = 0x42;
    const Bytes archive = Archive({{"PARAM.BIN", param}, {"_MAIN.REL", zg_rel}, {"OTHER.ADX", Bytes(3000, 0x77)}});
    Write(zg / "Image2.cvm", archive);
    problems.clear();
    dp::ModPatches speed{"speed", base / "Mods" / "speed",
                         dp::Parse("Image2.cvm/PARAM.BIN@0x10:f32 += 80 km/h\n"
                                   "image2.CVM/param.bin@0x14:u8 = 7\n"
                                   "Image2.cvm/NONE.BIN@0:u8 = 1\n"
                                   "Image3.cvm/X.BIN@0:u8 = 1\n"
                                   "Image2.cvm/_MAIN.REL@0x120:u8 = 7\n"
                                   "Image2.cvm/_MAIN.REL@0x211:u8 = 9\n",
                                   "RS9E8P", &problems)};
    if (!problems.empty() || speed.patches.size() != 6)
      return 22;
    built = dp::BuildPatchedFiles({speed}, zg, zg_out, true);
    Bytes param_expected = param;
    param_expected[0x10] = 0x3F;  // 241 km/h: 0x3F8ED098
    param_expected[0x11] = 0x8E;
    param_expected[0x12] = 0xD0;
    param_expected[0x13] = 0x98;
    param_expected[0x14] = 7;
    Bytes rel_expected = zg_rel;
    rel_expected[0x211] = 9;
    if (built.files != std::vector<std::string>{"Image2.cvm/PARAM.BIN", "Image2.cvm/_MAIN.REL"} ||
        built.applied != 3 || Read(zg_out / "Image2.cvm" / "PARAM.BIN") != param_expected ||
        Read(zg_out / "Image2.cvm" / "_MAIN.REL") != rel_expected || built.problems.size() != 3 ||
        !HasProblem(built.problems, "speed: patches.ini line 3", "Image2.cvm has no file NONE.BIN") ||
        !HasProblem(built.problems, "speed: patches.ini line 4", "no file Image3.cvm/X.BIN") ||
        !HasProblem(built.problems, "speed: patches.ini line 5", "StaticRecomp"))
      return 23;
    // The patched copy put into the archive (as the runtime does): it takes
    // the original's place, and the other files stay as they were.
    const cvm::Plan plan = cvm::PlanReplacements(
        zg / "Image2.cvm", "Image2.cvm",
        {{"PARAM.BIN", zg_out / "Image2.cvm" / "PARAM.BIN", "data patches"}});
    Bytes changed = archive;
    cvm::ApplyTo(&changed, plan.writes);
    Write(base / "zg-changed.cvm", changed);
    std::string why;
    if (plan.in_place != 1 || !plan.problems.empty() ||
        cvm::ReadInner(base / "zg-changed.cvm", "param.bin", dp::MAX_FILE, &why) != param_expected ||
        cvm::ReadInner(base / "zg-changed.cvm", "OTHER.ADX", dp::MAX_FILE, &why) != Bytes(3000, 0x77))
      return 24;
    // A mod's own copy is the starting point (a module's isn't, under
    // StaticRecomp), and the archive is never touched.
    Write(base / "Mods" / "speed" / "files" / "Image2.cvm" / "PARAM.BIN", Bytes(0x40, 0x01));
    Write(base / "Mods" / "speed" / "files" / "Image2.cvm" / "_MAIN.REL", Bytes(0x400, 0x55));
    built = dp::BuildPatchedFiles({speed}, zg, zg_out, true);
    const Bytes from_mod = Read(zg_out / "Image2.cvm" / "PARAM.BIN");
    if (from_mod.size() != 0x40 || from_mod[0x14] != 7 || from_mod[0x20] != 0x01 ||
        Read(zg_out / "Image2.cvm" / "_MAIN.REL") != rel_expected || Read(zg / "Image2.cvm") != archive)
      return 25;
    built = dp::BuildPatchedFiles({speed}, zg, zg_out, false);
    if (Read(zg_out / "Image2.cvm" / "_MAIN.REL")[0x10] != 0x55)
      return 26;
  }

  // Nothing applies: no copy, and the old copies go.
  built = dp::BuildPatchedFiles({}, game, out, false);
  if (!built.files.empty() || fs::exists(out / "_Main.rel"))
    return 14;
  // The game's files are never touched.
  if (Read(game / "_Main.rel") != rel || Read(game / "Sound" / "Voice.bin") != Bytes(8, 0x11))
    return 15;

  fs::remove_all(base);
  return 0;
}
