#pragma once

#include "../src/AppSettings.h"
#include "../src/ChatMessage.h"
#include "../src/LocalObsServer.h"
#include "../src/MessagePipeline.h"

#include <functional>
#include <optional>
#include <string>

namespace pitlane::lite::winui {

class LiteRuntime {
public:
    using LogSink = std::function<void(std::wstring)>;

    LiteRuntime();
    LiteRuntime(const LiteRuntime&) = delete;
    LiteRuntime& operator=(const LiteRuntime&) = delete;
    ~LiteRuntime();

    [[nodiscard]] const AppSettings& settings() const noexcept;
    [[nodiscard]] std::wstring settings_path() const;
    [[nodiscard]] std::wstring overlay_url() const;
    [[nodiscard]] bool obs_running() const noexcept;

    void update_settings(AppSettings settings);
    void start_obs(LogSink sink);
    void stop_obs();
    [[nodiscard]] std::optional<ChatMessage> enqueue_preview(std::wstring user_name, std::wstring text, ChatMessageKind kind);

private:
    static void obs_log_callback(void* context, std::wstring message);
    void emit_log(std::wstring message) const;

    AppSettings settings_;
    MessagePipeline pipeline_;
    LocalObsServer obs_server_;
    LogSink log_sink_;
    bool obs_running_ = false;
    int preview_id_ = 1;
};

}  // namespace pitlane::lite::winui
