#pragma once

#include "MainWindow.xaml.g.h"
#include "DashboardReport.h"

#include <string>

namespace winrt::ISmyPCokWinUI::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void RunButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void LoadScenarios();
        std::string SelectedScenario();
        std::string SelectedModules();
        void SetState(std::wstring const& state, std::wstring const& detail, bool running);
        void ShowError(std::wstring const& detail, std::wstring const& rawJson = {});
        void RenderReport(ispcok::dashboard::DashboardReport const& report, std::wstring const& rawJson);
        void ClearReport();

        bool m_capiAvailable = false;
    };
}

namespace winrt::ISmyPCokWinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
