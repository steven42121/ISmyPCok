#pragma once

#include "MainWindow.xaml.g.h"

#include <string>

namespace winrt::ISmyPCokWinUI::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void RunButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void BuildContent();
        void LoadScenarios();
        std::string SelectedScenario();
        std::string SelectedModules();

        winrt::Microsoft::UI::Xaml::Controls::TextBlock version_text{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ComboBox scenario_combo{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBox modules_box{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button run_button{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ProgressRing busy_ring{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBox result_box{ nullptr };
    };
}

namespace winrt::ISmyPCokWinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
