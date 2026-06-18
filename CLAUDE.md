# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**SatExplorer** - A Windows desktop application for InSAR (Interferometric Synthetic Aperture Radar) data processing and visualization. Built with C++ and Qt 5.15.2.

## Build Commands

Output directory: `bin/`
- Debug: `SatExplorer.exe` with `_d.dll` suffix libraries
- Release: `SatExplorer.exe` without debug suffix

User always builds by himself in another Visual Studio environment. Don't try to build after coding.

## Architecture

### High-Level Design

The application follows a Qt signal/slot, worker-thread, and interface-switching architecture:

```
MainWindow / WorkspaceUI / WorkflowUI
         ↓
Dialogs / Workflow Nodes
         ↓
Module-specific Workers / QRunnable Tasks (Thread Pool) / QtConcurrent Tasks
         ↓
External Processing DLLs + project XML/tree updates
```

### Key Components

**MainWindow** (`MainWindow.h/cpp`)
- Top-level window and central coordinator.
- Manages project open/save state, interface switching, shared project context, and project tree refresh.
- Coordinates updates from Workspace dialogs, Workflow nodes, and module-specific workers.

**WorkspaceUI** (`include/WorkspaceUI.h/cpp`)
- Traditional tree/menu/dialog based interface.
- Hosts legacy project-tree workflows while sharing project context with Workflow UI.

**WorkflowUI** (`include/WorkflowUI.h/cpp`)
- Node-based visual workflow editor built on QtNodes and ADS dock widgets.
- Owns the node scene, palette, node library, logger/detail panels, and workflow save/load integration.

**Module-specific Workers** (`*Worker.h/cpp`)
- Long-running processing is implemented in dedicated `QObject` workers such as `S1TopsBackGeocodingWorker`, `CutWorker`, `GeocodingWorker`, `Sentinel1ImportWorker`, etc.
- Workers are moved to `QThread` by dialogs or workflow nodes and expose standard signals such as `updateProcess`, `endProcess`, `errorProcess`, and `sendModel` where needed.
- Do not add new business logic to a global `MyThread` path; new/ported functionality should use module-specific workers or narrowly scoped `QtConcurrent::run` tasks.

**Background Tasks** (`*Task.h/cpp` / `*Task.cpp`)
- Heavy tasks (such as target detection, BM3D enhancement, and generic SAR import) can also be implemented as subclasses of `QRunnable` (and optionally `QObject` to support signals) and dispatched to `QThreadPool::globalInstance()`.


**Workflow Nodes** (`*Node.h/cpp`)
- Processing nodes generally inherit from `ExecutableNodeDelegateModel`.
- Import/source nodes generally inherit from `ImportNodeBase` and are usually manual by default because they require user-selected files.
- Nodes are responsible for embedded parameter widgets, port data, execution state, automatic/manual mode behavior, output restoration, and downstream data propagation.

**NodeUtils** (`include/NodeUtils.h`, `NodeUtils.cpp`)
- Shared utilities for project context lookup, overwrite checks, project DataNode cleanup, HDF5 locking, and H5-to-JPG preview generation.
- Use `NodeUtils::Hdf5Locker` around H5/external-DLL read/write critical sections.

**Project Management**
- Projects are stored as XML files with `.insar` extension; path checks should be case-insensitive.
- Uses TinyXML library for XML parsing and updates.
- TreeView displays hierarchical structure: Projects → Data Nodes → Data Files.
- Custom `TreeView` widget extends `QTreeView` with project-specific behavior.

**IPC** (`InSAR_IPC.h/cpp`)
- Windows shared memory-based inter-process communication.
- Used to communicate with `template_dem` console subprocess.
- Event-based synchronization.

### External Processing DLLs

Actual InSAR processing is done by external DLLs loaded from `bin/`:
- `FormatConversion_d.dll` - Data format conversion
- `ComplexMat_d.dll` - Complex matrix operations
- `Deflat_d.dll` - Deflation processing
- `Filter_d.dll` - Phase filtering
- `Registration_d.dll` - Image registration
- `Unwrap_d.dll` - Phase unwrapping
- `Dem_d.dll` - DEM generation
- `SBAS_d.dll` - SBAS time series analysis
- `Utils_d.dll` - Utility functions
- `Evaluation_d.dll` - Evaluation metrics

