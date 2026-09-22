# QSS 样式系统说明文档

## 概述

本项目使用 Qt 样式表（QSS）系统进行统一的外观管理。样式文件位于 `stylesheets/` 目录，支持三种主题切换：Light（浅色）、Dark（深色）、Fusion（扁平）。

---

## 目录结构

```
stylesheets/
├── README.md               # 本文档
├── application.qss          # 全局基础样式（字体、菜单、工具栏）
├── widgets.qss              # 通用控件样式（按钮、输入框、下拉框等）
├── dialogs.qss              # 对话框样式
├── mainwindow.qss          # 主窗口特定样式
├── nodeeditor.qss          # 节点编辑器样式
├── importnodes.qss          # 导入节点样式
└── themes/                # 主题变体
    ├── light.qss           # 浅色主题（默认）
    ├── dark.qss            # 深色主题
    └── fusion.qss         # Fusion扁平主题
```

---

## QSS 优先级（从高到低）

1. **themes/xxx.qss** - 主题特定样式，覆盖基础样式
2. **importnodes.qss** - 导入节点特定样式
3. **nodeeditor.qss** - 节点编辑器样式
4. **mainwindow.qss** - 主窗口样式
5. **dialogs.qss** - 对话框样式
6. **widgets.qss** - 通用控件样式（最基础）
7. **application.qss** - 全局基础样式

---

## 新旧效果对比

### 1. MainWindow 主窗口

| 组件 | 之前 | 之后 (Fusion主题) | 修改文件 |
|--------|-------|------------------|----------|
| **菜单栏** | Qt 默认白色/灰色 | Fusion 蓝色按钮，白色文字 | `mainwindow.qss` 或 `fusion.qss` |
| **工具栏** | Qt 默认 | Fusion 风格 | 同上 |
| **TreeView** | Qt 默认白色底，蓝色选中 | Fusion：无边框，蓝色选中项 | `mainwindow.qss` 中的 `MainWindow QTreeView` |
| **TabWidget** | Qt 默认 | Fusion：灰色未选中，蓝色选中项 | `widgets.qss` 中的 `QTabBar` |
| **进度条** | Qt 默认 | Fusion：蓝色进度块 (#4a9acf) | `widgets.qss` 中的 `QProgressBar` |
| **状态栏** | Qt 默认灰色 | Fusion 深灰色 | `application.qss` 中的 `QStatusBar` |

---

### 2. Import/Preprocessing 节点（对话框）

| 控件 | 之前 | 之后 | 修改文件 |
|------|-------|-------|----------|
| **按钮** | Qt 默认立体按钮 | Fusion：扁平蓝色按钮 | `fusion.qss` 中的 `QPushButton` |
| **输入框** | Qt 默认白色底，立体边框 | Fusion：白色底，细边框，聚焦时蓝色 | `fusion.qss` 中的 `QLineEdit` |
| **下拉框** | Qt 默认 | Fusion：白色底，细边框 | `fusion.qss` 中的 `QComboBox` |
| **复选框** | Qt 默认 | Fusion：圆角方框，选中蓝色 | `fusion.qss` 中的 `QCheckBox` |
| **分组框** | Qt 默认边框 | Fusion：圆角，蓝色标题 | `fusion.qss` 中的 `QGroupBox` |

**示例：** Sentinel-1、TerraSAR-X、ALOS-2 等导入对话框现在统一使用 Fusion 扁平风格。

---

### 4. Node Editor（节点编辑器窗口）

| 组件 | 之前 | 之后 | 修改文件 |
|------|-------|-------|----------|
| **画布** | 白色背景，无特殊样式 | Fusion：白色画布 | `nodeeditor.qss` 中的 `PaletteGraphicsView` |
| **节点树** | Qt 默认 | Fusion：无边框树形 | `nodeeditor.qss` 中的 `NodeTreeWidget` |
| **属性面板** | Qt 默认 | Fusion：深灰底色 | `nodeeditor.qss` 中的 `PropertyEditor` |
| **搜索框** | Qt 默认 | Fusion：圆角输入框 | `nodeeditor.qss` 中的 `QLineEdit#searchBox` |

---

### 5. ADS 组件

| 组件 | 之前 | 之后 | 修改文件 |
|------|-------|-------|----------|
| **DockWidget** | ADS CSS 样式 | QSS + ADS CSS **共存** | QSS 优先级较低，可能被 ADS 覆盖 |
| **Dock 标题栏** | ADS CSS | 可能被 ADS 控制 | 需要在 ADS 代码中修改，或在 CSS 文件中修改 |

**注意：** ADS 组件有自己的 CSS 系统（非 QSS），QSS 对其影响有限。如需修改 ADS 样式，需要：
1. 找到 `ADS/` 目录下的 CSS 文件
2. 直接修改 CSS 内容

---

### 6. QtNodes 框架

| 组件 | 之前 | 之后 | 修改文件 |
|------|-------|-------|----------|
| **节点** | `DefaultStyle.json` 控制 | JSON 仍控制节点外观 | 修改 `DefaultStyle.json` |
| **连接线** | JSON 控制 | 同上 | 修改 `DefaultStyle.json` |
| **端口** | JSON 控制 | 同上 | 修改 `DefaultStyle.json` |
| **节点内控件** | 无样式 | 现在有 QSS 样式 | `widgets.qss` 统一控制 |

**结论：** QtNodes 节点外观仍由 `DefaultStyle.json` 控制，但节点内部的控件（按钮、输入框等）现在有 QSS 样式。

---

## 颜色规范

### 主题颜色

```css
/* 主要颜色 */
Primary Blue:    #4a9acf  (Source Node, 主按钮)
Secondary Blue:  #3a7aaf  (Math Node, Display Node)

/* Light Theme */
Background Light:  #F5F5F5
Text Dark:        #333333
Border:           #CCCCCC
Selection:        #4a9acf

/* Dark Theme */
Background Dark:   #353535
Text Light:       #FFFFFF
```

---

## 如何修改样式效果

### 修改现有样式

所有 QSS 文件位于 `D:\SRC\InSAR_UI\stylesheets\` 目录。

### QSS 选择器语法

```css
/* 类选择器 */
QPushButton {
    background-color: #4a9acf;
}

/* ID 选择器（使用 objectName） */
QLineEdit#exampleLineEdit {
    background-color: #4a9acf;
}

