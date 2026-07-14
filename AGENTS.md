# Repository Guidelines

## Project Structure & Module Organization

SatExplorer is a Windows C++/Qt 5.15 desktop application for SAR/InSAR processing. Most implementation files are at the repository root, with matching headers under `include/`. Qt Designer forms live in `ui/`, icons and stylesheets in `resources/`, and build output plus runtime DLLs in `bin/`. The node editor is implemented in `QtNodes/src/` and `include/QtNodes/internal/`; docking support is vendored under `ADS/`. `template_dem/` contains the companion DEM console process. Architecture and build details are documented in `CLAUDE.md`, `BUILD_NOTES.md`, and `workflow_porting_sop.md`.

## Build, Test, and Development Commands

Use a Visual Studio Developer PowerShell with the v143 toolset and Qt 5.15.2 integration configured. Visual Studio builds are the canonical workflow.

```powershell
msbuild SatExplorer.sln /m /p:Configuration=Debug /p:Platform=x64
msbuild SatExplorer.sln /m /p:Configuration=Release /p:Platform=x64
.\bin\SatExplorer.exe
```

Debug and Release both output to `bin/`; Debug dependencies use the `_d` suffix. Required SDKs include OpenCV, HDF5, GDAL, QCustomPlot, and ONNX Runtime. Do not enable C++17 globally: the project defaults to C++14, while only ONNX-specific translation units require C++17.

## Coding Style & Naming Conventions

Use four-space indentation, Allman-style braces, and existing Qt signal/slot patterns. Name classes and matching files in PascalCase, for example `CutNode.cpp` and `CutWorker.cpp`. Long-running processing belongs in module-specific `Worker`, `QRunnable`, or `QtConcurrent` code, not UI classes. Protect HDF5 and external-DLL access with `NodeUtils::Hdf5Locker`. Keep comments concise and preserve the surrounding file's language and style.

## Testing Guidelines

No automated test framework or coverage target is configured. Build Debug and manually exercise affected dialogs or nodes. For workflow changes, verify manual and automatic execution, progress/error states, save/load restoration, downstream invalidation, and generated H5/JPG outputs. Test nodes are in `TestNodes.cpp` and `include/TestNodes.h`.

## Commit & Pull Request Guidelines

Recent commits use short Chinese descriptions such as `导入优化、节点持久化及进度条平滑等`. Keep commits focused and describe the behavioral change directly. Pull requests should include scope, affected workflows, manual verification steps, linked issues, and screenshots for UI changes. New nodes must update `NodeModels.cpp`, `WorkflowUI::getPaletteFullOrder()`, `SatExplorer.vcxproj`, and `SatExplorer.vcxproj.filters`. Never commit credentials, local absolute paths, or populated `Config.ini` secrets.
