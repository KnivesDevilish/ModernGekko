#pragma once

// The parts of the mods' sounds (mod_sounds.hpp) that don't need Dolphin, so
// tests/mod_sounds_test.cpp can check them: a mod's sounds.ini, its WAV files,
// and the mixing of its sounds as the words its code writes say.
//
// sounds.ini (in the mod's folder, next to patches.ini):
//
//   # The mod's code keeps four rows of four 32-bit words, one word for each
//   # controller port (0 to 3), at these addresses:
//   heartbeat = 0x80B7FB00     # goes up each frame the mod's code runs
//   loop_state = 0x80B7FB10    # 0 = the loop is off; N >= 1 = on, at level N - 1
//   event_count = 0x80B7FB20   # goes up by one for each event
//   event_id = 0x80B7FB30      # the last event's number (written before the count)
//   # And, so nothing plays while other code sits at those addresses: a word
//   # that only holds this value while the mod's code is in memory.
//   magic_address = 0x80B7FB40
//   magic_value = 0x534F4E44
//   loop = sounds/loop.wav              # plays while loop_state is on
//   loop_volume = 0.6                   # 0 to 2
//   loop_pitch_per_level = 0.06         # the loop plays this much faster per level
//   event.1 = sounds/level1.wav         # plays once when event 1 comes
//   event_volume = 0.9                  # 0 to 2
//
// A "#" or ";" after a space starts a comment, also at the end of a line.
//
// The loop fades out when its port's heartbeat stops for a moment (the game
// paused, the race over, other code running), whatever loop_state says.
//
// WAV files: PCM, 16 bits, mono or stereo, any sample rate (48000 Hz plays as
// it is; others are converted when they load).

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::detail
{
inline constexpr std::uint32_t MOD_SOUND_RATE = 48000;
inline constexpr int MOD_SOUND_PORTS = 4;
inline constexpr std::size_t MOD_SOUND_MAX_VOICES = 16;
// A port's loop keeps playing this long after its heartbeat last changed.
inline constexpr double MOD_SOUND_ALIVE_SECONDS = 0.15;

// A sound, as 48000 Hz stereo floats (left, right, left, right...).
struct ModSound
{
  std::vector<float> samples;
  std::size_t frames() const { return samples.size() / 2; }
};

struct ModSoundsConfig
{
  std::uint32_t heartbeat = 0;
  std::uint32_t loop_state = 0;
  std::uint32_t event_count = 0;
  std::uint32_t event_id = 0;
  std::uint32_t magic_address = 0;  // 0: none
  std::uint32_t magic_value = 0;
  std::string loop;  // a path in the mod's folder; empty: no loop
  double loop_volume = 1.0;
  double loop_pitch_per_level = 0.0;
  std::map<std::uint32_t, std::string> events;  // event number -> path
  double event_volume = 1.0;
};

namespace mod_sounds_text
{
inline std::string Trim(std::string_view text)
{
  std::size_t a = 0;
  std::size_t b = text.size();
  while (a < b && std::isspace(static_cast<unsigned char>(text[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1])))
    --b;
  return std::string(text.substr(a, b - a));
}

inline std::optional<std::uint32_t> ParseWord(std::string_view text)
{
  std::string t = Trim(text);
  int base = 10;
  if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
  {
    t = t.substr(2);
    base = 16;
  }
  if (t.empty() || t.size() > 10)
    return std::nullopt;
  std::uint64_t value = 0;
  for (const char c : t)
  {
    int digit;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (base == 16 && c >= 'a' && c <= 'f')
      digit = c - 'a' + 10;
    else if (base == 16 && c >= 'A' && c <= 'F')
      digit = c - 'A' + 10;
    else
      return std::nullopt;
    value = value * base + digit;
    if (value > 0xFFFFFFFFull)
      return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}

inline std::optional<double> ParseNumber(std::string_view text)
{
  const std::string t = Trim(text);
  if (t.empty())
    return std::nullopt;
  char* end = nullptr;
  const double value = std::strtod(t.c_str(), &end);
  if (end != t.c_str() + t.size() || !std::isfinite(value))
    return std::nullopt;
  return value;
}

// A word of the game's memory that a mod can use: in MEM1, 4-byte aligned.
inline bool GoodAddress(std::uint32_t address)
{
  return address >= 0x80000000u && address <= 0x817FFFFCu && (address & 3) == 0;
}

// A path inside the mod's folder: relative, without "..".
inline bool GoodPath(const std::string& path)
{
  if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos)
    return false;
  std::size_t start = 0;
  while (start <= path.size())
  {
    std::size_t end = path.find_first_of("/\\", start);
    if (end == std::string::npos)
      end = path.size();
    if (path.compare(start, end - start, "..") == 0)
      return false;
    start = end + 1;
  }
  return true;
}
}  // namespace mod_sounds_text

// Reads sounds.ini. Lines it doesn't understand go to `problems`; nullopt when
// what is left can't work (the four rows are needed, and a sound).
inline std::optional<ModSoundsConfig> ParseSoundsIni(std::string_view text,
                                                     std::vector<std::string>* problems)
{
  using namespace mod_sounds_text;
  ModSoundsConfig config;
  bool have_magic_value = false;
  std::size_t start = 0;
  int line_number = 0;
  while (start < text.size())
  {
    std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos)
      end = text.size();
    std::string line = Trim(text.substr(start, end - start));
    start = end + 1;
    ++line_number;
    if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
      continue;
    const std::size_t equals = line.find('=');
    const auto problem = [&](const std::string& why) {
      problems->push_back("line " + std::to_string(line_number) + ": " + why);
    };
    if (equals == std::string::npos)
    {
      problem("no '=' in \"" + line + "\"");
      continue;
    }
    std::string key = Trim(std::string_view(line).substr(0, equals));
    std::string value = std::string(std::string_view(line).substr(equals + 1));
    for (std::size_t i = 1; i < value.size(); ++i)
    {
      if ((value[i] == '#' || value[i] == ';') &&
          std::isspace(static_cast<unsigned char>(value[i - 1])))
      {
        value.resize(i);
        break;
      }
    }
    value = Trim(value);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto address = [&](std::uint32_t* out) {
      const auto word = ParseWord(value);
      if (!word || !GoodAddress(*word))
        problem(key + ": \"" + value + "\" is not a 4-byte aligned address from 0x80000000 to 0x817FFFFC");
      else
        *out = *word;
    };
    const auto volume = [&](double* out, double low, double high) {
      const auto number = ParseNumber(value);
      if (!number || *number < low || *number > high)
        problem(key + ": \"" + value + "\" is not a number from " + std::to_string(low) + " to " +
                std::to_string(high));
      else
        *out = *number;
    };
    const auto path = [&](std::string* out) {
      if (!GoodPath(value))
        problem(key + ": \"" + value + "\" is not a file in the mod's folder");
      else
        *out = value;
    };
    if (key == "heartbeat")
      address(&config.heartbeat);
    else if (key == "loop_state")
      address(&config.loop_state);
    else if (key == "event_count")
      address(&config.event_count);
    else if (key == "event_id")
      address(&config.event_id);
    else if (key == "magic_address")
      address(&config.magic_address);
    else if (key == "magic_value")
    {
      if (const auto word = ParseWord(value))
      {
        config.magic_value = *word;
        have_magic_value = true;
      }
      else
      {
        problem("magic_value: \"" + value + "\" is not a 32-bit number");
      }
    }
    else if (key == "loop")
      path(&config.loop);
    else if (key == "loop_volume")
      volume(&config.loop_volume, 0.0, 2.0);
    else if (key == "loop_pitch_per_level")
      volume(&config.loop_pitch_per_level, -0.5, 1.0);
    else if (key == "event_volume")
      volume(&config.event_volume, 0.0, 2.0);
    else if (key.rfind("event.", 0) == 0)
    {
      const auto number = ParseWord(key.substr(6));
      if (!number || *number == 0)
        problem(key + ": events are numbered from 1");
      else
        path(&config.events[*number]);
    }
    else
      problem("unknown key \"" + key + "\"");
  }
  // An event whose file was refused leaves an empty path: drop it.
  for (auto it = config.events.begin(); it != config.events.end();)
    it = it->second.empty() ? config.events.erase(it) : std::next(it);
  bool ok = true;
  for (const auto& [name, value] : {std::pair{"heartbeat", config.heartbeat},
                                    std::pair{"loop_state", config.loop_state},
                                    std::pair{"event_count", config.event_count},
                                    std::pair{"event_id", config.event_id}})
  {
    if (value == 0)
    {
      problems->push_back(std::string("no ") + name + " address");
      ok = false;
    }
  }
  if (config.magic_address != 0 && !have_magic_value)
  {
    problems->push_back("magic_address without magic_value");
    ok = false;
  }
  if (config.loop.empty() && config.events.empty())
  {
    problems->push_back("no sounds (loop or event.N)");
    ok = false;
  }
  if (!ok)
    return std::nullopt;
  return config;
}

