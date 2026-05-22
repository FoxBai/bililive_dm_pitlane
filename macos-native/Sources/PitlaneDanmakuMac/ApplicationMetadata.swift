import Foundation

enum ApplicationMetadata {
    static var name: String {
        bundleString("CFBundleName") ?? "Pitlane Danmaku"
    }

    static var version: String {
        bundleString("CFBundleShortVersionString") ?? "0.1.0"
    }

    static var build: String {
        bundleString("CFBundleVersion") ?? "1"
    }

    static var copyright: String {
        bundleString("NSHumanReadableCopyright") ?? "Copyright © 2026 FoxBai"
    }

    static var versionSummary: String {
        "\(version) (\(build))"
    }

    private static func bundleString(_ key: String) -> String? {
        Bundle.main.object(forInfoDictionaryKey: key) as? String
    }
}
