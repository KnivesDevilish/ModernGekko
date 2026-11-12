// The game selection screen's layout.txt and motion
// (tools/launcher_game_select_layout.hpp): a full layout read whole, a
// missing or senseless setting refused, the eases and the logo's pop where
// they start and end, and the intros: read, a broken one left out, and their
// frames and fades along the way.
#include "launcher_game_select_layout.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

using namespace moderngekko::frontend;

namespace
{
const std::string FULL = R"(# A comment line
canvas 1920 1333
left GXEE8P   # the left half
right RS9E8P
halves 930 938
teeth 815 866 950 1000 80
band 1294 1333 1835
picture frame frame.png 0 0
picture left-background left-background.png 0 0
picture left-character left-character.png 0 104
picture left-logo left-logo.png 65 974
picture right-background right-background.png 938 0
picture right-character right-character.png 938 0
picture right-logo right-logo.png 1089 944
font Teko-Bold.ttf
sound move move.wav
sound choose-left choose-left.wav
preview left files/TITLE_G.SFD 4:3 88-212 920-1091
preview-fade left 1.0 0.5 EEECEE 1.0
preview-fade right 1 2 000000 0
preview right files/Image3.cvm:OPENING.SFD 16:9 3372-3539
)";

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

std::string Without(const std::string& text, const std::string& line)
{
  std::string result = text;
  result.erase(result.find(line), line.size());
  return result;
}
}  // namespace

