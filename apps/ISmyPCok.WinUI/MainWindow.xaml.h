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

// The XAML compiler splits page codegen into declarations (.xaml.g.h, pulled in
// via MainWindow.g.h) and definitions (.xaml.g.hpp). Nothing in the WindowsAppSDK
// targets includes the latter, so InitializeComponent/Connect/GetBindingConnector
// are never instantiated and the link fails with LNK2019.
#if __has_include("XamlBindingInfo.xaml.g.hpp")
#include "XamlBindingInfo.xaml.g.hpp"
#endif
#if __has_include("MainWindow.xaml.g.hpp")
#include "MainWindow.xaml.g.hpp"
#endif
