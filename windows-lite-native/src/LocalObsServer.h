#pragma once

#include "AppSettings.h"
#include "ChatMessage.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pitlane::lite {

class LocalObsServer {
public:
    using LogCallback = void (*)(void* context, std::wstring message);

    LocalObsServer() = default;
    LocalObsServer(const LocalObsServer&) = delete;
    LocalObsServer& operator=(const LocalObsServer&) = delete;

    ~LocalObsServer();

    void start(const AppSettings& settings, LogCallback on_log, void* log_context);
    void stop();
    void broadcast(const ChatMessage& message);

    [[nodiscard]] std::wstring overlay_url() const;

private:
    void accept_loop();
    void handle_client(std::uintptr_t socket_value);
    void handle_events(std::uintptr_t socket_value);
    void serve_asset(std::uintptr_t socket_value, const std::wstring& path);

    static std::wstring read_request_path(std::uintptr_t socket_value);
    std::string build_overlay_html() const;
    static std::string message_json(const ChatMessage& message);
    static std::filesystem::path resolve_asset_path(const std::wstring& request_path);
    static void send_response(std::uintptr_t socket_value, int status, const char* reason, const char* content_type, const std::string& body);
    static void send_bytes(std::uintptr_t socket_value, const std::string& bytes);

    void log(std::wstring message) const;
    void remove_client(std::uintptr_t socket_value);

    AppSettings settings_;
    std::atomic_bool running_ = false;
    std::uintptr_t listener_ = 0;
    std::thread server_thread_;
    mutable std::mutex clients_mutex_;
    std::vector<std::uintptr_t> clients_;
    LogCallback on_log_ = nullptr;
    void* log_context_ = nullptr;
};

}  // 命名空间 pitlane::lite
