#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <exception>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    std::wstring ExecutableDirectory()
    {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if ((length == 0) || (length >= path.size()))
            return std::wstring();
        path.resize(length);
        const std::size_t separator = path.find_last_of(L"\\/");
        return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
    }

    void WriteTextFile(const std::wstring& path, const std::wstring& text)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        const wchar_t bom = 0xFEFF;
        WriteFile(file, &bom, sizeof(bom), &written, nullptr);
        const DWORD byte_count = static_cast<DWORD>(text.size() * sizeof(wchar_t));
        WriteFile(file, text.data(), byte_count, &written, nullptr);
        CloseHandle(file);
    }

    void ReportStartupFailure(const std::wstring& message)
    {
        const std::wstring directory = ExecutableDirectory();
        if (!directory.empty())
            WriteTextFile(directory + L"\\ISmyPCokWinUI-startup.log", message);

        wchar_t smoke_path[2]{};
        if (GetEnvironmentVariableW(L"ISPCOK_WINUI_SMOKE_READY_FILE", smoke_path, 2) > 0)
            ExitProcess(1);
        MessageBoxW(nullptr, message.c_str(), L"ISmyPCok startup failed", MB_OK | MB_ICONERROR);
    }

    void WriteSmokeReadyMarker()
    {
        std::wstring path(32768, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"ISPCOK_WINUI_SMOKE_READY_FILE",
                                                     path.data(), static_cast<DWORD>(path.size()));
        if ((length == 0) || (length >= path.size()))
            return;
        path.resize(length);
        WriteTextFile(path, L"ready");
    }
}

namespace winrt::ISmyPCokWinUI::implementation
{
    App::App()
    {
        UnhandledException([](auto const&, UnhandledExceptionEventArgs const& args)
        {
            ReportStartupFailure(std::wstring(L"Unhandled XAML exception:\r\n") + args.Message().c_str());
        });
        InitializeComponent();
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        window = make<MainWindow>();
        window.Activated([](auto const&, WindowActivatedEventArgs const& args)
        {
            if (args.WindowActivationState() != WindowActivationState::Deactivated)
                WriteSmokeReadyMarker();
        });
        window.Activate();
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&)
        {
            winrt::make<winrt::ISmyPCokWinUI::implementation::App>();
        });
        return 0;
    }
    catch (winrt::hresult_error const& error)
    {
        wchar_t code[32]{};
        swprintf_s(code, L"0x%08X", static_cast<unsigned int>(error.code().value));
        ReportStartupFailure(L"WinUI initialization failed (" + std::wstring(code) + L"):\r\n" + error.message().c_str());
    }
    catch (std::exception const& error)
    {
        ReportStartupFailure(std::wstring(L"WinUI initialization failed:\r\n") + winrt::to_hstring(error.what()).c_str());
    }
    catch (...)
    {
        ReportStartupFailure(L"WinUI initialization failed with an unknown error.");
    }
    return 1;
}
