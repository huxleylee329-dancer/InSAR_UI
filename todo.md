# InSAR_UI (SatExplorer) TODO List

## Technical Debt & Future Fixes

### 1. `FormatConversion.dll` TinyXML Destructor Crash (Memory Leak Workaround)

**Status:** 已完成。DLL已修复，泄漏模式已全部清理为栈对象
**Affected Files:** 
- **All UI and Worker task files** (e.g., `GenericSARImportTask.cpp`, `ClutterSuppression.cpp`, `treeview.cpp`, `MyThread.cpp`, `import_sentinel.cpp`, etc.)

**Background:**
During the refactoring of `MyThread` to the new `QRunnable` Task architecture, a critical bug in `FormatConversion_d.dll` was exposed. The `XMLFile` destructor (`~XMLFile()`) triggers a TinyXML assertion failure (`Expression: sentinel.prev == &sentinel` at `tinyxml.cpp` Line 1510) due to memory corruption. 

Because `SatExplorer.exe` compiles its own copy of `tinyxml.cpp` while `FormatConversion.dll` internally uses its own, the `TiXmlDocument` state diverges across the CRT/Heap boundary. Calling `~XMLFile()` inside the DLL via the EXE triggers this assertion. Furthermore, reusing the same `XMLFile` instance to call `LoadFile()` multiple times (e.g., in a `for` loop for Batch Import) triggers `doc.Clear()` internally inside the DLL, which also results in the exact same crash.

**Current Workaround:**
To prevent the application from crashing while using the existing DLL, we applied an "以毒攻毒" (fight fire with fire) approach globally across the codebase. We reverted all memory management of `XMLFile` to raw pointers without deletion:
```cpp
XMLFile* xml = new XMLFile(); // Intentional leak
```
This intentionally leaks ~2KB of heap memory per instance, bypassing the DLL destruction phase and successfully avoiding the crash. Additionally, for batch processing tasks, `new XMLFile()` is now explicitly placed **inside** the loop to ensure a fresh, uncorrupted object is used per iteration.

**已完成的代码侧修改：**
1. `FormatConversion.h` 已移除 `#include "tinyxml.h"` 依赖，改用前向声明 `class TiXmlElement; class TiXmlDocument;`
2. `XMLFile` 私有成员已从 `TiXmlDocument doc` 改为 `struct Impl; Impl* m_impl;`（PIMPL模式）
3. `FormatConversion.h` 已添加 `ProgressCallback` typedef 及 `TSX2h5`/`import_sentinel`/`ALOS2h5`/`sentinel2h5` 的进度回调重载声明
4. 全仓使用 TiXmlElement/TiXmlDocument 的文件已补加显式 `#include "tinyxml.h"`

**已完成：**
1. ✅ 将外部构建的新版 FormatConversion.dll / .lib 替换到 bin/ 和库目录
2. ✅ 在 Visual Studio 中对 SatExplorer 和 template_dem 执行 Clean + ReBuild
3. ✅ 验证崩溃场景不再触发
4. ✅ 全部 `new XMLFile()` 泄漏模式已恢复为栈对象（约 30 处，涉及 22 个文件）

### 2. Sentinel-1 Import Progress Bar Enhancement

**Status:** 已完成，用户已验证通过
**Affected Files:**
- `Sentinel1ImportHelper.cpp` - 单景/批量均使用 ProgressCallback
- `TSXImportWorker.cpp` - 单景/批量均使用 ProgressCallback
- `ALOS2ImportWorker.cpp` - 批量使用 ProgressCallback
- `template_dem/template_dem.cpp` - TSX2h5 通过 IPC processCallback 桥接
- `FormatConversion.h` - 已添加 ProgressCallback typedef 和回调重载声明

**已完成的改造：**
1. `FormatConversion.h` 添加了 `typedef int (*ProgressCallback)(int percent, const char* message, void* userData);`
2. `TSX2h5`、`import_sentinel`、`ALOS2h5`、`sentinel2h5` 均新增了带 `ProgressCallback + void* userData` 参数的重载声明
3. `Sentinel1ImportHelper.cpp`：单景导入映射 DLL 0-100 → UI 20-90；批量导入映射到整体 2-100
4. `TSXImportWorker.cpp`：单景/批量同上模式
5. `ALOS2ImportWorker.cpp`：批量同上模式
6. `template_dem/template_dem.cpp`：master 映射 1-10，slave 映射 11-20，通过 IPC processCallback 桥接

**待用户执行：**
1. 使用新版 FormatConversion.dll/.lib 重编
2. 验证进度条在导入过程中平滑更新
