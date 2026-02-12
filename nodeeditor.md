# Node Editor Integration Guide

本文档记录了 QtNodes 节点编辑器库的详细信息，用于在 SatExplorer 项目中实现基于节点的可视化流程编辑器功能。

## 概述

QtNodes 是一个基于 Qt 5/6 的节点编辑器库，采用 Model-View-Controller 架构，支持数据流传播、可视化编辑、撤销/重做、序列化等完整功能。

**库位置**: `D:\SRC\nodeeditor`

## 核心架构

### 架构分层

```
┌─────────────────────────────────────────────────────┐
│              UI Layer (Qt Widgets/View)             │
│   GraphicsView → BasicGraphicsScene/                │
│   DataFlowGraphicsScene → NodeGraphicsObject        │
└─────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────┐
│            Business Logic Layer (Model)             │
│   AbstractGraphModel → DataFlowGraphModel          │
│   NodeDelegateModelRegistry → NodeDelegateModel    │
└─────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────┐
│               Data Layer (NodeData)                 │
│   用户自定义数据类型 (继承自 NodeData)               │
└─────────────────────────────────────────────────────┘
```

### 核心组件

| 组件 | 说明 | 文件 |
|------|------|------|
| `AbstractGraphModel` | 图数据模型基类 | `AbstractGraphModel.h` |
| `DataFlowGraphModel` | 数据流图模型（带传播） | `DataFlowGraphModel.h` |
| `NodeDelegateModel` | 节点逻辑封装基类 | `NodeDelegateModel.h` |
| `NodeDelegateModelRegistry` | 节点模型工厂 | `NodeDelegateModelRegistry.h` |
| `NodeData` | 可传输数据基类 | `NodeData.h` |
| `BasicGraphicsScene` | 图形场景基类 | `BasicGraphicsScene.h` |
| `DataFlowGraphicsScene` | 带右键菜单的图形场景 | `DataFlowGraphicsScene.h` |
| `GraphicsView` | 带缩放/平移的图形视图 | `GraphicsView.h` |
| `NodeGraphicsObject` | 节点图形项 | `NodeGraphicsObject.h` |
| `ConnectionGraphicsObject` | 连线图形项 | `ConnectionGraphicsObject.h` |

## 关键类型定义

### 节点标识
```cpp
using NodeId = unsigned int;
static constexpr NodeId InvalidNodeId = std::numeric_limits<NodeId>::max();
```

### 端口标识
```cpp
using PortIndex = unsigned int;
static constexpr PortIndex InvalidPortIndex = std::numeric_limits<PortIndex>::max();
```

### 连接标识
```cpp
struct ConnectionId {
    NodeId outNodeId;      // 输出节点ID
    PortIndex outPortIndex; // 输出端口索引
    NodeId inNodeId;       // 输入节点ID
    PortIndex inPortIndex; // 输入端口索引
};
```

### 端口类型
```cpp
enum class PortType { In = 0, Out = 1, None = 2 };
```

### 端口连接策略
```cpp
enum class ConnectionPolicy { One, Many };  // 单连接/多连接
```

### 节点角色 (用于获取节点属性)
```cpp
enum class NodeRole {
    Type = 0,              // String 节点类型
    Position = 1,          // QPointF 节点位置
    Size = 2,              // QSize 节点尺寸（可调整大小）
    CaptionVisible = 3,    // bool 标题可见性
    Caption = 4,           // QString 标题文本
    Style = 5,             // JSON 样式
    InternalData = 6,      // QJsonObject 内部状态
    InPortCount = 7,       // unsigned int 输入端口数
    OutPortCount = 9,      // unsigned int 输出端口数
    Widget = 10            // QWidget* 嵌入控件
};
```

### 端口角色 (用于获取端口属性)
```cpp
enum class PortRole {
    Data = 0,                      // std::shared_ptr<NodeData> 数据
    DataType = 1,                  // QString 数据类型
    ConnectionPolicyRole = 2,       // ConnectionPolicy 连接策略
    CaptionVisible = 3,           // bool 端口标签可见性
    Caption = 4                    // QString 端口标签
};
```

## 数据流机制

### 数据传播流程

```
1. 节点数据变化
   NodeDelegateModel::dataUpdated(PortIndex)
        ↓
2. 图模型处理传播
   DataFlowGraphModel::onOutPortDataUpdated(NodeId, PortIndex)
        ↓
3. 读取输出数据，查找连接
   获取连接的目标节点和端口
        ↓
4. 设置输入数据
   DataFlowGraphModel::setPortData(nodeId, PortType::In, PortIndex, data)
        ↓
5. 节点接收数据
   NodeDelegateModel::setInData(std::shared_ptr<NodeData>, PortIndex)
        ↓
6. 节点重新计算并发射信号
   emit dataUpdated(PortIndex) // 触发新一轮传播
```

