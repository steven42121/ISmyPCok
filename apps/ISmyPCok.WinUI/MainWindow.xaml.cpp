#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <string>
#include <thread>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    const wchar_t kCapiDll[] = L"ispcok_capi.dll";
    const char kEntryVersion[] = "ispcok_version";
    const char kEntryRunModules[] = "ispcok_run_modules";
    const char kEntryFreeString[] = "ispcok_free_string";

    std::wstring CapiPath()
    {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if ((length == 0) || (length >= path.size()))
            return kCapiDll;
        path.resize(length);
        const std::size_t separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return kCapiDll;
        path.resize(separator + 1);
        path += kCapiDll;
        return path;
    }

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
            const std::wstring path = CapiPath();
            module = LoadLibraryW(path.c_str());
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
        BuildContent();
        Title(L"ISmyPCok");

        CapApi api;
        if (!api.Load())
        {
            version_text.Text(L"capi library not found (ispcok_capi.dll)");
            return;
        }
        const char* raw = api.version();
        version_text.Text(raw != nullptr ? winrt::to_hstring(raw) : L"unknown");

        LoadScenarios();
        run_button.IsEnabled(true);
    }

    void MainWindow::BuildContent()
    {
        using namespace Microsoft::UI::Xaml::Controls;

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
        run_button.Click({this, &MainWindow::RunButton_Click});
        action_panel.Children().Append(run_button);
        busy_ring = ProgressRing();
        busy_ring.IsActive(false);
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

        Content(root);
    }

    void MainWindow::LoadScenarios()
    {
        scenario_combo.Items().Append(box_value(L"game_engine"));
        scenario_combo.Items().Append(box_value(L"maa"));
        scenario_combo.Items().Append(box_value(L"llm_infer_server"));
        scenario_combo.SelectedIndex(0);
    }

    std::string MainWindow::SelectedScenario()
    {
        const auto selection = scenario_combo.SelectedItem();
        if (selection == nullptr)
            return std::string();
        const auto value = winrt::unbox_value_or<winrt::hstring>(selection, L"");
        return winrt::to_string(value);
    }

    std::string MainWindow::SelectedModules()
    {
        const auto text = modules_box.Text();
        return winrt::to_string(text);
    }

    void MainWindow::RunButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        run_button.IsEnabled(false);
        busy_ring.IsActive(true);
        result_box.Text(L"Running...");

        const std::string scenario = SelectedScenario();
        const std::string modules = SelectedModules();
        const auto dispatcher = DispatcherQueue();
        const auto weakThis = get_weak();

        std::thread([dispatcher, weakThis, scenario, modules]()
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
            dispatcher.TryEnqueue([weakThis, result]()
            {
                if (auto strongThis = weakThis.get())
                {
                    auto self = strongThis.get();
                    self->result_box.Text(result);
                    self->busy_ring.IsActive(false);
                    self->run_button.IsEnabled(true);
                }
            });
        }).detach();
    }
}
