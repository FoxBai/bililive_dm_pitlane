#include <cstdlib>
#include <iostream>
#include <string>

#include "../src/BilibiliClient.cpp"

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace pitlane::lite;

    const std::string live_json = R"json({
        "cmd":"DANMU_MSG:4:0:2:2:2:0",
        "info":[[0,1,25,1700000000,123456,"abc-mid"],"Hello \u0057orld",[42,"NormalUser"],[]]
    })json";
    const auto live_messages = parse_json_messages(live_json);
    require_true(live_messages.size() == 1, "DANMU_MSG should parse exactly one message.");
    require_true(live_messages[0].id == "live:abc-mid", "DANMU_MSG should prefer the server message id.");
    require_true(live_messages[0].user_id == 42, "DANMU_MSG should parse uid.");
    require_true(wide_to_utf8(live_messages[0].user_name) == "NormalUser", "DANMU_MSG should parse user name.");
    require_true(wide_to_utf8(live_messages[0].text) == "Hello World", "DANMU_MSG should unescape text.");

    const std::string super_chat_json = R"json({
        "cmd":"SUPER_CHAT_MESSAGE",
        "data":{"id":98765,"uid":77,"price":30,"message":"Pinned hello","user_info":{"uname":"ScUser"}}
    })json";
    const auto super_chat_messages = parse_json_messages(super_chat_json);
    require_true(super_chat_messages.size() == 1, "SUPER_CHAT_MESSAGE should parse exactly one message.");
    require_true(super_chat_messages[0].id == "sc:98765", "SUPER_CHAT_MESSAGE should prefer data.id.");
    require_true(super_chat_messages[0].is_super_chat(), "SUPER_CHAT_MESSAGE should be marked as SuperChat.");
    require_true(super_chat_messages[0].price.has_value() && *super_chat_messages[0].price == 30.0, "SUPER_CHAT_MESSAGE should parse price.");
    require_true(wide_to_utf8(super_chat_messages[0].user_name) == "ScUser", "SUPER_CHAT_MESSAGE should parse user_info.uname.");

    const std::string history_json = R"json({
        "code":0,
        "data":{
            "admin":[],
            "room":[
                {"uid":42,"nickname":"NormalUser","text":"History hello","timeline":"2026-05-18 12:00:00","check_info":{"ts":177,"ct":"history-key"}}
            ]
        }
    })json";
    const auto history_messages = parse_history_messages(history_json);
    require_true(history_messages.size() == 1, "History response should parse room messages.");
    require_true(history_messages[0].id.rfind("history:", 0) == 0, "History message should have a stable history id prefix.");
    require_true(history_messages[0].user_id == 42, "History message should parse uid.");
    require_true(wide_to_utf8(history_messages[0].user_name) == "NormalUser", "History message should parse nickname.");
    require_true(wide_to_utf8(history_messages[0].text) == "History hello", "History message should parse text.");

    std::cout << "BilibiliClient parsing smoke test passed." << '\n';
    return 0;
}
