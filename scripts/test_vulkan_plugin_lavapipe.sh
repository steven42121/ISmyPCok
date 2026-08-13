#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-lavapipe}"

if ! command -v cmake >/dev/null 2>&1; then
    printf '%s\n' "cmake is required" >&2
    exit 1
fi

if [[ ! -f /usr/include/vulkan/vulkan.h ]]; then
    printf '%s\n' "Vulkan headers are required (Debian/Ubuntu package: libvulkan-dev)" >&2
    exit 1
fi

ICD_FILE="${VK_ICD_FILENAMES:-}"
if [[ -z "${ICD_FILE}" ]]; then
    for candidate in /usr/share/vulkan/icd.d/lvp_icd.x86_64.json /usr/share/vulkan/icd.d/lvp_icd.i686.json; do
        if [[ -f "${candidate}" ]]; then
            ICD_FILE="${candidate}"
            break
        fi
    done
fi

if [[ -z "${ICD_FILE}" || ! -f "${ICD_FILE}" ]]; then
    printf '%s\n' "lavapipe ICD was not found (Debian/Ubuntu package: mesa-vulkan-drivers)" >&2
    exit 1
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DISPCOK_ENABLE_GPU_BACKENDS=ON \
    -DISPCOK_ENABLE_VULKAN_BACKEND=ON \
    -DISPCOK_ENABLE_CUDA_BACKEND=OFF \
    -DISPCOK_ENABLE_XPU_BACKEND=OFF \
    -DISPCOK_ENABLE_HIP_BACKEND=OFF

cmake --build "${BUILD_DIR}" --target ispcok_cli ispcok_plugin_gpu_vulkan --parallel 2

PLUGIN_DIR="${BUILD_DIR}/plugins"
if [[ ! -f "${PLUGIN_DIR}/ispcok_plugin_gpu_vulkan.so" ]]; then
    printf '%s\n' "Vulkan plugin build output was not found in ${PLUGIN_DIR}" >&2
    exit 1
fi

RESULT_JSON="$(VK_ICD_FILENAMES="${ICD_FILE}" "${BUILD_DIR}/ispcok_cli" run \
    --plugin-dir "${PLUGIN_DIR}" \
    --modules gpu_vulkan)"

RESULT_JSON="${RESULT_JSON}" python3 - <<'PY'
import json
import os

report = json.loads(os.environ["RESULT_JSON"])
modules = report.get("modules", [])
assert len(modules) == 1, f"expected one module, got {len(modules)}"
module = modules[0]
assert module.get("id") == "gpu_vulkan", module
assert module.get("plugin") is True, module
assert module.get("status") == "ok", module
assert module.get("score", 0) > 0, module
assert module.get("metrics", {}).get("fp32_gflops", 0) > 0, module
print(json.dumps(module, indent=2, sort_keys=True))
PY
