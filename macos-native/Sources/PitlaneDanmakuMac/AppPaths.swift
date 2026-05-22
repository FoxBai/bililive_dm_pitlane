import AppKit
import Foundation

enum AppPaths {
    private static let supportFolderName = "PitlaneDanmaku"

    static var supportDirectory: URL {
        if let override = ProcessInfo.processInfo.environment["PITLANE_SUPPORT_DIR"], !override.isEmpty {
            return URL(fileURLWithPath: override, isDirectory: true)
        }

        let root = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        return root.appendingPathComponent(supportFolderName, isDirectory: true)
    }

    static var settingsURL: URL {
        supportDirectory.appendingPathComponent("settings-macos.json")
    }

    static var diagnosticsDirectory: URL {
        supportDirectory.appendingPathComponent("Diagnostics", isDirectory: true)
    }

    static var crashReportsDirectory: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Logs/DiagnosticReports", isDirectory: true)
    }

    static func ensureRuntimeDirectories() {
        try? FileManager.default.createDirectory(at: supportDirectory, withIntermediateDirectories: true)
        try? FileManager.default.createDirectory(at: diagnosticsDirectory, withIntermediateDirectories: true)
    }

    static func openDirectory(_ url: URL) {
        ensureRuntimeDirectories()
        if FileManager.default.fileExists(atPath: url.path) {
            NSWorkspace.shared.open(url)
        } else {
            NSWorkspace.shared.open(url.deletingLastPathComponent())
        }
    }
}
