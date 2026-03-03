# 构建配置说明

本文档记录 SatExplorer 项目中 QtNodes 节点编辑器的构建配置经验，避免重复调试。

---

## Qt 静态链接集成要点

### 1. QtNodes 源文件集成

QtNodes 库通过静态链接方式集成到项目中，所有源文件都在项目内部：

**源文件位置：**
- `.cpp` 文件：`QtNodes/src/*.cpp`
- `.hpp` 头文件：`include/QtNodes/internal/*.hpp`

### 2. MOC 配置（关键）

QtNodes 中包含 `Q_OBJECT` 宏的头文件**必须**添加到 `<QtMoc>` 列表，否则会报链接错误。

在 `QtWidgetsApplication3.vcxproj` 中配置：

```xml
<QtMoc Include="include\QtNodes\internal\Definitions.hpp" />
<QtMoc Include="include\QtNodes\internal\AbstractGraphModel.hpp" />
<QtMoc Include="include\QtNodes\internal\BasicGraphicsScene.hpp" />
<QtMoc Include="include\QtNodes\internal\ConnectionGraphicsObject.hpp" />
<QtMoc Include="include\QtNodes\internal\DataFlowGraphicsScene.hpp" />
<QtMoc Include="include\QtNodes\internal\DataFlowGraphModel.hpp" />
<QtMoc Include="include\QtNodes\internal\GraphicsView.hpp" />
<QtMoc Include="include\QtNodes\internal\NodeDelegateModel.hpp" />
<QtMoc Include="include\QtNodes\internal\NodeGraphicsObject.hpp" />
```

**注意：** `Definitions.hpp` 包含 `Q_NAMESPACE` 和 `Q_ENUM_NS` 宏，也必须在 `<QtMoc>` 中。

### 3. Qt 资源文件（qrc）配置

**问题：** 重复编译导致链接错误
```
错误 LNK1120: 1 个无法解析的外部命令
错误 LNK2001: 无法解析的外部符号 "qInitResources_QtWidgetsApplication3"
```

**解决方案：** 使用手动生成的 qrc cpp 文件，禁用 QtRcc 自动生成

1. 手动使用 Qt rcc 生成资源文件：
   ```bash
   rcc resources/QtWidgetsApplication3.qrc -o qrc_QtWidgetsApplication3.cpp
   ```

2. 在 vcxproj 中配置（**不要同时使用两种方式**）：

   ```xml
   <!-- 方案1：使用手动生成的 qrc 文件（推荐） -->
   <ClCompile Include="qrc_QtWidgetsApplication3.cpp" />

   <!-- 禁用 QtRcc 自动生成 -->
   <!--
   <ItemGroup>
     <QtRcc Include="resources\QtWidgetsApplication3.qrc" />
   </ItemGroup>
   -->
   ```

**关键点：**
- ❌ 不要同时使用 `<ClCompile Include="qrc_*.cpp" />` 和 `<QtRcc Include="*.qrc" />`
- ✅ 只用一种方式，推荐使用手动生成的 cpp 文件
- `qrc_QtWidgetsApplication3.cpp` 文件应放在项目根目录

---

## 增量编译优化

### 问题
Qt Visual Studio Tools 可能将部分文件编译到 `QtWidget.B5697A67/x64/Debug/` 而不是 `x64\Debug/` 目录。

### 解决方案

**不修改 IntDir**：改变输出目录会触发完整重建。

**正确做法：**
1. 在 VS 中执行"清理"（Clean）来清除旧文件
2. 然后执行"生成"（Build）进行增量编译
3. 不要频繁修改 .vcxproj 文件（每次修改都可能触发完整重建）

**性能提示：**
- 修改 .cpp 文件后，只需重新编译该文件
- 修改 .h 文件后，会重新编译依赖它的所有 .cpp 文件（包括 MOC 文件）
- 如果不小心修改了 .vcxproj 文件，下次编译会重新编译所有文件

---

## 节点开发规范

### 1. 节点注册方式

使用 `NodeDelegateModelRegistry::registerModel` 注册节点：

```cpp
// 正确的注册方式
registry->registerModel<YourNodeType>("CategoryName");
```

