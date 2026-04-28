import Foundation
import SQLite3

// Read-only view onto ~/Library/Application Support/Dafeng/history.db.
//
// Schema (must mirror src/daemon/history_store.cc):
//   CREATE TABLE commits (
//     id     INTEGER PRIMARY KEY AUTOINCREMENT,
//     ts_us  INTEGER NOT NULL,
//     code   TEXT NOT NULL,
//     text   TEXT NOT NULL,
//     ctx    TEXT NOT NULL
//   );
//
// We open with SQLITE_OPEN_READONLY so the inspector cannot accidentally
// corrupt or write to the live database while the daemon is also using
// it. WAL mode on the daemon side means readers don't block writers.

struct HistoryEntry: Identifiable, Hashable {
    let id: Int64
    let timestamp: Date
    let code: String
    let text: String
    let context: String
}

enum HistoryError: Error {
    case databaseMissing
    case openFailed(String)
    case prepareFailed(String)
    case stepFailed(String)
}

struct HistoryStore {
    static var defaultDatabaseURL: URL {
        let home = FileManager.default.homeDirectoryForCurrentUser
        return home
            .appendingPathComponent("Library/Application Support/Dafeng/history.db")
    }

    /// Returns up to `limit` most-recent entries optionally filtered by a
    /// case-insensitive substring match on text or context.
    /// `since` (epoch seconds) optionally restricts to entries newer than t.
    static func recent(
        databaseURL: URL = defaultDatabaseURL,
        limit: Int = 200,
        textFilter: String? = nil
    ) throws -> [HistoryEntry] {
        guard FileManager.default.fileExists(atPath: databaseURL.path) else {
            throw HistoryError.databaseMissing
        }

        var db: OpaquePointer?
        let openFlags = SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX
        guard sqlite3_open_v2(databaseURL.path, &db, openFlags, nil) == SQLITE_OK,
              let db
        else {
            let err = sqlite3_errmsg(db).map { String(cString: $0) } ?? "?"
            sqlite3_close_v2(db)
            throw HistoryError.openFailed(err)
        }
        defer { sqlite3_close_v2(db) }

        let hasFilter = !(textFilter?.isEmpty ?? true)
        let sql = hasFilter
            ? "SELECT id, ts_us, code, text, ctx FROM commits "
              + "WHERE text LIKE ?1 OR ctx LIKE ?1 "
              + "ORDER BY ts_us DESC LIMIT ?2"
            : "SELECT id, ts_us, code, text, ctx FROM commits "
              + "ORDER BY ts_us DESC LIMIT ?1"

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK,
              let stmt
        else {
            throw HistoryError.prepareFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }

        // SQLITE_TRANSIENT pattern: pass via static, see SQLite docs.
        let SQLITE_TRANSIENT = unsafeBitCast(-1, to: sqlite3_destructor_type.self)
        if hasFilter {
            let pattern = "%\(textFilter!)%"
            sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT)
            sqlite3_bind_int(stmt, 2, Int32(limit))
        } else {
            sqlite3_bind_int(stmt, 1, Int32(limit))
        }

        var out: [HistoryEntry] = []
        while true {
            let rc = sqlite3_step(stmt)
            if rc == SQLITE_DONE { break }
            if rc != SQLITE_ROW {
                throw HistoryError.stepFailed(String(cString: sqlite3_errmsg(db)))
            }
            let id = sqlite3_column_int64(stmt, 0)
            let tsUs = sqlite3_column_int64(stmt, 1)
            let code = String(cString: sqlite3_column_text(stmt, 2))
            let text = String(cString: sqlite3_column_text(stmt, 3))
            let ctx  = String(cString: sqlite3_column_text(stmt, 4))
            out.append(HistoryEntry(
                id: id,
                timestamp: Date(timeIntervalSince1970: Double(tsUs) / 1_000_000),
                code: code,
                text: text,
                context: ctx
            ))
        }
        return out
    }

    /// Total number of rows in `commits`. Cheap (uses sqlite_stat1 if
    /// available) but not aggressively cached — call sparingly.
    static func totalCount(databaseURL: URL = defaultDatabaseURL) throws -> Int64 {
        guard FileManager.default.fileExists(atPath: databaseURL.path) else {
            throw HistoryError.databaseMissing
        }
        var db: OpaquePointer?
        guard sqlite3_open_v2(databaseURL.path, &db,
                              SQLITE_OPEN_READONLY, nil) == SQLITE_OK,
              let db
        else {
            sqlite3_close_v2(db)
            throw HistoryError.openFailed("(open failed)")
        }
        defer { sqlite3_close_v2(db) }

        var stmt: OpaquePointer?
        let sql = "SELECT COUNT(*) FROM commits"
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK,
              let stmt
        else {
            throw HistoryError.prepareFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }

        guard sqlite3_step(stmt) == SQLITE_ROW else {
            throw HistoryError.stepFailed("(no row)")
        }
        return sqlite3_column_int64(stmt, 0)
    }
}
