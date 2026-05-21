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

在 `SatExplorer.vcxproj` 中配置：

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

**配置方式：** 使用 QtRcc 自动生成（推荐）

在 `SatExplorer.vcxproj` 中配置：
```xml
<ItemGroup>
  <QtRcc Include="resources\SatExplorer.qrc" />
</ItemGroup>
```

QtRcc 会在构建时自动生成 `$(IntDir)qrc_SatExplorer.cpp`，例如：
- Debug: `x64\Debug\qrc_SatExplorer.cpp`
- Release: `x64\Release\qrc_SatExplorer.cpp`

**关键点：**
- ✅ 只使用 `<QtRcc Include="*.qrc" />` 方式
- ❌ 不要使用手动生成的 `<ClCompile Include="qrc_*.cpp" />`
- QtRcc 支持**增量编译**：仅在 qrc 文件或引用的资源文件修改时才重新生成
- `QtMsBuild\Qt.targets` 中的 QtRcc 目标已添加 `Inputs` 和 `Outputs` 属性以支持增量构建

**如果遇到链接错误：**
```
错误 LNK2001: 无法解析的外部符号 "qInitResources_SatExplorer"
```
确保：
1. QtRcc item group 已启用（未注释）
2. 没有同时使用手动生成的 qrc cpp 文件
3. 清理并重新构建项目

---

## 增量编译优化

### 问题症状
修改任何源文件后，Visual Studio 执行全量编译，耗时超过 1 分钟，而不是只编译修改的文件（通常只需几秒）。

### 问题根因
`QtMsBuild\Qt.targets` 文件中的 `QtMoc`、`QtUic` 和 `QtRcc` 目标缺少 `Inputs` 和 `Outputs` 属性，导致 MSBuild 无法正确跟踪文件依赖关系。每次构建时这些目标都会无条件执行，导致所有 MOC 文件都被重新生成和编译。

### 解决方案

在 `QtMsBuild\Qt.targets` 中为每个 Target 添加 `Inputs` 和 `Outputs` 属性：

**QtUic 目标（第 13-17 行）：**
```xml
<Target Name="QtUic" BeforeTargets="QtMoc"
        Inputs="@(QtUic)"
        Outputs="$(QtIntDir)ui_%(QtUic.Filename).h">
  <Exec Command="&quot;$(QtUicDir)\uic.exe&quot; &quot;%(QtUic.FullPath)&quot; -o &quot;$(QtIntDir)ui_%(Filename).h&quot;" Condition="'%(QtUic.FullPath)' != ''"/>
</Target>
```

**QtMoc 目标（第 26-33 行）：**
```xml
<Target Name="QtMoc" BeforeTargets="ClCompile"
        Inputs="@(QtMoc)"
        Outputs="$(QtIntDir)moc_%(QtMoc.Filename).cpp">
  <Exec Command="&quot;$(QtMocDir)\moc.exe&quot; &quot;%(QtMoc.FullPath)&quot; -o &quot;$(QtIntDir)moc_%(Filename).cpp&quot;" Condition="'%(QtMoc.FullPath)' != ''"/>
  <ItemGroup>
    <ClCompile Include="$(QtIntDir)moc_%(QtMoc.Filename).cpp" Condition="'%(QtMoc.FullPath)' != ''"/>
  </ItemGroup>
</Target>
```

### 修复后的行为

**修改 .cpp 文件：**
- 只重新编译该 .cpp 文件
- MOC 文件不会重新生成（因为头文件未修改）
- 链接步骤执行

**修改 .h 文件：**
- 重新编译该 .cpp 文件
- 重新生成并编译对应的 moc_*.cpp 文件
- 重新编译所有依赖该头文件的其他 .cpp 文件
- 链接步骤执行

### 验证步骤

1. 关闭 Visual Studio
2. 删除缓存目录：
   ```bash
   rm -rf D:/SRC/InSAR_UI/.vs
   rm -rf D:/SRC/InSAR_UI/x64
   ```
3. 重新打开 Visual Studio
4. 重新生成解决方案（第一次会全量编译）
5. 测试增量编译：修改 `Cut.cpp` 的一行，然后编译

### 预期结果

修改 `Cut.cpp` 后，应该只看到：
- `Cut.cpp` 被编译
- `moc_Cut.cpp` **不应该**被重新编译（因为 `include\Cut.h` 没有修改）
- 链接步骤执行
- 编译时间少于 10 秒

### 实际测试结果

