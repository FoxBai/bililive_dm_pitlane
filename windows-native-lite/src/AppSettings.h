#pragma once

#include <algorithm>
#include <string>

namespace pitlane::lite {

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

}  // namespace pitlane::lite
