# ISmyPCok

基于 [chronoxor/CppBenchmark](https://github.com/chronoxor/CppBenchmark) 的桌面电脑性能基准工具（C++ / CMake）。

## 当前已实现

- `cpu.integer_hash`：整数混洗与位运算吞吐
- `cpu.floating_point`：浮点三角函数与开方计算
- `memory.memcpy`：不同块大小下的拷贝带宽
- `memory.vector_scan`：不同数据规模下的内存扫描
- `threads.atomic_increment`：不同线程数下的原子递增吞吐

## 目录结构

- `3rd_party/CppBenchmark`：第三方依赖源码（按第三方接入）
- `src/pc_benchmark.cpp`：基准定义
- `scripts/fetch_3rd_party.ps1`：重新拉取第三方依赖

## 快速开始（Windows / Visual Studio）

```powershell
cmake -S . -B build
cmake --build build --config Release --target pc_benchmark
.\build\Release\pc_benchmark.exe -f "cpu.integer_hash" -q
```

## 常用命令

```powershell
# 运行全部基准（时间会较长）
.\build\Release\pc_benchmark.exe -f ".*" -q

# 只跑 CPU 基准
.\build\Release\pc_benchmark.exe -f "cpu.*" -q

# 只跑内存基准
.\build\Release\pc_benchmark.exe -f "memory.*" -q

# 输出 JSON 报告
.\build\Release\pc_benchmark.exe -f ".*" -q -o json
```

## 自动构建 Release（GitHub Actions）

- 发布 GitHub Release（`published`）后，仓库会自动触发 `.github/workflows/release.yml`。
- 工作流会在 `windows-latest` 构建 `Release` 版本，并上传压缩包到当前 Release Assets：
  - `pc_benchmark-<tag>-windows-x64.zip`

## 第三方依赖说明

`CppBenchmark` 本仓库本身依赖额外模块（`Catch2` / `zlib` / `HdrHistogram` / `cpp-optparse` 以及 `cmake` 脚本）。  
如果你把 `3rd_party/CppBenchmark` 删除了，可执行：

```powershell
.\scripts\fetch_3rd_party.ps1
```

再重新执行 CMake 构建命令即可。
