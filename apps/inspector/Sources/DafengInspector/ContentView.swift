import SwiftUI

enum InspectorTab: String, CaseIterable, Identifiable {
    case stats = "状态"
    case history = "输入历史"
    case learned = "已学到的词"

    var id: String { rawValue }

    var systemImage: String {
        switch self {
        case .stats:   return "gauge.with.dots.needle.50percent"
        case .history: return "clock.arrow.circlepath"
        case .learned: return "books.vertical"
        }
    }
}

struct ContentView: View {
    @State private var selection: InspectorTab = .stats

    var body: some View {
        NavigationSplitView {
            List(InspectorTab.allCases, selection: $selection) { tab in
                Label(tab.rawValue, systemImage: tab.systemImage)
                    .tag(tab)
            }
            .navigationSplitViewColumnWidth(180)
        } detail: {
            switch selection {
            case .stats:   StatsView()
            case .history: HistoryView()
            case .learned: LearnedDictView()
            }
        }
    }
}
