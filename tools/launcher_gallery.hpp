#pragma once

// The launcher's Screenshots page: the screenshots taken with F9 or the
// in-game menu, newest first, as a grid of small pictures; one chosen is
// shown large, with the previous and next ones a press away, and can be
// deleted. Pictures are read and shrunk on a thread of their own, so a page
// of big screenshots never stops the launcher, and the small ones are kept in
// the user directory's Cache, so they show at once the next time. The parts
// without SDL or ImGui are tools/launcher_screenshots.hpp.

#include "launcher_screenshots.hpp"
#include "launcher_text.hpp"
#include "launcher_ui.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace moderngekko::frontend::ui
{
class ScreenshotGallery
{
public:
  // folder: where the screenshots are (and its folders too when
  // recursive); cache: where their small pictures are kept.
  ScreenshotGallery(std::filesystem::path folder, std::filesystem::path cache, bool recursive)
      : m_folder(std::move(folder)), m_cache(std::move(cache)), m_recursive(recursive)
  {
    m_worker = std::jthread([this](std::stop_token stop) { Work(stop); });
  }

  ~ScreenshotGallery()
  {
    m_worker.request_stop();
    m_wake.notify_all();
    m_worker.join();
    for (Loaded& loaded : m_loaded)
      SDL_DestroySurface(loaded.surface);
    for (auto& [path, picture] : m_thumbnails)
      Forget(picture);
    Forget(m_full);
  }

  ScreenshotGallery(const ScreenshotGallery&) = delete;
  ScreenshotGallery& operator=(const ScreenshotGallery&) = delete;

  // The folder is looked at again on the next Draw, such as after the game,
  // which may have taken new ones.
  void Refresh() { m_listed_at = 0; }

  // The whole page, each frame it is shown.
  void Draw(SDL_Renderer* renderer, ImFont* bold, float scale)
  {
    // SDL's software renderer can't draw a big texture over a big area (its
    // triangles overflow and come out blank): a smaller large picture there.
    if (!m_renderer_checked)
    {
      const char* name = SDL_GetRendererName(renderer);
      if (name && SDL_strcmp(name, SDL_SOFTWARE_RENDERER) == 0)
        m_large_box = SOFTWARE_LARGE_BOX;
      m_renderer_checked = true;
    }
    Upload(renderer);
    if (m_listed_at == 0 || SDL_GetTicks() - m_listed_at >= 2000)
      List();
    std::optional<std::size_t> viewing;
    if (m_viewing)
    {
      for (std::size_t i = 0; i < m_files.size(); ++i)
      {
        if (m_files[i].path == *m_viewing)
          viewing = i;
      }
      if (!viewing)
        m_viewing.reset();
    }
    if (viewing)
      DrawViewer(*viewing, bold, scale);
    else
      DrawGrid(bold, scale);
    DrawDeleteQuestion(bold, scale);
  }

private:
  // Small pictures fit this box, and large ones this.
  static constexpr PictureSize THUMBNAIL_BOX{480, 270};
  static constexpr PictureSize LARGE_BOX{2560, 1600};
  static constexpr PictureSize SOFTWARE_LARGE_BOX{720, 450};

  struct Picture
  {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    std::filesystem::file_time_type time;
    bool asked = false;
    bool failed = false;
  };

  struct Job
  {
    std::filesystem::path path;
    std::filesystem::file_time_type time;
    bool large = false;
    PictureSize box;
  };

  struct Loaded
  {
    Job job;
    SDL_Surface* surface = nullptr;  // nothing when it couldn't be read
  };

  static void Forget(Picture& picture)
  {
    if (picture.texture)
      SDL_DestroyTexture(picture.texture);
    picture = {};
  }

  void List()
  {
    m_files = ListScreenshots(m_folder, m_recursive);
    m_listed_at = std::max<Uint64>(SDL_GetTicks(), 1);
    // Small pictures of screenshots no longer there, or changed, go.
    for (auto it = m_thumbnails.begin(); it != m_thumbnails.end();)
    {
      const auto file = std::find_if(m_files.begin(), m_files.end(), [&](const ScreenshotFile& f) {
        return f.path == it->first;
      });
      if (file == m_files.end() || file->time != it->second.time)
      {
        Forget(it->second);
        it = m_thumbnails.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void Ask(const ScreenshotFile& file, bool large)
  {
    {
      std::lock_guard lock(m_mutex);
      Job job{file.path, file.time, large, large ? m_large_box : THUMBNAIL_BOX};
      // The large picture first: it is the one being looked at, and the
      // last one asked for is the only one still wanted.
      if (large)
      {
        std::erase_if(m_jobs, [](const Job& other) { return other.large; });
        m_jobs.push_front(std::move(job));
      }
      else
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
  }

  // On the worker thread.
  void Work(std::stop_token stop)
  {
    for (;;)
    {
      Job job;
      {
        std::unique_lock lock(m_mutex);
        m_wake.wait(lock, stop, [&] { return !m_jobs.empty(); });
        if (stop.stop_requested())
          return;
        job = std::move(m_jobs.front());
        m_jobs.pop_front();
      }
      SDL_Surface* surface = job.large ? Shrunk(job.path, job.box, true) : Thumbnail(job);
      std::lock_guard lock(m_mutex);
      m_loaded.push_back({std::move(job), surface});
    }
  }

  // A picture read and shrunk to fit box: halved while it stays twice as
  // big, then scaled, so it looks smooth.
  static SDL_Surface* Shrunk(const std::filesystem::path& path, PictureSize box, bool no_bigger)
  {
    SDL_Surface* read = SDL_LoadPNG(path.string().c_str());
    if (!read)
      return nullptr;
    SDL_Surface* picture = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(read);
    if (!picture)
      return nullptr;
    const PictureSize target =
        FitPicture(picture->w, picture->h, box.width, box.height, no_bigger);
    for (int step = HalvingSteps(picture->w, picture->h, target); step > 0 && picture; --step)
    {
      SDL_Surface* half =
          SDL_ScaleSurface(picture, picture->w / 2, picture->h / 2, SDL_SCALEMODE_LINEAR);
      SDL_DestroySurface(picture);
      picture = half;
    }
    if (picture && (picture->w != target.width || picture->h != target.height))
    {
      SDL_Surface* scaled =
          SDL_ScaleSurface(picture, target.width, target.height, SDL_SCALEMODE_LINEAR);
      SDL_DestroySurface(picture);
      picture = scaled;
    }
    return picture;
  }

  // The small picture, from the cache when it is newer than the screenshot.
  SDL_Surface* Thumbnail(const Job& job) const
  {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path cached = m_cache / (job.path.stem().string() + ".png");
    if (fs::is_regular_file(cached, ec) && fs::last_write_time(cached, ec) >= job.time && !ec)
    {
      if (SDL_Surface* surface = SDL_LoadPNG(cached.string().c_str()))
        return surface;
    }
    SDL_Surface* surface = Shrunk(job.path, job.box, true);
    if (surface)
    {
      fs::create_directories(m_cache, ec);
      const fs::path part = cached.string() + ".part";
      if (SDL_SavePNG(surface, part.string().c_str()))
        fs::rename(part, cached, ec);
      else
        fs::remove(part, ec);
    }
    return surface;
  }

  // On the launcher's thread, which owns the renderer: a few pictures a
  // frame become textures.
  void Upload(SDL_Renderer* renderer)
  {
    std::vector<Loaded> loaded;
    {
      std::lock_guard lock(m_mutex);
      const std::size_t count = std::min<std::size_t>(m_loaded.size(), 4);
      loaded.assign(m_loaded.begin(), m_loaded.begin() + static_cast<std::ptrdiff_t>(count));
      m_loaded.erase(m_loaded.begin(), m_loaded.begin() + static_cast<std::ptrdiff_t>(count));
    }
    for (Loaded& item : loaded)
    {
      Picture picture;
      picture.time = item.job.time;
      picture.asked = true;
      if (item.surface)
      {
        picture.texture = SDL_CreateTextureFromSurface(renderer, item.surface);
        picture.width = item.surface->w;
        picture.height = item.surface->h;
        SDL_DestroySurface(item.surface);
        if (picture.texture)
          SDL_SetTextureScaleMode(picture.texture, SDL_SCALEMODE_LINEAR);
      }
      picture.failed = !picture.texture;
      if (item.job.large)
      {
        // Only the one still being looked at is kept.
        if (m_viewing && *m_viewing == item.job.path)
        {
          Forget(m_full);
          m_full = picture;
          m_full_path = item.job.path;
        }
        else if (picture.texture)
        {
          SDL_DestroyTexture(picture.texture);
        }
        continue;
      }
      Picture& kept = m_thumbnails[item.job.path];
      Forget(kept);
      kept = picture;
    }
  }

  static ImTextureRef TextureOf(const Picture& picture)
  {
    return ImTextureRef(
        static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(picture.texture)));
  }

  // picture fitted to the box at min..max, in the middle.
  static void DrawPicture(ImDrawList* list, const Picture& picture, ImVec2 min, ImVec2 max,
                          float rounding)
  {
    const PictureSize fitted =
        FitPicture(picture.width, picture.height, static_cast<int>(max.x - min.x),
                   static_cast<int>(max.y - min.y));
    const ImVec2 at(std::floor(min.x + (max.x - min.x - fitted.width) * 0.5f),
                    std::floor(min.y + (max.y - min.y - fitted.height) * 0.5f));
    list->AddImageRounded(TextureOf(picture), at,
                          ImVec2(at.x + fitted.width, at.y + fitted.height), ImVec2(0, 0),
                          ImVec2(1, 1), IM_COL32_WHITE, rounding);
  }

  void Open(const ScreenshotFile& file)
  {
    m_viewing = file.path;
    if (m_full_path != file.path)
    {
      Forget(m_full);
      m_full_path.clear();
      Ask(file, true);
    }
  }

  void DrawGrid(ImFont* bold, float scale)
  {
    DrawPageHeader(Tr("Screenshots"),
                   Tr("Taken with F9 or the in-game menu. Choose one to see it large."), bold);
    if (ImGui::Button(Tr("Open folder")))
      OpenFolder(m_folder);
    if (!m_files.empty())
    {
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s",
                          Format(Tr("Screenshots: %d"), static_cast<int>(m_files.size())).c_str());
    }
    ImGui::Spacing();
    if (m_files.empty())
    {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", Tr("No screenshots yet. Press F9 while playing, or Take "
                                   "screenshot in the in-game menu."));
      ImGui::PopTextWrapPos();
      return;
    }
    // Flattened, so the keys and a pad go from Open folder to the pictures
    // as on the rest of the page.
    ImGui::BeginChild("##screenshot_grid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened);
    const float spacing = 14.0f * scale;
    // A little room on the right for the pictures' rounded borders.
    const float available = ImGui::GetContentRegionAvail().x - 2.0f * scale;
    const int columns =
        std::max(1, static_cast<int>((available + spacing) / (220.0f * scale + spacing)));
    const float cell_width = (available - spacing * static_cast<float>(columns - 1)) /
                             static_cast<float>(columns);
    const float picture_height = std::floor(cell_width * 9.0f / 16.0f);
    const float rounding = 6.0f * scale;
    ImDrawList* list = ImGui::GetWindowDrawList();
    for (std::size_t i = 0; i < m_files.size(); ++i)
    {
      const ScreenshotFile& file = m_files[i];
      if (i % static_cast<std::size_t>(columns) != 0)
        ImGui::SameLine(0.0f, spacing);
      ImGui::BeginGroup();
      ImGui::PushID(static_cast<int>(i));
      const ImVec2 min = ImGui::GetCursorScreenPos();
      const ImVec2 max(min.x + cell_width, min.y + picture_height);
      // Reachable with the keys and a pad too, which invisible buttons
      // aren't unless asked.
      const bool chosen = ImGui::InvisibleButton("##screenshot", ImVec2(cell_width, picture_height),
                                                 ImGuiButtonFlags_EnableNav);
      const bool highlighted = ImGui::IsItemHovered() || ImGui::IsItemFocused();
      list->AddRectFilled(min, max, IM_COL32(12, 12, 14, 255), rounding);
      Picture& picture = m_thumbnails[file.path];
      if (ImGui::IsItemVisible() && !picture.asked)
      {
        picture.asked = true;
        picture.time = file.time;
        Ask(file, false);
      }
      if (picture.texture)
        DrawPicture(list, picture, min, max, rounding);
      else if (picture.failed)
        list->AddText(ImVec2(min.x + 10.0f * scale, min.y + 10.0f * scale),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), "?");
      list->AddRect(min, max,
                    highlighted ? ImGui::GetColorU32(LAUNCHER_ACCENT) : IM_COL32(40, 40, 43, 255),
                    rounding, 0, highlighted ? 2.0f * scale : 1.0f);
      ImGui::TextDisabled("%s", ScreenshotTimeLabel(file.time).c_str());
      ImGui::PopID();
      ImGui::EndGroup();
      if (chosen)
        Open(file);
      if (i % static_cast<std::size_t>(columns) == static_cast<std::size_t>(columns - 1))
        ImGui::Dummy(ImVec2(0.0f, spacing * 0.5f));
    }
    ImGui::EndChild();
  }

  void DrawViewer(std::size_t index, ImFont* bold, float scale)
  {
    const ScreenshotFile& file = m_files[index];
    const std::size_t count = m_files.size();
    const auto go = [&](std::size_t to) { Open(m_files[to]); };
    bool back = false;
    // The list is newest first: "Previous" is the older one, further on.
    bool older = false;
    bool newer = false;
    if (ImGui::Button(Tr("All screenshots")))
      back = true;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(bold, 0.0f);
    ImGui::TextUnformatted(ScreenshotTimeLabel(file.time).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%d / %d", static_cast<int>(index + 1), static_cast<int>(count));
    // Delete on the right.
    const float delete_width =
        ImGui::CalcTextSize(Tr("Delete")).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    const float room = ImGui::GetContentRegionAvail().x;
    if (room > delete_width)
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - delete_width);
    if (ImGui::Button(Tr("Delete")))
      AskToDelete(file.path);

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##screenshot_large", ImVec2(0.0f, -footer), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    {
      const ImVec2 min = ImGui::GetCursorScreenPos();
      const ImVec2 size = ImGui::GetContentRegionAvail();
      const ImVec2 max(min.x + size.x, min.y + size.y);
      ImDrawList* list = ImGui::GetWindowDrawList();
      list->AddRectFilled(min, max, IM_COL32(8, 8, 10, 255), 6.0f * scale);
      // The small picture until the large one is read.
      const bool large = m_full_path == file.path && m_full.texture;
      const auto thumbnail = m_thumbnails.find(file.path);
      if (large)
        DrawPicture(list, m_full, min, max, 0.0f);
      else if (thumbnail != m_thumbnails.end() && thumbnail->second.texture)
        DrawPicture(list, thumbnail->second, min, max, 0.0f);
      if (!large && !(m_full_path == file.path && m_full.failed))
      {
        list->AddText(ImVec2(min.x + 12.0f * scale, min.y + 10.0f * scale),
                      ImGui::GetColorU32(ImGuiCol_Text), Tr("Loading..."));
      }
      else if (m_full_path == file.path && m_full.failed)
      {
        list->AddText(ImVec2(min.x + 12.0f * scale, min.y + 10.0f * scale),
                      IM_COL32(255, 90, 77, 255), Tr("This picture can't be read."));
      }
      // A click on either half goes that way.
      if (ImGui::InvisibleButton("##screenshot_picture", size))
      {
        const float x = ImGui::GetIO().MousePos.x;
        if (x < min.x + size.x * 0.5f)
          newer = true;
        else
          older = true;
      }
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(index == 0);
    if (ImGui::Button(Tr("< Newer")))
      newer = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= count);
    if (ImGui::Button(Tr("Older >")))
      older = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    std::string details = FileSizeText(file.size);
    if (const auto size = PngHeaderSize(file.path))
      details = Format("%d x %d \xC2\xB7 %s", size->width, size->height, details.c_str());
    ImGui::TextDisabled("%s", details.c_str());

    // Keys and the pad: left and right go through them, Escape or B back.
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
    {
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadL1))
        newer = true;
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1))
        older = true;
      if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))
        back = true;
      if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        AskToDelete(file.path);
    }
    if (back)
      m_viewing.reset();
    else if (newer && index > 0)
      go(index - 1);
    else if (older && index + 1 < count)
      go(index + 1);
  }

  void AskToDelete(const std::filesystem::path& path)
  {
    m_deleting = path;
    m_delete_error.clear();
    ImGui::OpenPopup("##delete_screenshot");
  }

  void DrawDeleteQuestion(ImFont* bold, float scale)
  {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420.0f * scale, 0.0f));
    if (!ImGui::BeginPopupModal("##delete_screenshot", nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
      return;
    ImGui::PushFont(bold, 0.0f);
    ImGui::TextUnformatted(Tr("Delete this screenshot?"));
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(Tr("It is removed from your computer for good."));
    if (!m_delete_error.empty())
      ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s",
                         Format(Tr("Couldn't delete it: %s"), m_delete_error.c_str()).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(Tr("Delete")))
    {
      std::error_code ec;
      std::filesystem::remove(m_deleting, ec);
      if (ec)
      {
        m_delete_error = ec.message();
      }
      else
      {
        std::filesystem::remove(m_cache / (m_deleting.stem().string() + ".png"), ec);
        // The next one (older, or else newer) is shown in its place.
        if (m_viewing && *m_viewing == m_deleting)
        {
          std::optional<std::filesystem::path> next;
          for (std::size_t i = 0; i < m_files.size(); ++i)
          {
            if (m_files[i].path != m_deleting)
              continue;
            if (i + 1 < m_files.size())
              next = m_files[i + 1].path;
            else if (i > 0)
              next = m_files[i - 1].path;
          }
          m_viewing.reset();
          List();
          if (next)
          {
            for (const ScreenshotFile& file : m_files)
            {
              if (file.path == *next)
                Open(file);
            }
          }
        }
        else
        {
          List();
        }
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Cancel")))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  std::filesystem::path m_folder;
  std::filesystem::path m_cache;
  bool m_recursive = false;
  PictureSize m_large_box = LARGE_BOX;
  bool m_renderer_checked = false;

  std::vector<ScreenshotFile> m_files;
  Uint64 m_listed_at = 0;
  std::map<std::filesystem::path, Picture> m_thumbnails;
  Picture m_full;
  std::filesystem::path m_full_path;
  std::optional<std::filesystem::path> m_viewing;
  std::filesystem::path m_deleting;
  std::string m_delete_error;

  std::mutex m_mutex;
  std::condition_variable_any m_wake;
  std::deque<Job> m_jobs;
  std::vector<Loaded> m_loaded;
  std::jthread m_worker;
};
}  // namespace moderngekko::frontend::ui
