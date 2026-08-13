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

### Status mapping

ABI v1 has no explicit `status` field. The host uses this mapping:

- Non-zero return code: `error`. When the plugin provides a non-empty `message`, the host uses it; otherwise it uses the generic plugin error message.
- Return code `0`, finite zero/negative `score`, and a non-empty `message`: `not_supported`. GPU plugins use this convention for missing SDKs, drivers, or devices.
- Return code `0` and a positive `score`: `ok`.

`not_supported` results still pass the structural metric guardrails; return an empty metrics array with `metric_count = 0`.

## Ownership and lifetime

- `id` and `category` are treated as immutable plugin-owned strings valid for the loaded module lifetime.
- `message` and `metrics` data must remain valid until the next `run()` call for the module on the same thread, or until module unload.
- The host copies plugin result data immediately after `run()` returns.

## Defensive rules for plugin implementers

- Keep `metric_count` accurate.
- Do not return pointers to temporary stack memory.
- Synchronize mutable static buffers if used concurrently.
- Prefer deterministic metric names and units.

## Compatibility guidance

- Do not change symbol names or struct field order for ABI v1.
- For incompatible changes, add a new versioned entrypoint (for example `ispcok_get_module_v2`).
