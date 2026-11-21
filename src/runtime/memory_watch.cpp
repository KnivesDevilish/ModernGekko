// Memory watch (memory_watch.hpp): Dolphin memory breakpoints that only log,
// on the places MODERNGEKKO_MEMORY_WATCH names, and the instructions that
// hit them, from Dolphin's log, said in ours.
#include "memory_watch.hpp"

#include "memory_watch_text.hpp"

#include "Common/Logging/Log.h"
#include "Common/Logging/LogManager.h"
#include "Core/Core.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/BreakPoints.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace moderngekko::detail
{
struct MemoryWatch::State
{
  std::vector<WatchPlace> places;
  std::mutex mutex;
  // Where each place is now (0: nowhere yet).
  std::vector<std::uint32_t> where;
  // (place, written, instruction) -> times.
  std::map<std::tuple<std::size_t, bool, std::uint32_t>, std::uint64_t> hits;
  bool memmap_was_on = false;
  std::jthread thread;

  // On the CPU thread, as Dolphin logs a hit.
  void Hit(const BreakpointHit& hit)
  {
    std::lock_guard lock(mutex);
    for (std::size_t i = 0; i < places.size(); ++i)
    {
      if (where[i] == 0 || hit.address < where[i] || hit.address >= where[i] + 4)
        continue;
      auto& count = hits[{i, hit.write, hit.pc}];
      if (count++ == 0)
        std::fprintf(stderr, "memory watch: %s (%s) %s by 0x%08X\n", places[i].name.c_str(),
                     places[i].text.c_str(), hit.write ? "written" : "read", hit.pc);
    }
  }
};

namespace
{
// Dolphin's log, for the lines its memory breakpoints write.
class HitListener final : public Common::Log::LogListener
{
public:
  explicit HitListener(std::weak_ptr<MemoryWatch::State> state) : m_state(std::move(state)) {}
  void Log(Common::Log::LogLevel, const char* message) override
  {
    if (const auto hit = ParseBreakpointLog(message))
      if (const auto state = m_state.lock())
        state->Hit(*hit);
  }

private:
  std::weak_ptr<MemoryWatch::State> m_state;
};

// A breakpoint on the 4 bytes at `address` that logs reads and writes and
// doesn't stop the game.
TMemCheck MakeCheck(std::uint32_t address)
{
  TMemCheck check;
  check.start_address = address;
  check.end_address = address + 3;
  check.is_ranged = true;
  check.is_enabled = true;
  check.is_break_on_read = true;
  check.is_break_on_write = true;
  check.log_on_hit = true;
  check.break_on_hit = false;
  return check;
}

// A breakpoint that moves: from `from` to `to` (0: none).
struct Move
{
  std::size_t place;
  std::uint32_t from;
  std::uint32_t to;
};

// Changes the breakpoints all in one pause of the game. Each Add or Remove
// on its own pauses it twice, one right after the other, and pausing again
// just after a resume, while there are memory breakpoints, can leave this
// thread waiting forever: the CPU thread, after its first instruction,
// writes "running" over the pause. With the game paused here, this thread
// counts as the CPU thread, so Add, Remove and the update don't pause again.
void MoveChecks(Core::System& system, const std::vector<Move>& moves)
{
  const Core::CPUThreadGuard guard(system);
  MemChecks& checks = system.GetPowerPC().GetMemChecks();
  // Updates the JIT once, at the end, still inside the pause.
  DelayedMemCheckUpdate update(&checks);
  for (const Move& move : moves)
    if (move.from != 0)
      update |= checks.Remove(move.from);
  for (const Move& move : moves)
    if (move.to != 0)
      update |= checks.Add(MakeCheck(move.to));
}

// Puts the breakpoints where the places are now: a pointer's place moves
// when the pointer changes.
void Follow(MemoryWatch::State& state)
{
  Core::System& system = Core::System::GetInstance();
  if (Core::GetState(system) != Core::State::Running)
    return;
  Memory::MemoryManager& memory = system.GetMemory();
  std::vector<Move> moves;
  for (std::size_t i = 0; i < state.places.size(); ++i)
  {
    const WatchPlace& place = state.places[i];
    std::uint32_t address = place.address;
    if (place.through_pointer)
    {
      const std::uint32_t pointer = memory.Read_U32(place.address);
      address = IsGameRamAddress(pointer) ? pointer + place.offset : 0;
    }
    if (address != 0 && !IsGameRamAddress(address))
      address = 0;
    std::lock_guard lock(state.mutex);
    if (state.where[i] == address)
      continue;
    moves.push_back({i, state.where[i], address});
    state.where[i] = address;
  }
  if (moves.empty())
    return;
  MoveChecks(system, moves);
  for (const Move& move : moves)
    if (move.to != 0)
      std::fprintf(stderr, "memory watch: %s (%s) is at 0x%08X\n",
                   state.places[move.place].name.c_str(), state.places[move.place].text.c_str(),
                   move.to);
}
}  // namespace

MemoryWatch::MemoryWatch()
{
  const char* text = std::getenv("MODERNGEKKO_MEMORY_WATCH");
  if (!text || !*text)
    return;
  auto* log = Common::Log::LogManager::GetInstance();
  if (!log)
  {
    std::fprintf(stderr, "memory watch: Dolphin's log isn't there, so it can't be on\n");
    return;
  }
  std::vector<std::string> problems;
  auto state = std::make_shared<State>();
  state->places = ParseWatchList(text, &problems);
  for (const std::string& problem : problems)
    std::fprintf(stderr, "memory watch: %s, left out\n", problem.c_str());
  if (state->places.empty())
    return;
  state->where.assign(state->places.size(), 0);
  // Dolphin logs a memory breakpoint's hits as notices of its MEMMAP log.
  state->memmap_was_on = log->IsEnabled(Common::Log::LogType::MEMMAP);
  log->SetEnable(Common::Log::LogType::MEMMAP, true);
  log->RegisterListener(Common::Log::LogListener::LOG_WINDOW_LISTENER,
                        std::make_unique<HitListener>(state));
  log->EnableListener(Common::Log::LogListener::LOG_WINDOW_LISTENER, true);
  std::fprintf(stderr, "memory watch: on, %zu place(s)\n", state->places.size());
  m_state = state;
  state->thread = std::jthread([raw = state.get()](std::stop_token stop) {
    while (!stop.stop_requested())
    {
      Follow(*raw);
      for (int i = 0; i < 5 && !stop.stop_requested(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });
}

MemoryWatch::~MemoryWatch()
{
  if (!m_state)
    return;
  State& state = *m_state;
  state.thread.request_stop();
  if (state.thread.joinable())
    state.thread.join();
  Core::System& system = Core::System::GetInstance();
  std::vector<Move> moves;
  {
    std::lock_guard lock(state.mutex);
    for (std::size_t i = 0; i < state.where.size(); ++i)
      if (state.where[i] != 0)
        moves.push_back({i, state.where[i], 0});
    std::fill(state.where.begin(), state.where.end(), 0);
  }
  if (!moves.empty())
    MoveChecks(system, moves);
  if (auto* log = Common::Log::LogManager::GetInstance())
  {
    log->EnableListener(Common::Log::LogListener::LOG_WINDOW_LISTENER, false);
    log->SetEnable(Common::Log::LogType::MEMMAP, state.memmap_was_on);
  }
  // How many times each instruction touched each place, most first.
  std::lock_guard lock(state.mutex);
  for (std::size_t i = 0; i < state.places.size(); ++i)
  {
    std::vector<std::pair<std::uint64_t, std::pair<bool, std::uint32_t>>> list;
    for (const auto& [key, count] : state.hits)
      if (std::get<0>(key) == i)
        list.push_back({count, {std::get<1>(key), std::get<2>(key)}});
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::fprintf(stderr, "memory watch: %s (%s): %zu instruction(s)\n",
                 state.places[i].name.c_str(), state.places[i].text.c_str(), list.size());
    for (const auto& [count, what] : list)
      std::fprintf(stderr, "memory watch:   0x%08X %s %llu time(s)\n", what.second,
                   what.first ? "writes" : "reads", static_cast<unsigned long long>(count));
  }
}
}  // namespace moderngekko::detail
