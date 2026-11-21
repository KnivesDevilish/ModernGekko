#pragma once

// The text parts of the memory watch (memory_watch.hpp), apart from Dolphin
// so tests/memory_watch_test.cpp can check them: the list of places in
// MODERNGEKKO_MEMORY_WATCH, and the lines Dolphin logs when a memory
// breakpoint is hit.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::detail
{
// One place to watch: `address`, or (through_pointer) the address kept at
// `address`, plus `offset`.
struct WatchPlace
{
  bool through_pointer = false;
  std::uint32_t address = 0;
  std::uint32_t offset = 0;
  std::string name;  // the =name, or the text that names it
  std::string text;  // as it was written: [0x804970CC]+0x40C
};

// MEM1 or MEM2, as the game sees them (cached).
inline bool IsGameRamAddress(std::uint32_t address)
{
  return (address >= 0x80000000u && address < 0x81800000u) ||
         (address >= 0x90000000u && address < 0x94000000u);
}

namespace watch_text
{
inline std::string Trim(std::string_view text)
{
  std::size_t start = 0, end = text.size();
  while (start < end && std::isspace(static_cast<unsigned char>(text[start])))
    ++start;
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
    --end;
  return std::string(text.substr(start, end - start));
}

// A number in hex, with or without 0x.
inline std::optional<std::uint32_t> Hex(std::string_view text)
{
  std::string t = Trim(text);
  if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
    t = t.substr(2);
  if (t.empty() || t.size() > 8)
    return std::nullopt;
  std::uint32_t value = 0;
  for (const char c : t)
  {
    if (!std::isxdigit(static_cast<unsigned char>(c)))
      return std::nullopt;
    value = value << 4 | static_cast<std::uint32_t>(
                             std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                                                                         : std::tolower(c) - 'a' + 10);
  }
  return value;
}
}  // namespace watch_text

// The places in `text` (separated by commas). Ones that can't be read are
// named in *problems and left out.
inline std::vector<WatchPlace> ParseWatchList(std::string_view text,
                                              std::vector<std::string>* problems)
{
  using namespace watch_text;
  std::vector<WatchPlace> places;
  std::size_t start = 0;
  while (start <= text.size())
  {
    const std::size_t end = std::min(text.find(',', start), text.size());
    std::string entry = Trim(text.substr(start, end - start));
    start = end + 1;
    if (entry.empty())
      continue;
    WatchPlace place;
    std::string where = entry;
    if (const std::size_t equals = entry.find('='); equals != std::string::npos)
    {
      place.name = Trim(std::string_view(entry).substr(equals + 1));
      where = Trim(std::string_view(entry).substr(0, equals));
    }
    place.text = where;
    if (!where.empty() && where[0] == '[')
    {
      const std::size_t close = where.find(']');
      const std::size_t plus = where.find('+', close == std::string::npos ? 0 : close);
      const auto pointer = close == std::string::npos ? std::nullopt : Hex(where.substr(1, close - 1));
      const auto offset = plus == std::string::npos ? std::optional<std::uint32_t>(0)
                                                     : Hex(where.substr(plus + 1));
      const std::string between =
          close == std::string::npos ? std::string()
                                     : Trim(where.substr(close + 1, (plus == std::string::npos
                                                                         ? where.size()
                                                                         : plus) - close - 1));
      if (!pointer || !offset || !between.empty() || !IsGameRamAddress(*pointer))
      {
        problems->push_back(entry + ": write [pointer]+offset, such as [0x804970CC]+0x40C");
        continue;
      }
      place.through_pointer = true;
      place.address = *pointer;
      place.offset = *offset;
    }
    else
    {
      const auto address = Hex(where);
      if (!address || !IsGameRamAddress(*address))
      {
        problems->push_back(entry + ": not an address in the game's memory, such as 0x80B9E00C");
        continue;
      }
      place.address = *address;
    }
    if (place.name.empty())
      place.name = place.text;
    places.push_back(std::move(place));
  }
  return places;
}

// What Dolphin logs when a memory breakpoint that logs is hit:
//   ... MBP 80a12345 (name) Read32 3f3ed098 at 81548c0c (name)
struct BreakpointHit
{
  std::uint32_t pc = 0;
  bool write = false;
  std::uint32_t address = 0;
};

inline std::optional<BreakpointHit> ParseBreakpointLog(std::string_view line)
{
  using namespace watch_text;
  const std::size_t mbp = line.find("MBP ");
  if (mbp == std::string_view::npos || mbp + 12 > line.size())
    return std::nullopt;
  const auto pc = Hex(line.substr(mbp + 4, 8));
  if (!pc)
    return std::nullopt;
  BreakpointHit hit;
  hit.pc = *pc;
  const std::size_t write = line.find(") Write", mbp);
  const std::size_t read = line.find(") Read", mbp);
  if (write == std::string_view::npos && read == std::string_view::npos)
    return std::nullopt;
  hit.write = write != std::string_view::npos && (read == std::string_view::npos || write < read);
  // The last " at " followed by an address.
  std::size_t at = line.size();
  while ((at = line.rfind(" at ", at == 0 ? 0 : at - 1)) != std::string_view::npos)
  {
    if (at + 12 <= line.size())
    {
      if (const auto address = Hex(line.substr(at + 4, 8)))
      {
        hit.address = *address;
        return hit;
      }
    }
    if (at == 0)
      break;
  }
  return std::nullopt;
}
}  // namespace moderngekko::detail
