// The memory watch's text parts (src/runtime/memory_watch_text.hpp): the
// places in MODERNGEKKO_MEMORY_WATCH, and Dolphin's memory breakpoint lines.
#include "memory_watch_text.hpp"

#include <string>
#include <vector>

namespace mg = moderngekko::detail;

int main()
{
  std::vector<std::string> problems;
  const auto places = mg::ParseWatchList(
      " [0x804970CC]+0x40C=giro1 ,0x80B9E00C=tabla, [804970CC] + 3fc ,[0x804970CC],"
      "0x12345678,[0x804970CC]+zz,[0x10]+0x4,0x80B9E00C+4,,", &problems);
  if (places.size() != 4 || problems.size() != 4)
    return 1;
  if (!places[0].through_pointer || places[0].address != 0x804970CC || places[0].offset != 0x40C ||
      places[0].name != "giro1" || places[0].text != "[0x804970CC]+0x40C")
    return 2;
  if (places[1].through_pointer || places[1].address != 0x80B9E00C || places[1].name != "tabla")
    return 3;
  if (!places[2].through_pointer || places[2].offset != 0x3FC || places[2].name != "[804970CC] + 3fc")
    return 4;
  if (!places[3].through_pointer || places[3].offset != 0)
    return 5;
  // Not in the game's memory, a bad offset, a pointer that isn't, "+" on a plain address.
  if (problems[0].find("0x12345678") != 0 || problems[1].find("zz") == std::string::npos ||
      problems[2].find("[0x10]") != 0 || problems[3].find("0x80B9E00C+4") != 0)
    return 6;
  if (!mg::IsGameRamAddress(0x80000000) || mg::IsGameRamAddress(0x81800000) ||
      !mg::IsGameRamAddress(0x93FFFFFC) || mg::IsGameRamAddress(0x7FFFFFFF))
    return 7;

  // Dolphin's line (Core/PowerPC/BreakPoints.cpp), with its log prefix.
  auto hit = mg::ParseBreakpointLog(
      "12:34:567 Core/PowerPC/BreakPoints.cpp:435 N[MEMMAP]: MBP 80a12345 () Read32 3f3ed098 at "
      "81548c1c ()\n");
  if (!hit || hit->pc != 0x80A12345 || hit->write || hit->address != 0x81548C1C)
    return 8;
  hit = mg::ParseBreakpointLog(
      "N[MEMMAP]: MBP 800a6db4 (zz_800a6734_ at x) Write32 0 at 81548c20 (some name at 12)");
  if (!hit || hit->pc != 0x800A6DB4 || !hit->write || hit->address != 0x81548C20)
    return 9;
  if (mg::ParseBreakpointLog("N[CORE]: something else at 81548c20") ||
      mg::ParseBreakpointLog("MBP zz") || mg::ParseBreakpointLog("MBP 80001000 () Peek at 80000000"))
    return 10;
  return 0;
}
