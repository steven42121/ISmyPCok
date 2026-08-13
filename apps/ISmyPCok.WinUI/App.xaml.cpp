#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <exception>
#include <memory>
#include <string>
#include <thread>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    const wchar_t kCapiDll[] = L"ispcok_capi.dll";
    using VersionFn = const char* (*)();
    using RunModulesFn = int (*)(const char*, const char*, const char*, char**);
    using FreeStringFn = void (*)(char*);

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

    std::wstring CapiPath()
    {
        const std::wstring directory = ExecutableDirectory();
        return directory.empty() ? kCapiDll : directory + L"\\" + kCapiDll;
    }

    struct CapApi
    {
        HMODULE module = nullptr;
        VersionFn version = nullptr;
        RunModulesFn run_modules = nullptr;
        FreeStringFn free_string = nullptr;

        bool Load()
        {
            module = LoadLibraryW(CapiPath().c_str());
            if (module == nullptr)
                return false;
            version = reinterpret_cast<VersionFn>(GetProcAddress(module, "ispcok_version"));
            run_modules = reinterpret_cast<RunModulesFn>(GetProcAddress(module, "ispcok_run_modules"));
            free_string = reinterpret_cast<FreeStringFn>(GetProcAddress(module, "ispcok_free_string"));
            return (version != nullptr) && (run_modules != nullptr) && (free_string != nullptr);
        }

        ~CapApi()
        {
            if (module != nullptr)
                FreeLibrary(module);
        }
    };

    struct RuntimeApp : Microsoft::UI::Xaml::ApplicationT<RuntimeApp>
    {
        RuntimeApp()
        {
            UnhandledException([](auto const&, UnhandledExceptionEventArgs const& args)
            {
                ReportStartupFailure(std::wstring(L"Unhandled XAML exception:\r\n") + args.Message().c_str());
            });
            Resources().MergedDictionaries().Append(Microsoft::UI::Xaml::Controls::XamlControlsResources());
        }

        void OnLaunched(LaunchActivatedEventArgs const&)
        {
            BuildContent();
            window.Activated([](auto const&, WindowActivatedEventArgs const& args)
            {
                if (args.WindowActivationState() != WindowActivationState::Deactivated)
                    WriteSmokeReadyMarker();
            });
            window.Activate();
        }

    private:
        void BuildContent()
        {
            using namespace Microsoft::UI::Xaml::Controls;

            window = Window();
            window.Title(L"ISmyPCok");

            Grid root;
            root.Padding(Thickness{24.0, 24.0, 24.0, 24.0});
            root.RowSpacing(12.0);
            for (int index = 0; index < 4; ++index)
            {
                RowDefinition row;
                row.Height(GridLengthHelper::Auto());
                root.RowDefinitions().Append(row);
            }
            RowDefinition result_row;
            result_row.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            root.RowDefinitions().Append(result_row);

            StackPanel title_panel;
            title_panel.Orientation(Orientation::Horizontal);
            title_panel.Spacing(12.0);
            TextBlock title_text;
            title_text.Text(L"ISmyPCok");
            title_text.FontSize(28.0);
            title_panel.Children().Append(title_text);
            version_text = TextBlock();
            version_text.VerticalAlignment(VerticalAlignment::Bottom);
            title_panel.Children().Append(version_text);
            root.Children().Append(title_panel);

            StackPanel options_panel;
            options_panel.Orientation(Orientation::Horizontal);
            options_panel.Spacing(12.0);
            Grid::SetRow(options_panel, 1);
            scenario_combo = ComboBox();
            scenario_combo.Header(box_value(L"Scenario"));
            scenario_combo.MinWidth(200.0);
            for (auto const& scenario : {L"game_engine", L"maa", L"llm_infer_server"})
                scenario_combo.Items().Append(box_value(scenario));
            scenario_combo.SelectedIndex(0);
            options_panel.Children().Append(scenario_combo);
            modules_box = TextBox();
            modules_box.Header(box_value(L"Modules (comma separated, empty = all)"));
            modules_box.PlaceholderText(L"cpu_fp32,memory_bw,net_bw,net_rtt");
            modules_box.Width(360.0);
            options_panel.Children().Append(modules_box);
            root.Children().Append(options_panel);

            StackPanel action_panel;
            action_panel.Orientation(Orientation::Horizontal);
            action_panel.Spacing(12.0);
            Grid::SetRow(action_panel, 2);
            run_button = Button();
            run_button.Content(box_value(L"Run"));
            run_button.IsEnabled(false);
            run_button.Click([this](auto const&, auto const&) { RunBenchmark(); });
            action_panel.Children().Append(run_button);
            busy_ring = ProgressRing();
            busy_ring.Width(24.0);
            busy_ring.Height(24.0);
            action_panel.Children().Append(busy_ring);
            root.Children().Append(action_panel);

            TextBlock result_title;
            result_title.Text(L"Result");
            result_title.FontSize(20.0);
            Grid::SetRow(result_title, 3);
            root.Children().Append(result_title);
            result_box = TextBox();
            result_box.IsReadOnly(true);
            result_box.AcceptsReturn(true);
            result_box.TextWrapping(TextWrapping::Wrap);
            Grid::SetRow(result_box, 4);
            root.Children().Append(result_box);
            window.Content(root);

            CapApi api;
            if (!api.Load())
            {
                version_text.Text(L"capi library not found (ispcok_capi.dll)");
                return;
            }
            const char* raw = api.version();
            version_text.Text(raw != nullptr ? winrt::to_hstring(raw) : L"unknown");
            run_button.IsEnabled(true);
        }

        void RunBenchmark()
        {
            run_button.IsEnabled(false);
            busy_ring.IsActive(true);
            result_box.Text(L"Running...");

            const auto selected = scenario_combo.SelectedItem();
            const std::string scenario = selected == nullptr
                ? std::string()
                : winrt::to_string(unbox_value_or<hstring>(selected, L""));
            const std::string modules = winrt::to_string(modules_box.Text());
            const auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

            std::thread([this, dispatcher, scenario, modules]()
            {
                std::string output;
                CapApi api;
                if (!api.Load())
                {
                    output = "ispcok_capi.dll not found next to the executable";
                }
                else
                {
                    char* json = nullptr;
                    const int rc = api.run_modules(
                        modules.empty() ? nullptr : modules.c_str(),
                        scenario.empty() ? nullptr : scenario.c_str(),
                        nullptr,
                        &json);
                    if (rc != 0)
                        output = "ispcok_run_modules failed with code " + std::to_string(rc);
                    else if (json != nullptr)
                    {
                        output = json;
                        api.free_string(json);
                    }
                    else
                        output = "ispcok_run_modules returned empty result";
                }

                const auto result = winrt::to_hstring(output);
                dispatcher.TryEnqueue([this, result]()
                {
                    result_box.Text(result);
                    busy_ring.IsActive(false);
                    run_button.IsEnabled(true);
                });
            }).detach();
        }

        Window window{nullptr};
        Microsoft::UI::Xaml::Controls::TextBlock version_text{nullptr};
        Microsoft::UI::Xaml::Controls::ComboBox scenario_combo{nullptr};
        Microsoft::UI::Xaml::Controls::TextBox modules_box{nullptr};
        Microsoft::UI::Xaml::Controls::Button run_button{nullptr};
        Microsoft::UI::Xaml::Controls::ProgressRing busy_ring{nullptr};
        Microsoft::UI::Xaml::Controls::TextBox result_box{nullptr};
    };
}

namespace winrt::ISmyPCokWinUI::implementation
{
    App::App()
    {
        UnhandledException([](auto const&, UnhandledExceptionEventArgs const& args)
        {
            ReportStartupFailure(std::wstring(L"Unhandled XAML exception:\r\n") + args.Message().c_str());
        });
        Resources().MergedDictionaries().Append(Microsoft::UI::Xaml::Controls::XamlControlsResources());
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
            winrt::make<RuntimeApp>();
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
