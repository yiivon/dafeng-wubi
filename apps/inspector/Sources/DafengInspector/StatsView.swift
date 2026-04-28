import SwiftUI

struct DaemonStats {
    var uptimeSeconds: Int = 0
    var modelName: String = "—"
    var modelVersion: Int = 0
    var rerankCount: Int = 0
    var pingCount: Int = 0
    var commitCount: Int = 0
    var errorCount: Int = 0
    var meanLatencyUs: Int = 0
    var maxLatencyUs: Int = 0
}

enum DaemonState {
    case unknown
    case offline
    case online(DaemonStats)
    case toolMissing
    case error(String)
}

@MainActor
final class StatsViewModel: ObservableObject {
    @Published var state: DaemonState = .unknown
    @Published var lastRefresh: Date? = nil
    private var refreshTask: Task<Void, Never>? = nil

    func startAutoRefresh() {
        refreshTask?.cancel()
        refreshTask = Task { [weak self] in
            while !Task.isCancelled {
                await self?.refresh()
                try? await Task.sleep(nanoseconds: 3_000_000_000)
            }
        }
    }

    func stopAutoRefresh() {
        refreshTask?.cancel()
        refreshTask = nil
    }

    func refresh() async {
        let result = await Task.detached(priority: .userInitiated) {
            DafengCLI.run(arguments: ["stats"])
        }.value
        guard let r = result else {
            self.state = .toolMissing
            self.lastRefresh = Date()
            return
        }
        if r.exit != 0 {
            // Non-zero exit + stderr "stats failed" = daemon not running.
            if r.stderr.contains("daemon unreachable") || r.stderr.contains("timeout") {
                self.state = .offline
            } else {
                self.state = .error(r.stderr.trimmingCharacters(in: .whitespacesAndNewlines))
            }
            self.lastRefresh = Date()
            return
        }
        self.state = .online(parse(r.stdout))
        self.lastRefresh = Date()
    }

    private func parse(_ stdout: String) -> DaemonStats {
        var s = DaemonStats()
        for raw in stdout.split(whereSeparator: { $0.isNewline }) {
            let line = String(raw)
            guard let colon = line.firstIndex(of: ":") else { continue }
            let key = line[..<colon].trimmingCharacters(in: .whitespaces)
            let value = line[line.index(after: colon)...]
                .trimmingCharacters(in: .whitespaces)

            switch key {
            case "daemon uptime":
                s.uptimeSeconds = parseLeadingInt(value)
            case "rerank model":
                // "llama_cpp (v3)"
                let parts = value.split(separator: " ", maxSplits: 1)
                if let name = parts.first { s.modelName = String(name) }
                if let v = parts.last,
                   let lhs = v.firstIndex(of: "v"),
                   let rhs = v.lastIndex(of: ")"),
                   lhs < rhs {
                    s.modelVersion = Int(v[v.index(after: lhs)..<rhs]) ?? 0
                }
            case "rerank requests": s.rerankCount = parseLeadingInt(value)
            case "ping requests":   s.pingCount = parseLeadingInt(value)
            case "commit events":   s.commitCount = parseLeadingInt(value)
            case "errors":          s.errorCount = parseLeadingInt(value)
            case "rerank mean (us)": s.meanLatencyUs = parseLeadingInt(value)
            case "rerank max  (us)": s.maxLatencyUs = parseLeadingInt(value)
            default: break
            }
        }
        return s
    }

    private func parseLeadingInt(_ s: String) -> Int {
        var digits = ""
        for ch in s {
            if ch.isNumber { digits.append(ch) } else { break }
        }
        return Int(digits) ?? 0
    }
}

