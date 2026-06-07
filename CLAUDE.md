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
Module-specific Workers / QtConcurrent Tasks
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
- @.claude/rules/UI.md refer to this rule when applying UI colors or patterns.

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

## Key Processing Modules

**Import:** Sentinel-1, TerraSAR-X, COSMO-SkyMed, ALOS-2, generic SAR data import

**Sentinel-1 Tools:** Deburst, TOPS Back-geocoding, Frame/swath merging

**InSAR Processing:** Baseline estimation/formation/preview, Interferogram formation, Phase filtering, Phase unwrapping, DEM generation

**SBAS/DInSAR:** Time series analysis, Reference point re-selection, Deformation visualization, KML export

**General:** AOI cropping, Registration/Coregistration, Geocoding, Coordinate transformation

**Evaluation/Display:** Image display, target detection, clutter suppression, speckle denoise, ENL/SCR evaluation, logger/note nodes

## Dependencies

**Qt:** 5.15.2 (core, gui, widgets, charts)
**External Libraries:** OpenCV, HDF5, GDAL, TinyXML, QCustomPlot
**Compiler:** Visual Studio 2019/2022, Platform Toolset v143
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
- `.claude/rules/UI.md` - UI/UX design guidelines for colors and theming.

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
- Before changing workflow UI color, icon, or interaction patterns, check `.claude/rules/UI.md`.

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
