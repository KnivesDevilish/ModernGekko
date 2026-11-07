#include "game_border.hpp"

#include "Common/Buffer.h"
#include "Common/CommonTypes.h"
#include "Common/Image.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/RenderState.h"
#include "VideoCommon/TextureConfig.h"
#include "game_borders.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace moderngekko::detail
{
namespace
{
using frontend::BorderMotion;
using frontend::BorderPlacement;
using frontend::BorderRect;

// A grid slides one cell in this time, as the launcher's floor does.
constexpr double SECONDS_PER_CELL = 2.5;
// The rows or columns of a sliding grid are drawn in bands about this many
// window pixels wide, each moved by its own repeat. One pixel keeps the
// diagonal lines of a floor straight: a wider band moves all its rows by the
// repeat of its middle one, and the lines would show steps.
constexpr float BAND_PIXELS = 1.0f;

struct Picture
{
  u32 width = 0;
  u32 height = 0;
  // RGBA, kept to make the texture again for a new video backend.
  Common::UniqueBuffer<u8> pixels;
  // Made on the video thread the first time the picture is drawn, for the
  // video backend that presenter belongs to.
  std::unique_ptr<AbstractTexture> texture;
  const VideoCommon::Presenter* presenter = nullptr;
  bool failed = false;
};

struct Side
{
  Picture* picture = nullptr;
  BorderRect part;
  BorderMotion motion;
};

bool ReadPicture(const std::filesystem::path& path, Picture* picture)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return false;
  const std::vector<u8> bytes{std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>()};
  return !bytes.empty() &&
         Common::LoadPNG(std::span<const u8>(bytes), &picture->pixels, &picture->width,
                         &picture->height) &&
         picture->width > 0 && picture->height > 0;
}

// The video backend draws the on-screen UI with point sampling, which suits
// its text; the pictures are scaled, so they are drawn with linear sampling
// and the UI's own sampling is put back after them.
void UseLinearSampling(const ImDrawList*, const ImDrawCmd*)
{
  g_gfx->SetSamplerState(0, RenderState::GetLinearSamplerState());
}

void UsePointSampling(const ImDrawList*, const ImDrawCmd*)
{
  g_gfx->SetSamplerState(0, RenderState::GetPointSamplerState());
}

bool HasTexture(Picture& picture)
{
  if (picture.texture && picture.presenter == g_presenter.get())
    return true;
  if (picture.failed)
    return false;
  // A texture of a video backend that is gone can't be destroyed any more.
  if (picture.texture)
    (void)picture.texture.release();
  const TextureConfig config(picture.width, picture.height, 1, 1, 1,
                             AbstractTextureFormat::RGBA8, 0,
                             AbstractTextureType::Texture_2DArray);
  picture.texture = g_gfx->CreateTexture(config, "4:3 border");
  if (!picture.texture)
  {
    std::fprintf(stderr, "4:3 border: can't make a %ux%u texture\n", picture.width,
                 picture.height);
    picture.failed = true;
    return false;
  }
  picture.texture->Load(0, picture.width, picture.height, picture.width,
                        picture.pixels.data(), picture.pixels.size());
  picture.presenter = g_presenter.get();
  return true;
}

void DrawSide(ImDrawList* list, const Side& side, const BorderPlacement& placement,
              float progress)
{
  const Picture& picture = *side.picture;
  const ImTextureRef texture = *picture.texture;
  const float width = static_cast<float>(picture.width);
  const float height = static_cast<float>(picture.height);
  const BorderRect& shown = placement.picture;
  const BorderRect& screen = placement.screen;
  list->AddImage(texture, ImVec2(screen.x0, screen.y0), ImVec2(screen.x1, screen.y1),
                 ImVec2(shown.x0 / width, shown.y0 / height),
                 ImVec2(shown.x1 / width, shown.y1 / height));

  const BorderMotion& motion = side.motion;
  if (!motion.Slides())
    return;
  // Window pixels per picture pixel, across and down.
  const float scale_x = screen.Width() / shown.Width();
  const float scale_y = screen.Height() / shown.Height();
  // Each band shows the grid moved by part of its repeat, plus the grid a
  // whole repeat further back faded in as the cell ends. The two line up, so
  // the grid stays sharp; and when the cell starts again the picture is
  // back where it began, with no jump where its shading differs a cell away.
  const ImU32 fade_in = IM_COL32(255, 255, 255, static_cast<int>(std::lround(progress * 255.0f)));
  const bool fading = (fade_in >> IM_COL32_A_SHIFT) != 0;
  if (motion.Sideways())
  {
    // Sliding left, a window pixel shows the picture further right as time goes.
    const float direction = motion.direction == BorderMotion::Direction::Left ? 1.0f : -1.0f;
    const float first = std::max(static_cast<float>(motion.first), shown.y0);
    const float last = std::min(static_cast<float>(motion.last) + 1.0f, shown.y1);
    const float band = BAND_PIXELS / scale_y;
    for (float row = first; row < last; row += band)
    {
      const float end = std::min(row + band, last);
      const float period = static_cast<float>(motion.Period((row + end) * 0.5f));
      const ImVec2 top_left(screen.x0, screen.y0 + (row - shown.y0) * scale_y);
      const ImVec2 bottom_right(screen.x1, screen.y0 + (end - shown.y0) * scale_y);
      for (int behind = 0; behind < (fading ? 2 : 1); ++behind)
      {
        const float moved = direction * (progress - static_cast<float>(behind)) * period;
        list->AddImage(texture, top_left, bottom_right,
                       ImVec2((shown.x0 + moved) / width, row / height),
                       ImVec2((shown.x1 + moved) / width, end / height),
                       behind ? fade_in : IM_COL32_WHITE);
      }
    }
  }
  else
  {
    // Sliding up, a window pixel shows the picture further down as time goes.
    const float direction = motion.direction == BorderMotion::Direction::Up ? 1.0f : -1.0f;
    const float first = std::max(static_cast<float>(motion.first), shown.x0);
    const float last = std::min(static_cast<float>(motion.last) + 1.0f, shown.x1);
    const float band = BAND_PIXELS / scale_x;
    for (float column = first; column < last; column += band)
    {
      const float end = std::min(column + band, last);
      const float period = static_cast<float>(motion.Period((column + end) * 0.5f));
      const ImVec2 top_left(screen.x0 + (column - shown.x0) * scale_x, screen.y0);
      const ImVec2 bottom_right(screen.x0 + (end - shown.x0) * scale_x, screen.y1);
      for (int behind = 0; behind < (fading ? 2 : 1); ++behind)
      {
        const float moved = direction * (progress - static_cast<float>(behind)) * period;
        list->AddImage(texture, top_left, bottom_right,
                       ImVec2(column / width, (shown.y0 + moved) / height),
                       ImVec2(end / width, (shown.y1 + moved) / height),
                       behind ? fade_in : IM_COL32_WHITE);
      }
    }
  }
}
}  // namespace

