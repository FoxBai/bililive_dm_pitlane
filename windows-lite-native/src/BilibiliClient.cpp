#include "BilibiliClient.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#ifdef PITLANE_LITE_HAS_COMPRESSION
#include <brotli/decode.h>
#include <zlib.h>
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <optional>
#include <map>
#include <unordered_map>

namespace pitlane::lite {

namespace {

constexpr int kWbiMixinKeyTable[] = {
    46, 47, 18, 2, 53, 8, 23, 32,
    15, 50, 10, 31, 58, 3, 45, 35,
    27, 43, 5, 49, 33, 9, 42, 19,
    29, 28, 14, 39, 12, 38, 41, 13,
    37, 48, 7, 16, 24, 55, 40, 61,
    26, 17, 0, 1, 60, 51, 30, 4,
    22, 25, 54, 21, 56, 59, 6, 63,
    57, 62, 11, 36, 20, 34, 44, 52,
};
constexpr int kZlibWindowBits = 15;

struct WinHttpHandle {
    HINTERNET value = nullptr;

    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : value(handle) {}

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }

    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            reset();
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }

    ~WinHttpHandle() {
        reset();
    }

    void reset() {
        if (value != nullptr) {
            WinHttpCloseHandle(value);
            value = nullptr;
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return value != nullptr;
    }
};

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring output(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length);
    return output;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }

    std::string output(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length, nullptr, nullptr);
    return output;
}

std::wstring trim(std::wstring value) {
    auto is_space = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };

    while (!value.empty() && is_space(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(value.back())) {
        value.pop_back();
    }

    return value;
}

bool cookie_contains(const std::wstring& cookie, const std::wstring& name) {
    std::wstring needle = name + L"=";
    auto lower = [](std::wstring text) {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(towlower(ch));
        });
        return text;
    };

    return lower(cookie).find(lower(needle)) != std::wstring::npos;
}

std::wstring endpoint_name(const DanmakuHost& host, bool secure) {
    return host.host + L":" + std::to_wstring(secure ? host.secure_websocket_port : host.websocket_port);
}

bool contains_masked_name(const std::wstring& name) {
    return name.find(L'*') != std::wstring::npos || name.find(L'＊') != std::wstring::npos;
}

bool looks_masked_name(std::wstring name) {
    name = trim(std::move(name));
    const auto first_star = name.find(L'*');
    const auto first_full_star = name.find(L'＊');
    const auto first_mask = std::min(
        first_star == std::wstring::npos ? name.size() : first_star,
        first_full_star == std::wstring::npos ? name.size() : first_full_star);
    if (first_mask == 0 || first_mask >= name.size()) {
        return false;
    }

    return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(first_mask), name.end(), [](wchar_t ch) {
        return ch == L'*' || ch == L'＊';
    });
}

std::string filter_wbi_value(std::wstring value) {
    value.erase(
        std::remove_if(value.begin(), value.end(), [](wchar_t ch) {
            return ch == L'!' || ch == L'\'' || ch == L'(' || ch == L')' || ch == L'*';
        }),
        value.end());
    return wide_to_utf8(value);
}

std::string url_encode(std::string value) {
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            output << static_cast<char>(ch);
        } else {
            output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return output.str();
}

std::string md5_hex(const std::string& value) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("初始化 MD5 失败。");
    }

    DWORD hash_length = 0;
    DWORD result_length = 0;
    if (BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_length),
            sizeof(hash_length),
            &result_length,
            0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("读取 MD5 长度失败。");
    }

    std::vector<unsigned char> hash(hash_length);
    const auto status = BCryptHash(
        algorithm,
        nullptr,
        0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
        static_cast<ULONG>(value.size()),
        hash.data(),
        static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        throw std::runtime_error("计算 MD5 失败。");
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned char byte : hash) {
        output << std::setw(2) << static_cast<int>(byte);
    }
    return output.str();
}

std::string build_query_string(const std::map<std::string, std::string>& parameters) {
    std::string query;
    for (const auto& [key, value] : parameters) {
        if (!query.empty()) {
            query += '&';
        }
        query += url_encode(key);
        query += '=';
        query += url_encode(value);
    }
    return query;
}

int parse_int_field(const std::string& json, const std::string& name, int fallback) {
    const std::regex pattern("\"" + name + R"("\s*:\s*(\d+))");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        int value = fallback;
        auto first = match[1].first;
        auto last = match[1].second;
        if (std::from_chars(&*first, &*last, value).ec == std::errc{}) {
            return value;
        }
    }

    return fallback;
}