int main()
{
  const auto layout = ParseGameSelectLayout(FULL);
  if (!layout || layout->width != 1920.0f || layout->height != 1333.0f ||
      layout->left_game != "GXEE8P" || layout->right_game != "RS9E8P" ||
      layout->left_end != 930.0f || layout->right_start != 938.0f || layout->period != 80.0f ||
      layout->band_top != 1294.0f || layout->hints_right != 1835.0f ||
      layout->font != "Teko-Bold.ttf" || layout->pictures.size() != 7 ||
      layout->pictures.at("left-logo").x != 65.0f ||
      layout->pictures.at("right-logo").file != "right-logo.png" || !Near(layout->Middle(), 908.0f) ||
      layout->sounds.size() != 2 || layout->sounds.at("move") != "move.wav")
    return 1;
  // Every setting and picture is needed.
  for (const char* line : {"canvas 1920 1333\n", "left GXEE8P   # the left half\n",
                           "halves 930 938\n", "teeth 815 866 950 1000 80\n",
                           "band 1294 1333 1835\n",
                           "picture right-character right-character.png 938 0\n"})
  {
    if (ParseGameSelectLayout(Without(FULL, line)))
      return 2;
  }
  // Settings that make no sense: the same game twice, teeth out of order,
  // no period.
  std::string same = FULL;
  same.replace(same.find("right RS9E8P"), 12, "right GXEE8P");
  std::string crossed = FULL;
  crossed.replace(crossed.find("teeth 815 866"), 13, "teeth 900 866");
  std::string flat = FULL;
  flat.replace(flat.find("1000 80"), 7, "1000 0");
  if (ParseGameSelectLayout(same) || ParseGameSelectLayout(crossed) ||
      ParseGameSelectLayout(flat) || ParseGameSelectLayout(""))
    return 3;
  // No font and no sounds are fine: the launcher's own font, and silence.
  if (const auto silent = ParseGameSelectLayout(
          Without(Without(FULL, "sound move move.wav\n"), "sound choose-left choose-left.wav\n"));
      !silent || !silent->sounds.empty())
    return 9;
  const auto fontless = ParseGameSelectLayout(Without(FULL, "font Teko-Bold.ttf\n"));
  if (!fontless || !fontless->font.empty())
    return 4;

  using namespace game_select_motion;
  if (!Near(EaseOut(0.0f), 0.0f) || !Near(EaseOut(1.0f), 1.0f) || !Near(EaseOut(2.0f), 1.0f) ||
      !Near(EaseInOut(0.5f), 0.5f) || !Near(EaseInOut(-1.0f), 0.0f) || EaseOut(0.5f) <= 0.5f)
    return 5;
  const Pop before = LogoPop(-0.1f);
  const Pop growing = LogoPop(0.25f);
  const Pop after = LogoPop(1.0f);
  if (before.alpha != 0.0f || after.alpha != 1.0f || !Near(after.scale, 1.0f) ||
      growing.scale <= 1.0f)
    return 6;
  if (ChoiceFlash(-1.0f) != 0.0f || !Near(ChoiceFlash(0.08f), 1.0f) || ChoiceFlash(1.0f) != 0.0f)
    return 7;
  // The teeth: steady at rest, faster just after a move and after choosing.
  if (!Near(TeethSpeed(-1.0f, -1.0f), 45.0f) || TeethSpeed(0.0f, -1.0f) < 500.0f ||
      TeethSpeed(5.0f, -1.0f) > 46.0f || TeethSpeed(-1.0f, 0.0f) < 1600.0f)
    return 8;

  // The intros, either line first; a broken one is left out.
  const auto& left = layout->previews[0];
  const auto& right = layout->previews[1];
  if (!left || !right || left->video != "files/TITLE_G.SFD" || !Near(left->shape, 4.0f / 3.0f) ||
      left->parts.size() != 2 || left->parts[1] != std::pair(920, 1091) || left->Frames() != 297 ||
      !Near(left->fade_in, 1.0f) || !Near(left->hold, 1.0f) ||
      !Near(left->end_colour[0], 0xEE / 255.0f) || right->video != "files/Image3.cvm:OPENING.SFD" ||
      !Near(right->shape, 16.0f / 9.0f) || !Near(right->fade_out, 2.0f) ||
      right->end_colour[2] != 0.0f)
    return 10;
  for (const char* broken :
       {"preview left a.SFD 4:3 212-88\n", "preview left a.SFD 4:3 88-212 x\n",
        "preview left a.SFD 4x3 88-212\n", "preview left a.SFD 4:3\n",
        "preview middle a.SFD 4:3 1-2\n"})
  {
    const auto with = ParseGameSelectLayout(
        Without(FULL, "preview left files/TITLE_G.SFD 4:3 88-212 920-1091\n") + broken);
    if (!with || with->previews[0] || !with->previews[1])
      return 11;
  }
  if (const auto plain = ParseGameSelectLayout(
          Without(FULL, "preview right files/Image3.cvm:OPENING.SFD 16:9 3372-3539\n"));
      !plain || plain->previews[1])
    return 12;
  {
    using namespace game_select_preview;
    // Left: 297 frames at 24 a second, 12.375 s; in from black over 1 s,
    // to the colour over the last 0.5 s, then 1 s of it.
    const Moment start = MomentAt(*left, 24.0, 0.0);
    const Moment middle = MomentAt(*left, 24.0, 6.0);
    const Moment fading = MomentAt(*left, 24.0, 12.125);
    const Moment holding = MomentAt(*left, 24.0, 13.0);
    const Moment over = MomentAt(*left, 24.0, 13.4);
    if (start.frame != 0 || !Near(start.black, 1.0f) || start.colour != 0.0f || start.ended ||
        middle.frame != 144 || middle.black != 0.0f || middle.colour != 0.0f ||
        !Near(fading.colour, 0.5f) || holding.frame != 296 || !Near(holding.colour, 1.0f) ||
        holding.ended || !over.ended || !Near(VideoLength(*left, 24.0), 12.375f))
      return 13;
    // No fade out and no hold: nothing over it, and done as the video is.
    GameSelectPreview bare;
    bare.parts = {{0, 29}};
    if (MomentAt(bare, 30.0, 0.99).colour != 0.0f || MomentAt(bare, 30.0, 0.99).ended ||
        !MomentAt(bare, 30.0, 1.0).ended || MomentAt(bare, 0.0, 1.0).frame != 0)
      return 14;
  }

  std::puts("launcher game select: ok");
  return 0;
}
