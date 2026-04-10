#include "core/engine.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

std::string Trim(std::string text)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

std::vector<std::string> SplitCsv(const std::string& csv)
{
    std::vector<std::string> items;
    if (csv.empty())
        return items;

    std::size_t start = 0;
    while (start <= csv.size())
    {
        const std::size_t end = csv.find(',', start);
        if (end == std::string::npos)
        {
            items.emplace_back(Trim(csv.substr(start)));
            break;
        }
        items.emplace_back(Trim(csv.substr(start, end - start)));
        start = end + 1;
    }
    return items;
}

void PrintUsage()
{
    std::cout << "Usage:\n"
              << "  ispcok_cli list-modules [--plugin-dir <dir>]\n"
              << "  ispcok_cli list-scenarios\n"
              << "  ispcok_cli run [--modules a,b,c] [--scenario game_engine|maa|llm_infer_server] [--plugin-dir <dir>]\n";
}

bool IsKnownScenario(const std::string& scenario)
{
    const std::vector<std::string> scenarios = ispcok::ListScenarios();
    return std::find(scenarios.begin(), scenarios.end(), scenario) != scenarios.end();
}

std::set<std::string> AvailableModules(const std::string& plugin_dir)
{
    const std::vector<std::string> modules = ispcok::ListModules(plugin_dir);
    return std::set<std::string>(modules.begin(), modules.end());
}

bool ValidateRequestedModules(const std::vector<std::string>& requested, const std::string& plugin_dir)
{
    if (requested.empty())
        return true;

    const std::set<std::string> available = AvailableModules(plugin_dir);
    std::vector<std::string> unknown;
    for (const auto& module : requested)
    {
        if (module.empty())
            continue;
        if (available.count(module) == 0)
            unknown.emplace_back(module);
    }

    if (unknown.empty())
        return true;

    std::cerr << "Error: unknown module(s): ";
    for (size_t i = 0; i < unknown.size(); ++i)
    {
        if (i > 0)
            std::cerr << ", ";
        std::cerr << unknown[i];
    }
    std::cerr << '\n';
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    std::string command = argv[1];
    std::string modules_csv;
    std::string scenario;
    std::string plugin_dir;

    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if ((arg == "--modules") && (i + 1 < argc))
        {
            if (command != "run")
            {
                std::cerr << "Error: --modules is only valid for 'run'\n";
                return 2;
            }
            modules_csv = argv[++i];
            continue;
        }
        if (arg == "--modules")
        {
            std::cerr << "Error: --modules requires a value\n";
            return 2;
        }
        if ((arg == "--scenario") && (i + 1 < argc))
        {
            if (command != "run")
            {
                std::cerr << "Error: --scenario is only valid for 'run'\n";
                return 2;
            }
            scenario = argv[++i];
            continue;
        }
        if (arg == "--scenario")
        {
            std::cerr << "Error: --scenario requires a value\n";
            return 2;
        }
        if ((arg == "--plugin-dir") && (i + 1 < argc))
        {
            plugin_dir = argv[++i];
            continue;
        }
        if (arg == "--plugin-dir")
        {
            std::cerr << "Error: --plugin-dir requires a value\n";
            return 2;
        }
        if (arg == "--help" || arg == "-h")
        {
            PrintUsage();
            return 0;
        }
        std::cerr << "Error: unknown argument: " << arg << '\n';
        PrintUsage();
        return 2;
    }

    if (command == "list-modules")
    {
        const std::vector<std::string> modules = ispcok::ListModules(plugin_dir);
        for (const auto& module : modules)
            std::cout << module << '\n';
        return 0;
    }

    if (command == "list-scenarios")
    {
        const std::vector<std::string> scenarios = ispcok::ListScenarios();
        for (const auto& scenario_name : scenarios)
            std::cout << scenario_name << '\n';
        return 0;
    }

    if (command == "run")
    {
        ispcok::RunOptions options;
        options.modules = SplitCsv(modules_csv);
        options.scenario = scenario;
        options.plugin_dir = plugin_dir;

        if (!options.scenario.empty() && !IsKnownScenario(options.scenario))
        {
            std::cerr << "Error: unknown scenario: " << options.scenario << '\n';
            return 2;
        }
        if (!ValidateRequestedModules(options.modules, options.plugin_dir))
            return 2;

        const ispcok::RunReport report = ispcok::Run(options);
        std::cout << ispcok::ToJson(report) << '\n';
        return 0;
    }

    PrintUsage();
    return 1;
}
