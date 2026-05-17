#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AppSettings.h"
#include "BilibiliClient.h"
#include "ChatMessage.h"
#include "MessagePipeline.h"

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kRoomEditId = 1001;
constexpr int kCookieEditId = 1002;
constexpr int kOnlySuperChatId = 1003;
constexpr int kOverlayVisibleId = 1004;
constexpr int kConnectRoomId = 1005;
constexpr int kAddCommentId = 1006;
constexpr int kAddSuperChatId = 1007;
constexpr int kDrainTimerId = 2001;
constexpr UINT kAppendLogMessage = WM_APP + 1;
constexpr UINT kRealtimeChatMessage = WM_APP + 2;
constexpr UINT kConnectionEndedMessage = WM_APP + 3;

constexpr COLORREF kTransparentKey = RGB(1, 2, 3);
constexpr wchar_t kMainClassName[] = L"PitlaneDanmakuLiteWindow";
constexpr wchar_t kOverlayClassName[] = L"PitlaneDanmakuLiteOverlay";

struct OverlayItem {
    std::wstring user_name;
    std::wstring text;
    bool super_chat = false;
    int car_index = 0;
};

struct AppState {
    pitlane::lite::AppSettings settings;
    pitlane::lite::MessagePipeline pipeline{settings};
    HWND room_edit = nullptr;
    HWND cookie_edit = nullptr;
    HWND only_super_chat = nullptr;
    HWND overlay_visible = nullptr;
    HWND connect_button = nullptr;
    HWND log_list = nullptr;
    HWND overlay = nullptr;
    std::shared_ptr<std::atomic_bool> stream_running = std::make_shared<std::atomic_bool>(false);
    std::unique_ptr<Gdiplus::Image> comment_frame;
    std::vector<std::unique_ptr<Gdiplus::Image>> car_images;
    std::vector<OverlayItem> overlay_items;
    int next_message_id = 1;
    int next_car_index = 0;
};

std::filesystem::path executable_path() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }

    buffer.resize(length);
    return buffer;
}

