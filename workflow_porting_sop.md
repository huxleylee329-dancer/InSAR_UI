# Workspace UI 功能移植至 Workflow UI 及 MyThread 瘦身标准化流程 (草案)

本指南旨在规范将传统菜单/弹窗动作（Workspace UI）移植到基于节点的工作流（Workflow UI）中的标准化操作流程，并同步推进 `MyThread` 的瘦身与重构工作。

---

## 核心原则

1. **职责分离 (Separation of Concerns)**：
   - 界面类（`Node` / `Dialog`）仅负责参数收集、UI 展示和线程生命周期控制。
   - 业务逻辑与重度计算必须剥离至独立的 `Worker` 类中，不允许直接在 UI 线程或臃肿的 `MyThread` 中塞入具体业务实现。
2. **零副作用兼容**：
   - 剥离的 `Worker` 类必须同时服务于旧的 Workspace 弹窗与新的 Workflow 节点，确保老功能不被破坏，且代码复用最大化。
3. **严格的 UI 规范**：
   - 工作流嵌入式 Widget 必须显式设定固定宽度（如 `setFixedWidth(300)`），规避 `QtNodes` 框架的布局尺寸无限膨胀 Bug。

---

## 标准移植六步法

### 第一步：分析与对齐 (Research & Alignment)
- **分析源动作**：查找 `MainWindow.cpp` 中触发该 Action 的槽函数，识别其调用的 Dialog 类（如 `S1_TOPS_BackGeocoding`）。
- **分析输入输出**：分析 Dialog 收集的输入参数、生成的临时/永久文件，以及它是如何修改项目树模型（`QStandardItemModel`）和项目 XML 文件的。
- **确定算子规格**：
  - **端口定义**：输入端口与输出端口的数量和数据类型（例如统一采用 `"imported_file"` 以保持高互通性）。
  - **嵌入式参数**：哪些参数由前驱节点连线提供，哪些参数需要在节点嵌入式 Widget 中让用户输入（如 Master Index、Checkbox 等）。

### 第二步：解耦业务逻辑与 MyThread 瘦身 (Decoupling & Refactoring)
- **创建 Worker 类**：
  - 新建 `include/XxxWorker.h` 和 `XxxWorker.cpp`。
  - 继承自 `QObject`，提供与原 `MyThread::Xxx` 签名一致的槽函数。
  - 声明 `updateProcess`、`endProcess`、`errorProcess`、`sendModel` 等标准化信号。
- **迁移核心代码**：
  - 将原 `MyThread::Xxx` 完整的核心计算和业务逻辑移入新的 `Worker` 类。
  - 在迁移的代码中，将所有 `MyThread` 特有成员的使用，替换为标准的传入参数或类内局部变量。
- **瘦身 `MyThread`**：
  - 删去 `MyThread.h` 和 `MyThread.cpp` 中对应函数的声明和实现。
- **重构旧 Dialog**：
  - 将旧 Workspace UI 弹窗类中的 `MyThread*` 指针变更为 `XxxWorker*` 指针，实例化和信号槽连接无缝替换为新的 Worker 实例。

### 第三步：创建 Workflow 节点类 (Creating Workflow Node)
- **创建 Node 文件**：
  - 新建 `include/XxxNode.h` 和 `XxxNode.cpp`。
  - 继承自 `ExecutableNodeDelegateModel`。
- **实现关键接口**：
  - `caption()`：返回节点在画布上的友好展示名称（如 `"S1 TOPS Back-Geocoding"`）。
  - `name()`：返回节点类标识。
  - `dataType()`：定义输入输出端口的数据类型。
  - `save()` / `load()`：序列化和反序列化节点的所有用户输入参数（如选中项、Checkbox 状态等）。
- **构建嵌入式 Widget (`createWidget`)**：
  - 调用 `_widget->setFixedWidth(300)` 锁定宽度。
  - 布局中只保留核心参数配置（若输入数据未连接，应使部分控件处于“等待输入”或 Disabled 状态）。
  - 输入端口连线发生变更时，在 `setInData` / `updateLabels` 中动态刷新可选参数列表（例如通过 `projectModel()` 过滤并装载对应的图像文件名）。
- **生命周期与线程管理**：
  - 在 `executeProcessing()` 中创建 `QThread` 和 `XxxWorker`，使用 `moveToThread` 模式启动。
  - 绑定 `Worker` 的 `endProcess`、`errorProcess` 等信号到 Node 类的对应槽函数，确保执行完成时调用 `finishExecution()`、`setProgress(100)` 并释放线程。

### 第四步：注册与项目配置 (Registration & Project Setup)
- **模型注册**：
  - 在 `NodeModels.cpp` 的 `registerInSARNodeModels()` 中注册新的 Node 类。
- **面板排序**：
  - 在 `WorkflowUI.cpp` 的 `WorkflowUI::getPaletteFullOrder()` 中，根据对齐的分类与展示文字，将其加入对应的叶子项列表中。
- **编译配置更新**：
  - 在 `SatExplorer.vcxproj` 和 `SatExplorer.vcxproj.filters` 中增加新创建的 Header 和 Source 文件。

### 第五步：本地编译 (Compilation & Build)
- 启动 MSVC 编译器编译项目。
- 收集编译错误，特别是由于头文件包含顺序、Qt MOC 机制、命名空间冲突引起的问题。

