import Foundation

enum ChatMessageKind: String, Codable {
    case comment
    case superChat
}

struct ChatMessage: Hashable {
    var id: String
    var userName: String
    var text: String
    var kind: ChatMessageKind
    var receivedAt: Date
    var userId: Int64?
    var price: Decimal?

    var isSuperChat: Bool {
        kind == .superChat
    }

    static func comment(_ userName: String, _ text: String, userId: Int64? = nil, id: String? = nil) -> ChatMessage {
        ChatMessage(
            id: id ?? UUID().uuidString.replacingOccurrences(of: "-", with: ""),
            userName: userName,
            text: text,
            kind: .comment,
            receivedAt: Date(),
            userId: userId,
            price: nil
        )
    }

    static func superChat(_ userName: String, _ text: String, price: Decimal? = nil, userId: Int64? = nil, id: String? = nil) -> ChatMessage {
        ChatMessage(
            id: id ?? UUID().uuidString.replacingOccurrences(of: "-", with: ""),
            userName: userName,
            text: text,
            kind: .superChat,
            receivedAt: Date(),
            userId: userId,
            price: price
        )
    }
}

struct DanmakuHost: Hashable {
    var host: String
    var port: Int
    var webSocketPort: Int
    var secureWebSocketPort: Int
}

struct CarAsset: Codable, Hashable {
    var id: String
    var fileName: String
    var absolutePath: String
    var width: CGFloat
    var height: CGFloat
}

struct OverlayMessageDTO: Encodable {
    var id: String
    var userName: String
    var text: String
    var kind: String
    var price: Decimal?
    var receivedAt: String
}

enum OverlayLayout {
    static let baseWidth: CGFloat = 1280
    static let baseHeight: CGFloat = 900
    static let baseGap: CGFloat = 36

    static let frameWidth: CGFloat = 1280
    static let frameHeight: CGFloat = 900

    static let textLeft: CGFloat = 82
    static let textTop: CGFloat = 318
    static let textWidth: CGFloat = 650
    static let nameWidth: CGFloat = 620
    static let superChatNameWidth: CGFloat = 500
    static let nameFontSize: CGFloat = 70
    static let messageFontSize: CGFloat = 62
    static let messageLineHeight: CGFloat = 93
    static let messageTopMargin: CGFloat = 28
    static let messageMaxHeight: CGFloat = 280

    static let carLeft: CGFloat = 650
    static let carBottom: CGFloat = 48
}
