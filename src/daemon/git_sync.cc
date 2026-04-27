// libgit2-backed userdata sync. Compiled only when DAFENG_ENABLE_GIT_SYNC
// is ON; otherwise git_sync_stub.cc takes over.
//
// The error reporting pattern is "log + return false" — none of these are
// fatal to the daemon. The IME hot path doesn't depend on this file at
// all; it's the cold-path learner that drives sync.

#include "git_sync.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

#include <git2.h>

#include "dafeng/logging.h"

namespace dafeng {

namespace {

// libgit2 has a process-global init / shutdown that must be reference
// counted across multiple GitSync instances. Wrap it.
class Libgit2Lifecycle {
 public:
  Libgit2Lifecycle() {
    std::lock_guard<std::mutex> lock(mu_);
    if (refs_++ == 0) git_libgit2_init();
  }
  ~Libgit2Lifecycle() {
    std::lock_guard<std::mutex> lock(mu_);
    if (--refs_ == 0) git_libgit2_shutdown();
  }

 private:
  static std::mutex mu_;
  static int refs_;
};
std::mutex Libgit2Lifecycle::mu_;
int Libgit2Lifecycle::refs_ = 0;

void LogGitError(const char* what) {
  const git_error* err = git_error_last();
  DAFENG_LOG_WARN("git_sync: %s failed: %s", what,
                   err && err->message ? err->message : "(no detail)");
}

// SSH credentials callback for push. Only used when push_url starts with
// git@... and SSH key paths are configured.
struct SshContext {
  std::string pub_key;
  std::string priv_key;
};

int CredentialsCallback(git_credential** out, const char* /*url*/,
                         const char* user_from_url, unsigned int allowed,
                         void* payload) {
  if (!(allowed & GIT_CREDENTIAL_SSH_KEY)) {
    return GIT_PASSTHROUGH;
  }
  auto* ctx = static_cast<SshContext*>(payload);
  if (ctx == nullptr || ctx->priv_key.empty()) return GIT_PASSTHROUGH;
  return git_credential_ssh_key_new(out, user_from_url ? user_from_url : "git",
                                      ctx->pub_key.empty() ? nullptr
                                                           : ctx->pub_key.c_str(),
                                      ctx->priv_key.c_str(), nullptr);
}

class LibGit2Sync final : public IGitSync {
 public:
  explicit LibGit2Sync(GitSyncConfig config) : config_(std::move(config)) {}

  ~LibGit2Sync() override {
    if (repo_ != nullptr) git_repository_free(repo_);
  }

  bool Init() override {
    if (repo_ != nullptr) return true;
    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.flags |= GIT_REPOSITORY_INIT_NO_REINIT;  // idempotent
    opts.flags |= GIT_REPOSITORY_INIT_MKDIR;
    opts.flags |= GIT_REPOSITORY_INIT_MKPATH;
    int rc = git_repository_init_ext(&repo_, config_.repo_path.c_str(), &opts);
    if (rc == GIT_EEXISTS) {
      // Already a repo — open it instead.
      rc = git_repository_open(&repo_, config_.repo_path.c_str());
    }
    if (rc != 0) {
      LogGitError("init/open");
      repo_ = nullptr;
      return false;
    }
    return true;
  }

  bool CommitAll(const std::string& message) override {
    if (repo_ == nullptr && !Init()) return false;

    git_index* index = nullptr;
    if (git_repository_index(&index, repo_) != 0) {
      LogGitError("repository_index");
      return false;
    }
    auto idx_guard = std::unique_ptr<git_index, decltype(&git_index_free)>(
        index, git_index_free);

    if (git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr,
                          nullptr) != 0) {
      LogGitError("index_add_all");
      return false;
    }
    if (git_index_write(index) != 0) {
      LogGitError("index_write");
      return false;
    }

    git_oid tree_oid;
    if (git_index_write_tree(&tree_oid, index) != 0) {
      LogGitError("write_tree");
      return false;
    }