### 第六步：双向验证与问题记录 (Dual Verification & Troubleshooting)
- **旧弹窗回归测试**：确认 Workspace UI 的老功能是否依然正常运行。
- **新算子功能测试**：拖拽新算子，执行连线、参数修改、启动、取消、保存工程、重新加载工程等场景。
- **更新本 SOP**：将在该模块移植过程中遇到的独特问题（如特定的类库依赖、多线程信号阻塞等）记录到本指南的“避坑与经验总结”章节中。

---

## 移植潜在需求与功能设计规范 (Requirements & Specifications)

1. **自动运行与交互调参的平衡模态规范**：
   - **设计背景**：在全自动工作流（Automatic Mode）下，当上游节点就绪后，下游节点应立即自动执行。但若算子包含关键手工参数（如“主图像”或“参考点”选择），默认无脑执行可能导致用户来不及干预就启动了漫长的重度计算；而强制介入选择又会破坏批量自动化处理 of 连贯性。
   - **设计规范**：引入“自动/手动双模态”的复选框机制（例如：`[x] 默认首张图像为主图像`）。
     - **自动批处理模态**：默认选中，自动将关键参数赋予合理的默认值（如首张图），确保 `validateInputs()` 能够通过，连线后即可自动向下级联流转执行。
     - **交互式调参模态**：取消选中时，将关键参数初始化为无效值，下拉框切换为 `"请选择..."` 占位状态，从而在 `validateInputs()` 拦截自动执行，强迫用户主动选择参数，选择后再自动触发运行。

2. **多端口多路输出的自动恢复与数据同步规范**：
   - **设计背景**：工作流中的某些算子支持多路输出（例如：Port 0 输出 H5 核心成果数据，Port 1 输出用于展示 of JPG 预览数据）。在用户重新加载工程时，工作流不仅需要还原算子为 Completed 状态，还必须恢复所有输出端口的数据，供下游相关节点直接拉起预览 and 处理。
   - **设计规范**：在算子的 `validateAndRestoreOutput()` 方法中，必须显式实现多路输出 the 还原逻辑：
     - **自动检测与恢复**：通过路径扫描（如匹配 `*_regis.h5`）找出计算成果。
     - **预览数据自动补全**：如果发现对应的 `.jpg` 预览文件缺失，应调用 `generateJpgPreviewFromH5` 在后台静默补全生成。
     - **数据通知机制**：将恢复的数据对象分配给对应的输出端口（使用 `setOutputData(portIndex, data)`），并且**必须显式触发 `Q_EMIT dataUpdated(portIndex)` 信号**，向下游广播以使所有下游节点自动刷新。
     - **端口标题与别名规范**：必须显式重写 `portCaptionVisible()` 和 `portCaption()` 接口。对于输出端口，`portCaptionVisible` 应返回 `true`，`portCaption` 应分别为 Port 0 返回 `tr("成果 *")`，为 Port 1 返回 `tr("预览 ?")`，以使用户能直观区分核心成果与辅助预览图。
     - **端口可选性规范**：预览端口通常不作为下游核心算子计算的硬性数据依赖，因此必须显式重写 `portIsOptional()` 接口，在 `portIndex == 1`（即预览 Port）时返回 `true`。这可避免在未连接预览节点时，工作流拓扑校验报错从而阻断执行。

3. **输出冲突检查与覆盖/复用安全拦截规范**：
   - **设计背景**：工作流中的重度计算算子（如配准、Deburst等）执行耗时较长。如果用户在输出节点名称冲突或磁盘文件已存在时无脑重新计算，不仅会覆盖已有数据，还会消耗大量计算资源与时间。
   - **设计规范**：在算子的 `executeProcessing()` 头部启动计算前，必须进行文件存在性检查和拦截提示：
     - **检测范围**：预先根据参数计算出所有将要生成的输出文件路径列表。
     - **安全拦截**：调用 `NodeUtils::checkAndPromptOverwrite()`，传入项目上下文、目标节点名和待检查的路径列表。
     - **三种处理分支**：
       1. **覆盖 (Overwrite)**：清空旧目录，重新启动 Worker 进行完整后台计算。
       2. **保留并使用现有数据 (Load Existing)**：直接跳过耗时的 Worker 计算，直接将进度设为 100% 并调用 `onProcessingFinished()` 完成状态流转，秒级亮起绿色 Completed。
       3. **取消 (Cancel)**：中止整个计算流程，将状态退回到 `Idle`。

4. **详细视图 (Detail View) 图像预览与多图翻页集成规范**：
   - **设计背景**：双击节点打开“详细视图 (Detail View)”时，为了直观展现算子输出，需要在中间的 “Processing Info” 区域展现生成的预览图片。如果节点没有与该预览接口对接，将显示 `"No processing info available"` 导致可视化缺失。
   - **设计规范**：节点类必须重写 `previewImagePaths() const override` 虚函数：
     - **自适应图片扫描**：根据节点输出名称，扫描当前输出目录下已存在的所有 `.jpg` 预览图片文件并返回其绝对路径列表。
     - **主线程响应性能**：为契合主线程零卡顿规范，`previewImagePaths()` 内**仅执行本地文件存在性快速扫描**，不得在该函数内执行同步 H5 读取和 JPG 重建（缺失的 JPG 依赖工程载入或 LoadExisting 时触发的异步补救线程进行后台静默补全）。

---

## 技术实现细节与避坑经验 (Technical Details & Pitfalls)

1. **头文件隐式包含丢失 (编译错误 C2027)**：
   - **现象与根源**：重构并替换 Dialog 类中的 `MyThread` 时，由于去掉了 `MyThread.h` 的包含，Dialog 文件失去了 `<QThread>` 类的隐式引用。如果 Dialog 的实现中创建了 `QThread` 实例或使用了其静态方法，将引发“使用了未定义类型 QThread”的错误。
   - **避坑对策**：在剥离 Worker、为 Dialog 更换头文件时，凡是 Dialog 代码中显式创建了 `QThread` 实例（如 `new QThread(this)`）的地方，都必须在 Dialog 的 cpp 中显式加上 `#include <QThread>`。

