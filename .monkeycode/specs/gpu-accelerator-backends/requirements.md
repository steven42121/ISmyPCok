# Requirements Document

## Introduction

The ISmyPCok benchmark currently ships four GPU/accelerator modules (gpu_vulkan, cuda, xpu, hip) as `not_implemented` placeholders that always return score 0 with a "Planned: ..." message. This feature replaces those placeholders with real accelerator backends that execute actual compute benchmarks on available GPUs, and degrades gracefully to a `not_supported` status with an explanatory message when the corresponding SDK or a physical device is absent. The system must remain fully buildable, testable and functional in environments without GPUs or GPU SDKs, including the project CI runner and the local development container.

The feature covers four backends: Vulkan (gpu_vulkan), CUDA (cuda), oneAPI/Level Zero (xpu) and HIP (hip). It does not cover gpu_dx12 and npu, which remain `not_implemented` placeholders.

Key architecture decisions confirmed by the user:

- **Integration model**: Each real backend is compiled as a standalone dynamic plugin (.dll/.so). The ispcok_core library never links GPU SDKs. When a backend SDK is absent, the corresponding plugin is simply not built and the module remains a degraded placeholder.
- **Compute workload**: Each backend executes a single generic compute benchmark — a matrix multiplication / FP32 throughput kernel producing metrics such as FP32 GFLOPS and elapsed time. The kernel is identical across backends for cross-backend comparability.
- **GPU-less verification**: The Vulkan backend executes on the Mesa lavapipe (llvmpipe) software device in CI and the local container to assert real execution with real metrics. CUDA, HIP and xpu backends are verified via degradation-path unit tests only.

## Glossary

- **Accelerator backend module**: A plugin in ISmyPCok that measures compute or throughput performance of a GPU/accelerator, identified by id gpu_vulkan, cuda, xpu or hip. Real backends are shipped as dynamic plugins; the builtin modules remain placeholders.
- **Module status**: A string field in ModuleResult. Values used by this feature: `ok` (benchmark executed successfully), `not_supported` (module intentionally skipped with an explanatory message), `not_implemented` (module not yet built), `error` (module failed at runtime).
- **SDK presence**: The availability of the required headers/libraries for a backend (Vulkan headers, CUDA toolkit, Level Zero loader, HIP runtime) at build time.
- **Device presence**: The availability of a usable physical accelerator at run time (Vulkan physical device, CUDA device, Level Zero driver/device, HIP device).
- **Graceful degradation**: The behavior of a backend module that returns `not_supported` with a precise message instead of failing the run, crashing, or returning fabricated scores when its SDK or device is unavailable.
- **Mock plugin**: The existing sample plugin ispcok_plugin_gpu_vulkan_sample that currently overrides the gpu_vulkan module.

## Requirements

### Requirement 1: Real Vulkan backend

**User Story:** AS a user, I want the gpu_vulkan module to run a real Vulkan compute benchmark, so that I obtain genuine GPU throughput measurements instead of a placeholder.

#### Acceptance Criteria

1. WHEN the build configures with Vulkan SDK headers available, the system SHALL build the gpu_vulkan plugin as a real compute backend.
2. WHEN the gpu_vulkan plugin runs and detects at least one usable Vulkan physical device, the plugin SHALL execute the generic FP32 matrix multiplication kernel on that device and report real metrics and status `ok`.
3. WHEN the gpu_vulkan plugin runs and detects no usable Vulkan physical device, the plugin SHALL report status `not_supported`, score 0, and a message stating that no Vulkan device is available.
4. WHEN the gpu_vulkan plugin is not built because the Vulkan SDK is absent, the builtin gpu_vulkan module SHALL report status `not_supported`, score 0, and a message stating that the backend was not compiled.
5. WHEN the gpu_vulkan plugin runs on a Vulkan software device (for example lavapipe), the plugin SHALL still execute the kernel and report real non-zero metrics.

### Requirement 2: Real CUDA backend

**User Story:** AS a user, I want the cuda module to run a real CUDA compute benchmark, so that I obtain genuine NVIDIA GPU measurements.

#### Acceptance Criteria

1. WHEN the build configures with CUDA toolkit headers available, the system SHALL build the cuda plugin as a real compute backend.
2. WHEN the cuda plugin runs and detects at least one CUDA-capable device, the plugin SHALL execute the generic FP32 matrix multiplication kernel on that device and report real metrics and status `ok`.
3. WHEN the cuda plugin runs and detects no CUDA-capable device, the plugin SHALL report status `not_supported`, score 0, and a message stating that no CUDA device is available.
4. WHEN the cuda plugin is not built because the CUDA toolkit is absent, the builtin cuda module SHALL report status `not_supported`, score 0, and a message stating that the backend was not compiled.

### Requirement 3: Real oneAPI/Level Zero backend

**User Story:** AS a user, I want the xpu module to run a real Level Zero compute benchmark, so that I obtain genuine Intel accelerator measurements.

#### Acceptance Criteria

1. WHEN the build configures with Level Zero loader headers available, the system SHALL build the xpu plugin as a real compute backend.
2. WHEN the xpu plugin runs and detects at least one Level Zero driver with a usable device, the plugin SHALL execute the generic FP32 matrix multiplication kernel on that device and report real metrics and status `ok`.
3. WHEN the xpu plugin runs and detects no Level Zero device, the plugin SHALL report status `not_supported`, score 0, and a message stating that no Level Zero device is available.
4. WHEN the xpu plugin is not built because the Level Zero loader is absent, the builtin xpu module SHALL report status `not_supported`, score 0, and a message stating that the backend was not compiled.

