#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "LocalObsServer.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pitlane::lite {

namespace {

struct WinsockSession {
    WinsockSession() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockSession() {
        if (ok) {
            WSACleanup();
        }
    }

    bool ok = false;
};

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<std::size_t>(std::max(length, 0)), '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length, nullptr, nullptr);
    }
    return output;
}

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring output(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length);
    }
    return output;
}

std::string json_escape(std::string value) {
    std::string output;
    output.reserve(value.size() + 16);
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20) {
                const char* hex = "0123456789abcdef";
                output += "\\u00";
                output.push_back(hex[ch >> 4]);
                output.push_back(hex[ch & 0x0f]);
            } else {
                output.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return output;
}

std::string html_escape(std::string value) {
    std::string output;
    output.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&#39;"; break;
        default: output.push_back(ch); break;
        }
    }
    return output;
}

std::string content_type_for(const std::filesystem::path& path) {
    const auto extension = path.extension().wstring();
    if (extension == L".png") {
        return "image/png";
    }
    if (extension == L".jpg" || extension == L".jpeg") {
        return "image/jpeg";
    }
    if (extension == L".svg") {
        return "image/svg+xml";
    }
    if (extension == L".ttf") {
        return "font/ttf";
    }
    if (extension == L".json") {
        return "application/json; charset=utf-8";
    }
    return "application/octet-stream";
}

SOCKET as_socket(std::uintptr_t value) {
    return static_cast<SOCKET>(value);
}

}  // 匿名命名空间

LocalObsServer::~LocalObsServer() {
    stop();
}

void LocalObsServer::start(const AppSettings& settings, LogCallback on_log, void* log_context) {
    stop();
    settings_ = settings;
    settings_.normalize();
    on_log_ = on_log;
    log_context_ = log_context;

    static WinsockSession winsock;
    if (!winsock.ok) {
        throw std::runtime_error("WinSock 初始化失败。");
    }

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        throw std::runtime_error("创建 OBS 本地服务 socket 失败。");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<u_short>(settings_.obs_port));

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listener);
        throw std::runtime_error("启动 OBS 本地服务失败，端口可能被占用。");
    }

    listener_ = static_cast<std::uintptr_t>(listener);
    running_.store(true);
    server_thread_ = std::thread([this]() {
        accept_loop();
    });

    log(L"OBS 本地服务已启动：" + overlay_url());
}

