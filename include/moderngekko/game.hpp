#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace moderngekko
{
enum class GamePlatform
{
  GameCube,
  Wii,
};

struct GameMetadata
{
  std::filesystem::path root;
  std::filesystem::path main_dol;
  std::filesystem::path main_rel;
  std::string game_name;
  std::string disc_id;
  GamePlatform platform = GamePlatform::GameCube;
  std::uint32_t entry_point = 0;
  std::string dol_sha256;
  std::string rel_sha256;
  std::string assets_sha256;
};

struct GameInspectResult
{
  std::optional<GameMetadata> metadata;
  std::string error;

  explicit operator bool() const { return metadata.has_value(); }
};

struct GameInspectOptions
{
  // Hashing every file under files/ dominates an inspection: tens of seconds
  // for a multi-GB Wii game. Callers that never compare assets_sha256 can turn
  // it off; assets_sha256 is then left empty. The default keeps the full check.
  bool hash_assets = true;
};

GameInspectResult InspectGame(const std::filesystem::path& root,
                              GameInspectOptions options = {});
std::optional<std::string> HashFileSha256(const std::filesystem::path& path);
std::optional<std::string> HashDirectorySha256(const std::filesystem::path& root);
}  // namespace moderngekko

namespace ModernGekko = moderngekko;
