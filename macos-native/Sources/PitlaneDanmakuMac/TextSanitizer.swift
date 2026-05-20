import Foundation

enum TextSanitizer {
    static func normalize(_ message: ChatMessage, settings: AppSettings) -> ChatMessage? {
        var normalized = message
        normalized.userName = cleanText(message.userName, maxLength: settings.maxNicknameLength, repeatLimit: settings.maxRepeatCharacters)
        normalized.text = cleanText(message.text, maxLength: settings.maxMessageLength, repeatLimit: settings.maxRepeatCharacters)

        if normalized.userName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            normalized.userName = "匿名观众"
        }

        if !normalized.isSuperChat && normalized.text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return nil
        }

        if normalized.text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            normalized.text = " "
        }

        return normalized
    }

    private static func cleanText(_ value: String, maxLength: Int, repeatLimit: Int) -> String {
        var output = ""
        var previous: Character?
        var repeatCount = 0

        for raw in value {
            let isControl = raw.unicodeScalars.allSatisfy { CharacterSet.controlCharacters.contains($0) }
            let ch: Character = raw.isWhitespace || isControl ? " " : raw
            if ch == previous {
                repeatCount += 1
                if repeatCount > repeatLimit {
                    continue
                }
            } else {
                previous = ch
                repeatCount = 1
            }

            if ch == " ", output.last == " " {
                continue
            }

            output.append(ch)
            if output.count >= maxLength {
                break
            }
        }

        return output.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