    git_tree* tree = nullptr;
    if (git_tree_lookup(&tree, repo_, &tree_oid) != 0) {
      LogGitError("tree_lookup");
      return false;
    }
    auto tree_guard = std::unique_ptr<git_tree, decltype(&git_tree_free)>(
        tree, git_tree_free);

    git_signature* sig = nullptr;
    if (git_signature_now(&sig, config_.author_name.c_str(),
                            config_.author_email.c_str()) != 0) {
      LogGitError("signature_now");
      return false;
    }
    auto sig_guard =
        std::unique_ptr<git_signature, decltype(&git_signature_free)>(
            sig, git_signature_free);

    // If HEAD exists, parent it; otherwise this is the initial commit.
    git_commit* parent = nullptr;
    git_oid parent_oid;
    int has_head = git_reference_name_to_id(&parent_oid, repo_, "HEAD");
    if (has_head == 0) {
      if (git_commit_lookup(&parent, repo_, &parent_oid) != 0) {
        LogGitError("commit_lookup");
        return false;
      }
    }
    auto parent_guard = std::unique_ptr<git_commit, decltype(&git_commit_free)>(
        parent, git_commit_free);

    git_oid commit_oid;
    const git_commit* parents[1] = {parent};
    int n_parents = (parent != nullptr) ? 1 : 0;
    int rc = git_commit_create(&commit_oid, repo_, "HEAD", sig, sig, "UTF-8",
                                message.c_str(), tree, n_parents, parents);
    if (rc != 0) {
      LogGitError("commit_create");
      return false;
    }
    return true;
  }

  bool Push() override {
    if (!config_.push_enabled) {
      DAFENG_LOG_DEBUG("git_sync: push disabled by config — no-op");
      return true;
    }
    if (config_.push_url.empty()) {
      DAFENG_LOG_DEBUG("git_sync: push_url is empty — no-op");
      return true;
    }
    if (repo_ == nullptr && !Init()) return false;

    // Configure remote (idempotent: lookup-or-create).
    git_remote* remote = nullptr;
    int rc = git_remote_lookup(&remote, repo_, config_.remote_name.c_str());
    if (rc == GIT_ENOTFOUND) {
      rc = git_remote_create(&remote, repo_, config_.remote_name.c_str(),
                              config_.push_url.c_str());
    }
    if (rc != 0) {
      LogGitError("remote_lookup/create");
      return false;
    }
    auto remote_guard =
        std::unique_ptr<git_remote, decltype(&git_remote_free)>(
            remote, git_remote_free);

    SshContext ssh{config_.ssh_public_key, config_.ssh_private_key};

    git_push_options popts = GIT_PUSH_OPTIONS_INIT;
    popts.callbacks.credentials = CredentialsCallback;
    popts.callbacks.payload = &ssh;

    char refspec[] = "refs/heads/master:refs/heads/master";
    char* refspecs[] = {refspec};
    git_strarray refs = {refspecs, 1};
    if (git_remote_push(remote, &refs, &popts) != 0) {
      LogGitError("remote_push");
      return false;
    }
    return true;
  }

  bool HasChanges() override {
    if (repo_ == nullptr && !Init()) return false;
    git_status_options sopts = GIT_STATUS_OPTIONS_INIT;
    sopts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    sopts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                   GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
    git_status_list* statuses = nullptr;
    if (git_status_list_new(&statuses, repo_, &sopts) != 0) {
      LogGitError("status_list_new");
      return false;
    }
    const std::size_t n = git_status_list_entrycount(statuses);
    git_status_list_free(statuses);
    return n > 0;
  }

 private:
  Libgit2Lifecycle libgit2_;
  GitSyncConfig config_;
  git_repository* repo_ = nullptr;
};

}  // namespace

std::unique_ptr<IGitSync> MakeGitSync(const GitSyncConfig& config) {
  if (config.repo_path.empty()) return nullptr;
  return std::make_unique<LibGit2Sync>(config);
}

}  // namespace dafeng
