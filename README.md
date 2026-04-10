# ISmyPCok

基于 [chronoxor/CppBenchmark](https://github.com/chronoxor/CppBenchmark) 的电脑性能工具（C++ / CMake）。

## 接入方式

1. CLI：`ispcok_cli`
2. C ABI SDK：`ispcok_capi`（`extern "C"` 导出）
3. 插件模块：扫描目录下 `.dll` 动态加载

## 当前已实现模块（内建）

- 下方列表由脚本从 `ispcok_cli list-modules` 自动生成（避免文档与代码漂移）。

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

- 注：其中部分模块可能返回 `not_implemented` 或 `not_supported`（如 `cpu_avx*` 取决于构建选项和硬件支持）。

## 场景评分（初版）

<!-- SCENARIO_LIST_START -->
- `game_engine`
- `maa`
- `llm_infer_server`
<!-- SCENARIO_LIST_END -->

说明：
- 场景评分优先使用 `status=ok` 的模块。
- 缺失或非 `ok` 模块会使用 fallback 分数，并在 `bottlenecks` 中附加状态标记（例如 `accelerator:missing(...)`）。

## 目录结构

- `third_party/CppBenchmark`：第三方依赖源码
- `src/core`：模块、引擎、场景评分、JSON 报告
- `src/cli`：CLI 入口
- `src/capi`：C ABI 入口
- `src/pc_benchmark.cpp`：CppBenchmark 原始基准定义（独立可执行）
- `scripts/fetch_3rd_party.ps1`：拉取第三方依赖

## 开发文档

- 开发总览与架构：`docs/DEVELOPMENT.md`
- 插件 ABI 约定：`docs/PLUGIN_ABI.md`
- 同步 README 生成区块：`.\scripts\update_readme_generated.ps1`

## 快速开始（Windows / Visual Studio）

```powershell
cmake -S . -B build
cmake --build build --config Release --target ispcok_cli
.\build\Release\ispcok_cli.exe list-modules
.\build\Release\ispcok_cli.exe list-scenarios
.\build\Release\ispcok_cli.exe run --modules cpu_fp32,memory_bw --scenario llm_infer_server
```

## AVX2 / AVX-512（可选）

默认构建下，`cpu_avx2` / `cpu_avx512` 会返回 `not_supported`（因为没启用对应编译目标）。

```powershell
cmake -S . -B build-avx2 -DISPCOK_ENABLE_AVX2=ON
cmake --build build-avx2 --config Release --target ispcok_cli

cmake -S . -B build-avx512 -DISPCOK_ENABLE_AVX512=ON
cmake --build build-avx512 --config Release --target ispcok_cli
```

## CLI 常用命令

```powershell
.\build\Release\ispcok_cli.exe list-modules --plugin-dir .\plugins
.\build\Release\ispcok_cli.exe list-scenarios
.\build\Release\ispcok_cli.exe run --scenario game_engine
```

## C ABI（SDK）导出函数

头文件：`include/ispcok/capi.h`

- `const char* ispcok_version(void);`
- `int ispcok_run_modules(const char* modules_csv, const char* scenario, const char* plugin_dir, char** out_json);`
- `void ispcok_free_string(char* ptr);`

插件接口头文件：`include/ispcok/plugin_api.h`  
插件导出符号名：`ispcok_get_module_v1`

Python 接入示例：`examples/python/capi_demo.py`

```powershell
python .\examples\python\capi_demo.py
```

## 插件示例（GPU 占位替换）

```powershell
cmake --build build --config Release --target ispcok_plugin_sample_gpu
mkdir -Force .\plugins | Out-Null
Copy-Item .\build\Release\ispcok_plugin_gpu_vulkan_sample.dll .\plugins\
.\build\Release\ispcok_cli.exe run --plugin-dir .\plugins --modules gpu_vulkan --scenario game_engine
```

## 注意

- `pc_benchmark` 与 `ispcok_core` 是两套并行体系。`pc_benchmark` 用于 CppBenchmark 原始实验；模块化扩展以 `ispcok_core`（CLI/CAPI/Plugin）为主。

## 第三方依赖说明

如果你把 `third_party/CppBenchmark` 删除了，可执行：

```powershell
.\scripts\fetch_3rd_party.ps1
```

