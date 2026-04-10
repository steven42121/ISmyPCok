#include "core/engine.h"

#include "core/builtin_modules.h"
#include "core/json_writer.h"
#include "core/module.h"
#include "core/plugin_loader.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ispcok {
namespace {

std::string Trim(std::string text)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

struct ScoreTerm
{
    enum class Mode
    {
        Average,
        FirstOk
    };

    std::string key;
    Mode mode = Mode::Average;
    std::vector<std::string> modules;
    double fallback = 50.0;
    double weight = 0.0;
};

struct ModuleThreshold
{
    std::string module;
    double threshold = 0.0;
    std::string bottleneck;
};

struct TermThreshold
{
    std::string term;
    double threshold = 0.0;
    std::string bottleneck;
};

struct AvailabilityAnyRule
{
    std::string bottleneck;
    std::vector<std::string> modules;
};

struct ScenarioSpec
{
    std::string id;
    std::vector<ScoreTerm> terms;
    std::vector<ModuleThreshold> module_thresholds;
    std::vector<TermThreshold> term_thresholds;
    std::vector<std::string> required_non_ok_marks;
    std::vector<AvailabilityAnyRule> any_of_rules;
};

const std::vector<ScenarioSpec>& ScenarioSpecs()
{
    static const std::vector<ScenarioSpec> specs = {
        ScenarioSpec{
            "game_engine",
            {
                ScoreTerm{"cpu", ScoreTerm::Mode::Average, {"cpu_fp32", "cpu_branch_predict"}, 50.0, 0.25},
                ScoreTerm{"mem", ScoreTerm::Mode::Average, {"memory_bw"}, 50.0, 0.25},
                ScoreTerm{"gpu", ScoreTerm::Mode::Average, {"gpu_vulkan"}, 45.0, 0.50},
            },
            {
                ModuleThreshold{"memory_bw", 55.0, "memory_bw"},
                ModuleThreshold{"cpu_fp32", 55.0, "cpu_fp32"},
                ModuleThreshold{"cpu_branch_predict", 55.0, "cpu_branch_predict"},
                ModuleThreshold{"gpu_vulkan", 55.0, "gpu_vulkan"},
            },
            {},
            {"gpu_vulkan"},
            {}
        },
        ScenarioSpec{
            "maa",
            {
                ScoreTerm{"cpu", ScoreTerm::Mode::Average, {"cpu_scalar_int", "cpu_branch_predict"}, 50.0, 0.40},
                ScoreTerm{"disk", ScoreTerm::Mode::Average, {"disk_rand"}, 50.0, 0.25},
                ScoreTerm{"mem_latency", ScoreTerm::Mode::Average, {"memory_latency"}, 50.0, 0.20},
                ScoreTerm{"virt", ScoreTerm::Mode::Average, {"virt_state"}, 50.0, 0.15},
            },
            {
                ModuleThreshold{"cpu_scalar_int", 55.0, "cpu_scalar_int"},
                ModuleThreshold{"cpu_branch_predict", 55.0, "cpu_branch_predict"},
                ModuleThreshold{"disk_rand", 55.0, "disk_rand"},
                ModuleThreshold{"memory_latency", 55.0, "memory_latency"},
                ModuleThreshold{"virt_state", 55.0, "virt_state"},
            },
            {},
            {},
            {}
        },
        ScenarioSpec{
            "llm_infer_server",
            {
                ScoreTerm{"cpu", ScoreTerm::Mode::Average, {"cpu_fp32", "cpu_scalar_int"}, 50.0, 0.15},
                ScoreTerm{"mem", ScoreTerm::Mode::Average, {"memory_bw", "memory_latency"}, 50.0, 0.20},
                ScoreTerm{"accel", ScoreTerm::Mode::FirstOk, {"cuda", "npu"}, 45.0, 0.40},
                ScoreTerm{"net_bw", ScoreTerm::Mode::Average, {"net_bw"}, 50.0, 0.15},
                ScoreTerm{"net_rtt", ScoreTerm::Mode::Average, {"net_rtt"}, 50.0, 0.05},
                ScoreTerm{"virt", ScoreTerm::Mode::Average, {"virt_state"}, 50.0, 0.05},
            },
            {
                ModuleThreshold{"memory_bw", 60.0, "memory_bw"},
                ModuleThreshold{"memory_latency", 60.0, "memory_latency"},
                ModuleThreshold{"cpu_fp32", 60.0, "cpu_fp32"},
                ModuleThreshold{"cpu_scalar_int", 60.0, "cpu_scalar_int"},
                ModuleThreshold{"net_bw", 60.0, "net_bw"},
                ModuleThreshold{"net_rtt", 60.0, "net_rtt"},
            },
            {
                TermThreshold{"accel", 60.0, "accelerator"},
            },
            {},
            {
                AvailabilityAnyRule{"accelerator", {"cuda", "npu"}}
            }
        }
    };
    return specs;
}

std::optional<ScenarioSpec> FindScenarioSpec(const std::string& id)
{
    for (const auto& spec : ScenarioSpecs())
    {
        if (spec.id == id)
            return spec;
    }
    return std::nullopt;
}

ScenarioResult PredictScenario(const std::string& scenario_id, const std::vector<ModuleResult>& modules)
{
    ScenarioResult scenario;
    scenario.id = scenario_id;

    std::map<std::string, ModuleResult> by_module;
    for (const auto& module : modules)
        by_module[module.id] = module;

    auto score_of = [&by_module](const std::string& module_id, double fallback)
    {
        const auto it = by_module.find(module_id);
        if (it == by_module.end())
            return fallback;
        if (it->second.status != "ok")
            return fallback;
        return it->second.score;
    };

    auto status_of = [&by_module](const std::string& module_id) -> std::string
    {
        const auto it = by_module.find(module_id);
        if (it == by_module.end())
            return "missing";
        return it->second.status;
    };

    auto is_ok = [&by_module](const std::string& module_id) -> bool
    {
        const auto it = by_module.find(module_id);
        return (it != by_module.end()) && (it->second.status == "ok");
    };

    auto check_missing = [&by_module, &scenario](const std::string& module_id)
    {
        const auto it = by_module.find(module_id);
        if (it == by_module.end())
        {
            scenario.bottlenecks.emplace_back(module_id + ":missing");
            return;
        }
        if (it->second.status != "ok")
            scenario.bottlenecks.emplace_back(module_id + ":" + it->second.status);
    };

    const std::optional<ScenarioSpec> spec_opt = FindScenarioSpec(scenario_id);
    if (!spec_opt.has_value())
    {
        scenario.score = 0.0;
        scenario.bottlenecks.emplace_back("unknown_scenario");
        return scenario;
    }
    const ScenarioSpec& spec = *spec_opt;

    std::map<std::string, double> term_scores;
    for (const auto& term : spec.terms)
    {
        double term_score = term.fallback;
        if (term.mode == ScoreTerm::Mode::Average)
        {
            double sum = 0.0;
            if (term.modules.empty())
            {
                term_score = term.fallback;
            }
            else
            {
                for (const auto& module_id : term.modules)
                    sum += score_of(module_id, term.fallback);
                term_score = sum / static_cast<double>(term.modules.size());
            }
        }
        else
        {
            bool found_ok = false;
            for (const auto& module_id : term.modules)
            {
                if (is_ok(module_id))
                {
                    term_score = score_of(module_id, term.fallback);
                    found_ok = true;
                    break;
                }
            }
            if (!found_ok)
                term_score = term.fallback;
        }

        term_scores[term.key] = term_score;
        scenario.score += term_score * term.weight;
    }

    for (const auto& module_name : spec.required_non_ok_marks)
        check_missing(module_name);

    for (const auto& rule : spec.any_of_rules)
    {
        bool any_ok = false;
        for (const auto& module_id : rule.modules)
        {
            if (is_ok(module_id))
            {
                any_ok = true;
                break;
            }
        }
        if (!any_ok)
        {
            std::string status = rule.bottleneck + ":missing(";
            for (size_t i = 0; i < rule.modules.size(); ++i)
            {
                if (i > 0)
                    status += ",";
                status += rule.modules[i] + "=" + status_of(rule.modules[i]);
            }
            status += ")";
            scenario.bottlenecks.emplace_back(status);
        }
    }

    for (const auto& threshold : spec.module_thresholds)
    {
        if (score_of(threshold.module, 50.0) < threshold.threshold)
            scenario.bottlenecks.emplace_back(threshold.bottleneck);
    }

    for (const auto& threshold : spec.term_thresholds)
    {
        const auto it = term_scores.find(threshold.term);
        if ((it != term_scores.end()) && (it->second < threshold.threshold))
            scenario.bottlenecks.emplace_back(threshold.bottleneck);
    }

    return scenario;
}

std::vector<ModulePtr> CollectModules(const std::string& plugin_dir)
{
    std::vector<ModulePtr> modules = CreateBuiltinModules();
    std::vector<ModulePtr> plugins = LoadPluginModules(plugin_dir);

    // Plugin module with the same id overrides built-in module.
    for (const auto& plugin : plugins)
    {
        auto it = std::find_if(modules.begin(), modules.end(), [&](const ModulePtr& current)
        {
            return current->id() == plugin->id();
        });
        if (it == modules.end())
            modules.emplace_back(plugin);
        else
            *it = plugin;
    }
    return modules;
}

} // namespace