std::wstring parse_string_field(const std::string& json, const std::string& name) {
    const std::regex pattern("\"" + name + R"json("\s*:\s*"([^"]*)")json");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return utf8_to_wide(match[1].str());
    }

    return {};
}

std::vector<DanmakuHost> parse_hosts(const std::string& json) {
    std::vector<DanmakuHost> hosts;
    const std::regex host_pattern(
        R"json(\{[^{}]*"host"\s*:\s*"([^"]+)"[^{}]*"port"\s*:\s*(\d+)[^{}]*"ws_port"\s*:\s*(\d+)[^{}]*"wss_port"\s*:\s*(\d+)[^{}]*\})json");

    for (std::sregex_iterator it(json.begin(), json.end(), host_pattern), end; it != end; ++it) {
        DanmakuHost host;
        host.host = utf8_to_wide((*it)[1].str());
        std::from_chars(&*(*it)[2].first, &*(*it)[2].second, host.port);
        std::from_chars(&*(*it)[3].first, &*(*it)[3].second, host.websocket_port);
        std::from_chars(&*(*it)[4].first, &*(*it)[4].second, host.secure_websocket_port);
        hosts.push_back(std::move(host));
    }

    return hosts;
}

void throw_if_bilibili_failed(const std::string& json, const wchar_t* label) {
    const int code = parse_int_field(json, "code", 0);
    if (code != 0) {
        throw std::runtime_error(wide_to_utf8(std::wstring(label) + L"失败，B站返回 code=" + std::to_wstring(code)));
    }
}

void append_be16(std::string& output, int value) {
    output.push_back(static_cast<char>((value >> 8) & 0xff));
    output.push_back(static_cast<char>(value & 0xff));
}

void append_be32(std::string& output, int value) {
    output.push_back(static_cast<char>((value >> 24) & 0xff));
    output.push_back(static_cast<char>((value >> 16) & 0xff));
    output.push_back(static_cast<char>((value >> 8) & 0xff));
    output.push_back(static_cast<char>(value & 0xff));
}

int read_be16(const char* data) {
    return (static_cast<unsigned char>(data[0]) << 8) |
        static_cast<unsigned char>(data[1]);
}

int read_be32(const char* data) {
    return (static_cast<unsigned char>(data[0]) << 24) |
        (static_cast<unsigned char>(data[1]) << 16) |
        (static_cast<unsigned char>(data[2]) << 8) |
        static_cast<unsigned char>(data[3]);
}

std::optional<long long> extract_uid(const std::wstring& cookie) {
    const std::wregex pattern(LR"((?:^|;\s*)DedeUserID=(\d+))", std::regex_constants::icase);
    std::wsmatch match;
    if (!std::regex_search(cookie, match, pattern)) {
        return std::nullopt;
    }

    long long uid = 0;
    for (wchar_t ch : match[1].str()) {
        uid = uid * 10 + static_cast<long long>(ch - L'0');
    }
    return uid;
}

std::string build_packet(int operation, int version, const std::string& payload) {
    std::string packet;
    packet.reserve(16 + payload.size());
    append_be32(packet, static_cast<int>(16 + payload.size()));
    append_be16(packet, 16);
    append_be16(packet, version);
    append_be32(packet, operation);
    append_be32(packet, 1);
    packet += payload;
    return packet;
}

std::string build_auth_packet(const AppSettings& settings, int room_id, const std::wstring& token) {
    const auto uid = extract_uid(settings.cookie).value_or(0);
#ifdef PITLANE_LITE_HAS_COMPRESSION
    constexpr int protover = 3;
#else
    constexpr int protover = 0;
#endif
    const auto payload =
        std::string("{\"uid\":") + std::to_string(uid) +
        ",\"roomid\":" + std::to_string(room_id) +
        ",\"protover\":" + std::to_string(protover) + ",\"platform\":\"danmuji\",\"type\":2,\"key\":\"" +
        wide_to_utf8(token) +
        "\",\"buvid\":\"" + wide_to_utf8(settings.buvid3) + "\"}";
    return build_packet(7, 1, payload);
}

std::string inflate_with_window_bits(const std::string& payload, int window_bits) {
#ifdef PITLANE_LITE_HAS_COMPRESSION
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(payload.data()));
    stream.avail_in = static_cast<uInt>(payload.size());

    if (inflateInit2(&stream, window_bits) != Z_OK) {
        throw std::runtime_error("初始化 zlib 解压失败。");
    }

    std::string output;
    std::vector<char> buffer(64 * 1024);
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        const auto written = buffer.size() - stream.avail_out;
        output.append(buffer.data(), written);
    }

    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        throw std::runtime_error("zlib 解压失败。");
    }

    return output;