### NodeData 基类

```cpp
class NODE_EDITOR_PUBLIC NodeData {
public:
    virtual ~NodeData() = default;

    // 判断数据类型是否相同
    virtual bool sameType(NodeData const &nodeData) const;

    // 返回数据类型信息
    virtual NodeDataType type() const = 0;
};

struct NodeDataType {
    QString id;   // 数据类型唯一标识
    QString name; // 数据类型显示名称
};
```

### NodeDelegateModel 基类

```cpp
class NODE_EDITOR_PUBLIC NodeDelegateModel : public QObject {
    Q_OBJECT

public:
    // 节点标题（显示在节点上方）
    virtual QString caption() const = 0;

    // 节点名称（用于注册和识别）
    virtual QString name() const = 0;

    // 端口数量
    virtual unsigned int nPorts(PortType portType) const = 0;

    // 端口数据类型
    virtual NodeDataType dataType(PortType, PortIndex) const = 0;

    // 设置输入数据（接收上游节点传来的数据）
    virtual void setInData(std::shared_ptr<NodeData>, PortIndex) = 0;

    // 获取输出数据（供下游节点使用）
    virtual std::shared_ptr<NodeData> outData(PortIndex) = 0;

    // 获取嵌入的控件（可选）
    virtual QWidget *embeddedWidget() = 0;

signals:
    // 输出端口数据更新
    void dataUpdated(PortIndex);

    // 端口数据失效
    void dataInvalidated(PortIndex);

    // 计算开始
    void computingStarted();

    // 计算结束
    void computingFinished();

    // 端口数量变化
    void portsUpdated();
};
```

## 基本集成步骤

### 1. 定义自定义数据类型

```cpp
// 继承 NodeData，定义可传输的数据类型
class ImageData : public QtNodes::NodeData {
public:
    ImageData() = default;
    ImageData(cv::Mat image) : _image(image) {}

    NodeDataType type() const override {
        return NodeDataType{"image", "SAR Image"};
    }

    cv::Mat image() const { return _image; }

private:
    cv::Mat _image;
};

class MatrixData : public QtNodes::NodeData {
public:
    MatrixData() = default;
    MatrixData(Eigen::MatrixXd mat) : _matrix(mat) {}

    NodeDataType type() const override {
        return NodeDataType{"matrix", "Coordinate Matrix"};
    }

    Eigen::MatrixXd matrix() const { return _matrix; }

private:
    Eigen::MatrixXd _matrix;
};
```

### 2. 定义节点模型

```cpp
// 数据源节点（只有输出端口）
class ImageSourceNode : public QtNodes::NodeDelegateModel {
public:
    QString caption() const override { return "Image Source"; }
    QString name() const override { return "ImageSource"; }

    unsigned int nPorts(PortType portType) const override {
        return portType == PortType::Out ? 1 : 0;
    }

    NodeDataType dataType(PortType, PortIndex) const override {
        return ImageData().type();
    }

    std::shared_ptr<NodeData> outData(PortIndex) override {
        return std::make_shared<ImageData>(_image);
    }

    void setInData(std::shared_ptr<NodeData>, PortIndex) override {}

    QWidget *embeddedWidget() override { return nullptr; }

    void setImage(const cv::Mat& image) {
        _image = image;
        Q_EMIT dataUpdated(0);
    }

private:
    cv::Mat _image;
};

// 处理节点（有输入和输出端口）
class FilterNode : public QtNodes::NodeDelegateModel {
public:
    QString caption() const override { return "Filter"; }
    QString name() const override { return "Filter"; }

    unsigned int nPorts(PortType portType) const override {
        return portType == PortType::In ? 1 : (portType == PortType::Out ? 1 : 0);
    }

    NodeDataType dataType(PortType, PortIndex) const override {
        return ImageData().type();
    }

    void setInData(std::shared_ptr<NodeData> data, PortIndex) override {
        auto imageData = std::dynamic_pointer_cast<ImageData>(data);
        if (imageData) {
            // 执行滤波处理
            cv::GaussianBlur(imageData->image(), _result, cv::Size(5, 5), 0);
            Q_EMIT dataUpdated(0);
        }
    }

    std::shared_ptr<NodeData> outData(PortIndex) override {
        return std::make_shared<ImageData>(_result);
    }

    QWidget *embeddedWidget() override { return nullptr; }

private:
    cv::Mat _result;
};
```

### 3. 注册节点模型

```cpp
// 创建模型注册表
auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();

// 注册数据源节点
registry->registerModel<ImageSourceNode>("Data Sources");

// 注册处理节点
registry->registerModel<FilterNode>("Processing");
registry->registerModel<UnwrapNode>("Processing");
registry->registerModel<DemNode>("Processing");

// 注册输出节点
registry->registerModel<DisplayNode>("Visualization");
```

