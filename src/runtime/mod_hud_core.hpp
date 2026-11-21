#pragma once

// The parts of the mods' charge meters (mod_hud.hpp) that don't need Dolphin,
// so tests/mod_hud_test.cpp can check them: a mod's hud.ini, and what the
// meter shows from the words its code writes in the game's memory.
//
// hud.ini (in the mod's folder, next to patches.ini):
//
//   # Rows of four 32-bit words, one for each controller port (0 to 3):
//   heartbeat = 0x80B7FB00   # goes up each frame the mod's code runs
//   value = 0x80B7FB50       # how much is charged; 0 = not charging
//   # Words with the value each level starts at, from the first level up:
//   step = 0x80B7F010
//   step = 0x80B7F014
//   step = 0x80B7F018
//   # Optional: events (as in sounds.ini) that make the meter burst, as a
//   # release with the charge does.
//   event_count = 0x80B7FB20
//   event_id = 0x80B7FB30
//   burst_events = 5, 6, 7
//   # A word that only holds this value while the mod's code is in memory.
//   magic_address = 0x80B7FB40
//   magic_value = 0x534F4E44
//   # Which controller's meter, where and how big: the centre as fractions of
//   # the game's picture (0 to 1), the radius as a fraction of its height.
//   port = 0
//   x = 0.48
//   y = 0.60
//   size = 0.10
//   # The colour filling towards each level (as many as steps; #RRGGBB).
//   color = #19E6FF
//   color = #FF2BD6
//   color = #FFE23B
//
// The meter shows while its port charges (value > 0, the heartbeat going),
// filling up to each step; it flashes at each new level, and fades out when
// the charge ends.