2. **禁用状态下 Qt 下拉框 (QComboBox) 占位符不显示**：
   - **现象与根源**：在某些操作系统主题或 QSS 样式表下，当 `QComboBox` 处于 `Disabled` 状态（例如在自动模态下禁止用户手动修改主图像）时，调用 `setPlaceholderText` 设定的占位提示文字会显示为空白或被底色掩盖，造成界面引导缺失。
   - **避坑对策**：采用 **“虚拟项占位符机制”** 代替 `setPlaceholderText`：
     - **自动/禁止选择时**：下拉框中添加只读的引导虚拟项（如 `addItem("自动选择首张图像...")`）并保持 `Disabled`，该提示在任何主题下都可完美绘制。
     - **开启选择时**：动态清除虚拟项，在下拉框第 0 项加入 `"请选择主图像..."` 作为选择指引，第 1 项及之后加载真实文件名列表，实现 1-based 下拉索引与参数索引的优雅映射。

3. **未包含 `NodeDataTypes.h` 导致 `ImageInfoData` 未定义 (编译错误 C2065/C2923/C2664)**：
   - **现象与根源**：在引入 Port 1 可选预览端口并使用 `std::shared_ptr<ImageInfoData>` 存储预览数据时，如果在节点的头文件中漏掉了 `#include "NodeDataTypes.h"`，编译器会报错 `ImageInfoData 未声明的标识符`、`make_shared 模板参数无效`，以及 `std::shared_ptr` 的各种无法隐式类型转换的重载匹配失败。
   - **避坑对策**：工作流节点只要涉及多端口预览或 `ImageInfoData` 相关操作，其头文件或实现文件必须显式包含 `#include "NodeDataTypes.h"`，以保证 `ImageInfoData` 类型被完整定义且能安全向上转型为 `QtNodes::NodeData`。

4. **`projectPath()` 在工作流节点中返回 `.insar` 文件全路径而非目录 (路径错误)**：
   - **现象与根源**：在 Workspace 弹窗中，`savePath` 由用户选择的目录路径传入，直接指向工程根目录。但在 Workflow 节点中，`projectPath()` 返回的是 `.insar` 工程文件的**完整文件路径**（如 `D:/test/1.insar`），而非其所在目录。如果直接将此值拼接为输出路径（如 `projectPath() + "/" + dstNode`），Worker 内 `QDir::mkdir()` 和所有文件写入操作都会以文件为基路径，全部静默失败，磁盘上不会生成任何输出目录与数据，且不会有任何异常报错。
   - **避坑对策**：在所有基于 `projectPath()` 构建输出路径的工作流节点中，必须先对路径做后缀判断：
     ```cpp
     QString rawPath = projectPath();
     QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                   ? QFileInfo(rawPath).absolutePath()
                   : rawPath;
     ```
     之后所有的输出目录拼接一律使用 `dir` 而非 `rawPath`。

5. **Automatic Mode 下基类 `setInData` 完成后将状态强制覆盖回 `Idle`**：
   - **现象与根源**：在 `ExecutionMode::Automatic` 模式下，`ExecutableNodeDelegateModel::setInData()` 在完成数据接收后，若判断当前输出为空（Worker 尚未完成），会将节点状态强制置回 `Idle`，覆盖掉 `executeProcessing()` 里已经设置的 `Running` 状态。结果是节点界面上状态立刻跳回 Idle，进度条归零，完全看不到"Running"。
   - **避坑对策**：在 `executeProcessing()` 启动 Worker 并设置 `setState(Running)` 后，立即紧跟一个 `QTimer::singleShot(0, ...)` 延迟回调，在下一轮事件循环中检查线程是否仍在运行，若是则再次强制设置状态为 `Running`：
     ```cpp
     QTimer::singleShot(0, this, [this]() {
         if (m_thread && m_thread->isRunning())
             setState(ExecutionState::Running);
     });
     ```
     这是对 `QtNodes` 框架该已知行为最小侵入性的兼容性修复方案。

6. **同一函数内重复声明同名局部变量 (编译错误 C2374/C2086)**：
   - **现象与根源**：在 `executeProcessing()` 中，覆盖检查（overwrite check）与后续的图像数量统计（image count）两段逻辑都分别声明了 `QStandardItemModel* model = projectModel()`。由于两者在同一函数作用域内，编译器报"重定义；多次初始化"错误。
   - **避坑对策**：同一函数内只在**第一次使用处**带类型声明初始化该变量，之后所有复用处直接赋值（去掉 `QStandardItemModel*` 类型前缀）。代码审查时应将覆盖检查与主计算逻辑中对 `model` 的使用统一为同一个变量实例。

