# 插件 ABI 说明（v1）

头文件：`include/ispcok/plugin_api.h`  
入口符号：`ispcok_get_module_v1`

## 最小插件形态

- 导出 `ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)`。
- 填充：
  - `id`
  - `category`
  - `run`

## 结果约定

`run(IsPcOkPluginResultV1* out_result)` 需满足：

- 成功返回 `0`，失败返回非零。
- 填充：
  - `score`
  - `message`（可空，但建议提供）
  - `metrics` 数组指针
  - `metric_count`

## 所有权与生命周期

- `id` 与 `category` 视为插件持有，需在模块加载生命周期内有效。
- `message` 与 `metrics` 至少在 `run()` 返回前保持有效。
- Host 在 `run()` 内立即拷贝结果数据。

## 插件实现防御建议

- 保证 `metric_count` 与真实可访问长度一致。
- 不要返回栈上临时内存指针。
- 若使用可变静态缓冲区，需自行并发同步。
- 指标名与单位建议保持稳定可比较。

## 兼容性建议

- ABI v1 下不要改符号名与结构体字段顺序。
- 若需要不兼容升级，请新增版本化入口（例如 `ispcok_get_module_v2`）。
