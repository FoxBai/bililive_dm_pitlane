#pragma once

#include "AppSettings.h"
#include "ChatMessage.h"

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>

namespace pitlane::lite {

class MessagePipeline {
public:
    using ReadyCallback = std::function<void(const ChatMessage&)>;

    explicit MessagePipeline(AppSettings settings);

    void update_settings(AppSettings settings);
    void enqueue(const ChatMessage& message);
    [[nodiscard]] bool try_dequeue(ChatMessage& message);
    void set_ready_callback(ReadyCallback callback);

private:
    void remember_id(const std::string& id);

    std::mutex mutex_;
    AppSettings settings_;
    std::deque<ChatMessage> normal_queue_;
    std::deque<ChatMessage> priority_queue_;
    std::unordered_set<std::string> recent_ids_;
    std::deque<std::string> recent_id_order_;
    ReadyCallback ready_callback_;
};

}  // 命名空间 pitlane::lite
