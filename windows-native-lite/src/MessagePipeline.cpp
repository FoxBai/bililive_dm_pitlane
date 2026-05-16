#include "MessagePipeline.h"
#include "TextSanitizer.h"

namespace pitlane::lite {

MessagePipeline::MessagePipeline(AppSettings settings)
    : settings_(std::move(settings)) {
    settings_.normalize();
}

void MessagePipeline::update_settings(AppSettings settings) {
    std::scoped_lock lock(mutex_);
    settings_ = std::move(settings);
    settings_.normalize();

    if (settings_.only_super_chat) {
        normal_queue_.clear();
    }
}

void MessagePipeline::enqueue(const ChatMessage& message) {
    std::scoped_lock lock(mutex_);

    auto normalized = normalize_message(message, settings_);
    if (!normalized) {
        return;
    }

    if (settings_.only_super_chat && !normalized->is_super_chat()) {
        return;
    }

    if (!normalized->id.empty() && recent_ids_.contains(normalized->id)) {
        return;
    }

    remember_id(normalized->id);

    while (static_cast<int>(normal_queue_.size() + priority_queue_.size()) >= settings_.queue_limit) {
        if (!normal_queue_.empty()) {
            normal_queue_.pop_front();
            continue;
        }

        if (!normalized->is_super_chat()) {
            return;
        }

        if (!priority_queue_.empty()) {
            priority_queue_.pop_front();
        }
    }

    if (normalized->is_super_chat()) {
        priority_queue_.push_back(std::move(*normalized));
    } else {
        normal_queue_.push_back(std::move(*normalized));
    }
}

bool MessagePipeline::try_dequeue(ChatMessage& message) {
    std::scoped_lock lock(mutex_);

    if (!priority_queue_.empty()) {
        message = std::move(priority_queue_.front());
        priority_queue_.pop_front();
        return true;
    }

    if (!normal_queue_.empty()) {
        message = std::move(normal_queue_.front());
        normal_queue_.pop_front();
        return true;
    }

    return false;
}

void MessagePipeline::set_ready_callback(ReadyCallback callback) {
    std::scoped_lock lock(mutex_);
    ready_callback_ = std::move(callback);
}

void MessagePipeline::remember_id(const std::string& id) {
    if (id.empty()) {
        return;
    }

    recent_ids_.insert(id);
    recent_id_order_.push_back(id);

    while (recent_id_order_.size() > 500) {
        recent_ids_.erase(recent_id_order_.front());
        recent_id_order_.pop_front();
    }
}

}  // namespace pitlane::lite
