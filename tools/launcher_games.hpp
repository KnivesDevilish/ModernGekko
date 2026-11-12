// The games a build plays, by disc ID, with the name each one shows (in the
// game window's title, the launcher's game choice and Game Files):
//  - MODERNGEKKO_GAMES, "ID=Title|ID=Title" (CMake's MODERNGEKKO_GAMES list,
//    ID=Title;ID=Title), for a build that plays more than one game; the
//    first is the build's main game;
//  - else the one game MODERNGEKKO_REQUIRED_DISC_ID names, titled
//    MODERNGEKKO_FRONTEND_NAME when the target has it;
//  - else none: a build for any GameCube or Wii game.
// Each game is extracted to its own folder (games/<disc ID> in the user
// directory), so they sit side by side.
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend
{
struct KnownGame
{
  std::string disc_id;
  std::string title;
};

// "ID=Title|ID=Title". Each ID is six letters or digits; a game without a
// title takes its ID as one. Entries that aren't games are left out, and so
// is a second entry with the same ID.
inline std::vector<KnownGame> ParseGameList(std::string_view text)
{
  const auto trim = [](std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
      value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
      value.remove_suffix(1);
    return value;
  };
  std::vector<KnownGame> games;
  while (!text.empty())
  {
    const std::size_t bar = text.find('|');
    const std::string_view entry = trim(text.substr(0, bar));
    text = bar == std::string_view::npos ? std::string_view{} : text.substr(bar + 1);
    const std::size_t equals = entry.find('=');
    const std::string_view id = trim(entry.substr(0, equals));
    const std::string_view title =
        equals == std::string_view::npos ? id : trim(entry.substr(equals + 1));
    const bool valid_id = id.size() == 6 && std::ranges::all_of(id, [](char c) {
                            return std::isalnum(static_cast<unsigned char>(c)) != 0;
                          });
    if (!valid_id || std::ranges::any_of(games, [&](const KnownGame& game) {
          return game.disc_id == id;
        }))
      continue;
    games.push_back({std::string(id), std::string(title.empty() ? id : title)});
  }
  return games;
}

inline const std::vector<KnownGame>& KnownGames()
{
  static const std::vector<KnownGame> games = [] {
#if defined(MODERNGEKKO_GAMES)
    return ParseGameList(MODERNGEKKO_GAMES);
#elif defined(MODERNGEKKO_REQUIRED_DISC_ID) && defined(MODERNGEKKO_FRONTEND_NAME)
    return std::vector<KnownGame>{{MODERNGEKKO_REQUIRED_DISC_ID, MODERNGEKKO_FRONTEND_NAME}};
#elif defined(MODERNGEKKO_REQUIRED_DISC_ID)
    return std::vector<KnownGame>{{MODERNGEKKO_REQUIRED_DISC_ID, MODERNGEKKO_REQUIRED_DISC_ID}};
#else
    return std::vector<KnownGame>{};
#endif
  }();
  return games;
}

// More than one game: the launcher asks which one PLAY starts.
inline bool PlaysSeveralGames() { return KnownGames().size() > 1; }

inline const KnownGame* FindKnownGame(std::string_view disc_id,
                                      const std::vector<KnownGame>& games = KnownGames())
{
  const auto found = std::ranges::find(games, disc_id, &KnownGame::disc_id);
  return found == games.end() ? nullptr : &*found;
}

// Whether this build plays the game: any game when it names none.
inline bool AcceptsGame(std::string_view disc_id,
                        const std::vector<KnownGame>& games = KnownGames())
{
  return games.empty() || FindKnownGame(disc_id, games) != nullptr;
}

// The IDs for a message, such as "RS9E8P / GXEE8P".
inline std::string KnownGameIds(const std::vector<KnownGame>& games = KnownGames())
{
  std::string ids;
  for (const KnownGame& game : games)
    ids += (ids.empty() ? "" : " / ") + game.disc_id;
  return ids;
}
}  // namespace moderngekko::frontend
