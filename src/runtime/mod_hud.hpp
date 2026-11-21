#pragma once

// The mods' charge meters: a mod folder with a hud.ini (its format in
// mod_hud_core.hpp) gets a meter drawn over the game that fills while the
// mod's code charges something, as its words in the game's memory say. It is
// drawn on Dolphin's on-screen UI layer, before each frame is shown, as the
// 4:3 border is (game_border.hpp): no change to the game's picture or to the
// video backends.

#include <filesystem>
#include <memory>
#include <vector>

namespace moderngekko::detail
{
class ModHud
{
public:
  // Reads each mod's hud.ini; nullptr when no mod has one that works (the
  // reasons on stderr).
  static std::unique_ptr<ModHud> Create(const std::vector<std::filesystem::path>& mods);
  ~ModHud();
  ModHud(const ModHud&) = delete;
  ModHud& operator=(const ModHud&) = delete;

  // On the video thread, before a frame is presented (VideoEvents'
  // before_present_event): reads the mods' words and adds the meters to that
  // frame's on-screen UI.
  void AddToFrame();

  struct State;

private:
  explicit ModHud(std::unique_ptr<State> state);
  std::unique_ptr<State> m_state;
};
}  // namespace moderngekko::detail
