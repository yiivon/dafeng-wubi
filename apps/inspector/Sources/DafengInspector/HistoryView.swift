import SwiftUI

@MainActor
final class HistoryViewModel: ObservableObject {
    @Published var entries: [HistoryEntry] = []
    @Published var totalCount: Int64 = 0
    @Published var filter: String = ""
    @Published var limit: Int = 200
    @Published var loading: Bool = false
    @Published var errorMessage: String? = nil

    func refresh() async {
        loading = true
        defer { loading = false }
        let trimmed = filter.trimmingCharacters(in: .whitespacesAndNewlines)
        let queryFilter: String? = trimmed.isEmpty ? nil : trimmed
        let limitCopy = limit
        let result = await Task.detached(priority: .userInitiated) {
            do {
                let rows = try HistoryStore.recent(limit: limitCopy,
                                                   textFilter: queryFilter)
                let total = (try? HistoryStore.totalCount()) ?? Int64(rows.count)
                return Result<(rows: [HistoryEntry], total: Int64), Error>
                    .success((rows, total))
            } catch {
                return .failure(error)
            }
        }.value
        switch result {
        case .success(let payload):
            entries = payload.rows
            totalCount = payload.total
            errorMessage = nil
        case .failure(let err):
            entries = []
            totalCount = 0
            switch err {
            case HistoryError.databaseMissing:
                errorMessage = "history.db 不存在 — 还没有任何输入历史吗?"
            case HistoryError.openFailed(let msg):
                errorMessage = "打开数据库失败:\(msg)"
            default:
                errorMessage = "\(err)"
            }
        }
    }
}

struct HistoryView: View {
    @StateObject private var vm = HistoryViewModel()
    @State private var selectedID: Int64? = nil

    var body: some View {
        VStack(spacing: 0) {
            controlBar
            Divider()
            if let msg = vm.errorMessage {
                emptyState(message: msg, symbol: "exclamationmark.octagon")
            } else if vm.entries.isEmpty && !vm.loading {
                emptyState(message: "无匹配记录", symbol: "tray")
            } else {
                table
            }
        }
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    Task { await vm.refresh() }
                } label: {
                    Label("刷新", systemImage: "arrow.clockwise")
                }
                .disabled(vm.loading)
                .keyboardShortcut("r", modifiers: .command)
            }
        }
        .task { await vm.refresh() }
    }

    @ViewBuilder
    private var controlBar: some View {
        HStack {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            TextField("过滤(text 或 ctx 子串)", text: $vm.filter)
                .textFieldStyle(.roundedBorder)
                .onSubmit { Task { await vm.refresh() } }
            Picker("条数", selection: $vm.limit) {
                Text("100").tag(100)
                Text("200").tag(200)
                Text("500").tag(500)
                Text("1000").tag(1000)
            }
            .pickerStyle(.menu)
            .onChange(of: vm.limit) { _, _ in
                Task { await vm.refresh() }
            }
            if vm.loading {
                ProgressView().controlSize(.small)
            }
            Text("共 \(vm.totalCount) 条").foregroundStyle(.secondary)
        }
        .padding(12)
    }

    private var table: some View {
        Table(vm.entries, selection: $selectedID) {
            TableColumn("时间") { e in
                Text(e.timestamp.formatted(date: .numeric, time: .standard))
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }
            .width(min: 140, ideal: 160)
            TableColumn("码", value: \.code)
                .width(min: 60, ideal: 80)
            TableColumn("文字", value: \.text)
                .width(min: 80, ideal: 120)
            TableColumn("上下文") { e in
                Text(e.context)
                    .lineLimit(1)
                    .truncationMode(.tail)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func emptyState(message: String, symbol: String) -> some View {
        VStack(spacing: 12) {
            Image(systemName: symbol)
                .font(.system(size: 40))
                .foregroundStyle(.secondary)
            Text(message).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