7. **在 UI 主线程路径上调用 `generateJpgPreviewFromH5` 导致界面卡死或崩溃**：
   - **现象与根源**：`generateJpgPreviewFromH5` 内部调用 OpenCV 的 `cv::minMaxLoc` 等重度图像处理函数，耗时极长。凡是在 UI 主线程上被调用到的函数（包括 `onProcessingFinished()` 和 `validateAndRestoreOutput()`），都不能在其中调用此函数，否则会导致界面完全卡死，严重时因文件损坏触发内存异常而直接崩溃。
   - **具体触发路径**：
     - **路径 A**：`LoadExisting` 分支直接调用 `onProcessingFinished()` → 后者调用 `generateJpgPreviewFromH5` → 主线程崩溃。
     - **路径 B**：`LoadExisting` 分支改为调用 `validateAndRestoreOutput()` → 后者内部若也调用 `generateJpgPreviewFromH5` → 主线程同样卡死。
   - **避坑对策**：确立清晰的"预览图生成"职责边界：
     - **`onProcessingFinished()`（Worker 信号触发，本身在 Qt 信号队列中执行）**：可以调用 `generateJpgPreviewFromH5`，但要注意它实际上也运行在 UI 线程的事件循环中，若耗时过长也会卡顿，建议仅在 Worker 子线程内生成并通过信号回传。
     - **`validateAndRestoreOutput()`（工程加载时由基类直接同步调用）**：**严禁在主线程直接同步调用** `generateJpgPreviewFromH5`。若检测到 `.h5` 存在但 `.jpg` 缺失，必须通过 `QtConcurrent::run` 在后台线程池中异步补救生成，并配合 `QFutureWatcher` 监听。主线程应立即返回 `true` 以保证界面秒开不卡顿，待后台生成完毕后，再通过信号安全地更新 Port 1 数据并刷新 UI。
     - **通用原则**：重度图像处理及 IO 操作只能在**后台线程**（如 Worker 线程、或用 `QtConcurrent::run` 启动的后台线程）中执行，严禁在主线程同步阻塞调用。

8. **QGraphicsView 缩放高频 SAR 图像时最近邻抽样混叠与双线性插值模糊的取舍（插值自适应切换）**：
   - **现象与根源**：SAR 图像天然具有强烈的相干斑噪声（高频随机噪点）。在将其高分辨率大图缩展示在小视口（如 Detail View 预览区）时，若直接使用 Qt 默认的 `Qt::FastTransformation`（最近邻插值），会导致严重的抽样空间混叠（Aliasing）效应，使缩略图看起来明暗颗粒交织，产生刺眼斑点，极其粗糙。然而，若一刀切采用 `Qt::SmoothTransformation`（双线性插值），当用户放大到像素级（>= 1.0 比例）研判微观目标（如船只强反射点）时，像素网格边界会被模糊成朦胧的色彩渐变，丢失科学数据特有的锋利像素边界，严重干扰研判。
   - **避坑对策**：在图像视图控件 `ImageView` 的缩放变换入口（`fitImage()` 及 `wheelEvent()`）中实现动态自适应变换模式：
     1. 通过 `transform().m11()` 实时读取视口当前的绝对缩放比例；
     2. 当 `scale < 0.95` 时，将 `QGraphicsPixmapItem` 的转换模式设为 `Qt::SmoothTransformation`，消除缩小时的抽样混叠噪点，使全局图像过渡圆滑平整；
     3. 当 `scale >= 0.95` 时，切换回 `Qt::FastTransformation`，保证放大到像素网格时呈现清晰、纯正的“马赛克小方格”，展现 100% 原始细节。

9. **工程加载阶段多线程读写 XML 冲突与外部 DLL 崩溃避坑（TinyXML 算子端原生处理）**：
   - **现象与根源**：
     1. **XML 冲突与覆盖问题**：在工程加载流程（`loadWorkflowFromProject` -> `validateAndRestoreOutput`）中，如果在节点内部通过局部 `XMLFile xml;` 变量再次调用 `XMLFile_load` 读取并写入磁盘上的 `.insar` 配置文件，会发生严重的并发访问冲突。更致命的是，局部 `xml` 对文件的修改，会被随后 `MainWindow` 对全局 `this->project->XMLFile_save()` 的调用用旧的内存数据覆写，导致修改丢失。
     2. **TinyXML 外部 DLL 崩溃**：外部数据处理 DLL（如 `FormatConversion_d.dll`）若与主 EXE 分离编译（其源码 `FormatConversion.cpp` 不包含在主 `SatExplorer.sln` 的编译依赖中），如果 DLL 内部在执行 XML 补录（如 `XMLFile_add_backgeocoding`）时遭遇已存在但空的 DataNode 节点，执行 `LastChild()->ToElement()` 会由于空指针解引用触发 `0xC0000005` 运行时访问冲突崩溃。即使修改了 DLL 源码，只要用户不重新编译外部 DLL，运行时依然会崩溃。
   - **避坑对策**：
     1. **复用全局 XML 句柄**：节点类应当实现并使用 `projectXml()` 接口，通过 context 机制（`NodeUtils::getProjectContext`）直接检索并复用全局唯一的已解析 `XMLFile*` 句柄，对其内存树直接进行修改。这保证了所有节点对 XML 树的补录操作都是基于同一份内存镜像，最终由主工程自动进行统一的落盘保存，杜绝了并发冲突与覆盖问题。
     2. **EXE 算子端 native TinyXML 处理**：由于 `SatExplorer.exe` 本身静态编译且包含了完整的 TinyXML 库，我们应将所有需要在算子端进行的 XML 自愈补录动作（如 `validateAndRestoreOutput` 内的补录、Worker 结束后的项目树写入），直接在主 EXE 中用原生的 TinyXML API（`TiXmlElement` / `TiXmlText` 等）编写并实现。这完全绕过了外部 DLL 中的 XML 接口调用，使得即使外部 DLL 没有重新编译，也不会影响 XML 自愈，保证了工程加载的 100% 稳定运行与免崩溃特性。
     3. **内存释放安全**：在直接使用 TinyXML API 时，注意 `InsertBeforeChild` 复制机制。对于通过 `new` 申请的临时 `TiXmlElement` 节点，如果使用 `InsertBeforeChild` 插入，必须在操作后手动 `delete` 销毁原堆对象以防内存泄漏；而使用 `LinkEndChild` 则是接管所有权，无需手动释放。

