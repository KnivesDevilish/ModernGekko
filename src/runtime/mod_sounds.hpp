#pragma once

// The mods' sounds: a mod folder with a sounds.ini (mod_sounds_core.hpp says
// what goes in it) gets its WAV files mixed into the game's sound while the
// game runs, started and stopped by words its code writes in the game's
// memory. That's how a mod adds sounds the game doesn't have.
//
// They go through the mixer's channel for a Game Boy Advance on port 4,
// which a Wii game never uses. Only while the game runs: paused, nothing; and
// nothing more from the moment Dolphin starts stopping.

#include <filesystem>
#include <memory>
#include <vector>

namespace moderngekko::detail
{
class ModSounds
{
public:
  // Reads the sounds.ini of each mod folder (in order); nullptr when none
  // has one that works. Says in the log what it loaded and what it left out.
  static std::unique_ptr<ModSounds> Create(const std::vector<std::filesystem::path>& mods);
  ~ModSounds();
  ModSounds(const ModSounds&) = delete;
  ModSounds& operator=(const ModSounds&) = delete;

  struct State;

private:
  explicit ModSounds(std::shared_ptr<State> state);
  std::shared_ptr<State> m_state;
};
}  // namespace moderngekko::detail
