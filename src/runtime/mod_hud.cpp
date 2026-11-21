#include "mod_hud.hpp"

#include "mod_hud_core.hpp"
#include "mod_hud_draw.hpp"

#include "Common/HookableEvent.h"
#include "Core/Core.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "VideoCommon/Present.h"

#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace moderngekko::detail
{
namespace
{
namespace fs = std::filesystem;

std::string PathText(const fs::path& path)
{
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}
}  // namespace

struct ModHud::State
{
  std::vector<ModGauge> gauges;
  // Held while the meters read the game's memory; the state callback takes it
  // to say Dolphin is stopping, so from then on they don't.
  std::mutex mutex;
  bool stopping = false;
  Common::EventHook state_hook;
  int last_frame = -1;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

std::unique_ptr<ModHud> ModHud::Create(const std::vector<fs::path>& mods)
{
  auto state = std::make_unique<State>();
  for (const fs::path& mod : mods)
  {
    const fs::path ini_path = mod / "hud.ini";
    std::error_code ec;
    if (!fs::is_regular_file(ini_path, ec))
      continue;
    const std::string name = PathText(mod.filename());
    std::ifstream file(ini_path, std::ios::binary);
    if (!file)
    {
      std::cerr << "mod hud: " << name << ": can't read hud.ini\n";
      continue;
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::string> problems;
    auto config = ParseHudIni(text, &problems);
    for (const std::string& problem : problems)
      std::cerr << "mod hud: " << name << ": hud.ini, " << problem << '\n';
    if (!config)
    {
      std::cerr << "mod hud: " << name << ": left out\n";
      continue;
    }
    std::cerr << "mod hud: " << name << ": a meter with " << config->steps.size()
              << " level(s), port " << config->port + 1 << '\n';
    state->gauges.emplace_back(std::move(*config));
  }
  if (state->gauges.empty())
    return nullptr;
  return std::unique_ptr<ModHud>(new ModHud(std::move(state)));
}

ModHud::ModHud(std::unique_ptr<State> state) : m_state(std::move(state))
{
  m_state->state_hook = Core::AddOnStateChangedCallback([state = m_state.get()](Core::State s) {
    if (s == Core::State::Stopping || s == Core::State::Uninitialized)
    {
      std::lock_guard lock(state->mutex);
      state->stopping = true;
    }
  });
}

ModHud::~ModHud()
{
  m_state->state_hook = {};
}

void ModHud::AddToFrame()
{
  if (!g_presenter || !ImGui::GetCurrentContext())
    return;
  // Once per frame of the on-screen UI, as the border.
  const int frame = ImGui::GetFrameCount();
  if (frame == m_state->last_frame)
    return;
  m_state->last_frame = frame;
  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - m_state->start).count();
  {
    std::lock_guard lock(m_state->mutex);
    Core::System& system = Core::System::GetInstance();
    const Core::State core = Core::GetState(system);
    const bool readable =
        !m_state->stopping && (core == Core::State::Running || core == Core::State::Paused);
    Memory::MemoryManager& memory = system.GetMemory();
    const std::uint8_t* ram = readable ? memory.GetRAM() : nullptr;
    const std::uint32_t ram_size = readable ? memory.GetRamSizeReal() : 0;
    // Nothing to read (the game stopping): the meters see no heartbeat and fade.
    const auto read = [ram, ram_size](std::uint32_t address) -> std::optional<std::uint32_t> {
      const std::uint32_t offset = address - 0x80000000u;
      if (!ram || address < 0x80000000u || (address & 3) != 0 || offset + 4 > ram_size)
        return std::nullopt;
      const std::uint8_t* p = ram + offset;
      return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
             (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
    };
    for (ModGauge& gauge : m_state->gauges)
      gauge.Update(read, now);
  }
  // The game's picture in the window, from the last frame presented.
  const auto& game = g_presenter->GetTargetRectangle();
  const ImVec2 origin(static_cast<float>(game.left), static_cast<float>(game.top));
  const ImVec2 size(static_cast<float>(game.GetWidth()), static_cast<float>(game.GetHeight()));
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  for (const ModGauge& gauge : m_state->gauges)
    DrawGauge(list, gauge.Config(), gauge.Look(), origin, size);
}
}  // namespace moderngekko::detail
