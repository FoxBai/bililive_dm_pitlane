import Foundation

struct AppSettings: Codable, Equatable {
    static let defaultUserAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"

    var roomInput = ""
    var cookie = ""
    var userAgent = AppSettings.defaultUserAgent
    var buvid3 = ""
    var obsPort = 17333
    var launchIntervalMs = 900
    var queueLimit = 80
    var minVisibleItems = 5
    var maxNicknameLength = 18
    var maxMessageLength = 42
    var maxRepeatCharacters = 4
    var maxStageWidth = 3840
    var onlySuperChat = false

    mutating func normalize() {
        userAgent = userAgent.trimmingCharacters(in: .whitespacesAndNewlines)
        if userAgent.isEmpty {
            userAgent = Self.defaultUserAgent
        }

        roomInput = roomInput.trimmingCharacters(in: .whitespacesAndNewlines)
        cookie = Self.normalizeCookieInput(cookie)
        buvid3 = buvid3.trimmingCharacters(in: .whitespacesAndNewlines)
        if buvid3.isEmpty {
            buvid3 = UUID().uuidString.uppercased() + "infoc"
        }

        obsPort = obsPort.clamped(to: 1024...65535)
        launchIntervalMs = launchIntervalMs.clamped(to: 120...10000)
        queueLimit = queueLimit.clamped(to: 5...500)
        minVisibleItems = minVisibleItems.clamped(to: 1...12)
        maxNicknameLength = maxNicknameLength.clamped(to: 4...64)
        maxMessageLength = maxMessageLength.clamped(to: 4...200)
        maxRepeatCharacters = maxRepeatCharacters.clamped(to: 2...20)
        maxStageWidth = maxStageWidth.clamped(to: 960...7680)
    }

    func normalized() -> AppSettings {
        var copy = self
        copy.normalize()
        return copy
    }

    private static func normalizeCookieInput(_ value: String) -> String {
        let text = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return "" }

        let lines = text
            .replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
            .split(separator: "\n")
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }

        if let cookieLine = lines.first(where: { $0.lowercased().hasPrefix("cookie:") }) {
            return cleanCookieValue(String(cookieLine.dropFirst("Cookie:".count)))
        }

        if let range = text.range(of: "Cookie:", options: .caseInsensitive) {
            var cookieValue = String(text[range.upperBound...])
            if let end = cookieValue.firstIndex(where: { $0 == "\r" || $0 == "\n" }) {
                cookieValue = String(cookieValue[..<end])
            }
            return cleanCookieValue(cookieValue)
        }

        return cleanCookieValue(text)
    }

    private static func cleanCookieValue(_ value: String) -> String {
        value
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .trimmingCharacters(in: CharacterSet(charactersIn: "\"'`\\"))
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .trimmingCharacters(in: CharacterSet(charactersIn: ";"))
    }
}

private extension Comparable {
    func clamped(to limits: ClosedRange<Self>) -> Self {
        min(max(self, limits.lowerBound), limits.upperBound)
    }
}
