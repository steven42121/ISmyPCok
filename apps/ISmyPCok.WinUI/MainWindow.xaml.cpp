#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"

#include <string>
#include <thread>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    const wchar_t kCapiDll[] = L"ispcok_capi.dll";
    const wchar_t kEntryVersion[] = L"ispcok_version";
    const wchar_t kEntryRunModules[] = L"ispcok_run_modules";
    const wchar_t kEntryFreeString[] = L"ispcok_free_string";

    using VersionFn = const char* (*)();
    using RunModulesFn = int (*)(const char*, const char*, const char*, char**);
    using FreeStringFn = void (*)(char*);

    struct CapApi
    {
        HMODULE module = nullptr;
        VersionFn version = nullptr;
        RunModulesFn run_modules = nullptr;
        FreeStringFn free_string = nullptr;

        bool Load()
        {
            module = LoadLibraryW(kCapiDll);
            if (module == nullptr)
                return false;
            version = reinterpret_cast<VersionFn>(GetProcAddress(module, kEntryVersion));
            run_modules = reinterpret_cast<RunModulesFn>(GetProcAddress(module, kEntryRunModules));
            free_string = reinterpret_cast<FreeStringFn>(GetProcAddress(module, kEntryFreeString));
            return (version != nullptr) && (run_modules != nullptr) && (free_string != nullptr);
        }

        ~CapApi()
        {
            if (module != nullptr)
                FreeLibrary(module);
        }
    };
}

namespace winrt::ISmyPCokWinUI::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"ISmyPCok");

        CapApi api;
        if (!api.Load())
        {
            VersionText().Text(L"capi library not found (ispcok_capi.dll)");
            return;
        }
        const char* raw = api.version();
        VersionText().Text(raw != nullptr ? winrt::to_hstring(raw) : L"unknown");

        LoadScenarios();
        RunButton().IsEnabled(true);
    }

    void MainWindow::LoadScenarios()
    {
        ScenarioCombo().Items().Append(box_value(L"game_engine"));
        ScenarioCombo().Items().Append(box_value(L"maa"));
        ScenarioCombo().Items().Append(box_value(L"llm_infer_server"));
        ScenarioCombo().SelectedIndex(0);
    }

    std::string MainWindow::SelectedScenario() const
    {
        const auto selection = ScenarioCombo().SelectedItem();
        if (selection == nullptr)
            return std::string();
        const auto value = winrt::unbox_value_or<winrt::hstring>(selection, L"");
        return winrt::to_string(value);
    }

    std::string MainWindow::SelectedModules() const
    {
        const auto text = ModulesBox().Text();
        return winrt::to_string(text);
    }

    void MainWindow::RunButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RunButton().IsEnabled(false);
        BusyRing().IsActive(true);
        ResultBox().Text(L"Running...");

        const std::string scenario = SelectedScenario();
        const std::string modules = SelectedModules();
        const auto dispatcher = DispatcherQueue();

        std::thread([dispatcher, scenario, modules, this]()
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
                {
                    output = "ispcok_run_modules failed with code " + std::to_string(rc);
                }
                else if (json != nullptr)
                {
                    output = json;
                    api.free_string(json);
                }
                else
                {
                    output = "ispcok_run_modules returned empty result";
                }
            }

            const auto result = winrt::to_hstring(output);
            dispatcher.TryEnqueue([this, result]()
            {
                ResultBox().Text(result);
                BusyRing().IsActive(false);
                RunButton().IsEnabled(true);
            });
        }).detach();
    }
}
