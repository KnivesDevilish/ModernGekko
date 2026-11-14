#include "launcher_process.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace moderngekko::frontend
{
#if defined(_WIN32)
namespace
{
// One job for every game of this launcher, kept open until it exits. Only the
// game is in it: a program the game opens (SILENT_BREAKAWAY_OK) stays open.
HANDLE LauncherJob()
{
  static const HANDLE job = [] {
    HANDLE created = CreateJobObjectW(nullptr, nullptr);
    if (!created)
      return HANDLE{nullptr};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    if (!SetInformationJobObject(created, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits)))
    {
      CloseHandle(created);
      return HANDLE{nullptr};
    }
    return created;
  }();
  return job;
}
}  // namespace

void EndWithLauncher(std::int64_t process_id)
{
  const HANDLE job = LauncherJob();
  if (!job || process_id <= 0)
    return;
  const HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                     static_cast<DWORD>(process_id));
  if (!process)
    return;
  AssignProcessToJobObject(job, process);
  CloseHandle(process);
}
#else
void EndWithLauncher(std::int64_t) {}
#endif
}  // namespace moderngekko::frontend
