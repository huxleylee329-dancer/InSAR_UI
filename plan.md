# 节点编辑器实现计划

本计划用于实现 SatExplorer 项目中的基于节点的可视化流程编辑器功能。

## 进度总结（2026-03-05 更新）

- [x] 阶段 1: QtNodes 库集成 - 已完成
- [x] 阶段 2: 基础架构搭建 - 已完成
- [x] 阶段 2.5: 右侧节点面板 - 已完成 ✨
- [x] 阶段 3: 数据导入节点实现 - 部分完成（Sentinel-1）
- [ ] 阶段 4: 预处理节点实现 - 未开始
- [ ] 阶段 5: 配准节点实现 - 未开始
- [ ] 阶段 6: 干涉处理节点实现 - 未开始
- [ ] 阶段 7: 基线处理节点实现 - 未开始
- [ ] 阶段 8: SBAS 处理节点实现 - 未开始
- [ ] 阶段 9: 可视化节点实现 - 未开始
- [ ] 阶段 10: 工具节点实现 - 未开始
- [x] 阶段 11: 与 MyThread 集成 - 部分完成（导入节点）
- [x] 阶段 12: 保存与加载 - 已完成
- [x] 阶段 13: UI 集成 - 已完成
- [ ] 阶段 14: 测试与优化 - 部分完成
- [ ] 阶段 15: 文档与示例 - 未开始

---

## 已完成的工作（2026-03-05 更新）

### 右侧节点面板（阶段 2.5 - 已完成）

**文件**: `include/NodeEditorWindow.h`, `NodeEditorWindow.cpp`

**功能**:
- 右侧节点面板显示节点分类树
- 搜索框支持过滤节点列表
- 双击节点项添加到画布中心
- 拖拽节点到画布任意位置添加
- 折叠/展开功能（Adobe 风格 dock 风格）
- 3D 边框效果

**实现类**:
- `NodeTreeWidget` - 继承 QTreeWidget，支持拖拽
- `PaletteGraphicsView` - 继承 GraphicsView，接受拖放

**拖拽 MIME 格式**: `application/x-node-palette`

**面板结构优化**:
- 修正为正确的 3 级层级结构
- 实现了 `PaletteOrder` 结构体统一管理所有级别顺序
- 在 `getPaletteFullOrder()` 函数中修改顺序即可

**拖拽实现优化**:
- 使用 `setDragEnabled(false)` + 手动拖拽处理
- 距离检测：鼠标移动超过 10 像素才启动拖拽
- 分类项（有子项的）不能被拖拽
- 叶子项（没有子项的）可以被拖拽

**折叠/展开修复**:
- 修复了双击导致折叠/展开失效的问题
- 添加了 `mouseDoubleClickEvent()` 处理
- 禁用 Qt 内置拖拽，避免与折叠/展开冲突

**编码修复**:
- 为 `NodeEditorWindow.h` 和 `NodeEditorWindow.cpp` 添加 UTF-8 BOM
- 文件保存为 Unicode 格式，可以安全使用中文注释

---

### QtNodes 库集成（阶段 1 - 已完成）
   - QtNodes 源代码通过静态链接方式完全集成到项目中
   - 所有 QtNodes 源文件在 `QtNodes/src/` 目录下
   - 所有 QtNodes 头文件在 `include/QtNodes/internal/` 目录下
   - 在 `QtWidgetsApplication3.vcxproj` 中正确配置：
     - QtNodes 的 `.cpp` 文件添加到 `<ClCompile>` 列表
     - QtNodes 的 `.hpp` 文件添加到 `<ClInclude>` 列表
     - 包含 `Q_OBJECT` 宏的头文件添加到 `<QtMoc>` 处理列表
     - `Definitions.hpp`（包含 `Q_NAMESPACE`）添加到 `<QtMoc>`

2. **NodeEditorWindow 窗口类（阶段 2.1 - 已完成）**
   - `NodeEditorWindow.h` - 完整的头文件定义
   - `NodeEditorWindow.cpp` - 完整的实现，包括：
     - 工具栏（新建、保存、加载、清除、删除、退出按钮）
     - 菜单栏（文件、编辑、帮助菜单）
     - 场景和视图初始化
     - 保存/加载 JSON 格式的流程图
     - 深色主题样式配置
     - 状态栏显示节点和连接数量
     - **项目上下文传递**（setProjectContext, projectModel, projectPath, projectName）

3. **NodeDataTypes 自定义数据类型（阶段 2.2 - 已完成）**
   - `ImageData` - SAR 图像数据
   - `MetadataData` - 元数据
   - `BaselineData` - 基线数据
   - `PairListData` - 干涉对列表
   - `TimeSeriesData` - 时间序列数据
   - `CoordinateMatrixData` - 坐标变换矩阵

