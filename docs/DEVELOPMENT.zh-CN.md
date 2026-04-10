# 开发指南

## 运行时模型

当前仓库包含两条可执行链路：

1. `pc_benchmark`：
- 基于 `CppBenchmark` 宏的传统单体基准程序。
- 用于底层基准实验与快速验证。

2. `ispcok_core` 体系：
- `ispcok_cli`：命令行入口。
- `ispcok_capi`：稳定 C ABI 入口。
- 可选 `.dll` 插件运行时加载。
- 场景评分逻辑在 `src/core/engine.cpp`。

两条链路并行存在是有意设计。新特性建议优先在 `ispcok_core` 落地。

## 构建目标

- `ispcok_core`：核心引擎、内建模块、插件加载、JSON 输出。
- `ispcok_cli`：CLI 入口。
- `ispcok_capi`：C ABI 动态库。
- `ispcok_plugin_sample_gpu`：插件示例。
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
- `message` 与 `metrics` 至少在 `run()` 返回前保持有效。
- `metric_count` 必须与可访问数组长度一致。
- 不要返回未来异步阶段才会访问的裸指针。

### 安全加固现状

- 已实现：
  - 非零 `metric_count` + 空 `metrics` 拦截
  - `metric_count` 上限限制
  - 非有限值（NaN/Inf）拦截
- 规划中：
  - 更强所有权模型的 ABI v2
  - 更完善的异常隔离策略

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
- 运行命令：

```powershell
cmake --build build --config Release --target ispcok_tests
ctest --test-dir build -C Release --output-on-failure
```

- 已包含插件负面样例测试（错误返回结构、非法值等）。
