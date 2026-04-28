import SwiftUI

// The dropdown body. SwiftUI's `MenuBarExtra` with `.menuBarExtraStyle(.menu)`
// renders this as a native NSMenu — buttons become menu items, Toggle
// becomes a checkmarked item, Divider turns into a separator. Stays out
// of the way of Dock + Cmd-Tab because we use `.menuBarExtraStyle(.menu)`
// (no popover window).
struct MenuBarContent: View {
    @ObservedObject var controller: MenuBarController

    var body: some View {
        // Status header — non-clickable summary.
        Text(controller.statusTitle)

        Divider()

        // Pause / resume.
        if controller.canPause {
            Button("暂停 daemon(IME 走原生 RIME)") {
                controller.pause()
            }
        }
        if controller.canResume {
            Button("恢复 daemon") {
                controller.resume()
            }
        }

        // Backend swap. Only meaningful when daemon is up; otherwise hidden.
        if controller.canToggleBackend {
            Divider()
            Section("后端") {
                Button(action: { controller.switchTo(.llama) }) {
                    HStack {
                        if controller.currentBackend == .llama {
                            Image(systemName: "checkmark")
                        }
                        Text("LLM(智能,~5 ms / 候选)")
                    }
                }
                Button(action: { controller.switchTo(.deterministic) }) {
                    HStack {
                        if controller.currentBackend == .deterministic {
                            Image(systemName: "checkmark")
                        }
                        Text("快速(省电,~3 µs / 候选)")
                    }
                }
            }
        }

        Divider()
        Button("打开检查器…") {
            controller.openInspectorWindow()
        }

        Divider()
        Button("退出大风五笔检查器") {
            controller.quit()
        }
        .keyboardShortcut("q", modifiers: .command)
    }
}
