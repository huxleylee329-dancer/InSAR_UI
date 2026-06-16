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

**Status:** 待实现（分批）
**Affected Files:**
- `LUTANImportWorker.cpp` / `LUTANImportNode.cpp` / `include/LUTANImportNode.h`
- `HTHTImportWorker.cpp` / `HTHTImportNode.cpp` / `include/HTHTImportNode.h`
- `SpacetyImportWorker.cpp` / `SpacetyImportNode.cpp` / `include/SpacetyImportNode.h`
- `AIRSATImportWorker.cpp` / `AIRSATImportNode.cpp` / `include/AIRSATImportNode.h`
- `BiomassImportWorker.cpp` / `BiomassImportNode.cpp` / `include/BiomassImportNode.h`
- `LidarImportWorker.cpp` / `LidarImportNode.cpp` / `include/LidarImportNode.h`

**背景与分析：**

Sentinel-1 BatchImport 节点具有两个输出端口：
- **Port 0** (`imported_file`)：输出导入后的 `.h5` 文件路径列表，供下游配准/干涉等处理节点使用。
- **Port 1** (`image_info`, 可选)：输出对应的 `.jpg` 缩略图路径列表，可连接 `ImageDisplay` 节点在 Workflow 画布上实时预览图像。

Port 1 的完整工作链路：
1. Import Worker 在生成 `.h5` 文件的同时，调用 `NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "complex")` 生成同名 `.jpg` 缩略图。
2. `onImportFinished()` 中构造 `ImageInfoData(jpgPaths)`，通过 `setOutputData(1, m_imageInfoData)` 写入 Port 1。
3. `validateAndRestoreOutput()` 中（工程重新加载时）检测 `.jpg` 是否存在：若缺失，通过 `QtConcurrent::run + QFutureWatcher` 在后台异步补全生成，避免主线程卡顿。

**当前新增节点的差距：**

| 节点 | Worker 生成 JPG？ | 声明 Port 1？ | 可用？ |
|------|:---:|:---:|:---:|
| LuTan-1 | ❌ | ❌ | ❌ |
| Hongtu-1 | ❌ | ❌ | ❌ |
| Fucheng-1/Spacety | ❌ | ❌ | ❌ |
| AIRSAT | ❌ | ❌ | ❌ |
| Biomass L1A | ❌ | ❌ | ❌ |
| LiDAR | ❌（格式特殊） | ❌ | ❌ |

旧节点 ALOS-2 / CSK 同样没有 Port 1，属于同类历史遗留问题，可一并改造。

**LiDAR 的特殊说明：**
LiDAR 数据格式与 SLC SAR 不同，其 H5 内部数据不是复数格式，不能直接用 `"complex"` type 调用 `generateJpgPreviewFromH5`。需要先调查 LiDAR H5 文件的内部数据集结构（高程、强度等），再确定合适的 type 参数（可能为 `"phase"` 或其他）。

**实现步骤（每个格式逐一执行）：**

#### 步骤 1：Worker 中生成 JPG 缩略图

参考 `Sentinel1ImportHelper.cpp:L211` 的做法，在各格式 Worker 的导入流程完成后（`run()` 函数末尾，生成 `.h5` 之后），添加：

```cpp
// 生成 JPG 预览缩略图（在 Worker 线程中同步执行，不阻塞主线程）
QString jpgPath = QFileInfo(h5OutputPath).absolutePath() + "/"
                + QFileInfo(h5OutputPath).baseName() + ".jpg";
NodeUtils::generateJpgPreviewFromH5(h5OutputPath, jpgPath, "complex");
```

批量导入的 Worker 需在循环内对每个输出文件分别调用。

#### 步骤 2：Node 头文件添加声明

参考 `Sentinel1BatchImportNode.h`，在各 Node 的头文件中添加：

```cpp
// 新增成员变量
std::shared_ptr<ImageInfoData> m_imageInfoData;
QFutureWatcher<void> m_remedyWatcher;  // 用于 validateAndRestoreOutput 异步补全

// 新增方法声明（覆盖基类）
unsigned int nPorts(PortType portType) const override;
NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
QString portCaption(PortType portType, PortIndex portIndex) const override;
bool portIsOptional(PortType portType, PortIndex portIndex) const override;
std::shared_ptr<NodeData> outData(PortIndex port) override;
```

头文件需要新增 `#include "NodeDataTypes.h"` 和 `#include <QFutureWatcher>` / `#include <QtConcurrent/QtConcurrent>`。

#### 步骤 3：Node .cpp 实现端口方法

```cpp
unsigned int XxxImportNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 0;
    return 2;
}

NodeDataType XxxImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported Files"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool XxxImportNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString XxxImportNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return tr("成果 *");
        if (portIndex == 1) return tr("预览 ?");
    }
    return QString();
}

bool XxxImportNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out && portIndex == 1;
}

std::shared_ptr<NodeData> XxxImportNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}
```

#### 步骤 4：`onImportFinished()` 中填充 Port 1

在调用基类 `ImportNodeBase::onImportFinished()` 之后（Port 0 已被基类填充），追加 Port 1 的输出：

```cpp
// 双路输出：Port 1 预览
if (!m_importedFilePaths.isEmpty()) {
    QStringList jpgPaths;
    for (const QString& h5Path : m_importedFilePaths) {
        QFileInfo fi(h5Path);
        jpgPaths.append(fi.absolutePath() + "/" + fi.baseName() + ".jpg");
    }
    m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
    setOutputData(1, m_imageInfoData);
    Q_EMIT dataUpdated(1);
}
```

#### 步骤 5：`validateAndRestoreOutput()` 中异步补全 JPG

