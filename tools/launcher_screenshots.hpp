#pragma once

// The parts of the launcher's Screenshots page that need neither SDL nor
// ImGui (the page itself is tools/launcher_gallery.hpp), so
// tests/launcher_screenshots_test.cpp can check them: the screenshots in a
// folder, newest first; when each was taken; a PNG's size from its header;
// and the size a picture is shown at.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace moderngekko::frontend
{
struct ScreenshotFile
{
  std::filesystem::path path;
  std::filesystem::file_time_type time;
  std::uintmax_t size = 0;
};

// The PNG files in folder (and its folders when recursive), newest first;
// two from the same moment by name, the later one first.
inline std::vector<ScreenshotFile> ListScreenshots(const std::filesystem::path& folder,
                                                   bool recursive = false)
{
  namespace fs = std::filesystem;
  std::vector<ScreenshotFile> files;
  std::error_code ec;
  const auto add = [&](const fs::directory_entry& entry) {
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec))
      return;
    std::string extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".png")
      return;
    ScreenshotFile file;
    file.path = entry.path();
    file.time = entry.last_write_time(entry_ec);
    file.size = entry.file_size(entry_ec);
    files.push_back(std::move(file));
  };
  if (recursive)
  {
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
      add(*it);
  }
  else
  {
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
      add(*it);
  }
  std::sort(files.begin(), files.end(), [](const ScreenshotFile& a, const ScreenshotFile& b) {
    if (a.time != b.time)
      return a.time > b.time;
    return a.path.filename() > b.path.filename();
  });
  return files;
}

// When a screenshot was taken, from its file's time, as "2026-11-07 08:09"
// in the computer's time zone: names need not hold the date (SR-Source's are
// SR-Source_0001.png and on).
inline std::string ScreenshotTimeLabel(std::filesystem::file_time_type time)
{
  const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
  const std::time_t seconds = std::chrono::system_clock::to_time_t(system);
  std::tm local{};
#ifdef _WIN32
  if (localtime_s(&local, &seconds) != 0)
    return {};
#else
  if (!localtime_r(&seconds, &local))
    return {};
#endif
  char text[32];
  if (std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local) == 0)
    return {};
  return text;
}

struct PictureSize
{
  int width = 0;
  int height = 0;
};

// A PNG's width and height, from its header; nothing for anything else.
inline std::optional<PictureSize> PngHeaderSize(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  unsigned char head[24] = {};
  if (!file.read(reinterpret_cast<char*>(head), sizeof(head)))
    return std::nullopt;
  constexpr unsigned char SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (!std::equal(std::begin(SIGNATURE), std::end(SIGNATURE), head) ||
      !std::equal(head + 12, head + 16, "IHDR"))
    return std::nullopt;
  const auto big_endian = [](const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
  };
  const std::uint32_t width = big_endian(head + 16);
  const std::uint32_t height = big_endian(head + 20);
  if (width == 0 || height == 0 || width > 1u << 16 || height > 1u << 16)
    return std::nullopt;
  return PictureSize{static_cast<int>(width), static_cast<int>(height)};
}

// The largest size a width x height picture takes inside a box, keeping its
// shape; never larger than the picture itself when no_bigger.
inline PictureSize FitPicture(int width, int height, int box_width, int box_height,
                              bool no_bigger = false)
{
  if (width <= 0 || height <= 0 || box_width <= 0 || box_height <= 0)
    return {};
  double scale = std::min(static_cast<double>(box_width) / width,
                          static_cast<double>(box_height) / height);
  if (no_bigger)
    scale = std::min(scale, 1.0);
  return {std::max(1, static_cast<int>(width * scale)),
          std::max(1, static_cast<int>(height * scale))};
}

// How many times a picture is halved before its last scaling to fit a box:
// while both of its sides stay at least twice the size wanted. Halving a
// picture averages each 2x2 square, so a big screenshot shrinks smoothly.
inline int HalvingSteps(int width, int height, PictureSize wanted)
{
  int steps = 0;
  while (wanted.width > 0 && wanted.height > 0 && width >= wanted.width * 2 &&
         height >= wanted.height * 2)
  {
    width /= 2;
    height /= 2;
    ++steps;
  }
  return steps;
}

// A file size in a short form: "850 KB", "2.4 MB".
inline std::string FileSizeText(std::uintmax_t bytes)
{
  char text[32];
  if (bytes < 1000 * 1000)
    std::snprintf(text, sizeof(text), "%llu KB",
                  static_cast<unsigned long long>((bytes + 999) / 1000));
  else
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1000.0 * 1000.0));
  return text;
}
}  // namespace moderngekko::frontend
