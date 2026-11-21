#pragma once

// Memory watch, for finding which of a game's instructions read or write a
// value (to make mods): while the environment variable
// MODERNGEKKO_MEMORY_WATCH is set, the runtime puts Dolphin memory
// breakpoints that only log on the places it names, and says in the log
// which instructions (their addresses) touch each one.
//
//   MODERNGEKKO_MEMORY_WATCH='[0x804970CC]+0x40C=turn,0x80B9E00C=table'
//
// Places are separated by commas: an address, or [pointer]+offset (the
// address kept at `pointer`, plus `offset`, followed as it changes, say from
// one race to the next), each with an optional =name for the log.
//
// The log says each instruction the first time it touches a place:
//   memory watch: turn ([0x804970CC]+0x40C) read by 0x80A12345
// and, when the game closes, how many times each one did.
//
// Memory breakpoints work with JIT64 and the interpreter, not with
// StaticRecomp's recompiled code. They slow the game down while they are on.

#include <memory>

namespace moderngekko::detail
{
class MemoryWatch
{
public:
  // Starts watching, when the environment variable asks for it.
  MemoryWatch();
  // Stops, and logs how many times each instruction touched each place.
  ~MemoryWatch();
  MemoryWatch(const MemoryWatch&) = delete;
  MemoryWatch& operator=(const MemoryWatch&) = delete;

  struct State;

private:
  std::shared_ptr<State> m_state;
};
}  // namespace moderngekko::detail
