# Development Guide

## Runtime Model

This repository currently contains two executable stacks:

1. `pc_benchmark`:
- Legacy standalone benchmark executable based directly on `CppBenchmark` macros.
- Focused on low-level benchmark authoring and experimentation in a single binary.

2. `ispcok_core` ecosystem:
- `ispcok_cli` for command-line orchestration.
- `ispcok_capi` for stable C ABI integration.
- Optional plugin modules loaded at runtime (`.dll` on Windows, `.so` / `.dylib` on Linux/macOS).
- Scenario scoring in `src/core/engine.cpp`.

These two stacks intentionally coexist during migration. New modular features should be built in `ispcok_core`.

## Build Targets

- `ispcok_core`: core engine, built-in modules, plugin loader, JSON writer.
- `ispcok_cli`: CLI entrypoint.
- `ispcok_capi`: C ABI DLL.
- `ispcok_plugin_sample_gpu`: sample plugin module.
- `ispcok_plugin_gpu_vulkan`: real Vulkan FP32 compute plugin (built when the Vulkan SDK is detected).
- `ispcok_plugin_cuda`: real CUDA FP32 compute plugin (built when the CUDA toolkit is detected).
- `ispcok_plugin_xpu`: real Level Zero FP32 compute plugin (built when the Level Zero loader is detected).
- `ispcok_plugin_hip`: real HIP FP32 compute plugin (built when the HIP compiler/runtime is detected).
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
- `message` and `metrics` pointers must remain valid until the next `run()` call for the module on the same thread, or until module unload.
- `metric_count` must match the accessible `metrics` array length.
- Plugin must not return host-owned pointers for later asynchronous use.
- Shared device contexts must be serialized; thread-local result storage prevents concurrent `message/metrics` overwrite.

### Safety hardening status

- Implemented:
  - null metrics guard for non-zero `metric_count`
  - `metric_count` host-side upper bound
  - non-finite score/metric filtering
- Planned:
  - explicit ABI v2 contract with stronger ownership model
  - more robust crash/isolation strategy for malformed plugins

## GPU / Accelerator Plugins

`gpu_vulkan`, `cuda`, `xpu`, and `hip` are standalone dynamic plugins, so `ispcok_core` does not link GPU SDKs. A same-id plugin replaces the builtin degraded module; when the plugin is absent, the module returns `not_supported` with a `backend not compiled` message.

All four plugins execute the same 1024 x 1024 FP32 matrix multiplication and report:

- `fp32_gflops`
- `elapsed_ms`
- `checksum`

CMake switches:

- `ISPCOK_ENABLE_GPU_BACKENDS`
- `ISPCOK_ENABLE_VULKAN_BACKEND`
- `ISPCOK_ENABLE_CUDA_BACKEND`
- `ISPCOK_ENABLE_XPU_BACKEND`
- `ISPCOK_ENABLE_HIP_BACKEND`

Missing SDKs skip only the corresponding plugin target and leave CMake configuration successful.

Run the Vulkan plugin:

```bash
cmake -S . -B build -DISPCOK_ENABLE_VULKAN_BACKEND=ON
cmake --build build --target ispcok_cli ispcok_plugin_gpu_vulkan --parallel 2
./build/ispcok_cli run --plugin-dir ./build/plugins --modules gpu_vulkan
```

On Linux without a physical GPU, verify the real Vulkan compute path with Mesa lavapipe:

```bash
bash scripts/test_vulkan_plugin_lavapipe.sh
```

Debian / Ubuntu systems require `libvulkan-dev` and `mesa-vulkan-drivers`.

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
- Run (Windows):

```powershell
cmake --build build --config Release --target ispcok_tests
ctest --test-dir build -C Release --output-on-failure
```

- Run (Linux / macOS):

```bash
cmake --build build --target ispcok_tests
ctest --test-dir build --output-on-failure
```

- Includes plugin negative tests for malformed plugin outputs (Windows / Linux / macOS).
