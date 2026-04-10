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

- `src/core/builtin_modules.cpp`: built-in module implementations.
- `src/core/engine.cpp`: module selection, scenario prediction, report assembly.
- `src/core/plugin_loader.cpp`: runtime plugin loading and plugin module adapter.
- `src/core/json_writer.cpp`: JSON serialization.
- `src/cli/main.cpp`: CLI argument parsing and command dispatch.
- `src/capi/ispcok_capi.cpp`: C ABI wrapper and memory ownership boundary.

## Plugin ABI Contract (Current + Required)

Header: `include/ispcok/plugin_api.h`

### Host assumptions (current implementation)

- Plugin exports `ispcok_get_module_v1`.
- `id` / `category` are read from `IsPcOkPluginModuleV1`.
- `run()` fills `IsPcOkPluginResultV1` and returns `0` for success.
- Host copies metrics/message into host-owned containers immediately in `run()`.

### Ownership/lifetime requirements for plugin authors

- `id` / `category` must remain valid for the full loaded lifetime of the plugin module.
- `message` and `metrics` pointers must remain valid until `run()` returns.
- `metric_count` must match the accessible `metrics` array length.
- Plugin must not return host-owned pointers for later asynchronous use.

### Safety gaps to address

- Add strict validation (`metric_count` upper bound, null pointer checks for non-zero count, numeric sanity checks).
- Harden against malformed plugins and avoid trusting raw pointer contracts beyond immediate copy boundaries.
- Add explicit ABI versioning strategy for future incompatible changes.

## Scenario Rules

Current scenario scoring is hard-coded in `src/core/engine.cpp`.

- `game_engine`
- `maa`
- `llm_infer_server`

For scalability, move to table/config-driven weights and bottleneck rules:

- `config/scenarios/<scenario>.json` (or embedded constexpr tables)
- Validation for missing/unsupported modules per scenario

## CLI Behavior

Current behavior:

- Unknown CLI flags are ignored unless they conflict with known commands.

Recommended behavior (planned):

- Unknown flags should fail fast with a non-zero exit code.
- Show usage and highlight first invalid argument.

## JSON Output Notes

Current JSON escaping in `src/core/json_writer.cpp` handles common escapes but not all control characters.

Required behavior:

- Escape full `0x00-0x1F` range per JSON spec (`\u00XX`).
- Keep serialization deterministic for stable regression tests.

## Coding Rules for New Modules

- Module `id` must be stable, lowercase, snake_case.
- Return `status` in: `ok`, `not_supported`, `not_implemented`, `error`.
- Always fill `message` with actionable context.
- Keep module runtime short by default; long tests should be behind explicit opt-in.
- Keep metrics machine-readable and unit-explicit (`mibps`, `ns_per_access`, `avg_rtt_ms`, etc.).

## Test Recommendations

- Unit tests for scenario score calculation and bottleneck detection.
- Golden tests for JSON schema/stability.
- Plugin negative tests:
  - null message
  - null metrics with non-zero count
  - excessive `metric_count`
  - invalid UTF-8 / control characters
