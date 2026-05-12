# SatExplorer UI/UX 设计规范

本文档定义 SatExplorer 项目中所有 UI/UX 组件的颜色、样式和交互规则。

## 目录

- [颜色系统](#颜色系统)
- [节点状态颜色](#节点状态颜色)
- [Detail Window 样式](#detail-window-样式)
- [主题适配规则](#主题适配规则)

---

## 颜色系统

### 主程序主题色板

| 颜色 | 浅色主题 | 深色主题 |
|------|----------|----------|
| 主背景 | #f9f9f9 | #1a1c1c |
| Dock 背景 | #f3f3f3 | #121212 |
| 标题栏背景 | #f3f3f3 | #1a1c1c |
| 文本颜色 | #1a1c1c | #f9f9f9 |
| 主要按钮 | #005fac | #005fac |
| 按钮边框 | #666666 | #666666 |
| 按钮 hover | #0078d7 | #2a2a2a |
| 按钮 pressed | #3a8abf | #404040 |
| 辅助按钮 | #595f66 | #606060 |
| 滚动条 | #f1f1f1 | #2b2b2b |

### Material Design 3 颜色系统

| 颜色名称 | 浅色主题 | 深色主题 |
|----------|----------|----------|
| background | #f9f9f9 | #1a1c1c |
| on-background | #1a1c1c | #f9f9f9 |
| surface | #f9f9f9 | #1a1c1c |
| on-surface | #1a1c1c | #f9f9f9 |
| surface-variant | #e2e2e2 | #2f3131 |
| on-surface-variant | #414752 | #c1c7cf |
| surface-container | #eeeeee | #2f3131 |
| surface-container-low | #f3f3f3 | #2b2b2b |
| surface-container-high | #e8e8e8 | #2a2a2a |
| surface-container-highest | #e2e2e2 | #2a2a2a |
| surface-container-lowest | #ffffff | #1a1c1c |
| primary | #005fac | #a4c9ff |
| on-primary | #ffffff | #001c39 |
| primary-container | #0078d7 | #004884 |
| on-primary-container | #000510 | #d4e3ff |
| primary-fixed | #d4e3ff | #004884 |
| on-primary-fixed | #001c39 | #a4c9ff |
| primary-fixed-dim | #a4c9ff | #0078d7 |
| inverse-primary | #a4c9ff | #005fac |
| secondary | #595f66 | #c1c7cf |
| on-secondary | #ffffff | #161c22 |
| secondary-container | #dde3eb | #41474e |
| on-secondary-container | #5f656c | #dde3eb |
| secondary-fixed | #dde3eb | #41474e |
| on-secondary-fixed | #161c22 | #dde3eb |
| secondary-fixed-dim | #c1c7cf | #595f66 |
| on-secondary-fixed-variant | #41474e | #c1c7cf |
| tertiary | #994700 | #ffb68b |
| on-tertiary | #ffffff | #321300 |
| tertiary-container | #bf5a00 | #753400 |
| on-tertiary-container | #0e0300 | #ffdbc8 |
| tertiary-fixed | #ffdbc8 | #753400 |
| on-tertiary-fixed | #321300 | #ffdbc8 |
| tertiary-fixed-dim | #ffb68b | #bf5a00 |
| on-tertiary-fixed-variant | #753400 | #ffb68b |
| error | #ba1a1a | #ffb4ab |
| on-error | #ffffff | #690005 |
| error-container | #ffdad6 | #93000a |
| on-error-container | #93000a | #ffdad6 |
| outline | #717784 | #8f9198 |
| outline-variant | #c0c7d4 | #414752 |
| inverse-surface | #2f3131 | #f1f1f1 |
| inverse-on-surface | #f1f1f1 | #2f3131 |
| surface-dim | #dadada | #1a1c1c |
| surface-bright | #f9f9f9 | #2f3131 |
| surface-tint | #005fad | #0078d7 |

---

## 节点状态颜色

### 浅色主题

| 状态 | 颜色 | 说明 |
|------|------|------|
| Idle | 灰色（grayscale + opacity-60） | 等待状态，降饱和 |
| Ready | #005fac | 准备处理，主色调 |
| Running | #005fac | 正在处理，主色调 |
| Completed | #10b981 | 处理完成，绿色 |
| Stopped | #f59e0b | 停止状态，琥珀色 |
| Warning | #fbbf24 | 警告状态，黄色 |
| Error | #ef4444 | 错误状态，红色 |
| Disabled | 灰色（grayscale + opacity-40） | 禁用状态，降饱和 |

### 深色主题

| 状态 | 颜色 | 说明 |
|------|------|------|
| Idle | 灰色（grayscale + opacity-60） | 等待状态，降饱和 |
| Ready | #0078d7 | 准备处理，亮蓝色 |
| Running | #0078d7 | 正在处理，亮蓝色 |
| Completed | #4aa9cf | 处理完成，蓝绿色 |
| Stopped | #fbbf24 | 停止状态，琥珀色 |
| Warning | #fbbf24 | 警告状态，黄色 |
| Error | #ef4444 | 错误状态，红色 |
| Disabled | 灰色（grayscale + opacity-40） | 禁用状态，降饱和 |

### 节点头部样式

| 模式 | 头部背景 | 文字颜色 | 图标 |
|------|----------|----------|------|
| Automatic | #005fac | #ffffff | smart_toy |
| Manual | #ffffff | #1a1c1c | back_hand |

---

## 图标颜色

| 图标 | 颜色 |
|------|------|
| Automatic 模式图标 | #005fac (主色调) |
| Manual 模式图标 | #994700 (第三色) |
| 播放按钮 | #10b981 (绿色) |
| 停止按钮 | #ef4444 (红色) |
| 眼睛图标 | #94a3b8 (灰色) |
| 警告图标 | #fbbf24 (黄色) |
| 错误图标 | #ef4444 (红色) |

---

## Detail Window 样式

### 浅色主题

| 元素 | 样式定义 |
|------|----------|
| 窗口背景 | `background-color: #f9fafb;` |
| 卡片背景 | `background-color: #ffffff;` |
| 卡片边框 | `border: 1px solid #e5e7eb;` |
| 卡片标题背景 | `background-color: #ffffff;` |
| 卡片标题文字 | `color: #1e3a8a;` |
| 信息标签背景 | `background-color: #f3f4f6;` |
| 信息标签文字 | `color: #6b7280;` |
| 关闭按钮 | `background-color: #374151;` |
| 滚动条背景 | `background: #f1f1f1;` |
| 滚动条手柄 | `background: #c1c1c1;` |
| 滚动条手柄 hover | `background: #a8a8a8;` |

### 深色主题

| 元素 | 样式定义 |
|------|----------|
| 窗口背景 | `background-color: #1f2937;` |
| 卡片背景 | `background-color: #374151;` |
| 卡片边框 | `border: 1px solid #4b5563;` |
| 卡片标题背景 | `background-color: #374151;` |
| 卡片标题文字 | `color: #ffffff;` |
| 信息标签背景 | `background-color: #4b5563;` |
| 信息标签文字 | `color: #d1d5db;` |
| 关闭按钮 | `background-color: #6b7280;` |
| 滚动条背景 | `background: #374151;` |
| 滚动条手柄 | `background: #6b7280;` |
| 滚动条手柄 hover | `background: #9ca3af;` |

### 端口连接点样式

| 元素 | 样式定义 |
|------|----------|
| 连接点宽度 | 8px |
| 连接点高度 | 8px |
| 连接点形状 | 圆形 (border-radius: 50%) |
| 输入连接点背景 | #94a3b8 |
| 输入连接点边框 | #64748b |
| 输出连接点背景 | #3b82f6 |
| 输出连接点边框 | #2563eb |

---

## 主题适配规则

### 实施方式

1. **动态颜色选择**
   - 节点颜色根据父窗口主题属性自动切换
   - Detail Window 根据主题应用对应样式表

2. **主题检测**
   ```cpp
   bool isDarkTheme(QWidget* parent) {
       QVariant bgColor = parent->property("theme-background");
       if (bgColor.isValid()) {
           QColor color = bgColor.value<QColor>();
           // 深色主题：背景色较深
           if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
               return true;
           }
       }
       return false;
   }
   ```

3. **主题属性设置**
   - 主程序切换主题时需设置 `theme-background` 属性
   - Detail Window 通过父窗口属性检测主题

4. **CSS 类切换**
   - 使用 `dark` 类来控制深色主题
   - 浅色主题默认使用 `light` 类

---

## 设计原则

### 颜色使用规则

1. **Material Design 3 颜色系统**
   - 使用 Material Design 3 颜色系统作为基础
   - 主色调 #005fac 用于主要操作和状态
   - 第三色 #994700 用于 Manual 模式标识

2. **层次一致性**
   - 浅色主题：使用浅色系，保持清爽感
   - 深色主题：使用深色系，保持对比度

3. **状态可视化**
   - 使用颜色 + 图标组合增强识别度
   - Idle 和 Disabled 状态使用降饱和效果

### 交互规则

1. **状态可视化**
   - 每个状态有独特的视觉标识
   - 使用颜色 + 图标组合增强识别度

2. **模式区分**
   - Manual 模式使用不同的头部样式（白色背景 + 橙色图标）
   - Automatic 模式使用蓝色头部背景

3. **微交互**
   - 按钮悬停和按下状态有明确反馈
   - 滚动条平滑显示
   - 图标悬停时有缩放效果

---

## 视觉组件

### 节点结构

```
┌─────────────────────────────┐
│  [图标]  Node Title    ◯  ○   │ ← 深色/浅色自适应
│       ◯  ○                  │
│  [内容区域]                   │
│  [状态栏]                     │
└─────────────────────────────┘
```

### Detail Window

```
┌─────────────────────────────────┐
│  ┌─ Input ─┬─ Process ─┬─ Output ─┐
│  │ Port 1  │  [ID:123] │  Value A │  │
│  │ Port 2  │  [Type:Data]│  │         │  │
│  └────────┘ └────────────┘ └──────────┘
│                                   │
│                        [Close]        │
└─────────────────────────────────┘
```

### 工具栏按钮

```
┌─────────────────────────────────┐
│  [图标]                         │
│  [文字]                         │
└─────────────────────────────────┘
```

---

## 实现文件

- `include/QtNodes/internal/ExecutableNodePainter.hpp` - 节点绘制器头文件
- `QtNodes/src/ExecutableNodePainter.cpp` - 节点绘制器实现（含主题适配）
- `include/QtNodes/internal/NodeDetailWindow.hpp` - Detail Window 样式定义（含深浅主题）
- `QtNodes/src/NodeDetailWindow.cpp` - Detail Window 实现（含主题检测）

---

## 注意事项

1. **主题切换测试**
   - 浅色和深色主题都需要测试
   - 验证颜色对比度符合 WCAG AA 标准（4.5:1）

2. **性能优化**
   - 颜色使用 constexpr 避免运行时计算
   - 主题检测通过属性缓存提高效率

3. **一致性检查**
   - 添加新组件时需参考本文档定义的颜色
   - 修改颜色时需同步更新文档

4. **Material Design 3 遵循**
   - 遵循 Material Design 3 设计规范
   - 使用 Inter 字体作为主要字体
   - 使用 Material Symbols Outlined 图标库
