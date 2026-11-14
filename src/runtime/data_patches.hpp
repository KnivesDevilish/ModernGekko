#pragma once

// Data patches in file mods (tools/file_mods.hpp): a mod's patches.ini
// changes values inside the game's files (an Extreme Gear's stats, say) as
// the game starts, without the mod carrying any of the game's files: only
// the values its author chose. One change per line, "key = value":
//
//   # Sonic Riders (GXEE8P): an Extreme Gear's stats, by name
//   gear.Default.level1.boostSpeed = 300 km/h
//   gear.HighBooster.acceleration = 0.0009
//   # Any game: a value at a place in a file under files/
//   _Main.rel@0x1EA2C0:f32 = 1.2
//   # ...or in a file inside one of its .cvm archives (Zero Gravity)
//   Image2.cvm/_MAIN.REL@0x10:f32 = 1.2
//
// Raw keys are <file>@<offset>:<type>, the type one of u8 u16 u32 s8 s16
// s32 f32 (big-endian, as the consoles store them) or bytes (the value in
// hex: 3F 80 00 00). Speeds can be written in km/h (the games keep them as
// km/h divided by 216). "+=" adds to the value the file has and "*="
// multiplies it, and gear.*. changes every gear:
//
//   gear.*.topSpeed += 80 km/h
//   gear.*.level1.maxAir *= 1.5
//
// The gear table's layout (where it starts in Sonic Riders' _Main.rel, the
// size of each gear and what each field is) comes from the GPL-3.0 source of
// Sonic Riders Tournament Edition (github.com/RidersBoulevard/sonicriderste,
// include/riders/gear.hpp). gear.* keys only apply to the _Main.rel of the
// USA release they were found in, checked by its SHA-1.
//
// As the game starts, the runtime makes a patched copy of each file named,
// in Cache/patched/<disc ID>/ (from the game's own file, or from the last
// file mod that replaces it), and gives that copy to the disc as the file
// mods' are given. Nothing is written to the extracted game. Under
// StaticRecomp, changes to a module's code (a .rel's code sections) are left
// out: the recompiled module is the game's own code.
//
// A file over MAX_FILE (a whole .cvm archive, say) isn't patched this way:
// its copy would be too big. A file inside a .cvm is, from the archive (or
// from the last mod's files/<archive>.cvm/<file>): its patched copy, in
// Cache/patched/<disc ID>/<archive>.cvm/, goes into the archive as a mod's
// file would (cvm_mods.hpp).
//
// Apart from the standard library, so tests/data_patches_test.cpp can check it.

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cvm_mods.hpp"

