$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$thirdParty = Join-Path $root "third_party"
$cppBenchmark = Join-Path $thirdParty "CppBenchmark"

if (-not (Test-Path $thirdParty)) {
    New-Item -ItemType Directory -Path $thirdParty | Out-Null
}

if (-not (Test-Path $cppBenchmark)) {
    git clone --depth 1 https://github.com/chronoxor/CppBenchmark.git $cppBenchmark
}

Push-Location $cppBenchmark
try {
    if (-not (Test-Path "modules/Catch2")) {
        git clone --depth 1 --branch devel https://github.com/catchorg/Catch2.git modules/Catch2
    }
    if (-not (Test-Path "modules/cpp-optparse")) {
        git clone --depth 1 --branch master https://github.com/chronoxor/cpp-optparse.git modules/cpp-optparse
    }
    if (-not (Test-Path "modules/HdrHistogram")) {
        git clone --depth 1 --branch main https://github.com/HdrHistogram/HdrHistogram_c.git modules/HdrHistogram
    }
    if (-not (Test-Path "modules/zlib")) {
        git clone --depth 1 --branch master https://github.com/madler/zlib.git modules/zlib
    }
    if (-not (Test-Path "cmake")) {
        git clone --depth 1 --branch master https://github.com/chronoxor/CppCMakeScripts.git cmake
    }
}
finally {
    Pop-Location
}

Write-Host "Third-party sources are ready in $cppBenchmark"
