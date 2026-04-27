// Build-time stub returning nullptr from MakeGitSync. Active when
// DAFENG_ENABLE_GIT_SYNC is OFF — keeps the daemon's source tree
// compilable on machines without libgit2 installed.

#include "git_sync.h"

namespace dafeng {

std::unique_ptr<IGitSync> MakeGitSync(const GitSyncConfig& /*config*/) {
  return nullptr;
}

}  // namespace dafeng
