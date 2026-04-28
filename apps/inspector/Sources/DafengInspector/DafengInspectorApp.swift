import SwiftUI

// Without this, macOS quits the whole app when the user closes the
// inspector window — which would also remove the menu-bar item. This
// override keeps the app alive on close, so the menu bar keeps working
// and the user can re-open the window from the menu.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(
        _ sender: NSApplication) -> Bool { false }
}

@main
struct DafengInspectorApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var menubar = MenuBarController()

    var body: some Scene {
        // Main inspector window — Stats / History / Learned dict.
        WindowGroup("大风五笔检查器") {
            ContentView()
                .frame(minWidth: 720, minHeight: 480)
        }
        .windowResizability(.contentSize)

        // Status-bar item. macOS 13+; we target 14+.
        // `.menuBarExtraStyle(.menu)` makes it a native dropdown menu,
        // not a popover window — matches the convention every other
        // macOS daemon toggle uses (1Password, Rectangle, Bartender).
        MenuBarExtra {
            MenuBarContent(controller: menubar)
        } label: {
            Image(systemName: menubar.statusSymbol)
        }
        .menuBarExtraStyle(.menu)
    }
}