10. **工作流节点嵌入式 Widget 控件（QComboBox/QLineEdit）对齐规范与固定标签宽度避坑（Fixed Label Width Alignment）**：
    - **现象与根源**：
      在节点的 `createWidget()` 中，若采用独立的 `QHBoxLayout` 来承载“QLabel标签 + 输入控件（QComboBox/QLineEdit等）”行布局，并且使用 `setStretch(0, 3)` / `setStretch(1, 7)` 等比例分配，当标签字符长度不同（例如“选择工程”为 4 个字，“主图像选择”为 5 个字）时，它们在界面上的下拉框或文本框**长度和起点依然会有微小偏差，无法完全对齐**。
      这是因为 Qt 布局的拉伸比例分配是在**优先满足控件的 `sizeHint` 基础尺寸之后，再按比例分配剩余空间**的。由于 4 字标签与 5 字标签的 `sizeHint` 基础宽度不同，导致最终算出的物理像素宽度并不一致。
    - **避坑对策（方案1：固定标签宽度）**：
      1. **取消比例限制**：不应依赖 `setStretch` 或 `Expanding` 尺寸策略去勉强对齐，而是应该将标签和输入框解耦。
      2. **显式固定标签宽度**：在创建 `QLabel` 后，显式设置一个固定的物理像素宽度（例如 `projectLabel->setFixedWidth(80);`），该宽度应略大于包含最多文字的标签（如 5-6 个汉字一般使用 `80` 到 `90` 像素）。
      3. **自动伸缩对齐**：右侧的 `QComboBox` 和 `QLineEdit` 不用额外配置复杂属性，就会由于布局本身的限制，自动分配完全相同的剩余宽度，从而在纵向上保持完美的起点 and 宽度对齐。

11. **目标节点名输入框占位符统一规范（Output Node Name Placeholder）**：
    - **规范要求**：在所有算子节点的 `createWidget()` 中，目标节点名称输入框 `m_outputNodeNameEdit` 的占位符文本（Placeholder Text）应统一设定为 `QStringLiteral("自动生成或手动输入")`，而非自定义或非标准的中文提示，以在整个 SatExplorer 平台工作流界面维持完美一致的交互引导风格。

12. **嵌入式 Widget 创建时参数延迟装载自愈（Widget Initial Label Update）**：
    - **现象与根源**：在工作流中，如果节点之前已经连线，在重新加载工程或者重绘画布时，底层 `QtNodes` 框架会重建嵌入式 Widget（触发 `createWidget()`）。如果此时没有在 `createWidget()` 尾部显式调用 `updateLabels()`，界面上的下拉框或项目名称信息将显示为默认的 `"等待输入"` 或空白，直到下一次输入端口数据变更触发。
    - **避坑对策**：在算子 `createWidget()` 逻辑的**最末尾**，必须显式调用一次 `updateLabels()` 刷新方法。这将检查当前端口是否已附带输入数据，并立即安全加载、渲染项目名称及数据节点，确保界面秒开后的状态 100% 正确对齐。

13. **长时/重度运行后台算子的日志标准化规范 (Standard Logging for Long-Running Worker Tasks)**：
    - **现象与根源**：工作流中的配准（BackGeocoding）、拼幅（Deburst）、子带/图像拼接（Frame/Swath Merge）等核心算子属于后台长时运行的多线程计算任务（通常耗时数分钟到数小时不等）。如果在 `Worker` 线程的关键计算里程碑处缺乏结构化、有意义的日志输出，或者充斥着大量无用、高频触发的 `qDebug` 冗余输出，前者会导致在后台默默运算时运维/研发人员无从得知当前任务状态与执行进度（感觉像是“假死”）；后者则会在高频 GUI 刷新或参数装载时造成控制台日志风暴（Log Storm），严重时还会抢占 I/O 资源甚至卡顿 UI 线程。
    - **避坑与设计规范**：
      1. **清除临时调试日志**：全面排查并清除 `updateMasterImageCombo()` 等高频 UI 响应槽函数中的高频冗余 `qDebug` 输出。
      2. **标准化生产日志**：凡属于长时运行的业务 `Worker`（如 `S1TopsBackGeocodingWorker`、`S1DeburstWorker`、`S1FrameMergeWorker`、`S1SwathMergeWorker` 等），在进入耗时处理前及各个阶段里程碑处，必须通过 `InSARLogManager` 打印结构化的生产日志。
      3. **标准日志埋点规范**：
         - **任务启动**：日志输出 `Worker` 启动及参数详情（包含所选工程、目标输出节点、关键控制参数等，使用 `LogInfo`）。
         - **阶段性/循环进度**：在多 Burst 循环或多图像循环中，记录当前处理的图像名称或 Burst 索引进度（如 `Processing burst 1/12...`，使用 `LogInfo`）。
         - **核心算子结果**：当算子计算出重要科学数据或校正参数时，予以记录（如 ESD 计算出的方位向偏差量 `offset_a`）。
         - **数据存盘与回传**：在 H5 参数回写完毕、XML 序列化落盘前，以及向 UI 发送 `sendResults` 信号的节点，分别记录成功日志。
         - **异常与出错**：在任何参数检验不通过、文件读写失败的 `emit errorProcess` 出口前，同步调用 `LogError` 记录精准的错误原因和上下文环境。

