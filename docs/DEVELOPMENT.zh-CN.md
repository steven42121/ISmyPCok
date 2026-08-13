# 开发指南

## 运行时模型

当前仓库包含两条可执行链路：

1. `pc_benchmark`：
- 基于 `CppBenchmark` 宏的传统单体基准程序。
- 用于底层基准实验与快速验证。

2. `ispcok_core` 体系：
- `ispcok_cli`：命令行入口。
- `ispcok_capi`：稳定 C ABI 入口。
- 可选插件运行时加载（Windows `.dll` / Linux、macOS `.so`、`.dylib`）。
- 场景评分逻辑在 `src/core/engine.cpp`。

两条链路并行存在是有意设计。新特性建议优先在 `ispcok_core` 落地。

## 构建目标

- `ispcok_core`：核心引擎、内建模块、插件加载、JSON 输出。
- `ispcok_cli`：CLI 入口。
- `ispcok_capi`：C ABI 动态库。
- `ispcok_plugin_sample_gpu`：插件示例。
- `ispcok_plugin_gpu_vulkan`：真实 Vulkan FP32 compute 插件（检测到 Vulkan SDK 时构建）。
- `ispcok_plugin_cuda`：真实 CUDA FP32 compute 插件（检测到 CUDA toolkit 时构建）。
- `ispcok_plugin_xpu`：真实 Level Zero FP32 compute 插件（检测到 Level Zero loader 时构建）。
- `ispcok_plugin_hip`：真实 HIP FP32 compute 插件（检测到 HIP compiler/runtime 时构建）。
- `pc_benchmark`：传统 CppBenchmark 可执行文件。

## 架构映射

- `src/core/builtin_modules.cpp`：内建模块聚合注册。
- `src/core/engine.cpp`：模块选择、场景评分、报告装配。
- `src/core/plugin_loader.cpp`：插件加载与适配。
- `src/core/json_writer.cpp`：JSON 序列化。
- `src/cli/main.cpp`：CLI 参数解析与命令分发。
- `src/capi/ispcok_capi.cpp`：C ABI 包装与内存边界。

## 插件 ABI 约定

头文件：`include/ispcok/plugin_api.h`

### Host 侧假设

- 插件导出 `ispcok_get_module_v1`。
- 从 `IsPcOkPluginModuleV1` 读取 `id` / `category`。
- `run()` 填充 `IsPcOkPluginResultV1`，成功返回 `0`。
- Host 在 `run()` 内立即拷贝 `message/metrics` 到宿主内存。

### 插件作者生命周期要求

- `id` / `category` 在插件加载期内必须有效。
- `message` 与 `metrics` 至少保持有效到同一线程下一次调用该模块的 `run()`，或模块卸载。
- `metric_count` 必须与可访问数组长度一致。
- 不要返回未来异步阶段才会访问的裸指针。
- 共享设备上下文需自行串行化；每线程结果存储可避免并发覆盖 `message/metrics`。

### 安全加固现状

- 已实现：
  - 非零 `metric_count` + 空 `metrics` 拦截
  - `metric_count` 上限限制
  - 非有限值（NaN/Inf）拦截
- 规划中：
  - 更强所有权模型的 ABI v2
  - 更完善的异常隔离策略

## GPU / 加速器插件

`gpu_vulkan`、`cuda`、`xpu`、`hip` 使用独立动态插件，`ispcok_core` 不链接 GPU SDK。插件存在时，同 id 插件覆盖内建降级模块；插件缺席时，模块返回 `not_supported` 和 `backend not compiled` 消息。

四个插件执行同一组 1024 x 1024 FP32 矩阵乘法，并报告：

- `fp32_gflops`
- `elapsed_ms`
- `checksum`

CMake 开关：

- `ISPCOK_ENABLE_GPU_BACKENDS`
- `ISPCOK_ENABLE_VULKAN_BACKEND`
- `ISPCOK_ENABLE_CUDA_BACKEND`
- `ISPCOK_ENABLE_XPU_BACKEND`
- `ISPCOK_ENABLE_HIP_BACKEND`

SDK 缺失会跳过对应插件目标，CMake 配置继续成功。

运行 Vulkan 插件：

```bash
cmake -S . -B build -DISPCOK_ENABLE_VULKAN_BACKEND=ON
cmake --build build --target ispcok_cli ispcok_plugin_gpu_vulkan --parallel 2
./build/ispcok_cli run --plugin-dir ./build/plugins --modules gpu_vulkan
```

Linux 无物理 GPU 环境可使用 Mesa lavapipe 验证真实 Vulkan compute 路径：

```bash
bash scripts/test_vulkan_plugin_lavapipe.sh
```

Debian / Ubuntu 环境需要 `libvulkan-dev` 与 `mesa-vulkan-drivers`。

## 场景规则

场景定义采用表驱动，集中在 `src/core/engine.cpp`，由以下接口共用：

- `ListScenarios()`
- 运行期评分逻辑

当前场景：

- `game_engine`
- `maa`
- `llm_infer_server`

## CLI 行为

当前行为：

- 未知参数直接失败并返回非零。
- 缺失参数值直接失败。
- 未知场景直接失败。
- 指定未知模块直接失败。

## JSON 输出

`src/core/json_writer.cpp` 目前转义：

- `\`、`"`、`\n`、`\r`、`\t`
- 全部控制字符 `0x00-0x1F`（`\u00XX`）

## 新模块编码规则

- 模块 `id` 使用稳定的 snake_case 小写命名。
- `status` 只使用：`ok`、`not_supported`、`not_implemented`、`error`。
- `message` 需可读且可定位问题。
- 默认测试时长应短；长压测应显式开启。
- 指标需机器可读且带单位语义（`mibps`、`ns_per_access`、`avg_rtt_ms` 等）。

## 测试

- 单元测试：`tests/ispcok_tests.cpp`
- 运行命令（Windows）：

```powershell
cmake --build build --config Release --target ispcok_tests
ctest --test-dir build -C Release --output-on-failure
```

- 运行命令（Linux / macOS）：

```bash
cmake --build build --target ispcok_tests
ctest --test-dir build --output-on-failure
```

- 已包含插件负面样例测试（错误返回结构、非法值等），Windows / Linux / macOS 均可运行。