### UI Architecture

- Qt Designer `.ui` files in `ui/` directory support traditional dialogs and windows.
- Each processing module may have a dedicated dialog (e.g., `Filter_ui`, `Unwrap_ui`, `import_sentinel`) and/or a Workflow node.
- Custom widgets include `ColorBar`, `ImageView`, node detail overlays, palette/tree widgets, and ADS dock panels.
- QCustomPlot / Qt Charts are used for plotting and baseline visualization.

### Data Flow Patterns

Workspace flow:
```
User Action → Dialog UI → XxxWorker on QThread → External DLL
    → Dialog/MainWindow UI update → project tree/XML update
```

Workflow flow:
```
Node input data → prepareToStart() → executeProcessing()
    → XxxWorker on QThread or QtConcurrent task
    → NodeData outputs → downstream nodes + project tree/XML update
```

### Solution Structure

The Visual Studio solution contains two projects:
1. **SatExplorer** - Main GUI application (`SatExplorer.exe`)
2. **template_dem** - Console subprocess for DEM processing (uses IPC)

## Code Organization

- `include/` - Header files
- `ui/` - Qt Designer UI files
- `bin/` - Build output, executable, and DLL dependencies
- `template_dem/` - Console subprocess for DEM operations
- `QtNodes/src/` and `include/QtNodes/internal/` - QtNodes integration and workflow execution extensions
- `ADS/` - Qt Advanced Docking System integration
- `BM3D_cpp/` - C++ implementation of the BM3D (3D block-matching) denoising/enhancement algorithm

## Key Processing Modules

**Import:** Sentinel-1, TerraSAR-X, COSMO-SkyMed, ALOS-2, Generic SAR, AIRSAT, Biomass L1A, Hongtu-1 (HTHT), LuTan-1 (LUTAN), LiDAR, and Fucheng-1/Spacety

**Sentinel-1 Tools:** Deburst, TOPS Back-geocoding, Frame/swath merging

**InSAR Processing:** Baseline estimation/formation/preview, Interferogram formation, Phase filtering, Phase unwrapping, DEM generation

**SBAS/DInSAR:** Time series analysis, Reference point re-selection, Deformation visualization, KML export

**General:** AOI cropping, Registration/Coregistration, Geocoding, Coordinate transformation, BM3D image denoising/enhancement

**Evaluation/Display:** Image display, single/batch target detection, clutter suppression, speckle denoise, ENL/SCR evaluation, logger/note nodes

## Dependencies

**Qt:** 5.15.2 (core, gui, widgets, charts)
**External Libraries:** OpenCV, HDF5, GDAL, TinyXML, QCustomPlot, ONNX Runtime
**Compiler:** Visual Studio 2019/2022, Platform Toolset v143. Project uses C++14 by default, but C++17 is required for ONNX Runtime API files.
**OpenMP:** Enabled for parallel processing

## UI Architecture Updates (2026)

### Single Main Window with Interface Switching

Current UI architecture supports a welcome screen and single-window interface switching:

```
WelcomeScreen (startup) → MainWindow (single top-level)
                           ↓
                    InterfaceManager → switches between:
                           ↓
                    WorkspaceUI (traditional tree-based interface)
                        OR
                    WorkflowUI (node-based visual workflow editor)
```

**Core interface files:**
- `include/IApplicationInterface.h` - Abstract interface for switchable interfaces and shared project context.
- `include/InterfaceManager.h/cpp` - Manages interface switching and persistence.
- `include/WelcomeScreenUI.h/cpp` - VS Code-style welcome screen with new/open/recent projects.
- `include/WorkspaceUI.h/cpp` - Encapsulates traditional workspace interface.
- `include/WorkflowUI.h/cpp` - Encapsulates node-based workflow interface.

**Persistence:**
- Global default interface stored in `Config.ini` at `Interface/Default`.
- Per-project last used interface stored in project XML at `project/lastInterface`.
- Opening a project restores the last used interface.

