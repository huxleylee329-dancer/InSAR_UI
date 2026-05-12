# InSAR_UI (SatExplorer) Project Guidelines

## Project Overview
**SatExplorer** is a Windows desktop application for InSAR (Interferometric Synthetic Aperture Radar) data processing and visualization. It provides a comprehensive suite of tools for SAR data import, interferometric processing, and AI-based target detection.

### Core Technologies
- **Language:** C++ (primarily C++11/14, C++17 for specific modules)
- **Framework:** Qt 5.15.2 (Core, GUI, Widgets, Charts)
- **UI Libraries:** 
    - **QtNodes:** Visual node-based workflow editor.
    - **ADS (Advanced Docking System):** Flexible dockable panel management.
    - **QCustomPlot:** Plotting and baseline visualization.
- **AI/ML:** ONNX Runtime (for ship detection).
- **Data/IO:** OpenCV, HDF5, GDAL, TinyXML.
- **Build System:** Visual Studio (2019/2022) with Platform Toolset v143.

### Key Architecture
- **Single Window Architecture:** Managed by `InterfaceManager`, switching between:
    - **WorkspaceUI:** Traditional project-tree-based interface.
    - **WorkflowUI:** Node-based visual workflow editor (refactored from `NodeEditorWindow`).
- **Worker Thread Model:** `MyThread` handles long-running processing tasks in the background, communicating with the UI via signals/slots.
- **External Processing:** Core InSAR algorithms are implemented in external DLLs (e.g., `FormatConversion`, `Filter`, `Unwrap`, `Dem`).
- **IPC:** Windows shared memory-based communication for specialized subprocesses like `template_dem`.

---

## Building and Running

### Build Instructions
- **IDE:** Open `SatExplorer.sln` in Visual Studio 2019/2022.
- **Platform:** x64 only.
- **Configuration:** 
    - **Debug:** Produces `bin/SatExplorer.exe` with `_d.dll` dependencies.
    - **Release:** Produces `bin/SatExplorer.exe` with standard DLL dependencies.
- **Qt Integration:** Uses Qt VS Tools. MOC, UIC, and RCC are handled automatically via `QtMsBuild`.

### Runtime Requirements
- All necessary DLLs and the `resources/` directory must be present in the `bin/` folder.
- **Config.ini:** Global settings are stored here.
- **Project Files:** Uses `.Insar` (XML-based) project files.

---

## Development Conventions

### Coding Standards
- **C++ Version:** Use C++11/14 for most of the codebase.
- **C++17 Exception:** ONLY `TargetDetection.cpp` and `BatchTargetRecognition.cpp` should have C++17 enabled (required by ONNX Runtime). Do NOT enable C++17 for the entire project.
- **Encoding:** Files containing Chinese characters/comments MUST use **UTF-8 with BOM** encoding.
- **Simplicity First:** Prefer minimal, surgical changes. Avoid over-engineering or speculative abstractions.

### UI & Resources
- **Stylesheets:** Themes (Light/Dark) are managed via QSS files in `resources/stylesheets/`.
- **Icons:** All icons must be accessed via the Qt Resource System using the path prefix `:/SatExplorer/icon/`.
- **ADS Integration:** Use the Advanced Docking System for any new dockable panels to maintain UI consistency.

### Workflow & Nodes
- **Node Registration:** New nodes for the Workflow Editor must be registered in `NodeModels.cpp` within `registerInSARNodeModels()`.
- **Node State:** Nodes should support execution states (Idle, Running, Completed, Stopped, Error) with appropriate color coding defined in the UI rules.

### Threading & Safety
- **Worker Tasks:** Always use `MyThread` for processing tasks. Do not perform heavy computations on the GUI thread.
- **Thread Safety:** Use `QMutex` and check `stop_flag` within processing loops to support task cancellation.

---

## Key Directories
- `include/`: Header files (split into general and `QtNodes/internal/`).
- `ui/`: Qt Designer `.ui` files.
- `bin/`: Build output and runtime dependencies.
- `QtNodes/`: Custom integration of the node editor library.
- `ADS/`: Advanced Docking System library source.
- `BM3D_cpp/`: Speckle denoising algorithm implementation.