> ⚠️ **严禁在 `validateAndRestoreOutput()` 中同步调用 `generateJpgPreviewFromH5`**，否则主线程卡死甚至崩溃。必须使用 `QtConcurrent::run + QFutureWatcher` 异步执行。

参考 `Sentinel1BatchImportNode::validateAndRestoreOutput()` 的完整实现（`Sentinel1BatchImportNode.cpp:L687-L754`）：

```cpp
// 检测缺失的 JPG，异步补全，然后通过信号更新 Port 1
if (!missingH5s.isEmpty()) {
    m_remedyWatcher.cancel();
    m_remedyWatcher.waitForFinished();
    m_remedyWatcher.disconnect();

    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, allJpgPaths]() {
        m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    });

    QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
        for (int i = 0; i < missingH5s.size(); ++i)
            NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
    });
    m_remedyWatcher.setFuture(future);
} else {
    m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
    setOutputData(1, m_imageInfoData);
    Q_EMIT dataUpdated(1);
}
```

**参考文档：**
- `workflow_porting_sop.md:L89` — 预览数据自动补全规范
- `workflow_porting_sop.md:L161-L168` — `generateJpgPreviewFromH5` 线程安全警告
- `Sentinel1BatchImportNode.cpp:L554-L593` — `onImportFinished` 双路输出参考实现
- `Sentinel1BatchImportNode.cpp:L687-L754` — `validateAndRestoreOutput` 异步补全参考实现

---

### 4. Workflow 画布右键菜单与交互优化

**Status:** 待实现（分阶段）
**Affected Files:**
- `QtNodes/src/GraphicsView.cpp` — 重写 `contextMenuEvent`、`mouseDoubleClickEvent`、`keyPressEvent`
- `QtNodes/src/DataFlowGraphicsScene.cpp` — 重新设计 `createSceneMenu`，增加搜索自动聚焦
- `WorkflowUI.cpp` — 实现 Auto-Arrange、Export to Image、Clear Canvas 等全局 Action

**设计目标：**
- **降低新用户门槛**：右键画布提供直观的画布和节点管理菜单（复制、粘贴、对齐、缩放等），而不仅仅是添加节点。
- **提高专业用户效率**：支持通过 `Tab` 键或双击快速调出轻量模糊搜索栏，实现秒级节点添加。
- **净化画布视觉**：减少冗余菜单项，优化节点添加体验。

**画布右键菜单结构（Unified Context Menu）：**

| 分组 | 菜单项 | 说明 |
| :--- | :--- | :--- |
| **添加节点** | `[+] 添加节点 (Add Node...)` | 在鼠标位置弹出节点模糊搜索栏 |
| **剪贴板** | `粘贴 (Paste)` | 在鼠标位置粘贴已复制的节点 |
| | `全选 (Select All)` | 选中画布上所有节点 |
| | `清除选择 (Clear Selection)` | 取消所有选中 |
| **布局整理** | `自动整理布局 (Auto-Arrange Layout)` | 拓扑排序自动对齐节点 |
| | `新建注释节点 (Add Note)` | 在当前位置创建 NoteNode 文本便签 |
| **视图操作** | `自适应大小 (Zoom to Fit)` | 缩放并居中展示整个工作流 |
| | `恢复 100% 缩放 (Reset Zoom)` | 将画布比例重置为 1:1 |
| **画布管理** | `清空画布 (Clear Canvas...)` | 清空所有节点与连线（需确认提示） |
| | `导出为图片 (Export as Image...)` | 将工作流保存为本地图片 |

**快速添加节点通道：**

- **通道 A：双击画布 / 按 `Tab` 键**
  1. 在鼠标位置弹出精简搜索栏（单行输入框 + 匹配列表）
  2. 自动获取焦点，无需鼠标再次点击
  3. 支持模糊匹配（如输入 `det` 过滤出 `Target Detection`）
  4. 键盘 `Up/Down` 选择候选，`Enter` 直接生成节点

- **通道 B：上下文敏感拖拽连线**
  - 从引脚拖出连线在空白处松开时，弹出搜索菜单并**自动过滤**为类型兼容的节点

**技术实现路径：**

1. **统一右键菜单**：修改 `GraphicsView.cpp:contextMenuEvent`，不再直接调用 `createSceneMenu`；重新设计 `DataFlowGraphicsScene::createSceneMenu` 返回混合 `QMenu`。

2. **搜索弹窗自动聚焦**：在 `DataFlowGraphicsScene.cpp` 的搜索窗口 `show()` 后调用 `txtBox->setFocus()`；增加 `Up/Down/Enter` 键盘支持。

3. **双击与快捷键拦截**：在 `GraphicsView.cpp` 重写 `mouseDoubleClickEvent` 和 `keyPressEvent`，检测到双击或 `Tab` 时在鼠标位置弹出精简搜索框。

4. **全局 Action 对接**：在 `WorkflowUI.cpp` 实现 Auto-Arrange、Export to Image、Clear Canvas 等，通过信号槽绑定到右键菜单。

**阶段实施计划：**

```
第一阶段：混合右键菜单
    ↓ 构建新的混合 QMenu，挂载粘贴/视图适配等基础功能，添加节点改为二级菜单
第二阶段：快捷添加与自动聚焦
    ↓ 捕获双击与 Tab 快捷键，弹出搜索浮窗并自动聚焦到 Filter 输入框
第三阶段：键盘盲操优化
    ↓ 为搜索框列表增加 Up/Down 键支持，Enter 直接放置节点
第四阶段：全局 Action 整合
    ↓ 绑定复制/粘贴，对接保存为图片和画布清空等外围功能
```

> 遵循 **Simplicity First** 原则，修改控制在最小范围，优先复用现有 Qt 基础设施。
