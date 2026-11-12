#include "launcher_preview_video.hpp"

#include <cstddef>
#include <cstdio>

#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <map>
#include <string>

namespace moderngekko::frontend
{
namespace
{
std::uint32_t Little32(const unsigned char* bytes)
{
  return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8 |
         static_cast<std::uint32_t>(bytes[2]) << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
}

bool SameName(std::string_view a, std::string_view b)
{
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::toupper(static_cast<unsigned char>(x)) ==
                  std::toupper(static_cast<unsigned char>(y));
         });
}

std::FILE* OpenForReading(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return _wfopen(path.c_str(), L"rb");
#else
  return std::fopen(path.c_str(), "rb");
#endif
}

bool SeekTo(std::FILE* file, std::uint64_t position)
{
#if defined(_WIN32)
  return _fseeki64(file, static_cast<long long>(position), SEEK_SET) == 0;
#else
  return fseeko(file, static_cast<off_t>(position), SEEK_SET) == 0;
#endif
}

// pl_mpeg reads the video through these, as if its bytes were a whole file.
struct Reader
{
  std::FILE* file = nullptr;
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
  std::uint64_t position = 0;
};

void LoadBytes(plm_buffer_t* buffer, void* user)
{
  Reader& reader = *static_cast<Reader*>(user);
  if (buffer->discard_read_bytes)
    plm_buffer_discard_read_bytes(buffer);
  const std::uint64_t left = reader.length - std::min(reader.position, reader.length);
  const std::size_t wanted =
      static_cast<std::size_t>(std::min<std::uint64_t>(buffer->capacity - buffer->length, left));
  const std::size_t read =
      wanted ? std::fread(buffer->bytes + buffer->length, 1, wanted, reader.file) : 0;
  buffer->length += read;
  reader.position += read;
  if (read == 0)
    buffer->has_ended = TRUE;
}

void SeekBytes(plm_buffer_t*, std::size_t offset, void* user)
{
  Reader& reader = *static_cast<Reader*>(user);
  reader.position = std::min<std::uint64_t>(offset, reader.length);
  SeekTo(reader.file, reader.offset + reader.position);
}

std::size_t TellBytes(plm_buffer_t*, void* user)
{
  return static_cast<std::size_t>(static_cast<Reader*>(user)->position);
}

// pl_mpeg's plm_create_with_buffer, for the picture only. pl_mpeg believes
// the stream's first system header, and in Sofdec's videos it can list only
// the audio: Zero Gravity's OPENING.SFD says 4 audio streams and no video.
// So there is one video stream, always, and no audio: the games' sound is
// ADX, which isn't MPEG audio, and the intros play silent over the
// launcher's music.
plm_t* CreatePictureOnly(plm_buffer_t* buffer)
{
  plm_t* self = static_cast<plm_t*>(PLM_MALLOC(sizeof(plm_t)));
  std::memset(self, 0, sizeof(plm_t));
  self->demux = plm_demux_create(buffer, TRUE);
  self->video_enabled = TRUE;
  self->audio_enabled = FALSE;
  if (plm_demux_has_headers(self->demux))
  {
    self->demux->num_video_streams = 1;
    self->demux->num_audio_streams = 0;
  }
  plm_init_decoders(self);
  return self;
}

// TMPGEnc writes "\0TMPGEXS\0" in the user data of the videos it makes, and
// codes their intra DC with 11 bits, MPEG-2's way, though all the rest is
// MPEG-1. CRI's Sofdec videos (the games') are made with it, and decoded as
// plain MPEG-1 they come out as coloured noise. ffmpeg looks for the same
// mark ("tmpgexs" in its mpeg12dec.c). It's in the first pictures' user data.
bool HasTmpgexsMark(plm_t* plm)
{
  static constexpr std::array<std::uint8_t, 9> MARK = {0, 'T', 'M', 'P', 'G', 'E', 'X', 'S', 0};
  plm_demux_t* demux = plm->demux;
  plm_demux_buffer_seek(demux, 0);
  std::vector<std::uint8_t> window;
  for (int packets = 0; packets < 512;)
  {
    const plm_packet_t* packet = plm_demux_decode(demux);
    if (!packet)
      break;
    if (packet->type != plm->video_packet_type)
      continue;
    ++packets;
    window.insert(window.end(), packet->data, packet->data + packet->length);
    if (std::search(window.begin(), window.end(), MARK.begin(), MARK.end()) != window.end())
      return true;
    window.erase(window.begin(),
                 window.end() - static_cast<std::ptrdiff_t>(std::min<std::size_t>(8, window.size())));
  }
  return false;
}

