#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

#include <winrt/Windows.UI.Text.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace
{
    const wchar_t kCapiDll[] = L"ispcok_capi.dll";
    const char kEntryVersion[] = "ispcok_version";
    const char kEntryRunModules[] = "ispcok_run_modules";
    const char kEntryFreeString[] = "ispcok_free_string";

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

    hstring FormatScore(double value)
    {
        std::wostringstream output;
        output << std::fixed << std::setprecision(2) << value;
        return hstring(output.str());
    }

    hstring Join(std::vector<std::wstring> const& values)
    {
        if (values.empty())
            return L"No bottlenecks reported";

        std::wstring result;
        for (auto const& value : values)
        {
            if (!result.empty())
                result += L"  |  ";
            result += value;
        }
        return hstring(result);
    }

    TextBlock MakeText(hstring const& text, double size = 14.0)
    {
        TextBlock block;
        block.Text(text);
        block.FontSize(size);
        block.TextWrapping(TextWrapping::Wrap);
        return block;
    }

    Border MakeModuleCard(ispcok::dashboard::DashboardModule const& module)
    {
        Border card;
        card.Width(320);
        card.Margin(Thickness{ 0, 0, 12, 12 });
        card.Padding(Thickness{ 18 });
        card.CornerRadius(CornerRadius{ 8 });
        card.BorderThickness(Thickness{ 1 });
        card.Background(Application::Current().Resources().Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Media::Brush>());
        card.BorderBrush(Application::Current().Resources().Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Media::Brush>());

        StackPanel content;
        content.Spacing(7);

        Grid heading;
        heading.ColumnDefinitions().Append(ColumnDefinition());
        ColumnDefinition scoreColumn;
        scoreColumn.Width(GridLengthHelper::Auto());
        heading.ColumnDefinitions().Append(scoreColumn);

        auto id = MakeText(hstring(module.id), 18);
        id.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        heading.Children().Append(id);

        auto score = MakeText(FormatScore(module.score), 22);
        score.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        score.HorizontalAlignment(HorizontalAlignment::Right);
        Grid::SetColumn(score, 1);
        heading.Children().Append(score);
        content.Children().Append(heading);

        std::wstring metadata = module.category + L"  |  " + module.status + L"  |  ";
        metadata += module.plugin ? L"plugin" : L"built-in";
        auto metadataText = MakeText(hstring(metadata), 12);
        metadataText.Opacity(0.72);
        content.Children().Append(metadataText);

        if (!module.message.empty())
            content.Children().Append(MakeText(hstring(module.message)));

        if (!module.metrics.empty())
        {
            auto metricsTitle = MakeText(L"METRICS", 11);
            metricsTitle.Opacity(0.65);
            metricsTitle.Margin(Thickness{ 0, 5, 0, 0 });
            content.Children().Append(metricsTitle);
            for (auto const& metric : module.metrics)
            {
                content.Children().Append(MakeText(hstring(metric.name + L": " + FormatScore(metric.value).c_str()), 13));
            }
        }

        card.Child(content);
        return card;
    }
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
            SetState(L"Initialization error", L"ispcok_capi.dll was not found next to the executable.", false);
            return;
        }
        const char* raw = api.version();
        VersionText().Text(raw != nullptr ? winrt::to_hstring(raw) : L"unknown");

        LoadScenarios();
        m_capiAvailable = true;
        SetState(L"Ready", L"Choose a scenario and run the benchmark.", false);
    }

    void MainWindow::LoadScenarios()
    {
        ScenarioCombo().Items().Append(box_value(L"game_engine"));
        ScenarioCombo().Items().Append(box_value(L"maa"));
        ScenarioCombo().Items().Append(box_value(L"llm_infer_server"));
        ScenarioCombo().SelectedIndex(0);
    }

    std::string MainWindow::SelectedScenario()
    {
        const auto selection = ScenarioCombo().SelectedItem();
        if (selection == nullptr)
            return std::string();
        const auto value = winrt::unbox_value_or<winrt::hstring>(selection, L"");
        return winrt::to_string(value);
    }

    std::string MainWindow::SelectedModules()
    {
        const auto text = ModulesBox().Text();
        return winrt::to_string(text);
    }

    void MainWindow::RunButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ClearReport();
        SetState(L"Running", L"The selected benchmark modules are executing.", true);

        const std::string scenario = SelectedScenario();
        const std::string modules = SelectedModules();
        const auto dispatcher = DispatcherQueue();
        const auto weakThis = get_weak();

        std::thread([dispatcher, weakThis, scenario, modules]()
        {
            std::string output;
            std::string error;
            try
            {
                CapApi api;
                if (!api.Load())
                {
                    error = "ispcok_capi.dll was not found next to the executable.";
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
                        error = "ispcok_run_modules failed with code " + std::to_string(rc) + ".";
                        if (json != nullptr)
                            api.free_string(json);
                    }
                    else if (json != nullptr)
                    {
                        output = json;
                        api.free_string(json);
                    }
                    else
                    {
                        error = "ispcok_run_modules returned an empty result.";
                    }
                }
            }
            catch (std::exception const& exception)
            {
                error = std::string("Benchmark execution failed: ") + exception.what();
            }
            catch (...)
            {
                error = "Benchmark execution failed with an unknown error.";
            }

            const auto result = winrt::to_hstring(output);
            const auto executionError = winrt::to_hstring(error);
            dispatcher.TryEnqueue([weakThis, result, executionError]()
            {
                if (auto strongThis = weakThis.get())
                {
                    auto self = strongThis.get();
                    try
                    {
                        if (!executionError.empty())
                        {
                            self->ShowError(executionError.c_str());
                            return;
                        }

                        auto parsed = ispcok::dashboard::ParseDashboardReport(result.c_str());
                        if (!parsed)
                        {
                            self->ShowError(L"Report parsing failed: " + parsed.error, result.c_str());
                            return;
                        }
                        self->RenderReport(*parsed.report, result.c_str());
                    }
                    catch (winrt::hresult_error const& error)
                    {
                        self->ShowError(L"Dashboard rendering failed: " + std::wstring(error.message().c_str()), result.c_str());
                    }
                    catch (std::exception const& error)
                    {
                        self->ShowError(L"Dashboard rendering failed: " + std::wstring(winrt::to_hstring(error.what()).c_str()), result.c_str());
                    }
                    catch (...)
                    {
                        self->ShowError(L"Dashboard rendering failed with an unknown error.", result.c_str());
                    }
                }
            });
        }).detach();
    }

    void MainWindow::SetState(std::wstring const& state, std::wstring const& detail, bool running)
    {
        StatusText().Text(hstring(state));
        StatusDetailText().Text(hstring(detail));
        BusyRing().IsActive(running);
        RunButton().IsEnabled(m_capiAvailable && !running);
    }

    void MainWindow::ClearReport()
    {
        ScenarioCard().Visibility(Visibility::Collapsed);
        EmptyModulesText().Visibility(Visibility::Collapsed);
        ModuleCards().Items().Clear();
        RawJsonBox().Text(L"");
        RawJsonExpander().IsExpanded(false);
    }

    void MainWindow::ShowError(std::wstring const& detail, std::wstring const& rawJson)
    {
        ScenarioCard().Visibility(Visibility::Collapsed);
        ModuleCards().Items().Clear();
        EmptyModulesText().Visibility(Visibility::Visible);
        EmptyModulesText().Text(L"The dashboard could not display benchmark results.");
        RawJsonBox().Text(hstring(rawJson));
        RawJsonExpander().IsExpanded(!rawJson.empty());
        SetState(L"Error", detail, false);
    }

    void MainWindow::RenderReport(ispcok::dashboard::DashboardReport const& report, std::wstring const& rawJson)
    {
        ModuleCards().Items().Clear();
        RawJsonBox().Text(hstring(rawJson));
        RawJsonExpander().IsExpanded(false);

        if (report.scenario)
        {
            ScenarioNameText().Text(hstring(report.scenario->id));
            ScenarioScoreText().Text(FormatScore(report.scenario->score));
            BottlenecksText().Text(Join(report.scenario->bottlenecks));
            ScenarioCard().Visibility(Visibility::Visible);
        }
        else
        {
            ScenarioCard().Visibility(Visibility::Collapsed);
        }

        for (auto const& module : report.modules)
            ModuleCards().Items().Append(MakeModuleCard(module));

        const bool empty = report.modules.empty();
        EmptyModulesText().Text(L"The report completed without module results.");
        EmptyModulesText().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
        SetState(empty ? L"Empty result" : L"Completed",
            empty ? L"The report is valid and contains no modules." :
                std::to_wstring(report.modules.size()) + L" module result(s) loaded.",
            false);
    }
}