struct StatsView: View {
    @StateObject private var vm = StatsViewModel()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                header
                Divider()
                content
                Spacer(minLength: 24)
            }
            .padding(20)
        }
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    Task { await vm.refresh() }
                } label: {
                    Label("刷新", systemImage: "arrow.clockwise")
                }
                .keyboardShortcut("r", modifiers: .command)
            }
        }
        .task {
            vm.startAutoRefresh()
        }
        .onDisappear { vm.stopAutoRefresh() }
    }

    @ViewBuilder
    private var header: some View {
        HStack {
            Text("Daemon 状态").font(.title2).bold()
            Spacer()
            if let ts = vm.lastRefresh {
                Text("刷新于 \(ts.formatted(date: .omitted, time: .standard))")
                    .foregroundStyle(.secondary)
                    .font(.callout)
            }
        }
    }

    @ViewBuilder
    private var content: some View {
        switch vm.state {
        case .unknown:
            ProgressView("查询中…").frame(maxWidth: .infinity)
        case .toolMissing:
            offlineCard(symbol: "xmark.octagon.fill",
                        tint: .red,
                        title: "找不到 dafeng-cli",
                        body: "请确认输入法已安装,或设置环境变量 DAFENG_CLI 指向 dafeng-cli。")
        case .offline:
            offlineCard(symbol: "powerplug",
                        tint: .orange,
                        title: "Daemon 未运行",
                        body: "可在终端运行 `launchctl kickstart gui/$UID/com.dafeng.daemon` 重启。")
        case .error(let msg):
            offlineCard(symbol: "exclamationmark.triangle.fill",
                        tint: .red,
                        title: "查询出错",
                        body: msg.isEmpty ? "(无错误信息)" : msg)
        case .online(let s):
            onlineGrid(s)
        }
    }

    private func offlineCard(symbol: String, tint: Color, title: String, body: String) -> some View {
        HStack(alignment: .top, spacing: 16) {
            Image(systemName: symbol)
                .font(.system(size: 32))
                .foregroundStyle(tint)
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(.headline)
                Text(body).foregroundStyle(.secondary)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.background.secondary)
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private func onlineGrid(_ s: DaemonStats) -> some View {
        let cols = [GridItem(.adaptive(minimum: 200, maximum: 280), spacing: 12)]
        return LazyVGrid(columns: cols, spacing: 12) {
            statCard("已运行", value: formatUptime(s.uptimeSeconds), symbol: "clock")
            statCard("Rerank 后端", value: "\(s.modelName) v\(s.modelVersion)", symbol: "cpu")
            statCard("Rerank 请求", value: "\(s.rerankCount)", symbol: "arrow.triangle.2.circlepath")
            statCard("Ping 请求", value: "\(s.pingCount)", symbol: "antenna.radiowaves.left.and.right")
            statCard("提交事件", value: "\(s.commitCount)", symbol: "checkmark.circle")
            statCard("错误", value: "\(s.errorCount)",
                     symbol: "exclamationmark.triangle",
                     tint: s.errorCount > 0 ? .orange : .secondary)
            statCard("平均延迟", value: formatLatency(s.meanLatencyUs), symbol: "speedometer")
            statCard("最大延迟", value: formatLatency(s.maxLatencyUs),
                     symbol: "gauge.with.dots.needle.bottom.50percent",
                     tint: s.maxLatencyUs > 30_000 ? .orange : .secondary)
        }
    }

    private func statCard(_ title: String, value: String, symbol: String, tint: Color = .secondary) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 6) {
                Image(systemName: symbol).foregroundStyle(tint)
                Text(title).font(.callout).foregroundStyle(.secondary)
            }
            Text(value).font(.title3).bold().monospacedDigit()
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.background.secondary)
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private func formatUptime(_ s: Int) -> String {
        if s < 60 { return "\(s) 秒" }
        let m = s / 60, h = m / 60, d = h / 24
        if d > 0 { return "\(d) 天 \(h % 24) 小时" }
        if h > 0 { return "\(h) 小时 \(m % 60) 分" }
        return "\(m) 分 \(s % 60) 秒"
    }

    private func formatLatency(_ us: Int) -> String {
        if us == 0 { return "—" }
        if us < 1000 { return "\(us) µs" }
        return String(format: "%.2f ms", Double(us) / 1000.0)
    }
}
