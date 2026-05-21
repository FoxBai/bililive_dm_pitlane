@testable import PitlaneDanmakuMac
import XCTest

final class CoreBehaviorTests: XCTestCase {
    func testSettingsNormalizeExtractsCookieHeaderAndClampsValues() {
        var settings = AppSettings()
        settings.cookie = """
        Host: live.bilibili.com
        Cookie: foo=1; buvid3=abc; bar=2;
        Accept: */*
        """
        settings.obsPort = 80
        settings.launchIntervalMs = 20
        settings.queueLimit = 1
        settings.minVisibleItems = 99

        settings.normalize()

        XCTAssertEqual(settings.cookie, "foo=1; buvid3=abc; bar=2")
        XCTAssertEqual(settings.obsPort, 1024)
        XCTAssertEqual(settings.launchIntervalMs, 120)
        XCTAssertEqual(settings.queueLimit, 5)
        XCTAssertEqual(settings.minVisibleItems, 12)
    }

    func testTextSanitizerTrimsLengthAndRepeatedCharacters() {
        let settings = AppSettings(
            maxNicknameLength: 6,
            maxMessageLength: 12,
            maxRepeatCharacters: 2
        ).normalized()
        let message = ChatMessage.comment("  ", "哈哈哈哈    赛车出发啦啦啦")

        let normalized = TextSanitizer.normalize(message, settings: settings)

        XCTAssertEqual(normalized?.userName, "匿名观众")
        XCTAssertEqual(normalized?.text, "哈哈 赛车出发啦啦")
    }

    func testRealtimeDanmuMessageParsing() throws {
        let json = """
        {
          "cmd": "DANMU_MSG:4:0:2:2:2:0",
          "info": [
            [0, 1, 25, 16777215, 1710000000000, "message-id"],
            "进站换胎",
            [12345, "车迷小白"]
          ]
        }
        """
        let messages = BilibiliMessageParser.parseJsonMessages(Data(json.utf8))

        XCTAssertEqual(messages.count, 1)
        XCTAssertEqual(messages[0].kind, .comment)
        XCTAssertEqual(messages[0].userName, "车迷小白")
        XCTAssertEqual(messages[0].text, "进站换胎")
        XCTAssertEqual(messages[0].userId, 12345)
        XCTAssertTrue(messages[0].id.contains("message-id"))
    }

    func testSuperChatParsing() throws {
        let json = """
        {
          "cmd": "SUPER_CHAT_MESSAGE",
          "data": {
            "id": "sc-1",
            "uid": 9988,
            "price": 30,
            "message": "安全车出动",
            "user_info": { "uname": "RaceControl" }
          }
        }
        """
        let messages = BilibiliMessageParser.parseJsonMessages(Data(json.utf8))

        XCTAssertEqual(messages.count, 1)
        XCTAssertEqual(messages[0].kind, .superChat)
        XCTAssertEqual(messages[0].id, "sc:sc-1")
        XCTAssertEqual(messages[0].userName, "RaceControl")
        XCTAssertEqual(messages[0].text, "安全车出动")
        XCTAssertEqual(messages[0].userId, 9988)
        XCTAssertEqual(messages[0].price, Decimal(30))
    }

    func testHistoryMessagesParsing() {
        let root: [String: Any] = [
            "data": [
                "admin": [],
                "room": [
                    [
                        "text": "历史弹幕",
                        "nickname": "旧观众",
                        "uid": 42,
                        "timeline": "2026-05-21 10:00:00",
                        "check_info": ["ts": "1", "ct": "abc"]
                    ]
                ]
            ]
        ]

        let messages = BilibiliMessageParser.parseHistoryMessages(root)

        XCTAssertEqual(messages.count, 1)
        XCTAssertEqual(messages[0].kind, .comment)
        XCTAssertEqual(messages[0].userName, "旧观众")
        XCTAssertEqual(messages[0].text, "历史弹幕")
        XCTAssertEqual(messages[0].userId, 42)
        XCTAssertTrue(messages[0].id.hasPrefix("history:"))
    }
}
