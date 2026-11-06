#include "launcher_update.hpp"

#include "Common/HttpRequest.h"

#include <chrono>
#include <iostream>
#include <utility>

namespace moderngekko::frontend
{
UpdateCheck::UpdateCheck(std::string repository, std::string current_version)
    : m_repository(std::move(repository)), m_current_version(std::move(current_version))
{
}

UpdateCheck::~UpdateCheck()
{
  // A question still out ends within its time limit (Run).
  if (m_thread.joinable())
    m_thread.join();
}

void UpdateCheck::Start()
{
  {
    std::lock_guard lock(m_mutex);
    if (m_status.state == State::Checking)
      return;
    m_status.state = State::Checking;
  }
  // The previous question has its answer: its thread is done.
  if (m_thread.joinable())
    m_thread.join();
  m_thread = std::thread(&UpdateCheck::Run, this);
}

UpdateCheck::Status UpdateCheck::Get() const
{
  std::lock_guard lock(m_mutex);
  return m_status;
}

void UpdateCheck::Run()
{
  Status status;
  // Ten seconds at most, so closing the launcher during a check never waits
  // longer than that.
  Common::HttpRequest request(std::chrono::seconds(10));
  const Common::HttpRequest::Headers headers = {
      {"Accept", "application/vnd.github+json"},
      // GitHub turns away requests that don't name their program.
      {"User-Agent", "ModernGekko-Launcher/" + m_current_version},
  };
  const auto answer =
      request.IsValid() ?
          request.Get("https://api.github.com/repos/" + m_repository + "/releases/latest",
                      headers, Common::HttpRequest::AllowedReturnCodes::All) :
          std::nullopt;
  const int code = request.IsValid() ? request.GetLastResponseCode() : 0;
  if (!answer || code == 0)
  {
    status.state = State::Failed;
    std::cout << "updates: GitHub could not be reached\n";
  }
  else if (code == 404)
  {
    // GitHub's answer for a repository without any published release.
    status.state = State::NoReleases;
    std::cout << "updates: " << m_repository << " has no published release yet\n";
  }
  else if (code != 200)
  {
    status.state = State::Failed;
    std::cout << "updates: GitHub answered " << code << '\n';
  }
  else
  {
    status.latest = LatestReleaseTag(std::string_view(
        reinterpret_cast<const char*>(answer->data()), answer->size()));
    if (!ParseVersion(status.latest))
    {
      status.state = State::Failed;
      std::cout << "updates: can't read the latest release's version '" << status.latest
                << "'\n";
    }
    else
    {
      status.state = IsNewerVersion(status.latest, m_current_version) ? State::Available :
                                                                         State::UpToDate;
      std::cout << "updates: newest release " << status.latest << ", this is "
                << m_current_version << '\n';
    }
  }
  std::lock_guard lock(m_mutex);
  m_status = std::move(status);
}
}  // namespace moderngekko::frontend