### Node-Based Workflow Editor

The project includes a visual node-based workflow editor built on top of QtNodes:

**Key Files:**
- `include/WorkflowUI.h/cpp` - Main workflow UI container.
- `include/NodeModels.h`, `NodeModels.cpp` - Node model registration.
- `include/ImportNodeBase.h`, `ImportNodeBase.cpp` - Import/source node base class.
- `include/NodeDataTypes.h`, `include/ImportDataTypes.h` - Workflow data payload types.
- `QtNodes/src/ExecutableNodeDelegateModel.cpp`, `include/QtNodes/internal/ExecutableNodeDelegateModel.hpp` - Execution state, automatic/manual mode, save/load restoration, and port data propagation.

**Key Features:**
- Drag-and-drop node creation from palette.
- Automatic connection routing and downstream data propagation.
- Execution state visualization: `Idle`, `Pending`, `Running`, `Completed`, `Stopped`, `Warning`, `Error`, `Disabled`.
- Manual/Automatic execution mode toggle per executable node.
- `prepareToStart()` based startup validation and parameter snapshot for complex automatic nodes.
- Detail overlay for node data inspection, image preview, ROI selection, processing info, and charts.
- ADS (Qt Advanced Docking System) for dockable panels.
- Full light/dark/fusion theme support.

## Workflow Porting Guidance

Use `workflow_porting_sop.md` as the authoritative process for porting Workspace features to Workflow nodes and for worker-thread refactoring.

Important rules from the SOP:
- Keep UI classes (`Node` / `Dialog`) focused on parameters, presentation, state, and thread lifecycle; put heavy logic in module-specific `Worker` classes.
- Do not reintroduce `MyThread` as a new business-logic container.
- Workflow embedded widgets should generally lock width with `setFixedWidth(300)` and follow the project UI rules.
- Workflow processing outputs should pass `ImportedFileData` as sorted absolute `.h5` file paths, not output directory paths.
- `projectPath()` in workflow contexts may be the full `.insar` file path; convert it to the containing directory before building output paths.
- For H5 or external DLL read/write critical sections, use `NodeUtils::Hdf5Locker`.
- Avoid synchronous H5 preview generation or heavy IO/image processing on the UI thread; use Workers or `QtConcurrent::run`.
- Automatic node chains must guard against re-entry, avoid modal overwrite prompts in auto-triggered runs, and clear/propagate outputs when invalidated.
- `validateAndRestoreOutput()` must be self-contained for project load and should not depend on connected input data being available.
- When adding a new node, update `NodeModels.cpp`, `WorkflowUI::getPaletteFullOrder()`, `SatExplorer.vcxproj`, and `SatExplorer.vcxproj.filters`.

## Notes

- All code comments are in Chinese.
- Project configuration stored in `Config.ini`.
- No automated testing framework is configured.
- User builds manually in Visual Studio - do not attempt to build via command line.
- **C++ Standard Warning:** Do not enable C++17 globally. Only `TargetDetection.cpp` and `BatchTargetRecognition.cpp` are configured to compile under C++17 (for ONNX Runtime C++ API).

## Code Quality & Evaluation Criteria (代码质量与自我评估准则)

在提交任何代码修改或新节点开发之前，必须对照以下 5 个维度的具体指标进行自我评估，确保高水准交付：

### 1. 架构分离与线程规范 (Architectural Separation)
- [ ] **逻辑与UI分离**：UI 类（如 `Node`、`Dialog`）只负责参数收集、状态展示和线程生命周期管理，重度计算逻辑必须剥离至 Worker 线程或 Task 中。
- [ ] **主线程无阻塞**：严禁在主/UI 线程中同步生成 H5 预览图、执行 heavy I/O 或加载 DLL 计算。必须使用 `Worker`、`QRunnable` 或异步的 `QtConcurrent::run` 与 `QFutureWatcher`。
- [ ] **并发选型合规**：新开发的功能严禁使用全局 `MyThread` 类，长耗时任务必须根据场景选择“动态 Worker 到 `QThread`”或“`QRunnable` 投递到 `QThreadPool`”模式。

