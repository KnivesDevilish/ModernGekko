// Data patches in mods (src/runtime/data_patches.hpp): reading patches.ini
// and making the patched copies of the game's files.
#include "data_patches.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace dp = moderngekko::data_patches;

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
  // SHA-1 against the standard's own example.
  const std::string abc = "abc";
  if (dp::detail::Sha1(Bytes(abc.begin(), abc.end())) != "a9993e364706816aba3e25717850c26c9cd0d89d" ||
      dp::detail::Sha1({}) != "da39a3ee5e6b4b0d3255bfef95601890afd80709")
    return 1;

  // gear.* keys: the place in _Main.rel, the type, km/h, capitals anywhere.
  std::vector<dp::Problem> problems;
  auto patches = dp::Parse("# comment\n[Gears]\n"
                           "gear.Default.level1.boostSpeed = 216 km/h\r\n"
                           "GEAR.highbooster.ACCELERATION = 0.5\n"
                           "gear.Cannonball.level3.maxAir = 250000\n"
                           "gear.AirTank.costInGearShop = 0x10\n"
                           "gear.Default.weightStat = -2\n",
                           "GXEE8P", &problems);
  if (!problems.empty() || patches.size() != 5)
    return 2;
  // Default, level 1, boost speed: 0x1EA240 + 0x68 + 0x18; 216 km/h is 1.0f.
  if (patches[0].file != "_Main.rel" || patches[0].offset != 0x1EA240 + 0x68 + 0x18 ||
      patches[0].bytes != Bytes{0x3F, 0x80, 0x00, 0x00} ||
      patches[0].sha1 != dp::SONIC_RIDERS_MAIN_REL_SHA1)
    return 3;
  // HighBooster (gear 1), acceleration at 0xC: 0.5f.
  if (patches[1].offset != 0x1EA240 + 0x1D0 + 0xC || patches[1].bytes != Bytes{0x3F, 0x00, 0x00, 0x00})
    return 4;
  // Cannonball (gear 40), level 3, max air: 250000 = 0x0003D090.
  if (patches[2].offset != 0x1EA240 + 0x1D0 * 40 + 0x68 + 28 * 2 ||
      patches[2].bytes != Bytes{0x00, 0x03, 0xD0, 0x90})
    return 5;
  if (patches[3].offset != 0x1EA240 + 0x1D0 * 26 + 0x6 || patches[3].bytes != Bytes{0x00, 0x10} ||
      patches[4].offset != 0x1EA240 + 0xBF || patches[4].bytes != Bytes{0xFE})
    return 6;

  // Raw keys, and lines that can't be used.
  problems.clear();
  patches = dp::Parse("_Main.rel@0x10:u32 = 0xDEADBEEF\n"
                      "/sound/x.bin@16:bytes = 3f 80 00 00\n"
                      "_Main.rel@0x20:s16 = -1\n"
                      "_Main.rel@0x30:f32 = 1.5\n"
                      "no equals sign\n"
                      "gear.Nobody.topSpeed = 1\n"
                      "gear.Default.wings = 1\n"
                      "gear.Default.level4.maxAir = 1\n"
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
      patches[3].bytes != Bytes{0x3F, 0xC0, 0x00, 0x00} || !patches[0].sha1.empty())
    return 7;
  if (problems.size() != 11 || !HasProblem(problems, "patches.ini line 5", "=") ||
      !HasProblem(problems, "patches.ini line 6", "Nobody") ||
      !HasProblem(problems, "patches.ini line 7", "README") ||
      !HasProblem(problems, "patches.ini line 8", "README") ||
      !HasProblem(problems, "patches.ini line 9", "path under files/") ||
      !HasProblem(problems, "patches.ini line 10", "number") ||
      !HasProblem(problems, "patches.ini line 11", "type") ||
      !HasProblem(problems, "patches.ini line 12", "0 to 255") ||
      !HasProblem(problems, "patches.ini line 13", "hex") ||
      !HasProblem(problems, "patches.ini line 14", "number") ||
      !HasProblem(problems, "patches.ini line 15", "no value"))
    return 8;
  // gear.* is only for Sonic Riders.
  problems.clear();
  if (!dp::Parse("gear.Default.topSpeed = 1\n", "RS9E8P", &problems).empty() ||
      !HasProblem(problems, "patches.ini line 1", "GXEE8P"))
    return 9;

  // "+=", "*=" and every gear at once.
  problems.clear();
  patches = dp::Parse("gear.*.topSpeed += 80 km/h\n"
                      "gear.Default.level1.maxAir *= 1.5\n"
                      "_Main.rel@0x10:s16 += -3\n"
                      "_Main.rel@0x10:bytes += 1\n"
                      "gear.Default.topSpeed *= 2 km/h\n"
                      "gear.*.wings += 1\n",
                      "GXEE8P", &problems);
  if (patches.size() != 43 || problems.size() != 3 ||
      !HasProblem(problems, "patches.ini line 4", "number type") ||
      !HasProblem(problems, "patches.ini line 5", "number") ||
      !HasProblem(problems, "patches.ini line 6", "README"))
    return 17;
  for (std::size_t g = 0; g < 41; ++g)
    if (patches[g].op != dp::Op::Add || patches[g].offset != 0x1EA240 + 0x1D0 * g + 0x10 ||
        patches[g].bytes.size() != 4 || patches[g].operand != 80.0 / 216.0)
      return 18;
  if (patches[41].op != dp::Op::Multiply || patches[41].offset != 0x1EA240 + 0x68 ||
      patches[41].operand != 1.5 || patches[42].bytes.size() != 2 || patches[42].operand != -3)
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
  // Values made for another _Main.rel (the gear table's check) aren't used.
  dp::Patch gear{"_Main.rel", 0x300, {9}, std::string(dp::SONIC_RIDERS_MAIN_REL_SHA1), "patches.ini line 9"};
  second.patches.push_back(gear);
  // ...and are used when the file is the one they were made for.
  dp::Patch matching{"_Main.rel", 0x301, {8}, dp::detail::Sha1(rel), "patches.ini line 10"};
  second.patches.push_back(matching);
  if (!problems.empty())
    return 10;

  dp::Built built = dp::BuildPatchedFiles({first, voices, second}, game, out, false);
  Bytes expected = rel;
  expected[0x210] = 1;
  expected[0x220] = 2;
  expected[0x120] = 7;
  expected[0x301] = 8;
  Bytes voice(8, 0x22);
  voice[2] = 0x33;
  if (built.applied != 6 || built.applied_by_mod != std::vector<std::size_t>{3, 0, 3} || built.files != std::vector<std::string>{"_Main.rel", "Sound/Voice.bin"} ||
      Read(out / "_Main.rel") != expected || Read(out / "Sound" / "Voice.bin") != voice ||
      built.problems.size() != 3 || !HasProblem(built.problems, "first: patches.ini line 4", "past the end") ||
      !HasProblem(built.problems, "first: patches.ini line 5", "no file") ||
      !HasProblem(built.problems, "second: patches.ini line 9", "isn't the one"))
    return 11;

  // Under StaticRecomp a module's code isn't changed, and a mod's copy of a
  // module isn't the starting point (file mods leave those out too).
  Write(base / "Mods" / "voices" / "files" / "_Main.rel", Bytes(0x400, 0x55));
  built = dp::BuildPatchedFiles({first, voices, second}, game, out, true);
  expected[0x120] = 0;
  if (Read(out / "_Main.rel") != expected ||
      !HasProblem(built.problems, "first: patches.ini line 3", "StaticRecomp"))
    return 12;
  // Without StaticRecomp, the mod's module is the starting point (and then
  // the gear check, made for the game's own, fails).
  built = dp::BuildPatchedFiles({first, voices, second}, game, out, false);
  if (Read(out / "_Main.rel")[0x10] != 0x55 ||
      !HasProblem(built.problems, "second: patches.ini line 10", "isn't the one"))
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
