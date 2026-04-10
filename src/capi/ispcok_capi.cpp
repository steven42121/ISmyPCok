#include "ispcok/capi.h"

#include "core/engine.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

std::vector<std::string> SplitCsv(const char* csv)
{
    std::vector<std::string> items;
    if (csv == nullptr)
        return items;

    std::string text(csv);
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find(',', start);
        if (end == std::string::npos)
        {
            items.emplace_back(text.substr(start));
            break;
        }
        items.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return items;
}

char* CopyToHeap(const std::string& value)
{
    char* ptr = static_cast<char*>(std::malloc(value.size() + 1));
    if (ptr == nullptr)
        return nullptr;
    std::memcpy(ptr, value.c_str(), value.size() + 1);
    return ptr;
}

} // namespace

const char* ispcok_version(void)
{
    static const std::string version = ispcok::Version();
    return version.c_str();
}

int ispcok_run_modules(const char* modules_csv, const char* scenario, const char* plugin_dir, char** out_json)
{
    if (out_json == nullptr)
        return 1;

    *out_json = nullptr;

    ispcok::RunOptions options;
    options.modules = SplitCsv(modules_csv);
    if (scenario != nullptr)
        options.scenario = scenario;
    if (plugin_dir != nullptr)
        options.plugin_dir = plugin_dir;

    const ispcok::RunReport report = ispcok::Run(options);
    const std::string json = ispcok::ToJson(report);

    char* output = CopyToHeap(json);
    if (output == nullptr)
        return 2;

    *out_json = output;
    return 0;
}

void ispcok_free_string(char* ptr)
{
    std::free(ptr);
}
