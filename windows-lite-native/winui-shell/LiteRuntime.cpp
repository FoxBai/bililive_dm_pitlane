#include "pch.h"
#include "LiteRuntime.h"

#include "../src/SettingsStore.h"

#include <format>
#include <utility>

namespace pitlane::lite::winui {

LiteRuntime::LiteRuntime()
    : settings_(SettingsStore::load()),
      pipeline_(settings_) {
}

LiteRuntime::~LiteRuntime() {
    stop_obs();
}

const AppSettings& LiteRuntime::settings() const noexcept {
    return settings_;
}

std::wstring LiteRuntime::settings_path() const {
    return SettingsStore::settings_path().wstring();
}

std::wstring LiteRuntime::overlay_url() const {
    return obs_server_.overlay_url();
}

bool LiteRuntime::obs_running() const noexcept {
    return obs_running_;
}

void LiteRuntime::update_settings(AppSettings settings) {
    settings.normalize();
    settings_ = std::move(settings);
    pipeline_.update_settings(settings_);
    SettingsStore::save(settings_);
}

void LiteRuntime::start_obs(LogSink sink) {
    log_sink_ = std::move(sink);
    obs_server_.start(settings_, obs_log_callback, this);
    obs_running_ = true;
}

void LiteRuntime::stop_obs() {
    obs_server_.stop();
    obs_running_ = false;
}

std::optional<ChatMessage> LiteRuntime::enqueue_preview(std::wstring user_name, std::wstring text, ChatMessageKind kind) {
    ChatMessage message;
    message.id = std::format("winui-preview-{}", preview_id_++);
    message.user_name = std::move(user_name);
    message.text = std::move(text);
    message.kind = kind;
    if (kind == ChatMessageKind::SuperChat) {
        message.price = 30.0;
    }

    pipeline_.enqueue(message);

    ChatMessage ready;
    if (!pipeline_.try_dequeue(ready)) {
        return std::nullopt;
    }

    if (obs_running_) {
        obs_server_.broadcast(ready);
    }
    return ready;
}

void LiteRuntime::obs_log_callback(void* context, std::wstring message) {
    if (context == nullptr) {
        return;
    }

    static_cast<LiteRuntime*>(context)->emit_log(std::move(message));
}

void LiteRuntime::emit_log(std::wstring message) const {
    if (log_sink_) {
        log_sink_(std::move(message));
    }
}

}  // namespace pitlane::lite::winui