// An intra frame: its number, counted as the frames show from the video's
// start (as ffmpeg counts them, which is how layout.txt's parts were found),
// and where it is: the demuxer position from which the next video packet is
// the one its picture starts in, and how far into that packet's data.
struct IntraFrame
{
  int number = 0;
  std::size_t position = 0;
  std::size_t skip = 0;
};

// The intra frames up to the group of pictures after frame `until`, read from
// the picture and group headers alone (nothing is decoded), numbered by
// counting the pictures of each group, not by time stamps.
std::vector<IntraFrame> IndexIntraFrames(plm_t* plm, int until, const std::atomic<bool>& stop)
{
  static constexpr std::array<std::uint8_t, 3> START = {0x00, 0x00, 0x01};
  std::vector<IntraFrame> found;
  plm_demux_t* demux = plm->demux;
  const int type = plm->video_packet_type;
  plm_demux_buffer_seek(demux, 0);
  std::vector<std::uint8_t> window;  // the last bytes of the packet before, then this one's
  std::size_t kept_from = 0;         // where the packet those last bytes came from is
  std::size_t kept_offset = 0;       // and how far into its data they start
  int pictures = 0;                  // pictures so far, in the file's order
  int group = 0;                     // the number of the first frame of this group
  while (!stop.load())
  {
    const std::size_t position = plm_buffer_tell(demux->buffer);
    const plm_packet_t* packet = plm_demux_decode(demux);
    if (!packet)
      break;
    if (packet->type != type)
      continue;
    const std::size_t kept = window.size();
    window.insert(window.end(), packet->data, packet->data + packet->length);
    for (std::size_t at = 0;;)
    {
      const std::size_t code = static_cast<std::size_t>(
          std::search(window.begin() + static_cast<std::ptrdiff_t>(at), window.end(),
                      START.begin(), START.end()) -
          window.begin());
      if (code + 6 > window.size())
        break;  // too close to the end: looked at again with the next packet
      if (window[code + 3] == 0xB8)  // group of pictures
      {
        group = pictures;
        if (group > until)
          return found;
      }
      else if (window[code + 3] == 0x00)  // picture
      {
        const int order = (window[code + 4] << 2) | (window[code + 5] >> 6);
        if (((window[code + 5] >> 3) & 7) == 1)
        {
          if (code < kept)
            found.push_back({group + order, kept_from, kept_offset + code});
          else
            found.push_back({group + order, position, code - kept});
        }
        ++pictures;
      }
      at = code + 3;
    }
    const std::size_t keep = std::min<std::size_t>(5, window.size());
    window.erase(window.begin(), window.end() - static_cast<std::ptrdiff_t>(keep));
    kept_from = position;
    kept_offset = packet->length - std::min<std::size_t>(keep, packet->length);
  }
  return found;
}

// The intra frames of a video, kept while the launcher runs: the intros
// replay, and finding them reads most of the file.
std::vector<IntraFrame> CachedIntraFrames(const VideoSource& source, plm_t* plm, int until,
                                          const std::atomic<bool>& stop)
{
  static std::mutex mutex;
  static std::map<std::string, std::vector<IntraFrame>> cache;
  const std::string key = source.file.string() + '|' + std::to_string(source.offset) + '|' +
                          std::to_string(source.length) + '|' + std::to_string(until);
  {
    std::lock_guard lock(mutex);
    if (const auto known = cache.find(key); known != cache.end())
      return known->second;
  }
  std::vector<IntraFrame> found = IndexIntraFrames(plm, until, stop);
  if (!stop.load() && !found.empty())
  {
    std::lock_guard lock(mutex);
    cache[key] = found;
  }
  return found;
}

// Like pl_mpeg's plm_seek_frame: jumps to an intra frame and returns it,
// decoded; the frames after it are the next numbers, one each. In an open
// group of pictures the B frames that come right after the intra one show
// before it and need the frame before it, which wasn't decoded: they are left
// out.
plm_frame_t* JumpTo(plm_t* plm, const IntraFrame& intra)
{
  plm_demux_t* demux = plm->demux;
  plm_demux_buffer_seek(demux, intra.position);
  const plm_packet_t* packet = nullptr;
  while ((packet = plm_demux_decode(demux)) && packet->type != plm->video_packet_type)
  {
  }
  if (!packet || intra.skip >= packet->length)
    return nullptr;
  plm_video_t* video = plm->video_decoder;
  plm_video_rewind(video);
  plm_buffer_write(plm->video_buffer, packet->data + intra.skip, packet->length - intra.skip);
  plm->has_ended = FALSE;
  for (int tries = 0; tries < 16; ++tries)
  {
    plm_frame_t* frame = plm_video_decode(video);
    if (!frame)
      return nullptr;
    if (video->picture_type == PLM_VIDEO_PICTURE_TYPE_B && frame == &video->frame_current &&
        !video->assume_no_b_frames)
      continue;  // a B frame from before the intra one
    return frame;
  }
  return nullptr;
}
}  // namespace

