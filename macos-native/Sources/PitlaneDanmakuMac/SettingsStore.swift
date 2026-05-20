import Foundation

enum SettingsStore {
    static var settingsURL: URL {
        let root = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        return root.appendingPathComponent("PitlaneDanmaku", isDirectory: true)
            .appendingPathComponent("settings-macos.json")
    }

    static func load() -> AppSettings {
        do {
            let data = try Data(contentsOf: settingsURL)
            var settings = try JSONDecoder().decode(AppSettings.self, from: data)
            settings.normalize()
            return settings
        } catch {
            var settings = AppSettings()
            settings.normalize()
            return settings
        }
    }

    static func save(_ settings: AppSettings) {
        do {
            var normalized = settings
            normalized.normalize()
            let directory = settingsURL.deletingLastPathComponent()
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(normalized).write(to: settingsURL, options: .atomic)
        } catch {
            // Settings persistence should never interrupt the overlay pipeline.
        }
    }
}
