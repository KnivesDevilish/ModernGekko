// The mods' charge meters without Dolphin (src/runtime/mod_hud_core.hpp):
// hud.ini, and what a meter shows as the words in memory change.
#include "mod_hud_core.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mg = moderngekko::detail;

namespace
{
struct Memory
{
  std::map<std::uint32_t, std::uint32_t> words;
  mg::ModGauge::ReadWord Reader()
  {
    return [this](std::uint32_t a) -> std::optional<std::uint32_t> {
      const auto f = words.find(a);
      return f == words.end() ? std::optional<std::uint32_t>{0} : std::optional{f->second};
    };
  }
};

bool Near(double a, double b)
{
  return std::fabs(a - b) < 1e-3;
}
}  // namespace

int main()
{
  // 1. hud.ini
  std::vector<std::string> problems;
  const auto config = mg::ParseHudIni(
      "# comment\n"
      "[meter]\n"
      "heartbeat = 0x80B7FB00\n"
      "value=0x80B7FB50   # with a comment\n"
      "step = 0x80B7F010\n"
      "step = 0x80B7F014 ; another\n"
      "step = 0x80B7F018\n"
      "event_count = 0x80B7FB20\n"
      "event_id = 0x80B7FB30\n"
      "burst_events = 5, 6,7\n"
      "magic_address = 0x80B7FB40\n"
      "magic_value = 0x534F4E44\n"
      "x = 0.4 # left\n"
      "y = 0.7\n"
      "size = 0.12\n"
      "color = #00FF80\n"
      "color = ff0000   # without '#'\n"
      "color = blue\n"
      "size2 = 1\n"
      "y = 2\n"
      "step = 0x80B7F01A\n",
      &problems);
  if (!config || problems.size() != 4)  // blue, size2, y = 2, an unaligned step
    return 1;
  if (config->heartbeat != 0x80B7FB00 || config->value != 0x80B7FB50 ||
      config->steps != std::vector<std::uint32_t>{0x80B7F010, 0x80B7F014, 0x80B7F018} ||
      config->burst_events != std::vector<std::uint32_t>{5, 6, 7} ||
      config->magic_value != 0x534F4E44 || !Near(config->x, 0.4) || !Near(config->y, 0.7) ||
      !Near(config->size, 0.12) || config->port != 0)
    return 2;
  // Two colours given, the third from the defaults.
  if (config->colors != std::vector<std::uint32_t>{0x00FF80, 0xFF0000, 0xFFE23B})
    return 3;
  problems.clear();
  if (mg::ParseHudIni("value = 0x80000010\nstep = 0x80000020\n", &problems) || problems.empty())
    return 4;  // no heartbeat
  problems.clear();
  if (mg::ParseHudIni("heartbeat = 0x80000000\nvalue = 0x80000010\n", &problems))
    return 5;  // no steps
  problems.clear();
  if (mg::ParseHudIni("heartbeat = 0x80000000\nvalue = 0x80000010\nstep = 0x80000020\n"
                      "event_count = 0x80000030\n",
                      &problems))
    return 6;  // event_count without event_id
  problems.clear();
  const auto small = mg::ParseHudIni(
      "heartbeat = 0x80000000\nvalue = 0x80000010\nstep = 0x80000020\nport = 2\n"
      "color = #112233\ncolor = #445566\n",
      &problems);
  if (!small || !problems.empty() || small->port != 2 || small->colors.size() != 1 ||
      small->magic_address != 0 || small->event_count != 0)
    return 7;

  // 2. The meter
  Memory memory;
  memory.words[0x80B7F010] = 45;
  memory.words[0x80B7F014] = 100;
  memory.words[0x80B7F018] = 160;
  mg::ModGauge gauge(*config);
  const auto read = memory.Reader();
  double now = 0;
  std::uint32_t beat = 1;
  // A game frame: the heartbeat goes up, then the meter looks (60 a second).
  const auto frame = [&](std::uint32_t value) {
    memory.words[config->heartbeat] = ++beat;
    memory.words[config->value] = value;
    now += 1.0 / 60;
    gauge.Update(read, now);
  };
  // Without the magic word: nothing, whatever the other words say.
  for (int i = 0; i < 30; ++i)
    frame(60);
  if (gauge.Look().alpha != 0)
    return 8;
  // With it: shows and fills towards level 1.
  memory.words[config->magic_address] = config->magic_value;
  for (int i = 0; i < 10; ++i)
    frame(20);
  mg::GaugeLook look = gauge.Look();
  if (look.alpha != 1.0f || look.level != 0 || look.levels != 3 ||
      !Near(look.progress, 20.0 / 45) || look.flash != 0)
    return 9;
  // Level 1: a flash, and the fill starts again towards level 2.
  frame(70);
  look = gauge.Look();
  if (look.level != 1 || !Near(look.progress, 25.0 / 55) || !Near(look.flash, 1.0))
    return 10;
  for (int i = 0; i < 30; ++i)
    frame(71);
  if (gauge.Look().flash != 0 || gauge.Look().level != 1)  // half a second later
    return 11;
  // Past the last level: level 3, full.
  frame(200);
  look = gauge.Look();
  if (look.level != 3 || look.progress != 1.0f || look.flash <= 0)
    return 12;
  // The release: value 0 and event 7. A burst at level 3, and it fades.
  memory.words[config->event_id] = 7;
  memory.words[config->event_count] = 1;
  frame(0);
  look = gauge.Look();
  if (!Near(look.burst, 1.0) || look.burst_level != 3 || look.alpha >= 1.0f || look.alpha <= 0)
    return 13;
  for (int i = 0; i < 30; ++i)
    frame(0);
  look = gauge.Look();
  if (look.alpha != 0 || look.burst != 0 || look.level != 0)
    return 14;
  // An event not in burst_events: no burst.
  memory.words[config->event_id] = 1;
  memory.words[config->event_count] = 2;
  frame(10);
  if (gauge.Look().burst != 0)
    return 15;
  // The heartbeat stops (the game paused) with value still set: it fades out.
  for (int i = 0; i < 10; ++i)
    frame(30);
  if (gauge.Look().alpha != 1.0f)
    return 16;
  for (int i = 0; i < 30; ++i)
  {
    now += 1.0 / 60;
    gauge.Update(read, now);
  }
  if (gauge.Look().alpha != 0)
    return 17;
  // The magic word goes (other code there): it fades out too.
  for (int i = 0; i < 10; ++i)
    frame(30);
  memory.words[config->magic_address] = 0;
  frame(30);
  if (gauge.Look().alpha >= 1.0f)
    return 18;
  for (int i = 0; i < 30; ++i)
    frame(30);
  if (gauge.Look().alpha != 0)
    return 19;

  // 3. Another port: its row, not port 1's.
  mg::ModHudConfig second = *config;
  second.port = 1;
  mg::ModGauge other(second);
  memory.words[config->magic_address] = config->magic_value;
  for (int i = 0; i < 10; ++i)
  {
    frame(150);  // port 1 charges
    memory.words[config->heartbeat + 4] = beat;  // port 2's code runs, not charging
    memory.words[config->value + 4] = 0;
    other.Update(read, now);
  }
  if (other.Look().alpha != 0)
    return 20;
  for (int i = 0; i < 10; ++i)
  {
    frame(0);
    memory.words[config->heartbeat + 4] = beat;
    memory.words[config->value + 4] = 110;
    other.Update(read, now);
  }
  look = other.Look();
  if (look.alpha != 1.0f || look.level != 2 || !Near(look.progress, 10.0 / 60))
    return 21;
  // Unreadable memory (the game stopping): it fades out.
  const mg::ModGauge::ReadWord nothing = [](std::uint32_t) { return std::nullopt; };
  for (int i = 0; i < 30; ++i)
  {
    now += 1.0 / 60;
    other.Update(nothing, now);
  }
  if (other.Look().alpha != 0)
    return 22;
  return 0;
}