4. **NodeModels 节点模型注册表（阶段 2.3 - 已完成）**
   - `NodeModels.h` - 注册表接口定义
   - `NodeModels.cpp` - 包含注册表实现：
     - `registerTestNodeModels()` - 返回测试节点注册表
     - `registerInSARNodeModels()` - 返回 InSAR 节点注册表（包含 Sentinel-1 节点）

5. **UI 集成（阶段 13 - 已完成）**
   - MainWindow.h 中声明了 `on_actionNodeEditor_triggered()` 槽函数
   - MainWindow.cpp:666-680 中实现了打开 NodeEditorWindow 的逻辑
   - 节点编辑器可作为独立窗口打开
   - **传递当前项目上下文到节点编辑器**

6. **数据导入节点实现（阶段 3 - 部分完成）**
   - **`ImportDataTypes.h`** - 导入节点数据类型
     - `ImportedFileData` - 已导入文件数据类型（包含文件路径和节点名）

   - **`ImportNodeBase.h/cpp`** - 导入节点基类
     - 通用端口配置（无输入，单输出）
     - 项目上下文获取方法（projectModel, projectPath, projectName）
     - 进度更新和状态管理
     - 与 NodeEditorWindow 的集成接口

   - **`Sentinel1ImportNode.h/cpp`** - Sentinel-1 单文件导入节点
     - Manifest 文件选择
     - POD 文件选择（可选）
     - 子波束选择下拉框（iw1/iw2/iw3）
     - 极化方式下拉框（vv/vh）
     - 进度条和状态标签
     - 与 MyThread::import_sentinel 集成

   - **`Sentinel1BatchImportNode.h/cpp`** - Sentinel-1 批量导入节点
     - 文件列表控件（显示多个 manifest 路径）
     - 添加/删除文件按钮
     - 子波束和极化共享设置
     - 与 MyThread::import_sentinel_patch 集成

7. **Bug 修复记录（2026-03-04）**

   - **删除功能修复**：
     - 修改 `NodeEditorWindow::onDelete()` 实现真正的删除功能
     - 调用 `m_scene->selectedNodes()` 获取选中节点
     - 调用 `m_graphModel->deleteNode()` 删除节点

   - **Mac 删除快捷键修复**：
     - 修改 `QtNodes/src/GraphicsView.cpp` 中的删除快捷键
     - 同时支持 Delete 键（Windows）和 Backspace 键（Mac）
     - 将快捷键作用域从 WidgetShortcut 改为 WindowShortcut

   - **Widget 清理修复**：
     - 从 `Sentinel1ImportNode` 析构函数移除 `delete m_widget;`
     - 从 `Sentinel1BatchImportNode` 析构函数移除 `delete m_widget;`
     - QtNodes 通过 QGraphicsProxyWidget 管理 widget 生命周期，不应手动删除

### 当前限制

1. **节点模型未完全实现**：仅实现了 Sentinel-1 导入节点，其他导入节点和处理节点尚未实现
2. **UI 样式未优化**：导入节点界面使用默认样式，未应用深色主题
3. **与项目树集成不完整**：节点输出不会自动添加到项目树
4. **实际数据处理未验证**：虽然实现了与 MyThread 的接口，但尚未用真实数据测试

### 编译错误修复记录（2025-02-27）

修复了 QtNodes 集成过程中的编译和链接错误：

1. **Qt MOC 错误修复**：
   - 将包含 `Q_OBJECT` 宏的 QtNodes 头文件添加到 `<QtMoc>` 处理列表：
     - `AbstractGraphModel.hpp` - 抽象图模型基类
     - `BasicGraphicsScene.hpp` - 基础图形场景
     - `ConnectionGraphicsObject.hpp` - 连接图形对象
     - `DataFlowGraphicsScene.hpp` - 数据流图形场景
     - `DataFlowGraphModel.hpp` - 数据流图模型
     - `GraphicsView.hpp` - 图形视图
     - `NodeDelegateModel.hpp` - 节点代理模型
     - `NodeGraphicsObject.hpp` - 节点图形对象
   - 移除了 `Style.hpp`（Q_OBJECT 已被注释）

2. **Q_NAMESPACE 错误修复**：
   - 将 `Definitions.hpp` 从 `<ClInclude>` 移至 `<QtMoc>`，该文件包含 `Q_NAMESPACE` 和 `Q_ENUM_NS` 宏
   - 这解决了 Qt 5.15.2 中命名空间元对象的链接错误