14. **工作流节点“覆盖(Overwrite)”重运行的数据累加与 UI 树重复子节点问题避坑**：
    - **现象与根源**：在执行如 `S1TopsBackGeocodingNode` 等节点时，如果用户在生成同名输出时选择“覆盖(Overwrite)”以重新运行任务，物理文件确实会被重新生成并覆盖。但是，由于底层 Worker 在保存结果到工程 XML 时通常使用**追加(Append)**逻辑（例如在查找到现有的 `DataNode` 时继续向其下 `LinkEndChild(Data)`），如果在重新启动 Worker 前没有清空该节点，XML 内会累积指向同一批文件的多个 `Data` 条目。这会导致在执行完毕调用 `refreshProjectTree()` 刷新 UI 树时，Workspace UI 树里出现名字完全一样的重复子节点。
    - **避坑对策**：在节点主线程的 `executeProcessing()` 逻辑中，当 `checkAndPromptOverwrite()` 的返回结果为 `OverwriteResult::Overwrite`（即用户同意覆盖重写，或默认放行）且**即将进入** `setState(ExecutionState::Running)` 启动 Worker 之前，**必须显式调用** `NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode);`。这能确保在重运行的后台线程启动前，旧的树节点和对应的 XML 记录已被彻底清理。当 Worker 完成并追加 XML 时，就是在一张“白纸”上建立全新的单一节点，从而完美杜绝了数据累加与 UI 树节点的重影 Bug。

15. **工作流工程加载阶段状态恢复 (validateAndRestoreOutput) 的独立性与自闭性规范 (Load-Time State Restoration Autonomy)**：
    - **现象与根源**：在重新打开工程（工程加载流程 `loadWorkflowFromProject` -> 触发节点 `load`）时，工作流需要自动恢复已完成（Completed）算子的状态及输出端口数据。然而，在此阶段，底层 `QtNodes` 框架尚未实例化完节点的输入连接，导致 `m_inputData` 依然处于空指针（`nullptr`）状态，且项目树模型 `projectModel()` 等 UI 上层组件也可能处于断开或尚未就绪状态。如果 `validateAndRestoreOutput()` 的恢复逻辑依赖于 `m_inputData` 的参数（例如输入节点名称、输入文件列表等）或试图去遍历查询未加载完的项目树模型，就会触发校验失败，导致已完成的节点状态丢失，退回 `Idle` 甚至 `Error` 状态。
    - **避坑与设计规范**：
      1. **高自闭性（Self-containment）逻辑**：`validateAndRestoreOutput()` 的执行必须具备强自闭性，即**不能依赖任何输入端口数据 (`m_inputData`) 或动态项目模型遍历**。应直接依赖节点内已被成功反序列化的基础配置字段（如 `m_outputNodeName`，因为反序列化已经在 `load` 极早期完成）和 `projectPath()` 接口。
      2. **物理文件/路径直扫**：通过物理磁盘路径（如拼接 `projectPath() + "/" + m_outputNodeName`）并使用 `QDir::entryList` 进行直观的物理文件扫描（例如匹配 `*.h5`），来验证输出文件是否完整存在。如果存在，即代表该节点已经计算完成。
      3. **加载顺序控制（重要）**：在重写节点的 `load(const QJsonObject& json)` 时，**必须在调用基类 `ExecutableNodeDelegateModel::load(json)` 之前，完成所有当前节点自身特有参数的解析与赋值**。这是因为基类 `load()` 的内部实现会直接同步触发 `validateAndRestoreOutput()`。如果在调用基类 `load()` 时特有参数（如 `m_outputNodeName`）尚未被赋值，`validateAndRestoreOutput()` 将由于读到空值而直接返回失败，最终导致节点无法在工程加载时自动点亮绿色 `Completed` 状态。

16. **头文件全局命名空间污染引起的“clog”等标识符冲突避坑（Namespace Pollution Conflict）**：
    - **现象与根源**：如果项目中某些公共头文件（如 `Coordinate.h`、`ComplexMat.h`）在全局作用域编写了 `using namespace std;`，那么在其后被引入的任何 C/C++ 标准库头文件（或包含这些头文件的第三方库头文件，如 OpenCV via `Utils.h`）都极易在编译时产生严重的标识符冲突。最典型的是，`<complex.h>` 中定义的 `clog` 函数会与 `<iostream>` 中定义的标准输出流 `std::clog` 产生二义性冲突，导致 MSVC 编译器报错 `C2872: “clog”: 不明确的符号`。
    - **避坑对策**：
      1. **引入顺序防御**：在 `.cpp` 实现文件（如 `Baseline.cpp`）的最前端，**优先包含所有的标准库头文件、Qt 框架头文件以及包含第三方库接口的公共头文件（例如 `Utils.h` 等）**，然后再包含项目内的自定义业务头文件（如 `Coordinate.h` 等）。这能保证标准头文件在没有被 `using namespace std;` 污染的纯净命名空间下最先被编译完成。
      2. **源头杜绝（推荐）**：强烈禁止在任何 `.h` 头文件的全局作用域中书写 `using namespace` 语句，所有命名空间应在头文件中使用显式前缀限制（如 `std::string`、`std::vector`），仅允许在 `.cpp` 文件的私有实现内使用 `using namespace` 语句。

