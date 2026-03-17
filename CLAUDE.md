# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**SatExplorer** - A Windows desktop application for InSAR (Interferometric Synthetic Aperture Radar) data processing and visualization. Built with C++ and Qt 5.15.2.

## Build Commands

Output directory: `bin/`
- Debug: `SatExplorer.exe` with `_d.dll` suffix libraries
- Release: `SatExplorer.exe` without debug suffix

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

- 28 Qt Designer `.ui` files in `ui/` directory
- Each processing module has a dedicated dialog (e.g., `Filter_ui`, `Unwrap_ui`, `import_sentinel`)
- Custom widgets: `ColorBar` (color scale overlay), `ImageView` (custom image viewer)
- QCustomPlot library for plotting/baseline visualization

### Data Flow Pattern

```
User Action → Dialog UI → Signal → MainWindow → MyThread Worker
    → External DLL Processing → Signal → MainWindow UI Update
```

### Solution Structure

The Visual Studio solution contains two projects:
1. **QtWidgetsApplication3** - Main GUI application (`SatExplorer.exe`)
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

## Notes

- All code comments are in Chinese
- Project configuration stored in `Config.ini`
- No automated testing framework is configured
