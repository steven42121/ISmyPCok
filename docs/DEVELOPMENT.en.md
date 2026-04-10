# Development Guide

## Runtime Model

This repository currently contains two executable stacks:

1. `pc_benchmark`:
- Legacy standalone benchmark executable based directly on `CppBenchmark` macros.
- Focused on low-level benchmark authoring and experimentation in a single binary.

2. `ispcok_core` ecosystem:
- `ispcok_cli` for command-line orchestration.
- `ispcok_capi` for stable C ABI integration.
- Optional plugin `.dll` modules loaded at runtime.
- Scenario scoring in `src/core/engine.cpp`.

These two stacks intentionally coexist during migration. New modular features should be built in `ispcok_core`.

## Build Targets

- `ispcok_core`: core engine, built-in modules, plugin loader, JSON writer.
- `ispcok_cli`: CLI entrypoint.
- `ispcok_capi`: C ABI DLL.
- `ispcok_plugin_sample_gpu`: sample plugin module.
- `pc_benchmark`: legacy CppBenchmark executable.

## Architecture Map

- `src/core/builtin_modules.cpp`: built-in module registration/composition.
- `src/core/engine.cpp`: module selection, scenario prediction, report assembly.
- `src/core/plugin_loader.cpp`: runtime plugin loading and plugin module adapter.
- `src/core/json_writer.cpp`: JSON serialization.
- `src/cli/main.cpp`: CLI argument parsing and command dispatch.
- `src/capi/ispcok_capi.cpp`: C ABI wrapper and memory ownership boundary.

## Plugin ABI Contract

Header: `include/ispcok/plugin_api.h`

### Host assumptions

- Plugin exports `ispcok_get_module_v1`.
- `id` / `category` are read from `IsPcOkPluginModuleV1`.
- `run()` fills `IsPcOkPluginResultV1` and returns `0` for success.
- Host copies metrics/message into host-owned containers immediately in `run()`.

### Ownership/lifetime requirements for plugin authors

- `id` / `category` must remain valid for the full loaded lifetime of the plugin module.
- `message` and `metrics` pointers must remain valid until `run()` returns.
- `metric_count` must match the accessible `metrics` array length.
- Plugin must not return host-owned pointers for later asynchronous use.

### Safety hardening status

- Implemented:
  - null metrics guard for non-zero `metric_count`
  - `metric_count` host-side upper bound
  - non-finite score/metric filtering
- Planned:
  - explicit ABI v2 contract with stronger ownership model
  - more robust crash/isolation strategy for malformed plugins

## Scenario Rules

Scenario definitions are table-driven in `src/core/engine.cpp` and shared by:

- `ListScenarios()`
- runtime scoring/evaluation

Current scenarios:

- `game_engine`
- `maa`
- `llm_infer_server`

## CLI Behavior

Current behavior:

- Unknown flags fail fast with non-zero exit code.
- Missing option values fail fast.
- Unknown scenario names fail fast.
- Unknown explicit module selection fails fast.

## JSON Output

`src/core/json_writer.cpp` escapes:

- `\`, `"`, `\n`, `\r`, `\t`
- full control range `0x00-0x1F` as `\u00XX`

## Coding Rules for New Modules

- Module `id` must be stable, lowercase, snake_case.
- Return `status` in: `ok`, `not_supported`, `not_implemented`, `error`.
- Always fill `message` with actionable context.
- Keep module runtime short by default; long tests should be behind explicit opt-in.
- Keep metrics machine-readable and unit-explicit (`mibps`, `ns_per_access`, `avg_rtt_ms`, etc.).

## Tests

- Unit tests: `tests/ispcok_tests.cpp`
- Run:

```powershell
cmake --build build --config Release --target ispcok_tests
ctest --test-dir build -C Release --output-on-failure
```

- Includes plugin negative tests for malformed plugin outputs.
