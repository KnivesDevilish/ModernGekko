// The steps of a screenshot (src/runtime/screenshot_steps.hpp): never below
// the game's resolution, asked for only after enough frames at the raised
// one, done when Dolphin has taken it, given up after a long wait, and a PNG
// counts as saved only once it is written to the end.
#include "runtime/screenshot_steps.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
using moderngekko::detail::FinishedPngSize;
using moderngekko::detail::ScreenshotFrames;
using moderngekko::detail::ScreenshotScale;
using Action = ScreenshotFrames::Action;

// The start and end of a PNG as Dolphin writes one, for a 4224x3012 picture
// (the checksums aside): signature, IHDR, IDAT, IEND.
const std::vector<unsigned char> PNG = {
    0x89, 'P',  'N',  'G',  '\r', '\n', 0x1A, '\n', 0x00, 0x00, 0x00, 0x0D, 'I',  'H',  'D',
    'R',  0x00, 0x00, 0x10, 0x80, 0x00, 0x00, 0x0B, 0xC4, 0x08, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 'I',  'D',  'A',  'T',  0x08, 0xD7, 0x63, 0xF8,
    0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 'I',  'E',  'N',  'D',  0xAE, 0x42, 0x60, 0x82};

void Write(const std::filesystem::path& path, std::size_t bytes)
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(PNG.data()), static_cast<std::streamsize>(bytes));
}
}  // namespace

int main()
{
  if (ScreenshotScale(2, 6) != 6 || ScreenshotScale(8, 6) != 8 || ScreenshotScale(3, 0) != 3 ||
      ScreenshotScale(0, 6) != 6)
    return 81;
  {
    // Raised: the frames at the old scale don't count; the fourth at the
    // new one asks for the picture, and it is done once Dolphin took it.
    ScreenshotFrames frames(6, true);
    if (frames.OnFrame(2, false) != Action::Wait || frames.OnFrame(2, false) != Action::Wait)
      return 82;
    for (int i = 0; i < ScreenshotFrames::FRAMES_AT_SCALE - 1; ++i)
    {
      if (frames.OnFrame(6, false) != Action::Wait)
        return 83;
    }
    if (frames.OnFrame(6, false) != Action::Request)
      return 84;
    if (frames.OnFrame(6, true) != Action::Wait || frames.OnFrame(6, true) != Action::Wait ||
        frames.OnFrame(6, false) != Action::Done || frames.OnFrame(6, false) != Action::Wait)
      return 85;
  }
  {
    // At the game's own resolution, the next frame asks for it.
    ScreenshotFrames frames(3, false);
    if (frames.OnFrame(3, false) != Action::Request || frames.OnFrame(3, false) != Action::Done)
      return 86;
  }
  {
    // A resolution that never takes, or a picture never taken: given up
    // once, then nothing more.
    ScreenshotFrames never(6, true);
    for (int i = 1; i < ScreenshotFrames::GIVE_UP_AFTER; ++i)
    {
      if (never.OnFrame(2, false) != Action::Wait)
        return 87;
    }
    if (never.OnFrame(2, false) != Action::GiveUp || never.OnFrame(6, false) != Action::Wait)
      return 88;
    ScreenshotFrames stuck(3, false);
    stuck.OnFrame(3, false);
    int frame = 1;
    while (stuck.OnFrame(3, true) == Action::Wait && frame < 10 * ScreenshotFrames::GIVE_UP_AFTER)
      ++frame;
    if (frame >= ScreenshotFrames::GIVE_UP_AFTER + 1 || stuck.OnFrame(3, false) != Action::Wait)
      return 89;
  }
  {
    const std::filesystem::path png =
        std::filesystem::temp_directory_path() / "moderngekko_screenshot_steps_test.png";
    std::error_code ec;
    std::filesystem::remove(png, ec);
    if (FinishedPngSize(png))
      return 90;
    // Still being written: no IEND at the end yet.
    Write(png, 30);
    if (FinishedPngSize(png))
      return 91;
    Write(png, PNG.size() - 1);
    if (FinishedPngSize(png))
      return 92;
    Write(png, PNG.size());
    const auto size = FinishedPngSize(png);
    if (!size || size->width != 4224 || size->height != 3012)
      return 93;
    // Not a PNG at all.
    std::ofstream(png, std::ios::trunc) << "not a picture, but long enough to have a tail";
    if (FinishedPngSize(png))
      return 94;
    std::filesystem::remove(png, ec);
  }
  std::puts("screenshot steps: ok");
  return 0;
}
