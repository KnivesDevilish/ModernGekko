#include "mod_sounds.hpp"

#include "mod_sounds_core.hpp"

#include "AudioCommon/Mixer.h"
#include "AudioCommon/SoundStream.h"
#include "Common/HookableEvent.h"
#include "Core/Core.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace moderngekko::detail
{
namespace
{
namespace fs = std::filesystem;

// The mixer's Game Boy Advance channels (four, one per port) are mixed in
// always; a Wii game never has a GBA, so the one of port 4 is free.
constexpr std::size_t GBA_CHANNEL = 3;
// Mixer.h's GBA_SAMPLE_RATE_DIVIDEND: a GBA channel's rate is this divided by
// a whole number, so it takes 48000 Hz as 16777216 / 349 = 48072.25 Hz. Its
// samples go in at that rate, so it neither runs dry nor fills up.
constexpr double GBA_DIVIDEND = 16777216.0;

std::string PathText(const fs::path& path)
{
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::optional<std::vector<std::uint8_t>> ReadFile(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return std::nullopt;
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
}
}  // namespace

struct ModSounds::State
{
  ModSoundEngine engine;
  // Held while the sounds touch the game's memory or the mixer; the state
  // callback takes it to say Dolphin is stopping, so from then on they don't.
  std::mutex mutex;
  bool stopping = false;
  Common::EventHook state_hook;
  std::jthread thread;
};

namespace
{
void Run(std::stop_token stop, ModSounds::State* state)
{
  using clock = std::chrono::steady_clock;
  const double out_rate = GBA_DIVIDEND / std::floor(GBA_DIVIDEND / MOD_SOUND_RATE);
  const clock::time_point start = clock::now();
  clock::time_point last = start;
  double owed = 0.0;
  Mixer* set_up = nullptr;
  std::vector<float> mixed;
  std::vector<s16> samples;
  while (!stop.stop_requested())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const clock::time_point now = clock::now();
    const double elapsed = std::chrono::duration<double>(now - last).count();
    last = now;
    std::lock_guard lock(state->mutex);
    if (state->stopping)
      break;
    Core::System& system = Core::System::GetInstance();
    SoundStream* stream = system.GetSoundStream();
    Mixer* mixer = stream ? stream->GetMixer() : nullptr;
    if (!mixer || Core::GetState(system) != Core::State::Running)
    {
      // Paused or not ready: no sound, and no time owed for later.
      owed = 0.0;
      continue;
    }
    if (mixer != set_up)
    {
      mixer->SetGBAInputSampleRate(GBA_CHANNEL, MOD_SOUND_RATE);
      mixer->SetGBAVolume(GBA_CHANNEL, 256, 256);
      set_up = mixer;
      owed = 0.0;
    }
    owed += std::min(elapsed, 0.1) * out_rate;
    const std::size_t frames = static_cast<std::size_t>(owed);
    if (frames == 0)
      continue;
    owed -= static_cast<double>(frames);

    Memory::MemoryManager& memory = system.GetMemory();
    const std::uint8_t* ram = memory.GetRAM();
    const std::uint32_t ram_size = memory.GetRamSizeReal();
    const auto read = [ram, ram_size](std::uint32_t address) -> std::optional<std::uint32_t> {
      const std::uint32_t offset = address - 0x80000000u;
      if (!ram || address < 0x80000000u || (address & 3) != 0 || offset + 4 > ram_size)
        return std::nullopt;
      const std::uint8_t* p = ram + offset;
      return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
             (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
    };
    state->engine.Poll(read, std::chrono::duration<double>(now - start).count());

    mixed.resize(frames * 2);
    state->engine.Mix(mixed.data(), frames, out_rate);
    samples.resize(frames * 2);
    for (std::size_t i = 0; i < samples.size(); ++i)
      samples[i] = static_cast<s16>(std::clamp(std::lround(mixed[i] * 32767.0f), -32768L, 32767L));
    mixer->PushGBASamples(GBA_CHANNEL, samples.data(), frames);
  }
}
}  // namespace

std::unique_ptr<ModSounds> ModSounds::Create(const std::vector<fs::path>& mods)
{
  auto state = std::make_shared<State>();
  for (const fs::path& mod : mods)
  {
    const fs::path ini_path = mod / "sounds.ini";
    std::error_code ec;
    if (!fs::is_regular_file(ini_path, ec))
      continue;
    const std::string name = PathText(mod.filename());
    const auto text = ReadFile(ini_path);
    if (!text)
    {
      std::cerr << "mod sounds: " << name << ": can't read sounds.ini\n";
      continue;
    }
    std::vector<std::string> problems;
    const auto config = ParseSoundsIni(
        std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), &problems);
    for (const std::string& problem : problems)
      std::cerr << "mod sounds: " << name << ": sounds.ini, " << problem << '\n';
    if (!config)
    {
      std::cerr << "mod sounds: " << name << ": left out\n";
      continue;
    }
    std::map<std::string, std::shared_ptr<const ModSound>> loaded;
    const auto load = [&](const std::string& relative) -> std::shared_ptr<const ModSound> {
      if (const auto found = loaded.find(relative); found != loaded.end())
        return found->second;
      std::shared_ptr<const ModSound> sound;
      const auto bytes = ReadFile(mod / fs::path(std::u8string(relative.begin(), relative.end())));
      std::string why;
      if (!bytes)
        why = "can't read it";
      else if (auto parsed = ParseWav(*bytes, &why))
        sound = std::make_shared<const ModSound>(std::move(*parsed));
      if (!sound)
        std::cerr << "mod sounds: " << name << ": " << relative << ": " << why << ", left out\n";
      loaded[relative] = sound;
      return sound;
    };
    std::shared_ptr<const ModSound> loop = config->loop.empty() ? nullptr : load(config->loop);
    std::map<std::uint32_t, std::shared_ptr<const ModSound>> events;
    for (const auto& [number, relative] : config->events)
    {
      if (auto sound = load(relative))
        events[number] = std::move(sound);
    }
    if (!loop && events.empty())
    {
      std::cerr << "mod sounds: " << name << ": no sound loaded, left out\n";
      continue;
    }
    std::size_t count = 0;
    for (const auto& [relative, sound] : loaded)
      count += sound ? 1 : 0;
    std::cerr << "mod sounds: " << name << ": " << count << " sound(s)"
              << (loop ? ", a loop" : "") << ", " << events.size() << " event(s)\n";
    state->engine.AddMod(*config, std::move(loop), std::move(events));
  }
  if (state->engine.Empty())
    return nullptr;
  return std::unique_ptr<ModSounds>(new ModSounds(std::move(state)));
}

ModSounds::ModSounds(std::shared_ptr<State> state) : m_state(std::move(state))
{
  m_state->state_hook = Core::AddOnStateChangedCallback([state = m_state.get()](Core::State s) {
    if (s == Core::State::Stopping || s == Core::State::Uninitialized)
    {
      std::lock_guard lock(state->mutex);
      state->stopping = true;
    }
  });
  m_state->thread = std::jthread([state = m_state.get()](std::stop_token stop) {
    Run(std::move(stop), state);
  });
}

ModSounds::~ModSounds()
{
  m_state->thread.request_stop();
  if (m_state->thread.joinable())
    m_state->thread.join();
  m_state->state_hook = {};
}
}  // namespace moderngekko::detail
