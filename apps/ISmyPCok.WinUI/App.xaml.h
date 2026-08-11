#pragma once

#include "App.xaml.g.h"

namespace winrt::ISmyPCokWinUI::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };
    };
}

namespace winrt::ISmyPCokWinUI::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
