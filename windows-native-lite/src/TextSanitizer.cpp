#include "TextSanitizer.h"

#include <cwctype>

namespace pitlane::lite {

namespace {

std::wstring trim(std::wstring value) {
    auto begin = value.begin();
    while (begin != value.end() && std::iswspace(*begin)) {
        ++begin;
    }

    auto end = value.end();
    while (end != begin && std::iswspace(*(end - 1))) {
        --end;
    }

    return std::wstring(begin, end);
}

}  // namespace

std::optional<ChatMessage> normalize_message(const ChatMessage& message, const AppSettings& settings) {
    auto normalized = message;
    normalized.user_name = clean_text(message.user_name, settings.max_nickname_length, settings.max_repeat_characters);
    normalized.text = clean_text(message.text, settings.max_message_length, settings.max_repeat_characters);

    if (normalized.user_name.empty()) {
        normalized.user_name = L"匿名观众";
    }

    if (!normalized.is_super_chat() && normalized.text.empty()) {
        return std::nullopt;
    }

    return normalized;
}

std::wstring clean_text(std::wstring value, int max_length, int max_repeat_characters) {
    value = trim(std::move(value));
    if (value.empty()) {
        return value;
    }

    std::wstring output;
    output.reserve(value.size());

    wchar_t last = 0;
    int repeat_count = 0;
    for (auto ch : value) {
        if (std::iswcntrl(ch) && ch != L'\n' && ch != L'\t') {
            continue;
        }

        if (ch == last) {
            ++repeat_count;
            if (repeat_count > max_repeat_characters) {
                continue;
            }
        } else {
            last = ch;
            repeat_count = 1;
        }

        output.push_back(ch);
        if (static_cast<int>(output.size()) >= max_length) {
            break;
        }
    }

    return trim(std::move(output));
}

}  // namespace pitlane::lite
