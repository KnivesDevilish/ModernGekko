#pragma once

// The parts of a screenshot that need nothing from Dolphin
// (src/runtime/screenshot.cpp takes it), so tests/screenshot_steps_test.cpp
// can check them: the internal resolution it is taken at, when it is asked
// for while the game draws, and whether the picture is on disk yet.
//
// Dolphin saves a screenshot from the picture the game copied out for the
// screen (its XFB), at the resolution the game was drawn at. So for a sharper
// picture the game is drawn at a higher internal resolution for a few frames:
// the new resolution starts after the frame being drawn ends, the game's own
// effects that reuse the last frame (blur, glow) need another one to catch up,
// and the screen may still be showing the frame before the last one.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace moderngekko::detail
{
// The internal resolution scale a screenshot is taken at: the one asked for,
// or the game's own when that is already as high. Dolphin's scale 0 is "as
// big as the window".
inline int ScreenshotScale(int game_scale, int asked)
{
  return std::max(game_scale, asked);
}

// Follows the frames the game draws while a screenshot is taken. OnFrame is
// told, at the end of each one, the scale the video backend draws at now and
// whether Dolphin still has a screenshot to take; it says what to do then.
class ScreenshotFrames
{
public:
  enum class Action
  {
    Wait,
    Request,  // ask Dolphin for the screenshot now
    Done,     // Dolphin took it: the resolution can go back
    GiveUp,   // too many frames went by
  };

  // A raised resolution needs FRAMES_AT_SCALE frames drawn at it: the first
  // may have started before the change, and one more lets the game's effects
  // and the screen catch up. At the game's own one frame is enough.
  static constexpr int FRAMES_AT_SCALE = 4;
  // About ten seconds of the game's frames.
  static constexpr int GIVE_UP_AFTER = 600;

  ScreenshotFrames(int scale, bool raised)
      : m_scale(scale), m_needed(raised ? FRAMES_AT_SCALE : 1)
  {
  }

  Action OnFrame(int active_scale, bool request_pending)
  {
    if (m_step == Step::Over)
      return Action::Wait;
    if (m_step == Step::Drawing)
    {
      if (active_scale == m_scale && ++m_at_scale >= m_needed)
      {
        m_step = Step::Saving;
        return Action::Request;
      }
    }
    else if (!request_pending)
    {
      m_step = Step::Over;
      return Action::Done;
    }
    if (++m_frames >= GIVE_UP_AFTER)
    {
      m_step = Step::Over;
      return Action::GiveUp;
    }
    return Action::Wait;
  }

private:
  enum class Step
  {
    Drawing,
    Saving,
    Over,
  };

  int m_scale;
  int m_needed;
  int m_at_scale = 0;
  int m_frames = 0;
  Step m_step = Step::Drawing;
};

// A screenshot's file name: the name chosen for them (the game's, such as
// "SR-ZGS"; letters, digits, "-" and "_" kept) and the next number after the
// highest already in the folder, four digits at least: "SR-ZGS_0001.png".
// Numbers taken out of the middle are not given again, so a new screenshot
// always sorts after the others.
inline std::string NextScreenshotName(const std::filesystem::path& folder, std::string_view name)
{
  std::string prefix;
  for (const char c : name)
  {
    const bool kept = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    prefix += kept ? c : '_';
  }
  if (prefix.empty())
    prefix = "Screenshot";
  unsigned long highest = 0;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
  {
    const std::string file = it->path().filename().string();
    if (file.size() <= prefix.size() + 5 || !file.starts_with(prefix + "_") ||
        !file.ends_with(".png"))
      continue;
    const std::string_view digits =
        std::string_view(file).substr(prefix.size() + 1, file.size() - prefix.size() - 5);
    if (digits.empty() || digits.size() > 9 ||
        !std::all_of(digits.begin(), digits.end(),
                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
      continue;
    highest = std::max(highest, std::stoul(std::string(digits)));
  }
  char number[16];
  std::snprintf(number, sizeof(number), "%04lu", highest + 1);
  return prefix + "_" + number + ".png";
}

struct PngSize
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// The size of a PNG once it is all written: Dolphin writes a screenshot on a
// thread of its own after taking it, and the file ends with the IEND chunk
// only when that is done. Nothing while it is missing or still being written.
inline std::optional<PngSize> FinishedPngSize(const std::filesystem::path& path)
{
  constexpr unsigned char SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  constexpr unsigned char IEND[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return std::nullopt;
  unsigned char head[24] = {};
  if (!file.read(reinterpret_cast<char*>(head), sizeof(head)) ||
      !std::equal(std::begin(SIGNATURE), std::end(SIGNATURE), head) ||
      !std::equal(head + 12, head + 16, "IHDR"))
    return std::nullopt;
  file.seekg(-static_cast<std::streamoff>(sizeof(IEND)), std::ios::end);
  unsigned char tail[sizeof(IEND)] = {};
  if (!file.read(reinterpret_cast<char*>(tail), sizeof(tail)) ||
      !std::equal(std::begin(IEND), std::end(IEND), tail))
    return std::nullopt;
  const auto big_endian = [](const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
  };
  return PngSize{big_endian(head + 16), big_endian(head + 20)};
}
}  // namespace moderngekko::detail