### Requirement 4: Real HIP backend

**User Story:** AS a user, I want the hip module to run a real HIP compute benchmark, so that I obtain genuine AMD GPU measurements.

#### Acceptance Criteria

1. WHEN the build configures with HIP runtime headers available, the system SHALL build the hip plugin as a real compute backend.
2. WHEN the hip plugin runs and detects at least one HIP-capable device, the plugin SHALL execute the generic FP32 matrix multiplication kernel on that device and report real metrics and status `ok`.
3. WHEN the hip plugin runs and detects no HIP-capable device, the plugin SHALL report status `not_supported`, score 0, and a message stating that no HIP device is available.
4. WHEN the hip plugin is not built because the HIP runtime is absent, the builtin hip module SHALL report status `not_supported`, score 0, and a message stating that the backend was not compiled.

### Requirement 5: Unified compute workload

**User Story:** AS a user, I want all four real backends to run the same generic FP32 matrix multiplication kernel, so that results are comparable across backends.

#### Acceptance Criteria

1. WHEN any real backend executes successfully, the backend SHALL perform FP32 matrix multiplication of the same fixed problem size across all backends.
2. WHEN any real backend executes successfully, the backend SHALL report at least the metrics FP32 GFLOPS and elapsed milliseconds.
3. WHEN any real backend executes successfully, the backend SHALL report status `ok` and a positive score proportional to FP32 GFLOPS.
4. WHEN the matrix multiplication result is incorrect on the device, the backend SHALL report status `error` with a message stating the result mismatch.

### Requirement 6: Unified device detection and degradation semantics

**User Story:** AS a user, I want all four accelerator modules to follow the same device-detection and failure-reporting semantics, so that run results are consistent and predictable.

#### Acceptance Criteria

1. WHEN any accelerator backend module detects no usable device at run time, the system SHALL return status `not_supported`, score 0, and a message identifying the missing device.
2. WHEN any accelerator backend module encounters an unexpected initialization or execution error, the system SHALL return status `error`, score 0, and a message describing the failure.
3. WHEN any accelerator backend module returns `not_supported` or `error`, the system SHALL exclude the module from scenario score aggregation the same way existing skipped modules are handled.
4. WHILE the run executes an accelerator module, the system SHALL bound the run duration so that a hanging driver probe cannot stall the whole benchmark run.

### Requirement 7: Metrics and scenario compatibility

**User Story:** AS a user, I want real backend results to provide metrics that the scenario scorer can consume, so that a GPU benchmark run produces a meaningful overall score.

#### Acceptance Criteria

1. WHEN an accelerator backend module runs successfully, the system SHALL report at least one numeric metric with the existing metric naming conventions used by builtin modules.
2. WHEN an accelerator backend module reports metrics, the system SHALL make those metrics available to the same scenario aggregation path used by builtin modules.
3. WHEN the real gpu_vulkan plugin is present in the plugin directory, the system SHALL use the real plugin result instead of the builtin gpu_vulkan placeholder.

### Requirement 8: Optional build integration

**User Story:** AS a maintainer, I want each real backend compiled only when its SDK is present, so that building the project never fails due to a missing GPU SDK.

#### Acceptance Criteria

1. WHEN CMake configuration detects a backend SDK, the system SHALL enable the corresponding real backend build.
2. WHEN CMake configuration does not detect a backend SDK, the system SHALL skip the corresponding real backend build and leave the module present in degraded mode without failing configuration.
3. WHEN a backend is disabled via an explicit CMake option, the system SHALL skip that backend build and leave the module in degraded mode.
4. WHEN the build includes at least one real backend, the system SHALL continue to produce all existing artifacts (core library, CLI, capi, sample plugins, pc_benchmark).

### Requirement 9: Build and regression safety on GPU-less environments

**User Story:** AS a maintainer, I want the feature to be buildable and testable on GPU-less machines, so that CI and local development continue to pass.

#### Acceptance Criteria

1. WHEN the project builds on a machine without any GPU SDK, the system SHALL complete configuration, build, and existing smoke tests successfully.
2. WHEN the project tests on a machine without any GPU device, the system SHALL exercise the degradation paths of all four accelerator modules and assert their `not_supported` status and message content.
3. WHEN the feature is enabled, the system SHALL keep the existing Windows and Linux build scripts and CI workflow functional.

### Requirement 10: Plugin override behavior preserved

**User Story:** AS a plugin author, I want the plugin override mechanism to keep working, so that third-party plugins can still replace builtin accelerator modules.

#### Acceptance Criteria

1. WHEN a plugin with id equal to a builtin accelerator module id is loaded, the system SHALL use the plugin module in place of the builtin module.
2. WHEN the mock gpu_vulkan sample plugin is loaded, the system SHALL use the mock plugin result instead of the builtin gpu_vulkan implementation.

### Requirement 11: Documentation update

**User Story:** AS a user, I want the supported backends, enablement options and degradation behavior documented, so that I know how to build and interpret accelerator results.

#### Acceptance Criteria

1. WHEN the feature is released, the system SHALL document each backend's support status, build-time enablement and runtime degradation behavior in the development documentation.
2. WHEN the feature is released, the system SHALL document the accelerator modules in the generated README module list with the same format used for builtin modules.
