# 节点编辑器实现计划

本计划用于实现 SatExplorer 项目中的基于节点的可视化流程编辑器功能。

## 进度总结（2026-03-02 更新）

- [x] 阶段 1: QtNodes 库集成 - 已完成
- [x] 阶段 2: 基础架构搭建 - 部分完成
- [ ] 阶段 3: 数据导入节点实现 - 未开始
- [ ] 阶段 4: 预处理节点实现 - 未开始
- [ ] 阶段 5: 配准节点实现 - 未开始
- [ ] 阶段 6: 干涉处理节点实现 - 未开始
- [ ] 阶段 7: 基线处理节点实现 - 未开始
- [ ] 阶段 8: SBAS 处理节点实现 - 未开始
- [ ] 阶段 9: 可视化节点实现 - 未开始
- [ ] 阶段 10: 工具节点实现 - 未开始
- [ ] 阶段 11: 与 MyThread 集成 - 未开始
- [x] 阶段 12: 保存与加载 - 部分完成（基础框架）
- [x] 阶段 13: UI 集成 - 已完成
- [ ] 阶段 14: 测试与优化 - 未开始
- [ ] 阶段 15: 文档与示例 - 未开始

### 已完成的工作（2026-03-02 更新）

1. **QtNodes 库集成（阶段 1 - 已完成）**
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

3. **NodeDataTypes 自定义数据类型（阶段 2.2 - 已完成）**
   - `ImageData` - SAR 图像数据
   - `MetadataData` - 元数据
   - `BaselineData` - 基线数据
   - `PairListData` - 干涉对列表
   - `TimeSeriesData` - 时间序列数据
   - `CoordinateMatrixData` - 坐标变换矩阵

4. **NodeModels 节点模型注册表（阶段 2.3 - 部分完成）**
   - `NodeModels.h` - 注册表接口定义
   - `NodeModels.cpp` - 仅包含空实现的注册表：
     - `registerTestNodeModels()` - 返回空注册表
     - `registerInSARNodeModels()` - 当前直接返回空测试注册表（TODO: 需实现）

5. **UI 集成（阶段 13 - 已完成）**
   - MainWindow.h 中声明了 `on_actionNodeEditor_triggered()` 槽函数
   - MainWindow.cpp:666-671 中实现了打开 NodeEditorWindow 的逻辑
   - 节点编辑器可作为独立窗口打开

### 当前限制

1. **节点模型未实现**：虽然创建了数据类型和注册表框架，但没有任何具体的节点模型实现（如 ImportNode、FilterNode 等）
2. **注册表为空**：当前 `registerInSARNodeModels()` 返回空注册表，无法创建任何节点
3. **与 MyThread 无集成**：节点处理功能尚未与现有的 MyThread 工作线程集成
4. **流程保存/加载仅为框架**：虽然实现了 JSON 保存/加载的基础代码，但需要进一步验证和测试

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

### 2.2 创建自定义数据类型文件
- [x] 创建 `include\NodeDataTypes.h`
- [x] 实现 `ImageData` 类（SAR 图像数据）
- [x] 实现 `MetadataData` 类（元数据）
- [x] 实现 `BaselineData` 类（基线数据）
- [x] 实现 `PairListData` 类（干涉对列表）
- [x] 实现 `TimeSeriesData` 类（时间序列数据）
- [x] 实现 `CoordinateMatrixData` 类（坐标变换矩阵）

### 2.3 创建节点模型基类和注册表
- [x] 创建 `include\NodeModels.h`
- [x] 创建 `NodeModels.cpp`
- [x] 定义 InSAR 节点注册表初始化函数
- [x] 实现 `registerInSARNodeModels()` 函数（已注册测试节点）
- [x] 创建示例测试节点以验证编辑器功能

**测试节点已创建：**
- `TestNodes.h` - 测试节点头文件
- `TestNodes.cpp` - 测试节点实现
  - `SimpleSourceNode` - 源节点（输出固定值）
  - `SimpleMathNode` - 数学节点（连接两个输入）
  - `SimpleDisplayNode` - 显示节点

**测试方法：**
1. 打开节点编辑器（菜单 → 节点编辑器）
2. 从右侧面板**拖拽**节点到画布
3. 连接节点：拖拽输出端口到输入端口

## 阶段 3: 数据导入节点实现

### 3.1 导入节点基类
- [ ] 创建 `ImportNodeBase` 抽象基类
- [ ] 实现文件选择对话框
- [ ] 实现进度更新信号

### 3.2 Sentinel-1 导入节点
- [ ] 创建 `Sentinel1ImportNode` 类
- [ ] 实现 POD 文件选择
- [ ] 实现 manifest 文件选择
- [ ] 实现子波束和极化参数设置
- [ ] 调用 MyThread::import_sentinel 方法

### 3.3 TSX 导入节点
- [ ] 创建 `TSXImportNode` 类
- [ ] 实现 XML 文件选择
- [ ] 实现极化参数设置
- [ ] 调用 MyThread::import_TSX 方法

### 3.4 其他导入节点
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
- [ ] 创建 `NodeThread` 类继承 QThread
- [ ] 将节点处理操作转移到后台线程
- [ ] 实现进度信号从节点到界面的传递
- [ ] 实现取消操作机制

### 11.2 错误处理
- [ ] 为每个节点添加错误处理逻辑
- [ ] 实现错误信号传递
- [ ] 在界面上显示错误信息

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
- [x] 创建并显示 NodeEditorWindow（MainWindow.cpp:666-671）

### 13.2 与现有项目树集成
- [ ] 实现节点输出自动添加到项目树
- [ ] 实现从项目树拖拽数据到节点编辑器

### 13.3 自定义样式
- [x] 应用适合 SatExplorer 的深色主题（NodeEditorWindow.cpp:155-198）
- [ ] 为不同类型节点使用不同颜色
- [ ] 自定义图标和标签

## 阶段 14: 测试与优化

### 14.1 单元测试
- [ ] 测试每个节点的基本功能
- [ ] 测试数据流传播
- [ ] 测试连接/断开连接

### 14.2 集成测试
- [ ] 测试完整的处理流程
- [ ] 测试保存/加载功能
- [ ] 测试取消操作

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
2. **阶段 3-10**（可并行）：各类节点实现
3. **阶段 11**（节点实现后）：MyThread 集成
4. **阶段 12-13**（功能完成后）：保存加载和 UI 集成
5. **阶段 14**：测试与优化
6. **阶段 15**：文档

## 关键决策点

1. **节点线程模型**：每个节点使用独立线程还是共享线程池？
2. **数据存储**：大文件（HDF5）是存储路径还是直接数据？
3. **项目文件格式**：扩展现有 XML 还是使用新的 JSON 格式？
4. **实时预览**：是否需要实时预览节点输出？
