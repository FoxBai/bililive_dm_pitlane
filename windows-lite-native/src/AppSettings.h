#pragma once

#include <algorithm>
#include <cwctype>
#include <string>

namespace pitlane::lite {

namespace detail {

inline std::wstring trim_copy(std::wstring value) {
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

inline std::wstring lower_copy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

inline bool starts_with_ci(const std::wstring& value, const std::wstring& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }

    return lower_copy(value.substr(0, prefix.size())) == lower_copy(prefix);
}

inline std::wstring clean_cookie_value(std::wstring value) {
    value = trim_copy(std::move(value));
    while (!value.empty() && (value.front() == L'"' || value.front() == L'\'' || value.front() == L'`' || value.front() == L'\\')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == L'"' || value.back() == L'\'' || value.back() == L'`' || value.back() == L'\\' || value.back() == L';')) {
        value.pop_back();
    }
    return trim_copy(std::move(value));
}

inline std::wstring normalize_cookie_input(std::wstring value) {
    value = trim_copy(std::move(value));
    if (value.empty()) {
        return {};
    }

    std::size_t line_start = 0;
    while (line_start < value.size()) {
        auto line_end = value.find_first_of(L"\r\n", line_start);
        auto line = trim_copy(value.substr(line_start, line_end == std::wstring::npos ? std::wstring::npos : line_end - line_start));
        if (starts_with_ci(line, L"Cookie:")) {
            return clean_cookie_value(line.substr(7));
        }
        if (line_end == std::wstring::npos) {
            break;
        }
        line_start = line_end + 1;
        while (line_start < value.size() && (value[line_start] == L'\r' || value[line_start] == L'\n')) {
            ++line_start;
        }
    }

    const auto lower = lower_copy(value);
    const auto embedded = lower.find(L"cookie:");
    if (embedded != std::wstring::npos) {
        auto cookie_value = value.substr(embedded + 7);
        const auto line_end = cookie_value.find_first_of(L"\r\n");
        if (line_end != std::wstring::npos) {
            cookie_value.resize(line_end);
        }
        return clean_cookie_value(std::move(cookie_value));
    }

    return clean_cookie_value(std::move(value));
}

}  // 命名空间 detail

struct AppSettings {
    std::wstring room_input;
    std::wstring cookie;
    std::wstring user_agent =
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        L"(KHTML, like Gecko) Chrome/124.0 Safari/537.36";
    std::wstring buvid3;

    int obs_port = 17333;
    int launch_interval_ms = 900;
    int queue_limit = 80;
    int min_visible_items = 5;
    int max_nickname_length = 18;
    int max_message_length = 42;
    int max_repeat_characters = 4;
    int max_stage_width = 3840;
    bool only_super_chat = false;

    void normalize() {
        user_agent = detail::trim_copy(std::move(user_agent));
        if (user_agent.empty()) {
            user_agent =
                L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                L"(KHTML, like Gecko) Chrome/124.0 Safari/537.36";
        }
        room_input = detail::trim_copy(std::move(room_input));
        cookie = detail::normalize_cookie_input(std::move(cookie));
        buvid3 = detail::trim_copy(std::move(buvid3));
        obs_port = std::clamp(obs_port, 1024, 65535);
        launch_interval_ms = std::clamp(launch_interval_ms, 120, 10000);
        queue_limit = std::clamp(queue_limit, 5, 500);
        min_visible_items = std::clamp(min_visible_items, 1, 12);
        max_nickname_length = std::clamp(max_nickname_length, 4, 64);
        max_message_length = std::clamp(max_message_length, 4, 200);
        max_repeat_characters = std::clamp(max_repeat_characters, 2, 20);
        max_stage_width = std::clamp(max_stage_width, 960, 7680);
    }
};

}  // 命名空间 pitlane::lite
