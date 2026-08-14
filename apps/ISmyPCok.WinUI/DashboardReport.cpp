#include "pch.h"
#include "DashboardReport.h"

#include <cmath>
#include <stdexcept>

#include <winrt/Windows.Data.Json.h>

namespace ispcok::dashboard
{
    namespace
    {
        using winrt::Windows::Data::Json::JsonArray;
        using winrt::Windows::Data::Json::JsonObject;
        using winrt::Windows::Data::Json::JsonValueType;

        std::runtime_error TypeError(std::wstring const& path, wchar_t const* expected)
        {
            return std::runtime_error(winrt::to_string(path + L" must be " + expected));
        }

        auto RequiredValue(JsonObject const& object, wchar_t const* name, JsonValueType type, std::wstring const& path)
        {
            if (!object.HasKey(name))
            {
                throw std::runtime_error(winrt::to_string(path + L" is required"));
            }

            auto value = object.GetNamedValue(name);
            if (value.ValueType() != type)
            {
                switch (type)
                {
                case JsonValueType::String:
                    throw TypeError(path, L"a string");
                case JsonValueType::Number:
                    throw TypeError(path, L"a number");
                case JsonValueType::Boolean:
                    throw TypeError(path, L"a boolean");
                case JsonValueType::Array:
                    throw TypeError(path, L"an array");
                case JsonValueType::Object:
                    throw TypeError(path, L"an object");
                default:
                    throw TypeError(path, L"a compatible JSON value");
                }
            }
            return value;
        }

        std::wstring RequiredString(JsonObject const& object, wchar_t const* name, std::wstring const& path)
        {
            return RequiredValue(object, name, JsonValueType::String, path).GetString().c_str();
        }

        double RequiredNumber(JsonObject const& object, wchar_t const* name, std::wstring const& path)
        {
            const double value = RequiredValue(object, name, JsonValueType::Number, path).GetNumber();
            if (!std::isfinite(value))
            {
                throw std::runtime_error(winrt::to_string(path + L" must be finite"));
            }
            return value;
        }

        DashboardModule ParseModule(JsonObject const& object, uint32_t index)
        {
            const std::wstring path = L"modules[" + std::to_wstring(index) + L"]";
            DashboardModule module;
            module.id = RequiredString(object, L"id", path + L".id");
            module.category = RequiredString(object, L"category", path + L".category");
            module.status = RequiredString(object, L"status", path + L".status");
            module.score = RequiredNumber(object, L"score", path + L".score");
            module.plugin = RequiredValue(object, L"plugin", JsonValueType::Boolean, path + L".plugin").GetBoolean();
            module.message = RequiredString(object, L"message", path + L".message");

            auto metrics = RequiredValue(object, L"metrics", JsonValueType::Object, path + L".metrics").GetObject();
            for (auto const& entry : metrics)
            {
                const std::wstring metricName = entry.Key().c_str();
                const std::wstring metricPath = path + L".metrics." + metricName;
                if (entry.Value().ValueType() != JsonValueType::Number)
                {
                    throw TypeError(metricPath, L"a number");
                }
                const double value = entry.Value().GetNumber();
                if (!std::isfinite(value))
                {
                    throw std::runtime_error(winrt::to_string(metricPath + L" must be finite"));
                }
                module.metrics.push_back({ metricName, value });
            }
            return module;
        }

        DashboardScenario ParseScenario(JsonObject const& object)
        {
            DashboardScenario scenario;
            scenario.id = RequiredString(object, L"id", L"scenario.id");
            scenario.score = RequiredNumber(object, L"score", L"scenario.score");
            JsonArray bottlenecks = RequiredValue(
                object, L"bottlenecks", JsonValueType::Array, L"scenario.bottlenecks").GetArray();
            for (uint32_t index = 0; index < bottlenecks.Size(); ++index)
            {
                auto value = bottlenecks.GetAt(index);
                if (value.ValueType() != JsonValueType::String)
                {
                    throw TypeError(L"scenario.bottlenecks[" + std::to_wstring(index) + L"]", L"a string");
                }
                scenario.bottlenecks.emplace_back(value.GetString().c_str());
            }
            return scenario;
        }
    }

    DashboardParseResult ParseDashboardReport(std::wstring const& json) noexcept
    {
        try
        {
            JsonObject root = JsonObject::Parse(winrt::hstring(json));
            DashboardReport report;
            report.version = RequiredString(root, L"version", L"version");

            JsonArray modules = RequiredValue(root, L"modules", JsonValueType::Array, L"modules").GetArray();
            report.modules.reserve(modules.Size());
            for (uint32_t index = 0; index < modules.Size(); ++index)
            {
                auto value = modules.GetAt(index);
                if (value.ValueType() != JsonValueType::Object)
                {
                    throw TypeError(L"modules[" + std::to_wstring(index) + L"]", L"an object");
                }
                report.modules.push_back(ParseModule(value.GetObject(), index));
            }

            if (root.HasKey(L"scenario"))
            {
                auto scenario = root.GetNamedValue(L"scenario");
                if (scenario.ValueType() != JsonValueType::Object)
                {
                    throw TypeError(L"scenario", L"an object");
                }
                report.scenario = ParseScenario(scenario.GetObject());
            }

            return { std::move(report), {} };
        }
        catch (winrt::hresult_error const& error)
        {
            return { std::nullopt, L"Invalid JSON: " + std::wstring(error.message().c_str()) };
        }
        catch (std::exception const& error)
        {
            return { std::nullopt, winrt::to_hstring(error.what()).c_str() };
        }
        catch (...)
        {
            return { std::nullopt, L"Unknown report parsing error" };
        }
    }
}
