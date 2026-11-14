// Memory snapshots, for finding where a game keeps a value (its speed, say)
// to make mods: Shift+F9, while the environment variable
// MODERNGEKKO_MEMORY_SNAPSHOTS is set (to anything but 0), saves the game's
// memory to MemorySnapshots/<game id>/<game id>_0001.mem and so on in the
// user directory, and takes F9's screenshot, so what the screen showed at
// that moment (the speedometer) can be read later.
//
// A snapshot is "MGMS", then big-endian u32s: 1 (the format), MEM1's size,
// MEM2's size (0 on GameCube); then MEM1 and MEM2 as the console had them
// (from 0x80000000 and 0x90000000). It is the game's own data: it stays on
// this computer.
#include "screenshot_keys.hpp"

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace moderngekko::detail
{
bool MemorySnapshotsOn()
{
  const char* value = std::getenv("MODERNGEKKO_MEMORY_SNAPSHOTS");
  return value && *value && std::string_view(value) != "0";
}

namespace
{
void PutU32(std::vector<std::uint8_t>* out, std::uint32_t value)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    out->push_back(static_cast<std::uint8_t>(value >> shift));
}
}  // namespace

void TakeMemorySnapshot()
{
  if (!MemorySnapshotsOn())
    return;
  Core::System& system = Core::System::GetInstance();
  const Core::State state = Core::GetState(system);
  if (state != Core::State::Running && state != Core::State::Paused)
    return;
  const std::string game = SConfig::GetInstance().GetGameID();
  const std::string folder =
      File::GetUserPath(D_USER_IDX) + "MemorySnapshots" + DIR_SEP + game + DIR_SEP;
  if (!File::CreateFullPath(folder))
  {
    std::fprintf(stderr, "memory snapshot: can't make %s\n", folder.c_str());
    return;
  }
  std::string path;
  for (int number = 1; number < 10000; ++number)
  {
    char name[32];
    std::snprintf(name, sizeof(name), "_%04d.mem", number);
    if (!File::Exists(folder + game + name))
    {
      path = folder + game + name;
      break;
    }
  }
  if (path.empty())
    return;
  // Copied on the CPU thread, between its instructions, so it is one moment;
  // written to disk on a thread of its own, so the game doesn't wait.
  Core::RunOnCPUThread(system, [path] {
    Memory::MemoryManager& memory = Core::System::GetInstance().GetMemory();
    const std::uint32_t ram_size = memory.GetRamSizeReal();
    const std::uint8_t* exram = memory.GetEXRAM();
    const std::uint32_t exram_size = exram ? memory.GetExRamSizeReal() : 0;
    auto data = std::make_shared<std::vector<std::uint8_t>>();
    data->reserve(16 + std::size_t{ram_size} + exram_size);
    data->insert(data->end(), {'M', 'G', 'M', 'S'});
    PutU32(data.get(), 1);
    PutU32(data.get(), ram_size);
    PutU32(data.get(), exram_size);
    data->insert(data->end(), memory.GetRAM(), memory.GetRAM() + ram_size);
    if (exram_size != 0)
      data->insert(data->end(), exram, exram + exram_size);
    std::thread([data, path] {
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      file.write(reinterpret_cast<const char*>(data->data()),
                 static_cast<std::streamsize>(data->size()));
      std::fprintf(stderr, file ? "memory snapshot: %s\n" : "memory snapshot: can't write %s\n",
                   path.c_str());
    }).detach();
  });
  TakeScreenshot();
}
}  // namespace moderngekko::detail