17. **无物理产出之可视化/预览节点的状态持久化与零负载恢复规范（Zero-I/O Visualizer State Persistence）**：
    - **现象与根源**：基线预览（`BaselinePreviewNode`）等可视化或评估节点与配准等计算算子不同，它们不产生任何实际的物理目录和落盘成果文件，也不往项目 XML 树上添加节点。如果仅通过 `validateAndRestoreOutput()` 去做物理文件扫描校验，在工程加载阶段将由于没有磁盘物理文件而触发校验失败，使得节点无法恢复为 `Completed`（已完成）状态，强迫用户每次打开工程时都必须重新运行。
    - **避坑与设计规范**：
      1. **状态与数据内存缓存**：对此类纯可视化节点，必须在类内增加数据缓存（如 `m_temporalBaselines` 和 `m_spatialBaselines` 的 QList）。
      2. **重写 `save`/`load` 双向序列化**：在节点 `save()` 时，将已计算出的预览核心结果（如时空基线数据列表、所选主星索引）直接以 `QJsonArray` 的形式序列化写入节点的 JSON 配置树中；在 `load()` 时同步还原解析，将配置写回内存缓存。
      3. **零磁盘 I/O 还原验证**：在 `validateAndRestoreOutput()` 内，直接检测内存缓存是否存在且有效。如果缓存完好，则直接在内存中构建对应的输出数据对象（如包裹 JSON 字符串的 `BaselineData`），直接标记 `Completed` 和 `100%` 进度，以“零磁盘 I/O 损耗”的速度瞬间复原算子状态，确保用户重新进入工作流工程后，无需重新运行即可直接点击按钮秒开图表。

18. **工作流节点尺寸动态改变时的 UI 边界刷新同步规范 (Dynamic Layout Size Synchronization)**：
    - **现象与根源**：在工作流中，如果某些节点需要在执行完成后动态展开额外的展示元素（如 `BaselinePreviewNode` 在计算完后，状态标签文本从 1 行拓展为 3 行），这会导致内嵌 `QWidget` 整体布局的物理高度发生改变。然而，`QtNodes` 框架的后台代理 `QGraphicsProxyWidget` 并不会自动监听和计算它的包围盒，导致外层节点的矩形框仍然保持原来的尺寸，使得新增的元素或底部的按钮被挤出节点卡片边界之外（被裁剪或悬浮在框外）。
    - **避坑与设计规范**：
      1. **实现专属尺寸更新函数**：在节点中实现 `void updateWidgetSize()`，在此函数中强制限制宽度并重新排版计算尺寸。
      2. **发射尺寸刷新信号**：在此函数最后务必调用 `Q_EMIT embeddedWidgetSizeUpdated();`，以通知 `QtNodes` 框架重绘当前节点的物理包围盒：
         ```cpp
         void MyNode::updateWidgetSize() {
             if (_widget) {
                 _widget->setFixedWidth(300); // 严格锁宽
                 _widget->adjustSize();       // 刷新 Layout 的实际几何大小
                 Q_EMIT embeddedWidgetSizeUpdated(); // 发射框架同步刷新信号
             }
         }
         ```
      3. **状态变化时主动调用**：在任何更新 QLabel 文本（`updateLabels()`）、载入 JSON 数据（`load()`）或计算成功（`onProcessingFinished()`）的地方，尾部强制触发此同步。

19. **无显示 QWidget 的离线高保真图表导出与等比缩放排版规范 (Offscreen Chart Export & Element Scaling)**：
    - **现象与根源**：为了在详情视图（Detail View）中静态展示如基线时空图表等复杂的科学绘图，需要在不渲染出屏幕的情况下离线导出图表图片。由于 `QWidget` 和 `QChart` 未经历 `show()` 或实际窗口渲染，它们的布局尺寸默认处于 `0x0` 状态，直接使用 `scene()->render()` 会产生空白或错位的白色底板。此外，即便提升了离线导出的分辨率（如 `1920x1440`），若不显式等比缩放文字和线段大小，在高清画布上所有的轴标题、刻度和折线会显得极其微小和纤细，导致用户依然看不清楚。
    - **避坑与设计规范**：
      1. **强制几何重排**：在开始渲染前，必须对 `QChart` 进行显式尺寸重置，并指定图形场景（`QGraphicsScene`）的边界投影坐标：
         ```cpp
         preview->chart()->resize(QSizeF(1920, 1440));
         preview->chart()->scene()->setSceneRect(0, 0, 1920, 1440);
         ```
      2. **文字与线段等比缩放**：离线大图渲染时，应通过 API 遍历调整图表内部的字体大小、折线宽度与散点标记：
         - 图表大标题字号设为 `32pt` (Bold)，坐标轴标题字号设为 `22pt` (Bold)，标签刻度字号设为 `18pt`。
         - 将折线宽度（`QLineSeries`）乘以 `2.5` 倍（利用 `pen.setWidthF`），将散点标记（`QScatterSeries`）尺寸乘以 `2.0` 倍。
      3. **无损压缩保存**：使用 `pixmap.save(path, "JPG", 100);` 指定质量为 `100` 以关闭有损压缩，避免文字与网格线边缘出现大量 JPEG 模糊杂点。

