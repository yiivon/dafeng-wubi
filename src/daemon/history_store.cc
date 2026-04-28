#include "history_store.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <utility>

#include <sqlite3.h>

#if defined(_WIN32)
// Windows ACL inheritance under %APPDATA% is owner-only by default; we
// rely on that and skip the explicit chmod step below.
#else
#include <sys/stat.h>
#endif

#include "dafeng/logging.h"

namespace dafeng {

namespace {

// Privacy guard: refuse if `target` is not a descendant of `root`. Both
// must exist or be reachable; we do a normalized-prefix comparison.
bool PathIsUnder(const std::filesystem::path& target,
                  const std::filesystem::path& root) {
  std::error_code ec;
  auto t = std::filesystem::weakly_canonical(target, ec);
  if (ec) return false;
  auto r = std::filesystem::weakly_canonical(root, ec);
  if (ec) return false;
  // Both paths must be absolute for a meaningful comparison.
  if (!t.is_absolute() || !r.is_absolute()) return false;
  auto t_str = t.string();
  auto r_str = r.string();
  if (t_str.size() < r_str.size()) return false;
  if (t_str.compare(0, r_str.size(), r_str) != 0) return false;
  // Avoid /Foo/bar matching /Foo/barbaz: the next char must be a separator
  // or the strings must be equal.
  if (t_str.size() == r_str.size()) return true;
  return t_str[r_str.size()] == std::filesystem::path::preferred_separator;
}

uint64_t NowUs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

class SqliteHistoryStore final : public IHistoryStore {
 public:
  explicit SqliteHistoryStore(sqlite3* db) : db_(db) {}

  ~SqliteHistoryStore() override {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_ != nullptr) {
      sqlite3_close_v2(db_);
      db_ = nullptr;
    }
  }

  bool Insert(const CommitEvent& ev) override {
    std::lock_guard<std::mutex> lock(mu_);
    static constexpr const char* kSql =
        "INSERT INTO commits(ts_us, code, text, ctx) VALUES(?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
      DAFENG_LOG_WARN("history: prepare insert failed: %s",
                       sqlite3_errmsg(db_));
      return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(NowUs()));
    sqlite3_bind_text(stmt, 2, ev.code.c_str(),
                      static_cast<int>(ev.code.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ev.committed_text.c_str(),
                      static_cast<int>(ev.committed_text.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, ev.context_before.c_str(),
                      static_cast<int>(ev.context_before.size()),
                      SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
  }

  std::vector<HistoryEntry> RecentEntries(uint64_t limit) override {
    std::vector<HistoryEntry> out;
    std::lock_guard<std::mutex> lock(mu_);
    static constexpr const char* kSql =
        "SELECT id, ts_us, code, text, ctx FROM commits "
        "ORDER BY ts_us DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
      return out;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      HistoryEntry e;
      e.id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
      e.ts_us = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
      e.code = ColumnText(stmt, 2);
      e.committed_text = ColumnText(stmt, 3);
      e.context_before = ColumnText(stmt, 4);
      out.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return out;
  }

  uint64_t Count() override {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM commits", -1, &stmt,
                            nullptr) != SQLITE_OK) {
      return 0;
    }
    uint64_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      n = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return n;
  }

  uint64_t Prune(std::chrono::seconds older_than) override {
    std::lock_guard<std::mutex> lock(mu_);
    const uint64_t cutoff_us =
        NowUs() - static_cast<uint64_t>(older_than.count()) * 1'000'000ULL;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM commits WHERE ts_us < ?", -1,
                            &stmt, nullptr) != SQLITE_OK) {
      return 0;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(cutoff_us));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
    return static_cast<uint64_t>(sqlite3_changes(db_));
  }

  bool Vacuum() override {
    std::lock_guard<std::mutex> lock(mu_);
    return sqlite3_exec(db_, "VACUUM", nullptr, nullptr, nullptr) == SQLITE_OK;
  }

 private:
  static std::string ColumnText(sqlite3_stmt* stmt, int idx) {
    const unsigned char* p = sqlite3_column_text(stmt, idx);
    if (p == nullptr) return {};
    int n = sqlite3_column_bytes(stmt, idx);
    return std::string(reinterpret_cast<const char*>(p),
                        static_cast<size_t>(n));
  }

  std::mutex mu_;
  sqlite3* db_ = nullptr;
};

bool ApplySchema(sqlite3* db) {
  static constexpr const char* kStmts =
      "PRAGMA journal_mode=WAL;"
      "PRAGMA synchronous=NORMAL;"
      "PRAGMA foreign_keys=ON;"
      "CREATE TABLE IF NOT EXISTS commits ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  ts_us INTEGER NOT NULL,"
      "  code TEXT NOT NULL,"
      "  text TEXT NOT NULL,"
      "  ctx TEXT NOT NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS idx_commits_ts ON commits(ts_us);"
      "CREATE INDEX IF NOT EXISTS idx_commits_text ON commits(text);"
      "CREATE TABLE IF NOT EXISTS meta ("
      "  k TEXT PRIMARY KEY,"
      "  v TEXT"
      ");";
  char* err = nullptr;
  if (sqlite3_exec(db, kStmts, nullptr, nullptr, &err) != SQLITE_OK) {
    DAFENG_LOG_ERROR("history: schema apply failed: %s", err ? err : "?");
    sqlite3_free(err);
    return false;
  }
  return true;
}

}  // namespace

std::unique_ptr<IHistoryStore> MakeSqliteHistoryStore(
    const std::filesystem::path& db_path,
    const std::filesystem::path& data_root) {
  // Privacy guard: db_path must be inside data_root. This blocks an
  // accidental config that points history.db at e.g. ~/Documents/.
  if (!PathIsUnder(db_path, data_root)) {
    DAFENG_LOG_ERROR(
        "history: refusing to open %s — not under data root %s. This is the "
        "CLAUDE.md privacy boundary.",
        db_path.string().c_str(), data_root.string().c_str());
    return nullptr;
  }

  std::error_code ec;
  std::filesystem::create_directories(db_path.parent_path(), ec);
  if (ec) {
    DAFENG_LOG_ERROR("history: parent mkdir failed: %s", ec.message().c_str());
    return nullptr;
  }

  sqlite3* db = nullptr;
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                     SQLITE_OPEN_FULLMUTEX;
  // SQLite documents path strings as UTF-8. On Windows, path::c_str() is
  // wchar_t* and path::string() is in the active code page; only u8string()
  // gives us UTF-8. On POSIX u8string() is just the native bytes (already
  // UTF-8 on macOS/Linux), so the same call works cross-platform.
  const std::string db_path_utf8 = db_path.u8string();
  if (sqlite3_open_v2(db_path_utf8.c_str(), &db, flags, nullptr) != SQLITE_OK) {
    DAFENG_LOG_ERROR("history: open failed: %s",
                       db ? sqlite3_errmsg(db) : "(null)");
    if (db != nullptr) sqlite3_close_v2(db);
    return nullptr;
  }

#if !defined(_WIN32)
  // Enforce file mode 0600 — owner read/write only. SQLite respects umask
  // but this is the explicit version. On Windows, ACL inheritance from
  // %APPDATA%/Dafeng/ already restricts the file to the current user.
  ::chmod(db_path_utf8.c_str(), S_IRUSR | S_IWUSR);
#endif

  if (!ApplySchema(db)) {
    sqlite3_close_v2(db);
    return nullptr;
  }
  return std::make_unique<SqliteHistoryStore>(db);
}

}  // namespace dafeng
