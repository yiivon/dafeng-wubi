import SwiftUI

@MainActor
final class LearnedDictViewModel: ObservableObject {
    @Published var entries: [LearnedEntry] = []
    @Published var filter: String = ""
    @Published var loading: Bool = false
    @Published var errorMessage: String? = nil
    @Published var redeployHint: Bool = false

    private var header: String = ""

    var filteredEntries: [LearnedEntry] {
        let q = filter.trimmingCharacters(in: .whitespacesAndNewlines)
        if q.isEmpty { return entries }
        return entries.filter { e in
            e.text.localizedStandardContains(q) ||
            e.code.localizedStandardContains(q)
        }
    }

    func refresh() async {
        loading = true
        defer { loading = false }
        do {
            let (h, e) = try LearnedDict.load()
            header = h
            entries = e
            errorMessage = nil
        } catch LearnedDictError.fileMissing {
            errorMessage = "尚未生成 dafeng_learned.dict.yaml — 跑一次 `dafeng-cli learn` 试试。"
            entries = []
        } catch {
            errorMessage = "\(error)"
            entries = []
        }
    }

    func delete(_ entry: LearnedEntry) async {
        let surviving = entries.filter { $0 != entry }
        do {
            try LearnedDict.save(header: header, entries: surviving)
            entries = surviving
            redeployHint = true
        } catch {
            errorMessage = "写入失败:\(error)"
        }
    }
}

struct LearnedDictView: View {
    @StateObject private var vm = LearnedDictViewModel()
    @State private var pendingDelete: LearnedEntry? = nil

    var body: some View {
        VStack(spacing: 0) {
            controlBar
            Divider()
            if let msg = vm.errorMessage {
                emptyState(message: msg, symbol: "exclamationmark.octagon")
            } else if vm.entries.isEmpty && !vm.loading {
                emptyState(message: "未学到任何词。", symbol: "tray")
            } else {
                table
            }
            if vm.redeployHint { redeployBanner }
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
        .alert("删除这条学到的词?",
               isPresented: Binding(
                 get: { pendingDelete != nil },
                 set: { if !$0 { pendingDelete = nil } })) {
            Button("取消", role: .cancel) { pendingDelete = nil }
            Button("删除", role: .destructive) {
                if let e = pendingDelete {
                    Task {
                        await vm.delete(e)
                        pendingDelete = nil
                    }
                }
            }
        } message: {
            if let e = pendingDelete {
                Text("\(e.text)  ·  码:\(e.code)  ·  权重:\(e.weight)\n\n删除后下次重新部署即生效。")
            }
        }
        .task { await vm.refresh() }
    }

    @ViewBuilder
    private var controlBar: some View {
        HStack {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            TextField("过滤(text 或 code 子串)", text: $vm.filter)
                .textFieldStyle(.roundedBorder)
            if vm.loading {
                ProgressView().controlSize(.small)
            }
            Text("共 \(vm.entries.count) 条").foregroundStyle(.secondary)
        }
        .padding(12)
    }

    private var table: some View {
        Table(vm.filteredEntries) {
            TableColumn("文字", value: \.text)
                .width(min: 100, ideal: 160)
            TableColumn("码", value: \.code)
                .width(min: 80, ideal: 100)
            TableColumn("权重") { e in
                Text("\(e.weight)").monospacedDigit()
            }
            .width(min: 60, ideal: 70)
            TableColumn("") { e in
                Button(role: .destructive) {
                    pendingDelete = e
                } label: {
                    Label("删除", systemImage: "trash")
                        .labelStyle(.iconOnly)
                }
                .buttonStyle(.borderless)
                .help("从学到的词典中移除")
            }
            .width(40)
        }
    }

    private var redeployBanner: some View {
        HStack(spacing: 12) {
            Image(systemName: "info.circle.fill").foregroundStyle(.blue)
            Text("已删除。请到 **菜单栏鼠须管 → 重新部署** 让删除生效。")
            Spacer()
            Button("好") { vm.redeployHint = false }
                .buttonStyle(.borderless)
        }
        .padding(12)
        .background(.blue.opacity(0.08))
    }

    private func emptyState(message: String, symbol: String) -> some View {
        VStack(spacing: 12) {
            Image(systemName: symbol)
                .font(.system(size: 40))
                .foregroundStyle(.secondary)
            Text(message)
                .multilineTextAlignment(.center)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