std::optional<std::pair<std::uint64_t, std::uint64_t>> FindInIso(std::istream& image,
                                                                 std::string_view name)
{
  constexpr std::size_t SECTOR = 2048;
  constexpr std::size_t LOOK = 256 * 1024;
  std::vector<unsigned char> head(LOOK);
  image.clear();
  image.seekg(0);
  image.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<std::size_t>(image.gcount()));
  // The primary volume descriptor, 16 sectors into the image.
  static constexpr std::array<unsigned char, 6> MARK = {0x01, 'C', 'D', '0', '0', '1'};
  const auto found = std::search(head.begin(), head.end(), MARK.begin(), MARK.end());
  const std::size_t mark = static_cast<std::size_t>(found - head.begin());
  if (found == head.end() || mark < 16 * SECTOR || mark + 190 > head.size())
    return std::nullopt;
  const std::uint64_t base = mark - 16 * SECTOR;
  const unsigned char* pvd = head.data() + mark;
  const std::uint64_t block = (pvd[128] | pvd[129] << 8) ? (pvd[128] | pvd[129] << 8) : SECTOR;
  // The folders still to look in: their first block and their length.
  std::vector<std::pair<std::uint64_t, std::uint64_t>> folders = {
      {Little32(pvd + 156 + 2), Little32(pvd + 156 + 10)}};
  int looked = 0;
  while (!folders.empty() && ++looked < 4096)
  {
    const auto [lba, length] = folders.back();
    folders.pop_back();
    if (length == 0 || length > 16 * 1024 * 1024)
      continue;
    std::vector<unsigned char> records(static_cast<std::size_t>(length));
    image.clear();
    image.seekg(static_cast<std::streamoff>(base + lba * block));
    image.read(reinterpret_cast<char*>(records.data()), static_cast<std::streamsize>(length));
    records.resize(static_cast<std::size_t>(image.gcount()));
    for (std::size_t at = 0; at < records.size();)
    {
      const std::size_t size = records[at];
      if (size == 0)  // the rest of this block is empty
      {
        at = (at / block + 1) * block;
        continue;
      }
      if (size < 34 || at + size > records.size())
        break;
      const unsigned char* record = records.data() + at;
      at += size;
      const std::size_t name_length = record[32];
      if (33 + name_length > size)
        continue;
      std::string_view entry(reinterpret_cast<const char*>(record + 33), name_length);
      if (entry == std::string_view("\0", 1) || entry == "\x01")
        continue;  // the folder itself and its parent
      if (const std::size_t semicolon = entry.find(';'); semicolon != std::string_view::npos)
        entry = entry.substr(0, semicolon);
      const std::uint64_t extent = Little32(record + 2);
      const std::uint64_t data_length = Little32(record + 10);
      if (record[25] & 0x02)
        folders.emplace_back(extent, data_length);
      else if (SameName(entry, name))
        return std::pair<std::uint64_t, std::uint64_t>(base + extent * block, data_length);
    }
  }
  return std::nullopt;
}

std::optional<VideoSource> FindVideoSource(const std::filesystem::path& root,
                                           std::string_view video)
{
  std::error_code ec;
  const std::size_t colon = video.find(':');
  const std::filesystem::path file = root / std::filesystem::path(std::string(video.substr(0, colon)));
  if (!std::filesystem::is_regular_file(file, ec))
    return std::nullopt;
  if (colon == std::string_view::npos)
  {
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec || size == 0)
      return std::nullopt;
    return VideoSource{file, 0, size};
  }
  std::ifstream image(file, std::ios::binary);
  const auto inside = FindInIso(image, video.substr(colon + 1));
  if (!inside || inside->second == 0)
    return std::nullopt;
  return VideoSource{file, inside->first, inside->second};
}

void PreviewVideo::Start(const VideoSource& source, std::vector<std::pair<int, int>> parts)
{
  Stop();
  m_thread = std::thread(&PreviewVideo::Run, this, source, std::move(parts));
}

void PreviewVideo::Stop()
{
  if (m_thread.joinable())
  {
    m_stop = true;
    m_room.notify_all();
    m_thread.join();
  }
  std::lock_guard lock(m_mutex);
  for (Frame& frame : m_frames)
    m_spare.push_back(std::move(frame.rgba));
  m_frames.clear();
  m_stop = false;
  m_ready = false;
  m_failed = false;
  m_width = 0;
  m_height = 0;
  m_frame_rate = 0.0;
}

