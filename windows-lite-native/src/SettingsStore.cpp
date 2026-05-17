#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "SettingsStore.h"

#include <windows.h>

#include <cstdlib>
#include <string>

namespace pitlane::lite {

namespace {

constexpr wchar_t kSection[] = L"settings";

std::wstring read_string(const std::filesystem::path& path, const wchar_t* key, const std::wstring& fallback) {
    std::wstring buffer(4096, L'\0');
    const auto length = GetPrivateProfileStringW(
        kSection,
        key,
        fallback.c_str(),
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        path.c_str());
    buffer.resize(length);
    return buffer;
}

int read_int(const std::filesystem::path& path, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(kSection, key, fallback, path.c_str()));
}

void write_string(const std::filesystem::path& path, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(kSection, key, value.c_str(), path.c_str());
}

void write_int(const std::filesystem::path& path, const wchar_t* key, int value) {
    WritePrivateProfileStringW(kSection, key, std::to_wstring(value).c_str(), path.c_str());
}

}  // 匿名命名空间

AppSettings SettingsStore::load() {
    AppSettings settings;
    const auto path = settings_path();
    if (!std::filesystem::exists(path)) {
        settings.normalize();
        return settings;
    }

    settings.room_input = read_string(path, L"room_input", settings.room_input);
    settings.cookie = read_string(path, L"cookie", settings.cookie);
    settings.user_agent = read_string(path, L"user_agent", settings.user_agent);
    settings.buvid3 = read_string(path, L"buvid3", settings.buvid3);
    settings.obs_port = read_int(path, L"obs_port", settings.obs_port);
    settings.launch_interval_ms = read_int(path, L"launch_interval_ms", settings.launch_interval_ms);
    settings.queue_limit = read_int(path, L"queue_limit", settings.queue_limit);
    settings.min_visible_items = read_int(path, L"min_visible_items", settings.min_visible_items);
    settings.max_nickname_length = read_int(path, L"max_nickname_length", settings.max_nickname_length);
    settings.max_message_length = read_int(path, L"max_message_length", settings.max_message_length);
    settings.max_repeat_characters = read_int(path, L"max_repeat_characters", settings.max_repeat_characters);
    settings.max_stage_width = read_int(path, L"max_stage_width", settings.max_stage_width);
    settings.only_super_chat = read_int(path, L"only_super_chat", settings.only_super_chat ? 1 : 0) != 0;
    settings.normalize();
    return settings;
}

void SettingsStore::save(const AppSettings& value) {
    try {
        auto settings = value;
        settings.normalize();
        const auto path = settings_path();
        std::filesystem::create_directories(path.parent_path());

        write_string(path, L"room_input", settings.room_input);
        write_string(path, L"cookie", settings.cookie);
        write_string(path, L"user_agent", settings.user_agent);
        write_string(path, L"buvid3", settings.buvid3);
        write_int(path, L"obs_port", settings.obs_port);
        write_int(path, L"launch_interval_ms", settings.launch_interval_ms);
        write_int(path, L"queue_limit", settings.queue_limit);
        write_int(path, L"min_visible_items", settings.min_visible_items);
        write_int(path, L"max_nickname_length", settings.max_nickname_length);
        write_int(path, L"max_message_length", settings.max_message_length);
        write_int(path, L"max_repeat_characters", settings.max_repeat_characters);
        write_int(path, L"max_stage_width", settings.max_stage_width);
        write_int(path, L"only_super_chat", settings.only_super_chat ? 1 : 0);
    } catch (...) {
        // 设置保存失败不应影响弹幕展示。
    }
}

std::filesystem::path SettingsStore::settings_path() {
    wchar_t* local_app_data = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&local_app_data, &length, L"LOCALAPPDATA") == 0 && local_app_data != nullptr && *local_app_data != L'\0') {
        auto path = std::filesystem::path(local_app_data) / L"PitlaneDanmakuLite" / L"settings.ini";
        free(local_app_data);
        return path;
    }
    free(local_app_data);

    return std::filesystem::current_path() / L"PitlaneDanmakuLite.settings.ini";
}

}  // 命名空间 pitlane::lite