### 4. 创建图模型和视图

```cpp
// 创建数据流图模型
QtNodes::DataFlowGraphModel dataFlowGraphModel(registry);

// 创建图形场景
auto scene = new QtNodes::DataFlowGraphicsScene(dataFlowGraphModel, parent);

// 创建图形视图
auto view = new QtNodes::GraphicsView(scene);

// 设置视图属性
view->setSceneRect(-10000, -10000, 20000, 20000);

// 添加到主窗口
QVBoxLayout *layout = new QVBoxLayout(parent);
layout->addWidget(view);
```

### 5. 保存/加载流程

```cpp
// 保存到 JSON 文件
QJsonObject sceneJson;
scene->save(sceneJson);

QFile saveFile("workflow.json");
saveFile.open(QIODevice::WriteOnly);
saveFile.write(QJsonDocument(sceneJson).toJson());

// 从 JSON 文件加载
QFile loadFile("workflow.json");
loadFile.open(QIODevice::ReadOnly);
QJsonDocument loadDoc(QJsonDocument::fromJson(loadFile.readAll()));
scene->load(loadDoc.object());
```

## 高级功能

### 1. 撤销/重做

```cpp
// 获取撤销栈
QUndoStack *undoStack = scene->undoStack();

// 连接到菜单
undoAction->setStack(undoStack);
redoAction->setStack(undoStack);
```

### 2. 自定义样式

```cpp
// 设置连线样式
QtNodes::ConnectionStyle::setConnectionStyle(R"(
  {
    "ConnectionStyle": {
      "NormalColor": "#006400",
      "HoveredColor": "#00FF00",
      "SelectedColor": "#0000FF",
      "LineWidth": 3.0,
      "UseDataDefinedColors": true
    }
  }
)");

// 设置节点样式
QtNodes::NodeStyle::setNodeStyle(R"(
  {
    "NodeStyle": {
      "NormalBoundaryColor": "#2a5a8f",
      "SelectedBoundaryColor": "#00FF00",
      "GradientColor0": "#006400",
      "GradientColor1": "#008000",
      "GradientColor2": "#009900",
      "GradientColor3": "#00AA00",
      "FontColor": "white",
      "FontFamily": "Arial",
      "FontSize": 12
    }
  }
)");

// 设置视图背景样式
QtNodes::GraphicsViewStyle::setGraphicsViewStyle(R"(
  {
    "GraphicsViewStyle": {
      "BackgroundColor": "#2b2b2b",
      "GridColor": "#404040",
      "FineGridColor": "#303030",
      "FontSize": 10
    }
  }
)");
```

### 3. 嵌入控件

```cpp
class NumberNode : public QtNodes::NodeDelegateModel {
public:
    QWidget *embeddedWidget() override {
        if (!_widget) {
            _widget = new QLineEdit();
            _widget->setPlaceholderText("Enter number");
            _widget->setStyleSheet("background: #333; color: white;");
            connect(_widget, &QLineEdit::textChanged,
                    this, &NumberNode::onTextChanged);
        }
        return _widget;
    }

    void onTextChanged(const QString& text) {
        _number = text.toDouble();
        Q_EMIT dataUpdated(0);
    }

private:
    QLineEdit *_widget = nullptr;
    double _number = 0.0;
};
```

### 4. 动态端口

```cpp
class DynamicNode : public QtNodes::NodeDelegateModel {
public:
    unsigned int nPorts(PortType portType) const override {
        return portType == PortType::In ? _inputPorts.size() : 1;
    }

    void addInputPort() {
        _inputPorts.append(QString("Port %1").arg(_inputPorts.size()));
        Q_EMIT portsUpdated();
    }

    void removeInputPort() {
        if (!_inputPorts.isEmpty()) {
            _inputPorts.removeLast();
            Q_EMIT portsUpdated();
        }
    }

private:
    QStringList _inputPorts;
};
```

### 5. 竖直布局

```cpp
// 设置节点竖直排列（端口从上到下）
scene->setOrientation(Qt::Vertical);
```

### 6. 锁定节点

```cpp
// 设置节点标志（不可移动/选择）
dataFlowGraphModel.setNodeFlags(nodeId, QtNodes::NodeFlag::Locked);
```

## 序列化格式

### 节点数据
```json
{
  "id": 0,
  "internal-data": {
    "model-name": "ImageSource",
    "model-data": {
      "filename": "scene1.tif",
      "parameters": {...}
    }
  },
  "position": {"x": -338, "y": -160}
}
```

### 连接数据
```json
{
  "inPortIndex": 0,
  "inNodeId": 1,
  "outNodeId": 0,
  "outPortIndex": 0
}
```