bool PreviewVideo::Take(int index, std::vector<std::uint8_t>& rgba)
{
  std::lock_guard lock(m_mutex);
  std::optional<Frame> newest;
  while (!m_frames.empty() && m_frames.front().index <= index)
  {
    if (newest)
      m_spare.push_back(std::move(newest->rgba));
    newest = std::move(m_frames.front());
    m_frames.pop_front();
  }
  if (!newest)
    return false;
  if (!rgba.empty())
    m_spare.push_back(std::move(rgba));
  rgba = std::move(newest->rgba);
  m_room.notify_all();
  return true;
}

bool PreviewVideo::WaitForRoom()
{
  std::unique_lock lock(m_mutex);
  m_room.wait(lock, [&] { return m_stop.load() || m_frames.size() < AHEAD; });
  return !m_stop.load();
}

void PreviewVideo::Push(Frame frame)
{
  std::lock_guard lock(m_mutex);
  m_frames.push_back(std::move(frame));
}

void PreviewVideo::Run(VideoSource source, std::vector<std::pair<int, int>> parts)
{
  Reader reader{OpenForReading(source.file), source.offset, source.length, 0};
  if (!reader.file || !SeekTo(reader.file, source.offset))
  {
    if (reader.file)
      std::fclose(reader.file);
    m_failed = true;
    return;
  }
  plm_buffer_t* buffer = plm_buffer_create_with_callbacks(
      LoadBytes, SeekBytes, TellBytes, static_cast<std::size_t>(source.length), &reader);
  plm_t* plm = CreatePictureOnly(buffer);
  const double fps = plm_get_framerate(plm);
  const int width = plm_get_width(plm);
  const int height = plm_get_height(plm);
  bool any = false;
  if (fps > 0.0 && width > 0 && height > 0)
  {
    m_width = width;
    m_height = height;
    m_frame_rate = fps;
    int current = -1;  // the number of the frame last decoded, counted from 0
    int index = 0;     // where the part starts, counted along all the parts
    int until = 0;
    for (const auto& part : parts)
      until = std::max(until, part.first);
    if (HasTmpgexsMark(plm))
      plm->video_decoder->dc_precision_shift = 3;
    const std::vector<IntraFrame> intra = CachedIntraFrames(source, plm, until, m_stop);
    plm_rewind(plm);
    if (intra.empty())
      std::cout << "intro video: no intra frames found, playing it from the start\n";
    for (const auto& [first, last] : parts)
    {
      plm_frame_t* frame = nullptr;
      // The last intra frame before the part: when it's ahead of where the
      // decoding is, or the part is behind, jump to it and decode on from
      // there. Without one, from the start, frame by frame.
      const auto after = std::upper_bound(
          intra.begin(), intra.end(), first,
          [](int number, const IntraFrame& frame) { return number < frame.number; });
      const IntraFrame* jump_to = after == intra.begin() ? nullptr : &*(after - 1);
      const bool back = current >= first;
      if (jump_to && (back || jump_to->number > current + 1))
      {
        frame = JumpTo(plm, *jump_to);
        if (frame)
        {
          current = jump_to->number;
        }
        else
        {
          std::cout << "intro video: can't jump to frame " << jump_to->number
                    << ", playing it from the start\n";
          plm_rewind(plm);
          current = -1;
        }
      }
      else if (back)
      {
        plm_rewind(plm);
        current = -1;
      }
      while (!m_stop.load())
      {
        if (frame && current >= first)
        {
          if (current > last)
            break;
          if (!WaitForRoom())
            break;
          Frame decoded;
          decoded.index = index + (current - first);
          {
            std::lock_guard lock(m_mutex);
            if (!m_spare.empty())
            {
              decoded.rgba = std::move(m_spare.back());
              m_spare.pop_back();
            }
          }
          decoded.rgba.resize(static_cast<std::size_t>(width) * height * 4);
          plm_frame_to_rgba(frame, decoded.rgba.data(), width * 4);
          Push(std::move(decoded));
          if (!any)
          {
            any = true;
            m_ready = true;
          }
          if (current >= last)
            break;
        }
        frame = plm_decode_video(plm);
        if (!frame)
        {
          // The video ended before this part did: the next part jumps.
          current = std::numeric_limits<int>::max();
          break;
        }
        ++current;
      }
      if (m_stop.load())
        break;
      index += last - first + 1;
    }
  }
  plm_destroy(plm);
  std::fclose(reader.file);
  if (!any && !m_stop.load())
    m_failed = true;
}
}  // namespace moderngekko::frontend
