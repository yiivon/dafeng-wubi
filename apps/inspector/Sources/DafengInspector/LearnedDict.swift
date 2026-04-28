import Foundation

// Read/write ~/Library/Rime/dafeng_learned.dict.yaml.
//
// The file has a YAML header (between two `---` / `...` markers) and
// then a series of TSV records: text<TAB>code<TAB>weight. We don't
// pull in a YAML library — the header is intentionally simple and
// only the metadata fields we know about appear there. We preserve
// the header bytes verbatim and only edit the TSV body.

struct LearnedEntry: Identifiable, Hashable {
    let id = UUID()
    let text: String
    let code: String
    let weight: Int
}

enum LearnedDictError: Error {
    case fileMissing
    case readFailed(String)
    case writeFailed(String)
    case parseFailed(String)
}

struct LearnedDict {
    static var defaultURL: URL {
        let home = FileManager.default.homeDirectoryForCurrentUser
        return home.appendingPathComponent("Library/Rime/dafeng_learned.dict.yaml")
    }

    /// Returns (header bytes preserved verbatim, list of entries).
    static func load(from url: URL = defaultURL) throws -> (header: String, entries: [LearnedEntry]) {
        guard FileManager.default.fileExists(atPath: url.path) else {
            throw LearnedDictError.fileMissing
        }
        let raw: String
        do {
            raw = try String(contentsOf: url, encoding: .utf8)
        } catch {
            throw LearnedDictError.readFailed("\(error)")
        }
        // Header ends at the first `\n...\n` (YAML end-of-document marker).
        // We keep everything up to and including that marker as the header.
        let (header, body) = splitHeaderBody(raw)
        var entries: [LearnedEntry] = []
        for raw in body.split(whereSeparator: { $0.isNewline }) {
            let line = String(raw).trimmingCharacters(in: .whitespacesAndNewlines)
            if line.isEmpty || line.hasPrefix("#") { continue }
            // text<TAB>code<TAB>weight   (some files have only text<TAB>code)
            let cols = line.components(separatedBy: "\t")
            guard cols.count >= 2 else { continue }
            let weight = cols.count >= 3 ? Int(cols[2]) ?? 1 : 1
            entries.append(LearnedEntry(text: cols[0],
                                        code: cols[1],
                                        weight: weight))
        }
        return (header, entries)
    }

    /// Rewrite the file with the given entries, keeping `header` verbatim.
    /// Caller is responsible for cuing `菜单栏 鼠须管 → 重新部署` afterward.
    static func save(header: String, entries: [LearnedEntry], to url: URL = defaultURL) throws {
        var out = header
        if !out.hasSuffix("\n") { out.append("\n") }
        for e in entries.sorted(by: { ($0.weight, $0.code) > ($1.weight, $1.code) }) {
            out.append("\(e.text)\t\(e.code)\t\(e.weight)\n")
        }
        do {
            try out.write(to: url, atomically: true, encoding: .utf8)
        } catch {
            throw LearnedDictError.writeFailed("\(error)")
        }
    }

    private static func splitHeaderBody(_ raw: String) -> (String, String) {
        // Find a line that's exactly "..." after a newline.
        let lines = raw.components(separatedBy: "\n")
        var endIdx: Int? = nil
        for (i, line) in lines.enumerated() {
            if line.trimmingCharacters(in: .whitespaces) == "..." {
                endIdx = i
                break
            }
        }
        guard let end = endIdx else {
            // No `...` marker; treat whole file as body.
            return ("", raw)
        }
        let header = lines[0...end].joined(separator: "\n") + "\n"
        let body = lines[(end + 1)...].joined(separator: "\n")
        return (header, body)
    }
}
