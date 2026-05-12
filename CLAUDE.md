# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**SatExplorer** - A Windows desktop application for InSAR (Interferometric Synthetic Aperture Radar) data processing and visualization. Built with C++ and Qt 5.15.2.

## Build Commands

Output directory: `bin/`
- Debug: `SatExplorer.exe` with `_d.dll` suffix libraries
- Release: `SatExplorer.exe` without debug suffix

User always builds by himself in another visual studio environment. Don't try to build after coding.

## Architecture

### High-Level Design

The application follows an MVC-inspired pattern with Qt's signal/slot mechanism:

```
MainWindow (Controller) → MyThread (Worker) → External Processing DLLs
         ↓                                             ↓
      UI Updates                                Data Processing
```

### Key Components

**MainWindow** (`MainWindow.h/cpp`)
- Central coordinator for all operations
- Manages the project tree view (`QStandardItemModel`)
- Handles UI events and coordinates processing requests
- Receives updates from `MyThread` via signals (`updateProcess`, `endProcess`, `sendModel`)

**MyThread** (`MyThread.h/cpp`)
- Worker thread moved to `QThread` for non-blocking operations
- Contains all processing slots that call external DLL functions
- Uses `QMutex` and `stop_flag` for thread safety and cancellation
- Signals progress and completion back to MainWindow

**Project Management**
- Projects stored as XML files (`.Insar` extension)
- Uses TinyXML library for XML parsing
- TreeView displays hierarchical structure: Projects → Data Nodes → Data Files
- Custom `TreeView` widget extends `QTreeView` with project-specific behavior

**IPC** (`InSAR_IPC.h/cpp`)
- Windows shared memory-based inter-process communication
- Used to communicate with `template_dem` console subprocess
- Event-based synchronization

### External Processing DLLs

All actual InSAR processing is done by external DLLs loaded from `bin/`:
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

- 33 Qt Designer `.ui` files in `ui/` directory
- Each processing module has a dedicated dialog (e.g., `Filter_ui`, `Unwrap_ui`, `import_sentinel`)
- Custom widgets: `ColorBar` (color scale overlay), `ImageView` (custom image viewer)
- QCustomPlot library for plotting/baseline visualization
- @.claude/rules/UI.md  refer to this rule when apply UI color or pattern

### Data Flow Pattern

```
User Action → Dialog UI → Signal → MainWindow → MyThread Worker
    → External DLL Processing → Signal → MainWindow UI Update
```

### Solution Structure

The Visual Studio solution contains two projects:
1. **SatExplorer** - Main GUI application (`SatExplorer.exe`)
2. **template_dem** - Console subprocess for DEM processing (uses IPC)

## Code Organization

- `include/` - All header files
- `ui/` - Qt Designer UI files
- `bin/` - Build output, executable, and DLL dependencies
- `template_dem/` - Console subprocess for DEM operations

## Key Processing Modules

**Import:** Sentinel-1, TerraSAR-X, COSMO-SkyMed, ALOS-2 data import

**Sentinel-1 Tools:** Deburst, Back-geocoding, Frame/swath merging

**InSAR Processing:** Baseline estimation, Interferogram formation, Phase filtering, Phase unwrapping, DEM generation

**SBAS/DInSAR:** Time series analysis, Reference point re-selection, Deformation visualization, KML export

**General:** AOI cropping, Registration, Geocoding, Coordinate transformation

## Dependencies

**Qt:** 5.15.2 (core, gui, widgets, charts)
**External Libraries:** OpenCV, HDF5, GDAL, TinyXML, QCustomPlot
**Compiler:** Visual Studio 2019/2022, Platform Toolset v143
**OpenMP:** Enabled for parallel processing

## UI Architecture Updates (2026)

### Major UI Restructuring - Single Main Window with Interface Switching

Recent major UI restructuring to support welcome screen and single-window interface switching:

**New Architecture:**
```
WelcomeScreen (startup) → MainWindow (single top-level)
                           ↓
                    InterfaceManager → switches between:
                           ↓
                    WorkspaceUI (traditional tree-based interface)
                        OR
                    WorkflowUI (node-based visual workflow editor)
```

**New Files Added:**
- `include/IApplicationInterface.h` - Abstract interface for all switchable interfaces
- `include/InterfaceManager.h/cpp` - Manages interface switching and persistence
- `include/WelcomeScreenUI.h/cpp` - VS Code-style welcome screen with new/open/recent projects
- `include/WorkspaceUI.h/cpp` - Encapsulates traditional workspace interface
- `include/WorkflowUI.h/cpp` - Encapsulates node-based workflow interface (refactored from NodeEditorWindow)

**Persistence:**
- Global default interface stored in `Config.ini` at `Interface/Default`
- Per-project last used interface stored in project XML at `project/lastInterface`
- Opening a project restores the last used interface

### Node-Based Workflow Editor

The project includes a visual node-based workflow editor built on top of QtNodes:

**Key Files:**
- `include/WorkflowUI.h/cpp` - Main workflow UI container (refactored from NodeEditorWindow)
- `include/NodeModels.h` - Node delegate model definitions (import nodes, processing nodes, test nodes)
- `QtNodes/src/` - QtNodes static integration with custom extensions for execution state display
- `.claude/rules/UI.md` - UI/UX design guidelines for colors and theming

**Key Features:**
- Drag-and-drop node creation from palette
- Automatic connection routing
- Execution state visualization (Idle/Running/Completed/Stopped/Error) with color coding
- Manual/Automatic execution mode toggle per node
- Detail overlay for node data inspection
- ADS (Qt Advanced Docking System) for dockable panels
- Full light/dark theme support

## Notes

- All code comments are in Chinese
- Project configuration stored in `Config.ini`
- No automated testing framework is configured
- User builds manually in Visual Studio - do not attempt to build via command line

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