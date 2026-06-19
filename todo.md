# InSAR_UI (SatExplorer) TODO List

## Technical Debt & Future Fixes

### 1. `FormatConversion.dll` TinyXML Destructor Crash (Memory Leak Workaround)

**Status:** ✅ 已完成

DLL 已修复（PIMPL 模式），全仓约 30 处 `new XMLFile()` 泄漏模式已恢复为栈对象。

---

### 2. Sentinel-1 Import Progress Bar Enhancement

**Status:** ✅ 已完成

`FormatConversion.h` 新增 `ProgressCallback` typedef 及回调重载声明。Sentinel-1、TSX、ALOS2 Worker 均已接入进度回调，template_dem 通过 IPC processCallback 桥接。

---

### 3. 新增 Import 节点的 `image_info` 预览端口支持

**Status:** ✅ 已完成

双端口流转架构（Port 0 imported_file / Port 1 image_info）已全部就绪。所有导入节点均支持预览图自动生成与工程恢复时异步补全。

**LiDAR/GEDI 兼容性修复（2026-06-19）：**
- `BaseImportWorker` 和 `ImportNodeBase` 新增 `previewDataType()` 虚函数（默认 `"complex"`）
- `LidarImportWorker` 和 `LidarImportNode` 覆写为 `"dem"`，利用 H5 中已有的 DEM 数据集生成 JET 色彩预览图
- `BaseImportWorker::import_patch` 预览生成和项目树类型标记均改为调用虚函数
- `ImportNodeBase::validateAndRestoreOutput` Lambda 捕获改为按值传入预览类型

---

### 4. Workflow 画布右键菜单与交互优化

**Status:** ✅ 已完成

统一右键菜单、轻量悬浮搜索框（`NodeSearchPopup`）、双击/Tab 触发、剪贴板状态管理、全局画布 Action（自适应大小/100%缩放/清空画布/导出图片）均已实现。
