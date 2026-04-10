#include "core/engine.h"

#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool condition, const std::string& message)
{
    if (!condition)
        std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

bool Skip(const std::string& message)
{
    std::cout << "[SKIP] " << message << '\n';
    return true;
}

bool TestJsonEscapesControlChars()
{
    ispcok::RunReport report;
    report.version = "test";

    ispcok::ModuleResult module;
    module.id = "json_escape_module";
    module.category = "test";
    module.status = "ok";
    module.score = 1.0;
    module.message = std::string("line1\nline2\t") + static_cast<char>(0x01) + "\"quoted\"";
    module.metrics["value"] = 1.0;
    report.modules.push_back(module);

    const std::string json = ispcok::ToJson(report);
    return Expect(json.find("\\n") != std::string::npos, "JSON should escape newline") &&
           Expect(json.find("\\t") != std::string::npos, "JSON should escape tab") &&
           Expect(json.find("\\u0001") != std::string::npos, "JSON should escape control chars with \\u00XX") &&
           Expect(json.find("\\\"quoted\\\"") != std::string::npos, "JSON should escape quotes");
}

bool TestScenarioRegistry()
{
    const std::vector<std::string> scenarios = ispcok::ListScenarios();
    bool ok = true;
    ok = ok && Expect(!scenarios.empty(), "Scenario list should not be empty");
    ok = ok && Expect(std::find(scenarios.begin(), scenarios.end(), "game_engine") != scenarios.end(), "game_engine should be registered");
    ok = ok && Expect(std::find(scenarios.begin(), scenarios.end(), "maa") != scenarios.end(), "maa should be registered");
    ok = ok && Expect(std::find(scenarios.begin(), scenarios.end(), "llm_infer_server") != scenarios.end(), "llm_infer_server should be registered");
    return ok;
}

bool TestRunSelectedModuleOnly()
{
    ispcok::RunOptions options;
    options.modules = {"cpu_fp32"};
    const ispcok::RunReport report = ispcok::Run(options);

    bool ok = true;
    ok = ok && Expect(report.modules.size() == 1, "Selecting a single module should return exactly one module");
    if (!report.modules.empty())
    {
        ok = ok && Expect(report.modules[0].id == "cpu_fp32", "Returned module id should match selection");
        ok = ok && Expect(report.modules[0].status == "ok", "cpu_fp32 should run successfully");
    }
    return ok;
}

bool TestLlmScenarioMarksMissingAccelerator()
{
    ispcok::RunOptions options;
    options.scenario = "llm_infer_server";
    const ispcok::RunReport report = ispcok::Run(options);

    bool found = false;
    if (report.has_scenario)
    {
        for (const auto& bottleneck : report.scenario.bottlenecks)
        {
            if (bottleneck.find("accelerator:missing(") != std::string::npos)
            {
                found = true;
                break;
            }
        }
    }
    return Expect(report.has_scenario, "Scenario result should exist") &&
           Expect(found, "llm_infer_server should mark missing accelerator when no accelerator module is ok");
}

bool TestBadPluginGuardrails(const std::string& plugin_dir)
{
#if !defined(_WIN32)
    (void)plugin_dir;
    return Skip("BadPluginGuardrails is Windows-only (plugin loader currently uses Win32 .dll APIs)");
#else
    if (plugin_dir.empty())
        return Skip("plugin_dir not provided, skipping plugin guardrail checks");
    if (!std::filesystem::exists(plugin_dir))
        return Skip("plugin_dir does not exist, skipping plugin guardrail checks");

    auto run_single = [&plugin_dir](const std::string& module_id)
    {
        ispcok::RunOptions options;
        options.plugin_dir = plugin_dir;
        options.modules = {module_id};
        return ispcok::Run(options);
    };

    bool ok = true;

    {
        const ispcok::RunReport report = run_single("bad_null_metrics");
        ok = ok && Expect(report.modules.size() == 1, "bad_null_metrics should be discovered");
        if (!report.modules.empty())
        {
            ok = ok && Expect(report.modules[0].status == "error", "bad_null_metrics should be rejected");
            ok = ok && Expect(report.modules[0].message.find("null metrics") != std::string::npos, "bad_null_metrics error message should mention null metrics");
        }
    }

    {
        const ispcok::RunReport report = run_single("bad_metric_overflow");
        ok = ok && Expect(report.modules.size() == 1, "bad_metric_overflow should be discovered");
        if (!report.modules.empty())
        {
            ok = ok && Expect(report.modules[0].status == "error", "bad_metric_overflow should be rejected");
            ok = ok && Expect(report.modules[0].message.find("exceeds host limit") != std::string::npos, "bad_metric_overflow error message should mention host limit");
        }
    }

    {
        const ispcok::RunReport report = run_single("bad_nan_score");
        ok = ok && Expect(report.modules.size() == 1, "bad_nan_score should be discovered");
        if (!report.modules.empty())
        {
            ok = ok && Expect(report.modules[0].status == "error", "bad_nan_score should be rejected");
            ok = ok && Expect(report.modules[0].message.find("non-finite score") != std::string::npos, "bad_nan_score error message should mention non-finite score");
        }
    }

    return ok;
#endif
}

} // namespace

int main(int argc, char** argv)
{
    std::string plugin_dir;
    if (argc >= 2)
        plugin_dir = argv[1];
    else
    {
        std::error_code ec;
        const std::filesystem::path exe_path = std::filesystem::absolute(argv[0], ec);
        if (!ec)
            plugin_dir = exe_path.parent_path().string();
    }

    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"JsonEscapesControlChars", &TestJsonEscapesControlChars},
        {"ScenarioRegistry", &TestScenarioRegistry},
        {"RunSelectedModuleOnly", &TestRunSelectedModuleOnly},
        {"LlmScenarioMarksMissingAccelerator", &TestLlmScenarioMarksMissingAccelerator},
        {"BadPluginGuardrails", [plugin_dir]() { return TestBadPluginGuardrails(plugin_dir); }},
    };

    int failed = 0;
    for (const auto& test : tests)
    {
        const bool ok = test.second();
        if (ok)
            std::cout << "[PASS] " << test.first << '\n';
        else
            ++failed;
    }

    if (failed != 0)
    {
        std::cerr << failed << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All tests passed.\n";
    return 0;
}