void LocalObsServer::stop() {
    running_.store(false);
    if (listener_ != 0) {
        closesocket(as_socket(listener_));
        listener_ = 0;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    std::vector<std::uintptr_t> clients;
    {
        std::lock_guard lock(clients_mutex_);
        clients.swap(clients_);
    }
    for (const auto client : clients) {
        closesocket(as_socket(client));
    }
}

void LocalObsServer::broadcast(const ChatMessage& message) {
    const auto line = "data: " + message_json(message) + "\n\n";
    std::vector<std::uintptr_t> clients;
    {
        std::lock_guard lock(clients_mutex_);
        clients = clients_;
    }

    for (const auto client : clients) {
        const int sent = send(as_socket(client), line.data(), static_cast<int>(line.size()), 0);
        if (sent == SOCKET_ERROR) {
            remove_client(client);
        }
    }
}

std::wstring LocalObsServer::overlay_url() const {
    return L"http://127.0.0.1:" + std::to_wstring(settings_.obs_port) + L"/overlay";
}

void LocalObsServer::accept_loop() {
    while (running_.load()) {
        const SOCKET client = accept(as_socket(listener_), nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (running_.load()) {
                log(L"OBS 本地服务接收连接失败。");
            }
            return;
        }

        std::thread([this, client_value = static_cast<std::uintptr_t>(client)]() {
            handle_client(client_value);
        }).detach();
    }
}

void LocalObsServer::handle_client(std::uintptr_t socket_value) {
    const auto path = read_request_path(socket_value);
    if (path.empty()) {
        closesocket(as_socket(socket_value));
        return;
    }

    if (path == L"/" || path == L"/overlay") {
        send_response(socket_value, 200, "OK", "text/html; charset=utf-8", build_overlay_html());
        closesocket(as_socket(socket_value));
        return;
    }

    if (path == L"/health") {
        std::ostringstream body;
        body << "{\"status\":\"ok\",\"overlayUrl\":\""
             << json_escape(wide_to_utf8(overlay_url()))
             << "\",\"minVisibleItems\":"
             << settings_.min_visible_items
             << ",\"maxStageWidth\":"
             << settings_.max_stage_width
             << "}";
        send_response(socket_value, 200, "OK", "application/json; charset=utf-8", body.str());
        closesocket(as_socket(socket_value));
        return;
    }

    if (path == L"/events") {
        handle_events(socket_value);
        return;
    }

    if (path.rfind(L"/assets/", 0) == 0) {
        serve_asset(socket_value, path);
        closesocket(as_socket(socket_value));
        return;
    }

    send_response(socket_value, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
    closesocket(as_socket(socket_value));
}

void LocalObsServer::handle_events(std::uintptr_t socket_value) {
    const std::string headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
        ": connected\n\n";
    send_bytes(socket_value, headers);

    {
        std::lock_guard lock(clients_mutex_);
        clients_.push_back(socket_value);
    }
}

void LocalObsServer::serve_asset(std::uintptr_t socket_value, const std::wstring& path) {
    const auto file_path = resolve_asset_path(path);
    if (file_path.empty()) {
        send_response(socket_value, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
        return;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        send_response(socket_value, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
        return;
    }

    std::ostringstream body;
    body << file.rdbuf();
    send_response(socket_value, 200, "OK", content_type_for(file_path).c_str(), body.str());
}

std::wstring LocalObsServer::read_request_path(std::uintptr_t socket_value) {
    std::string request;
    std::vector<char> buffer(4096);
    const int received = recv(as_socket(socket_value), buffer.data(), static_cast<int>(buffer.size() - 1), 0);
    if (received <= 0) {
        return {};
    }
    request.assign(buffer.data(), static_cast<std::size_t>(received));

    const auto line_end = request.find("\r\n");
    const auto request_line = request.substr(0, line_end == std::string::npos ? request.size() : line_end);
    const auto first_space = request_line.find(' ');
    const auto second_space = first_space == std::string::npos ? std::string::npos : request_line.find(' ', first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos) {
        return {};
    }

    auto target = request_line.substr(first_space + 1, second_space - first_space - 1);
    const auto query = target.find('?');
    if (query != std::string::npos) {
        target.resize(query);
    }
    return utf8_to_wide(target);
}

std::string LocalObsServer::build_overlay_html() const {
    std::ostringstream html;
    html << R"html(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Pitlane Danmaku Lite Overlay</title>
  <style>
    @font-face {
      font-family: "HarmonyOS Sans SC";
      src: url("/assets/fonts/HarmonyOS_Sans_SC_Regular.ttf") format("truetype");
      font-weight: 400;
    }
    @font-face {
      font-family: "HarmonyOS Sans SC";
      src: url("/assets/fonts/HarmonyOS_Sans_SC_Bold.ttf") format("truetype");
      font-weight: 900;
    }
    html, body, #stage {
      width: 100%;
      height: 100%;
      margin: 0;
      overflow: hidden;
      background: transparent;
      font-family: "HarmonyOS Sans SC", "Microsoft YaHei", "Segoe UI", sans-serif;
    }
    .item {
      position: absolute;
      width: 1280px;
      height: 900px;
      left: 0;
      bottom: 10px;
      transform-origin: left bottom;
      will-change: transform, opacity;
      opacity: 1;
      transition: opacity .28s ease;
    }
    .frame {
      position: absolute;
      inset: 0;
      width: 1280px;
      height: 900px;
      object-fit: contain;
      filter: drop-shadow(0 12px 18px rgba(0,0,0,.2));
    }
    .car {
      position: absolute;
      left: 650px;
      object-fit: contain;
      pointer-events: none;
      filter: drop-shadow(0 10px 10px rgba(0,0,0,.22));
    }
    .text {
      position: absolute;
      left: 82px;
      top: 318px;
      width: 650px;
      color: white;
      text-shadow: 0 3px 8px rgba(0,0,0,.55);
    }
    .name {
      display: flex;
      gap: 12px;
      align-items: center;
      max-width: 620px;
      font-size: 70px;
      line-height: 1;
      font-weight: 900;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .message {
      margin-top: 28px;
      max-height: 280px;
      font-size: 62px;
      line-height: 93px;
      font-weight: 650;
      display: -webkit-box;
      -webkit-line-clamp: 3;
      -webkit-box-orient: vertical;
      overflow: hidden;
    }
    .badge {
      display: none;
      flex: 0 0 auto;
      padding: 7px 14px 8px;
      border-radius: 10px;
      color: #16120a;
      background: #fde68a;
      font-size: 30px;
      line-height: 1;
      font-weight: 900;
      text-shadow: none;
    }
    .nameText {
      min-width: 0;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .superchat .badge {
      display: inline-block;
    }
    .superchat .name {
      max-width: 500px;
    }
    .superchat .message {
      color: #fff7d6;
      text-shadow: 0 3px 8px rgba(0,0,0,.58), 0 0 12px rgba(253,230,138,.22);
    }
    .superchat .car {
      filter: drop-shadow(0 10px 10px rgba(0,0,0,.24)) drop-shadow(0 0 16px rgba(253,230,138,.25));
    }
  </style>
</head>
<body>
  <div id="stage"></div>
  <script>
    const stage = document.getElementById("stage");
    const framePath = "/assets/comment-box/comment_frame.png";
    const cars = [
      { id: "car_01", url: "/assets/cars/car_01.png", width: 555, height: 215 },
      { id: "car_02", url: "/assets/cars/car_02.png", width: 531, height: 215 },
      { id: "car_03", url: "/assets/cars/car_03.png", width: 562, height: 209 },
      { id: "car_04", url: "/assets/cars/car_04.png", width: 555, height: 209 },
      { id: "car_05", url: "/assets/cars/car_05.png", width: 585, height: 200 },
      { id: "car_06", url: "/assets/cars/car_06.png", width: 556, height: 201 },
      { id: "car_07", url: "/assets/cars/car_07.png", width: 570, height: 203 },
      { id: "car_08", url: "/assets/cars/car_08.png", width: 556, height: 202 },
      { id: "car_09", url: "/assets/cars/car_09.png", width: 591, height: 206 },
      { id: "car_10", url: "/assets/cars/car_10.png", width: 595, height: 207 }
    ];
    const items = [];
    const queue = [];
    const baseWidth = 1280;
    const baseHeight = 900;
    const baseGap = 36;
    const minVisible = )html"
         << settings_.min_visible_items
         << R"html(;
    const maxStageWidth = )html"
         << settings_.max_stage_width
         << R"html(;
    const carBottom = 48;
    let lastInsertAt = 0;

    [framePath, ...cars.map(car => car.url)].forEach(url => {
      const image = new Image();
      image.src = url;
    });

    function escapeHtml(value) {
      return String(value ?? "").replace(/[&<>"']/g, ch => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[ch]));
    }
    function pickCar() {
      return cars[Math.floor(Math.random() * cars.length)];
    }
    function scaleForStage() {
      const heightScale = Math.max(0.2, (stage.clientHeight - 10) / baseHeight);
      const widthScale = Math.max(0.2, stage.clientWidth / (minVisible * baseWidth + (minVisible - 1) * baseGap));
      return Math.min(1, heightScale, widthScale);
    }
    function layout() {
      const scale = scaleForStage();
      const width = stage.clientWidth;
      const itemWidth = baseWidth * scale;
      const gap = baseGap * scale;
      const capacity = Math.max(minVisible, Math.floor(Math.min(width, maxStageWidth) / (itemWidth + gap)));
      while (items.filter(item => !item.leaving).length > capacity) {
        const item = items.pop();
        if (!item) break;
        item.leaving = true;
        item.target = width + itemWidth;
        items.unshift(item);
      }

      let cursor = width - itemWidth - 10;
      for (let index = items.length - 1; index >= 0; index--) {
        const item = items[index];
        item.scale = scale;
        if (!item.leaving) {
          item.target = Math.max(-itemWidth, cursor);
          cursor -= itemWidth + gap;
        }
      }
    }
    function addMessage(message) {
      const car = pickCar();
      const carTop = baseHeight - car.height - carBottom;
      const node = document.createElement("div");
      node.className = `item ${message.kind === "superchat" ? "superchat" : "comment"}`;
      const badgeText = message.kind === "superchat" && message.price ? `SC ¥${message.price}` : "SC";
      node.innerHTML = `
        <img class="frame" src="${framePath}" alt="">
        <img class="car" src="${car.url}" alt="" style="top:${carTop}px;width:${car.width}px;height:${car.height}px">
        <div class="text">
          <div class="name"><span class="badge">${escapeHtml(badgeText)}</span><span class="nameText">${escapeHtml(message.userName)}</span></div>
          <div class="message">${escapeHtml(message.text)}</div>
        </div>`;
      stage.appendChild(node);
      items.unshift({ node, x: -baseWidth, target: 0, scale: scaleForStage(), leaving: false });
      layout();
    }
    function enqueueMessage(message) {
      queue.push(message);
      while (queue.length > 80) queue.shift();
    }
    function tick(now) {
      const interval = Math.max(180, Math.min(420, )html"
         << settings_.launch_interval_ms
         << R"html());
      if (queue.length > 0 && now - lastInsertAt >= interval) {
        addMessage(queue.shift());
        lastInsertAt = now;
      }
      for (let index = items.length - 1; index >= 0; index--) {
        const item = items[index];
        const pressure = Math.min(1, items.length / Math.max(1, minVisible + 3));
        const easing = item.leaving ? 0.075 : 0.045 + pressure * 0.035;
        item.x += (item.target - item.x) * easing;
        item.node.style.transform = `translateX(${item.x}px) scale(${item.scale})`;
        item.node.style.opacity = item.leaving ? "0" : "1";
        if (item.leaving && item.x > stage.clientWidth + baseWidth) {
          item.node.remove();
          items.splice(index, 1);
        }
      }
      requestAnimationFrame(tick);
    }
    addEventListener("resize", layout);
    const events = new EventSource("/events");
    events.onmessage = event => enqueueMessage(JSON.parse(event.data));
    requestAnimationFrame(tick);
  </script>
</body>
</html>)html";
    return html.str();
}

std::string LocalObsServer::message_json(const ChatMessage& message) {
    std::ostringstream json;
    json << "{\"id\":\"" << json_escape(message.id)
         << "\",\"userName\":\"" << json_escape(wide_to_utf8(message.user_name))
         << "\",\"text\":\"" << json_escape(wide_to_utf8(message.text))
         << "\",\"kind\":\"" << (message.is_super_chat() ? "superchat" : "comment")
         << "\",\"price\":";
    if (message.price.has_value()) {
        json << *message.price;
    } else {
        json << "null";
    }
    json << "}";
    return json.str();
}

std::filesystem::path LocalObsServer::resolve_asset_path(const std::wstring& request_path) {
    auto relative = request_path.substr(std::wstring(L"/assets/").size());
    if (relative.find(L"..") != std::wstring::npos || relative.find(L":") != std::wstring::npos) {
        return {};
    }

    std::replace(relative.begin(), relative.end(), L'/', std::filesystem::path::preferred_separator);
    std::vector<std::filesystem::path> roots{std::filesystem::current_path()};
    auto probe = std::filesystem::current_path();
    for (int i = 0; i < 6 && !probe.empty(); ++i) {
        roots.push_back(probe);
        probe = probe.parent_path();
    }

    for (const auto& root : roots) {
        const auto candidate = root / L"assets" / relative;
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

void LocalObsServer::send_response(std::uintptr_t socket_value, int status, const char* reason, const char* content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Cache-Control: no-cache\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    send_bytes(socket_value, response.str());
}

void LocalObsServer::send_bytes(std::uintptr_t socket_value, const std::string& bytes) {
    send(as_socket(socket_value), bytes.data(), static_cast<int>(bytes.size()), 0);
}

void LocalObsServer::log(std::wstring message) const {
    if (on_log_ != nullptr) {
        on_log_(log_context_, std::move(message));
    }
}

void LocalObsServer::remove_client(std::uintptr_t socket_value) {
    std::lock_guard lock(clients_mutex_);
    const auto it = std::remove(clients_.begin(), clients_.end(), socket_value);
    if (it != clients_.end()) {
        closesocket(as_socket(socket_value));
        clients_.erase(it, clients_.end());
    }
}

}  // 命名空间 pitlane::lite