#else
    (void)payload;
    (void)window_bits;
    throw std::runtime_error("当前构建未启用 zlib 解压依赖。");
#endif
}

std::string decompress_zlib(const std::string& payload) {
    try {
        return inflate_with_window_bits(payload, kZlibWindowBits);
    } catch (const std::exception&) {
        return inflate_with_window_bits(payload, -kZlibWindowBits);
    }
}

std::string decompress_brotli(const std::string& payload) {
#ifdef PITLANE_LITE_HAS_COMPRESSION
    BrotliDecoderState* state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (state == nullptr) {
        throw std::runtime_error("初始化 brotli 解压失败。");
    }

    const uint8_t* next_in = reinterpret_cast<const uint8_t*>(payload.data());
    std::size_t available_in = payload.size();
    std::string output;
    std::vector<uint8_t> buffer(64 * 1024);

    for (;;) {
        uint8_t* next_out = buffer.data();
        std::size_t available_out = buffer.size();
        const auto result = BrotliDecoderDecompressStream(
            state,
            &available_in,
            &next_in,
            &available_out,
            &next_out,
            nullptr);
        output.append(reinterpret_cast<const char*>(buffer.data()), buffer.size() - available_out);

        if (result == BROTLI_DECODER_RESULT_SUCCESS) {
            BrotliDecoderDestroyInstance(state);
            return output;
        }
        if (result == BROTLI_DECODER_RESULT_ERROR) {
            BrotliDecoderDestroyInstance(state);
            throw std::runtime_error("brotli 解压失败。");
        }
        if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && available_in == 0) {
            BrotliDecoderDestroyInstance(state);
            throw std::runtime_error("brotli 数据不完整。");
        }
    }
#else
    (void)payload;
    throw std::runtime_error("当前构建未启用 brotli 解压依赖。");
#endif
}