**注意：** `registerModel` 只接受一个 category 参数，不接受自定义名称。节点名称由节点的 `name()` 方法返回。

### 2. 节点结构模板

每个节点需要：
- 继承 `QtNodes::NodeDelegateModel`
- 添加 `Q_OBJECT` 宏
- 实现必要的虚函数

```cpp
class YourNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    YourNode();
    ~YourNode() = default;

    QString caption() const override;
    QString name() const override;
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    QWidget *embeddedWidget() override;

private:
    // 节点特定数据
};
```

---

## 新增节点到项目

当添加新的节点类时，需要在 vcxproj 中添加：

1. **头文件** - 添加到 `<QtMoc>`（如果包含 Q_OBJECT）或 `<ClInclude>`：
   ```xml
   <QtMoc Include="include\YourNode.h" />
   ```

2. **源文件** - 添加到 `<ClCompile>`：
   ```xml
   <ClCompile Include="YourNode.cpp" />
   ```

3. **注册节点** - 在 `NodeModels.cpp` 中：
   ```cpp
   registry->registerModel<YourNode>("YourCategory");
   ```

---

## 测试节点

测试节点位于 `TestNodes.h` 和 `TestNodes.cpp`，用于验证编辑器框架。

**禁用测试节点：** 在 `NodeModels.cpp` 中注释掉：
```cpp
// #define ENABLE_TEST_NODES
```

---

## 节点编辑器使用说明

1. **打开编辑器：** 菜单 → 节点编辑器
2. **创建节点：** 从右侧面板**拖拽**节点到画布（不是点击）
3. **连接节点：** 拖拽节点的输出端口到另一个节点的输入端口
4. **删除节点：** 选中节点后按 Delete 键
5. **保存/加载：** 使用工具栏的保存/加载按钮

---

## 常见编译错误

### 错误：Q_NAMESPACE 未定义
```
error LNK2019: 无法解析的外部符号 "qt_static_metacall"
```
**解决：** 将 `Definitions.hpp` 添加到 `<QtMoc>` 列表

### 错误：qInitResources 未定义
```
error LNK2001: 无法解析的外部符号 "qInitResources_QtWidgetsApplication3"
```
**解决：** 检查 qrc 文件配置，确保只使用一种编译方式（手动或自动，不要同时使用）

### 错误：registerModel 未找到匹配的重载
```
error C2672: "QtNodes::NodeDelegateModelRegistry::registerModel": 未找到匹配的重载函数
```
**解决：** 检查调用方式，只传递 category 参数：
```cpp
registry->registerModel<YourNode>("Category");  // 正确
registry->registerModel<YourNode>("Category", "Name");  // 错误
```

### 错误：QDebug 无法打印复杂类型
```
error C2678: 二进制"<<": 没有找到接受"QDebug"类型的左操作数的运算符
```
**解决：** 遍历并逐个打印，不要直接打印 QSet、QMap 等容器：
```cpp
for (auto it = map.begin(); it != map.end(); ++it) {
    qDebug() << "  Key:" << it->first << "Value:" << it->second;
}
```

---

## 项目文件结构

```
D:\SRC\InSAR_UI\
├── QtNodes\src\              # QtNodes 源文件（静态链接）
├── include\
│   ├── QtNodes\internal\      # QtNodes 头文件
│   ├── NodeEditorWindow.h     # 节点编辑器窗口
│   ├── NodeDataTypes.h        # 自定义数据类型
│   ├── NodeModels.h           # 节点模型注册表
│   └── TestNodes.h           # 测试节点
├── NodeEditorWindow.cpp
├── NodeModels.cpp
├── TestNodes.cpp
├── qrc_QtWidgetsApplication3.cpp  # 手动生成的资源文件
├── resources\
│   └── QtWidgetsApplication3.qrc
├── Debug\                    # obj 文件目录（生成）
├── bin\                      # exe 和 dll 输出目录
├── QtWidget.B5697A67\      # Qt VS Tools 临时目录（正常）
└── QtWidgetsApplication3.vcxproj    # VS 项目文件
```

---

## 最后更新：2026-03-02
