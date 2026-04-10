# Plugin ABI Notes (v1)

Header: `include/ispcok/plugin_api.h`
Entrypoint: `ispcok_get_module_v1`

## Minimal plugin shape

- Export `ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)`.
- Fill:
  - `id`
  - `category`
  - `run`

## Result contract

`run(IsPcOkPluginResultV1* out_result)` should:

- Return `0` on success, non-zero on failure.
- Fill:
  - `score`
  - `message` (nullable but recommended)
  - `metrics` array pointer
  - `metric_count`

## Ownership and lifetime

- `id` and `category` are treated as immutable plugin-owned strings valid for the entire module lifetime.
- `message` and `metrics` data must be valid at least until `run()` returns.
- Host copies result data immediately after `run()` returns.

## Defensive rules for plugin implementers

- Keep `metric_count` accurate.
- Avoid returning pointers to temporary stack memory.
- Avoid mutating static buffers concurrently unless synchronized.
- Prefer deterministic metric names and units.

## Compatibility guidance

- Do not change symbol names or struct field order for ABI v1.
- If incompatible changes are needed, add a new versioned entrypoint (example: `ispcok_get_module_v2`).