/* 层级选择器 */
MainWindow QTreeView {
    background-color: white;
}

/* 状态伪类 */
QPushButton:hover {
    background-color: #3a7aaf;
}

QPushButton:pressed {
    background-color: #2a6a9f;
}

QPushButton:disabled {
    color: #AAAAAA;
}

/* 自定义属性 */
QPushButton.primary {
    background-color: #4a9acf;
}
```

### 修改示例

#### 场景1：修改 Fusion 主题的按钮颜色

编辑 `stylesheets/themes/fusion.qss`，找到 `QPushButton` 部分：
```css
QPushButton {
    background-color: #4a9acf;  /* 改这里 */
    color: white;
    font-weight: bold;
}
```

#### 场景2：修改所有主题的输入框边框

编辑 `stylesheets/widgets.qss`（影响所有主题）：
```css
QLineEdit {
    border: 2px solid #999999;  /* 改边框 */
}
```

#### 场景3：修改节点编辑器的搜索框

编辑 `stylesheets/nodeeditor.qss`：
```css
QLineEdit#searchBox {
    background-color: #F0F0F0;  /* 改背景 */
    border: 2px solid #4a9acf;  /* 改边框 */
}
```

#### 场景5：修改主窗口 TreeView 样式

编辑 `stylesheets/mainwindow.qss`：
```css
MainWindow QTreeView {
    background-color: #FAFAFA;  /* 改背景 */
    border: 1px solid #E0E0E0;  /* 改边框 */
}

