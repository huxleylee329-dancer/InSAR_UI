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

---

### 3. 新增 Import 节点的 `image_info` 预览端口支持

**Status:** 基础双端口架构已完成，LiDAR 预览生成有遗留优化
**Affected Files:**
- `ImportNodeBase.cpp` / `include/ImportNodeBase.h`
- `BaseImportWorker.cpp` / `include/BaseImportWorker.h`
- 所有继承自上述基类的导入节点与 Worker（如 LUTAN, HTHT, Spacety, AIRSAT, Biomass, LiDAR）

**已完成的工作 (Completed):**
1. **双端口流转架构**：通过引入基类 `ImportNodeBase`，所有新增的子类导入节点均已默认支持 Port 0 (`imported_file`) 和 Port 1 (`image_info`) 双路输出。
2. **预览数据自动补全**：在加载工程时，`ImportNodeBase::validateAndRestoreOutput()` 已支持检测并调用 `QtConcurrent::run` 在后台异步补全缺失的 JPG 预览，不会阻塞 UI 线程。
3. **Worker 导入静默生成**：所有 Worker 继承的 `BaseImportWorker::import_patch` 在导入转换完成后自动调用 `generateJpgPreviewFromH5` 生成预览图。

**待优化与未完成工作 (Pending & Remaining):**
1. **LiDAR/GEDI 预览图生成兼容性**：目前 `BaseImportWorker` 生成缩略图时硬编码使用了 `"complex"` 复数格式。由于雷达非复数数据（如 LiDAR/GEDI H5 内部的 GEDI L2A/L2B 高程、质量矩阵数据集）格式与 SLC 不同，导致 LiDAR/GEDI 导入时无法成功生成 JPG 缩略图。
   - *优化建议*：重构 `BaseImportWorker`，提供如 `previewDataType()` 虚函数或配置项，允许 LiDAR 等特定子类 Worker 指定其所需的预览图渲染类型（例如 `"dem"` 或特定数据集名，而非默认的复数格式）。

---

### 4. Workflow 画布右键菜单与交互优化

**Status:** 已完成

**Affected Files:**
- `include/NodeSearchPopup.h` — 新增轻量悬浮搜索弹窗类声明
- `NodeSearchPopup.cpp` — 搜索弹窗实现（模糊搜索、键盘导航、自动销毁、屏幕边界检查）
- `include/QtNodes/internal/GraphicsView.hpp` — 添加 `mouseDoubleClickEvent`、搜索弹窗、全局 Action、`hasValidPasteData`/`updatePasteActionState` 声明
- `QtNodes/src/GraphicsView.cpp` — 重写 `contextMenuEvent`（统一右键菜单）、`mouseDoubleClickEvent`（双击唤起）、`keyPressEvent`（Tab 键唤起）；实现 `onZoomToFit`/`onResetZoom`/`onClearCanvas`/`onExportAsImage`/`onSelectAll`/`hasValidPasteData`/`updatePasteActionState`；粘贴 Action 根据剪贴板状态自动 enable/disable
- `QtNodes/src/DataFlowGraphicsScene.cpp` — 简化 `createSceneMenu`（移除 QTreeWidget 分类树，改为扁平 QAction 列表 + 搜索过滤）
- `QtNodes/src/NodeGraphicsObject.cpp` — 右键点击节点时自动选中（支持 Ctrl 追加选择）
- `include/WorkflowUI.h` — 添加 `onExportCanvasAsImage`/`onClearCanvas` 声明
- `WorkflowUI.cpp` — 实现 `onExportCanvasAsImage`/`onClearCanvas` 方法；连接 `nodeContextMenu` 信号实现节点右键上下文菜单（复制/粘贴/删除/全选/清除选择）
- `SatExplorer.vcxproj` / `SatExplorer.vcxproj.filters` — 注册 NodeSearchPopup 源文件和头文件

---

**已实现的功能清单：**

1. **统一右键菜单**
   - 空白区域右键：添加节点、复制/粘贴/全选/清除选择、自适应大小/100%缩放、清空画布/导出图片
   - 节点右键：复制/粘贴/删除/全选/清除选择（右键自动选中节点，支持 Ctrl 追加）
   - 菜单项附带快捷键提示（`\tCtrl+C` 等）

2. **轻量悬浮搜索框 (`NodeSearchPopup`)**
   - `Qt::Popup | Qt::FramelessWindowHint`，`WA_DeleteOnClose`
   - 模糊搜索（不区分大小写）、上下键导航、回车创建节点、Esc 关闭
   - listWidget 焦点时按键自动转回 lineEdit 继续输入
   - 屏幕边界自适应、失去焦点自动销毁

3. **触发方式**
   - 空白区域双击、Tab 键（无其他文本框焦点时）
   - 右键菜单「添加节点」项

4. **剪贴板状态管理**
   - `hasValidPasteData()` 检查剪贴板 MIME 类型 `application/qt-nodes-graph` 的有效性
   - `QClipboard::changed` 信号实时更新粘贴 Action 的 enable/disable
   - 快捷键 `Ctrl+V`、空白右键菜单、节点右键菜单三处粘贴项同步状态

5. **全局画布 Action**
   - 自适应大小 (Zoom to Fit)、恢复 100% 缩放 (Reset Zoom)
   - 清空画布 (Clear Canvas，二次确认对话框)
   - 导出为图片 (Export as Image，1:1 比例渲染，50px 边距，空画布检查)
