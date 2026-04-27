#pragma once

// Thin wrapper over libgit2 for the userdata Git sync. Opinionated for
// Phase 2.4.E:
//   - Operates on a single local repository (the userdata dir).
//   - Init is idempotent — calling on an existing repo is a no-op.
//   - Commit-all stages the working tree and creates a new commit.
//   - Push requires explicit opt-in via Config::push_enabled. Default is
//     OFF — privacy-conservative per CLAUDE.md "网络请求,先停下来确认".
//
// All operations return false on libgit2 error; the caller is expected to
// log and continue. None of these block the IME hot path; they're called
// from the daemon's idle / scheduled learner.

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace dafeng {

struct GitSyncConfig {
  std::string repo_path;            // local repo on disk (userdata dir)
  std::string author_name = "dafeng-daemon";
  std::string author_email = "dafeng@localhost";

  // Remote configuration. push_url is set only if the user has wired up a
  // private repo for cross-device sync.
  std::string remote_name = "origin";
  std::string push_url;             // empty = no remote configured
  bool push_enabled = false;        // default OFF; flip via config.yaml

  // SSH key paths for push auth. Only used when push_enabled and the
  // remote URL is git@...
  std::string ssh_public_key;
  std::string ssh_private_key;
};

class IGitSync {
 public:
  virtual ~IGitSync() = default;

  // Initialize the repository if not already a Git repo. Returns true if
  // the repo exists or was created. Idempotent.
  virtual bool Init() = 0;

  // git add . && git commit -m message. Returns true on success or if
  // there are no changes to commit (treats nothing-to-do as success).
  virtual bool CommitAll(const std::string& message) = 0;

  // Push to `remote_name` if push_enabled. Returns true on success; if
  // push_enabled is false, returns true and logs at debug level.
  virtual bool Push() = 0;

  // True iff the repository's working tree has uncommitted changes.
  virtual bool HasChanges() = 0;
};

// Returns nullptr if libgit2 isn't built into the daemon, or if `repo_path`
// is invalid. The factory itself is implemented in git_sync.cc when
// DAFENG_ENABLE_GIT_SYNC=ON; otherwise a stub returns nullptr.
std::unique_ptr<IGitSync> MakeGitSync(const GitSyncConfig& config);

}  // namespace dafeng
