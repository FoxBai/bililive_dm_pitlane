import Foundation

enum SettingsStore {
    private static let currentSchemaVersion = 1

    static var settingsURL: URL {
        AppPaths.settingsURL
    }

    static func load() -> AppSettings {
        do {
            let data = try Data(contentsOf: settingsURL)
            return try decodeSettings(from: data)
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
            try encodeSettings(normalized).write(to: settingsURL, options: .atomic)
        } catch {
            // Settings persistence should never interrupt the overlay pipeline.
        }
    }

    static func decodeSettings(from data: Data) throws -> AppSettings {
        let decoder = JSONDecoder()

        if let envelope = try? decoder.decode(SettingsEnvelope.self, from: data),
           envelope.schemaVersion <= currentSchemaVersion {
            var settings = envelope.settings
            settings.normalize()
            return settings
        }

        var legacySettings = try decoder.decode(AppSettings.self, from: data)
        legacySettings.normalize()
        return legacySettings
    }

    static func encodeSettings(_ settings: AppSettings) throws -> Data {
        var normalized = settings
        normalized.normalize()

        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try encoder.encode(SettingsEnvelope(
            schemaVersion: currentSchemaVersion,
            settings: normalized
        ))
    }

    private struct SettingsEnvelope: Codable {
        var schemaVersion: Int
        var settings: AppSettings
    }
}
