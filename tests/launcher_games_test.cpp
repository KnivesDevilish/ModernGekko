// The list of games a build plays (tools/launcher_games.hpp): how
// MODERNGEKKO_GAMES is read, and which discs a build takes.
#include "launcher_games.hpp"

#include <cstdio>

using moderngekko::frontend::AcceptsGame;
using moderngekko::frontend::FindKnownGame;
using moderngekko::frontend::KnownGame;
using moderngekko::frontend::KnownGameIds;
using moderngekko::frontend::ParseGameList;

int main()
{
  // Two games, in order, with their titles; spaces around the parts go.
  const std::vector<KnownGame> two =
      ParseGameList("RS9E8P=Sonic Riders: Zero Gravity Source| GXEE8P = Sonic Riders: Source ");
  if (two.size() != 2 || two[0].disc_id != "RS9E8P" ||
      two[0].title != "Sonic Riders: Zero Gravity Source" || two[1].disc_id != "GXEE8P" ||
      two[1].title != "Sonic Riders: Source")
    return 1;
  // A game without a title shows its ID.
  const std::vector<KnownGame> untitled = ParseGameList("GXEE8P|RS9E8P=");
  if (untitled.size() != 2 || untitled[0].title != "GXEE8P" || untitled[1].title != "RS9E8P")
    return 2;
  // IDs that aren't six letters or digits, empty entries and a repeated ID
  // are left out.
  const std::vector<KnownGame> messy =
      ParseGameList("|RS9E8=Short|RS9E8P7=Long|RS-E8P=Dash||GXEE8P=One|GXEE8P=Again|");
  if (messy.size() != 1 || messy[0].disc_id != "GXEE8P" || messy[0].title != "One")
    return 3;
  if (!ParseGameList("").empty())
    return 4;

  // Which discs a build takes: its games, or any without a list.
  if (!AcceptsGame("GXEE8P", two) || !AcceptsGame("RS9E8P", two) ||
      AcceptsGame("GALE01", two) || !AcceptsGame("GALE01", {}))
    return 5;
  const KnownGame* found = FindKnownGame("GXEE8P", two);
  if (!found || found->title != "Sonic Riders: Source" || FindKnownGame("GALE01", two))
    return 6;
  if (KnownGameIds(two) != "RS9E8P / GXEE8P" || KnownGameIds({}) != "")
    return 7;

  std::puts("launcher games: ok");
  return 0;
}
