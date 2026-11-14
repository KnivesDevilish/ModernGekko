// Texture packs in mods (src/runtime/texture_mods.hpp): the textures of the
// mods turned on, gathered into one folder per game.
#include "texture_mods.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using moderngekko::texture_mods::BuildTextureView;
using moderngekko::texture_mods::ClearTextureView;
using moderngekko::texture_mods::HasTextures;
using moderngekko::texture_mods::TextureView;

namespace
{
void Write(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string Read(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::set<std::string> Names(const fs::path& folder)
{
  std::set<std::string> names;
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    names.insert(it->path().filename().string());
  return names;
}
}  // namespace

int main()
{
  const fs::path base =
      fs::temp_directory_path() /
      ("moderngekko-texture-mods-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path mods = base / "Mods";
  const fs::path root = base / "Cache" / "textures";

  // Two packs and a mod with only files. Subfolders, capitals in the
  // extension, and files that aren't textures.
  Write(mods / "green" / "textures" / "menus" / "tex1_256x256_aaaa_9.png", "green menu");
  Write(mods / "green" / "textures" / "tex1_128x128_bbbb_14.PNG", "green logo");
  Write(mods / "green" / "textures" / "notes.txt", "not a texture");
  Write(mods / "blue" / "textures" / "tex1_128x128_bbbb_14.PNG", "blue logo");
  Write(mods / "blue" / "textures" / "tex1_64x64_cccc_5.dds", "blue dds");
  Write(mods / "voices" / "files" / "voice.adx", "voice");
  if (!HasTextures(mods / "green") || HasTextures(mods / "voices") || HasTextures(mods / "gone"))
    return 1;

  // The later mod wins; the files mod and the .txt are left out.
  const std::vector<fs::path> order = {mods / "voices", mods / "green", mods / "blue"};
  TextureView view = BuildTextureView(order, root, "RS9E8P");
  const fs::path folder = root / "RS9E8P";
  if (!view.error.empty() || view.textures != 3 || view.mods != 2 || view.reused ||
      Names(folder) != std::set<std::string>{"tex1_256x256_aaaa_9.png",
                                             "tex1_128x128_bbbb_14.PNG", "tex1_64x64_cccc_5.dds"} ||
      Read(folder / "tex1_128x128_bbbb_14.PNG") != "blue logo" ||
      Read(folder / "tex1_256x256_aaaa_9.png") != "green menu" || !fs::exists(root / "RS9E8P.list"))
    return 2;
  // Links, not copies (the test folder is on one drive).
  if (view.copied != 0 || fs::hard_link_count(folder / "tex1_64x64_cccc_5.dds") < 2)
    return 3;

  // Nothing changed: left as it was.
  view = BuildTextureView(order, root, "RS9E8P");
  if (!view.error.empty() || !view.reused || view.textures != 3)
    return 4;

  // Another order: the other logo. A texture added: there too.
  Write(mods / "green" / "textures" / "tex1_32x32_dddd_9.png", "green new");
  view = BuildTextureView({mods / "blue", mods / "green"}, root, "RS9E8P");
  if (!view.error.empty() || view.reused || view.textures != 4 ||
      Read(folder / "tex1_128x128_bbbb_14.PNG") != "green logo" ||
      Read(folder / "tex1_32x32_dddd_9.png") != "green new")
    return 5;

  // One pack off: its textures go.
  view = BuildTextureView({mods / "blue"}, root, "RS9E8P");
  if (!view.error.empty() || view.textures != 2 ||
      Names(folder) != std::set<std::string>{"tex1_128x128_bbbb_14.PNG", "tex1_64x64_cccc_5.dds"})
    return 6;

  // A half-made folder (no list) is made again, not taken as ready.
  fs::remove(root / "RS9E8P.list");
  Write(folder / "tex1_stale_0.png", "stale");
  view = BuildTextureView({mods / "blue"}, root, "RS9E8P");
  if (!view.error.empty() || view.reused || fs::exists(folder / "tex1_stale_0.png"))
    return 7;

  // Each game has its own folder; no disc ID, nothing is touched.
  view = BuildTextureView({mods / "green"}, root, "GXEE8P");
  if (!view.error.empty() || view.textures != 3 || !fs::is_directory(root / "GXEE8P") ||
      !fs::is_directory(folder))
    return 8;
  view = BuildTextureView({mods / "green"}, root, "..");
  if (view.error.empty() || !fs::is_directory(root / "GXEE8P"))
    return 9;

  // No texture mods: the folder and its list go; the other game's stay.
  ClearTextureView(root, "RS9E8P");
  ClearTextureView(root, "");
  if (fs::exists(folder) || fs::exists(root / "RS9E8P.list") || !fs::is_directory(root / "GXEE8P"))
    return 10;
  // The packs themselves are never touched.
  if (Read(mods / "blue" / "textures" / "tex1_128x128_bbbb_14.PNG") != "blue logo" ||
      Read(mods / "green" / "textures" / "notes.txt") != "not a texture")
    return 11;

  fs::remove_all(base);
  return 0;
}
