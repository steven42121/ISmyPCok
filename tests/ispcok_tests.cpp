#include "core/engine.h"
#include "../plugins/common/npu_matmul_workload.h"

#include <cmath>
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

bool TestNpuHalfConversion()
{
    using ispcok::plugins::FloatToHalf;
    using ispcok::plugins::HalfToFloat;

    bool ok = true;
    const std::vector<float> exact_values = {
        0.0f, -0.0f, 1.0f, -2.0f, 0.5f, 0.25f, 65504.0f,
        1.0f / 256.0f, 17.0f / 256.0f};
    for (float value : exact_values)
    {
        const float round_trip = HalfToFloat(FloatToHalf(value));
        ok = ok && Expect(round_trip == value, "binary16 exact value should round-trip");
    }
    ok = ok && Expect(std::isinf(HalfToFloat(FloatToHalf(INFINITY))), "positive infinity should round-trip");
    ok = ok && Expect(std::isinf(HalfToFloat(FloatToHalf(-INFINITY))), "negative infinity should round-trip");
    ok = ok && Expect(std::isnan(HalfToFloat(FloatToHalf(NAN))), "NaN should remain NaN");

    std::vector<float> a;
    std::vector<float> b;
    ispcok::plugins::FillNpuMatrices(a, b);
    for (float value : a)
        ok = ok && Expect(HalfToFloat(FloatToHalf(value)) == value, "NPU input A should be exactly representable as binary16");
    for (float value : b)
        ok = ok && Expect(HalfToFloat(FloatToHalf(value)) == value, "NPU input B should be exactly representable as binary16");
    return ok;
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

bool TestNetworkModulesRunOnHost()
{
    ispcok::RunOptions options;
    options.modules = {"net_rtt", "net_bw"};
    const ispcok::RunReport report = ispcok::Run(options);

    bool ok = true;
    ok = ok && Expect(report.modules.size() == 2, "net_rtt and net_bw should both be discovered");
    for (const auto& module : report.modules)
    {
        ok = ok && Expect(module.status == "ok", std::string(module.id) + " should run successfully on this host");
        if (module.id == "net_rtt")
        {
            ok = ok && Expect(module.metrics.count("avg_rtt_ms") == 1, "net_rtt should report avg_rtt_ms");
            ok = ok && Expect(module.metrics.at("avg_rtt_ms") >= 0.0, "net_rtt avg_rtt_ms should be non-negative");
        }
        if (module.id == "net_bw")
        {
            ok = ok && Expect(module.metrics.count("mibps") == 1, "net_bw should report mibps");
        }
    }
    return ok;
}

bool TestBadPluginGuardrails(const std::string& plugin_dir)
{
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
}

bool TestAcceleratorModulesDegradeGracefully()
{
    // Without any accelerator SDK or plugin, the modules must degrade
    // to "not_supported" with an explanatory message instead of failing the
    // run, crashing, or reporting a fabricated score.
    const std::vector<std::string> accelerator_ids = {"gpu_vulkan", "cuda", "hip", "xpu", "gpu_dx12", "npu"};
    ispcok::RunOptions options;
    options.modules = accelerator_ids;
    const ispcok::RunReport report = ispcok::Run(options);

    bool ok = true;
    ok = ok && Expect(report.modules.size() == accelerator_ids.size(), "all accelerator modules should be discovered");

    for (const auto& module : report.modules)
    {
        ok = ok && Expect(module.status != "not_implemented", std::string(module.id) + " should no longer be a not_implemented placeholder");
        ok = ok && Expect(module.status == "ok" || module.status == "not_supported" || module.status == "error",
                          std::string(module.id) + " should be ok, not_supported, or error, got " + module.status);
        if (module.status == "ok")
            ok = ok && Expect(module.score > 0.0, std::string(module.id) + " ok result should have a positive score");
        if (module.status == "not_supported")
        {
            ok = ok && Expect(module.score == 0.0, std::string(module.id) + " not_supported should have score 0");
            ok = ok && Expect(!module.message.empty(), std::string(module.id) + " not_supported should carry an explanatory message");
        }
    }
    return ok;
}

bool TestPluginOverrideAcceleratorModule(const std::string& real_plugin_dir)
{
    // A plugin with the same id as a builtin accelerator module must replace
    // the builtin module via the existing override mechanism.
    if (real_plugin_dir.empty() || !std::filesystem::exists(real_plugin_dir))
        return Skip("real Vulkan plugin was not built, skipping plugin override checks");

    ispcok::RunOptions options;
    options.plugin_dir = real_plugin_dir;
    options.modules = {"gpu_vulkan"};
    const ispcok::RunReport report = ispcok::Run(options);

    bool ok = true;
    ok = ok && Expect(report.modules.size() == 1, "gpu_vulkan should be discovered from the plugin directory");
    if (!report.modules.empty())
    {
        ok = ok && Expect(report.modules[0].plugin == true, "gpu_vulkan result should come from a plugin");
        ok = ok && Expect(report.modules[0].status == "ok" || report.modules[0].status == "not_supported" || report.modules[0].status == "error",
                          "plugin gpu_vulkan should be ok, not_supported, or error");
    }
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    std::string plugin_dir;
    std::string real_plugin_dir;
    if (argc >= 2)
        plugin_dir = argv[1];
    else
    {
        std::error_code ec;
        const std::filesystem::path exe_path = std::filesystem::absolute(argv[0], ec);
        if (!ec)
            plugin_dir = exe_path.parent_path().string();
    }
    if (argc >= 3)
        real_plugin_dir = argv[2];

    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"JsonEscapesControlChars", &TestJsonEscapesControlChars},
        {"NpuHalfConversion", &TestNpuHalfConversion},
        {"ScenarioRegistry", &TestScenarioRegistry},
        {"RunSelectedModuleOnly", &TestRunSelectedModuleOnly},
        {"LlmScenarioMarksMissingAccelerator", &TestLlmScenarioMarksMissingAccelerator},
        {"NetworkModulesRunOnHost", &TestNetworkModulesRunOnHost},
        {"AcceleratorModulesDegradeGracefully", &TestAcceleratorModulesDegradeGracefully},
        {"BadPluginGuardrails", [plugin_dir]() { return TestBadPluginGuardrails(plugin_dir); }},
        {"PluginOverrideAcceleratorModule", [real_plugin_dir]() { return TestPluginOverrideAcceleratorModule(real_plugin_dir); }},
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