// A WAV file's bytes as a ModSound (48000 Hz stereo).
inline std::optional<ModSound> ParseWav(const std::vector<std::uint8_t>& bytes, std::string* why)
{
  const auto u16 = [&](std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at] | (bytes[at + 1] << 8));
  };
  const auto u32 = [&](std::size_t at) { return u16(at) | (u16(at + 2) << 16); };
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
  {
    *why = "not a WAV file";
    return std::nullopt;
  }
  std::uint32_t format = 0, channels = 0, rate = 0, bits = 0;
  std::size_t data_at = 0, data_size = 0;
  for (std::size_t at = 12; at + 8 <= bytes.size();)
  {
    const std::uint32_t size = u32(at + 4);
    const std::size_t body = at + 8;
    if (body + size > bytes.size() && std::memcmp(bytes.data() + at, "data", 4) != 0)
      break;
    if (std::memcmp(bytes.data() + at, "fmt ", 4) == 0 && size >= 16)
    {
      format = u16(body);
      channels = u16(body + 2);
      rate = u32(body + 4);
      bits = u16(body + 14);
      if (format == 0xFFFE && size >= 26)
        format = u16(body + 24);  // WAVE_FORMAT_EXTENSIBLE: the subformat's first word
    }
    else if (std::memcmp(bytes.data() + at, "data", 4) == 0)
    {
      data_at = body;
      data_size = std::min<std::size_t>(size, bytes.size() - body);
      break;
    }
    at = body + size + (size & 1);
  }
  if (format != 1 || bits != 16 || (channels != 1 && channels != 2) || rate < 4000 ||
      rate > 192000)
  {
    *why = "not a 16-bit PCM WAV, mono or stereo (format " + std::to_string(format) + ", " +
           std::to_string(bits) + " bits, " + std::to_string(channels) + " channel(s), " +
           std::to_string(rate) + " Hz)";
    return std::nullopt;
  }
  if (data_at == 0)
  {
    *why = "no data in the WAV file";
    return std::nullopt;
  }
  const std::size_t in_frames = data_size / (2 * channels);
  if (in_frames == 0)
  {
    *why = "the WAV file is empty";
    return std::nullopt;
  }
  std::vector<float> in(in_frames * 2);
  for (std::size_t i = 0; i < in_frames; ++i)
  {
    for (std::uint32_t c = 0; c < 2; ++c)
    {
      const std::size_t at = data_at + (i * channels + std::min(c, channels - 1)) * 2;
      in[i * 2 + c] = static_cast<std::int16_t>(u16(at)) / 32768.0f;
    }
  }
  ModSound sound;
  if (rate == MOD_SOUND_RATE)
  {
    sound.samples = std::move(in);
    return sound;
  }
  // Another rate: to 48000 Hz, straight lines between its samples.
  const std::size_t out_frames =
      std::max<std::size_t>(1, static_cast<std::size_t>(
                                   std::llround(double(in_frames) * MOD_SOUND_RATE / rate)));
  sound.samples.resize(out_frames * 2);
  for (std::size_t i = 0; i < out_frames; ++i)
  {
    const double pos = double(i) * rate / MOD_SOUND_RATE;
    const std::size_t k = std::min<std::size_t>(static_cast<std::size_t>(pos), in_frames - 1);
    const std::size_t k1 = std::min(k + 1, in_frames - 1);
    const float f = static_cast<float>(pos - k);
    for (int c = 0; c < 2; ++c)
      sound.samples[i * 2 + c] = in[k * 2 + c] * (1 - f) + in[k1 * 2 + c] * f;
  }
  return sound;
}