#include "mod_sounds_core.hpp"  // mod_sounds_text: Trim, ParseWord, ParseNumber, GoodAddress

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::detail
{
inline constexpr std::size_t MOD_HUD_MAX_STEPS = 8;

struct ModHudConfig
{
  std::uint32_t heartbeat = 0;
  std::uint32_t value = 0;
  std::vector<std::uint32_t> steps;  // addresses of the words with each level's value
  std::uint32_t event_count = 0;     // 0: no bursts
  std::uint32_t event_id = 0;
  std::vector<std::uint32_t> burst_events;
  std::uint32_t magic_address = 0;   // 0: none
  std::uint32_t magic_value = 0;
  int port = 0;
  double x = 0.48;
  double y = 0.60;
  double size = 0.10;
  std::vector<std::uint32_t> colors;  // 0xRRGGBB, one per step
};

inline std::optional<std::uint32_t> ParseColor(std::string_view text)
{
  std::string t = mod_sounds_text::Trim(text);
  if (!t.empty() && t[0] == '#')
    t = t.substr(1);
  if (t.size() != 6)
    return std::nullopt;
  return mod_sounds_text::ParseWord("0x" + t);
}

// Reads hud.ini. Lines it doesn't understand go to `problems`; nullopt when
// what is left can't work.
inline std::optional<ModHudConfig> ParseHudIni(std::string_view text,
                                               std::vector<std::string>* problems)
{
  using namespace mod_sounds_text;
  ModHudConfig config;
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
    const auto problem = [&](const std::string& why) {
      problems->push_back("line " + std::to_string(line_number) + ": " + why);
    };
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos)
    {
      problem("no '=' in \"" + line + "\"");
      continue;
    }
    std::string key = Trim(std::string_view(line).substr(0, equals));
    std::string value(std::string_view(line).substr(equals + 1));
    for (std::size_t i = 1; i < value.size(); ++i)
    {
      // A comment after a space; a colour's '#' comes right after '=' or a space too,
      // so only " #" followed by a space or the end starts one.
      if ((value[i] == ';' || (value[i] == '#' && (i + 1 == value.size() || value[i + 1] == ' '))) &&
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
    const auto fraction = [&](double* out, double low, double high) {
      const auto number = ParseNumber(value);
      if (!number || *number < low || *number > high)
        problem(key + ": \"" + value + "\" is not a number from " + std::to_string(low) + " to " +
                std::to_string(high));
      else
        *out = *number;
    };
    if (key == "heartbeat")
      address(&config.heartbeat);
    else if (key == "value")
      address(&config.value);
    else if (key == "step")
    {
      std::uint32_t step = 0;
      address(&step);
      if (step != 0 && config.steps.size() < MOD_HUD_MAX_STEPS)
        config.steps.push_back(step);
      else if (step != 0)
        problem("more than " + std::to_string(MOD_HUD_MAX_STEPS) + " steps");
    }
    else if (key == "event_count")
      address(&config.event_count);
    else if (key == "event_id")
      address(&config.event_id);
    else if (key == "burst_events")
    {
      std::size_t from = 0;
      while (from <= value.size())
      {
        std::size_t comma = value.find(',', from);
        if (comma == std::string::npos)
          comma = value.size();
        const auto number = ParseWord(std::string_view(value).substr(from, comma - from));
        if (number)
          config.burst_events.push_back(*number);
        else
          problem("burst_events: \"" + value + "\" is not a list of numbers");
        from = comma + 1;
      }
    }
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
    else if (key == "port")
    {
      double port = 0;
      fraction(&port, 0, 3);
      config.port = static_cast<int>(port);
    }
    else if (key == "x")
      fraction(&config.x, 0.0, 1.0);
    else if (key == "y")
      fraction(&config.y, 0.0, 1.0);
    else if (key == "size")
      fraction(&config.size, 0.01, 0.5);
    else if (key == "color")
    {
      if (const auto color = ParseColor(value))
        config.colors.push_back(*color);
      else
        problem("color: \"" + value + "\" is not a colour as #RRGGBB");
    }
    else
      problem("unknown key \"" + key + "\"");
  }
  bool ok = true;
  if (config.heartbeat == 0 || config.value == 0)
  {
    problems->push_back("heartbeat and value are needed");
    ok = false;
  }
  if (config.steps.empty())
  {
    problems->push_back("no steps");
    ok = false;
  }
  if (config.magic_address != 0 && !have_magic_value)
  {
    problems->push_back("magic_address without magic_value");
    ok = false;
  }
  if ((config.event_count == 0) != (config.event_id == 0))
  {
    problems->push_back("event_count and event_id go together");
    ok = false;
  }
  if (!ok)
    return std::nullopt;
  // Colours: the ones missing, from a cyan-magenta-yellow set.
  static constexpr std::uint32_t DEFAULTS[] = {0x19E6FF, 0xFF2BD6, 0xFFE23B, 0x7CFF4F,
                                               0xFF7A1A, 0x8F6BFF, 0xFFFFFF, 0x19E6FF};
  while (config.colors.size() < config.steps.size())
    config.colors.push_back(DEFAULTS[config.colors.size() % std::size(DEFAULTS)]);
  config.colors.resize(config.steps.size());
  return config;
}

// What the meter shows in one frame.
struct GaugeLook
{
  float alpha = 0;       // 0: hidden; 1: fully shown
  int level = 0;         // levels reached (0 to steps)
  int levels = 0;        // how many there are
  float progress = 0;    // towards the next level, 0 to 1 (1 at the last)
  float flash = 0;       // 1 just after a new level, down to 0
  float burst = 0;       // 1 just after a burst event, down to 0
  int burst_level = 0;   // the level the burst came at
  double time = 0;       // seconds, for things that pulse
};

// The meter of one mod: follows its words, and says what to draw.
class ModGauge
{
public:
  using ReadWord = std::function<std::optional<std::uint32_t>(std::uint32_t address)>;

  static constexpr double ALIVE_SECONDS = 0.15;
  static constexpr double FADE_IN_SECONDS = 0.08;
  static constexpr double FADE_OUT_SECONDS = 0.30;
  static constexpr double FLASH_SECONDS = 0.30;
  static constexpr double BURST_SECONDS = 0.40;

  explicit ModGauge(ModHudConfig config) : m_config(std::move(config)) {}
  const ModHudConfig& Config() const { return m_config; }

  // Reads the words; `now` in seconds, from any start, going up.
  void Update(const ReadWord& read, double now)
  {
    const ModHudConfig& c = m_config;
    const std::uint32_t offset = 4u * static_cast<std::uint32_t>(c.port);
    const double dt = m_last_now < 0 ? 0.0 : std::clamp(now - m_last_now, 0.0, 0.25);
    m_last_now = now;
    const bool here = c.magic_address == 0 || read(c.magic_address) == std::optional{c.magic_value};
    bool charging = false;
    if (!here)
    {
      m_seen = false;
      m_counted = false;
    }
    else
    {
      if (const auto beat = read(c.heartbeat + offset))
      {
        if (m_seen && *beat != m_last_beat)
          m_alive_at = now;
        m_last_beat = *beat;
        m_seen = true;
      }
      const bool alive = m_seen && m_alive_at >= 0 && now - m_alive_at < ALIVE_SECONDS;
      const auto value = read(c.value + offset);
      charging = alive && value && *value > 0;
      if (charging)
      {
        int level = 0;
        std::uint32_t below = 0;
        std::uint32_t above = 0;
        for (std::size_t k = 0; k < c.steps.size(); ++k)
        {
          const std::uint32_t step = read(c.steps[k]).value_or(0xFFFFFFFFu);
          if (*value >= step)
          {
            level = static_cast<int>(k) + 1;
            below = step;
          }
          else
          {
            above = step;
            break;
          }
        }
        const int levels = static_cast<int>(c.steps.size());
        float progress = 1.0f;
        if (level < levels)
          progress = above > below ? static_cast<float>(*value - below) / (above - below) : 0.0f;
        if (level > m_level && m_alpha > 0.0f)
          m_flash_at = now;
        m_level = level;
        m_progress = std::clamp(progress, 0.0f, 1.0f);
      }
      if (c.event_count != 0)
      {
        if (const auto count = read(c.event_count + offset))
        {
          if (m_counted && *count != m_last_count && alive)
          {
            const auto id = read(c.event_id + offset);
            if (id && std::find(c.burst_events.begin(), c.burst_events.end(), *id) !=
                          c.burst_events.end())
            {
              m_burst_at = now;
              m_burst_level = m_level;
            }
          }
          m_last_count = *count;
          m_counted = true;
        }
      }
    }
    if (charging)
      m_alpha = std::min(1.0f, m_alpha + static_cast<float>(dt / FADE_IN_SECONDS));
    else
      m_alpha = std::max(0.0f, m_alpha - static_cast<float>(dt / FADE_OUT_SECONDS));
    if (m_alpha <= 0.0f && !charging)
    {
      m_level = 0;
      m_progress = 0.0f;
    }
    m_now = now;
  }

  GaugeLook Look() const
  {
    GaugeLook look;
    look.alpha = m_alpha;
    look.level = m_level;
    look.levels = static_cast<int>(m_config.steps.size());
    look.progress = m_progress;
    if (m_flash_at >= 0 && m_now - m_flash_at < FLASH_SECONDS)
      look.flash = static_cast<float>(1.0 - (m_now - m_flash_at) / FLASH_SECONDS);
    if (m_burst_at >= 0 && m_now - m_burst_at < BURST_SECONDS)
    {
      look.burst = static_cast<float>(1.0 - (m_now - m_burst_at) / BURST_SECONDS);
      look.burst_level = m_burst_level;
    }
    look.time = m_now;
    return look;
  }

private:
  ModHudConfig m_config;
  double m_last_now = -1.0;
  double m_now = 0.0;
  bool m_seen = false;
  std::uint32_t m_last_beat = 0;
  double m_alive_at = -1.0;
  bool m_counted = false;
  std::uint32_t m_last_count = 0;
  int m_level = 0;
  float m_progress = 0.0f;
  float m_alpha = 0.0f;
  double m_flash_at = -1.0;
  double m_burst_at = -1.0;
  int m_burst_level = 0;
};
}  // namespace moderngekko::detail
