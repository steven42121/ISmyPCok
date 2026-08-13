# ISmyPCok.WinUI

WinUI3 desktop front-end for ISmyPCok, written in C++/WinRT. Benchmark reports are rendered as a card dashboard with scenario score, bottlenecks, module status, metrics, and expandable raw JSON.

It loads `ispcok_capi.dll` at runtime (via `LoadLibraryW`) and runs benchmarks
on a background thread, pushing results back to the UI through a
`DispatcherQueue`. No MSIX packaging: the app is self-contained and unpackaged
so the built `exe` plus the CAPI dll can be distributed directly.

## Requirements

- Visual Studio 2022 (v143 toolset)
- NuGet packages restored automatically:
  - `Microsoft.WindowsAppSDK` 1.5.x
  - `Microsoft.Windows.CppWinRT` 2.0.x
  - `Microsoft.Windows.SDK.BuildTools`

## Build

1. Build the CAPI DLL (Release, x64) first:

   ```powershell
   cmake -S . -B build -A x64
   cmake --build build --config Release --target ispcok_capi
   ```

2. Open `ISmyPCok.WinUI.vcxproj` in Visual Studio 2022 and build the
   `Release | x64` configuration. A post-build step copies `ispcok_capi.dll`
   from the repo-level `build\Release` folder next to the exe.

3. Run `ISmyPCok.WinUI.exe`. The header shows the CAPI version (e.g.
   `26h2-0810`); pick a scenario, optionally restrict modules, and press Run.

## Notes

- If `ispcok_capi.dll` is missing at startup the app shows a warning and stays
  usable but cannot run benchmarks.
- `app.manifest` enables `PerMonitorV2` DPI awareness for the unpackaged app.
