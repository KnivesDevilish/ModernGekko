// Check for updates: how versions are read and compared, and what is taken
// from GitHub's answer about a repository's latest release.
#include "launcher_update.hpp"

#include <string>
#include <vector>

int main()
{
  using moderngekko::frontend::IsNewerVersion;
  using moderngekko::frontend::LatestReleaseTag;
  using moderngekko::frontend::ParseVersion;
  using moderngekko::frontend::ReleasesPage;

  // Tags as they are usually written, with or without a v.
  if (ParseVersion("0.1.0") != std::vector<int>{0, 1, 0} ||
      ParseVersion("v0.2") != std::vector<int>{0, 2} ||
      ParseVersion("V1.10.3") != std::vector<int>{1, 10, 3} ||
      ParseVersion("v2.0.0-beta.1") != std::vector<int>{2, 0, 0} ||
      ParseVersion("1.2.3+build7") != std::vector<int>{1, 2, 3} ||
      ParseVersion("1.2.3.4") != std::vector<int>{1, 2, 3, 4})
    return 1;
  // Anything else isn't a version.
  if (ParseVersion("") || ParseVersion("v") || ParseVersion("1..2") || ParseVersion(".1") ||
      ParseVersion("1.") || ParseVersion("1.2.3.4.5") || ParseVersion("latest") ||
      ParseVersion("1.2a") || ParseVersion(" 1.2") || ParseVersion("1234567.0"))
    return 2;

  // Newer means a higher number at the first place they differ; missing
  // places count as 0.
  if (!IsNewerVersion("v0.2.0", "0.1.0") || !IsNewerVersion("0.1.1", "0.1") ||
      !IsNewerVersion("1.0", "0.9.9") || !IsNewerVersion("0.10.0", "0.9.0"))
    return 3;
  if (IsNewerVersion("v0.1.0", "0.1.0") || IsNewerVersion("0.1", "0.1.0") ||
      IsNewerVersion("0.1.0", "0.2.0") || IsNewerVersion("0.9.9", "1.0"))
    return 4;
  // Unreadable on either side: never offered as an update.
  if (IsNewerVersion("nightly", "0.1.0") || IsNewerVersion("v9.9.9", "") ||
      IsNewerVersion("", "0.1.0"))
    return 5;

  // GitHub's answer: the tag, wherever it sits among the other fields.
  if (LatestReleaseTag(R"({"url":"https://api.github.com/x","tag_name":"v0.2.0",)"
                       R"("name":"SR-ZGS 0.2","draft":false,"prerelease":false,)"
                       R"("assets":[{"name":"a.zip"}]})") != "v0.2.0")
    return 6;
  // Not an answer about a release: nothing.
  if (!LatestReleaseTag(R"({"message":"Not Found","status":"404"})").empty() ||
      !LatestReleaseTag(R"({"tag_name":2})").empty() ||
      !LatestReleaseTag(R"(["v0.2.0"])").empty() || !LatestReleaseTag("").empty() ||
      !LatestReleaseTag("<html>rate limited</html>").empty() ||
      !LatestReleaseTag(R"({"tag_name":"v0.2.0")").empty())
    return 7;

  if (ReleasesPage("KnivesDevilish/SR-ZGS") !=
      "https://github.com/KnivesDevilish/SR-ZGS/releases/latest")
    return 8;
  return 0;
}