std::filesystem::path find_asset(const std::filesystem::path& relative_path) {
    std::vector<std::filesystem::path> roots;
    roots.push_back(std::filesystem::current_path());

    auto probe = executable_path().parent_path();
    for (int i = 0; i < 5 && !probe.empty(); ++i) {
        roots.push_back(probe);
        probe = probe.parent_path();
    }

    for (const auto& root : roots) {
        auto candidate = root / relative_path;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

std::unique_ptr<Gdiplus::Image> load_image_asset(const std::filesystem::path& relative_path) {
    auto path = find_asset(relative_path);
    if (path.empty()) {
        return nullptr;
    }

    auto image = std::make_unique<Gdiplus::Image>(path.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok) {
        return nullptr;
    }

    return image;
}

std::vector<std::unique_ptr<Gdiplus::Image>> load_car_assets() {
    std::vector<std::unique_ptr<Gdiplus::Image>> images;
    for (int i = 1; i <= 10; ++i) {
        const auto relative = std::filesystem::path(
            std::format(L"assets/cars/car_{:02}.png", i));
        auto image = load_image_asset(relative);
        if (image != nullptr) {
            images.push_back(std::move(image));
        }
    }

    return images;
}

void load_project_fonts() {
    const std::filesystem::path bold = find_asset(L"assets/fonts/HarmonyOS_Sans_SC_Bold.ttf");
    if (!bold.empty()) {
        AddFontResourceExW(bold.c_str(), FR_PRIVATE, nullptr);
    }

    const std::filesystem::path regular = find_asset(L"assets/fonts/HarmonyOS_Sans_SC_Regular.ttf");
    if (!regular.empty()) {
        AddFontResourceExW(regular.c_str(), FR_PRIVATE, nullptr);
    }
}

std::wstring get_window_text(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring value(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        GetWindowTextW(hwnd, value.data(), length + 1);
    }

    return value;
}

void add_label(HWND parent, int x, int y, int width, const wchar_t* text) {
    CreateWindowExW(
        0,
        L"STATIC",
        text,
        WS_CHILD | WS_VISIBLE,
        x,
        y,
        width,
        24,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
}

HWND add_edit(HWND parent, int id, int x, int y, int width, const wchar_t* text = L"") {
    return CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x,
        y,
        width,
        28,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
}

HWND add_button(HWND parent, int id, int x, int y, int width, const wchar_t* text) {
    return CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x,
        y,
        width,
        32,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
}

void append_log(HWND list, const std::wstring& value) {
    const auto index = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    SendMessageW(list, LB_SETTOPINDEX, static_cast<WPARAM>(index), 0);
}

void post_log(HWND hwnd, std::wstring value) {
    PostMessageW(hwnd, kAppendLogMessage, 0, reinterpret_cast<LPARAM>(new std::wstring(std::move(value))));
}

void post_chat_message(HWND hwnd, pitlane::lite::ChatMessage message) {
    PostMessageW(hwnd, kRealtimeChatMessage, 0, reinterpret_cast<LPARAM>(new pitlane::lite::ChatMessage(std::move(message))));
}

void paint_overlay(HWND hwnd, AppState& state) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);

    RECT client{};
    GetClientRect(hwnd, &client);

    HBRUSH transparent_brush = CreateSolidBrush(kTransparentKey);
    FillRect(dc, &client, transparent_brush);
    DeleteObject(transparent_brush);

    SetBkMode(dc, TRANSPARENT);

    HFONT user_font = CreateFontW(
        -70,
        0,
        0,
        0,
        FW_HEAVY,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"HarmonyOS Sans SC");
    HFONT text_font = CreateFontW(
        -62,
        0,
        0,
        0,
        FW_MEDIUM,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"HarmonyOS Sans SC");

    const int margin = 24;
    const int item_height = 258;
    int y = margin;
    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    const auto first = state.overlay_items.size() > 2 ? state.overlay_items.size() - 2 : 0;
    for (std::size_t i = first; i < state.overlay_items.size(); ++i) {
        const auto& item = state.overlay_items[i];
        RECT bubble{
            margin,
            y,
            client.right - margin,
            y + item_height,
        };

        if (state.comment_frame != nullptr) {
            graphics.DrawImage(
                state.comment_frame.get(),
                Gdiplus::Rect(bubble.left, bubble.top, bubble.right - bubble.left, bubble.bottom - bubble.top));
        } else {
            HBRUSH bubble_brush = CreateSolidBrush(item.super_chat ? RGB(238, 156, 47) : RGB(24, 25, 31));
            HPEN border_pen = CreatePen(PS_SOLID, 3, item.super_chat ? RGB(255, 230, 128) : RGB(69, 72, 80));
            auto old_brush = SelectObject(dc, bubble_brush);
            auto old_pen = SelectObject(dc, border_pen);
            RoundRect(dc, bubble.left, bubble.top, bubble.right, bubble.bottom, 24, 24);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
            DeleteObject(border_pen);
            DeleteObject(bubble_brush);
        }

        if (item.super_chat) {
            HBRUSH accent_brush = CreateSolidBrush(RGB(238, 156, 47));
            RECT accent{bubble.left + 22, bubble.top + 34, bubble.right - 22, bubble.top + 46};
            FillRect(dc, &accent, accent_brush);
            DeleteObject(accent_brush);
        }

        if (!state.car_images.empty()) {
            const auto car_index = static_cast<std::size_t>(item.car_index) % state.car_images.size();
            auto& car = state.car_images[car_index];
            if (car != nullptr) {
                const int car_width = 270;
                const int car_height = 104;
                graphics.DrawImage(
                    car.get(),
                    Gdiplus::Rect(bubble.right - car_width - 38, bubble.bottom - car_height - 28, car_width, car_height));
            }
        }

        RECT user_rect{bubble.left + 38, bubble.top + 48, bubble.right - 360, bubble.top + 118};
        RECT text_rect{bubble.left + 38, bubble.top + 134, bubble.right - 330, bubble.bottom - 28};

        auto old_font = SelectObject(dc, user_font);
        SetTextColor(dc, item.super_chat ? RGB(255, 221, 128) : RGB(255, 255, 255));
        DrawTextW(dc, item.user_name.c_str(), -1, &user_rect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SelectObject(dc, text_font);
        SetTextColor(dc, RGB(238, 238, 238));
        DrawTextW(dc, item.text.c_str(), -1, &text_rect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old_font);

        y += item_height + 22;
    }

    DeleteObject(text_font);
    DeleteObject(user_font);
    EndPaint(hwnd, &paint);
}

void push_overlay_item(AppState& state, const pitlane::lite::ChatMessage& message) {
    state.overlay_items.push_back(OverlayItem{
        .user_name = message.user_name,
        .text = message.text,
        .super_chat = message.is_super_chat(),
        .car_index = state.next_car_index++,
    });

    constexpr std::size_t max_items = 5;
    if (state.overlay_items.size() > max_items) {
        state.overlay_items.erase(state.overlay_items.begin(), state.overlay_items.end() - max_items);
    }

    if (state.overlay != nullptr) {
        InvalidateRect(state.overlay, nullptr, FALSE);
    }
}

void sync_settings(AppState& state) {
    state.settings.room_input = get_window_text(state.room_edit);
    state.settings.cookie = get_window_text(state.cookie_edit);
    state.settings.only_super_chat = SendMessageW(state.only_super_chat, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state.settings.normalize();
    state.pipeline.update_settings(state.settings);
}

void enqueue_sample(AppState& state, pitlane::lite::ChatMessageKind kind) {
    sync_settings(state);

    const bool is_super_chat = kind == pitlane::lite::ChatMessageKind::SuperChat;
    pitlane::lite::ChatMessage message;
    message.id = std::format("lite-sample-{}", state.next_message_id++);
    message.user_name = is_super_chat ? L"醒目车手" : L"测试观众";
    message.text = is_super_chat ? L"这是一条醒目留言预览" : L"这是一条普通弹幕预览";
    message.kind = kind;
    if (is_super_chat) {
        message.price = 30.0;
    }

    state.pipeline.enqueue(message);
}

void connect_room_async(
    HWND hwnd,
    pitlane::lite::AppSettings settings,
    std::shared_ptr<std::atomic_bool> running) {
    std::thread([hwnd, settings = std::move(settings), running = std::move(running)]() mutable {
        try {
            pitlane::lite::BilibiliClient client;
            client.stream_messages(
                settings,
                [hwnd](pitlane::lite::ChatMessage message) {
                    post_chat_message(hwnd, std::move(message));
                },
                [hwnd](std::wstring message) {
                    post_log(hwnd, std::move(message));
                },
                running);
        } catch (const std::exception& ex) {
            const std::string message = ex.what();
            const int length = MultiByteToWideChar(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), nullptr, 0);
            std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
            if (length > 0) {
                MultiByteToWideChar(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), wide.data(), length);
            }
            if (running->load()) {
                post_log(hwnd, L"连接预检失败：" + wide);
            }
        }
        running->store(false);
        PostMessageW(hwnd, kConnectionEndedMessage, 0, 0);
    }).detach();
}

void drain_messages(AppState& state) {
    pitlane::lite::ChatMessage message;
    while (state.pipeline.try_dequeue(message)) {
        const auto prefix = message.is_super_chat() ? L"[醒目留言] " : L"[弹幕] ";
        append_log(state.log_list, std::wstring(prefix) + message.user_name + L": " + message.text);
        push_overlay_item(state, message);
    }
}

void sync_overlay_visibility(AppState& state) {
    if (state.overlay == nullptr) {
        return;
    }

    const bool visible = SendMessageW(state.overlay_visible, BM_GETCHECK, 0, 0) == BST_CHECKED;
    ShowWindow(state.overlay, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void layout(HWND hwnd, AppState& state) {
    RECT rect{};
    GetClientRect(hwnd, &rect);

    const int margin = 16;
    const int label_width = 86;
    const int edit_x = margin + label_width;
    const int edit_width = rect.right - edit_x - margin;

    MoveWindow(state.room_edit, edit_x, 16, edit_width, 28, TRUE);
    MoveWindow(state.cookie_edit, edit_x, 52, edit_width, 28, TRUE);
    MoveWindow(state.only_super_chat, margin, 92, 180, 28, TRUE);
    MoveWindow(state.overlay_visible, 220, 92, 160, 28, TRUE);
    MoveWindow(state.connect_button, margin, 124, 128, 32, TRUE);

    const int list_top = 164;
    MoveWindow(state.log_list, margin, list_top, rect.right - margin * 2, rect.bottom - list_top - margin, TRUE);
}

LRESULT CALLBACK overlay_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }

    case WM_PAINT:
        if (state != nullptr) {
            paint_overlay(hwnd, *state);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

HWND create_overlay_window(HWND owner, AppState& state) {
    HWND overlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        kOverlayClassName,
        L"Pitlane Danmaku Lite Overlay",
        WS_POPUP,
        80,
        80,
        1180,
        680,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    if (overlay != nullptr) {
        SetLayeredWindowAttributes(overlay, kTransparentKey, 245, LWA_COLORKEY | LWA_ALPHA);
    }

    return overlay;
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        auto owned_state = std::make_unique<AppState>();

        add_label(hwnd, 16, 20, 86, L"直播间");
        add_label(hwnd, 16, 56, 86, L"Cookie");

        owned_state->room_edit = add_edit(hwnd, kRoomEditId, 102, 16, 420, L"");
        owned_state->cookie_edit = add_edit(hwnd, kCookieEditId, 102, 52, 420, L"");
        owned_state->only_super_chat = CreateWindowExW(
            0,
            L"BUTTON",
            L"仅显示醒目留言",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            16,
            92,
            180,
            28,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOnlySuperChatId)),
            GetModuleHandleW(nullptr),
            nullptr);

        owned_state->overlay_visible = CreateWindowExW(
            0,
            L"BUTTON",
            L"显示叠加预览",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            220,
            92,
            160,
            28,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverlayVisibleId)),
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(owned_state->overlay_visible, BM_SETCHECK, BST_CHECKED, 0);

        add_button(hwnd, kAddCommentId, 400, 90, 128, L"添加测试弹幕");
        add_button(hwnd, kAddSuperChatId, 540, 90, 148, L"添加醒目留言");
        owned_state->connect_button = add_button(hwnd, kConnectRoomId, 16, 124, 128, L"连接直播间");

        owned_state->log_list = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            16,
            140,
            520,
            240,
            hwnd,
            nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
        owned_state->comment_frame = load_image_asset(L"assets/comment-box/comment_frame.png");
        owned_state->car_images = load_car_assets();

        auto* raw_state = owned_state.get();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owned_state.release()));
        raw_state->overlay = create_overlay_window(hwnd, *raw_state);
        sync_overlay_visibility(*raw_state);
        SetTimer(hwnd, kDrainTimerId, 150, nullptr);
        return 0;
    }

    case WM_SIZE:
        if (state != nullptr) {
            layout(hwnd, *state);
        }
        return 0;

    case WM_COMMAND:
        if (state == nullptr) {
            break;
        }

        switch (LOWORD(w_param)) {
        case kOnlySuperChatId:
            sync_settings(*state);
            append_log(state->log_list, state->settings.only_super_chat ? L"已开启：仅显示醒目留言" : L"已关闭：仅显示醒目留言");
            return 0;

        case kOverlayVisibleId:
            sync_overlay_visibility(*state);
            return 0;

        case kConnectRoomId:
            if (state->stream_running->load()) {
                state->stream_running->store(false);
                SetWindowTextW(state->connect_button, L"连接直播间");
                append_log(state->log_list, L"正在断开直播间...");
                return 0;
            }
            sync_settings(*state);
            state->stream_running->store(true);
            SetWindowTextW(state->connect_button, L"断开直播间");
            append_log(state->log_list, L"正在连接 B站直播间 API...");
            connect_room_async(hwnd, state->settings, state->stream_running);
            return 0;

        case kAddCommentId:
            enqueue_sample(*state, pitlane::lite::ChatMessageKind::Comment);
            drain_messages(*state);
            return 0;

        case kAddSuperChatId:
            enqueue_sample(*state, pitlane::lite::ChatMessageKind::SuperChat);
            drain_messages(*state);
            return 0;
        }
        break;

    case kAppendLogMessage:
        if (state != nullptr) {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(l_param));
            append_log(state->log_list, *text);
        }
        return 0;

    case kRealtimeChatMessage:
        if (state != nullptr) {
            std::unique_ptr<pitlane::lite::ChatMessage> chat_message(reinterpret_cast<pitlane::lite::ChatMessage*>(l_param));
            state->pipeline.enqueue(*chat_message);
            drain_messages(*state);
        }
        return 0;

    case kConnectionEndedMessage:
        if (state != nullptr) {
            state->stream_running->store(false);
            SetWindowTextW(state->connect_button, L"连接直播间");
            append_log(state->log_list, L"实时连接已结束。");
        }
        return 0;

    case WM_TIMER:
        if (state != nullptr && w_param == kDrainTimerId) {
            drain_messages(*state);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, kDrainTimerId);
        if (state != nullptr) {
            state->stream_running->store(false);
        }
        if (state != nullptr && state->overlay != nullptr) {
            DestroyWindow(state->overlay);
            state->overlay = nullptr;
        }
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    Gdiplus::GdiplusStartupInput gdiplus_startup_input;
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_startup_input, nullptr) != Gdiplus::Ok) {
        return 1;
    }
    load_project_fonts();

    INITCOMMONCONTROLSEX controls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_STANDARD_CLASSES,
    };
    InitCommonControlsEx(&controls);

    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = window_proc,
        .hInstance = instance,
        .hIcon = LoadIconW(nullptr, IDI_APPLICATION),
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kMainClassName,
        .hIconSm = LoadIconW(nullptr, IDI_APPLICATION),
    };

    if (RegisterClassExW(&window_class) == 0) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    WNDCLASSEXW overlay_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = overlay_proc,
        .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = CreateSolidBrush(kTransparentKey),
        .lpszClassName = kOverlayClassName,
    };

    if (RegisterClassExW(&overlay_class) == 0) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kMainClassName,
        L"Pitlane Danmaku Lite",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        760,
        520,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (hwnd == nullptr) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    ShowWindow(hwnd, show_command == SW_HIDE ? SW_SHOWNORMAL : show_command);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    const int exit_code = static_cast<int>(message.wParam);
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return exit_code;
}