### 完整保存格式
```json
{
  "nodes": [
    {"id": 0, "model-name": "ImageSource", "position": {"x": -338, "y": -160}, "internal-data": {...}},
    {"id": 1, "model-name": "Filter", "position": {"x": 100, "y": -160}, "internal-data": {...}}
  ],
  "connections": [
    {"inNodeId": 1, "inPortIndex": 0, "outNodeId": 0, "outPortIndex": 0}
  ]
}
```

## InSAR 应用中的节点设计建议

### 节点分类

| 分类 | 节点类型 | 输入 | 输出 |
|------|---------|------|------|
| 数据导入 | Sentinel导入 | - | ImageData |
| 数据导入 | TSX导入 | - | ImageData |
| 数据导入 | CSK导入 | - | ImageData |
| 数据导入 | ALOS2导入 | - | ImageData |
| 预处理 | S1 Deburst | ImageData | ImageData |
| 预处理 | S1 帧拼接 | ImageData×2 | ImageData |
| 预处理 | S1 条带拼接 | ImageData×3 | ImageData |
| 配准 | 几何配准 | ImageData×2 | ImageData, MatrixData |
| 配准 | S1 BackGeocoding | ImageData×N | ImageData |
| 干涉处理 | 干涉图生成 | ImageData×2 | ImageData |
| 干涉处理 | 滤波 | ImageData | ImageData |
| 干涉处理 | 相位解缠 | ImageData | ImageData |
| 干涉处理 | DEM生成 | ImageData | ImageData |
| 基线处理 | 基线估计 | ImageData×N | BaselineData |
| 基线处理 | 基线组生成 | ImageData×N, BaselineData | PairListData |
| SBAS处理 | 时间序列分析 | ImageData×N, PairListData | TimeSeriesData |
| SBAS处理 | 参考点重选 | TimeSeriesData | TimeSeriesData |
| 可视化 | 图像显示 | ImageData | - |
| 可视化 | 变形预览 | TimeSeriesData | - |
| 可视化 | 基线预览 | BaselineData | - |
| 工具 | 裁剪 | ImageData | ImageData |
| 工具 | 地理编码 | ImageData | ImageData |

### 数据类型设计

```cpp
class ImageData : public QtNodes::NodeData {
    // SAR图像数据（OpenCV Mat或HDF5文件路径）
};

class MetadataData : public QtNodes::NodeData {
    // 元数据（轨道信息、极化方式等）
};

class BaselineData : public QtNodes::NodeData {
    // 基线数据（时间基线、空间基线）
};

class PairListData : public QtNodes::NodeData {
    // 干涉对列表
};

class TimeSeriesData : public QtNodes::NodeData {
    // 时间序列形变数据
};

class CoordinateMatrixData : public QtNodes::NodeData {
    // 坐标变换矩阵
};
```

## 构建和依赖

### 依赖
- Qt >= 5.15 (支持 Qt6)
- CMake >= 3.8

### 构建命令
```bash
cd D:\SRC\nodeeditor
mkdir build && cd build
cmake .. -DUSE_QT6=off  # 或 -DUSE_QT6=on
cmake --build .
```

### CMake 集成
```cmake
find_package(QtNodes REQUIRED)

target_link_libraries(SatExplorer
    QtNodes::QtNodes
    Qt5::Widgets
    Qt5::Core
    Qt5::Gui
    Qt5::Charts
)
```

## 参考示例

| 示例 | 说明 | 位置 |
|------|------|------|
| calculator | 完整的数据流计算器示例 | `examples/calculator/` |
| dynamic_ports | 动态端口管理 | `examples/dynamic_ports/` |
| simple_graph_model | 简化的图模型 | `examples/simple_graph_model/` |
| styles | 自定义样式示例 | `examples/styles/` |
| resizable_images | 可调整大小带控件的节点 | `examples/resizable_images/` |

## 集成到 SatExplorer 的建议

### 1. 添加到 VS 项目

修改 `QtWidgetsApplication3.vcxproj`，添加：
- `D:\SRC\nodeeditor\include` 到 AdditionalIncludeDirectories
- `D:\SRC\nodeeditor\src` 中的源文件
- 链接 `QtNodes` 库

### 2. 创建节点编辑器模块

新建文件：
- `NodeEditorWindow.h/cpp` - 节点编辑器主窗口
- `NodeDataTypes.h` - 自定义数据类型
- `NodeModels.h/cpp` - InSAR 节点模型

### 3. 与现有系统集成

- 将节点编辑器作为 MainWindow 的新标签页或独立窗口
- 使用 InSAR_IPC 与后台处理进程通信
- 将节点图保存到项目文件中（扩展现有的 XML 格式）
