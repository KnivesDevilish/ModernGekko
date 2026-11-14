#pragma once

// Texture packs in file mods (tools/file_mods.hpp). A mod's textures/ folder
// holds custom textures: PNG or DDS files named tex1_... after the texture
// they replace, as Dolphin dumps and loads them, in any subfolders. They only
// change what is drawn, never the game's files.
//
// The graphics part reads custom textures from one folder per game, so the
// runtime gathers the textures of the mods turned on for the game into
// Cache/textures/<disc ID>/ before it starts: hard links (no copies, no extra
// space), or copies where a link can't be made (another drive, say). When two
// mods have a texture with the same name, the later one's is used, as with
// files. <disc ID>.list, beside the folder, says what is in it, so the next
// start leaves it as it is when nothing changed.
//
// Apart from the standard library, so tests/texture_mods_test.cpp can check it.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace moderngekko::texture_mods
{
namespace detail
{
inline std::string Utf8(const std::filesystem::path& path)
{
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

inline std::filesystem::path FromUtf8(std::string_view text)
{
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// Six letters or digits: a disc ID, never something like "..".
inline bool IsDiscId(std::string_view id)
{
  return id.size() == 6 && std::ranges::all_of(id, [](char c) {
           return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
         });
}

inline bool IsTexture(const std::filesystem::path& path)
{
  std::string extension = Utf8(path.extension());
  for (char& c : extension)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return extension == ".png" || extension == ".dds";
}

inline std::string ReadAll(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
}  // namespace detail

// Whether the mod in `folder` has a textures folder.
inline bool HasTextures(const std::filesystem::path& folder)
{
  std::error_code ec;
  return std::filesystem::is_directory(folder / "textures", ec);
}

struct TextureView
{
  std::size_t textures = 0;   // in the folder (each name once)
  std::size_t mods = 0;       // mods with a textures folder
  std::size_t copied = 0;     // copies, where a link couldn't be made
  bool reused = false;        // nothing changed since last time: left as it was
  std::string error;          // empty when the folder is ready
};

// Gathers the textures of `mods` (folders, in the order they apply) into
// root/<disc_id>/. The graphics part then reads root as its textures folder.
inline TextureView BuildTextureView(const std::vector<std::filesystem::path>& mods,
                                    const std::filesystem::path& root, std::string_view disc_id)
{
  namespace fs = std::filesystem;
  TextureView view;
  if (!detail::IsDiscId(disc_id))
  {
    view.error = "the game has no disc ID";
    return view;
  }

  // Each texture's name, and where it comes from: the last mod that has it.
  struct Source
  {
    fs::path path;
    std::uintmax_t size = 0;
    long long time = 0;
  };
  std::map<std::string, Source> textures;
  for (const fs::path& mod : mods)
  {
    if (!HasTextures(mod))
      continue;
    ++view.mods;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(mod / "textures",
                                             fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec))
    {
      std::error_code entry_ec;
      if (!it->is_regular_file(entry_ec) || !detail::IsTexture(it->path()))
        continue;
      Source source;
      source.path = it->path();
      source.size = it->file_size(entry_ec);
      source.time = static_cast<long long>(
          it->last_write_time(entry_ec).time_since_epoch().count());
      textures[detail::Utf8(it->path().filename())] = std::move(source);
    }
  }
  view.textures = textures.size();

  std::ostringstream listing;
  for (const auto& [name, source] : textures)
    listing << name << '\t' << detail::Utf8(source.path) << '\t' << source.size << '\t'
            << source.time << '\n';

  const fs::path folder = root / fs::path(std::string(disc_id));
  const fs::path list = root / fs::path(std::string(disc_id) + ".list");
  std::error_code ec;
  if (fs::is_directory(folder, ec) && fs::exists(list, ec) &&
      detail::ReadAll(list) == listing.str())
  {
    view.reused = true;
    return view;
  }

  // Made again from nothing: the list goes first, so a folder left half made
  // is never taken as ready.
  fs::remove(list, ec);
  fs::remove_all(folder, ec);
  if (ec)
  {
    view.error = "can't empty " + detail::Utf8(folder) + " (" + ec.message() + ")";
    return view;
  }
  fs::create_directories(folder, ec);
  if (ec)
  {
    view.error = "can't make " + detail::Utf8(folder) + " (" + ec.message() + ")";
    return view;
  }
  for (const auto& [name, source] : textures)
  {
    const fs::path target = folder / detail::FromUtf8(name);
    fs::create_hard_link(source.path, target, ec);
    if (!ec)
      continue;
    ec.clear();
    fs::copy_file(source.path, target, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
      view.error = "can't copy " + detail::Utf8(source.path) + " (" + ec.message() + ")";
      return view;
    }
    ++view.copied;
  }
  {
    std::ofstream file(list, std::ios::binary | std::ios::trunc);
    file << listing.str();
    if (!file)
    {
      view.error = "can't write " + detail::Utf8(list);
      return view;
    }
  }
  return view;
}

// No texture mods for this game: the folder and its list go, so copies made
// for it don't take space.
inline void ClearTextureView(const std::filesystem::path& root, std::string_view disc_id)
{
  namespace fs = std::filesystem;
  if (!detail::IsDiscId(disc_id))
    return;
  std::error_code ec;
  fs::remove(root / fs::path(std::string(disc_id) + ".list"), ec);
  fs::remove_all(root / fs::path(std::string(disc_id)), ec);
}
}  // namespace moderngekko::texture_mods
