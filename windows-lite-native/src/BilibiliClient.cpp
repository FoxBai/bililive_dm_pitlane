#include "BilibiliClient.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
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
#include <cstdint>
#include <cwctype>
#include <ctime>
#include <deque>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <optional>
#include <map>
#include <unordered_map>
#include <unordered_set>

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

struct WinsockSession {
    WinsockSession() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockSession() {
        if (ok) {
            WSACleanup();
        }
    }

    bool ok = false;
};

struct SocketHandle {
    SOCKET value = INVALID_SOCKET;

    SocketHandle() = default;
    explicit SocketHandle(SOCKET socket) : value(socket) {}

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : value(other.value) {
        other.value = INVALID_SOCKET;
    }

    SocketHandle& operator=(SocketHandle&& other) noexcept {
        if (this != &other) {
            reset();
            value = other.value;
            other.value = INVALID_SOCKET;
        }
        return *this;
    }

    ~SocketHandle() {
        reset();
    }

    void reset() {
        if (value != INVALID_SOCKET) {
            closesocket(value);
            value = INVALID_SOCKET;
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return value != INVALID_SOCKET;
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

std::wstring tcp_endpoint_name(const DanmakuHost& host) {
    return host.host + L":" + std::to_wstring(host.port);
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

bool is_json_ws(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

void skip_json_ws(std::string_view text, std::size_t& offset) {
    while (offset < text.size() && is_json_ws(text[offset])) {
        ++offset;
    }
}

std::string_view trim_json(std::string_view text) {
    while (!text.empty() && is_json_ws(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_json_ws(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<int> hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> read_json_hex4(std::string_view value, std::size_t offset) {
    if (offset + 4 > value.size()) {
        return std::nullopt;
    }

    std::uint32_t codepoint = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const auto digit = hex_digit(value[offset + index]);
        if (!digit.has_value()) {
            return std::nullopt;
        }
        codepoint = (codepoint << 4) | static_cast<std::uint32_t>(*digit);
    }
    return codepoint;
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::optional<std::string> decode_json_string(std::string_view value) {
    value = trim_json(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return std::nullopt;
    }

    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        const char ch = value[index];
        if (ch != '\\') {
            output.push_back(ch);
            continue;
        }

        if (++index + 1 > value.size()) {
            return std::nullopt;
        }

        switch (value[index]) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
            const auto first = read_json_hex4(value, index + 1);
            if (!first.has_value()) {
                return std::nullopt;
            }
            index += 4;

            std::uint32_t codepoint = *first;
            if (codepoint >= 0xd800 && codepoint <= 0xdbff &&
                index + 6 < value.size() &&
                value[index + 1] == '\\' &&
                value[index + 2] == 'u') {
                const auto second = read_json_hex4(value, index + 3);
                if (second.has_value() && *second >= 0xdc00 && *second <= 0xdfff) {
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (*second - 0xdc00);
                    index += 6;
                }
            }
            append_utf8(output, codepoint);
            break;
        }
        default:
            output.push_back(value[index]);
            break;
        }
    }
    return output;
}

std::optional<std::string_view> json_value_span(std::string_view text, std::size_t offset) {
    skip_json_ws(text, offset);
    if (offset >= text.size()) {
        return std::nullopt;
    }

    const char first = text[offset];
    if (first == '"') {
        bool escaped = false;
        for (std::size_t index = offset + 1; index < text.size(); ++index) {
            const char ch = text[index];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                return text.substr(offset, index - offset + 1);
            }
        }
        return std::nullopt;
    }

    if (first == '{' || first == '[') {
        std::vector<char> closers;
        closers.push_back(first == '{' ? '}' : ']');
        bool in_string = false;
        bool escaped = false;
        for (std::size_t index = offset + 1; index < text.size(); ++index) {
            const char ch = text[index];
            if (in_string) {
                if (escaped) {
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == '"') {
                    in_string = false;
                }
                continue;
            }

            if (ch == '"') {
                in_string = true;
            } else if (ch == '{') {
                closers.push_back('}');
            } else if (ch == '[') {
                closers.push_back(']');
            } else if (!closers.empty() && ch == closers.back()) {
                closers.pop_back();
                if (closers.empty()) {
                    return text.substr(offset, index - offset + 1);
                }
            }
        }
        return std::nullopt;
    }

    std::size_t end = offset;
    while (end < text.size() &&
           text[end] != ',' &&
           text[end] != '}' &&
           text[end] != ']' &&
           !is_json_ws(text[end])) {
        ++end;
    }
    return text.substr(offset, end - offset);
}

std::vector<std::string_view> root_json_objects(std::string_view json) {
    std::vector<std::string_view> objects;
    std::size_t offset = 0;
    while (offset < json.size()) {
        const auto begin = json.find('{', offset);
        if (begin == std::string_view::npos) {
            break;
        }

        const auto object = json_value_span(json, begin);
        if (!object.has_value() || trim_json(*object).empty() || trim_json(*object).front() != '{') {
            offset = begin + 1;
            continue;
        }

        objects.push_back(*object);
        offset = static_cast<std::size_t>(object->data() - json.data()) + object->size();
    }
    return objects;
}

std::optional<std::string_view> find_json_property(std::string_view object, std::string_view name) {
    object = trim_json(object);
    if (object.size() < 2 || object.front() != '{') {
        return std::nullopt;
    }

    std::size_t offset = 1;
    while (offset < object.size()) {
        skip_json_ws(object, offset);
        if (offset >= object.size() || object[offset] == '}') {
            break;
        }

        const auto key_span = json_value_span(object, offset);
        if (!key_span.has_value()) {
            return std::nullopt;
        }
        const auto key = decode_json_string(*key_span);
        offset = static_cast<std::size_t>(key_span->data() - object.data()) + key_span->size();

        skip_json_ws(object, offset);
        if (offset >= object.size() || object[offset] != ':') {
            return std::nullopt;
        }
        ++offset;

        const auto value_span = json_value_span(object, offset);
        if (!value_span.has_value()) {
            return std::nullopt;
        }
        if (key.has_value() && *key == name) {
            return value_span;
        }

        offset = static_cast<std::size_t>(value_span->data() - object.data()) + value_span->size();
        skip_json_ws(object, offset);
        if (offset < object.size() && object[offset] == ',') {
            ++offset;
        }
    }
    return std::nullopt;
}

std::vector<std::string_view> json_array_values(std::string_view array) {
    std::vector<std::string_view> values;
    array = trim_json(array);
    if (array.size() < 2 || array.front() != '[') {
        return values;
    }

    std::size_t offset = 1;
    while (offset < array.size()) {
        skip_json_ws(array, offset);
        if (offset >= array.size() || array[offset] == ']') {
            break;
        }

        const auto value_span = json_value_span(array, offset);
        if (!value_span.has_value()) {
            break;
        }
        values.push_back(*value_span);
        offset = static_cast<std::size_t>(value_span->data() - array.data()) + value_span->size();
        skip_json_ws(array, offset);
        if (offset < array.size() && array[offset] == ',') {
            ++offset;
        }
    }
    return values;
}

std::string json_value_to_string(std::string_view value) {
    value = trim_json(value);
    if (value.empty()) {
        return {};
    }
    if (value.front() == '"') {
        return decode_json_string(value).value_or(std::string{});
    }
    return std::string(value);
}

std::string json_string_property(std::string_view object, std::string_view name) {
    const auto value = find_json_property(object, name);
    if (!value.has_value()) {
        return {};
    }
    return json_value_to_string(*value);
}

std::optional<long long> json_int64_value(std::string_view value) {
    const auto text = json_value_to_string(value);
    if (text.empty()) {
        return std::nullopt;
    }

    long long number = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
        return number;
    }
    return std::nullopt;
}

std::optional<long long> json_int64_property(std::string_view object, std::string_view name) {
    const auto value = find_json_property(object, name);
    return value.has_value() ? json_int64_value(*value) : std::nullopt;
}

std::optional<double> json_double_value(std::string_view value) {
    const auto text = json_value_to_string(value);
    if (text.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t parsed = 0;
        const double number = std::stod(text, &parsed);
        if (parsed == text.size()) {
            return number;
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<double> json_double_property(std::string_view object, std::string_view name) {
    const auto value = find_json_property(object, name);
    return value.has_value() ? json_double_value(*value) : std::nullopt;
}

std::string stable_hash(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : value) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

std::string stable_message_key(std::string_view preferred, std::string_view fallback) {
    const auto key = json_value_to_string(preferred);
    if (!key.empty() && key != "0" && key != "null") {
        return key;
    }
    return stable_hash(fallback);
}

std::optional<ChatMessage> parse_danmu_message(std::string_view object) {
    const auto info = find_json_property(object, "info");
    if (!info.has_value()) {
        return std::nullopt;
    }

    const auto info_values = json_array_values(*info);
    if (info_values.size() < 3) {
        return std::nullopt;
    }

    const auto user_values = json_array_values(info_values[2]);
    if (user_values.size() < 2) {
        return std::nullopt;
    }

    ChatMessage message;
    message.kind = ChatMessageKind::Comment;
    message.text = utf8_to_wide(json_value_to_string(info_values[1]));
    message.user_id = json_int64_value(user_values[0]);
    message.user_name = utf8_to_wide(json_value_to_string(user_values[1]));

    std::string message_key;
    const auto meta_values = json_array_values(info_values[0]);
    if (meta_values.size() > 5) {
        message_key = stable_message_key(meta_values[5], object);
    } else if (meta_values.size() > 4) {
        message_key = stable_message_key(meta_values[4], object);
    } else {
        message_key = stable_hash(object);
    }
    message.id = "live:" + message_key;

    if (message.text.empty()) {
        return std::nullopt;
    }
    return message;
}

std::optional<ChatMessage> parse_super_chat_message(std::string_view object) {
    const auto data = find_json_property(object, "data");
    const auto payload = data.has_value() ? *data : object;
    const auto user_info = find_json_property(payload, "user_info");

    ChatMessage message;
    message.kind = ChatMessageKind::SuperChat;
    message.text = utf8_to_wide(json_string_property(payload, "message"));
    message.user_id = json_int64_property(payload, "uid");
    message.price = json_double_property(payload, "price");
    if (user_info.has_value()) {
        message.user_name = utf8_to_wide(json_string_property(*user_info, "uname"));
    }
    if (message.user_name.empty()) {
        message.user_name = utf8_to_wide(json_string_property(payload, "uname"));
    }

    const auto id_value = find_json_property(payload, "id");
    message.id = "sc:" + (id_value.has_value() ? stable_message_key(*id_value, object) : stable_hash(object));
    if (message.text.empty()) {
        return std::nullopt;
    }
    return message;
}

std::optional<ChatMessage> parse_history_message(std::string_view object) {
    const auto text = json_string_property(object, "text");
    if (text.empty()) {
        return std::nullopt;
    }

    ChatMessage message;
    message.kind = ChatMessageKind::Comment;
    message.text = utf8_to_wide(text);
    message.user_name = utf8_to_wide(json_string_property(object, "nickname"));
    if (message.user_name.empty()) {
        message.user_name = utf8_to_wide(json_string_property(object, "uname"));
    }
    message.user_id = json_int64_property(object, "uid");

    std::string id_source;
    if (const auto check_info = find_json_property(object, "check_info"); check_info.has_value()) {
        id_source = json_string_property(*check_info, "ct");
        if (id_source.empty()) {
            if (const auto ts = find_json_property(*check_info, "ts"); ts.has_value()) {
                id_source = json_value_to_string(*ts);
            }
        }
    }
    if (id_source.empty()) {
        id_source = json_string_property(object, "id");
    }
    if (id_source.empty()) {
        const auto timeline = json_string_property(object, "timeline");
        id_source = std::to_string(message.user_id.value_or(0)) + "|" + timeline + "|" + text;
    }
    message.id = "history:" + stable_hash(id_source);

    return message;
}

std::vector<ChatMessage> parse_json_messages(const std::string& json) {
    std::vector<ChatMessage> messages;
    for (const auto object : root_json_objects(json)) {
        const auto command = json_string_property(object, "cmd");
        std::optional<ChatMessage> message;
        if (command.rfind("DANMU_MSG", 0) == 0) {
            message = parse_danmu_message(object);
        } else if (command.rfind("SUPER_CHAT_MESSAGE", 0) == 0) {
            message = parse_super_chat_message(object);
        }

        if (message.has_value()) {
            messages.push_back(std::move(*message));
        }
    }
    return messages;
}

std::vector<ChatMessage> parse_history_messages(const std::string& json) {
    std::vector<ChatMessage> messages;
    const auto objects = root_json_objects(json);
    if (objects.empty()) {
        return messages;
    }

    const auto data = find_json_property(objects.front(), "data");
    if (!data.has_value()) {
        return messages;
    }

    for (const auto array_name : {"admin", "room"}) {
        const auto array = find_json_property(*data, array_name);
        if (!array.has_value()) {
            continue;
        }
        for (const auto item : json_array_values(*array)) {
            if (const auto message = parse_history_message(item); message.has_value()) {
                messages.push_back(std::move(*message));
            }
        }
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

bool resolve_name_from_cache(
    ChatMessage& message,
    const BilibiliClient::LogCallback& on_log,
    std::unordered_map<long long, std::wstring>& user_name_cache,
    bool& masked_name_warned) {
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
                return false;
            }
        } else if (!message.user_name.empty() && !contains_masked_name(message.user_name)) {
            user_name_cache[*message.user_id] = message.user_name;
        }
    }

    return true;
}

bool publish_with_name_cache(
    ChatMessage message,
    const BilibiliClient::MessageCallback& on_message,
    const BilibiliClient::LogCallback& on_log,
    std::unordered_map<long long, std::wstring>& user_name_cache,
    bool& masked_name_warned,
    long long& received_count) {
    if (!resolve_name_from_cache(message, on_log, user_name_cache, masked_name_warned)) {
        return false;
    }

    ++received_count;
    if (received_count <= 5 || received_count % 20 == 0) {
        on_log(L"已接收 B站实时弹幕 " + std::to_wstring(received_count) + L" 条。");
    }
    on_message(std::move(message));
    return true;
}

SocketHandle connect_tcp_socket(const DanmakuHost& host) {
    static WinsockSession winsock;
    if (!winsock.ok) {
        throw std::runtime_error("WinSock 初始化失败。");
    }

    addrinfoW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfoW* raw_result = nullptr;
    const auto service = std::to_wstring(host.port);
    const int resolve_result = GetAddrInfoW(host.host.c_str(), service.c_str(), &hints, &raw_result);
    if (resolve_result != 0 || raw_result == nullptr) {
        throw std::runtime_error("解析 TCP 弹幕服务器地址失败。");
    }

    std::unique_ptr<addrinfoW, decltype(&FreeAddrInfoW)> addresses(raw_result, FreeAddrInfoW);
    for (auto* address = addresses.get(); address != nullptr; address = address->ai_next) {
        SocketHandle socket(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        if (!socket) {
            continue;
        }

        DWORD timeout_ms = 5000;
        setsockopt(socket.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        setsockopt(socket.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

        if (::connect(socket.value, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
            return socket;
        }
    }

    throw std::runtime_error("连接 TCP 弹幕服务器失败。");
}

void send_all(SOCKET socket, const std::string& bytes) {
    std::size_t sent_total = 0;
    while (sent_total < bytes.size()) {
        const int sent = send(
            socket,
            bytes.data() + sent_total,
            static_cast<int>(bytes.size() - sent_total),
            0);
        if (sent == SOCKET_ERROR || sent == 0) {
            throw std::runtime_error("发送 TCP 弹幕数据失败。");
        }
        sent_total += static_cast<std::size_t>(sent);
    }
}

bool read_exact(SOCKET socket, char* data, std::size_t size, const std::shared_ptr<std::atomic_bool>& running) {
    std::size_t read_total = 0;
    while (read_total < size && running->load()) {
        const int received = recv(socket, data + read_total, static_cast<int>(size - read_total), 0);
        if (received > 0) {
            read_total += static_cast<std::size_t>(received);
            continue;
        }
        if (received == 0) {
            return false;
        }

        const int error = WSAGetLastError();
        if (error == WSAETIMEDOUT) {
            continue;
        }
        throw std::runtime_error("读取 TCP 弹幕数据失败：" + std::to_string(error));
    }

    return read_total == size;
}

void stream_tcp_endpoint(
    const AppSettings& settings,
    const BilibiliConnectionInfo& info,
    const DanmakuHost& host,
    const BilibiliClient::MessageCallback& on_message,
    const BilibiliClient::LogCallback& on_log,
    const std::shared_ptr<std::atomic_bool>& running) {
    if (host.port <= 0) {
        throw std::runtime_error("TCP 弹幕服务器端口无效。");
    }

    auto socket = connect_tcp_socket(host);
    on_log(L"已连接 TCP 弹幕服务器 " + tcp_endpoint_name(host));

    send_all(socket.value, build_auth_packet(settings, info.resolved_room_id, info.token));

    std::atomic_bool socket_open{true};
    std::thread heartbeat([&]() {
        while (running->load() && socket_open.load()) {
            for (int i = 0; i < 30 && running->load() && socket_open.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!running->load() || !socket_open.load()) {
                break;
            }
            try {
                send_all(socket.value, build_packet(2, 1, {}));
            } catch (...) {
                return;
            }
        }
    });

    try {
        std::vector<char> header(16);
        while (running->load()) {
            if (!read_exact(socket.value, header.data(), header.size(), running)) {
                break;
            }

            const int packet_length = read_be32(header.data());
            const int header_length = read_be16(header.data() + 4);
            const int version = read_be16(header.data() + 6);
            const int operation = read_be32(header.data() + 8);
            if (packet_length < header_length || header_length < 16 || packet_length > 16 * 1024 * 1024) {
                throw std::runtime_error("非法 TCP 弹幕包长度：" + std::to_string(packet_length));
            }

            std::string payload(static_cast<std::size_t>(packet_length - header_length), '\0');
            if (!payload.empty() && !read_exact(socket.value, payload.data(), payload.size(), running)) {
                break;
            }

            process_packet(version, operation, payload, on_message, on_log);
        }
    } catch (...) {
        socket_open.store(false);
        socket.reset();
        if (heartbeat.joinable()) {
            heartbeat.join();
        }
        throw;
    }

    socket_open.store(false);
    socket.reset();
    if (heartbeat.joinable()) {
        heartbeat.join();
    }
}

void stream_websocket_endpoint(
    const AppSettings& settings,
    const BilibiliConnectionInfo& info,
    const DanmakuHost& host,
    bool secure,
    const BilibiliClient::MessageCallback& on_message,
    const BilibiliClient::LogCallback& on_log,
    const std::shared_ptr<std::atomic_bool>& running) {
    const auto port = secure ? host.secure_websocket_port : host.websocket_port;
    if (port <= 0) {
        throw std::runtime_error("弹幕服务器端口无效。");
    }

    WinHttpHandle session(WinHttpOpen(
        settings.user_agent.empty() ? L"PitlaneDanmakuLite/0.1.1" : settings.user_agent.c_str(),
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
                process_packet_payload(message_buffer, on_message, on_log);
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
    std::mutex cache_mutex;
    std::unordered_set<std::string> history_seen_ids;
    std::deque<std::string> history_seen_order;
    auto last_realtime_message_at = std::chrono::steady_clock::time_point{};
    auto publish_realtime = [&](ChatMessage message) {
        std::lock_guard lock(cache_mutex);
        if (publish_with_name_cache(
                std::move(message),
                on_message,
                on_log,
                user_name_cache,
                masked_name_warned,
                received_count)) {
            last_realtime_message_at = std::chrono::steady_clock::now();
        }
    };
    on_log(L"B站真实房间号：" + std::to_wstring(info.resolved_room_id) + L"，可用弹幕服务器：" + std::to_wstring(info.hosts.size()) + L" 个。");

    std::thread history_thread([&, room_id = info.resolved_room_id]() {
        bool seeded = false;
        bool warned = false;
        while (running->load()) {
            try {
                auto history_messages = fetch_recent_history(effective_settings, room_id);
                std::vector<ChatMessage> fresh_messages;
                bool realtime_quiet = false;
                int displayed_count = 0;
                {
                    std::lock_guard lock(cache_mutex);
                    for (auto it = history_messages.rbegin(); it != history_messages.rend(); ++it) {
                        ChatMessage message = *it;
                        if (!history_seen_ids.insert(message.id).second) {
                            continue;
                        }
                        history_seen_order.push_back(message.id);
                        if (resolve_name_from_cache(message, on_log, user_name_cache, masked_name_warned)) {
                            fresh_messages.push_back(std::move(message));
                        }
                    }

                    while (history_seen_order.size() > 500) {
                        history_seen_ids.erase(history_seen_order.front());
                        history_seen_order.pop_front();
                    }

                    realtime_quiet = last_realtime_message_at == std::chrono::steady_clock::time_point{} ||
                        std::chrono::steady_clock::now() - last_realtime_message_at > std::chrono::seconds(6);
                    if (seeded && realtime_quiet) {
                        for (auto& message : fresh_messages) {
                            on_message(std::move(message));
                            ++displayed_count;
                        }
                    }
                }

                if (!seeded) {
                    seeded = true;
                    on_log(L"历史弹幕补偿轮询已启动。");
                } else if (realtime_quiet && displayed_count > 0) {
                    if (displayed_count > 0) {
                        on_log(L"历史弹幕补偿显示 " + std::to_wstring(displayed_count) + L" 条。");
                    }
                }
                warned = false;
            } catch (const std::exception& ex) {
                if (!warned) {
                    warned = true;
                    on_log(L"历史弹幕补偿轮询失败：" + utf8_to_wide(ex.what()));
                }
            }

            for (int i = 0; i < 30 && running->load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });

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
                        publish_realtime,
                        on_log,
                        running);
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

            if (!connected && running->load() && host.port > 0) {
                try {
                    stream_tcp_endpoint(
                        effective_settings,
                        info,
                        host,
                        publish_realtime,
                        on_log,
                        running);
                    connected = true;
                } catch (const std::exception& ex) {
                    const std::string error_message = ex.what();
                    const int length = MultiByteToWideChar(CP_UTF8, 0, error_message.data(), static_cast<int>(error_message.size()), nullptr, 0);
                    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
                    if (length > 0) {
                        MultiByteToWideChar(CP_UTF8, 0, error_message.data(), static_cast<int>(error_message.size()), wide.data(), length);
                    }
                    on_log(L"TCP 弹幕服务器 " + tcp_endpoint_name(host) + L" 断开：" + wide);
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

    if (history_thread.joinable()) {
        history_thread.join();
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

std::vector<ChatMessage> BilibiliClient::fetch_recent_history(const AppSettings& settings, int room_id) {
    const auto body = http_get(
        L"api.live.bilibili.com",
        L"/xlive/web-room/v1/dM/gethistory?roomid=" + std::to_wstring(room_id) + L"&room_type=0",
        settings);
    throw_if_bilibili_failed(body, L"历史弹幕");
    return parse_history_messages(body);
}

std::string BilibiliClient::http_get(const std::wstring& host, const std::wstring& path, const AppSettings& settings) {
    WinHttpHandle session(WinHttpOpen(
        settings.user_agent.empty() ? L"PitlaneDanmakuLite/0.1.1" : settings.user_agent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        throw std::runtime_error("WinHTTP 初始化失败。");
    }
    WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000);

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
