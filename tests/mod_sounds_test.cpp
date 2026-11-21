// The mods' sounds without Dolphin (src/runtime/mod_sounds_core.hpp): sounds.ini,
// WAV files, and the mixing as the words in memory say.
#include "mod_sounds_core.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mg = moderngekko::detail;

namespace
{
std::vector<std::uint8_t> Wav(std::uint32_t rate, std::uint16_t channels,
                              const std::vector<std::int16_t>& samples, std::uint16_t bits = 16)
{
  std::vector<std::uint8_t> b;
  const auto put32 = [&](std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
      b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  };
  const auto put16 = [&](std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
  };
  const auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
  tag("RIFF");
  put32(36 + static_cast<std::uint32_t>(samples.size() * 2));
  tag("WAVE");
  tag("LIST");  // a chunk to skip
  put32(3);
  b.insert(b.end(), {1, 2, 3, 0});
  tag("fmt ");
  put32(16);
  put16(1);
  put16(channels);
  put32(rate);
  put32(rate * channels * 2);
  put16(static_cast<std::uint16_t>(channels * 2));
  put16(bits);
  tag("data");
  put32(static_cast<std::uint32_t>(samples.size() * 2));
  for (const std::int16_t s : samples)
    put16(static_cast<std::uint16_t>(s));
  return b;
}

struct Memory
{
  std::map<std::uint32_t, std::uint32_t> words;
  mg::ModSoundEngine::ReadWord Reader()
  {
    return [this](std::uint32_t a) -> std::optional<std::uint32_t> {
      const auto f = words.find(a);
      return f == words.end() ? std::optional<std::uint32_t>{0} : std::optional{f->second};
    };
  }
};

double Peak(const std::vector<float>& v)
{
  double m = 0;
  for (const float x : v)
    m = std::max(m, double(std::fabs(x)));
  return m;
}
}  // namespace

int main()
{
  // 1. sounds.ini
  std::vector<std::string> problems;
  const auto config = mg::ParseSoundsIni(
      "# comment\n"
      "heartbeat = 0x80B7FB00\n"
      "loop_state=0x80B7FB10   # with a comment\n"
      "event_count = 0x80B7FB20\n"
      "event_id = 0x80B7FB30\n"
      "magic_address = 0x80B7FB40\n"
      "magic_value = 0x534F4E44\n"
      "loop = sonidos/derrape.wav\n"
      "loop_volume = 0.5\n"
      "loop_pitch_per_level = 0.06\n"
      "event.1 = sonidos/nivel1.wav\n"
      "event.5 = sonidos/soltar.wav\n"
      "event.0 = nada.wav\n"
      "event.2 = ../fuera.wav\n"
      "colour = blue\n"
      "heartbeat2 0x1\n",
      &problems);
  if (!config || problems.size() != 4)
    return 1;
  if (config->heartbeat != 0x80B7FB00 || config->loop_state != 0x80B7FB10 ||
      config->magic_value != 0x534F4E44 || config->loop != "sonidos/derrape.wav" ||
      config->loop_volume != 0.5 || config->events.size() != 2 ||
      config->events.at(5) != "sonidos/soltar.wav")
    return 2;
  problems.clear();
  if (mg::ParseSoundsIni("loop = a.wav\nheartbeat = 0x80000002\n", &problems) || problems.size() < 4)
    return 3;

  // 2. WAV files
  std::string why;
  const auto stereo = mg::ParseWav(Wav(48000, 2, {100, -100, 200, -200, 300, -300}), &why);
  if (!stereo || stereo->frames() != 3 || std::fabs(stereo->samples[5] - (-300 / 32768.0f)) > 1e-6f)
    return 4;
  const auto mono = mg::ParseWav(Wav(24000, 1, {0, 1000, 2000, 3000}), &why);
  if (!mono || mono->frames() != 8 || mono->samples[0] != mono->samples[1] ||
      std::fabs(mono->samples[2 * 2] - 1000 / 32768.0f) > 1e-6f)
    return 5;
  if (mg::ParseWav(Wav(48000, 1, {1, 2}, 8), &why) || why.find("8 bits") == std::string::npos)
    return 6;
  if (mg::ParseWav({'R', 'I', 'F', 'F'}, &why))
    return 7;

  // 3. Mixing
  auto loop = std::make_shared<mg::ModSound>();
  loop->samples.assign(480 * 2, 0.5f);  // 10 ms of a constant
  auto shot = std::make_shared<mg::ModSound>();
  shot->samples.assign(4800 * 2, 0.25f);  // 100 ms
  mg::ModSoundEngine engine;
  engine.AddMod(*config, loop, {{1, shot}, {5, shot}});
  Memory memory;
  const auto read = memory.Reader();
  std::vector<float> out(4800 * 2);
  double now = 0;
  const auto step = [&](double seconds) {
    engine.Poll(read, now);
    engine.Mix(out.data(), static_cast<std::size_t>(48000 * seconds), 48000);
    now += seconds;
  };
  // Without the magic word: nothing, whatever the other words say.
  memory.words[config->loop_state] = 1;
  for (std::uint32_t beat = 1; beat < 5; ++beat)
  {
    memory.words[config->heartbeat] = beat;
    step(0.05);
  }
  if (Peak(out) != 0)
    return 8;
  // With it, and a heartbeat: the loop fades in.
  memory.words[config->magic_address] = config->magic_value;
  for (std::uint32_t beat = 10; beat < 20; ++beat)
  {
    memory.words[config->heartbeat] = beat;
    step(0.05);
  }
  if (std::fabs(Peak(out) - 0.25) > 0.01)  // 0.5 x loop_volume 0.5
    return 9;
  // The heartbeat stops (the game paused): the loop fades out.
  for (int i = 0; i < 20; ++i)
    step(0.05);
  if (Peak(out) > 1e-3)
    return 10;
  // Heartbeat again, loop_state 0: silence; then an event: it plays once.
  memory.words[config->loop_state] = 0;
  memory.words[config->heartbeat] = 100;
  step(0.01);
  memory.words[config->heartbeat] = 101;
  memory.words[config->event_id] = 1;
  memory.words[config->event_count] = 1;
  step(0.01);
  if (engine.Voices() != 1 || std::fabs(Peak(out) - 0.25 * 1.0) > 1e-3)
    return 11;
  for (std::uint32_t beat = 102; beat < 110; ++beat)
  {
    memory.words[config->heartbeat] = beat;
    step(0.05);
  }
  if (engine.Voices() != 0)
    return 12;
  // An event with no sound, and one on another port.
  memory.words[config->event_id] = 3;
  memory.words[config->event_count] = 2;
  memory.words[config->heartbeat + 4] = 1;
  step(0.01);
  memory.words[config->heartbeat + 4] = 2;
  memory.words[config->heartbeat] = 111;
  memory.words[config->event_id + 4] = 5;
  memory.words[config->event_count + 4] = 7;
  step(0.01);
  if (engine.Voices() != 1)  // port 2's event 5; port 1's event 3 has no sound
    return 13;
  memory.words[config->heartbeat + 4] = 3;
  memory.words[config->event_count + 4] = 9;  // two at once: the last one plays
  step(0.01);
  if (engine.Voices() != 2)
    return 14;
  // Level 2: the loop plays 12% faster (a 10 ms loop: a constant, but it keeps going).
  memory.words[config->loop_state] = 3;
  for (std::uint32_t beat = 200; beat < 210; ++beat)
  {
    memory.words[config->heartbeat] = beat;
    step(0.05);
  }
  if (std::fabs(Peak(out) - 0.25) > 0.01)
    return 15;
  return 0;
}
