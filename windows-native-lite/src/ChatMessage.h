#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace pitlane::lite {

enum class ChatMessageKind {
    Comment,
    SuperChat
};

struct ChatMessage {
    std::string id;
    std::wstring user_name;
    std::wstring text;
    ChatMessageKind kind = ChatMessageKind::Comment;
    std::chrono::system_clock::time_point received_at = std::chrono::system_clock::now();
    std::optional<std::int64_t> user_id;
    std::optional<double> price;

    [[nodiscard]] bool is_super_chat() const noexcept {
        return kind == ChatMessageKind::SuperChat;
    }
};

}  // namespace pitlane::lite
