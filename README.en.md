# ISmyPCok

A PC performance toolkit (C++ / CMake) built on top of [chronoxor/CppBenchmark](https://github.com/chronoxor/CppBenchmark).

## Integration Modes

1. CLI: `ispcok_cli`
2. C ABI SDK: `ispcok_capi` (`extern "C"` exports)
3. Plugin modules: runtime `.dll` discovery/loading

## Built-in Modules

- The list below is auto-generated from `ispcok_cli list-modules` to avoid docs drift.

<!-- MODULE_LIST_START -->
- `cpu_avx2`
- `cpu_avx512`
- `cpu_branch_predict`
- `cpu_fp32`
- `cpu_scalar_int`
- `cuda`
- `disk_rand`
- `disk_seq`
- `gpu_dx12`
- `gpu_vulkan`
- `hip`
- `memory_bw`
- `memory_latency`
- `net_bw`
- `net_rtt`
- `npu`
- `virt_state`
- `xpu`
<!-- MODULE_LIST_END -->

- Note: some modules can return `not_implemented` or `not_supported` depending on build flags/hardware.

## Scenario Scoring (Initial)

<!-- SCENARIO_LIST_START -->
- `game_engine`
- `maa`
- `llm_infer_server`
<!-- SCENARIO_LIST_END -->

Notes:
- Scenario scoring prioritizes modules with `status=ok`.
- Missing/non-`ok` modules use fallback scores and are reflected in `bottlenecks` (for example `accelerator:missing(...)`).

## Repository Layout

- `third_party/CppBenchmark`: third-party dependency sources
- `src/core`: modules, engine, scenario scoring, JSON report
- `src/cli`: CLI entrypoint
- `src/capi`: C ABI entrypoint
- `src/pc_benchmark.cpp`: standalone CppBenchmark executable source
- `scripts/fetch_3rd_party.ps1`: third-party bootstrap script

## Developer Docs

- Development guide: `docs/DEVELOPMENT.en.md` / `docs/DEVELOPMENT.zh-CN.md`
- Plugin ABI notes: `docs/PLUGIN_ABI.en.md` / `docs/PLUGIN_ABI.zh-CN.md`
- Sync README generated blocks: `.\scripts\update_readme_generated.ps1`

## Quick Start (Windows / Visual Studio)

```powershell
cmake -S . -B build
cmake --build build --config Release --target ispcok_cli
.\build\Release\ispcok_cli.exe list-modules
.\build\Release\ispcok_cli.exe list-scenarios
.\build\Release\ispcok_cli.exe run --modules cpu_fp32,memory_bw --scenario llm_infer_server
```

## AVX2 / AVX-512 (Optional)

By default, `cpu_avx2` / `cpu_avx512` may return `not_supported` because ISA targets are not enabled.

```powershell
cmake -S . -B build-avx2 -DISPCOK_ENABLE_AVX2=ON
cmake --build build-avx2 --config Release --target ispcok_cli

cmake -S . -B build-avx512 -DISPCOK_ENABLE_AVX512=ON
cmake --build build-avx512 --config Release --target ispcok_cli
```

## Common CLI Commands

```powershell
.\build\Release\ispcok_cli.exe list-modules --plugin-dir .\plugins
.\build\Release\ispcok_cli.exe list-scenarios
.\build\Release\ispcok_cli.exe run --scenario game_engine
```

## Tests

```powershell
cmake --build build --config Release --target ispcok_tests
ctest --test-dir build -C Release --output-on-failure
```

## C ABI Exports

Header: `include/ispcok/capi.h`

- `const char* ispcok_version(void);`
- `int ispcok_run_modules(const char* modules_csv, const char* scenario, const char* plugin_dir, char** out_json);`
- `void ispcok_free_string(char* ptr);`

Plugin ABI header: `include/ispcok/plugin_api.h`  
Plugin entrypoint symbol: `ispcok_get_module_v1`

Python integration example: `examples/python/capi_demo.py`

```powershell
python .\examples\python\capi_demo.py
```

## Plugin Example (Replace `gpu_vulkan` placeholder)

```powershell
cmake --build build --config Release --target ispcok_plugin_sample_gpu
mkdir -Force .\plugins | Out-Null
Copy-Item .\build\Release\ispcok_plugin_gpu_vulkan_sample.dll .\plugins\
.\build\Release\ispcok_cli.exe run --plugin-dir .\plugins --modules gpu_vulkan --scenario game_engine
```

## Notes

- `pc_benchmark` and `ispcok_core` coexist intentionally. Use `pc_benchmark` for raw CppBenchmark experiments and `ispcok_core` for modular integration.

## Third-Party Bootstrap

If `third_party/CppBenchmark` is missing:

```powershell
.\scripts\fetch_3rd_party.ps1
```
