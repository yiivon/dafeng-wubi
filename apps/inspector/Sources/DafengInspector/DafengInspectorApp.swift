import SwiftUI

@main
struct DafengInspectorApp: App {
    var body: some Scene {
        WindowGroup("大风五笔检查器") {
            ContentView()
                .frame(minWidth: 720, minHeight: 480)
        }
        .windowResizability(.contentSize)
    }
}