✅ **测试通过**（2026-03-03）
- 修改 `Cut.cpp` 后只有该文件被编译
- 编译时间：7 秒（相比修复前的 1 分 15 秒）
- 增量编译正常工作

### 额外配置

在 `SatExplorer.vcxproj` 的 Debug|x64 配置中添加了 `IntDir`：
```xml
<PropertyGroup Condition="'$(Configuration)|$(Platform)' == 'Debug|x64'">
    <OutDir>.\bin\</OutDir>
    <TargetName>SatExplorer</TargetName>
    <IntDir>x64\Debug\</IntDir>
</PropertyGroup>
```

虽然添加 `IntDir` 有助于保持输出目录清晰，但**真正解决增量编译问题**的是在 `Qt.targets` 中添加 `Inputs` 和 `Outputs` 属性。

---

## QtRcc 自动生成优化（2026-03-06）

### 问题背景
之前项目使用手动生成的 `qrc_SatExplorer.cpp` 文件，QtRcc 自动生成被禁用。这种方式有以下缺点：
- 手动维护，容易忘记更新
- 无法利用增量构建优势
- qrc 文件内容变化时需要手动重新生成

### 解决方案

**1. 启用 QtRcc 自动生成**
在 `Qt.targets` 中为 QtRcc 目标添加 `Inputs` 和 `Outputs` 属性以支持增量构建：
```xml
<Target Name="QtRcc" BeforeTargets="QtMoc"
        Inputs="@(QtRcc)"
        Outputs="$(QtIntDir)qrc_%(QtRcc.Filename).cpp">
  <Exec Command="&quot;$(QtRccDir)\rcc.exe&quot; &quot;%(QtRcc.FullPath)&quot; -o &quot;$(QtIntDir)qrc_%(Filename).cpp&quot;" Condition="'%(QtRcc.FullPath)' != ''"/>
  <ItemGroup>
    <ClCompile Include="$(QtIntDir)qrc_%(QtRcc.Filename).cpp" Condition="'%(QtRcc.FullPath)' != ''"/>
  </ItemGroup>
</Target>
```

**2. 启用 QtRcc item group**
在 `SatExplorer.vcxproj` 中取消注释：
```xml
<ItemGroup>
  <QtRcc Include="resources\SatExplorer.qrc" />
</ItemGroup>
```

**3. 移除手动编译配置**
删除：
```xml
<ClCompile Include="qrc_SatExplorer.cpp" />
```

**4. 删除手动生成的文件**
删除项目根目录下的 `qrc_SatExplorer.cpp` 文件。

**5. 移除 PostBuildEvent 复制命令**
删除 Release 配置中的 icon 目录复制命令（不再需要 bin/icon/）：
```xml
<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
  <PostBuildEvent>
    <Command>xcopy /Y /E /I "$(ProjectDir)resources\icon" "$(OutDir)icon"</Command>
  </PostBuildEvent>
</ItemDefinitionGroup>
```

### 图标路径修复

**问题：** 所有 UI 文件和 `icon_source.h` 中的图标路径使用 `:/SatExplorer/bin/icon/`，但 qrc 文件中定义的路径是 `:/SatExplorer/icon/`（没有 bin），导致图标无法显示。

**解决：** 统一修改为正确的资源路径 `:/SatExplorer/icon/`

修改的文件：
- `include/icon_source.h` - 图标常量定义
- `ui/*.ui` - 所有 UI 文件（MainWindow.ui 及 25 个对话框 UI 文件）

修复后，所有图标都通过 Qt 资源系统正确加载，不再依赖 `bin/icon/` 目录。

### 资源文件路径修复

修复 `resources/SatExplorer.rc` 中的 include 路径：
```xml
<!-- 修复前 -->
#include "resource.h"   <!-- 找不到，因为 .rc 在 resources/ 目录 -->

<!-- 修复后 -->
#include "../resource.h"  <!-- 正确指向项目根目录 -->
```

### .gitignore 更新

添加以下忽略规则：
```gitignore
# Resource Compiler temporary files
RCa*

# Screenshot directory for Claude
screenshot/
```

---