// The sounds of all the mods, mixed as their words say.
class ModSoundEngine
{
public:
  using ReadWord = std::function<std::optional<std::uint32_t>(std::uint32_t address)>;

  void AddMod(const ModSoundsConfig& config, std::shared_ptr<const ModSound> loop,
              std::map<std::uint32_t, std::shared_ptr<const ModSound>> events)
  {
    Mod mod;
    mod.config = config;
    mod.loop = std::move(loop);
    mod.events = std::move(events);
    m_mods.push_back(std::move(mod));
  }

  bool Empty() const { return m_mods.empty(); }

  // Reads the mods' words. `now` in seconds, from any start.
  void Poll(const ReadWord& read, double now)
  {
    for (Mod& mod : m_mods)
    {
      const ModSoundsConfig& c = mod.config;
      const bool here =
          c.magic_address == 0 || read(c.magic_address) == std::optional{c.magic_value};
      for (int p = 0; p < MOD_SOUND_PORTS; ++p)
      {
        Port& port = mod.ports[p];
        const std::uint32_t offset = 4u * static_cast<std::uint32_t>(p);
        if (!here)
        {
          port.seen = false;
          port.counted = false;
          port.loop_on = false;
          continue;
        }
        if (const auto beat = read(c.heartbeat + offset))
        {
          // Alive only once it changes: a word that just sits there is no heartbeat.
          if (port.seen && *beat != port.last_beat)
            port.alive_at = now;
          port.last_beat = *beat;
          port.seen = true;
        }
        const bool alive = port.seen && port.alive_at >= 0 &&
                           now - port.alive_at < MOD_SOUND_ALIVE_SECONDS;
        const auto loop = read(c.loop_state + offset);
        port.loop_on = alive && loop && *loop >= 1 && *loop <= 64;
        if (port.loop_on)
          port.level = static_cast<int>(*loop) - 1;
        if (const auto count = read(c.event_count + offset))
        {
          if (port.counted && *count != port.last_count && alive)
          {
            if (const auto id = read(c.event_id + offset))
            {
              const auto found = mod.events.find(*id);
              if (found != mod.events.end() && found->second)
                Play(found->second, static_cast<float>(c.event_volume));
            }
          }
          port.last_count = *count;
          port.counted = true;
        }
      }
    }
  }

