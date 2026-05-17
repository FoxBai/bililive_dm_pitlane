#include "pch.h"
#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <algorithm>
#include <cmath>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace winrt::PitlaneDanmaku::Lite::WinUI::implementation
{
    namespace
    {
        hstring to_hstring(const std::wstring& value)
        {
            return hstring{ value.c_str(), static_cast<uint32_t>(value.size()) };
        }

        std::wstring to_wstring(hstring const& value)
        {
            return std::wstring{ value.c_str(), value.size() };
        }

        int number_value(NumberBox const& box, int fallback)
        {
            const double value = box.Value();
            if (!std::isfinite(value)) {
                return fallback;
            }
            return static_cast<int>(std::lround(value));
        }

        std::wstring utf8_to_wide(const char* value)
        {
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
    }

    MainWindow::MainWindow()
    {
        Title(L"Pitlane Danmaku Lite");
        ApplySettingsToUi(runtime_.settings());
        AppendLog(L"WinUI/C++ 外壳已启动。");
        AppendLog(hstring{ L"配置文件：" } + to_hstring(runtime_.settings_path()));
    }

    void MainWindow::ConnectButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        runtime_.update_settings(CollectSettingsFromUi());
        if (!connected_ && RoomInput().Text().empty())
        {
            HealthInfo().Severity(InfoBarSeverity::Warning);
            HealthInfo().Title(L"缺少直播间");
            HealthInfo().Message(L"请输入房间号或直播间链接后再连接。");
            HealthInfo().IsOpen(true);
            AppendLog(L"连接被拦截：直播间输入为空。");
            return;
        }

        SetConnected(!connected_);
    }

    void MainWindow::SaveSettingsButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        runtime_.update_settings(CollectSettingsFromUi());
        if (runtime_.obs_running()) {
            RestartObsService();
        }

        AppendLog(L"设置已保存。");
        HealthInfo().Severity(InfoBarSeverity::Success);
        HealthInfo().Title(L"设置已确认");
        HealthInfo().Message(L"Cookie、OBS 端口和显示参数已写入 lite 设置模型。");
        HealthInfo().IsOpen(true);
    }

    void MainWindow::RestartObsButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        runtime_.update_settings(CollectSettingsFromUi());
        RestartObsService();
    }

    void MainWindow::AddTestCommentButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        runtime_.update_settings(CollectSettingsFromUi());
        auto message = runtime_.enqueue_preview(L"测试观众", L"这是一条测试弹幕，外壳会把它推给预览列表。", pitlane::lite::ChatMessageKind::Comment);
        if (!message) {
            AppendLog(L"普通测试弹幕已被当前过滤规则拦截。");
            return;
        }
        AddPreviewMessage(*message);
    }

    void MainWindow::AddSuperChatButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        runtime_.update_settings(CollectSettingsFromUi());
        auto message = runtime_.enqueue_preview(L"醒目用户", L"SuperChat 测试：按钮链路、过滤和 OBS 广播已就位。", pitlane::lite::ChatMessageKind::SuperChat);
        if (message) {
            AddPreviewMessage(*message);
        }
    }

    void MainWindow::OnlySuperChatSwitch_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (OnlySuperChatSwitch().IsOn())
        {
            AppendLog(L"已开启只显示 SuperChat / 醒目留言。");
        }
        else
        {
            AppendLog(L"已关闭只显示 SuperChat / 醒目留言。");
        }
    }

    pitlane::lite::AppSettings MainWindow::CollectSettingsFromUi()
    {
        auto settings = runtime_.settings();
        settings.room_input = to_wstring(RoomInput().Text());
        settings.cookie = to_wstring(CookieInput().Text());
        settings.obs_port = number_value(ObsPortInput(), settings.obs_port);
        settings.min_visible_items = number_value(VisibleItemsInput(), settings.min_visible_items);
        settings.max_stage_width = number_value(StageWidthInput(), settings.max_stage_width);
        settings.only_super_chat = OnlySuperChatSwitch().IsOn();
        settings.normalize();
        return settings;
    }

    void MainWindow::ApplySettingsToUi(const pitlane::lite::AppSettings& settings)
    {
        RoomInput().Text(to_hstring(settings.room_input));
        CookieInput().Text(to_hstring(settings.cookie));
        ObsPortInput().Value(settings.obs_port);
        VisibleItemsInput().Value(settings.min_visible_items);
        StageWidthInput().Value(settings.max_stage_width);
        OnlySuperChatSwitch().IsOn(settings.only_super_chat);
    }

    void MainWindow::AppendLog(hstring const& message)
    {
        TextBlock item;
        item.Text(message);
        item.TextWrapping(TextWrapping::Wrap);
        item.FontSize(13);
        item.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
        LogList().Items().Append(item);
        LogList().ScrollIntoView(item);
    }

    void MainWindow::AddPreviewMessage(const pitlane::lite::ChatMessage& message)
    {
        preview_index_ += 1;
        const bool super_chat = message.is_super_chat();

        StackPanel row;
        row.Spacing(4);
        row.Margin(ThicknessHelper::FromLengths(0, 0, 0, 12));

        TextBlock title;
        title.Text(super_chat ? hstring{ L"醒目留言  " } + to_hstring(message.user_name) : to_hstring(message.user_name));
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.FontSize(14);
        title.Foreground(SolidColorBrush(super_chat ? Windows::UI::Color{ 255, 126, 81, 27 } : Windows::UI::Color{ 255, 22, 27, 34 }));

        TextBlock body;
        body.Text(to_hstring(message.text));
        body.TextWrapping(TextWrapping::Wrap);
        body.FontSize(14);
        body.Foreground(SolidColorBrush(Windows::UI::Color{ 255, 63, 74, 86 }));

        row.Children().Append(title);
        row.Children().Append(body);
        PreviewList().Items().Append(row);
        PreviewList().ScrollIntoView(row);

        AppendLog(hstring{ super_chat ? L"添加醒目留言预览 #" : L"添加普通弹幕预览 #" } + winrt::to_hstring(preview_index_));
    }

    void MainWindow::RestartObsService()
    {
        try {
            runtime_.start_obs([this](std::wstring message) {
                auto dispatcher = DispatcherQueue();
                dispatcher.TryEnqueue([this, message = std::move(message)]() {
                    AppendLog(to_hstring(message));
                });
            });
            SetObsRunning(true);
            AppendLog(hstring{ L"OBS 本地服务已启动：" } + to_hstring(runtime_.overlay_url()));
            HealthInfo().Severity(InfoBarSeverity::Success);
            HealthInfo().Title(L"OBS 服务已启动");
            HealthInfo().Message(hstring{ L"浏览器源地址：" } + to_hstring(runtime_.overlay_url()));
            HealthInfo().IsOpen(true);
        } catch (const std::exception& ex) {
            SetObsRunning(false);
            const auto message = utf8_to_wide(ex.what());
            AppendLog(hstring{ L"OBS 本地服务启动失败：" } + to_hstring(message));
            HealthInfo().Severity(InfoBarSeverity::Error);
            HealthInfo().Title(L"OBS 服务启动失败");
            HealthInfo().Message(to_hstring(message));
            HealthInfo().IsOpen(true);
        }
    }

    void MainWindow::SetConnected(bool connected)
    {
        connected_ = connected;
        ConnectButton().Content(box_value(connected ? L"断开直播间" : L"连接直播间"));
        LiveRing().IsActive(connected);
        LiveStateText().Text(connected ? L"接收中" : L"待连接");
        HealthInfo().Severity(connected ? InfoBarSeverity::Success : InfoBarSeverity::Informational);
        HealthInfo().Title(connected ? L"直播间已连接" : L"直播间已断开");
        HealthInfo().Message(connected ? L"外壳状态已进入接收模式，下一步接入 BilibiliClient。"
                                       : L"已停止接收，OBS 预览和本地服务设置保留。");
        HealthInfo().IsOpen(true);
        AppendLog(connected ? hstring{ L"连接直播间：" } + RoomInput().Text() : hstring{ L"已断开直播间。" });
    }

    void MainWindow::SetObsRunning(bool running)
    {
        RestartObsButton().Content(box_value(running ? L"重启 OBS 服务" : L"启动 OBS 服务"));
    }
}
