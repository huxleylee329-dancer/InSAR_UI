# SatExplorer UI/UX 设计规范

本文档定义 SatExplorer 项目中所有 UI/UX 组件的颜色、样式和交互规则。

## 目录

- [颜色系统](#颜色系统)
- [节点状态颜色](#节点状态颜色)
- [Detail Window 玻璃态](#detail-window-玻璃态)
- [主题适配规则](#主题适配规则)

---

## 颜色系统

### 主程序主题色板

| 颜色 | 浅色主题 | 深色主题 |
|------|----------|----------|
| 主背景 | #F5F5F5 | #353535 |
| Dock 背景 | #F0F0F0 | #2B2B2B |
| 标题栏背景 | #E0E0E0 | #404040 |
| 文本颜色 | #333333 | #FFFFFF |
| 主要按钮 | #4A9ACF | #4A9ACF |
| 按钮边框 | #666666 | #666666 |
| 按钮 hover | #5AB9DF | #505050 |
| 按钮 pressed | #3A8ABF | #404040 |
| 辅助按钮 | #505050 | #606060 |
| 滚动条 | #F0F0F0 | #2B2B2B |

---

## 节点状态颜色

### 浅色主题

| 状态 | 起始颜色 | 结束颜色 | 说明 |
|------|----------|----------|
| Idle | #F1F5F9 | #E2E8F0 | 浅蓝灰，柔和 |
| Pending | #A7F3D0 | #A7F3D0 | 淡青色，低饱和 |
| Running | #3B82F6 | #60A5FA | 系统蓝，渐变 |
| Completed | #10B981 | #10B981 | 专业绿，纯色 |
| Stopped | #F59E0B | #D97706 | 系统琥珀色，渐变 |
| Warning | #FBBF24 | #F59E0B | 柔和黄，与主程序协调 |
| Error | #EF4444 | #DC2626 | 标准红，渐变 |
| Disabled | #94A3B8 | #94A3B8 | 降饱和灰 |

### 深色主题

| 状态 | 起始颜色 | 结束颜色 | 说明 |
|------|----------|----------|
| Idle | #404040 | #404040 | 深灰，与标题栏一致 |
| Pending | #404040 | #404040 | 深灰 |
| Running | #2B404B | #4A748D | 深蓝，低饱和 |
| Completed | #4AA9CF | #4AA9CF | 亮蓝绿，与主程序协调 |
| Stopped | #FBBF24 | #D97706 | 亮琥珀，与主程序协调 |
| Warning | #FBBF24 | #D97706 | 柔和黄，与主程序协调 |
| Error | #EF4444 | #DC2626 | 标准红，渐变 |
| Disabled | #505050 | #505050 | 深灰，与辅助按钮一致 |

---

## 图标颜色

| 图标 | 颜色 |
|------|------|
| Automatic 模式图标 | #3B82F6 (系统蓝) |
| Manual 模式图标 | #F59E0B (系统琥珀) |
| 播放按钮 | #10B981 (专业绿) |
| 停止按钮 | #EF4444 (专业红) |
| 眼睛图标 | #94A3B8 (柔和灰) |

---

## Detail Window 玻璃态

### 浅色主题

| 元素 | 样式定义 |
|------|----------|
| 窗口背景 | `background-color: rgba(241, 245, 249, 0.95);` |
| 卡片背景 | `background-color: rgba(255, 255, 255, 0.7);` |
| 卡片边框 | `border: 1px solid rgba(30, 58, 138, 0.15);` |
| 卡片标题背景 | `background: rgba(241, 245, 249, 0.5);` |
| 卡片标题文字 | `color: #1E3A8A;` |
| 信息标签背景 | `background-color: rgba(241, 245, 249, 0.6);` |
| 信息标签文字 | `color: #1E3A8A;` |
| 信息标签边框 | `border-left: 3px solid #3B82F6;` |
| 关闭按钮 | `background-color: #3B82F6;` |
| 滚动条背景 | `background: rgba(241, 245, 249, 0.3);` |
| 滚动条手柄 | `background: rgba(59, 130, 246, 0.6);` |

### 深色主题

| 元素 | 样式定义 |
|------|----------|
| 窗口背景 | `background-color: rgba(43, 64, 75, 0.95);` |
| 卡片标题背景 | `background: rgba(64, 64, 64, 0.5);` |
| 卡片标题文字 | `color: #FFFFFF;` |
| 信息标签背景 | `background: rgba(64, 64, 64, 0.6);` |
| 信息标签文字 | `color: #FFFFFF;` |
| 信息标签边框 | `border-left: 3px solid #2B404B;` |
| 关闭按钮 | `background-color: #2B404B;` |
| 滚动条背景 | `background: rgba(64, 64, 64, 0.3);` |
| 滚动条手柄 | `background: rgba(74, 116, 141, 0.6);` |

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

---

## 设计原则

### 颜色使用规则

1. **低饱和度优先**
   - 所有节点状态颜色使用低饱和度版本
   - 避免使用鲜艳的霓虹色
   - 保持与主程序主题色板协调

2. **层次一致性**
   - 浅色主题：使用浅色系，保持清爽感
   - 深色主题：使用深色系，保持对比度

3. **玻璃态效果**
   - 使用半透明背景模拟毛玻璃
   - 细腻边框增强层次感
   - 保持内容可读性

### 交互规则

1. **状态可视化**
   - 每个状态有独特的视觉标识
   - 使用颜色 + 图标组合增强识别度

2. **模式区分**
   - Manual 模式使用左上角 "M" 徽章标识
   - Automatic 模式无额外标识

3. **微交互**
   - 按钮悬停和按下状态有明确反馈
   - 滚动条平滑显示

---

## 视觉组件

### 节点节点

```
┌─────────────────────────────┐
│  [M]  Node Title       ◯  ○   │ ← 深色/浅色自适应
│       ◯  ○                  │
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
