#pragma once

#include "AppSettings.h"
#include "ChatMessage.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pitlane::lite {

struct DanmakuHost {
    std::wstring host;
    int port = 2243;
    int websocket_port = 2244;
    int secure_websocket_port = 443;
};

struct BilibiliConnectionInfo {
    int requested_room_id = 0;
    int resolved_room_id = 0;
    std::wstring token;
    std::vector<DanmakuHost> hosts;
};

class BilibiliClient {
public:
    using MessageCallback = std::function<void(ChatMessage)>;
    using LogCallback = std::function<void(std::wstring)>;

    BilibiliConnectionInfo fetch_connection_info(const AppSettings& settings) const;
    void stream_messages(
        const AppSettings& settings,
        MessageCallback on_message,
        LogCallback on_log,
        const std::shared_ptr<std::atomic_bool>& running) const;
    static std::wstring build_cookie_header(const AppSettings& settings);

private:
    static int parse_room_id(const std::wstring& input);
    static std::wstring build_wbi_signed_query(
        const std::vector<std::pair<std::wstring, std::wstring>>& parameters,
        const AppSettings& settings);
    static std::wstring fetch_wbi_mixin_key(const AppSettings& settings);
    static std::wstring fetch_buvid3(const AppSettings& settings);
    static std::string http_get(const std::wstring& host, const std::wstring& path, const AppSettings& settings);
};

}  // 命名空间 pitlane::lite