## 节点开发规范

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
error LNK2001: 无法解析的外部符号 "qInitResources_SatExplorer"
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
├── qrc_SatExplorer.cpp  # 手动生成的资源文件
├── resources\
│   └── SatExplorer.qrc
├── Debug\                    # obj 文件目录（生成）
├── bin\                      # exe 和 dll 输出目录
├── QtWidget.B5697A67\      # Qt VS Tools 临时目录（正常）
└── SatExplorer.vcxproj    # VS 项目文件
```

---

## 最后更新：2026-03-06

### 更新记录

**2026-03-06：**
- 启用 QtRcc 自动生成：在 `Qt.targets` 中为 QtRcc 添加 `Inputs` 和 `Outputs` 属性
- 移除手动生成的 `qrc_SatExplorer.cpp` 和相关编译配置
- 修复图标路径问题：统一所有 UI 文件和 `icon_source.h` 使用 `:/SatExplorer/icon/` 路径
- 修复资源文件 include 路径：`resources/SatExplorer.rc` 改为 `#include "../resource.h"`
- 移除 PostBuildEvent：删除 Release 配置中的 icon 目录复制命令，删除 Debug 配置中的空 PostBuildEvent
- 更新 .gitignore：添加 `RCa*` 和 `screenshot/` 忽略规则

**2026-03-03：**
- 修复增量编译失效问题：在 `Qt.targets` 中添加 `Inputs` 和 `Outputs` 属性
- 编译时间从全量编译的 1 分 15 秒降至增量编译的 7 秒
- 更新 `SatExplorer.vcxproj` 添加 `IntDir=x64\Debug\` 配置

**2026-03-02：**
- 初始版本，记录 QtNodes 集成和基本构建配置

**2026-05-21：**
- 记录 MSVC 编译包含中文字符的源文件报 `C2001: 常量中有换行符` 和 `C2146` 语法错误的终极解决方案。

---

## 避坑指南：MSVC 中文编码与 `C2001` 错误

### 问题症状
在 `.cpp` 或 `.h` 文件中直接写中文字符串常量（例如 `new QLabel("模型选择：")`），使用 MSVC 编译时报：
- `error C2001: 常量中有换行符`
- `error C2146: 语法错误: 缺少“)”`

### 问题根因
MSVC 编译器在未检测到文件带有 UTF-8 BOM（Byte Order Mark）时，会默认按系统本地编码（GBK/CP936）读取文件。此时，UTF-8 编码的中文字符串结尾字节（如中文全角冒号 `：` 的尾字节）如果与紧接着的英文双引号 `"` 结合，恰好被误判为一个有效的 GBK 宽字符，就会导致编译器认为该字符串常量**没有闭合引号**，从而抛出“常量中有换行符”的错误。

### 解决方案

#### 方案 1（临时规避）：加空格结尾
如果在中文字符和双引号之间加一个英文空格（例如 `"模型选择： "`），这个空格的字节 `0x20` 不会和中文编码尾字节混合成宽字符，可以规避该编译错误。但治标不治本。

#### 方案 2（常见要求）：带 BOM 的 UTF-8 + Pragma
确保文件保存为 **UTF-8 with BOM**，并在文件开头声明：
```cpp
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
```
**注意：** 如果使用 Python 或脚本批量添加 BOM，处理不当极易引入预处理乱码（`C2014: 预处理器命令必须作为第一个非空白空间启动`）。

#### 方案 3（终极最安全方案）：16 进制转义序列替换中文字符
将源文件中的非 ASCII 字符全部通过 Python 脚本（或者手动）转成 `\x` 形式的 UTF-8 字节转义。这样整个源码文件为纯 ASCII，彻底免疫任何编译器的 codepage 猜测问题。
例如：
```cpp
// 原代码: 
new QLabel(QString::fromUtf8("模型选择："));

// 安全替换代码: 
new QLabel(QString::fromUtf8("\xe6\xa8\xa1\xe5\x9e\x8b\xe9\x80\x89\xe6\x8b\xa9\xef\xbc\x9a"));
```
此方法能够确保无论在任何语言环境和工具链中编译，中文都不会乱码且绝不产生编译错误。

---

## 避坑指南： `.vcxproj` 绝对路径硬编码

如果在通过脚本或手动复制项目工程时，发现 `#include` 找不到文件，或者引用的头文件是被旧目录下的同名文件覆盖，请检查 `SatExplorer.vcxproj` 和 `SatExplorer.vcxproj.filters` 中的绝对路径硬编码。
尤其是通过 Qt VS Tools 或手动添加 `.h` 时，可能产生形如 `D:\SRC\InSAR_UI\...` 的绝对路径，需要手动将所有工程文件中的旧路径全局替换为相对路径或当前工作区路径，否则会导致类重复定义 (`error C2011`) 或成员找不到的诡异问题。
