import CryptoKit
import Foundation

enum BilibiliMessageParser {
    static func parseJsonMessages(_ payload: Data) -> [ChatMessage] {
        splitJSONObjects(payload).compactMap { data in
            guard let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                return nil
            }
            return parseRoot(root, rawPayload: data)
        }
    }

    static func parseHistoryMessages(_ root: [String: Any]) -> [ChatMessage] {
        guard let data = root["data"] as? [String: Any] else {
            return []
        }

        var messages: [ChatMessage] = []
        parseHistoryArray(data["admin"], into: &messages)
        parseHistoryArray(data["room"], into: &messages)
        return messages
    }

    private static func parseHistoryArray(_ value: Any?, into messages: inout [ChatMessage]) {
        guard let items = value as? [[String: Any]] else { return }
        for item in items {
            if let message = parseHistoryItem(item) {
                messages.append(message)
            }
        }
    }

    private static func parseHistoryItem(_ item: [String: Any]) -> ChatMessage? {
        let text = asString(item["text"])
        guard !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return nil
        }

        let userName = asString(item["nickname"])
        let uid = asInt64(item["uid"])
        let timeline = asString(item["timeline"])
        var checkText = ""
        if let checkInfo = item["check_info"] as? [String: Any] {
            checkText = "\(asString(checkInfo["ts"])):\(asString(checkInfo["ct"]))"
        }

        let rawId = "\(timeline):\(uid.map(String.init) ?? ""):\(userName):\(text):\(checkText)"
        let id = "history:" + stableHash(Data(rawId.utf8))
        return .comment(userName, text, userId: uid, id: id)
    }

    private static func parseRoot(_ root: [String: Any], rawPayload: Data) -> ChatMessage? {
        let command = asString(root["cmd"])
            .split(separator: ":", maxSplits: 1)
            .first
            .map(String.init) ?? ""

        switch command {
        case "DANMU_MSG":
            return parseDanmuMessage(root, rawPayload: rawPayload)
        case "SUPER_CHAT_MESSAGE":
            return parseSuperChat(root, rawPayload: rawPayload)
        default:
            return nil
        }
    }

    private static func parseDanmuMessage(_ root: [String: Any], rawPayload: Data) -> ChatMessage? {
        guard let info = root["info"] as? [Any] else {
            return nil
        }

        let text = arrayString(info, index: 1)
        guard !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return nil
        }

        let userInfo = arrayValue(info, index: 2) as? [Any]
        let uid = arrayInt64(userInfo, index: 0)
        let userName = arrayString(userInfo, index: 1)
        let meta = arrayValue(info, index: 0) as? [Any]
        let timestamp = arrayString(meta, index: 4)
        let messageId = arrayString(meta, index: 5)
        let id = "\(timestamp):\(messageId):\(stableHash(rawPayload))"
        return .comment(userName, text, userId: uid, id: id)
    }

    private static func parseSuperChat(_ root: [String: Any], rawPayload: Data) -> ChatMessage? {
        guard let data = root["data"] as? [String: Any] else {
            return nil
        }

        let text = asString(data["message"])
        guard !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            return nil
        }

        var id = asString(data["id"])
        if id.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            id = stableHash(rawPayload)
        }

        let uid = asInt64(data["uid"])
        let price = asDecimal(data["price"])
        let userInfo = data["user_info"] as? [String: Any]
        let userName = asString(userInfo?["uname"])

        return .superChat(userName, text, price: price, userId: uid, id: "sc:" + id)
    }

    private static func splitJSONObjects(_ payload: Data) -> [Data] {
        guard let text = String(data: payload, encoding: .utf8) else {
            return []
        }

        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if let data = trimmed.data(using: .utf8),
           (try? JSONSerialization.jsonObject(with: data)) != nil {
            return [data]
        }

        var chunks: [Data] = []
        var depth = 0
        var start: String.Index?
        var isInString = false
        var isEscaped = false

        for index in text.indices {
            let ch = text[index]
            if isInString {
                if isEscaped {
                    isEscaped = false
                } else if ch == "\\" {
                    isEscaped = true
                } else if ch == "\"" {
                    isInString = false
                }
                continue
            }

            if ch == "\"" {
                isInString = true
                continue
            }

            if ch == "{" {
                if depth == 0 {
                    start = index
                }
                depth += 1
            } else if ch == "}" {
                depth -= 1
                if depth == 0, let startIndex = start {
                    let next = text.index(after: index)
                    if let data = String(text[startIndex..<next]).data(using: .utf8) {
                        chunks.append(data)
                    }
                    start = nil
                }
            }
        }

        return chunks
    }

    private static func arrayValue(_ array: [Any]?, index: Int) -> Any? {
        guard let array, array.indices.contains(index) else {
            return nil
        }
        return array[index]
    }

    private static func arrayString(_ array: [Any]?, index: Int) -> String {
        asString(arrayValue(array, index: index))
    }

    private static func arrayInt64(_ array: [Any]?, index: Int) -> Int64? {
        asInt64(arrayValue(array, index: index))
    }

    private static func asString(_ value: Any?) -> String {
        switch value {
        case let value as String:
            return value
        case let value as NSNumber:
            return value.stringValue
        case let value as Int:
            return String(value)
        case let value as Int64:
            return String(value)
        case let value as Double:
            return String(value)
        default:
            return ""
        }
    }

    private static func asInt64(_ value: Any?) -> Int64? {
        switch value {
        case let value as NSNumber:
            return value.int64Value
        case let value as Int64:
            return value
        case let value as Int:
            return Int64(value)
        case let value as String:
            return Int64(value)
        default:
            return nil
        }
    }

    private static func asDecimal(_ value: Any?) -> Decimal? {
        switch value {
        case let value as Decimal:
            return value
        case let value as NSNumber:
            return value.decimalValue
        case let value as String:
            return Decimal(string: value)
        default:
            return nil
        }
    }

    private static func stableHash(_ data: Data) -> String {
        let digest = SHA256.hash(data: data)
        return digest.prefix(8).map { String(format: "%02X", $0) }.joined()
    }
}
