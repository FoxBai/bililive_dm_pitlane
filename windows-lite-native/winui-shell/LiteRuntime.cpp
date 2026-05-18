#include "pch.h"
#include "LiteRuntime.h"

#include "../src/SettingsStore.h"

#include <format>
#include <thread>
#include <utility>

namespace pitlane::lite::winui {

struct LiteRuntime::RuntimeState {
    explicit RuntimeState(AppSettings settings)
        : pipeline(std::move(settings)) {
    }

    MessagePipeline pipeline;
    LocalObsServer obs_server;
    std::mutex obs_mutex;
    std::atomic_bool obs_running = false;
};

struct LiteRuntime::LiveSession {
    std::shared_ptr<std::atomic_bool> running = std::make_shared<std::atomic_bool>(false);
    std::mutex callback_mutex;
    LogSink log_sink;
    MessageSink message_sink;
    LiveEndedSink ended_sink;
};

namespace {

std::wstring utf8_to_wide(const char* value) {
    if (value == nullptr || *value == '\0') {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (length <= 0) {
        return L"未知错误";
    }

    std::wstring output(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, output.data(), length);
    return output;
}

}  // namespace

LiteRuntime::LiteRuntime()
    : settings_(SettingsStore::load()),
      state_(std::make_shared<RuntimeState>(settings_)) {
}

LiteRuntime::~LiteRuntime() {
    stop_live();
    stop_obs();
}

const AppSettings& LiteRuntime::settings() const noexcept {
    return settings_;
}

std::wstring LiteRuntime::settings_path() const {
    return SettingsStore::settings_path().wstring();
}

std::wstring LiteRuntime::overlay_url() const {
    return L"http://127.0.0.1:" + std::to_wstring(settings_.obs_port) + L"/overlay";
}

bool LiteRuntime::obs_running() const noexcept {
    return state_->obs_running.load();
}

bool LiteRuntime::live_running() const noexcept {
    return live_session_ != nullptr && live_session_->running->load();
}

void LiteRuntime::update_settings(AppSettings settings) {
    settings.normalize();
    settings_ = std::move(settings);
    state_->pipeline.update_settings(settings_);
    SettingsStore::save(settings_);
}

void LiteRuntime::start_obs(LogSink sink) {
    log_sink_ = std::move(sink);
    std::lock_guard lock(state_->obs_mutex);
    state_->obs_server.start(settings_, obs_log_callback, this);
    state_->obs_running.store(true);
}

void LiteRuntime::stop_obs() {
    std::lock_guard lock(state_->obs_mutex);
    state_->obs_server.stop();
    state_->obs_running.store(false);
}

void LiteRuntime::start_live(AppSettings settings, LogSink log_sink, MessageSink message_sink, LiveEndedSink ended_sink) {
    stop_live();
    update_settings(std::move(settings));

    auto session = std::make_shared<LiveSession>();
    session->running->store(true);
    session->log_sink = std::move(log_sink);
    session->message_sink = std::move(message_sink);
    session->ended_sink = std::move(ended_sink);
    live_session_ = session;

    auto state = state_;
    auto live_settings = settings_;
    live_log(session, L"正在连接直播间：" + live_settings.room_input);

    std::thread([state, session, live_settings = std::move(live_settings)]() mutable {
        try {
            BilibiliClient client;
            client.stream_messages(
                live_settings,
                [state, session](ChatMessage message) {
                    live_message(state, session, std::move(message));
                },
                [session](std::wstring message) {
                    live_log(session, std::move(message));
                },
                session->running);
        } catch (const std::exception& ex) {
            if (session->running->load()) {
                live_log(session, L"直播间连接失败：" + utf8_to_wide(ex.what()));
            }
        }

        session->running->store(false);
        live_ended(session);
    }).detach();
}

void LiteRuntime::stop_live() {
    auto session = std::move(live_session_);
    if (!session) {
        return;
    }

    session->running->store(false);
    std::lock_guard lock(session->callback_mutex);
    session->log_sink = nullptr;
    session->message_sink = nullptr;
    session->ended_sink = nullptr;
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

    state_->pipeline.enqueue(message);

    ChatMessage ready;
    if (!state_->pipeline.try_dequeue(ready)) {
        return std::nullopt;
    }

    if (state_->obs_running.load()) {
        std::lock_guard lock(state_->obs_mutex);
        if (state_->obs_running.load()) {
            state_->obs_server.broadcast(ready);
        }
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

void LiteRuntime::live_log(const std::shared_ptr<LiveSession>& session, std::wstring message) {
    std::lock_guard lock(session->callback_mutex);
    if (session->log_sink) {
        session->log_sink(std::move(message));
    }
}

void LiteRuntime::live_message(const std::shared_ptr<RuntimeState>& state, const std::shared_ptr<LiveSession>& session, ChatMessage message) {
    if (!session->running->load()) {
        return;
    }

    state->pipeline.enqueue(message);

    ChatMessage ready;
    while (state->pipeline.try_dequeue(ready)) {
        if (state->obs_running.load()) {
            std::lock_guard lock(state->obs_mutex);
            if (state->obs_running.load()) {
                state->obs_server.broadcast(ready);
            }
        }

        std::lock_guard lock(session->callback_mutex);
        if (session->message_sink) {
            session->message_sink(ready);
        }
    }
}

void LiteRuntime::live_ended(const std::shared_ptr<LiveSession>& session) {
    std::lock_guard lock(session->callback_mutex);
    if (session->ended_sink) {
        session->ended_sink();
    }
}

}  // namespace pitlane::lite::winui
