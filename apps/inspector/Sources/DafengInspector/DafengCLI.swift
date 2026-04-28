import Foundation

// Thin wrapper around the `dafeng-cli` executable. We shell out instead of
// re-implementing the IPC client in Swift — the CLI is already maintained,
// covers the full protocol, and changes ship in lockstep with the daemon.
//
// The inspector finds dafeng-cli by probing $DAFENG_CLI, then $PATH, then
// the canonical install locations.
enum DafengCLI {
    /// Resolve the dafeng-cli binary path. Returns nil if it can't be found.
    static func locate() -> URL? {
        if let env = ProcessInfo.processInfo.environment["DAFENG_CLI"],
           !env.isEmpty,
           FileManager.default.isExecutableFile(atPath: env) {
            return URL(fileURLWithPath: env)
        }
        let candidates = [
            "/usr/local/bin/dafeng-cli",
            "/opt/homebrew/bin/dafeng-cli",
        ]
        for path in candidates {
            if FileManager.default.isExecutableFile(atPath: path) {
                return URL(fileURLWithPath: path)
            }
        }
        // Last resort: $PATH lookup via /usr/bin/which.
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/which")
        task.arguments = ["dafeng-cli"]
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError = Pipe()
        do {
            try task.run()
            task.waitUntilExit()
            if task.terminationStatus == 0 {
                let data = pipe.fileHandleForReading.readDataToEndOfFile()
                let trimmed = String(data: data, encoding: .utf8)?
                    .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                if !trimmed.isEmpty {
                    return URL(fileURLWithPath: trimmed)
                }
            }
        } catch {
            return nil
        }
        return nil
    }

    /// Run `dafeng-cli <args>` and return stdout (or nil on launch failure).
    /// Stderr is captured separately and returned alongside.
    @discardableResult
    static func run(arguments: [String], timeout: TimeInterval = 2.0)
        -> (stdout: String, stderr: String, exit: Int32)?
    {
        guard let bin = locate() else { return nil }
        let task = Process()
        task.executableURL = bin
        task.arguments = arguments

        let outPipe = Pipe()
        let errPipe = Pipe()
        task.standardOutput = outPipe
        task.standardError = errPipe

        do {
            try task.run()
        } catch {
            return nil
        }

        // Soft timeout: spin a one-shot watchdog that terminates the task
        // if it overshoots. Pure-CLI calls usually finish in tens of ms.
        let deadline = DispatchTime.now() + timeout
        DispatchQueue.global().asyncAfter(deadline: deadline) {
            if task.isRunning { task.terminate() }
        }
        task.waitUntilExit()

        let outData = outPipe.fileHandleForReading.readDataToEndOfFile()
        let errData = errPipe.fileHandleForReading.readDataToEndOfFile()
        return (
            String(data: outData, encoding: .utf8) ?? "",
            String(data: errData, encoding: .utf8) ?? "",
            task.terminationStatus
        )
    }
}
