#pragma once

// "Check for updates": the launcher asks GitHub for the newest published
// release of the project (a build names it with MODERNGEKKO_UPDATE_REPOSITORY,
// such as KnivesDevilish/SR-ZGS) and compares its version with its own
// (MODERNGEKKO_FRONTEND_VERSION). It only reads: nothing is downloaded or
// installed, and a newer version is shown with a link to its download page.
//
// The version rules and reading GitHub's answer are here, testable on their
// own; UpdateCheck (launcher_update.cpp) does the asking, on a thread of its
// own, with Dolphin's HTTP client.

#include <picojson.h>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace moderngekko::frontend
{
// "0.2.1", "v0.2" or "V1.0.0-beta" as numbers ({0, 2, 1}, {0, 2}, {1, 0, 0}):
// an optional v, then one to four numbers between dots. Whatever follows a
// "-" or "+" (a test build's label) is left out. Anything else: nullopt.
inline std::optional<std::vector<int>> ParseVersion(std::string_view text)
{
  if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
    text.remove_prefix(1);
  text = text.substr(0, text.find_first_of("-+"));
  std::vector<int> numbers;
  std::size_t start = 0;
  while (true)
  {
    const std::size_t dot = text.find('.', start);
    const std::string_view part =
        text.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
    if (part.empty() || part.size() > 6)
      return std::nullopt;
    int number = 0;
    for (const char digit : part)
    {
      if (digit < '0' || digit > '9')
        return std::nullopt;
      number = number * 10 + (digit - '0');
    }
    numbers.push_back(number);
    if (numbers.size() > 4)
      return std::nullopt;
    if (dot == std::string_view::npos)
      return numbers;
    start = dot + 1;
  }
}

// Whether latest is a newer version than current ("0.2" counts as "0.2.0").
// False when either can't be read.
inline bool IsNewerVersion(std::string_view latest, std::string_view current)
{
  const auto newest = ParseVersion(latest);
  const auto installed = ParseVersion(current);
  if (!newest || !installed)
    return false;
  const std::size_t count = std::max(newest->size(), installed->size());
  for (std::size_t i = 0; i < count; ++i)
  {
    const int a = i < newest->size() ? (*newest)[i] : 0;
    const int b = i < installed->size() ? (*installed)[i] : 0;
    if (a != b)
      return a > b;
  }
  return false;
}

// The version of GitHub's answer for a repository's latest release (its
// "tag_name", such as "v0.2.0"), or empty when the answer isn't one.
inline std::string LatestReleaseTag(std::string_view json)
{
  picojson::value answer;
  const std::string error =
      picojson::parse(answer, std::string(json.begin(), json.end()));
  if (!error.empty() || !answer.is<picojson::object>())
    return {};
  const picojson::object& release = answer.get<picojson::object>();
  const auto tag = release.find("tag_name");
  if (tag == release.end() || !tag->second.is<std::string>())
    return {};
  return tag->second.get<std::string>();
}

// Where people download the newest release; GitHub sends the browser on
// to it, so the address never comes from the answer itself.
inline std::string ReleasesPage(std::string_view repository)
{
  return "https://github.com/" + std::string(repository) + "/releases/latest";
}

// One question to GitHub at a time, answered on a thread of its own so the
// launcher never waits for the network.
class UpdateCheck
{
public:
  enum class State
  {
    Idle,        // not asked yet
    Checking,    // waiting for GitHub
    UpToDate,    // this is the newest version
    Available,   // a newer one is published
    NoReleases,  // the repository has published no version yet
    Failed,      // no answer (offline, GitHub busy...)
  };
  struct Status
  {
    State state = State::Idle;
    std::string latest;  // the newest release's version, once known
  };

  UpdateCheck(std::string repository, std::string current_version);
  ~UpdateCheck();
  UpdateCheck(const UpdateCheck&) = delete;
  UpdateCheck& operator=(const UpdateCheck&) = delete;

  // Asks again, unless a question is still waiting for its answer.
  void Start();
  Status Get() const;
  const std::string& Repository() const { return m_repository; }
  const std::string& CurrentVersion() const { return m_current_version; }

private:
  void Run();

  std::string m_repository;
  std::string m_current_version;
  mutable std::mutex m_mutex;
  Status m_status;
  std::thread m_thread;
};
}  // namespace moderngekko::frontend
