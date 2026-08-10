#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "App.g.cpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ISmyPCokWinUI::implementation
{
    App::App()
    {
        Initialize();
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        window = make<MainWindow>();
        window.Activate();
    }
}
