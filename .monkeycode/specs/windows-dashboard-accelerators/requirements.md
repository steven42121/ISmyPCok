# Requirements Document

## Introduction

This feature upgrades the Windows desktop application from a raw JSON viewer to a card-based benchmark dashboard and replaces the remaining `gpu_dx12` and `npu` placeholders with real Windows accelerator plugins. The dashboard presents scenario score, bottlenecks, module status, score, messages, and metrics while preserving access to the raw report. The new backends remain isolated dynamic plugins so `ispcok_core` keeps its SDK-free architecture.

## Glossary

- **Dashboard**: The WinUI result surface that renders scenario and module results as cards.
- **DX12 backend**: The `gpu_dx12` plugin that executes an FP32 matrix multiplication compute workload on a Direct3D 12 adapter.
- **NPU backend**: The `npu` plugin that executes an FP16 DirectML workload on a DXCore machine-learning adapter.
- **Hardware result**: A benchmark result produced by a physical GPU or NPU adapter.
- **Diagnostic adapter**: A software adapter used only for automated correctness validation, such as D3D12 WARP.
- **Graceful degradation**: A result with status `not_supported`, score 0, and a precise reason when the required API or hardware is unavailable.

## Requirements

### Requirement 1: Card-based benchmark dashboard

**User Story:** AS a Windows user, I want benchmark results displayed as cards, so that I can understand system performance without reading raw JSON.

#### Acceptance Criteria

1. WHEN a benchmark completes with a valid report, the WinUI application SHALL display one card for every returned module.
2. WHEN a module card is displayed, the WinUI application SHALL show the module id, category, status, score, message, plugin indicator, and numeric metrics.
3. WHEN a report contains a scenario result, the WinUI application SHALL display the scenario id, total score, and bottlenecks in a summary card.
4. WHEN the application window width changes, the dashboard SHALL wrap module cards and preserve vertical scrolling.
5. WHEN the user expands the raw result section, the WinUI application SHALL display the complete JSON report.

### Requirement 2: Dashboard states and resilience

**User Story:** AS a Windows user, I want clear run and error states, so that I know whether the benchmark is ready, running, completed, or failed.

#### Acceptance Criteria

1. WHILE a benchmark runs, the WinUI application SHALL disable the run action and display active progress feedback.
2. WHEN a benchmark succeeds, the WinUI application SHALL restore the run action and display a completion status.
3. IF the C API returns an error or empty result, the WinUI application SHALL restore the run action and display a precise error status.
4. IF report parsing fails, the WinUI application SHALL display the parse error and expose the raw report for diagnosis.
5. IF the window closes before a background run completes, the application SHALL discard the pending UI update safely.

### Requirement 3: Report parser compatibility

**User Story:** AS a maintainer, I want a typed dashboard report parser, so that UI rendering remains independent from JSON field ordering.

#### Acceptance Criteria

1. WHEN the parser receives a valid core report, the parser SHALL preserve every module and metric value.
2. WHEN the parser receives a report without a scenario object, the parser SHALL return a valid dashboard report with no scenario summary.
3. WHEN the parser encounters unknown additional fields, the parser SHALL preserve compatibility by ignoring those fields.
4. IF a required field is missing or has an incompatible type, the parser SHALL return a descriptive parse error.

### Requirement 4: Preserve the known-good WinUI startup chain

**User Story:** AS a Windows user, I want the dashboard upgrade to retain the proven application startup path, so that the window opens reliably.

#### Acceptance Criteria

1. WHEN the dashboard is implemented, the application SHALL retain the generated `AppT` and `MainWindowT` XAML types.
2. WHEN the application launches, `App` and `MainWindow` SHALL continue to initialize through `InitializeComponent()`.
3. WHEN the release ZIP is tested, the WinUI executable SHALL create a top-level window and remain active for the stability interval.

### Requirement 5: Real Direct3D 12 backend

**User Story:** AS a Windows user, I want a real Direct3D 12 compute benchmark, so that I can measure the native Windows GPU path.

#### Acceptance Criteria

1. WHEN the project builds on Windows with the Direct3D 12 SDK available, the system SHALL build an independent `gpu_dx12` plugin.
2. WHEN a usable hardware adapter is available, the plugin SHALL execute the shared FP32 matrix multiplication workload and validate the result checksum.
3. WHEN execution succeeds, the plugin SHALL report status `ok`, a positive score, `fp32_gflops`, `elapsed_ms`, and `checksum` metrics.
4. WHEN no usable hardware adapter exists, the plugin SHALL report status `not_supported`, score 0, and an explanatory message.
5. WHERE the diagnostic WARP option is enabled, the plugin SHALL permit WARP execution for automated correctness validation and identify the result as diagnostic software execution.

### Requirement 6: Real Windows NPU backend

**User Story:** AS a Windows user with an NPU, I want a real DirectML benchmark, so that I can measure dedicated machine-learning acceleration.

#### Acceptance Criteria

1. WHEN the project builds on Windows with DXCore and DirectML headers available, the system SHALL build an independent `npu` plugin.
2. WHEN a compatible non-graphics machine-learning adapter is available, the plugin SHALL create a DirectML device on that adapter and execute a fixed FP16 matrix multiplication workload.
3. WHEN execution succeeds, the plugin SHALL report status `ok`, a positive score, `fp16_gflops`, `elapsed_ms`, and `checksum` metrics.
4. WHEN Windows NPU APIs, a compatible adapter, or the required DirectML feature level are unavailable, the plugin SHALL report status `not_supported`, score 0, and an explanatory message.
5. WHEN selecting an adapter, the plugin SHALL accept a machine-learning or core-compute adapter and SHALL require the adapter to lack graphics capability.
6. IF the selected device fails during initialization, execution, synchronization, or result validation, the plugin SHALL report status `error` with diagnostic context.

### Requirement 7: Plugin isolation and degradation

**User Story:** AS a maintainer, I want Windows accelerator SDKs isolated in plugins, so that all existing builds remain portable.

#### Acceptance Criteria

1. WHEN the Windows plugins are enabled, `ispcok_core` SHALL remain free of Direct3D 12, DXCore, and DirectML link dependencies.
2. WHEN building on a non-Windows platform, the system SHALL skip both Windows plugins and complete the existing build and tests.
3. WHEN either plugin is absent, the corresponding builtin module SHALL report `not_supported` with a backend-not-compiled message.
4. WHEN a Windows plugin is loaded, the plugin SHALL override the builtin module with the same id through ABI v1.

### Requirement 8: Automated verification and release packaging

**User Story:** AS a maintainer, I want automated build and packaging checks, so that the combined feature ships as a complete Windows release.

#### Acceptance Criteria

1. WHEN Windows CI builds the project, the workflow SHALL compile the DX12 and NPU plugins.
2. WHEN Windows CI validates DX12 correctness, the workflow SHALL execute the DX12 plugin with WARP and verify a valid checksum and positive throughput.
3. WHEN Windows CI runs without NPU hardware, the workflow SHALL verify that the NPU plugin loads and returns a precise `not_supported` result.
4. WHEN a Windows release is packaged, the workflow SHALL include the new plugins and apply Authenticode signing when signing credentials are configured.
5. WHEN the final WinUI ZIP is tested, the workflow SHALL verify window startup and the presence of required runtime and plugin files.
