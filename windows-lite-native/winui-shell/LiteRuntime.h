#pragma once

#include "../src/AppSettings.h"
#include "../src/BilibiliClient.h"
#include "../src/ChatMessage.h"
#include "../src/LocalObsServer.h"
#include "../src/MessagePipeline.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace pitlane::lite::winui {

class LiteRuntime {
public:
    using LogSink = std::function<void(std::wstring)>;
    using MessageSink = std::function<void(ChatMessage)>;
    using LiveEndedSink = std::function<void()>;

    LiteRuntime();
    LiteRuntime(const LiteRuntime&) = delete;
    LiteRuntime& operator=(const LiteRuntime&) = delete;
    ~LiteRuntime();

    [[nodiscard]] const AppSettings& settings() const noexcept;
    [[nodiscard]] std::wstring settings_path() const;
    [[nodiscard]] std::wstring overlay_url() const;
    [[nodiscard]] bool obs_running() const noexcept;
    [[nodiscard]] bool live_running() const noexcept;

    void update_settings(AppSettings settings);
    void start_obs(LogSink sink);
    void stop_obs();
    void start_live(AppSettings settings, LogSink log_sink, MessageSink message_sink, LiveEndedSink ended_sink);
    void stop_live();
    [[nodiscard]] std::optional<ChatMessage> enqueue_preview(std::wstring user_name, std::wstring text, ChatMessageKind kind);

private:
    struct RuntimeState;
    struct LiveSession;

    static void obs_log_callback(void* context, std::wstring message);
    static void live_log(const std::shared_ptr<LiveSession>& session, std::wstring message);
    static void live_message(const std::shared_ptr<RuntimeState>& state, const std::shared_ptr<LiveSession>& session, ChatMessage message);
    static void live_ended(const std::shared_ptr<LiveSession>& session);
    void emit_log(std::wstring message) const;

    AppSettings settings_;
    std::shared_ptr<RuntimeState> state_;
    std::shared_ptr<LiveSession> live_session_;
    LogSink log_sink_;
    int preview_id_ = 1;
};

}  // namespace pitlane::lite::winui