namespace moderngekko::data_patches
{
// One change: `bytes` written at `offset` in files/<file>.
enum class Op
{
  Set,       // =   the bytes
  Add,       // +=  the operand added to the value there
  Multiply,  // *=  the value there times the operand
};

struct Patch
{
  std::string file;  // under files/, with '/' ("Image2.cvm/x.bin": inside an archive)
  std::uint32_t offset = 0;
  std::vector<std::uint8_t> bytes;  // for Set; for Add and Multiply, as many as the type has
  std::string sha1;   // when not empty, the file must be this one (lowercase hex)
  std::string where;  // "patches.ini line 3", for messages
  Op op = Op::Set;
  int type = 0;        // for Add and Multiply: the Type, as a number
  double operand = 0;  // for Add and Multiply
};

struct Problem
{
  std::string where;
  std::string why;
};

// Sonic Riders (USA, GXEE8P): its _Main.rel and the Extreme Gear table in it.
inline constexpr std::string_view SONIC_RIDERS = "GXEE8P";
inline constexpr std::string_view SONIC_RIDERS_MAIN_REL = "_Main.rel";
inline constexpr std::string_view SONIC_RIDERS_MAIN_REL_SHA1 =
    "1607fbb1ccabf116642c3c59ee9b21c304a1db26";
inline constexpr std::uint32_t GEARS = 0x1EA240;
inline constexpr std::uint32_t GEAR_SIZE = 0x1D0;
inline constexpr std::array<std::string_view, 41> GEAR_NAMES = {
    "Default",      "HighBooster",  "AutoSlider",  "PowerfulGear", "Fastest",    "TurboStar",
    "SpeedBalancer", "BlueStar2",   "Access",      "Beginner",     "Accelerator", "TrapGear",
    "LightBoard",   "SlideBooster", "Legend",      "MagicCarpet",  "AirBroom",   "Hovercraft",
    "ChaosEmerald", "Faster",       "Gambler",     "PowerGear",    "OpaOpa",     "TheCrazy",
    "Berserker",    "ERider",       "AirTank",     "HeavyBike",    "Destroyer",  "Omnipotence",
    "CoverS",       "CoverF",       "CoverP",      "HangOn",       "SuperHangOn", "Darkness",
    "Grinder",      "AdvantageS",   "AdvantageF",  "AdvantageP",   "Cannonball"};

enum class Type
{
  U8,
  U16,
  U32,
  S8,
  S16,
  S32,
  F32,
  Bytes
};

struct GearField
{
  std::string_view name;
  std::uint32_t offset;  // in the gear, or in a level's stats
  Type type;
};

// A gear's own fields (Tournament Edition's names).
inline constexpr std::array<GearField, 28> GEAR_FIELDS = {{
    {"useFlags", 0x0, Type::U32},
    {"type", 0x4, Type::U8},
    {"model", 0x5, Type::U8},
    {"costInGearShop", 0x6, Type::U16},
    {"acceleration", 0xC, Type::F32},
    {"topSpeed", 0x10, Type::F32},
    {"offRoadSpeed", 0x14, Type::F32},
    {"speedHandlingMultiplier", 0x18, Type::F32},
    {"weight", 0x1C, Type::F32},
    {"extraTypeAttributes", 0x23, Type::U8},
    {"turningSpeedLoss", 0x24, Type::F32},
    {"handling", 0x28, Type::F32},
    {"backAxelHandling", 0x2C, Type::F32},
    {"frontAxelHandling", 0x30, Type::F32},
    {"driftRadius", 0x34, Type::F32},
    {"driftRotation", 0x38, Type::F32},
    {"backAxelDriftRotation", 0x3C, Type::F32},
    {"frontAxelDriftRotation", 0x40, Type::F32},
    {"driftDashFrames", 0x50, Type::S32},
    {"trickAirGainMultiplier", 0x54, Type::F32},
    {"shortcutAirGainMultiplier", 0x58, Type::F32},
    {"QTEAirGainMultiplier", 0x5C, Type::F32},
    {"specialFlags", 0x60, Type::U32},
    {"jumpChargeCostMultiplier", 0x64, Type::F32},
    {"tempoStat", 0xBC, Type::S8},
    {"efficiencyStat", 0xBD, Type::S8},
    {"combatStat", 0xBE, Type::S8},
    {"weightStat", 0xBF, Type::S8},
}};

// Each level's stats (level1, level2, level3), 28 bytes apart from 0x68.
inline constexpr std::uint32_t GEAR_LEVELS = 0x68;
inline constexpr std::uint32_t GEAR_LEVEL_SIZE = 28;
inline constexpr std::array<GearField, 7> GEAR_LEVEL_FIELDS = {{
    {"maxAir", 0x0, Type::S32},
    {"passiveAirDrain", 0x4, Type::S32},
    {"driftingAirCost", 0x8, Type::S32},
    {"boostCost", 0xC, Type::S32},
    {"tornadoCost", 0x10, Type::S32},
    {"driftDashSpeed", 0x14, Type::F32},
    {"boostSpeed", 0x18, Type::F32},
}};

inline constexpr std::uint64_t MAX_FILE = 64ull << 20;

// The games keep speeds as km/h divided by this.
inline constexpr double KMH_PER_UNIT = 216.0;

namespace detail
{
inline std::string Trim(std::string_view text)
{
  const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!text.empty() && space(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && space(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return std::string(text);
}

inline std::string Lower(std::string_view text)
{
  std::string out(text);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

inline bool EqualNoCase(std::string_view a, std::string_view b)
{
  return Lower(a) == Lower(b);
}

inline std::optional<std::uint64_t> ParseUnsigned(std::string_view text)
{
  std::string t = Trim(text);
  int base = 10;
  std::string_view digits = t;
  if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
  {
    digits.remove_prefix(2);
    base = 16;
  }
  std::uint64_t value = 0;
  const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
  if (ec != std::errc() || end != digits.data() + digits.size() || digits.empty())
    return std::nullopt;
  return value;
}

inline std::optional<std::int64_t> ParseInteger(std::string_view text)
{
  std::string t = Trim(text);
  const bool negative = !t.empty() && t[0] == '-';
  const auto magnitude = ParseUnsigned(negative ? std::string_view(t).substr(1) : t);
  if (!magnitude || *magnitude > 0x7FFFFFFFFFFFFFFFull)
    return std::nullopt;
  return negative ? -static_cast<std::int64_t>(*magnitude) : static_cast<std::int64_t>(*magnitude);
}

// A number, or a speed in km/h ("300 km/h").
inline std::optional<double> ParseFloat(std::string_view text)
{
  std::string t = Trim(text);
  bool kmh = false;
  if (Lower(t).ends_with("km/h"))
  {
    t = Trim(std::string_view(t).substr(0, t.size() - 4));
    kmh = true;
  }
  double value = 0;
  const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), value);
  if (ec != std::errc() || end != t.data() + t.size() || t.empty())
    return std::nullopt;
  return kmh ? value / KMH_PER_UNIT : value;
}

inline void PutBigEndian(std::vector<std::uint8_t>* out, std::uint64_t value, int size)
{
  for (int i = size - 1; i >= 0; --i)
    out->push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

// The value's bytes for a type; empty, with why, when it doesn't fit.
inline std::vector<std::uint8_t> Encode(Type type, std::string_view value, std::string* why)
{
  std::vector<std::uint8_t> out;
  const auto integer = [&](std::int64_t low, std::int64_t high, int size) {
    const auto parsed = ParseInteger(value);
    if (!parsed || *parsed < low || *parsed > high)
    {
      *why = "the value isn't a whole number from " + std::to_string(low) + " to " +
             std::to_string(high);
      return;
    }
    PutBigEndian(&out, static_cast<std::uint64_t>(*parsed), size);
  };
  switch (type)
  {
  case Type::U8:
    integer(0, 0xFF, 1);
    break;
  case Type::U16:
    integer(0, 0xFFFF, 2);
    break;
  case Type::U32:
    integer(0, 0xFFFFFFFFll, 4);
    break;
  case Type::S8:
    integer(-0x80, 0x7F, 1);
    break;
  case Type::S16:
    integer(-0x8000, 0x7FFF, 2);
    break;
  case Type::S32:
    integer(-0x80000000ll, 0x7FFFFFFF, 4);
    break;
  case Type::F32:
  {
    const auto parsed = ParseFloat(value);
    if (!parsed)
    {
      *why = "the value isn't a number";
      break;
    }
    PutBigEndian(&out, std::bit_cast<std::uint32_t>(static_cast<float>(*parsed)), 4);
    break;
  }
  case Type::Bytes:
  {
    std::string hex;
    for (char c : value)
      if (!std::isspace(static_cast<unsigned char>(c)))
        hex.push_back(c);
    if (hex.empty() || hex.size() % 2 != 0)
    {
      *why = "bytes need pairs of hex digits, such as 3F 80 00 00";
      break;
    }
    for (std::size_t i = 0; i < hex.size(); i += 2)
    {
      std::uint8_t byte = 0;
      const auto [end, ec] = std::from_chars(hex.data() + i, hex.data() + i + 2, byte, 16);
      if (ec != std::errc() || end != hex.data() + i + 2)
      {
        *why = "bytes need pairs of hex digits, such as 3F 80 00 00";
        out.clear();
        break;
      }
      out.push_back(byte);
    }
    break;
  }
  }
  if (!why->empty())
    out.clear();
  return out;
}

inline std::optional<Type> TypeNamed(std::string_view name)
{
  static constexpr std::array<std::pair<std::string_view, Type>, 8> types = {{
      {"u8", Type::U8},
      {"u16", Type::U16},
      {"u32", Type::U32},
      {"s8", Type::S8},
      {"s16", Type::S16},
      {"s32", Type::S32},
      {"f32", Type::F32},
      {"bytes", Type::Bytes},
  }};
  for (const auto& [text, type] : types)
    if (EqualNoCase(name, text))
      return type;
  return std::nullopt;
}

inline std::size_t TypeSize(Type type)
{
  switch (type)
  {
  case Type::U8:
  case Type::S8:
    return 1;
  case Type::U16:
  case Type::S16:
    return 2;
  default:
    return 4;
  }
}

// A path under files/: no "..", no drive or leading slash, '/' between parts.
inline std::optional<std::string> CleanFilePath(std::string_view text)
{
  std::string path = Trim(text);
  std::replace(path.begin(), path.end(), '\\', '/');
  while (!path.empty() && path.front() == '/')
    path.erase(path.begin());
  if (path.empty() || path.find(':') != std::string::npos)
    return std::nullopt;
  std::size_t start = 0;
  while (start <= path.size())
  {
    const std::size_t end = std::min(path.find('/', start), path.size());
    const std::string_view part(path.data() + start, end - start);
    if (part.empty() || part == "." || part == "..")
      return std::nullopt;
    start = end + 1;
  }
  return path;
}

// SHA-1, for the files gear.* keys were made for.
inline std::string Sha1(const std::vector<std::uint8_t>& data)
{
  std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::vector<std::uint8_t> message = data;
  const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
  message.push_back(0x80);
  while (message.size() % 64 != 56)
    message.push_back(0);
  for (int i = 7; i >= 0; --i)
    message.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
  for (std::size_t chunk = 0; chunk < message.size(); chunk += 64)
  {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = static_cast<std::uint32_t>(message[chunk + 4 * i]) << 24 |
             static_cast<std::uint32_t>(message[chunk + 4 * i + 1]) << 16 |
             static_cast<std::uint32_t>(message[chunk + 4 * i + 2]) << 8 |
             message[chunk + 4 * i + 3];
    for (int i = 16; i < 80; ++i)
      w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i)
    {
      std::uint32_t f, k;
      if (i < 20)
        f = (b & c) | (~b & d), k = 0x5A827999;
      else if (i < 40)
        f = b ^ c ^ d, k = 0x6ED9EBA1;
      else if (i < 60)
        f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
      else
        f = b ^ c ^ d, k = 0xCA62C1D6;
      const std::uint32_t temp = std::rotl(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = std::rotl(b, 30);
      b = a;
      a = temp;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
  }
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  for (std::uint32_t word : h)
    for (int i = 28; i >= 0; i -= 4)
      out.push_back(digits[(word >> i) & 0xF]);
  return out;
}

inline std::optional<std::vector<std::uint8_t>> ReadFile(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return std::nullopt;
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>());
}

inline std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t at)
{
  return static_cast<std::uint32_t>(data[at]) << 24 | static_cast<std::uint32_t>(data[at + 1]) << 16 |
         static_cast<std::uint32_t>(data[at + 2]) << 8 | data[at + 3];
}

// The code sections of a REL module, as [start, end) in the file. Empty when
// it doesn't look like one.
inline std::vector<std::pair<std::uint32_t, std::uint32_t>>
RelCodeSections(const std::vector<std::uint8_t>& rel)
{
  std::vector<std::pair<std::uint32_t, std::uint32_t>> code;
  if (rel.size() < 0x40)
    return code;
  const std::uint32_t count = ReadU32(rel, 0x0C);
  const std::uint32_t table = ReadU32(rel, 0x10);
  if (count > 64 || table > rel.size() || table + 8ull * count > rel.size())
    return code;
  for (std::uint32_t i = 0; i < count; ++i)
  {
    const std::uint32_t offset = ReadU32(rel, table + 8 * i);
    const std::uint32_t size = ReadU32(rel, table + 8 * i + 4);
    if ((offset & 1) && size != 0)
      code.emplace_back(offset & ~1u, (offset & ~1u) + size);
  }
  return code;
}

inline bool EndsWithNoCase(std::string_view text, std::string_view end)
{
  return text.size() >= end.size() && EqualNoCase(text.substr(text.size() - end.size()), end);
}

// `relative` (parts split by '/') under `root`, each part found whatever its
// capitals, as the disc finds them: the path as it is on disk, or nothing.
inline std::optional<std::filesystem::path> FindNoCase(const std::filesystem::path& root,
                                                       std::string_view relative)
{
  namespace fs = std::filesystem;
  fs::path found;
  fs::path here = root;
  std::size_t start = 0;
  while (start <= relative.size())
  {
    const std::size_t end = std::min(relative.find('/', start), relative.size());
    const std::string_view part = relative.substr(start, end - start);
    start = end + 1;
    std::error_code ec;
    std::optional<fs::path> match;
    for (fs::directory_iterator it(here, ec), stop; !ec && it != stop; it.increment(ec))
    {
      const std::u8string name = it->path().filename().u8string();
      if (EqualNoCase(std::string(name.begin(), name.end()), part))
      {
        match = it->path().filename();
        break;
      }
    }
    if (!match)
      return std::nullopt;
    found /= *match;
    here /= *match;
  }
  std::error_code ec;
  if (!fs::is_regular_file(here, ec))
    return std::nullopt;
  return found;
}
}  // namespace detail

// The changes in one mod's patches.ini text, for the game `disc_id`. Lines
// that can't be used are named in *problems and left out.
inline std::vector<Patch> Parse(std::string_view text, std::string_view disc_id,
                                std::vector<Problem>* problems)
{
  using namespace detail;
  std::vector<Patch> patches;
  std::size_t line_number = 0;
  std::size_t start = 0;
  while (start <= text.size())
  {
    std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos)
      end = text.size();
    std::string line(text.substr(start, end - start));
    start = end + 1;
    ++line_number;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[')
      continue;
    const std::string where = "patches.ini line " + std::to_string(line_number);
    const auto fail = [&](std::string why) { problems->push_back({where, std::move(why)}); };
    const std::size_t equals = trimmed.find('=');
    if (equals == std::string::npos)
    {
      fail("it has no \"=\"");
      continue;
    }
    // "+=" and "*=": a change to the value the file has.
    Op op = Op::Set;
    std::size_t key_end = equals;
    if (equals > 0 && (trimmed[equals - 1] == '+' || trimmed[equals - 1] == '*'))
    {
      op = trimmed[equals - 1] == '+' ? Op::Add : Op::Multiply;
      key_end = equals - 1;
    }
    const std::string key = Trim(std::string_view(trimmed).substr(0, key_end));
    const std::string value = Trim(std::string_view(trimmed).substr(equals + 1));

    Patch patch;
    patch.where = where;
    patch.op = op;
    Type type = Type::U8;
    // The gears a gear.* key names: one, or all of them with "*".
    std::vector<std::uint32_t> gear_offsets;
    if (Lower(key).starts_with("gear."))
    {
      if (disc_id != SONIC_RIDERS)
      {
        fail("gear.* is for Sonic Riders (GXEE8P)");
        continue;
      }
      // gear.<name>.<field> or gear.<name>.level<1-3>.<field>
      std::vector<std::string> parts;
      std::size_t from = 5;
      while (from <= key.size())
      {
        const std::size_t dot = std::min(key.find('.', from), key.size());
        parts.push_back(key.substr(from, dot - from));
        from = dot + 1;
      }
      const auto gear = std::ranges::find_if(GEAR_NAMES, [&](std::string_view name) {
        return !parts.empty() && EqualNoCase(name, parts[0]);
      });
      const bool every_gear = !parts.empty() && parts[0] == "*";
      if (gear == GEAR_NAMES.end() && !every_gear)
      {
        fail("there is no gear called \"" + (parts.empty() ? std::string() : parts[0]) + "\"");
        continue;
      }
      std::uint32_t offset = 0;
      const GearField* field = nullptr;
      if (parts.size() == 2)
      {
        for (const GearField& candidate : GEAR_FIELDS)
          if (EqualNoCase(candidate.name, parts[1]))
            field = &candidate;
      }
      else if (parts.size() == 3 && Lower(parts[1]).starts_with("level") && parts[1].size() == 6 &&
               parts[1][5] >= '1' && parts[1][5] <= '3')
      {
        offset += GEAR_LEVELS + GEAR_LEVEL_SIZE * static_cast<std::uint32_t>(parts[1][5] - '1');
        for (const GearField& candidate : GEAR_LEVEL_FIELDS)
          if (EqualNoCase(candidate.name, parts[2]))
            field = &candidate;
      }
      if (!field)
      {
        fail("\"" + key + "\" isn't one of a gear's values (the README lists them)");
        continue;
      }
      patch.file = std::string(SONIC_RIDERS_MAIN_REL);
      patch.sha1 = std::string(SONIC_RIDERS_MAIN_REL_SHA1);
      type = field->type;
      for (std::size_t g = 0; g < GEAR_NAMES.size(); ++g)
        if (every_gear || static_cast<std::ptrdiff_t>(g) == gear - GEAR_NAMES.begin())
          gear_offsets.push_back(GEARS + GEAR_SIZE * static_cast<std::uint32_t>(g) + offset +
                                 field->offset);
    }
    else
    {
      // <file>@<offset>:<type>
      const std::size_t at = key.rfind('@');
      const std::size_t colon = key.rfind(':');
      if (at == std::string::npos || colon == std::string::npos || colon < at)
      {
        fail("\"" + key + "\" is neither gear.<name>.<value> nor <file>@<offset>:<type>");
        continue;
      }
      const auto file = CleanFilePath(std::string_view(key).substr(0, at));
      const auto offset = ParseUnsigned(std::string_view(key).substr(at + 1, colon - at - 1));
      const auto parsed_type = TypeNamed(Trim(std::string_view(key).substr(colon + 1)));
      if (!file)
      {
        fail("the file must be a path under files/, such as _Main.rel");
        continue;
      }
      if (!offset || *offset > 0xFFFFFFFFu)
      {
        fail("the place in the file must be a number, such as 0x1EA2C0");
        continue;
      }
      if (!parsed_type)
      {
        fail("the type must be u8, u16, u32, s8, s16, s32, f32 or bytes");
        continue;
      }
      patch.file = *file;
      gear_offsets.push_back(static_cast<std::uint32_t>(*offset));
      type = *parsed_type;
    }
    if (value.empty())
    {
      fail("it has no value");
      continue;
    }
    if (op == Op::Set)
    {
      std::string why;
      patch.bytes = Encode(type, value, &why);
      if (patch.bytes.empty())
      {
        fail(why);
        continue;
      }
    }
    else
    {
      if (type == Type::Bytes)
      {
        fail("\"+=\" and \"*=\" need a number type, not bytes");
        continue;
      }
      // km/h is for adding to a speed; a multiplier is just a number.
      const auto operand = ParseFloat(value);
      if (!operand || (op == Op::Multiply && Lower(value).ends_with("km/h")))
      {
        fail("the value isn't a number");
        continue;
      }
      patch.operand = *operand;
      patch.type = static_cast<int>(type);
      patch.bytes.assign(TypeSize(type), 0);
    }
    for (const std::uint32_t at : gear_offsets)
    {
      Patch one = patch;
      one.offset = at;
      patches.push_back(std::move(one));
    }
  }
  return patches;
}

// A change of `patch` (Add or Multiply) worked out on the bytes the file has:
// the new bytes, or nothing, with why, when the result doesn't fit the type.
inline std::vector<std::uint8_t> Combine(const Patch& patch, const std::uint8_t* current,
                                         std::string* why)
{
  using namespace detail;
  const Type type = static_cast<Type>(patch.type);
  const std::size_t size = TypeSize(type);
  std::uint64_t raw = 0;
  for (std::size_t i = 0; i < size; ++i)
    raw = raw << 8 | current[i];
  const auto apply = [&](double value) {
    return patch.op == Op::Add ? value + patch.operand : value * patch.operand;
  };
  std::vector<std::uint8_t> out;
  if (type == Type::F32)
  {
    const float result =
        static_cast<float>(apply(std::bit_cast<float>(static_cast<std::uint32_t>(raw))));
    PutBigEndian(&out, std::bit_cast<std::uint32_t>(result), 4);
    return out;
  }
  const bool is_signed = type == Type::S8 || type == Type::S16 || type == Type::S32;
  double value = static_cast<double>(raw);
  if (is_signed && (raw >> (8 * size - 1)))
    value -= static_cast<double>(1ull << (8 * size));
  const double result = std::round(apply(value));
  const double low = is_signed ? -static_cast<double>(1ull << (8 * size - 1)) : 0.0;
  const double high = is_signed ? static_cast<double>((1ull << (8 * size - 1)) - 1)
                                : static_cast<double>((1ull << (8 * size)) - 1);
  if (result < low || result > high)
  {
    *why = "the result doesn't fit (" + std::to_string(static_cast<long long>(result)) + ")";
    return {};
  }
  PutBigEndian(&out, static_cast<std::uint64_t>(static_cast<std::int64_t>(result)), static_cast<int>(size));
  return out;
}

// One mod's changes, in the order the mods apply.
struct ModPatches
{
  std::string id;                 // the mod's folder name, for messages
  std::filesystem::path folder;   // the mod's folder (its files/ may hold the base file)
  std::vector<Patch> patches;
};

struct Built
{
  std::vector<std::string> files;  // written under the output folder, under files/'s paths
  std::size_t applied = 0;
  std::vector<std::size_t> applied_by_mod;  // as many as mods, in their order
  std::vector<Problem> problems;   // "<mod id>: patches.ini line N", why
};

// Writes the patched copy of each file the mods change into out/<file>.
// A file's starting point is the last mod's files/<file> that has it (unless
// it is a module's code under StaticRecomp, as with file mods), else the
// game's own game_files/<file>. Changes apply in order: a later mod's to the
// same place win.
inline Built BuildPatchedFiles(const std::vector<ModPatches>& mods,
                               const std::filesystem::path& game_files,
                               const std::filesystem::path& out, bool static_recomp)
{
  namespace fs = std::filesystem;
  using namespace detail;
  Built built;
  built.applied_by_mod.assign(mods.size(), 0);
  std::error_code ec;
  fs::remove_all(out, ec);

  // The files, in the order they are first named.
  std::vector<std::string> files;
  for (const ModPatches& mod : mods)
    for (const Patch& patch : mod.patches)
      if (std::ranges::find_if(files, [&](const std::string& f) { return EqualNoCase(f, patch.file); }) ==
          files.end())
        files.push_back(patch.file);

  for (const std::string& file : files)
  {
    const bool code_file = EndsWithNoCase(file, ".rel") || EndsWithNoCase(file, ".dol");
    const auto problem = [&](const ModPatches& mod, const Patch& patch, std::string why) {
      built.problems.push_back({mod.id + ": " + patch.where, std::move(why)});
    };
    const auto all_fail = [&](const std::string& why) {
      for (const ModPatches& mod : mods)
        for (const Patch& patch : mod.patches)
          if (EqualNoCase(patch.file, file))
            problem(mod, patch, why);
    };
    // A file inside an archive: "<archive>.cvm/<file>".
    const auto inside = cvm_mods::SplitInside(file);
    // The game's file (or archive), with its capitals; the copy goes at the
    // same path.
    const auto in_game = FindNoCase(game_files, inside ? inside->first : file);
    fs::path relative = in_game ? *in_game : fs::path(std::u8string(file.begin(), file.end()));
    if (inside)
      relative = (in_game ? *in_game : fs::path(std::u8string(inside->first.begin(), inside->first.end()))) /
                 fs::path(std::u8string(inside->second.begin(), inside->second.end()));
    std::optional<fs::path> base;
    if (in_game && !inside)
      base = game_files / *in_game;
    if (!(static_recomp && code_file))
      for (const ModPatches& mod : mods)
        if (const auto in_mod = FindNoCase(mod.folder / "files", file))
          base = mod.folder / "files" / *in_mod;
    if (base && fs::file_size(*base, ec) > MAX_FILE)
    {
      all_fail(file + " is too big for data patches (over 64 MB)");
      continue;
    }
    std::optional<std::vector<std::uint8_t>> data;
    if (base)
    {
      data = ReadFile(*base);
    }
    else if (inside && in_game)
    {
      std::string why;
      data = cvm_mods::ReadInner(game_files / *in_game, inside->second, MAX_FILE, &why);
      if (!data)
      {
        all_fail(inside->first + " " + why);
        continue;
      }
    }
    if (!data)
    {
      all_fail("the game has no file " + file);
      continue;
    }
    const std::string sha1 = Sha1(*data);
    const auto code = static_recomp && EndsWithNoCase(file, ".rel") ? RelCodeSections(*data)
                                                                    : decltype(RelCodeSections(*data)){};
    std::size_t applied_here = 0;
    std::vector<std::size_t> applied_by_mod(mods.size(), 0);
    for (std::size_t m = 0; m < mods.size(); ++m)
      for (const Patch& patch : mods[m].patches)
      {
        const ModPatches& mod = mods[m];
        if (!EqualNoCase(patch.file, file))
          continue;
        if (!patch.sha1.empty() && patch.sha1 != sha1)
        {
          problem(mod, patch, file + " isn't the one these values were found in");
          continue;
        }
        const std::uint64_t end = static_cast<std::uint64_t>(patch.offset) + patch.bytes.size();
        if (end > data->size())
        {
          problem(mod, patch, "it is past the end of " + file);
          continue;
        }
        const bool touches_code = std::ranges::any_of(code, [&](const auto& section) {
          return patch.offset < section.second && end > section.first;
        });
        if (touches_code)
        {
          problem(mod, patch, "it changes code, left out with StaticRecomp");
          continue;
        }
        if (patch.op == Op::Set)
        {
          std::ranges::copy(patch.bytes, data->begin() + patch.offset);
        }
        else
        {
          std::string why;
          const std::vector<std::uint8_t> bytes = Combine(patch, data->data() + patch.offset, &why);
          if (bytes.empty())
          {
            problem(mod, patch, why);
            continue;
          }
          std::ranges::copy(bytes, data->begin() + patch.offset);
        }
        ++applied_here;
        ++applied_by_mod[m];
      }
    if (applied_here == 0)
      continue;
    const fs::path target = out / relative;
    fs::create_directories(target.parent_path(), ec);
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data->data()),
                 static_cast<std::streamsize>(data->size()));
    const std::u8string written = relative.generic_u8string();
    if (!stream)
    {
      const std::u8string where = target.u8string();
      built.problems.push_back({file, "can't write " + std::string(where.begin(), where.end())});
      continue;
    }
    built.files.emplace_back(written.begin(), written.end());
    built.applied += applied_here;
    for (std::size_t m = 0; m < mods.size(); ++m)
      built.applied_by_mod[m] += applied_by_mod[m];
  }
  return built;
}
}  // namespace moderngekko::data_patches
