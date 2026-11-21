// File mods (tools/file_mods.hpp): which folders are mods, what their mod.ini
// says, and which ones apply to a game, in which order.
#include "file_mods.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using moderngekko::frontend::EnabledFileMods;
using moderngekko::frontend::FileMod;
using moderngekko::frontend::FileModProblem;
using moderngekko::frontend::ListFileMods;
using moderngekko::frontend::ReadDiscId;
using moderngekko::frontend::ReadFileMod;

namespace
{
void Write(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::vector<std::string> Ids(const std::vector<FileMod>& mods)
{
  std::vector<std::string> ids;
  for (const FileMod& mod : mods)
    ids.push_back(mod.id);
  return ids;
}
}  // namespace

int main()
{
  const fs::path base =
      fs::temp_directory_path() /
      ("moderngekko-file-mods-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path program = base / "program" / "Mods";
  const fs::path user = base / "user" / "Mods";

  // A full mod.ini; keys in any case, spaces, comments and a section.
  Write(user / "voices" / "mod.ini",
        "# Spanish voices\r\n[Mod]\r\n Name = Voces en espa\xC3\xB1ol \r\nGAME=rs9e8p\r\n"
        "version=1.2\r\nauthor=Someone\r\ndescription=Every voice.\r\nunknown=kept quiet\r\n");
  Write(user / "voices" / "files" / "Sound" / "voice.adx", "voice");
  const auto voices = ReadFileMod(user / "voices");
  if (!voices || voices->id != "voices" || voices->name != "Voces en espa\xC3\xB1ol" ||
      voices->game != "RS9E8P" || voices->version != "1.2" || voices->author != "Someone" ||
      voices->description != "Every voice.")
    return 1;

  // No name: the folder's. Another game.
  Write(user / "music" / "mod.ini", "game=GXEE8P\n");
  Write(user / "music" / "files" / "bgm.adx", "music");
  const auto music = ReadFileMod(user / "music");
  if (!music || music->name != "music" || music->game != "GXEE8P")
    return 2;

  // A texture pack: textures and no files.
  Write(user / "green" / "mod.ini", "name=Green menus\ngame=RS9E8P\n");
  Write(user / "green" / "textures" / "tex1_8x8_aaaa_9.png", "texture");
  const auto green = ReadFileMod(user / "green");
  if (!green || green->has_files || !green->has_textures || voices->has_textures ||
      !voices->has_files)
    return 8;

  // Data patches only.
  Write(user / "turbo" / "mod.ini", "game=GXEE8P\n");
  Write(user / "turbo" / "patches.ini", "_Main.rel@0x1000:f32 = 300 km/h\n");
  const auto turbo = ReadFileMod(user / "turbo");
  if (!turbo || !turbo->has_patches || turbo->has_files || turbo->has_textures ||
      voices->has_patches)
    return 9;

  // Not mods: no game, a game that isn't a disc ID, no files or textures folder.
  Write(user / "nogame" / "mod.ini", "name=No game\n");
  fs::create_directories(user / "nogame" / "files");
  Write(user / "badgame" / "mod.ini", "game=RS9E\n");
  fs::create_directories(user / "badgame" / "files");
  Write(user / "nofiles" / "mod.ini", "game=RS9E8P\n");
  const char* problem = "";
  if (ReadFileMod(user / "nogame", &problem) ||
      problem != moderngekko::frontend::FILE_MOD_NO_GAME || ReadFileMod(user / "badgame") ||
      ReadFileMod(user / "nofiles", &problem) ||
      problem != moderngekko::frontend::FILE_MOD_NO_FILES ||
      ReadFileMod(user / "missing", &problem) ||
      problem != moderngekko::frontend::FILE_MOD_NO_INI)
    return 3;

  // Left out of the list without a word: a folder with no mod.ini, a code
  // mod's .mgm folder, a file.
  fs::create_directories(user / "plain" / "files");
  Write(user / "code.mgm" / "mod.ini", "game=RS9E8P\n");
  fs::create_directories(user / "code.mgm" / "files");
  Write(user / "notes.txt", "notes");

  // The program's Mods: one of its own, and one the user's replaces.
  Write(program / "extra" / "mod.ini", "name=Extra\ngame=RS9E8P\n");
  fs::create_directories(program / "extra" / "files");
  Write(program / "voices" / "mod.ini", "name=Old voices\ngame=RS9E8P\n");
  fs::create_directories(program / "voices" / "files");

  std::vector<FileModProblem> problems;
  const auto mods = ListFileMods({program, user, base / "nowhere"}, &problems);
  // Sorted by name: Extra, Green menus, music, turbo, Voces en español.
  if (Ids(mods) != std::vector<std::string>{"extra", "green", "music", "turbo", "voices"} ||
      mods[4].folder != user / "voices" || problems.size() != 3)
    return 4;
  for (const FileModProblem& found : problems)
  {
    const bool known = (found.folder == "nogame" || found.folder == "badgame")
                           ? found.why == moderngekko::frontend::FILE_MOD_NO_GAME
                       : found.folder == "nofiles"
                           ? found.why == moderngekko::frontend::FILE_MOD_NO_FILES
                           : false;
    if (!known)
      return 7;
  }

  // Enabled ones for a game, in config.ini's order; unknown names, another
  // game's mods and repeats are skipped.
  const std::vector<std::string> enabled = {"voices", "gone", "music", "extra", "voices"};
  if (Ids(EnabledFileMods(mods, enabled, "RS9E8P")) !=
          std::vector<std::string>{"voices", "extra"} ||
      Ids(EnabledFileMods(mods, enabled, "GXEE8P")) != std::vector<std::string>{"music"} ||
      !EnabledFileMods(mods, {}, "RS9E8P").empty() ||
      !EnabledFileMods(mods, enabled, "").empty())
    return 5;

  // The disc ID from sys/boot.bin.
  Write(base / "game" / "sys" / "boot.bin", std::string("RS9E8P\x00\x01", 8) + std::string(32, '\0'));
  Write(base / "short" / "sys" / "boot.bin", "RS9");
  Write(base / "odd" / "sys" / "boot.bin", "rs9e8p and more");
  if (ReadDiscId(base / "game") != "RS9E8P" || !ReadDiscId(base / "short").empty() ||
      !ReadDiscId(base / "odd").empty() || !ReadDiscId(base / "nowhere").empty())
    return 6;

  fs::remove_all(base);
  return 0;
}