20. **零/弱上下文节点中的工程路径多层级安全回溯机制 (Robust Project Path Backtracking)**：
    - **现象与根源**：工作流加载（`load`）或反序列化状态恢复（`validateAndRestoreOutput`）在极早期发生，此时节点的内嵌 UI 小部件（`_widget`）尚未被 parent 挂载在主窗口或工作流场景中，传入 `NodeUtils::getProjectContext(_widget)` 时因为 widget 为 `nullptr` 或无父级树直接返回 `nullptr`，造成节点无法顺利推导出工程所在的物理根目录。
    - **避坑与设计规范**：
      1. **实施主备层级多级回溯**：在解析工程根路径的方法中，切勿只依赖 widget 的上下文遍历，应优先回溯主界面环境。
      2. **顶层窗口主动搜寻兜底**：若局部上下文返回空指针，应通过 `QApplication::topLevelWidgets()` 搜索当前活跃的 `MainWindow` 实例，提取 `workspaceUI()` 或者 `interfaceManager()` 来作为主要数据源，确保在无 widget 挂载的特殊阶段亦能精准取回工程文件路径：
         ```cpp
         QString MyNode::projectPath() const {
             IApplicationInterface* iface = nullptr;
             if (_widget) iface = NodeUtils::getProjectContext(_widget);
             if (!iface) {
                 for (QWidget* w : QApplication::topLevelWidgets()) {
                     if (auto* mainWin = qobject_cast<MainWindow*>(w)) {
                         if (mainWin->workspaceUI()) { iface = mainWin->workspaceUI(); break; }
                         if (mainWin->interfaceManager()) { iface = mainWin->interfaceManager()->currentInterface(); if (iface) break; }
                     }
                 }
             }
             // ... 路径处理逻辑
         }
         ```

21. **C++前置声明对隐式向上转型造成的编译器二义性冲突避坑 (Typecasting Forward-Declaration Resolution)**：
    - **现象与根源**：在头文件（例如 `MainWindow.h`）中，为了防止多重包含死锁（Circular Header Inclusions），通常会使用前置声明（如 `class WorkspaceUI;`）。如果在其它业务类的实现文件（`.cpp`）里，直接将该前置声明的指针赋值给父类接口指针（如将 `WorkspaceUI*` 赋值给 `IApplicationInterface*`），而未引入该子类的具体头文件，MSVC 编译器将报错：`C2440: “=”: 无法从“WorkspaceUI *”转换为“IApplicationInterface *”，指向的类型不相关`。这是因为编译器虽然知道这两个类的存在，但不知道它们之间存在继承结构。
    - **避坑对策**：在任何需要执行转型（Upcast）或使用成员方法的 `.cpp` 实现文件头部，**必须包含该子类的具体实现头文件（如 `#include "WorkspaceUI.h"`）**。这能向编译器充分暴露其继承机制，消除转换的二义性并顺利通过编译。

22. **QSS 样式表特定选择器优先级覆盖导致禁用态（Disabled）外观失效避坑 (QSS Specificity vs. Disabled State)**：
    - **现象与根源**：当在节点内嵌 `QWidget` 中通过 `setEnabled(false)` 动态禁用输入框（`QLineEdit`/`QComboBox`）或标签（`QLabel`）时，它们的外观看起来与启用状态没有任何区别。这是因为样式表中定义了非常具体的选择器（如 `QWidget#NodeEmbeddedWidget QLineEdit`），其包含 ID 选择器因而具有极高优先级，彻底覆盖了通用的 `QLineEdit:disabled` 全局声明。
    - **避坑对策**：在各主题样式表（如 `dark.qss`, `light.qss`, `fusion.qss`）中，凡是为 `#NodeEmbeddedWidget` 定义了子控件样式的地方，必须同步显式定义其对应的 `:disabled` 状态样式（例如 `QWidget#NodeEmbeddedWidget QLineEdit:disabled`, `QWidget#NodeEmbeddedWidget QLabel:disabled`），并配置明显的灰色前景色/暗色背景色以确保视觉引导。

23. **工作流参数变动无法触发项目脏标记与退出保存提示避坑 (Workflow Parameter Change Dirty State Propagation)**：
    - **现象与根源**：工作流场景仅在节点增删、连线变动或位置更新时才向主窗口发出 `modified` 信号以更新 `m_projectModified`。当用户在节点内嵌面板中修改文本框、复选框等核心参数时，节点状态由 `Completed` 重置为 `Idle`，但因为场景没有发生拓扑级修改，所以不会标记项目为“已修改”。这导致用户退出程序或关闭项目时，不会有任何提示保存的弹窗，所有修改过的参数被直接遗弃。
    - **避坑对策**：
      1. 在框架级的 `ExecutableNodeDelegateModel::invalidateExecution()` 方法尾部，当节点的运行状态被无效化重置为 `Idle` 时，显式发送场景修改信号：`if (_scene) Q_EMIT _scene->modified(_scene);`。
      2. 在节点局部的 `invalidateNodeData` 参数修改回调 lambda 中，也需显式加上 `if (_scene) Q_EMIT _scene->modified(_scene);`。这可确保即便节点原先已处于 `Idle` 状态时参数再次被修改，也能成功将脏状态广播至 `MainWindow`，让退出保存提示 100% 触发。

24. **多实例节点临时及预览图片路径冲突与重写覆盖避坑 (Offscreen Render Path Collision in Multi-instance Nodes)**：
    - **现象与根源**：如果在工作流中放置了两个或多个相同类型的节点（例如两组独立的基线预览算子），若其离线渲染/生成的临时图片或成果路径采用硬编码的文件名（例如 `.temp/baseline_preview.jpg`），则后运行 of 节点产生的输出会强行覆盖掉先运行节点的输出，导致界面预览出现错乱，且在 `.temp` 下无法区分各个节点的预览图。
    - **避坑对策**：在拼接预览图或临时文件路径时，切忌使用固定的静态文件名，必须将节点唯一的标识 ID（`_nodeId`）融合进文件名中（如 `dir + "/.temp/baseline_preview_" + QString::number(_nodeId) + ".jpg"`）。由于 `_nodeId` 在工作流中是绝对唯一且随工程持久化保存的，这能从根本上保证多个节点实例的输出数据处于完全隔离的沙盒路径中，绝不发生相互冲突。
