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
        void LoadScenarios();
        std::string SelectedScenario();
        std::string SelectedModules();
    };
}

namespace winrt::ISmyPCokWinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
