#!/usr/bin/env bash
# Fetch third-party CppBenchmark dependency tree (Linux / macOS).
# Mirror of scripts/fetch_3rd_party.ps1 for POSIX shells.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY="${ROOT}/third_party"
CPP_BENCHMARK="${THIRD_PARTY}/CppBenchmark"

mkdir -p "${THIRD_PARTY}"

if [[ ! -d "${CPP_BENCHMARK}" ]]; then
    git clone --depth 1 https://github.com/chronoxor/CppBenchmark.git "${CPP_BENCHMARK}"
fi

cd "${CPP_BENCHMARK}"

clone_if_missing() {
    local name="$1"
    local branch="$2"
    local url="$3"
    if [[ ! -d "${name}" ]]; then
        git clone --depth 1 --branch "${branch}" "${url}" "${name}"
    fi
}

clone_if_missing "modules/Catch2" "devel" "https://github.com/catchorg/Catch2.git"
clone_if_missing "modules/cpp-optparse" "main" "https://github.com/chronoxor/cpp-optparse.git"
clone_if_missing "modules/HdrHistogram" "main" "https://github.com/HdrHistogram/HdrHistogram_c.git"
clone_if_missing "modules/zlib" "master" "https://github.com/madler/zlib.git"
clone_if_missing "cmake" "master" "https://github.com/chronoxor/CppCMakeScripts.git"

echo "Third-party sources are ready in ${CPP_BENCHMARK}"