  // Writes `frames` stereo frames (left, right...) to `out`, at `out_rate` Hz.
  void Mix(float* out, std::size_t frames, double out_rate)
  {
    std::fill(out, out + frames * 2, 0.0f);
    const double step = MOD_SOUND_RATE / out_rate;
    const float attack = 1.0f - static_cast<float>(std::exp(-1.0 / (out_rate * 0.02)));
    const float release = 1.0f - static_cast<float>(std::exp(-1.0 / (out_rate * 0.08)));
    const double glide = 1.0 - std::exp(-1.0 / (out_rate * 0.05));
    for (Mod& mod : m_mods)
    {
      if (!mod.loop || mod.loop->frames() == 0)
        continue;
      const ModSound& sound = *mod.loop;
      const double length = static_cast<double>(sound.frames());
      const float volume = static_cast<float>(mod.config.loop_volume);
      for (Port& port : mod.ports)
      {
        if (!port.loop_on && port.gain < 1e-4f)
        {
          port.gain = 0.0f;
          port.pos = 0.0;
          port.rate = 1.0 + mod.config.loop_pitch_per_level * port.level;
          continue;
        }
        const float target = port.loop_on ? 1.0f : 0.0f;
        const double want = std::max(0.25, 1.0 + mod.config.loop_pitch_per_level * port.level);
        for (std::size_t i = 0; i < frames; ++i)
        {
          port.gain += (target - port.gain) * (target > port.gain ? attack : release);
          port.rate += (want - port.rate) * glide;
          const std::size_t k = static_cast<std::size_t>(port.pos);
          const std::size_t k1 = k + 1 < sound.frames() ? k + 1 : 0;
          const float f = static_cast<float>(port.pos - static_cast<double>(k));
          const float g = port.gain * volume;
          out[i * 2] += (sound.samples[k * 2] * (1 - f) + sound.samples[k1 * 2] * f) * g;
          out[i * 2 + 1] += (sound.samples[k * 2 + 1] * (1 - f) + sound.samples[k1 * 2 + 1] * f) * g;
          port.pos += step * port.rate;
          while (port.pos >= length)
            port.pos -= length;
        }
      }
    }
    for (Voice& voice : m_voices)
    {
      const ModSound& sound = *voice.sound;
      for (std::size_t i = 0; i < frames && voice.pos < static_cast<double>(sound.frames()); ++i)
      {
        const std::size_t k = static_cast<std::size_t>(voice.pos);
        const std::size_t k1 = std::min(k + 1, sound.frames() - 1);
        const float f = static_cast<float>(voice.pos - static_cast<double>(k));
        out[i * 2] += (sound.samples[k * 2] * (1 - f) + sound.samples[k1 * 2] * f) * voice.gain;
        out[i * 2 + 1] +=
            (sound.samples[k * 2 + 1] * (1 - f) + sound.samples[k1 * 2 + 1] * f) * voice.gain;
        voice.pos += step;
      }
    }
    std::erase_if(m_voices, [](const Voice& voice) {
      return voice.pos >= static_cast<double>(voice.sound->frames());
    });
  }

  // For the tests: how many sounds that play once are playing.
  std::size_t Voices() const { return m_voices.size(); }

private:
  struct Port
  {
    bool seen = false;
    std::uint32_t last_beat = 0;
    double alive_at = -1.0;
    bool counted = false;
    std::uint32_t last_count = 0;
    bool loop_on = false;
    int level = 0;
    double pos = 0.0;
    float gain = 0.0f;
    double rate = 1.0;
  };
  struct Mod
  {
    ModSoundsConfig config;
    std::shared_ptr<const ModSound> loop;
    std::map<std::uint32_t, std::shared_ptr<const ModSound>> events;
    std::array<Port, MOD_SOUND_PORTS> ports;
  };
  struct Voice
  {
    std::shared_ptr<const ModSound> sound;
    double pos = 0.0;
    float gain = 1.0f;
  };

  void Play(std::shared_ptr<const ModSound> sound, float gain)
  {
    if (m_voices.size() >= MOD_SOUND_MAX_VOICES)
      m_voices.erase(m_voices.begin());
    m_voices.push_back(Voice{std::move(sound), 0.0, gain});
  }

  std::vector<Mod> m_mods;
  std::vector<Voice> m_voices;
};
}  // namespace moderngekko::detail
