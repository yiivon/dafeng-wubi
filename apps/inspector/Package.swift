// swift-tools-version:5.9
//
// Dafeng Inspector — a small SwiftUI app that reads
//   1. dafeng-cli stats   (daemon health, counters, latency)
//   2. ~/Library/Application Support/Dafeng/history.db   (commit history)
//   3. ~/Library/Rime/dafeng_learned.dict.yaml           (learned phrases)
//
// Built with Swift Package Manager so it lives next to the C++ tree
// without dragging an .xcodeproj. The packaging script wraps the built
// executable in a minimal `.app` bundle.

import PackageDescription

let package = Package(
    name: "DafengInspector",
    platforms: [
        .macOS(.v14),
    ],
    products: [
        .executable(name: "DafengInspector", targets: ["DafengInspector"]),
    ],
    targets: [
        .executableTarget(
            name: "DafengInspector",
            path: "Sources/DafengInspector"
        ),
    ]
)
