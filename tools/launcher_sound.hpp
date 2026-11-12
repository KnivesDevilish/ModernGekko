// Short sounds the launcher plays over its music (the game selection screen's:
// moving, choosing, going back): a WAV file's 16-bit PCM, read whole, and
// made into the music's own rate and channels when it plays
// (MusicPlayer::PlayEffect, tools/launcher_music.hpp). Apart from SDL and the
// audio output, so tests/launcher_sound_test.cpp can check it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace moderngekko::frontend
{
struct SoundEffect
{
  int rate = 0;
  int channels = 0;                  // 1 or 2
  std::vector<std::int16_t> samples;  // interleaved, frames x channels
};

// A RIFF WAVE file of 16-bit PCM, mono or stereo; nothing for anything else.
inline std::optional<SoundEffect> ParseWav(const std::vector<unsigned char>& bytes)
{
  const auto u16 = [&](std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at] | bytes[at + 1] << 8);
  };
  const auto u32 = [&](std::size_t at) { return u16(at) | u16(at + 2) << 16; };
  const auto tag = [&](std::size_t at, std::string_view name) {
    return std::equal(name.begin(), name.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at));
  };
  if (bytes.size() < 12 || !tag(0, "RIFF") || !tag(8, "WAVE"))
    return std::nullopt;
  SoundEffect sound;
  bool has_format = false;
  std::size_t at = 12;
  while (at + 8 <= bytes.size())
  {
    const std::size_t size = u32(at + 4);
    const std::size_t body = at + 8;
    if (size > bytes.size() - body)
      return std::nullopt;
    if (tag(at, "fmt ") && size >= 16)
    {
      const std::uint32_t format = u16(body);
      const std::uint32_t bits = u16(body + 14);
      sound.channels = static_cast<int>(u16(body + 2));
      sound.rate = static_cast<int>(u32(body + 4));
      // PCM, or WAVE_FORMAT_EXTENSIBLE carrying it; 16 bits.
      if ((format != 1 && format != 0xFFFE) || bits != 16 || sound.channels < 1 ||
          sound.channels > 2 || sound.rate < 8000 || sound.rate > 192000)
        return std::nullopt;
      has_format = true;
    }
    else if (tag(at, "data") && has_format)
    {
      const std::size_t count = size / 2;
      sound.samples.resize(count - count % static_cast<std::size_t>(sound.channels));
      for (std::size_t i = 0; i < sound.samples.size(); ++i)
        sound.samples[i] = static_cast<std::int16_t>(u16(body + i * 2));
      return sound.samples.empty() ? std::nullopt : std::optional(std::move(sound));
    }
    at = body + size + (size & 1);  // chunks keep an even length
  }
  return std::nullopt;
}

inline std::shared_ptr<const SoundEffect> LoadSoundEffect(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return nullptr;
  const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file),
                                         std::istreambuf_iterator<char>()};
  auto sound = ParseWav(bytes);
  return sound ? std::make_shared<const SoundEffect>(std::move(*sound)) : nullptr;
}

// The sound at another rate and channel count (1 or 2), as the music plays:
// straight lines between the samples, both sides averaged for mono.
inline std::vector<std::int16_t> ConvertSound(const SoundEffect& sound, int rate, int channels)
{
  std::vector<std::int16_t> out;
  if (sound.rate <= 0 || sound.channels <= 0 || rate <= 0 || channels < 1 || channels > 2)
    return out;
  const std::size_t frames = sound.samples.size() / static_cast<std::size_t>(sound.channels);
  if (frames == 0)
    return out;
  const auto sample = [&](std::size_t frame, int channel) {
    const int c = std::min(channel, sound.channels - 1);
    return static_cast<double>(sound.samples[frame * static_cast<std::size_t>(sound.channels) +
                                             static_cast<std::size_t>(c)]);
  };
  const auto frames_out = static_cast<std::size_t>(
      static_cast<double>(frames) * rate / sound.rate);
  out.reserve(frames_out * static_cast<std::size_t>(channels));
  const double step = static_cast<double>(sound.rate) / rate;
  for (std::size_t i = 0; i < frames_out; ++i)
  {
    const double position = static_cast<double>(i) * step;
    const std::size_t first = std::min(static_cast<std::size_t>(position), frames - 1);
    const std::size_t next = std::min(first + 1, frames - 1);
    const double t = position - static_cast<double>(first);
    for (int c = 0; c < channels; ++c)
    {
      double value;
      if (channels == 1 && sound.channels == 2)
        value = ((sample(first, 0) + sample(first, 1)) * (1.0 - t) +
                 (sample(next, 0) + sample(next, 1)) * t) *
                0.5;
      else
        value = sample(first, c) * (1.0 - t) + sample(next, c) * t;
      out.push_back(static_cast<std::int16_t>(std::clamp(value, -32768.0, 32767.0)));
    }
  }
  return out;
}

// Adds one sound's next samples into a block of the music, clipping at the
// 16-bit range. Returns how many it added.
inline std::size_t MixInto(std::int16_t* block, std::size_t count, const std::int16_t* sound,
                           std::size_t remaining, float gain)
{
  const std::size_t n = std::min(count, remaining);
  for (std::size_t i = 0; i < n; ++i)
  {
    const float mixed = static_cast<float>(block[i]) + static_cast<float>(sound[i]) * gain;
    block[i] = static_cast<std::int16_t>(std::clamp(mixed, -32768.0f, 32767.0f));
  }
  return n;
}
}  // namespace moderngekko::frontend