3. **Qt 资源编译错误修复**：
   - 使用 Qt rcc 工具手动生成 `qrc_QtWidgetsApplication3.cpp`
   - 将生成的文件添加到 `<ClCompile>` 列表
   - 从 `<None>` 中移除重复的 qrc 文件引用
   - 这解决了 `qInitResources_QtWidgetsApplication3` 未定义的链接错误

4. **项目文件修改**：
   - 修改 `QtWidgetsApplication3.vcxproj` 添加上述 MOC 和资源配置

---

## 阶段 1: QtNodes 库集成

### 1.1 构建或获取 QtNodes 库
- [x] 检查 `D:\SRC\nodeeditor` 是否已构建完成
- [x] 如未构建，执行 CMake 构建命令生成 QtNodes 库文件
- [x] 确认库文件位置（Debug: `QtNodes_d.lib`, Release: `QtNodes.lib`）

### 1.2 修改项目文件集成 QtNodes
- [x] 编辑 `QtWidgetsApplication3.vcxproj`
- [x] 添加 `D:\SRC\nodeeditor\include` 到 AdditionalIncludeDirectories
- [x] 添加 QtNodes 源文件到项目（或配置为库链接）
- [x] 添加 QtNodes 库目录和库名称到 Linker 配置
- [x] 确保 Qt 相关模块（core, gui, widgets）已配置

### 1.3 验证集成
- [x] 尝试编译项目，确认无编译错误
- [x] 添加简单的测试代码，确认 QtNodes 头文件可以正常引用

## 阶段 2: 基础架构搭建

### 2.1 创建节点编辑器窗口类
- [x] 创建 `include\NodeEditorWindow.h`
- [x] 创建 `NodeEditorWindow.cpp`
- [x] 继承 QMainWindow，设计基本布局
- [x] 添加工具栏（新建、保存、加载、清除、删除、退出按钮）
- [x] 添加 central widget 容器用于 GraphicsView
- [x] 实现场景和视图初始化
- [x] 实现保存/加载 JSON 格式流程图
- [x] 应用深色主题样式
- [x] 添加项目上下文传递接口

### 2.2 创建自定义数据类型文件
- [x] 创建 `include\NodeDataTypes.h`
- [x] 实现 `ImageData` 类（SAR 图像数据）
- [x] 实现 `MetadataData` 类（元数据）
- [x] 实现 `BaselineData` 类（基线数据）
- [x] 实现 `PairListData` 类（干涉对列表）
- [x] 实现 `TimeSeriesData` 类（时间序列数据）
- [x] 实现 `CoordinateMatrixData` 类（坐标变换矩阵）
- [x] 实现 `ImportedFileData` 类（已导入文件数据）

### 2.3 创建节点模型基类和注册表
- [x] 创建 `include\NodeModels.h`
- [x] 创建 `NodeModels.cpp`
- [x] 定义 InSAR 节点注册表初始化函数
- [x] 实现 `registerInSARNodeModels()` 函数（已注册 Sentinel-1 节点和测试节点）
- [x] 创建示例测试节点以验证编辑器功能

**测试节点已创建：**
- `TestNodes.h` - 测试节点头文件
- `TestNodes.cpp` - 测试节点实现
  - `SimpleSourceNode` - 源节点（输出固定值）
  - `SimpleMathNode` - 数学节点（连接两个输入）
  - `SimpleDisplayNode` - 显示节点

**测试方法：**
1. 打开节点编辑器（菜单 → 节点编辑器）
2. **双击**或**拖拽**右侧面板中的节点到画布
3. 连接节点：拖拽输出端口到输入端口
4. 选中节点后点击工具栏的 Delete 按钮（或按 Delete/Backspace 键）删除节点
5. 使用搜索框过滤节点列表
6. 点击面板右上角折叠按钮测试折叠/展开功能

## 阶段 3: 数据导入节点实现

### 3.1 导入节点基类
- [x] 创建 `ImportNodeBase` 抽象基类
- [x] 实现文件选择对话框
- [x] 实现进度更新信号
- [x] 实现项目上下文获取方法

### 3.2 Sentinel-1 导入节点
- [x] 创建 `Sentinel1ImportNode` 类
- [x] 实现 POD 文件选择
- [x] 实现 manifest 文件选择
- [x] 实现子波束和极化参数设置
- [x] 调用 MyThread::import_sentinel 方法
- [x] 实现文件名自动生成

### 3.3 Sentinel-1 批量导入节点
- [x] 创建 `Sentinel1BatchImportNode` 类
- [x] 实现文件列表管理（添加/删除）
- [x] 实现子波束和极化参数设置
- [x] 调用 MyThread::import_sentinel_patch 方法