std::vector<ChatMessage> parse_json_messages(const std::string& json) {
    std::vector<ChatMessage> messages;

    const std::regex danmu_pattern(
        R"json("cmd"\s*:\s*"DANMU_MSG[^"]*"[\s\S]*?"info"\s*:\s*\[\s*\[[\s\S]*?\]\s*,\s*"([^"]*)"[\s\S]*?\[\s*(\d+)\s*,\s*"([^"]*)")json");
    for (std::sregex_iterator it(json.begin(), json.end(), danmu_pattern), end; it != end; ++it) {
        ChatMessage message;
        message.id = "live:" + std::to_string(std::hash<std::string>{}((*it).str()));
        message.text = utf8_to_wide((*it)[1].str());
        message.user_id = std::stoll((*it)[2].str());
        message.user_name = utf8_to_wide((*it)[3].str());
        message.kind = ChatMessageKind::Comment;
        messages.push_back(std::move(message));
    }

    const std::regex super_chat_pattern(
        R"json("cmd"\s*:\s*"SUPER_CHAT_MESSAGE[^"]*"[\s\S]*?"message"\s*:\s*"([^"]*)"[\s\S]*?"uid"\s*:\s*(\d+)[\s\S]*?"price"\s*:\s*(\d+(?:\.\d+)?)[\s\S]*?"uname"\s*:\s*"([^"]*)")json");
    for (std::sregex_iterator it(json.begin(), json.end(), super_chat_pattern), end; it != end; ++it) {
        ChatMessage message;
        message.id = "sc:" + std::to_string(std::hash<std::string>{}((*it).str()));
        message.text = utf8_to_wide((*it)[1].str());
        message.user_id = std::stoll((*it)[2].str());
        message.price = std::stod((*it)[3].str());
        message.user_name = utf8_to_wide((*it)[4].str());
        message.kind = ChatMessageKind::SuperChat;
        messages.push_back(std::move(message));
    }

    return messages;
}

void process_packet_payload(
    const std::string& payload,
    const BilibiliClient::MessageCallback& on_message,
    const BilibiliClient::LogCallback& on_log);

void process_packet(int version, int operation, const std::string& payload, const BilibiliClient::MessageCallback& on_message, const BilibiliClient::LogCallback& on_log) {
    if (operation == 8) {
        on_log(L"弹幕服务器认证成功，正在等待实时弹幕。");
        return;
    }

    if (operation != 5) {
        return;
    }

    try {
        switch (version) {
        case 0:
        case 1:
            for (auto& message : parse_json_messages(payload)) {
                on_message(std::move(message));
            }
            break;
        case 2:
            process_packet_payload(decompress_zlib(payload), on_message, on_log);
            break;
        case 3:
            process_packet_payload(decompress_brotli(payload), on_message, on_log);
            break;
        default:
            on_log(L"收到未知协议版本弹幕包：" + std::to_wstring(version));
            break;
        }
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        const int length = MultiByteToWideChar(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
        if (length > 0) {
            MultiByteToWideChar(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), wide.data(), length);
        }
        on_log(L"弹幕包解析失败：" + wide);
    }
}

void process_packet_payload(
    const std::string& payload,
    const BilibiliClient::MessageCallback& on_message,
    const BilibiliClient::LogCallback& on_log) {
    std::size_t offset = 0;
    bool processed_nested = false;
    while (offset + 16 <= payload.size()) {
        const int packet_length = read_be32(payload.data() + offset);
        const int header_length = read_be16(payload.data() + offset + 4);
        const int version = read_be16(payload.data() + offset + 6);
        const int operation = read_be32(payload.data() + offset + 8);
        if (packet_length < 16 || header_length < 16 || offset + static_cast<std::size_t>(packet_length) > payload.size()) {
            break;
        }

        process_packet(
            version,
            operation,
            payload.substr(offset + header_length, static_cast<std::size_t>(packet_length - header_length)),
            on_message,
            on_log);
        offset += static_cast<std::size_t>(packet_length);
        processed_nested = true;
    }

    if (!processed_nested) {
        for (auto& message : parse_json_messages(payload)) {
            on_message(std::move(message));
        }
    }
}

void sleep_until_retry(const std::shared_ptr<std::atomic_bool>& running) {
    for (int i = 0; i < 50 && running->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void stream_websocket_endpoint(
    const AppSettings& settings,
    const BilibiliConnectionInfo& info,
    const DanmakuHost& host,
    bool secure,
    const BilibiliClient::MessageCallback& on_message,
    const BilibiliClient::LogCallback& on_log,
    const std::shared_ptr<std::atomic_bool>& running,
    std::unordered_map<long long, std::wstring>& user_name_cache,
    bool& masked_name_warned,
    long long& received_count) {
    const auto port = secure ? host.secure_websocket_port : host.websocket_port;
    if (port <= 0) {
        throw std::runtime_error("弹幕服务器端口无效。");
    }

    WinHttpHandle session(WinHttpOpen(
        settings.user_agent.empty() ? L"PitlaneDanmakuLite/0.1" : settings.user_agent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        throw std::runtime_error("WinHTTP 初始化失败。");
    }
    WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000);

    WinHttpHandle connection(WinHttpConnect(session.value, host.host.c_str(), static_cast<INTERNET_PORT>(port), 0));
    if (!connection) {
        throw std::runtime_error("连接弹幕服务器失败。");
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.value,
        L"GET",
        L"/sub",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        throw std::runtime_error("创建 WebSocket 请求失败。");
    }

    std::wstring headers =
        L"Origin: https://live.bilibili.com\r\n"
        L"Referer: https://live.bilibili.com/" + std::to_wstring(info.resolved_room_id) + L"\r\n";
    const auto cookie = BilibiliClient::build_cookie_header(settings);
    if (!cookie.empty()) {
        headers += L"Cookie: " + cookie + L"\r\n";
    }

    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        throw std::runtime_error("设置 WebSocket 升级失败。");
    }

    if (!WinHttpSendRequest(
            request.value,
            headers.c_str(),
            static_cast<DWORD>(headers.size()),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        throw std::runtime_error("WebSocket 握手失败。");
    }

    WinHttpHandle socket(WinHttpWebSocketCompleteUpgrade(request.value, 0));
    request.value = nullptr;
    if (!socket) {
        throw std::runtime_error("WebSocket 升级失败。");
    }

    on_log(std::wstring(L"已连接 ") + (secure ? L"WSS" : L"WS") + L" 弹幕服务器 " + endpoint_name(host, secure));

    const auto auth_packet = build_auth_packet(settings, info.resolved_room_id, info.token);
    if (WinHttpWebSocketSend(
            socket.value,
            WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
            const_cast<char*>(auth_packet.data()),
            static_cast<DWORD>(auth_packet.size())) != NO_ERROR) {
        throw std::runtime_error("发送弹幕认证包失败。");
    }

    std::atomic_bool socket_open{true};
    std::thread heartbeat([&]() {
        while (running->load() && socket_open.load()) {
            for (int i = 0; i < 30 && running->load() && socket_open.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!running->load() || !socket_open.load()) {
                break;
            }
            const auto heartbeat_packet = build_packet(2, 1, {});
            WinHttpWebSocketSend(
                socket.value,
                WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                const_cast<char*>(heartbeat_packet.data()),
                static_cast<DWORD>(heartbeat_packet.size()));
        }
    });

    auto cached_on_message = [&](ChatMessage message) {
        if (message.user_id.has_value() && *message.user_id > 0) {
            if (looks_masked_name(message.user_name)) {
                const auto cached = user_name_cache.find(*message.user_id);
                if (cached != user_name_cache.end()) {
                    message.user_name = cached->second;
                } else {
                    if (!masked_name_warned) {
                        masked_name_warned = true;
                        on_log(L"收到 B站返回的脱敏昵称，已跳过无法恢复的弹幕；如仍频繁出现，请填写登录后的网页 Cookie。");
                    }
                    return;
                }
            } else if (!message.user_name.empty() && !contains_masked_name(message.user_name)) {
                user_name_cache[*message.user_id] = message.user_name;
            }
        }

        ++received_count;
        if (received_count <= 5 || received_count % 20 == 0) {
            on_log(L"已接收 B站实时弹幕 " + std::to_wstring(received_count) + L" 条。");
        }
        on_message(std::move(message));
    };

    std::string message_buffer;
    std::vector<char> buffer(64 * 1024);
    try {
        while (running->load()) {
            DWORD bytes_read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type{};
            const DWORD error = WinHttpWebSocketReceive(
                socket.value,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytes_read,
                &buffer_type);
            if (error != NO_ERROR) {
                if (error == ERROR_WINHTTP_TIMEOUT) {
                    continue;
                }
                throw std::runtime_error("读取 WebSocket 数据失败：" + std::to_string(error));
            }

            if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                on_log(L"弹幕服务器已关闭连接。");
                break;
            }

            message_buffer.append(buffer.data(), buffer.data() + bytes_read);
            if (buffer_type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                process_packet_payload(message_buffer, cached_on_message, on_log);
                message_buffer.clear();
            }
        }
    } catch (...) {
        socket_open.store(false);
        WinHttpWebSocketClose(socket.value, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        if (heartbeat.joinable()) {
            heartbeat.join();
        }
        throw;
    }

    socket_open.store(false);
    WinHttpWebSocketClose(socket.value, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    if (heartbeat.joinable()) {
        heartbeat.join();
    }
}

}  // 匿名命名空间

BilibiliConnectionInfo BilibiliClient::fetch_connection_info(const AppSettings& settings) const {
    const int requested_room_id = parse_room_id(settings.room_input);
    const auto room_init = http_get(
        L"api.live.bilibili.com",
        L"/room/v1/Room/room_init?id=" + std::to_wstring(requested_room_id),
        settings);
    throw_if_bilibili_failed(room_init, L"房间初始化");

    BilibiliConnectionInfo info;
    info.requested_room_id = requested_room_id;
    info.resolved_room_id = parse_int_field(room_init, "room_id", requested_room_id);

    const auto danmu_query = build_wbi_signed_query(
        {
            {L"id", std::to_wstring(info.resolved_room_id)},
            {L"type", L"0"},
            {L"web_location", L"444.8"},
        },
        settings);
    const auto danmu_info = http_get(
        L"api.live.bilibili.com",
        L"/xlive/web-room/v1/index/getDanmuInfo?" + danmu_query,
        settings);
    throw_if_bilibili_failed(danmu_info, L"弹幕服务器信息");

    info.token = parse_string_field(danmu_info, "token");
    info.hosts = parse_hosts(danmu_info);
    if (info.hosts.empty()) {
        info.hosts.push_back(DanmakuHost{.host = L"broadcastlv.chat.bilibili.com"});
    }

    return info;
}

void BilibiliClient::stream_messages(
    const AppSettings& settings,
    MessageCallback on_message,
    LogCallback on_log,
    const std::shared_ptr<std::atomic_bool>& running) const {
    auto effective_settings = settings;
    effective_settings.normalize();
    if (!cookie_contains(effective_settings.cookie, L"buvid3") && trim(effective_settings.buvid3).empty()) {
        try {
            effective_settings.buvid3 = fetch_buvid3(effective_settings);
            if (!effective_settings.buvid3.empty()) {
                on_log(L"已自动获取 buvid3，用于提升 B站弹幕握手稳定性。");
            }
        } catch (const std::exception&) {
            on_log(L"自动获取 buvid3 失败，将继续尝试使用当前 Cookie 连接。");
        }
    }

    if (extract_uid(effective_settings.cookie).has_value()) {
        on_log(L"已检测到登录 Cookie，将使用登录 uid 进行弹幕握手。");
    } else {
        on_log(L"未检测到登录 Cookie uid；如一段时间后昵称被 B站脱敏，请粘贴登录后的网页 Cookie。");
    }

    auto info = fetch_connection_info(effective_settings);
    if (info.hosts.empty()) {
        throw std::runtime_error("没有可用的弹幕服务器。");
    }

    std::unordered_map<long long, std::wstring> user_name_cache;
    bool masked_name_warned = false;
    long long received_count = 0;
    on_log(L"B站真实房间号：" + std::to_wstring(info.resolved_room_id) + L"，可用弹幕服务器：" + std::to_wstring(info.hosts.size()) + L" 个。");

    while (running->load()) {
        for (const auto& host : info.hosts) {
            if (!running->load()) {
                break;
            }

            bool connected = false;
            for (const bool secure : {true, false}) {
                if (!running->load()) {
                    break;
                }
                if ((secure && host.secure_websocket_port <= 0) || (!secure && host.websocket_port <= 0)) {
                    continue;
                }

                try {
                    stream_websocket_endpoint(
                        effective_settings,
                        info,
                        host,
                        secure,
                        on_message,
                        on_log,
                        running,
                        user_name_cache,
                        masked_name_warned,
                        received_count);
                    connected = true;
                    break;
                } catch (const std::exception& ex) {
                    const std::string error_message = ex.what();
                    const int length = MultiByteToWideChar(CP_UTF8, 0, error_message.data(), static_cast<int>(error_message.size()), nullptr, 0);
                    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
                    if (length > 0) {
                        MultiByteToWideChar(CP_UTF8, 0, error_message.data(), static_cast<int>(error_message.size()), wide.data(), length);
                    }
                    on_log(std::wstring(secure ? L"WSS" : L"WS") + L" 弹幕服务器 " + endpoint_name(host, secure) + L" 断开：" + wide);
                }
            }
            if (connected && running->load()) {
                on_log(L"当前弹幕服务器连接结束，正在尝试下一个节点。");
            }
        }

        if (running->load()) {
            on_log(L"全部弹幕服务器都已断开，5 秒后重连。");
            sleep_until_retry(running);
        }
    }
}

int BilibiliClient::parse_room_id(const std::wstring& input) {
    const auto text = trim(input);
    const std::wregex pattern(LR"((\d+))");
    std::wsmatch match;
    if (!std::regex_search(text, match, pattern)) {
        throw std::invalid_argument("请输入 B站直播间 ID 或直播间 URL。");
    }

    int room_id = 0;
    const auto digits = match[1].str();
    for (wchar_t ch : digits) {
        if (ch < L'0' || ch > L'9') {
            continue;
        }
        room_id = room_id * 10 + static_cast<int>(ch - L'0');
    }

    if (room_id <= 0) {
        throw std::invalid_argument("直播间 ID 无效。");
    }

    return room_id;
}

std::wstring BilibiliClient::build_cookie_header(const AppSettings& settings) {
    auto cookie = trim(settings.cookie);
    while (!cookie.empty() && cookie.back() == L';') {
        cookie.pop_back();
    }

    if (cookie_contains(cookie, L"buvid3") || trim(settings.buvid3).empty()) {
        return cookie;
    }

    const auto buvid_cookie = L"buvid3=" + trim(settings.buvid3);
    return cookie.empty() ? buvid_cookie : cookie + L"; " + buvid_cookie;
}

std::wstring BilibiliClient::build_wbi_signed_query(
    const std::vector<std::pair<std::wstring, std::wstring>>& parameters,
    const AppSettings& settings) {
    std::map<std::string, std::string> signed_parameters;
    for (const auto& [key, value] : parameters) {
        signed_parameters[wide_to_utf8(key)] = filter_wbi_value(value);
    }

    signed_parameters["wts"] = std::to_string(std::time(nullptr));
    const auto mixin_key = wide_to_utf8(fetch_wbi_mixin_key(settings));
    const auto unsigned_query = build_query_string(signed_parameters);
    signed_parameters["w_rid"] = md5_hex(unsigned_query + mixin_key);
    return utf8_to_wide(build_query_string(signed_parameters));
}

std::wstring BilibiliClient::fetch_wbi_mixin_key(const AppSettings& settings) {
    const auto body = http_get(
        L"api.bilibili.com",
        L"/x/web-interface/nav",
        settings);
    throw_if_bilibili_failed(body, L"WBI key");

    const auto img_url = parse_string_field(body, "img_url");
    const auto sub_url = parse_string_field(body, "sub_url");
    auto extract_key = [](const std::wstring& url) {
        const std::wregex pattern(LR"(/([^/?#]+)\.[A-Za-z0-9]+(?:[?#].*)?$)");
        std::wsmatch match;
        if (!std::regex_search(url, match, pattern)) {
            throw std::runtime_error("B站 WBI key URL 无法解析。");
        }
        return match[1].str();
    };

    const auto raw_key = extract_key(img_url) + extract_key(sub_url);
    if (raw_key.size() < std::size(kWbiMixinKeyTable)) {
        throw std::runtime_error("B站 WBI key 长度异常。");
    }

    std::wstring mixin_key;
    mixin_key.reserve(32);
    for (int i = 0; i < 32; ++i) {
        mixin_key.push_back(raw_key[static_cast<std::size_t>(kWbiMixinKeyTable[i])]);
    }
    return mixin_key;
}

std::wstring BilibiliClient::fetch_buvid3(const AppSettings& settings) {
    const auto body = http_get(
        L"api.bilibili.com",
        L"/x/frontend/finger/spi",
        settings);
    throw_if_bilibili_failed(body, L"buvid3 获取");

    return parse_string_field(body, "b_3");
}

std::string BilibiliClient::http_get(const std::wstring& host, const std::wstring& path, const AppSettings& settings) {
    WinHttpHandle session(WinHttpOpen(
        settings.user_agent.empty() ? L"PitlaneDanmakuLite/0.1" : settings.user_agent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        throw std::runtime_error("WinHTTP 初始化失败。");
    }

    WinHttpHandle connection(WinHttpConnect(session.value, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        throw std::runtime_error("连接 B站 API 主机失败。");
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.value,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request) {
        throw std::runtime_error("创建 HTTP 请求失败。");
    }

    std::wstring headers =
        L"Accept: application/json\r\n"
        L"Origin: https://live.bilibili.com\r\n"
        L"Referer: https://live.bilibili.com/\r\n";
    const auto cookie = build_cookie_header(settings);
    if (!cookie.empty()) {
        headers += L"Cookie: " + cookie + L"\r\n";
    }

    if (!WinHttpSendRequest(
            request.value,
            headers.c_str(),
            static_cast<DWORD>(headers.size()),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        throw std::runtime_error("发送 HTTP 请求失败。");
    }

    if (!WinHttpReceiveResponse(request.value, nullptr)) {
        throw std::runtime_error("读取 HTTP 响应失败。");
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(
        request.value,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_size,
        WINHTTP_NO_HEADER_INDEX);
    if (status_code < 200 || status_code >= 300) {
        throw std::runtime_error("B站 API HTTP 状态异常：" + std::to_string(status_code));
    }

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            throw std::runtime_error("查询 HTTP 响应数据失败。");
        }

        if (available == 0) {
            break;
        }

        const auto old_size = body.size();
        body.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.value, body.data() + old_size, available, &read)) {
            throw std::runtime_error("读取 HTTP 响应数据失败。");
        }
        body.resize(old_size + read);
    }

    return body;
}

}  // 命名空间 pitlane::lite
