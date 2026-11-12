// The launcher's short sounds (tools/launcher_sound.hpp): a WAV read with its
// rate, channels and samples, files that aren't 16-bit PCM refused, the sound
// made into another rate and channel count, and mixing that clips instead of
// wrapping around.
#include "launcher_sound.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace moderngekko::frontend;

namespace
{
void Put16(std::vector<unsigned char>& bytes, unsigned value)
{
  bytes.push_back(static_cast<unsigned char>(value & 0xFF));
  bytes.push_back(static_cast<unsigned char>(value >> 8 & 0xFF));
}

void Put32(std::vector<unsigned char>& bytes, unsigned value)
{
  Put16(bytes, value & 0xFFFF);
  Put16(bytes, value >> 16);
}

void PutTag(std::vector<unsigned char>& bytes, const char* tag)
{
  bytes.insert(bytes.end(), tag, tag + 4);
}

// A WAV with an extra chunk before the data, as some programs write them.
std::vector<unsigned char> Wav(unsigned format, unsigned channels, unsigned rate, unsigned bits,
                               const std::vector<short>& samples)
{
  std::vector<unsigned char> bytes;
  PutTag(bytes, "RIFF");
  Put32(bytes, 0);
  PutTag(bytes, "WAVE");
  PutTag(bytes, "fmt ");
  Put32(bytes, 16);
  Put16(bytes, format);
  Put16(bytes, channels);
  Put32(bytes, rate);
  Put32(bytes, rate * channels * bits / 8);
  Put16(bytes, channels * bits / 8);
  Put16(bytes, bits);
  PutTag(bytes, "LIST");
  Put32(bytes, 3);  // odd: a padding byte follows
  bytes.insert(bytes.end(), {'a', 'b', 'c', 0});
  PutTag(bytes, "data");
  Put32(bytes, static_cast<unsigned>(samples.size() * 2));
  for (const short sample : samples)
    Put16(bytes, static_cast<unsigned short>(sample));
  return bytes;
}
}  // namespace

int main()
{
  const auto stereo = ParseWav(Wav(1, 2, 48000, 16, {100, -100, 200, -200, 300, -300}));
  if (!stereo || stereo->rate != 48000 || stereo->channels != 2 || stereo->samples.size() != 6 ||
      stereo->samples[1] != -100 || stereo->samples[5] != -300)
    return 1;
  // Refused: 8-bit, compressed, five channels, no data, not a WAV at all.
  if (ParseWav(Wav(1, 2, 48000, 8, {1, 2})) || ParseWav(Wav(2, 2, 48000, 16, {1, 2})) ||
      ParseWav(Wav(1, 5, 48000, 16, {1, 2, 3, 4, 5})) || ParseWav(Wav(1, 1, 48000, 16, {})) ||
      ParseWav({'R', 'I', 'F', 'F'}) || ParseWav({}))
    return 2;
  std::vector<unsigned char> cut = Wav(1, 1, 44100, 16, {1, 2, 3, 4});
  cut.resize(cut.size() - 3);  // the data chunk says more than the file has
  if (ParseWav(cut))
    return 3;

  // Half the rate: half the frames; mono from stereo averages both sides.
  SoundEffect ramp;
  ramp.rate = 48000;
  ramp.channels = 2;
  for (int i = 0; i < 8; ++i)
  {
    ramp.samples.push_back(static_cast<std::int16_t>(i * 100));
    ramp.samples.push_back(static_cast<std::int16_t>(i * 100 + 50));
  }
  const auto half = ConvertSound(ramp, 24000, 2);
  if (half.size() != 8 || half[0] != 0 || half[1] != 50 || half[2] != 200 || half[3] != 250)
    return 4;
  const auto mono = ConvertSound(ramp, 48000, 1);
  if (mono.size() != 8 || mono[0] != 25 || mono[1] != 125)
    return 5;
  // A faster rate: in between the samples, on the straight line.
  const auto twice = ConvertSound(ramp, 96000, 2);
  if (twice.size() != 32 || twice[2] != 50 || twice[4] != 100)
    return 6;
  if (!ConvertSound(ramp, 0, 2).empty() || !ConvertSound(ramp, 48000, 3).empty())
    return 7;

  // Mixing: added at the gain, clipped at the ends of the range.
  std::vector<std::int16_t> block = {1000, 32000, -32000, 5};
  const std::vector<std::int16_t> sound = {1000, 1000, -1000};
  if (MixInto(block.data(), block.size(), sound.data(), sound.size(), 0.5f) != 3 ||
      block[0] != 1500 || block[1] != 32500 || block[2] != -32500 || block[3] != 5)
    return 8;
  if (MixInto(block.data(), block.size(), sound.data(), sound.size(), 2.0f) != 3 ||
      block[1] != 32767 || block[2] != -32768)
    return 9;

  std::puts("launcher sound: ok");
  return 0;
}