### 2. 线程安全与数据契约 (Thread Safety & Data Contract)
- [ ] **HDF5 锁保护**：所有涉及 H5 文件读写、外部 DLL 处理的关键代码段，必须使用 RAII 模式的 `NodeUtils::Hdf5Locker` 进行加锁保护，防止多线程死锁。
- [ ] **端口契约一致性**：工作流导入/处理节点的输出必须使用 `ImportedFileData` 传输“已排序的绝对 `.h5` 文件路径列表”，严禁传输目录路径。
- [ ] **路径转换正确**：工作流中的工程路径可能为 `.insar` 文件本身，在拼接输出目录时，必须先使用 `QFileInfo` 获取其所在目录。

### 3. 工作流鲁棒性与工程恢复 (Workflow Robustness)
- [ ] **自包含的工程恢复**：`validateAndRestoreOutput()` 在恢复节点状态时必须是完全自包含的，必须在不依赖上游输入节点存在/连接的条件下，正确校验并载入本地已有输出。
- [ ] **防重入与失效传递**：自动运行链条（Auto-chain）在收到上游变化信号时，必须具备防重入保护，且当下游节点输入无效时，必须正确清除自身输出并传递失效信号。
- [ ] **后台执行无弹窗**：自动运行或后台执行期间，严禁弹出阻断式的模态对话框（如覆盖提示），避免导致后台线程永久挂起。

### 4. 构建配置与注册完整性 (Build & Integration)
- [ ] **编译标准防扩散**：全局 C++ 编译标准必须保持在项目默认的 C++14。只有使用了 ONNX Runtime API 的特定源文件（如 `TargetDetection.cpp`、`BatchTargetRecognition.cpp`）才允许在 `SatExplorer.vcxproj` 中被局部设置为 C++17。
- [ ] **节点注册无遗漏**：开发新节点后，必须在以下 4 个位置进行同步注册：
  1. `NodeModels.cpp` (注册节点模型)
  2. `WorkflowUI::getPaletteFullOrder()` (加入左侧调色板排序)
  3. `SatExplorer.vcxproj` (包含源文件和头文件)
  4. `SatExplorer.vcxproj.filters` (配置 IDE 目录结构过滤)

### 5. 代码风格与非侵入性 (Style & Non-intrusiveness)
- [ ] **中文注释**：所有新增及修改的代码注释必须全部使用中文。
- [ ] **外科手术式修改**：仅对目标逻辑进行侵入，禁止重构不相关的相邻代码、修改他人格式或清理无关的历史遗留代码。
- [ ] **垃圾代码清理**：由本次修改产生的孤立临时变量、未使用的 `#include` 或未被调用的私有方法，必须在提交前清理完毕。


## Behavioral guidelines

1. Think Before Coding
Don't assume. Don't hide confusion. Surface tradeoffs.

Before implementing:
State your assumptions explicitly. If uncertain, ask.
If multiple interpretations exist, present them - don't pick silently.
If a simpler approach exists, say so. Push back when warranted.
If something is unclear, stop. Name what's confusing. Ask.

2. Simplicity First
Minimum code that solves the problem. Nothing speculative.

No features beyond what was asked.
No abstractions for single-use code.
No "flexibility" or "configurability" that wasn't requested.
No error handling for impossible scenarios.
If you write 200 lines and it could be 50, rewrite it.
Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

3. Surgical Changes
Touch only what you must. Clean up only your own mess.

When editing existing code:
Don't "improve" adjacent code, comments, or formatting.
Don't refactor things that aren't broken.
Match existing style, even if you'd do it differently.
If you notice unrelated dead code, mention it - don't delete it.
When your changes create orphans:

Remove imports/variables/functions that YOUR changes made unused.
Don't remove pre-existing dead code unless asked.
The test: Every changed line should trace directly to the user's request.

4. Goal-Driven Execution
Define success criteria. Loop until verified.

Transform tasks into verifiable goals:
"Add validation" → "Write tests for invalid inputs, then make them pass"
"Fix the bug" → "Write a test that reproduces it, then make it pass"
"Refactor X" → "Ensure tests pass before and after"
For multi-step tasks, state a brief plan:

1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.