### 3.4 TSX 导入节点
- [ ] 创建 `TSXImportNode` 类
- [ ] 实现 XML 文件选择
- [ ] 实现极化参数设置
- [ ] 调用 MyThread::import_TSX 方法

### 3.5 其他导入节点
- [ ] 创建 `CSKImportNode` 类
- [ ] 创建 `ALOS2ImportNode` 类
- [ ] 实现各自的参数设置界面

## 阶段 4: 预处理节点实现

### 4.1 S1 Deburst 节点
- [ ] 创建 `S1DeburstNode` 类
- [ ] 实现输入输出端口（ImageData）
- [ ] 调用 MyThread::S1_Deburst 方法

### 4.2 S1 帧拼接节点
- [ ] 创建 `S1FrameMergeNode` 类
- [ ] 实现多输入端口（2个 ImageData）
- [ ] 实现索引参数设置
- [ ] 调用 MyThread::S1_frame_merge 方法

### 4.3 S1 条带拼接节点
- [ ] 创建 `S1SwathMergeNode` 类
- [ ] 实现多输入端口（3个 ImageData）
- [ ] 调用 MyThread::S1_swath_merge 方法

## 阶段 5: 配准节点实现

### 5.1 几何配准节点
- [ ] 创建 `RegistrationNode` 类
- [ ] 实现多输入端口（多个 ImageData）
- [ ] 实现配准参数设置（主图像索引、插值次数等）
- [ ] 调用 MyThread::Regis 方法
- [ ] 输出: ImageData + CoordinateMatrixData

### 5.2 S1 BackGeocoding 节点
- [ ] 创建 `S1BackGeocodingNode` 类
- [ ] 实现参数设置（图像数量、主图像索引、ESD开关）
- [ ] 调用 MyThread::S1_TOPS_BackGeocoding 方法

## 阶段 6: 干涉处理节点实现

### 6.1 干涉图生成节点
- [ ] 创建 `InterferogramNode` 类
- [ ] 实现 2 输入（主从图像）
- [ ] 实现参数设置（去平、地形相位、相干性等）
- [ ] 调用 MyThread::Interferometric 方法

### 6.2 滤波节点
- [ ] 创建 `FilterNode` 类
- [ ] 实现滤波类型选择（Goldstein、基于斜率）
- [ ] 实现参数设置（窗口大小、alpha 值）
- [ ] 调用 MyThread::Denoise 方法

### 6.3 相位解缠节点
- [ ] 创建 `UnwrapNode` 类
- [ ] 实现解缠方法选择（MCF、SNAPHU）
- [ ] 实现参数设置（相干性阈值等）
- [ ] 调用 MyThread::QUnwrap 方法

### 6.4 DEM 生成节点
- [ ] 创建 `DemNode` 类
- [ ] 实现参数设置
- [ ] 调用 MyThread::QDem 方法

## 阶段 7: 基线处理节点实现

### 7.1 基线估计节点
- [ ] 创建 `BaselineNode` 类
- [ ] 实现多输入（多个 ImageData）
- [ ] 调用 MyThread::Baseline_Estimate 方法
- [ ] 输出: BaselineData

### 7.2 基线组生成节点
- [ ] 创建 `BaselineFormationNode` 类
- [ ] 实现 ImageData + BaselineData 输入
- [ ] 实现参数设置（主图像索引）
- [ ] 调用 MyThread::Baseline_Formation 方法
- [ ] 输出: PairListData

## 阶段 8: SBAS 处理节点实现

### 8.1 时间序列分析节点
- [ ] 创建 `SBASTimeSeriesNode` 类
- [ ] 实现多参数设置（时间阈值、空间阈值、多视等）
- [ ] 实现解缠方法选择
- [ ] 调用 MyThread::SBAS_time_series 方法
- [ ] 输出: TimeSeriesData

### 8.2 参考点重选节点
- [ ] 创建 `SBASReferenceReselectionNode` 类
- [ ] 实现参考点坐标设置
- [ ] 实现 GCPs 设置
- [ ] 调用 MyThread::SBAS_reference_reselection 方法

## 阶段 9: 可视化节点实现

### 9.1 图像显示节点
- [ ] 创建 `DisplayNode` 类
- [ ] 实现 ImageData 输入
- [ ] 集成现有的 ImageView 和 ColorBar 组件
- [ ] 在节点中嵌入预览控件

### 9.2 变形预览节点
- [ ] 创建 `DeformationPreviewNode` 类
- [ ] 实现 TimeSeriesData 输入
- [ ] 集成现有的 Deformation_Preview_Window

