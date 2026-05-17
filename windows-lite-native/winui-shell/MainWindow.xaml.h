#pragma once

#include "LiteRuntime.h"
#include "MainWindow.g.h"

namespace winrt::PitlaneDanmaku::Lite::WinUI::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void ConnectButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SaveSettingsButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RestartObsButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddTestCommentButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddSuperChatButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnlySuperChatSwitch_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        pitlane::lite::winui::LiteRuntime runtime_;
        bool connected_ = false;
        int preview_index_ = 0;

        pitlane::lite::AppSettings CollectSettingsFromUi();
        void ApplySettingsToUi(const pitlane::lite::AppSettings& settings);
        void AppendLog(winrt::hstring const& message);
        void AddPreviewMessage(const pitlane::lite::ChatMessage& message);
        void RestartObsService();
        void SetConnected(bool connected);
        void SetObsRunning(bool running);
    };
}

namespace winrt::PitlaneDanmaku::Lite::WinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