MainWindow QTreeView::item:selected {
    background-color: #4a9acf;  /* 改选中颜色 */
    color: white;
}
```

### 测试修改

1. 修改 QSS 文件后，**重新运行程序**（QSS 文件直接从文件读取，无需重新编译）
2. 或者在运行时切换主题（Appearance 菜单）来刷新样式
3. 修改立即生效，无需重启（除非修改代码逻辑）

---

## 主题切换

### 切换方法

1. 在主菜单中点击 **Appearance** → 选择主题：
   - Light Theme（浅色）
   - Dark Theme（深色）
   - Fusion Theme（扁平）

2. 主题选择会自动保存到 `Config.ini`：
```ini
[Appearance]
Theme=fusion  # 或 light/dark
```

3. 下次启动程序时自动加载上次选择的主题

---

## 特殊情况说明

### ADS 组件

ADS 组件样式需要修改 CSS 文件（在 `ADS/` 目录下）：
- CSS 优先级通常高于 QSS
- 如果 ADS 样式和 QSS 冲突，ADS 会覆盖
- 修改方式：直接编辑 `.css` 文件

### QtNodes 节点外观

节点本身的外观（颜色、阴影、边框）由 `DefaultStyle.json` 控制，而非 QSS。如需修改：
- 编辑 `resources/DefaultStyle.json` 或 QtNodes 配置目录中的该文件
- QSS 只影响节点**内部**的控件样式

### 添加新的 objectName 控件

如果要给新控件添加特殊样式：
1. 在代码中设置 `objectName`：
```cpp
myWidget->setObjectName("myCustomWidget");
```

2. 在 QSS 文件中添加样式：
```css
QWidget#myCustomWidget {
    background-color: #4a9acf;
}
```

---

## 常用 QSS 选择器

| 选择器 | 语法 | 示例 |
|--------|------|------|
| 通用类型 | `TypeName` | `QPushButton` |
| ID 选择器 | `TypeName#idName` | `QLineEdit#searchBox` |
| 类选择器 | `.className` | `.primary` |
| 层级选择器 | `Parent Child` | `MainWindow QTreeView` |
| 伪类状态 | `:state` | `QPushButton:hover` |
| 属性选择器 | `[property=value]` | `QCheckBox[checked=true]` |

### 常用伪类

- `:hover` - 鼠标悬停
- `:pressed` - 按下状态
- `:checked` - 选中状态
- `:disabled` - 禁用状态
- `:selected` - 选中项
- `:focus` - 获得焦点
- `:first` - 第一个子元素
- `:last` - 最后一个子元素

---

## 相关代码文件

| 文件 | 作用 | 修改时机 |
|------|------|----------|
| `main.cpp` | QSS 加载和初始化 | 修改加载逻辑、添加新的样式文件 |
| `MainWindow.h/cpp` | 主题切换功能 | 添加新的主题选项、菜单项 |
| `Config.ini` | 主题配置持久化 | 修改默认主题、添加新配置项 |
---

## 调试

### 查看 QSS 加载日志

运行程序时，控制台会输出 QSS 文件加载信息：
```
Loaded stylesheet: "D:/SRC/InSAR_UI/stylesheets/application.qss" Size: 123 chars
Loaded stylesheet: "D:/SRC/InSAR_UI/stylesheets/widgets.qss" Size: 4567 chars
...
```

如果看到 `Failed to load stylesheet`，说明文件路径有问题。

### 常见问题

1. **样式未生效**
   - 检查文件路径是否正确
   - 检查 CSS 优先级（ADS 可能覆盖 QSS）
   - 检查 objectName 是否正确设置

2. **样式冲突**
   - ADS CSS 优先级高，需要在 CSS 文件中修改
   - 检查 QSS 选择器是否过于宽泛

3. **性能问题**
   - 减少 QSS 文件大小
   - 避免过于复杂的选择器
   - 使用 ID 选择器而非通用选择器

---

## 更新日志

- 2026-03-20: 初始实现 QSS 现代化，支持 Light/Dark/Fusion 三种主题
