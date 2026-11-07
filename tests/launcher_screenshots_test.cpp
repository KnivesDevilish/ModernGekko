// The launcher's Screenshots page (tools/launcher_screenshots.hpp): only
// PNGs, newest first; when each was taken, from its file; a PNG's size from
// its header; pictures fitted to a box and halved before.
#include "launcher_screenshots.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace moderngekko::frontend;

void Write(const fs::path& path, const std::string& text, fs::file_time_type time)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
  fs::last_write_time(path, time);
}

std::string Png(unsigned width, unsigned height)
{
  std::string bytes = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n', 0, 0, 0, 13,
                       'I',    'H', 'D', 'R'};
  for (const unsigned value : {width, height})
  {
    for (int shift = 24; shift >= 0; shift -= 8)
      bytes += static_cast<char>((value >> shift) & 0xFF);
  }
  return bytes + std::string(20, '\0');
}
}  // namespace

int main()
{
#ifndef _WIN32
  setenv("TZ", "UTC", 1);
  tzset();
#endif
  const fs::path root = fs::temp_directory_path() / "moderngekko_launcher_screenshots_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  const auto now = fs::file_time_type::clock::now();
  const fs::path folder = root / "RS9E8P";
  Write(folder / "RS9E8P_2026-11-07_08-00-00.png", Png(4224, 3168), now - std::chrono::hours(2));
  Write(folder / "RS9E8P_2026-11-07_09-00-00.png", Png(1280, 720), now - std::chrono::hours(1));
  Write(folder / "RS9E8P_2026-11-07_09-00-00_1.png", Png(1280, 720), now - std::chrono::hours(1));
  Write(folder / "Notes.txt", "not a picture", now);
  Write(folder / "OLD.PNG", Png(640, 480), now - std::chrono::hours(3));
  Write(folder / "sub" / "RS9E8P_2026-11-08_10-00-00.png", Png(10, 10), now);

  const auto files = ListScreenshots(folder);
  if (files.size() != 4 || files[0].path.filename() != "RS9E8P_2026-11-07_09-00-00_1.png" ||
      files[1].path.filename() != "RS9E8P_2026-11-07_09-00-00.png" ||
      files[2].path.filename() != "RS9E8P_2026-11-07_08-00-00.png" ||
      files[3].path.filename() != "OLD.PNG" || files[2].size != Png(1, 1).size())
    return 121;
  const auto all = ListScreenshots(folder, true);
  if (all.size() != 5 || all[0].path.filename() != "RS9E8P_2026-11-08_10-00-00.png")
    return 122;
  if (!ListScreenshots(root / "missing").empty())
    return 123;

  // When each was taken, from its file's time, in the computer's time zone
  // (here UTC).
  {
    const auto taken = std::chrono::clock_cast<fs::file_time_type::clock>(
        std::chrono::system_clock::from_time_t(1794054896));
    if (ScreenshotTimeLabel(taken) != "2026-11-07 12:34")
      return 124;
  }

  const auto size = PngHeaderSize(files[2].path);
  if (!size || size->width != 4224 || size->height != 3168 || PngHeaderSize(folder / "Notes.txt") ||
      PngHeaderSize(root / "missing.png"))
    return 125;

  // A 4:3 picture in a 16:9 box: as tall as the box; a small one stays small
  // when it may not grow.
  const auto fitted = FitPicture(4224, 3168, 1600, 900);
  const auto small = FitPicture(320, 240, 1600, 900, true);
  const auto grown = FitPicture(320, 240, 1600, 900);
  if (fitted.width != 1200 || fitted.height != 900 || small.width != 320 || small.height != 240 ||
      grown.width != 1200 || grown.height != 900 || FitPicture(0, 10, 10, 10).width != 0)
    return 126;
  // 4224x3168 to a 400x300 thumbnail: 2112, 1056, 528, then scaled.
  if (HalvingSteps(4224, 3168, {400, 300}) != 3 || HalvingSteps(800, 600, {400, 300}) != 1 ||
      HalvingSteps(799, 600, {400, 300}) != 0 || HalvingSteps(100, 100, {400, 300}) != 0)
    return 127;
  if (FileSizeText(850 * 1000) != "850 KB" || FileSizeText(2'400'000) != "2.4 MB" ||
      FileSizeText(1) != "1 KB")
    return 128;
  fs::remove_all(root, ec);
  std::puts("launcher screenshots: ok");
  return 0;
}