### 9.3 基线预览节点
- [ ] 创建 `BaselinePreviewNode` 类
- [ ] 实现 BaselineData 输入
- [ ] 集成现有的 Baseline_Preview

## 阶段 10: 工具节点实现

### 10.1 裁剪节点
- [ ] 创建 `CutNode` 类
- [ ] 实现参数设置（经纬度范围或像素范围）
- [ ] 调用 MyThread::Cut 或 MyThread::Cut2 方法

### 10.2 地理编码节点
- [ ] 创建 `GeocodingNode` 类
- [ ] 实现类型选择（产品/图像）
- [ ] 调用 MyThread::Geocoding 方法

## 阶段 11: 与 MyThread 集成

### 11.1 异步处理集成
- [x] 为导入节点实现独立 MyThread 实例
- [ ] 将节点处理操作转移到后台线程
- [x] 实现进度信号从节点到界面的传递
- [x] 实现取消操作机制

### 11.2 错误处理
- [x] 为导入节点添加错误处理逻辑
- [x] 实现错误信号传递
- [x] 在界面上显示错误信息

## 阶段 12: 保存与加载

### 12.1 流程保存
- [x] 实现从图模型生成 JSON（NodeEditorWindow.cpp:229-245）
- [ ] 扩展现有的 XML 项目格式以包含流程图
- [ ] 实现流程保存到项目文件

### 12.2 流程加载
- [x] 实现从 JSON 恢复图模型（NodeEditorWindow.cpp:247-281）
- [ ] 实现节点状态的恢复
- [ ] 处理文件路径引用

### 12.3 导入导出
- [x] 实现导出流程图到独立 JSON 文件（NodeEditorWindow.cpp:229-245）
- [x] 实现从独立 JSON 文件导入流程图（NodeEditorWindow.cpp:247-281）
- [ ] 测试保存/加载功能的完整性

## 阶段 13: UI 集成

### 13.1 添加到 MainWindow
- [x] 在 MainWindow 中添加"节点编辑器"菜单项（on_actionNodeEditor_triggered）
- [x] 创建并显示 NodeEditorWindow（MainWindow.cpp:666-680）
- [x] 传递项目上下文到节点编辑器

### 13.2 与现有项目树集成
- [ ] 实现节点输出自动添加到项目树
- [ ] 实现从项目树拖拽数据到节点编辑器

### 13.3 自定义样式
- [x] 应用适合 SatExplorer 的深色主题（NodeEditorWindow.cpp:155-198）
- [ ] 为不同类型节点使用不同颜色
- [ ] 自定义图标和标签
- [ ] 恢复启动画面（当前临时禁用以加速开发）

## 阶段 14: 测试与优化

### 14.1 单元测试
- [x] 测试 TestNode 的基本功能
- [x] 测试节点拖拽和连接
- [x] 测试节点删除功能
- [ ] 测试 Sentinel-1 导入节点功能
- [ ] 测试数据流传播

### 14.2 集成测试
- [ ] 测试完整的处理流程
- [ ] 测试保存/加载功能
- [ ] 测试取消操作
- [ ] 测试实际 Sentinel-1 数据导入

### 14.3 性能优化
- [ ] 优化大量节点的渲染性能
- [ ] 优化数据传输效率
- [ ] 减少内存占用

## 阶段 15: 文档与示例

### 15.1 用户文档
- [ ] 编写节点编辑器使用说明
- [ ] 为每种节点编写参数说明
- [ ] 创建示例工作流

### 15.2 开发文档
- [ ] 编写节点开发指南
- [ ] 记录扩展 API
- [ ] 更新 CLAUDE.md

---

## 执行顺序建议

按照以下顺序执行各阶段：

1. **阶段 1-2**（必须先完成）：库集成和基础架构
2. **阶段 3**：数据导入节点实现（已完成 Sentinel-1）
3. **阶段 4-10**（可并行）：各类处理节点实现
4. **阶段 11**：MyThread 集成
5. **阶段 12-13**：保存加载和 UI 集成
6. **阶段 14**：测试与优化
7. **阶段 15**：文档

## 关键决策点

1. **节点线程模型**：每个节点使用独立线程还是共享线程池？——当前方案：每个导入节点使用独立 MyThread 实例
2. **数据存储**：大文件（HDF5）是存储路径还是直接数据？——当前方案：存储文件路径
3. **项目文件格式**：扩展现有 XML 还是使用新的 JSON 格式？——当前方案：流程图使用 JSON，项目使用 XML
4. **实时预览**：是否需要实时预览节点输出？——待定
5. **UI 样式**：是否统一应用深色主题？——待定（用户反馈当前样式不满意）