std::string Version()
{
    return "0.2.0";
}

std::vector<std::string> ListModules(const std::string& plugin_dir)
{
    std::vector<std::string> ids;
    for (const auto& module : CollectModules(plugin_dir))
        ids.emplace_back(module->id());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::vector<std::string> ListScenarios()
{
    std::vector<std::string> scenarios;
    scenarios.reserve(ScenarioSpecs().size());
    for (const auto& spec : ScenarioSpecs())
        scenarios.emplace_back(spec.id);
    return scenarios;
}

RunReport Run(const RunOptions& options)
{
    RunReport report;
    report.version = Version();

    std::vector<ModulePtr> modules = CollectModules(options.plugin_dir);
    std::set<std::string> selected;
    for (const auto& item : options.modules)
    {
        const std::string normalized = Trim(item);
        if (!normalized.empty())
            selected.insert(normalized);
    }

    for (const auto& module : modules)
    {
        const bool should_run = selected.empty() || (selected.count(module->id()) > 0);
        if (!should_run)
            continue;

        ModuleResult result = module->run();
        if (result.id.empty())
            result.id = module->id();
        if (result.category.empty())
            result.category = module->category();
        if (result.status.empty())
            result.status = "ok";
        result.plugin = module->is_plugin();
        report.modules.emplace_back(std::move(result));
    }

    std::sort(report.modules.begin(), report.modules.end(), [](const ModuleResult& lhs, const ModuleResult& rhs)
    {
        return lhs.id < rhs.id;
    });

    if (!options.scenario.empty())
    {
        report.has_scenario = true;
        report.scenario = PredictScenario(options.scenario, report.modules);
    }

    return report;
}

std::string ToJson(const RunReport& report)
{
    return ReportToJson(report);
}

} // namespace ispcok