struct GameBorderOverlay::Impl
{
  // By file name: both sides may show parts of the same picture.
  std::map<std::string, Picture> pictures;
  Side left;
  Side right;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  int last_frame = -1;
};

std::unique_ptr<GameBorderOverlay> GameBorderOverlay::Create(const std::filesystem::path& folder,
                                                             const std::string& id)
{
  const frontend::GameBorderList list = frontend::LoadGameBorders(folder);
  for (const std::string& problem : list.problems)
    std::fprintf(stderr, "4:3 border: borders.txt: %s\n", problem.c_str());
  const frontend::GameBorder* border = list.Find(id);
  if (!border)
  {
    std::fprintf(stderr, "4:3 border: %s is not in %s\n", id.c_str(),
                 (folder / "borders.txt").string().c_str());
    return nullptr;
  }

  auto impl = std::make_unique<Impl>();
  const auto make_side = [&](const frontend::BorderSide& from, Side* side) {
    auto [entry, added] = impl->pictures.try_emplace(from.picture);
    if (added && !ReadPicture(folder / from.picture, &entry->second))
    {
      std::fprintf(stderr, "4:3 border: can't read %s\n",
                   (folder / from.picture).string().c_str());
      return false;
    }
    if (entry->second.pixels.empty())
      return false;
    side->picture = &entry->second;
    const float width = static_cast<float>(entry->second.width);
    const float height = static_cast<float>(entry->second.height);
    side->part = from.part.value_or(BorderRect{0.0f, 0.0f, width, height});
    side->part.x1 = std::min(side->part.x1, width);
    side->part.y1 = std::min(side->part.y1, height);
    if (side->part.Empty())
    {
      std::fprintf(stderr, "4:3 border: the part of %s is outside the picture\n",
                   from.picture.c_str());
      return false;
    }
    side->motion = from.motion;
    return true;
  };
  if (!make_side(border->left, &impl->left) || !make_side(border->right, &impl->right))
    return nullptr;
  std::fprintf(stderr, "4:3 border: %s\n", id.c_str());
  return std::unique_ptr<GameBorderOverlay>(new GameBorderOverlay(std::move(impl)));
}

GameBorderOverlay::GameBorderOverlay(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
{
}

GameBorderOverlay::~GameBorderOverlay()
{
  // Dolphin destroys its video backend before the runtime is destroyed, and
  // a texture can't be destroyed after its backend (Vulkan's would crash).
  // The process ends right after the game, which frees them.
  for (auto& [name, picture] : m_impl->pictures)
    (void)picture.texture.release();
}

void GameBorderOverlay::AddToFrame()
{
  if (!g_presenter || !g_gfx || !ImGui::GetCurrentContext())
    return;
  // Once per frame of the on-screen UI: a frame that repeats the last one is
  // not presented, and the next one would get the border twice.
  const int frame = ImGui::GetFrameCount();
  if (frame == m_impl->last_frame)
    return;
  m_impl->last_frame = frame;

  // The bars on either side of the game, from the last frame presented.
  const auto& game = g_presenter->GetTargetRectangle();
  const float width = static_cast<float>(g_presenter->GetBackbufferWidth());
  const float height = static_cast<float>(g_presenter->GetBackbufferHeight());
  const std::optional<BorderPlacement> left = frontend::PlaceBorder(
      BorderRect{0.0f, 0.0f, static_cast<float>(game.left), height}, m_impl->left.part, true);
  const std::optional<BorderPlacement> right = frontend::PlaceBorder(
      BorderRect{static_cast<float>(game.right), 0.0f, width, height}, m_impl->right.part, false);
  const bool draw_left = left && HasTexture(*m_impl->left.picture);
  const bool draw_right = right && HasTexture(*m_impl->right.picture);
  if (!draw_left && !draw_right)
    return;

  const double cells =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - m_impl->start).count() /
      SECONDS_PER_CELL;
  const float progress = static_cast<float>(cells - std::floor(cells));
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  list->AddCallback(UseLinearSampling, nullptr);
  if (draw_left)
    DrawSide(list, m_impl->left, *left, progress);
  if (draw_right)
    DrawSide(list, m_impl->right, *right, progress);
  list->AddCallback(UsePointSampling, nullptr);
}
}  // namespace moderngekko::detail
