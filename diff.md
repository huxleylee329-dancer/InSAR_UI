# 暂存区变更交接包

## 比较对象与范围

- **基线提交**：`30c7d9114f4b743253414880a72e103dbf32f338`（`2026-07-23T18:14:05+08:00`，提交说明：`S1 Tops Back Geocoding调试及template_dem工程移除`）。
- **比较对象**：本仓库当前 Git index（暂存区）；即命令 `git diff --cached 30c7d9114f4b743253414880a72e103dbf32f338` 的结果。
- **快照含义**：该提交正是当前 `HEAD`，所以此处不是两个提交之间的比较，而是“该提交中的文件内容”与“当前已暂存、准备提交的文件内容”之间的差异。
- **覆盖范围**：21 个暂存文件，`2,830` 行新增、`182` 行删除。下方为完整、未省略的 Git unified diff；目标项目应以其自身对应文件当前内容为基础，按 hunk 的上下文合并，不应整体覆盖目标文件。
- **未纳入的工作区内容**：`.claude/worktrees/`、`benchmark.md`、`insar-insar-sentinel-1-insar-streamed-wozniak.md`、`internal_algorithm_comparison_plan.md`、`plugin_extension_plan.md` 均为未跟踪项，不属于暂存区，故不在本补丁中。

## 变更索引

| 模块 | 文件 | 变更摘要 |
| --- | --- | --- |
| 配准与裁剪质量评估 | `CoregistrationNode.cpp`, `CoregistrationWorker.cpp`, `include/CoregistrationNode.h`, `include/CoregistrationWorker.h`, `CutNode.cpp` | 增加裁剪配准评估界面、全图相干/相位图预览和残余行列偏移显示；工作器记录 `offset_row`/`offset_col`，并调整仿射偏移反采样和配准流程。 |
| DEM 自动执行与诊断 | `DemNode.cpp`, `DemWorker.cpp`, `include/DemNode.h` | 在执行前提交并校验界面参数，保存本次执行快照；新增 DEM 生成结果诊断与可视化详情。 |
| 去噪诊断 | `DenoiseNode.cpp`, `DenoiseWorker.cpp` | 扩展去噪结果评估视图、统计指标及工作器输出处理。 |
| 干涉图形成诊断 | `InterferometricFormationNode.cpp`, `include/InterferometricFormationNode.h` | 新增干涉结果详情/评估界面，并补强输出恢复和处理完成后的状态。 |
| 解缠诊断 | `UnwrapNode.cpp`, `UnwrapWorker.cpp`, `include/UnwrapNode.h` | 新增解缠结果自动诊断与详情展示；工作器调整输出文件处理。 |
| 通用节点详情与状态 | `QtNodes/src/ExecutableNodeDelegateModel.cpp`, `QtNodes/src/NodeDetailWindow.cpp`, `include/QtNodes/internal/NodeDetailWindow.hpp` | 输入数据更新时清理失败信息；扩展校验对比表与基础详情窗口的布局、主题/状态展示能力。 |
| TOPS 回地理编码 | `S1TopsBackGeocodingNode.cpp`, `S1TopsBackGeocodingWorker.cpp` | 调整诊断文案与处理日志/错误信息。 |
| 人工验证工具 | `scratch_interf_eval.cpp`（新增） | 436 行独立的干涉评估/调试草稿程序；项目集成时应判断是否需要加入工程，补丁本身未修改 `.vcxproj` 或 `.filters`。 |

## 合并注意事项

- 这是一组 UI 与工作器协同改动。新节点详情页依赖 `NodeDetailWindow` 的新增接口；配准评估依赖底层 `Registration.h` 中的 `CropEvalResult` 和 `AnalyzeCropRegistration`。目标项目缺少相应 DLL/API 时，不能只拷贝 UI 代码。
- 本补丁没有改动 `SatExplorer.vcxproj`、`SatExplorer.vcxproj.filters` 或 Designer `.ui` 文件。若目标项目需要编译新增 `scratch_interf_eval.cpp`，必须由目标项目自行决定并同步更新工程文件。
- 补丁只表达暂存区内容，未包含未暂存修改或未跟踪资料。合并前应对照目标项目的 Qt 5.15、C++14 约束以及现有 DLL 接口。

## 逐文件作用说明（供合并模型参考）

### 整体意图和依赖链

这不是单一功能补丁，而是“处理参数可追溯 + 结果质量诊断”的一组协同修改。合并时应优先保持以下链路完整：

1. 处理工作器将实际运行参数和结果写入 H5；相应节点的详情/验证页从 H5 读取这些记录，与当前节点配置和输入数据进行比较。
2. `NodeDetailWindow`/`BaseValidationWidget` 提供这些大型诊断页共享的布局、表格和异步加载能力；诊断节点不能只合并自身 `.cpp` 而遗漏这两个公共文件和头文件声明。
3. 配准链路将偏移量升级为双精度子像素值，并贯穿 H5 持久化、项目 XML 恢复、裁剪结果评估和界面展示；任何一处仍按整数处理都会破坏该链路。

下列说明均以本次暂存 diff 为依据；“诊断”只核验文件、元数据和数值一致性，不等价于算法科学正确性。除特别说明外，目标项目应同时合并同名头文件声明和实现文件。

### 配准、裁剪与回地理编码

- **`CoregistrationNode.cpp`**：
  - 将恢复输出/序列化过程兼容 `offset_row`、`offset_col` 的双精度值，避免项目恢复时截断子像素偏移。
  - 禁用旧的网格插值参数控件并说明底层已采用抛物线拟合；新增“配准评估”页，异步调用 `AnalyzeCropRegistration`，展示全图相干性、相位图、统计量、残余 Y/X 偏移和 PASS/WARNING/低相干状态。
  - 评估结果使用节点地址构造临时 JPG 名称，销毁时清理文件；依赖 `Registration.h` 的 `CropEvalResult` ABI 和 `ImageView`。

- **`CoregistrationWorker.cpp`**：
  - 是本组功能的算法与数据源改动。块匹配由“插值后匹配再除以倍数”改为直接 `real_coherent` 子像素匹配；仅接受 `SNR >= 3.0` 的块，并对无效块和离群块分开统计。
  - 将偏移矩阵、输出偏移、H5 `offset_row`/`offset_col` 及汇总容器从整数改为 `double`/`CV_64F`；在每个副影像上持久化累积的子像素偏移。
  - 以保留块拟合行/列仿射模型，新增反向仿射坐标映射的双线性复数重采样，替代被 `#if 0` 保留的旧路径；同时增加块质量、拟合 RMS、中心预测和重采样参数日志。该文件与底层 `Registration::real_coherent` 的新签名和偏移符号约定强耦合，不能只移植 UI。

- **`include/CoregistrationNode.h`**：声明配准节点用于质量评估的新详情页工厂/辅助接口，必须与 `CoregistrationNode.cpp` 一起合并。

- **`include/CoregistrationWorker.h`**：声明新的 `ResampleSlaveInverseWithAffineOffset` 私有辅助函数，使 `.cpp` 的反向仿射重采样可编译。

- **`CutNode.cpp`**：扩展既有裁剪配准评估页，调用前将 `CropEvalResult::structSize` 设为 ABI 要求的大小，并增加残余 Y/X 偏移显示；任一方向绝对值大于 `0.5` 像元时用红色提示。它消费与 `CoregistrationNode` 相同的 DLL 评估接口。

- **`S1TopsBackGeocodingNode.cpp`**：为 `AlignmentResult` 及其数组做零初始化并填入 `structSize`，适配带结构大小字段的 DLL ABI；质量判断由整数零偏移改为小于 `0.01` 像元的双精度近零判断，日志以两位小数记录残余偏移。

- **`S1TopsBackGeocodingWorker.cpp`**：与节点端一致，调用对齐评估 API 前零初始化 `AlignmentResult[5]` 并设置每个元素的 `structSize`，避免未初始化字段或 ABI 尺寸协商失败。

### DEM、去噪、干涉与解缠的可追溯诊断

- **`DemNode.cpp`**：
  - 自动执行前显式提交界面参数、校验输出节点名和 Newton 迭代次数，并保存本轮的预备参数；这使自动工作流不能在参数不完整时进入运行状态。
  - 新增 DEM 结果诊断页：按输入/输出名称配对 H5，检查 `dem` 尺寸、有限高程像元、min/max/均值/标准差、关键下游依赖数据集，以及实际记录的方法和迭代次数。计算在后台完成，旧 H5 未记录元数据时显示诊断而非伪造“匹配”。

- **`include/DemNode.h`**：声明参数提交函数和本轮执行快照字段，支撑 `DemNode.cpp` 的执行前校验与后台诊断。

- **`DemWorker.cpp`**：在每个 DEM 输出 H5 中写入 `dem_generation_method` 与 `dem_generation_iterations`。这是 `DemNode` 验证页比较“当前配置”和“实际产物”的证据来源；旧产物可以没有这些字段。

- **`DenoiseNode.cpp`**：将去噪验证从简单的线性均值/标准差比较升级为缠绕相位诊断：读取实际写入的算法与窗口参数，计算输入/输出的缠绕相位差圆均值、圆标准差、集中度，以及梯度 RMS 和相位残差点密度的变化。尺寸不一致或旧 H5 缺少元数据时明确显示不可比/未记录。

- **`DenoiseWorker.cpp`**：在去噪输出 H5 写入 `denoise_method`；Slope 分支额外写入 `denoise_slope_pre_win` 和 `denoise_slope_win`。字段名必须与 `DenoiseNode.cpp` 的读取逻辑一致，否则详情页只能显示旧结果状态。

- **`InterferometricFormationNode.cpp`**：新增干涉质量评估页。它从已恢复的相位/相干 H5/JPG 输出中选择影像对，在后台读取相干矩阵，按至多约 500 万采样计算均值、中位数、最大值和高相干比例，并基于经验阈值给出三档状态。输出恢复和完成状态也相应补强。该页是结果解释工具，不改变干涉形成算法。

- **`include/InterferometricFormationNode.h`**：公开干涉评估详情页工厂并调整相关访问权限，使 `InterferometricFormationNode.cpp` 中的新评估组件能访问节点的输出状态。

- **`UnwrapNode.cpp`**：新增解缠结果验证页。对每个输入/输出 H5 配对，核查输出存在性、尺寸、记录的解缠方法/阈值和元数据一致性；计算重新缠绕残差 RMSE/P95、有效像元覆盖率、有效区域连通域、最大区域比例与高梯度候选风险区域。该诊断明确提示：这些指标不能单独证明不存在整数周模糊。

- **`include/UnwrapNode.h`**：声明解缠验证详情页工厂，配合 `UnwrapNode.cpp` 的新组件。

- **`UnwrapWorker.cpp`**：在每个解缠输出 H5 写入 `unwrap_method` 和 `unwrap_coherence_threshold`；这是 `UnwrapNode` 判断输出是否由当前参数产生的唯一持久化依据。写入失败会使该处理分支返回失败，而不是生成无法追溯的产物。

### 公共执行与详情页基础设施

- **`QtNodes/src/ExecutableNodeDelegateModel.cpp`**：自动触发执行前清空上一轮启动拒绝信息；如果子类在 `processAutomatically()` 中拒绝启动，则发出 `executionStartRejected`。这让 `DemNode` 等执行前校验能向工作流报告准确失败原因，而不会残留旧错误。

- **`QtNodes/src/NodeDetailWindow.cpp`**：
  - `ValidationComparisonTable` 新增只展示诊断值的行，适合“旧结果未记录元数据”等非通过/失败的事实。
  - `BaseValidationWidget` 允许自定义比较表标题、横向或纵向内容布局、可滚动特征面板和网格特征布局；改进长文本换行、尺寸策略和状态描述显示。DEM/解缠等诊断页依赖这些能力，否则大表格会被挤压或溢出。

- **`include/QtNodes/internal/NodeDetailWindow.hpp`**：同步公开 `addDiagnostic`、扩展 `setupBaseUI` 参数、保存特征滚动区并声明 `replaceFeatureFormWithGrid`；必须与对应 `.cpp` 同步合并，属于公共 API 变更。

### 独立草稿文件

- **`scratch_interf_eval.cpp`（新增）**：包含一份 436 行的干涉形成评估组件草稿，内容与 `InterferometricFormationNode.cpp` 中的评估页高度重复，且本补丁没有将它加入 `.vcxproj`/`.filters`。它是人工试验/参考实现，不是当前应用的编译单元；目标项目应默认不加入工程，除非先去重并明确其用途。

## 完整补丁

diff --git a/CoregistrationNode.cpp b/CoregistrationNode.cpp
index bb571ea27f036135b6aca3c518bfcda1fcd09ce4..2d1db06d57bbbbc7dcc413df7b5918e44bc8f5d5 100644
--- a/CoregistrationNode.cpp
+++ b/CoregistrationNode.cpp
@@ -20,6 +20,12 @@
 #include <QFileDialog>
 #include <QtConcurrent/QtConcurrent>
 
+
+#include "Registration.h"
+#include "ImageView.h"
+#include "QtNodes/internal/NodeDetailWindow.hpp"
+#include <QTimer>
+
 namespace QtNodes {
 
 CoregistrationNode::CoregistrationNode()
@@ -325,7 +331,7 @@ void CoregistrationNode::createWidget()
             }
         }
     });
-    
+
     m_demBrowseBtn = new QPushButton(QStringLiteral("浏览..."));
     m_demBrowseBtn->setStyleSheet(
         "QPushButton:disabled {"
@@ -454,10 +460,10 @@ void CoregistrationNode::updateParameterWidgetsEnableState()
     if (m_methodCombo) m_methodCombo->setEnabled(enableWidgets);
     if (m_defaultFirstMasterCheckBox) m_defaultFirstMasterCheckBox->setEnabled(enableWidgets);
     if (m_masterImageCombo) m_masterImageCombo->setEnabled(enableWidgets && !m_defaultFirstMaster);
-    
+
     bool isCoarse = (m_method == "Coarse");
     bool hasDemConn = (m_demInputData != nullptr);
-    if (m_interpCombo) m_interpCombo->setEnabled(enableWidgets && isCoarse);
+    if (m_interpCombo) { m_interpCombo->setEnabled(false); m_interpCombo->setToolTip(QStringLiteral("底层算法已升级为抛物线拟合，无需网格插值")); }
     if (m_blockSizeCombo) m_blockSizeCombo->setEnabled(enableWidgets && isCoarse);
     if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !isCoarse && !hasDemConn);
     if (m_demPathEdit) m_demPathEdit->setEnabled(enableWidgets && !isCoarse && !hasDemConn);
@@ -1044,7 +1050,7 @@ bool CoregistrationNode::validateAndRestoreOutput()
                 NodeUtils::Hdf5Locker locker;
                 FormatConversion FC;
                 Utils util;
-                
+
                 Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
                 Mat tmp_double = Mat::zeros(1, 1, CV_64FC1);
                 double interp_interval = 0;
@@ -1061,7 +1067,7 @@ bool CoregistrationNode::validateAndRestoreOutput()
                     NodeUtils::readMatFromH5(masterPath, "state_vec", State_Vec_Master, CV_64F);
                     NodeUtils::readMatFromH5(masterPath, "lon_coefficient", Lon_Coeff_Master, CV_64F);
                     NodeUtils::readMatFromH5(masterPath, "lat_coefficient", Lat_Coeff_Master, CV_64F);
-                    
+
                     double prf = 0.0;
                     NodeUtils::readScalarFromH5(masterPath, "prf", prf);
                     if (prf != 0) {
@@ -1094,7 +1100,7 @@ bool CoregistrationNode::validateAndRestoreOutput()
                             NodeUtils::readMatFromH5(slavePath, "state_vec", State_Vec_Slave, CV_64F);
                             NodeUtils::readMatFromH5(slavePath, "lon_coefficient", Lon_Coeff_Slave, CV_64F);
                             NodeUtils::readMatFromH5(slavePath, "lat_coefficient", Lat_Coeff_Slave, CV_64F);
-                            
+
                             double prf_slave = 0.0;
                             NodeUtils::readScalarFromH5(slavePath, "prf", prf_slave);
                             if (prf_slave != 0) {
@@ -1107,7 +1113,7 @@ bool CoregistrationNode::validateAndRestoreOutput()
                             sprintf_s(tmp_d2s, "%.4f", delta);
                             temporal_baseline += QString("%1 ").arg(QString(tmp_d2s));
 
-                            int offset_row = 0, offset_col = 0;
+                            double offset_row = 0.0, offset_col = 0.0;
                             QString regisName = resolveOutputFileName(QFileInfo(slavePath).completeBaseName());
                             if (!regisName.endsWith(".h5", Qt::CaseInsensitive)) regisName += ".h5";
                             QString regisPath = projDir + "/" + nodeName + "/" + regisName;
@@ -1138,7 +1144,7 @@ bool CoregistrationNode::validateAndRestoreOutput()
                     QString relativePath = QString("/%1/%2").arg(nodeName).arg(outName);
                     QString outH5Path = projDir + "/" + nodeName + "/" + outName;
 
-                    int rowOffset = 0, colOffset = 0;
+                    double rowOffset = 0.0, colOffset = 0.0;
                     NodeUtils::readScalarFromH5(outH5Path, "offset_row", rowOffset);
                     NodeUtils::readScalarFromH5(outH5Path, "offset_col", colOffset);
 
@@ -1161,11 +1167,11 @@ bool CoregistrationNode::validateAndRestoreOutput()
                     dataElem->LinkEndChild(dataPathNode);
 
                     TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
-                    rowOffsetNode->LinkEndChild(new TiXmlText(QString::number(rowOffset).toStdString().c_str()));
+                    rowOffsetNode->LinkEndChild(new TiXmlText(QString::number(rowOffset, 'g', 17).toStdString().c_str()));
                     dataElem->LinkEndChild(rowOffsetNode);
 
                     TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
-                    colOffsetNode->LinkEndChild(new TiXmlText(QString::number(colOffset).toStdString().c_str()));
+                    colOffsetNode->LinkEndChild(new TiXmlText(QString::number(colOffset, 'g', 17).toStdString().c_str()));
                     dataElem->LinkEndChild(colOffsetNode);
 
                     dataNodeElem->LinkEndChild(dataElem);
@@ -1254,4 +1260,359 @@ XMLFile* CoregistrationNode::projectXml() const
     return iface ? iface->projectXml() : nullptr;
 }
 
+
+struct CoregisEvalThreadResult {
+    int retCode;
+    CropEvalResult evalResult;
+};
+
+class CoregistrationEvalWidget : public QWidget
+{
+public:
+    explicit CoregistrationEvalWidget(CoregistrationNode* node, QWidget* parent = nullptr)
+        : QWidget(parent)
+        , m_node(node)
+        , m_hasResults(false)
+    {
+        m_outputPaths = m_node->getOutputPaths();
+
+        // 绑定唯一的临时路径以防多节点运行冲突
+        QString tempDir = QDir::tempPath();
+        m_tempCoherenceJpg = tempDir + QString("/crop_coherence_%1.jpg").arg(reinterpret_cast<quintptr>(m_node));
+        m_tempPhaseJpg = tempDir + QString("/crop_phase_%1.jpg").arg(reinterpret_cast<quintptr>(m_node));
+
+        // 界面布局
+        auto* mainLayout = new QHBoxLayout(this);
+        mainLayout->setContentsMargins(12, 12, 12, 12);
+        mainLayout->setSpacing(12);
+
+        // 左侧栏：评估参数与定量指标显示
+        auto* leftContainer = new QWidget();
+        auto* leftLayout = new QVBoxLayout(leftContainer);
+        leftLayout->setContentsMargins(0, 0, 0, 0);
+        leftLayout->setSpacing(10);
+
+        auto* selectionLayout = new QHBoxLayout();
+        auto* selLabel = new QLabel(tr("分析影像对:"));
+        selLabel->setStyleSheet("font-weight: bold;");
+        selectionLayout->addWidget(selLabel);
+
+        m_slaveCombo = new QComboBox();
+        int effMasterIdx1 = m_node->defaultFirstMaster() ? 1 : m_node->masterIndex();
+        int masterIdx0 = (effMasterIdx1 >= 1 && effMasterIdx1 <= m_outputPaths.size()) ? (effMasterIdx1 - 1) : 0;
+        if (m_outputPaths.size() > 1) {
+            QString masterName = QFileInfo(m_outputPaths[masterIdx0]).completeBaseName();
+            for (int i = 0; i < m_outputPaths.size(); ++i) {
+                if (i == masterIdx0) continue;
+                QString slaveName = QFileInfo(m_outputPaths[i]).completeBaseName();
+                m_slaveCombo->addItem(QString("%1 -> %2").arg(slaveName).arg(masterName), i);
+            }
+        } else {
+            m_slaveCombo->addItem(tr("无可配准的副影像"));
+            m_slaveCombo->setEnabled(false);
+        }
+        selectionLayout->addWidget(m_slaveCombo, 1);
+        leftLayout->addLayout(selectionLayout);
+
+        // 状态评估卡片
+        m_statusCard = new QFrame();
+        m_statusCard->setFrameShape(QFrame::StyledPanel);
+        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
+
+        auto* cardLayout = new QVBoxLayout(m_statusCard);
+        cardLayout->setContentsMargins(10, 8, 10, 8);
+        cardLayout->setSpacing(4);
+
+        m_statusCardTitle = new QLabel(tr("未评估"));
+        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
+        cardLayout->addWidget(m_statusCardTitle);
+
+        m_statusCardDesc = new QLabel(tr("请点击评估获取相干性及对齐精度诊断结果。"));
+        m_statusCardDesc->setWordWrap(true);
+        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
+        cardLayout->addWidget(m_statusCardDesc);
+
+        leftLayout->addWidget(m_statusCard);
+
+        // 定量指标统计表
+        auto* metricsFrame = new QFrame();
+        metricsFrame->setFrameShape(QFrame::StyledPanel);
+        bool isDark = NodeDetailWindow::isDarkTheme(this);
+        metricsFrame->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
+            .arg(isDark ? "#374151" : "#FFFFFF")
+            .arg(isDark ? "#4B5563" : "#E5E7EB"));
+
+        auto* formLayout = new QFormLayout(metricsFrame);
+        formLayout->setContentsMargins(12, 12, 12, 12);
+        formLayout->setSpacing(10);
+        formLayout->setLabelAlignment(Qt::AlignLeft);
+
+        auto createValueLabel = [isDark]() {
+            auto* label = new QLabel("-");
+            label->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
+            return label;
+        };
+
+        m_meanCohLabel = createValueLabel();
+        m_medianCohLabel = createValueLabel();
+        m_maxCohLabel = createValueLabel();
+        m_highCohPctLabel = createValueLabel();
+        m_offsetYLabel = createValueLabel();
+        m_offsetXLabel = createValueLabel();
+
+        auto addFormRow = [formLayout, isDark](const QString& title, QWidget* valueWidget) {
+            auto* label = new QLabel(title);
+            label->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
+            formLayout->addRow(label, valueWidget);
+        };
+
+        addFormRow(tr("相干系数平均值:"), m_meanCohLabel);
+        addFormRow(tr("相干系数中位数:"), m_medianCohLabel);
+        addFormRow(tr("相干系数最大值:"), m_maxCohLabel);
+        addFormRow(tr("高相干像素比例 (>0.5):"), m_highCohPctLabel);
+        addFormRow(tr("垂直残余偏移 (Y):"), m_offsetYLabel);
+        addFormRow(tr("水平残余偏移 (X):"), m_offsetXLabel);
+
+        leftLayout->addWidget(metricsFrame);
+
+        m_statusLabel = new QLabel(tr("准备就绪。请选择影像对开始评估。"));
+        m_statusLabel->setWordWrap(true);
+        m_statusLabel->setStyleSheet(isDark ? "color: #9CA3AF; font-size: 11px;" : "color: #6B7280; font-size: 11px;");
+        leftLayout->addWidget(m_statusLabel);
+
+        leftLayout->addStretch(1);
+        mainLayout->addWidget(leftContainer, 4);
+
+        // 右侧栏：大图显示与双模选择
+        auto* rightContainer = new QWidget();
+        auto* rightLayout = new QVBoxLayout(rightContainer);
+        rightLayout->setContentsMargins(0, 0, 0, 0);
+        rightLayout->setSpacing(8);
+
+        auto* modeLayout = new QHBoxLayout();
+        auto* modeLabel = new QLabel(tr("显示模式:"));
+        modeLabel->setStyleSheet("font-weight: bold;");
+        modeLayout->addWidget(modeLabel);
+
+        m_visualModeCombo = new QComboBox();
+        m_visualModeCombo->addItem(tr("全图干涉相位图"), 0);
+        m_visualModeCombo->addItem(tr("全图相干性系数图"), 1);
+        modeLayout->addWidget(m_visualModeCombo, 1);
+        rightLayout->addLayout(modeLayout);
+
+        m_imageView = new ImageView();
+        m_imageView->setMinimumSize(256, 256);
+        m_imageView->setStyleSheet(QString("border: 1px solid %1; border-radius: 4px;")
+            .arg(isDark ? "#4B5563" : "#D1D5DB"));
+        rightLayout->addWidget(m_imageView, 1);
+
+        mainLayout->addWidget(rightContainer, 5);
+
+        // 绑定信号槽
+        connect(m_slaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CoregistrationEvalWidget::onSlaveChanged);
+        connect(m_visualModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CoregistrationEvalWidget::onVisualModeChanged);
+        connect(&m_watcher, &QFutureWatcher<CoregisEvalThreadResult>::finished, this, &CoregistrationEvalWidget::onEvaluationFinished);
+
+        // 自动计算初始评估
+        if (m_outputPaths.size() > 1) {
+            QTimer::singleShot(200, this, [this]() {
+                startEvaluation();
+            });
+        }
+    }
+
+    ~CoregistrationEvalWidget() override
+    {
+        m_watcher.cancel();
+        m_watcher.waitForFinished();
+
+        // 销毁时清理生成的临时图像文件，遵守“垃圾代码与临时文件清理”规则
+        if (QFile::exists(m_tempCoherenceJpg)) QFile::remove(m_tempCoherenceJpg);
+        if (QFile::exists(m_tempPhaseJpg)) QFile::remove(m_tempPhaseJpg);
+    }
+
+private:
+    void onSlaveChanged(int index)
+    {
+        Q_UNUSED(index);
+        startEvaluation();
+    }
+
+    void onVisualModeChanged(int index)
+    {
+        Q_UNUSED(index);
+        updateImageView();
+    }
+
+    void startEvaluation()
+    {
+        if (m_watcher.isRunning()) {
+            return;
+        }
+
+        int effMasterIdx1 = m_node->defaultFirstMaster() ? 1 : m_node->masterIndex();
+        int masterIdx0 = (effMasterIdx1 >= 1 && effMasterIdx1 <= m_outputPaths.size()) ? (effMasterIdx1 - 1) : 0;
+        if (m_outputPaths.size() <= 1 || m_slaveCombo->currentIndex() < 0) {
+            return;
+        }
+        int slaveIdx0 = m_slaveCombo->currentData().toInt();
+        if (slaveIdx0 < 0 || slaveIdx0 >= m_outputPaths.size() || slaveIdx0 == masterIdx0) {
+            return;
+        }
+
+        m_statusLabel->setText(tr("正在计算裁剪区全图相干性与干涉相位，请稍候..."));
+        m_imageView->setImage(QImage());
+
+        // 重置指标标签
+        m_meanCohLabel->setText("-");
+        m_medianCohLabel->setText("-");
+        m_maxCohLabel->setText("-");
+        m_highCohPctLabel->setText("-");
+        m_offsetYLabel->setText("-");
+        m_offsetXLabel->setText("-");
+        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
+        m_statusCardTitle->setText(tr("未评估"));
+        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
+        m_statusCardDesc->setText(tr("请等待评估获取相干性及对齐精度诊断结果。"));
+
+        QString masterPath = m_outputPaths[masterIdx0];
+        QString slavePath = m_outputPaths[slaveIdx0];
+
+        if (!QFile::exists(masterPath) || !QFile::exists(slavePath)) {
+            m_statusLabel->setText(tr("错误：主图像或副图像裁剪文件不存在，请确保节点已成功运行！"));
+            return;
+        }
+
+        m_slaveCombo->setEnabled(false);
+
+        QString cohJpg = m_tempCoherenceJpg;
+        QString phaseJpg = m_tempPhaseJpg;
+
+        QFuture<CoregisEvalThreadResult> future = QtConcurrent::run([masterPath, slavePath, cohJpg, phaseJpg]() {
+            NodeUtils::Hdf5Locker locker(masterPath);
+            CoregisEvalThreadResult res{};
+            res.evalResult.structSize = sizeof(CropEvalResult);
+            res.retCode = AnalyzeCropRegistration(
+                masterPath.toLocal8Bit().constData(),
+                slavePath.toLocal8Bit().constData(),
+                cohJpg.toLocal8Bit().constData(),
+                phaseJpg.toLocal8Bit().constData(),
+                -1.0, -1.0, -1.0, -1.0,
+                &res.evalResult
+            );
+            return res;
+        });
+
+        m_watcher.setFuture(future);
+    }
+
+    void onEvaluationFinished()
+    {
+        m_slaveCombo->setEnabled(true);
+
+        CoregisEvalThreadResult threadRes = m_watcher.result();
+        if (threadRes.retCode != 0) {
+            m_statusLabel->setText(tr("裁剪配准评估失败，错误码：%1").arg(threadRes.retCode));
+            return;
+        }
+
+        m_evalResult = threadRes.evalResult;
+        m_hasResults = true;
+        m_statusLabel->setText(tr("配准评估完成。"));
+
+        // 填充指标数据
+        m_meanCohLabel->setText(QString::number(m_evalResult.meanCoherence, 'f', 4));
+        m_medianCohLabel->setText(QString::number(m_evalResult.medianCoherence, 'f', 4));
+        m_maxCohLabel->setText(QString::number(m_evalResult.maxCoherence, 'f', 4));
+        m_highCohPctLabel->setText(QString("%1%").arg(QString::number(m_evalResult.highCoherencePct * 100.0, 'f', 2)));
+
+        m_offsetYLabel->setText(QString::number(m_evalResult.offsetY, 'f', 2));
+        m_offsetXLabel->setText(QString::number(m_evalResult.offsetX, 'f', 2));
+
+        // 动态样式刷新
+        bool isDark = NodeDetailWindow::isDarkTheme(this);
+
+        if (std::abs(m_evalResult.offsetY) > 0.5 || std::abs(m_evalResult.offsetX) > 0.5) {
+            m_offsetYLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #EF4444;");
+            m_offsetXLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #EF4444;");
+        } else {
+            QString defaultColor = QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937");
+            m_offsetYLabel->setStyleSheet(defaultColor);
+            m_offsetXLabel->setStyleSheet(defaultColor);
+        }
+
+        if (m_evalResult.assessmentStatus == 0) {
+            m_statusCardTitle->setText(tr("通过 (PASS)"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
+            m_statusCardDesc->setText(tr("全图相干性优秀，主副影像配准成功，完全满足干涉处理要求。"));
+            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #10B981; border-radius: 4px;")
+                .arg(isDark ? "#064E3B" : "#D1FAE5"));
+        } else if (m_evalResult.assessmentStatus == 1) {
+            m_statusCardTitle->setText(tr("提醒 (WARNING)"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
+            m_statusCardDesc->setText(tr("全图相干性一般。局部可能存在轻微失相干，或包含水体、森林。建议核对质量。"));
+            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #F59E0B; border-radius: 4px;")
+                .arg(isDark ? "#78350F" : "#FEF3C7"));
+        } else {
+            m_statusCardTitle->setText(tr("低相干 (LOW COHERENCE)"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
+            m_statusCardDesc->setText(tr("地表相干性较低，请注意解缠质量。"));
+            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #EF4444; border-radius: 4px;")
+                .arg(isDark ? "#7F1D1D" : "#FEE2E2"));
+        }
+
+        updateImageView();
+    }
+
+    void updateImageView()
+    {
+        if (!m_hasResults) {
+            m_imageView->setImage(QImage());
+            return;
+        }
+
+        int mode = m_visualModeCombo->currentData().toInt();
+        QString imgPath = (mode == 0) ? m_tempPhaseJpg : m_tempCoherenceJpg;
+
+        if (QFile::exists(imgPath)) {
+            m_imageView->loadImage(imgPath);
+        } else {
+            m_imageView->setImage(QImage());
+        }
+    }
+
+    CoregistrationNode* m_node;
+    QStringList m_outputPaths;
+    QComboBox* m_slaveCombo;
+    QComboBox* m_visualModeCombo;
+    ImageView* m_imageView;
+
+    QFrame* m_statusCard;
+    QLabel* m_statusCardTitle;
+    QLabel* m_statusCardDesc;
+
+    QLabel* m_meanCohLabel;
+    QLabel* m_medianCohLabel;
+    QLabel* m_maxCohLabel;
+    QLabel* m_highCohPctLabel;
+    QLabel* m_offsetYLabel;
+    QLabel* m_offsetXLabel;
+    QLabel* m_statusLabel;
+
+    QString m_tempCoherenceJpg;
+    QString m_tempPhaseJpg;
+
+    CropEvalResult m_evalResult;
+    bool m_hasResults;
+    QFutureWatcher<CoregisEvalThreadResult> m_watcher;
+};
+
+// 选项卡页面工厂实现
+
+
+::QWidget* CoregistrationNode::createInterferometryWidget(::QWidget* parent)
+{
+    return new CoregistrationEvalWidget(this, parent);
+}
+
 } // namespace QtNodes
diff --git a/CoregistrationWorker.cpp b/CoregistrationWorker.cpp
index 9051e7bfbb5a8b4d96a1309862cd7a616289a853..d25fe255404d7f453a06d3a970931ff55021ab50 100644
--- a/CoregistrationWorker.cpp
+++ b/CoregistrationWorker.cpp
@@ -9,6 +9,7 @@
 #include <QCoreApplication>
 #include <QFile>
 #include <atomic>
+#include <algorithm>
 #include <QFileInfo>
 #include <QRegularExpression>
 #include "InSARLogManager.h"
@@ -110,6 +111,87 @@ CoregistrationWorker::~CoregistrationWorker()
 {
 }
 
+void CoregistrationWorker::ResampleSlaveInverseWithAffineOffset(const ComplexMat& slave, ComplexMat& out,
+    int outputRows, int outputCols, const Mat& coefRows, const Mat& coefCols,
+    double offsetX, double offsetY, double scaleX, double scaleY, CoregistrationWorker* worker)
+{
+    const int rowsSlave = slave.GetRows();
+    const int colsSlave = slave.GetCols();
+    const int type = slave.type();
+    out.re = Mat::zeros(outputRows, outputCols, type);
+    out.im = Mat::zeros(outputRows, outputCols, type);
+
+#pragma omp parallel for schedule(guided)
+    for (int i = 0; i < outputRows; i++)
+    {
+        if (worker && worker->isStopRequested()) {
+            continue;
+        }
+
+        double x, y, sampleRow, sampleCol;
+        Mat tmp(1, 3, CV_64F);
+        Mat result;
+        int row0, col0, row1, col1;
+        double offsetRows, offsetCols, upper, lower;
+        for (int j = 0; j < outputCols; j++)
+        {
+            sampleCol = static_cast<double>(j);
+            sampleRow = static_cast<double>(i);
+            x = (sampleCol - offsetX) / scaleX;
+            y = (sampleRow - offsetY) / scaleY;
+            tmp.at<double>(0, 0) = 1.0;
+            tmp.at<double>(0, 1) = x;
+            tmp.at<double>(0, 2) = y;
+            result = tmp * coefRows;
+            offsetRows = result.at<double>(0, 0);
+            result = tmp * coefCols;
+            offsetCols = result.at<double>(0, 0);
+
+            sampleRow += offsetRows;
+            sampleCol += offsetCols;
+
+            row0 = static_cast<int>(floor(sampleRow));
+            col0 = static_cast<int>(floor(sampleCol));
+            if (row0 < 0 || col0 < 0 || row0 > rowsSlave - 1 || col0 > colsSlave - 1)
+            {
+                continue;
+            }
+
+            row1 = row0 + 1;
+            col1 = col0 + 1;
+            row1 = row1 >= rowsSlave - 1 ? rowsSlave - 1 : row1;
+            col1 = col1 >= colsSlave - 1 ? colsSlave - 1 : col1;
+            if (type == CV_16S)
+            {
+                upper = static_cast<double>(slave.re.at<short>(row0, col0)) + static_cast<double>(slave.re.at<short>(row0, col1) - slave.re.at<short>(row0, col0)) * (sampleCol - static_cast<double>(col0));
+                lower = static_cast<double>(slave.re.at<short>(row1, col0)) + static_cast<double>(slave.re.at<short>(row1, col1) - slave.re.at<short>(row1, col0)) * (sampleCol - static_cast<double>(col0));
+                out.re.at<short>(i, j) = upper + static_cast<double>(lower - upper) * (sampleRow - static_cast<double>(row0));
+                upper = static_cast<double>(slave.im.at<short>(row0, col0)) + static_cast<double>(slave.im.at<short>(row0, col1) - slave.im.at<short>(row0, col0)) * (sampleCol - static_cast<double>(col0));
+                lower = static_cast<double>(slave.im.at<short>(row1, col0)) + static_cast<double>(slave.im.at<short>(row1, col1) - slave.im.at<short>(row1, col0)) * (sampleCol - static_cast<double>(col0));
+                out.im.at<short>(i, j) = upper + static_cast<double>(lower - upper) * (sampleRow - static_cast<double>(row0));
+            }
+            else if (type == CV_32F)
+            {
+                upper = slave.re.at<float>(row0, col0) + (slave.re.at<float>(row0, col1) - slave.re.at<float>(row0, col0)) * (sampleCol - static_cast<double>(col0));
+                lower = slave.re.at<float>(row1, col0) + (slave.re.at<float>(row1, col1) - slave.re.at<float>(row1, col0)) * (sampleCol - static_cast<double>(col0));
+                out.re.at<float>(i, j) = upper + (lower - upper) * (sampleRow - static_cast<double>(row0));
+                upper = slave.im.at<float>(row0, col0) + (slave.im.at<float>(row0, col1) - slave.im.at<float>(row0, col0)) * (sampleCol - static_cast<double>(col0));
+                lower = slave.im.at<float>(row1, col0) + (slave.im.at<float>(row1, col1) - slave.im.at<float>(row1, col0)) * (sampleCol - static_cast<double>(col0));
+                out.im.at<float>(i, j) = upper + (lower - upper) * (sampleRow - static_cast<double>(row0));
+            }
+            else
+            {
+                upper = slave.re.at<double>(row0, col0) + (slave.re.at<double>(row0, col1) - slave.re.at<double>(row0, col0)) * (sampleCol - static_cast<double>(col0));
+                lower = slave.re.at<double>(row1, col0) + (slave.re.at<double>(row1, col1) - slave.re.at<double>(row1, col0)) * (sampleCol - static_cast<double>(col0));
+                out.re.at<double>(i, j) = upper + (lower - upper) * (sampleRow - static_cast<double>(row0));
+                upper = slave.im.at<double>(row0, col0) + (slave.im.at<double>(row0, col1) - slave.im.at<double>(row0, col0)) * (sampleCol - static_cast<double>(col0));
+                lower = slave.im.at<double>(row1, col0) + (slave.im.at<double>(row1, col1) - slave.im.at<double>(row1, col0)) * (sampleCol - static_cast<double>(col0));
+                out.im.at<double>(i, j) = upper + (lower - upper) * (sampleRow - static_cast<double>(row0));
+            }
+        }
+    }
+}
+
 QString CoregistrationWorker::resolveOutputFileName(const QString& originalName) const
 {
     QString pattern = m_filePattern.trimmed();
@@ -201,7 +283,7 @@ void CoregistrationWorker::Regis(QList<int> para, QString save_path, QString pro
     Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
     Mat tmp_double = Mat::zeros(1, 1, CV_64FC1);
     double interp_interval;
-    int offset_row, offset_col;
+    double offset_row = 0.0, offset_col = 0.0;
     int Rows, Cols;
     double time_Master = 0;
     string time_master_str;
@@ -227,8 +309,8 @@ void CoregistrationWorker::Regis(QList<int> para, QString save_path, QString pro
     }
     QString temporal_baseline, B_parallel, B_effect;
     /*添加图像到model中并复制h5参数*/
-    vector<int> Row_offset;
-    vector<int> Col_offset;
+    vector<double> Row_offset;
+    vector<double> Col_offset;
 	emit updateProcess(90, QStringLiteral("写入辅助参数……"));
     for (int i = 0; i < image_number; i++)
     {
@@ -268,7 +350,7 @@ void CoregistrationWorker::Regis(QList<int> para, QString save_path, QString pro
         }
 
         /*写入辅助参数到h5*/
-		offset_row = offset_col = 0;
+		offset_row = offset_col = 0.0;
         {
             NodeUtils::Hdf5Locker locker;
             FC.Copy_para_from_h5_2_h5(SAR_images.at(i).c_str(), SAR_images_regis.at(i).c_str());
@@ -277,12 +359,12 @@ void CoregistrationWorker::Regis(QList<int> para, QString save_path, QString pro
             
             QString slaveImgPath = QString::fromStdString(SAR_images.at(i));
             NodeUtils::readScalarFromH5(slaveImgPath, "offset_row", offset_row);
-		    offset_row += offset_row_out.at<int>(i, 0);
-            FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "offset_row", offset_row);
+		    offset_row += offset_row_out.at<double>(i, 0);
+            FC.write_double_to_h5(SAR_images_regis.at(i).c_str(), "offset_row", offset_row);
             Row_offset.push_back(offset_row);
             NodeUtils::readScalarFromH5(slaveImgPath, "offset_col", offset_col);
-		    offset_col += offset_col_out.at<int>(i, 0);
-            FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "offset_col", offset_col);
+		    offset_col += offset_col_out.at<double>(i, 0);
+            FC.write_double_to_h5(SAR_images_regis.at(i).c_str(), "offset_col", offset_col);
             Col_offset.push_back(offset_col);
             FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "azimuth_len", Rows);
             FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "range_len", Cols);
@@ -646,8 +728,8 @@ int CoregistrationWorker::Registration_copy(
 	int n_images = SAR_images.size();
 	int num_slaves = n_images - 1;
 	int slave_idx = 0;
-	offset_col_out.create(n_images, 1, CV_32S);
-	offset_row_out.create(n_images, 1, CV_32S);
+	offset_col_out.create(n_images, 1, CV_64F);
+	offset_row_out.create(n_images, 1, CV_64F);
 	Mat images_rows, images_cols, tmp;
 	images_rows = Mat::zeros(n_images, 1, CV_32S); images_cols = Mat::zeros(n_images, 1, CV_32S);
 	for (int i = 0; i < n_images; i++)
@@ -684,7 +766,8 @@ int CoregistrationWorker::Registration_copy(
 		fprintf(stderr, "stack_coregistration(): try smaller blocksize!\n");
 		return -1;
 	}
-	Mat offset_r = Mat::zeros(m, n, CV_64F); Mat offset_c = Mat::zeros(m, n, CV_64F);
+	Mat offset_r = Mat::zeros(m, n, CV_64F); Mat offset_c = Mat::zeros(m, n, CV_64F); Mat eligibleMask = Mat::zeros(m, n, CV_8U);
+	Mat snr_values = Mat::zeros(m, n, CV_64F);
 	Mat offset_coord_row = Mat::zeros(m, n, CV_64F);
 	Mat offset_coord_col = Mat::zeros(m, n, CV_64F);
 	//子块中心坐标
@@ -705,8 +788,8 @@ int CoregistrationWorker::Registration_copy(
 		if (cancellationRequested()) return -2;
 		if (ii == Master_index - 1)
 		{
-			offset_row_out.at<int>(ii, 0) = 0;
-			offset_col_out.at<int>(ii, 0) = 0;
+			offset_row_out.at<double>(ii, 0) = 0.0;
+			offset_col_out.at<double>(ii, 0) = 0.0;
 			continue;
 		}
 		if (!b_block)//不分块读取
@@ -754,10 +837,23 @@ int CoregistrationWorker::Registration_copy(
 		std::atomic<int> completed_blocks(0);
 		int init_pct = static_cast<int>(start_p);
 		std::atomic<int> max_reported_pct(init_pct);
+		m = images_rows.at<int>(ii, 0) / blocksize;
+		n = images_cols.at<int>(ii, 0) / blocksize;
 		int total_blocks = m * n;
-		int mm, nn;
-		mm = images_rows.at<int>(ii, 0) / blocksize;
-		nn = images_cols.at<int>(ii, 0) / blocksize;
+		offset_r = Mat::zeros(m, n, CV_64F);
+		offset_c = Mat::zeros(m, n, CV_64F);
+		eligibleMask = Mat::zeros(m, n, CV_8U);
+		offset_coord_row = Mat::zeros(m, n, CV_64F);
+		snr_values = Mat::zeros(m, n, CV_64F);
+		offset_coord_col = Mat::zeros(m, n, CV_64F);
+		for (int i = 0; i < m; i++)
+		{
+			for (int j = 0; j < n; j++)
+			{
+				offset_coord_row.at<double>(i, j) = ((double)blocksize) / 2 * (double)(2 * i + 1);
+				offset_coord_col.at<double>(i, j) = ((double)blocksize) / 2 * (double)(2 * j + 1);
+			}
+		}
 		if (!b_block)
 		{
 # pragma omp parallel for schedule(guided)
@@ -767,12 +863,13 @@ int CoregistrationWorker::Registration_copy(
 				if (isStopRequested()) {
 					continue;
 				}
-				int offset_row, offset_col, move_r, move_c;
-				ComplexMat master, slave, master_interp, slave_interp;
+				int offset_row, offset_col;
+				double move_r, move_c, snr_val;
+				ComplexMat master, slave;
 				for (int k = 0; k < n; k++)
 				{
 					offset_row = j * blocksize; offset_col = k * blocksize;
-					if ((j + 1) * blocksize < images_rows.at<int>(ii, 0) && (k + 1) * blocksize < images_cols.at<int>(ii, 0))
+					if ((j + 1) * blocksize <= images_rows.at<int>(ii, 0) && (k + 1) * blocksize <= images_cols.at<int>(ii, 0))
 					{
 						master = master_w(cv::Range(offset_row, offset_row + blocksize), cv::Range(offset_col, offset_col + blocksize));
 						slave = slave_w(cv::Range(offset_row, offset_row + blocksize), cv::Range(offset_col, offset_col + blocksize));
@@ -780,12 +877,16 @@ int CoregistrationWorker::Registration_copy(
 						//计算偏移量
 						if (master.type() != CV_64F) master.convertTo(master, CV_64F);
 						if (slave.type() != CV_64F) slave.convertTo(slave, CV_64F);
-						move_r = 0; move_c = 0;
-						ret = regis.interp_paddingzero(master, master_interp, interp_times);
-						ret = regis.interp_paddingzero(slave, slave_interp, interp_times);
-						ret = regis.real_coherent(master_interp, slave_interp, &move_r, &move_c);
-						offset_r.at<double>(j, k) = double(move_r) / double(interp_times);
-						offset_c.at<double>(j, k) = double(move_c) / double(interp_times);
+						move_r = 0.0; move_c = 0.0; snr_val = 0.0;
+						int matchRet = regis.real_coherent(master, slave, &move_r, &move_c, &snr_val);
+						if (matchRet >= 0 && snr_val >= 3.0) {
+							offset_r.at<double>(j, k) = move_r;
+							offset_c.at<double>(j, k) = move_c;
+							eligibleMask.at<uchar>(j, k) = 1;
+							snr_values.at<double>(j, k) = snr_val;
+						} else {
+							eligibleMask.at<uchar>(j, k) = 0;
+						}
 					}
 					int current_done = ++completed_blocks;
 					int step = std::max(1, total_blocks / 50);
@@ -814,8 +915,9 @@ int CoregistrationWorker::Registration_copy(
 		}
 		else
 		{
-			int offset_row, offset_col, move_r, move_c;
-			ComplexMat master, slave, master_interp, slave_interp;
+			int offset_row, offset_col;
+			double move_r, move_c, snr_val;
+			ComplexMat master, slave;
 			for (int j = 0; j < m; j++)
 			{
 				if (isStopRequested()) {
@@ -824,7 +926,7 @@ int CoregistrationWorker::Registration_copy(
 				for (int k = 0; k < n; k++)
 				{
 					offset_row = j * blocksize; offset_col = k * blocksize;
-					if ((j + 1) * blocksize < images_rows.at<int>(ii, 0) && (k + 1) * blocksize < images_cols.at<int>(ii, 0))
+					if ((j + 1) * blocksize <= images_rows.at<int>(ii, 0) && (k + 1) * blocksize <= images_cols.at<int>(ii, 0))
 					{
 						ret = conversion.read_subarray_from_h5(SAR_images[Master_index - 1].c_str(), "s_im", offset_row, offset_col, blocksize, blocksize, master.im);
 						if (cancellationRequested()) return -2;
@@ -843,17 +945,17 @@ int CoregistrationWorker::Registration_copy(
 						if (master.type() != CV_64F) master.convertTo(master, CV_64F);
 						if (slave.type() != CV_64F) slave.convertTo(slave, CV_64F);
 
-						ret = regis.interp_paddingzero(master, master_interp, interp_times);
-						if (cancellationRequested()) return -2;
-						if (ret < 0) return -1;
-						ret = regis.interp_paddingzero(slave, slave_interp, interp_times);
-						if (cancellationRequested()) return -2;
-						if (ret < 0) return -1;
-						ret = regis.real_coherent(master_interp, slave_interp, &move_r, &move_c);
+						move_r = 0.0; move_c = 0.0; snr_val = 0.0;
+						int matchRet = regis.real_coherent(master, slave, &move_r, &move_c, &snr_val);
 						if (cancellationRequested()) return -2;
-						if (ret < 0) return -1;
-						offset_r.at<double>(j, k) = double(move_r) / double(interp_times);
-						offset_c.at<double>(j, k) = double(move_c) / double(interp_times);
+						if (matchRet >= 0 && snr_val >= 3.0) {
+							offset_r.at<double>(j, k) = move_r;
+							offset_c.at<double>(j, k) = move_c;
+							snr_values.at<double>(j, k) = snr_val;
+							eligibleMask.at<uchar>(j, k) = 1;
+						} else {
+							eligibleMask.at<uchar>(j, k) = 0;
+						}
 					}
 					int current_done = ++completed_blocks;
 					int step = std::max(1, total_blocks / 50);
@@ -882,40 +984,96 @@ int CoregistrationWorker::Registration_copy(
 
 
 		//剔除outliers
-		m = mm; n = nn;//更新实际子块行列数
 		Mat sentinel = Mat::zeros(m, n, CV_64F);
-		int ix, iy, count = 0, c = 0; double delta, thresh = 2.0;
+		std::vector<double> rawRows;
+		std::vector<double> rawCols;
+		std::vector<double> rawSnr;
+		rawRows.reserve(total_blocks);
+		rawCols.reserve(total_blocks);
+		rawSnr.reserve(total_blocks);
+		for (int blockRow = 0; blockRow < m; ++blockRow)
+		{
+			for (int blockCol = 0; blockCol < n; ++blockCol)
+			{
+				if (eligibleMask.at<uchar>(blockRow, blockCol) == 1) {
+					rawRows.push_back(offset_r.at<double>(blockRow, blockCol));
+					rawCols.push_back(offset_c.at<double>(blockRow, blockCol));
+					rawSnr.push_back(snr_values.at<double>(blockRow, blockCol));
+				}
+			}
+		}
+		const int rawEligibleCount = static_cast<int>(rawRows.size());
+		if (rawEligibleCount > 0)
+		{
+			auto medianOf = [](std::vector<double> values) {
+				std::sort(values.begin(), values.end());
+				const size_t middle = values.size() / 2;
+				return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) * 0.5 : values[middle];
+			};
+			double minRow, maxRow, minCol, maxCol, minSnr, maxSnr;
+			cv::minMaxLoc(offset_r, &minRow, &maxRow, nullptr, nullptr, eligibleMask);
+			cv::minMaxLoc(offset_c, &minCol, &maxCol, nullptr, nullptr, eligibleMask);
+			cv::minMaxLoc(snr_values, &minSnr, &maxSnr, nullptr, nullptr, eligibleMask);
+			const QString slaveName = QFileInfo(QString::fromStdString(SAR_images[ii])).fileName();
+			InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1]: grid=%2x%3, accepted=%4/%5; dy(mean=%6, median=%7, min=%8, max=%9); dx(mean=%10, median=%11, min=%12, max=%13); snr(mean=%14, median=%15, min=%16, max=%17)")
+				.arg(slaveName).arg(m).arg(n).arg(rawEligibleCount).arg(total_blocks)
+				.arg(cv::mean(offset_r, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawRows), 0, 'f', 4).arg(minRow, 0, 'f', 4).arg(maxRow, 0, 'f', 4)
+				.arg(cv::mean(offset_c, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawCols), 0, 'f', 4).arg(minCol, 0, 'f', 4).arg(maxCol, 0, 'f', 4)
+				.arg(cv::mean(snr_values, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawSnr), 0, 'f', 4).arg(minSnr, 0, 'f', 4).arg(maxSnr, 0, 'f', 4));
+		}
+		else
+		{
+			InSARLogManager::LogWarning("CoregistrationWorker", QString("Registration diagnostics [%1]: no blocks passed real_coherent/SNR filtering.").arg(QFileInfo(QString::fromStdString(SAR_images[ii])).fileName()));
+		}
+
+		int count = 0, c = 0; double delta, thresh = 2.0;
 		for (int i = 0; i < m; i++)
 		{
 			for (int j = 0; j < n; j++)
 			{
-				count = 0;
-				//上
-				ix = j;
-				iy = i - 1; iy = iy < 0 ? 0 : iy;
-				delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix));
-				delta += fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
-				if (fabs(delta) >= thresh) count++;
-				//下
-				ix = j;
-				iy = i + 1; iy = iy > m - 1 ? m - 1 : iy;
-				delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix));
-				delta += fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
-				if (fabs(delta) >= thresh) count++;
-				//左
-				ix = j - 1; ix = ix < 0 ? 0 : ix;
-				iy = i;
-				delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix));
-				delta += fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
-				if (fabs(delta) >= thresh) count++;
-				//右
-				ix = j + 1; ix = ix > n - 1 ? n - 1 : ix;
-				iy = i;
-				delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix));
-				delta += fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
-				if (fabs(delta) >= thresh) count++;
-
-				if (count > 2) { sentinel.at<double>(i, j) = 1.0; c++; }
+				if (eligibleMask.at<uchar>(i, j) == 0) {
+					sentinel.at<double>(i, j) = 1.0;
+					c++;
+					continue;
+				}
+
+				int valid_neighbors = 0;
+				int anomaly_count = 0;
+				int ix, iy;
+
+				// 上
+				ix = j; iy = i - 1;
+				if (iy >= 0 && eligibleMask.at<uchar>(iy, ix) == 1) {
+					valid_neighbors++;
+					delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix)) + fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
+					if (delta >= thresh) anomaly_count++;
+				}
+				// 下
+				ix = j; iy = i + 1;
+				if (iy < m && eligibleMask.at<uchar>(iy, ix) == 1) {
+					valid_neighbors++;
+					delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix)) + fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
+					if (delta >= thresh) anomaly_count++;
+				}
+				// 左
+				ix = j - 1; iy = i;
+				if (ix >= 0 && eligibleMask.at<uchar>(iy, ix) == 1) {
+					valid_neighbors++;
+					delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix)) + fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
+					if (delta >= thresh) anomaly_count++;
+				}
+				// 右
+				ix = j + 1; iy = i;
+				if (ix < n && eligibleMask.at<uchar>(iy, ix) == 1) {
+					valid_neighbors++;
+					delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix)) + fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
+					if (delta >= thresh) anomaly_count++;
+				}
+
+				if (valid_neighbors >= 2 && anomaly_count >= 2) {
+					sentinel.at<double>(i, j) = 1.0;
+					c++;
+				}
 			}
 		}
 		Mat offset_c_0, offset_r_0, offset_coord_row_0, offset_coord_col_0;
@@ -939,6 +1097,14 @@ int CoregistrationWorker::Registration_copy(
 			}
 		}
 
+		InSARLogManager::LogInfo("CoregistrationWorker",
+			QString("Registration diagnostics [%1]: filtering retained=%2/%3 blocks; qualityRejected=%4, outlierRejected=%5.")
+				.arg(QFileInfo(QString::fromStdString(SAR_images[ii])).fileName())
+				.arg(count)
+				.arg(total_blocks)
+				.arg(total_blocks - rawEligibleCount)
+				.arg(rawEligibleCount - count));
+
 
 		m = 1; n = count;
 		if (count < 10)
@@ -998,6 +1164,7 @@ int CoregistrationWorker::Registration_copy(
 		if (!cv::solve(A, b_r, coef_r, cv::DECOMP_NORMAL))
 		{
 			fprintf(stderr, "stack_coregistration(): matrix defficiency!\n");
+
 			return -1;
 		}
 		if (!cv::solve(A, b_c, coef_c, cv::DECOMP_NORMAL))
@@ -1006,6 +1173,20 @@ int CoregistrationWorker::Registration_copy(
 			return -1;
 		}
 
+		const double centerRow = static_cast<double>(rows / 2);
+		const double centerCol = static_cast<double>(cols / 2);
+		const double centerX = (centerCol - offset_x) / scale_x;
+		const double centerY = (centerRow - offset_y) / scale_y;
+		const double centerDy = coef_r.at<double>(0, 0) + coef_r.at<double>(1, 0) * centerX + coef_r.at<double>(2, 0) * centerY;
+		const double centerDx = coef_c.at<double>(0, 0) + coef_c.at<double>(1, 0) * centerX + coef_c.at<double>(2, 0) * centerY;
+		const QString diagnosticSlaveName = QFileInfo(QString::fromStdString(SAR_images[ii])).fileName();
+		InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1]: fit row=[%2, %3, %4], col=[%5, %6, %7], rms(row=%8, col=%9), center=(r=%10, c=%11) predicts dy=%12, dx=%13.")
+			.arg(diagnosticSlaveName)
+			.arg(coef_r.at<double>(0, 0), 0, 'f', 8).arg(coef_r.at<double>(1, 0), 0, 'f', 8).arg(coef_r.at<double>(2, 0), 0, 'f', 8)
+			.arg(coef_c.at<double>(0, 0), 0, 'f', 8).arg(coef_c.at<double>(1, 0), 0, 'f', 8).arg(coef_c.at<double>(2, 0), 0, 'f', 8)
+			.arg(rms1, 0, 'f', 6).arg(rms2, 0, 'f', 6)
+			.arg(centerRow, 0, 'f', 1).arg(centerCol, 0, 'f', 1).arg(centerDy, 0, 'f', 6).arg(centerDx, 0, 'f', 6));
+
 		/*---------------------------------------*/
 		/*    双线性插值获取重采样后的辅图像     */
 		/*---------------------------------------*/
@@ -1015,8 +1196,23 @@ int CoregistrationWorker::Registration_copy(
 		tt.at<double>(0, 0) = 1.0;
 		tt.at<double>(0, 1) = (0.0 - offset_x) / scale_x;
 		tt.at<double>(0, 2) = (0.0 - offset_y) / scale_y;
-		offset_row_out.at<int>(ii, 0) = sum(tt * coef_r)[0];
-		offset_col_out.at<int>(ii, 0) = sum(tt * coef_c)[0];
+		offset_row_out.at<double>(ii, 0) = sum(tt * coef_r)[0];
+		offset_col_out.at<double>(ii, 0) = sum(tt * coef_c)[0];
+
+		ComplexMat slave1;
+		ret = conversion.read_slc_from_h5(SAR_images[ii].c_str(), slave1);
+		if (cancellationRequested()) return -2;
+		if (ret < 0) return -1;
+		type = slave1.type();
+		ComplexMat slave_tmp;
+		InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1]: manual-bilinear resample input=%2x%3 type=%4, output=%5x%6, center source=(r=%7, c=%8), topLeft dy=%9, dx=%10.")
+			.arg(diagnosticSlaveName).arg(slave1.GetCols()).arg(slave1.GetRows()).arg(type).arg(cols).arg(rows)
+			.arg(centerRow + centerDy, 0, 'f', 6).arg(centerCol + centerDx, 0, 'f', 6)
+			.arg(offset_row_out.at<double>(ii, 0), 0, 'f', 6).arg(offset_col_out.at<double>(ii, 0), 0, 'f', 6));
+		ResampleSlaveInverseWithAffineOffset(slave1, slave_tmp, rows, cols,
+			coef_r, coef_c, offset_x, offset_y, scale_x, scale_y, this);
+#if 0
+		offset_col_out.at<double>(ii, 0) = sum(tt * coef_c)[0];
 
 		ComplexMat slave1;
 		ret = conversion.read_slc_from_h5(SAR_images[ii].c_str(), slave1);
@@ -1113,6 +1309,7 @@ int CoregistrationWorker::Registration_copy(
 
 			}
 		}
+#endif
 		if (cancellationRequested()) return -2;
 
 		ret = conversion.write_slc_to_h5(SAR_images_out[ii].c_str(), slave_tmp);
diff --git a/CutNode.cpp b/CutNode.cpp
index d9534f2924c89ab8cce9c81babf8a26cfc955ceb..dbd0919e38144be4c175de87880d2e3758f88a7f 100644
--- a/CutNode.cpp
+++ b/CutNode.cpp
@@ -1774,6 +1774,8 @@ public:
         m_medianCohLabel = createValueLabel();
         m_maxCohLabel = createValueLabel();
         m_highCohPctLabel = createValueLabel();
+        m_offsetYLabel = createValueLabel();
+        m_offsetXLabel = createValueLabel();
 
         auto addFormRow = [formLayout, isDark](const QString& title, QWidget* valueWidget) {
             auto* label = new QLabel(title);
@@ -1785,6 +1787,8 @@ public:
         addFormRow(tr("相干系数中位数:"), m_medianCohLabel);
         addFormRow(tr("相干系数最大值:"), m_maxCohLabel);
         addFormRow(tr("高相干像素比例 (>0.5):"), m_highCohPctLabel);
+        addFormRow(tr("垂直残余偏移 (Y):"), m_offsetYLabel);
+        addFormRow(tr("水平残余偏移 (X):"), m_offsetXLabel);
 
         leftLayout->addWidget(metricsFrame);
 
@@ -1876,6 +1880,8 @@ private:
         m_medianCohLabel->setText("-");
         m_maxCohLabel->setText("-");
         m_highCohPctLabel->setText("-");
+        m_offsetYLabel->setText("-");
+        m_offsetXLabel->setText("-");
         m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
         m_statusCardTitle->setText(tr("未评估"));
         m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
@@ -1896,7 +1902,8 @@ private:
 
         QFuture<CropEvalThreadResult> future = QtConcurrent::run([masterPath, slavePath, cohJpg, phaseJpg]() {
             NodeUtils::Hdf5Locker locker(masterPath);
-            CropEvalThreadResult res;
+            CropEvalThreadResult res{};
+            res.evalResult.structSize = sizeof(CropEvalResult);
             res.retCode = AnalyzeCropRegistration(
                 masterPath.toLocal8Bit().constData(),
                 slavePath.toLocal8Bit().constData(),
@@ -1930,9 +1937,22 @@ private:
         m_medianCohLabel->setText(QString::number(m_evalResult.medianCoherence, 'f', 4));
         m_maxCohLabel->setText(QString::number(m_evalResult.maxCoherence, 'f', 4));
         m_highCohPctLabel->setText(QString("%1%").arg(QString::number(m_evalResult.highCoherencePct * 100.0, 'f', 2)));
+        
+        m_offsetYLabel->setText(QString::number(m_evalResult.offsetY, 'f', 2));
+        m_offsetXLabel->setText(QString::number(m_evalResult.offsetX, 'f', 2));
 
         // 动态样式刷新
         bool isDark = NodeDetailWindow::isDarkTheme(this);
+        
+        if (std::abs(m_evalResult.offsetY) > 0.5 || std::abs(m_evalResult.offsetX) > 0.5) {
+            m_offsetYLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #EF4444;");
+            m_offsetXLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #EF4444;");
+        } else {
+            QString defaultColor = QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937");
+            m_offsetYLabel->setStyleSheet(defaultColor);
+            m_offsetXLabel->setStyleSheet(defaultColor);
+        }
+
         if (m_evalResult.assessmentStatus == 0) {
             m_statusCardTitle->setText(tr("通过 (PASS)"));
             m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
@@ -1987,6 +2007,8 @@ private:
     QLabel* m_medianCohLabel;
     QLabel* m_maxCohLabel;
     QLabel* m_highCohPctLabel;
+    QLabel* m_offsetYLabel;
+    QLabel* m_offsetXLabel;
     QLabel* m_statusLabel;
 
     QString m_tempCoherenceJpg;
diff --git a/DemNode.cpp b/DemNode.cpp
index 2ce133aae06fd5eaafbfa698fcdd5769193d8e56..9c38c7c05c29af827baaed51e31bd4b6cd414544 100644
--- a/DemNode.cpp
+++ b/DemNode.cpp
@@ -16,11 +16,17 @@
 #include <QDir>
 #include <QApplication>
 #include <QDateTime>
+#include <QHash>
 #include <QStandardItemModel>
 #include <QDebug>
 #include <QMessageBox>
 #include <QTimer>
 #include <QtConcurrent/QtConcurrent>
+#include <opencv2/core.hpp>
+#include <algorithm>
+#include <cmath>
+#include <limits>
+#include "QtNodes/internal/NodeDetailWindow.hpp"
 
 namespace QtNodes {
 
@@ -307,7 +313,7 @@ bool DemNode::validateInputs() const
         return false;
     }
 
-    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
+    QString dstNode = m_outputNodeName.trimmed();
     if (dstNode.isEmpty()) {
         return false;
     }
@@ -319,32 +325,55 @@ bool DemNode::validateInputs() const
     }
 
     // Check parameters
-    if (m_method == 1) {
-        bool ok = false;
-        int times = m_timesEdit ? m_timesEdit->text().toInt(&ok) : true;
-        if (!ok || times <= 0) {
-            return false;
-        }
+    if (m_method == 1 && m_times <= 0) {
+        return false;
+    }
+
+    return true;
+}
+
+bool DemNode::commitWidgetParametersForExecution()
+{
+    if (m_outputNodeNameEdit) {
+        m_outputNodeName = m_outputNodeNameEdit->text().trimmed();
     }
 
+    if (!m_timesEdit) {
+        return true;
+    }
+
+    bool ok = false;
+    const int times = m_timesEdit->text().toInt(&ok);
+    if (!ok || times <= 0) {
+        setStartFailureMessage(QStringLiteral("迭代次数必须是正整数。"));
+        return false;
+    }
+
+    m_times = times;
     return true;
 }
 
 bool DemNode::prepareToStart()
 {
-    if (!validateInputs())
+    if (!commitWidgetParametersForExecution()) {
         return false;
+    }
+
+    if (!validateInputs()) {
+        setStartFailureMessage(QStringLiteral("请检查输入数据和输出配置是否完整。"));
+        return false;
+    }
 
-    m_preparedDstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
+    m_preparedDstNode = m_outputNodeName.trimmed().isEmpty()
         ? generateDefaultOutputName()
-        : m_outputNodeNameEdit->text().trimmed();
+        : m_outputNodeName.trimmed();
 
     m_preparedSavePath = projectPath();
     m_preparedProjectName = projectName();
     m_preparedSrcNode = m_inputData->nodeName();
 
     m_preparedMethod = m_method;
-    m_preparedTimes = m_timesEdit ? m_timesEdit->text().toInt() : m_times;
+    m_preparedTimes = m_times;
 
     QStringList srcPaths = m_inputData->filePaths();
 
@@ -821,4 +850,383 @@ void DemNode::processAutomatically()
     }
 }
 
+namespace {
+
+QString demGenerationMethodName(int method)
+{
+    switch (method) {
+    case 1: return QObject::tr("Newton 迭代反演");
+    default: return QObject::tr("未知方法 (%1)").arg(method);
+    }
+}
+
+struct DemGenerationImageDiagnostics
+{
+    QString inputName;
+    bool outputFound = false;
+    bool inputRead = false;
+    bool outputRead = false;
+    bool dimensionsMatch = false;
+    int inputRows = 0;
+    int inputCols = 0;
+    int outputRows = 0;
+    int outputCols = 0;
+    bool hasRecordedMethod = false;
+    int recordedMethod = 0;
+    bool hasRecordedIterations = false;
+    int recordedIterations = 0;
+    bool dependenciesComplete = false;
+    QStringList missingDependencies;
+    qint64 totalPixels = 0;
+    qint64 finitePixels = 0;
+    double minHeight = std::numeric_limits<double>::infinity();
+    double maxHeight = -std::numeric_limits<double>::infinity();
+    double sumHeight = 0.0;
+    double sumSquaredHeight = 0.0;
+};
+
+struct DemGenerationValidationResults
+{
+    bool success = false;
+    QString errorMessage;
+    int expectedMethod = 1;
+    int expectedIterations = 20;
+    bool hasRecordedMethod = false;
+    int recordedMethod = 0;
+    bool hasRecordedIterations = false;
+    int recordedIterations = 0;
+    bool metadataConsistent = true;
+    QList<DemGenerationImageDiagnostics> images;
+};
+
+class DemGenerationValidationWidget : public BaseValidationWidget
+{
+public:
+    DemGenerationValidationWidget(DemNode* node, QWidget* parent)
+        : BaseValidationWidget(node, parent)
+        , m_node(node)
+    {
+        setupUI();
+        startAsyncValidation();
+    }
+
+private:
+    void setupUI()
+    {
+        setupBaseUI(QObject::tr("正在诊断 DEM 反演结果..."),
+                    QObject::tr("正在核对输出完整性、参数记录、几何尺寸和高程数值有效性。"),
+                    QObject::tr("DEM 反演诊断汇总"),
+                    QObject::tr("DEM 参数与结果诊断"));
+
+        m_validPixelsLabel = createFeatureLabel();
+        m_heightRangeLabel = createFeatureLabel();
+        m_heightMomentsLabel = createFeatureLabel();
+        m_dimensionsLabel = createFeatureLabel();
+        m_dependenciesLabel = createFeatureLabel();
+        m_issuesLabel = createFeatureLabel();
+
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("有效 DEM 像元:")), m_validPixelsLabel);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("高程数值范围:")), m_heightRangeLabel);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("均值 / 标准差:")), m_heightMomentsLabel);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("尺寸匹配结果:")), m_dimensionsLabel);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("下游关键数据:")), m_dependenciesLabel);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缺失或异常结果:")), m_issuesLabel);
+    }
+
+    void setNotExecutedState()
+    {
+        m_statusTitle->setText(QObject::tr("诊断不可用"));
+        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
+        m_statusDesc->setText(QObject::tr("请先成功执行 DEM Generation 节点，再查看诊断结果。"));
+        m_compTable->clearComparison();
+        m_compTable->setEnabled(false);
+        m_validPixelsLabel->setText(QObject::tr("未执行"));
+        m_heightRangeLabel->setText(QObject::tr("未执行"));
+        m_heightMomentsLabel->setText(QObject::tr("未执行"));
+        m_dimensionsLabel->setText(QObject::tr("未执行"));
+        m_dependenciesLabel->setText(QObject::tr("未执行"));
+        m_issuesLabel->setText(QObject::tr("未执行"));
+    }
+
+    void startAsyncValidation() override
+    {
+        m_isTimedOut = false;
+        if (m_node->executionState() != ExecutionState::Completed) {
+            setNotExecutedState();
+            return;
+        }
+
+        const auto inputData = m_node->inputDataForValidation();
+        const auto outputData = std::dynamic_pointer_cast<ImportedFileData>(m_node->outData(0));
+        if (!inputData || inputData->filePaths().isEmpty() || !outputData || outputData->filePaths().isEmpty()) {
+            m_statusTitle->setText(QObject::tr("诊断失败"));
+            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
+            m_statusDesc->setText(QObject::tr("未找到完整的输入相位或输出 DEM H5 文件列表。"));
+            return;
+        }
+
+        const QJsonObject settings = m_node->save();
+        const QStringList inputPaths = inputData->filePaths();
+        const QStringList outputPaths = outputData->filePaths();
+        const int expectedMethod = settings.value("method").toInt(1);
+        const int expectedIterations = settings.value("times").toInt(20);
+        m_loadingOverlay->startLoading(QObject::tr("正在读取全部 DEM 结果并计算数值统计..."));
+
+        QFuture<DemGenerationValidationResults> future = QtConcurrent::run(
+            [inputPaths, outputPaths, expectedMethod, expectedIterations]() {
+                NodeUtils::Hdf5Locker locker;
+                DemGenerationValidationResults result;
+                result.expectedMethod = expectedMethod;
+                result.expectedIterations = expectedIterations;
+
+                QHash<QString, QString> outputsByBaseName;
+                for (const QString& outputPath : outputPaths) {
+                    outputsByBaseName.insert(QFileInfo(outputPath).baseName(), outputPath);
+                }
+
+                for (const QString& inputPath : inputPaths) {
+                    DemGenerationImageDiagnostics image;
+                    image.inputName = QFileInfo(inputPath).baseName();
+                    const QString outputPath = outputsByBaseName.value(image.inputName + QStringLiteral("_dem"));
+                    image.outputFound = !outputPath.isEmpty();
+                    if (!image.outputFound) {
+                        result.images.append(image);
+                        continue;
+                    }
+
+                    cv::Mat inputPhase;
+                    cv::Mat dem;
+                    image.inputRead = NodeUtils::readMatFromH5(inputPath, "phase", inputPhase) && !inputPhase.empty();
+                    image.outputRead = NodeUtils::readMatFromH5(outputPath, "dem", dem) && !dem.empty();
+                    if (image.inputRead) {
+                        image.inputRows = inputPhase.rows;
+                        image.inputCols = inputPhase.cols;
+                    }
+                    if (image.outputRead) {
+                        image.outputRows = dem.rows;
+                        image.outputCols = dem.cols;
+                    }
+                    image.dimensionsMatch = image.inputRead && image.outputRead && inputPhase.size() == dem.size();
+
+                    image.hasRecordedMethod = NodeUtils::readScalarFromH5(
+                        outputPath, "dem_generation_method", image.recordedMethod);
+                    image.hasRecordedIterations = NodeUtils::readScalarFromH5(
+                        outputPath, "dem_generation_iterations", image.recordedIterations);
+                    if (image.hasRecordedMethod) {
+                        if (!result.hasRecordedMethod) {
+                            result.hasRecordedMethod = true;
+                            result.recordedMethod = image.recordedMethod;
+                        } else if (result.recordedMethod != image.recordedMethod) {
+                            result.metadataConsistent = false;
+                        }
+                    }
+                    if (image.hasRecordedIterations) {
+                        if (!result.hasRecordedIterations) {
+                            result.hasRecordedIterations = true;
+                            result.recordedIterations = image.recordedIterations;
+                        } else if (result.recordedIterations != image.recordedIterations) {
+                            result.metadataConsistent = false;
+                        }
+                    }
+
+                    std::string source;
+                    cv::Mat auxiliary;
+                    const bool source1Present = NodeUtils::readStringFromH5(outputPath, "source_1", source);
+                    const bool source2Present = NodeUtils::readStringFromH5(outputPath, "source_2", source);
+                    const bool flatPhasePresent = NodeUtils::readMatFromH5(outputPath, "flat_phase_coefficient", auxiliary) && !auxiliary.empty();
+                    const bool rangeLengthPresent = NodeUtils::readMatFromH5(outputPath, "range_len", auxiliary) && !auxiliary.empty();
+                    const bool azimuthLengthPresent = NodeUtils::readMatFromH5(outputPath, "azimuth_len", auxiliary) && !auxiliary.empty();
+                    const bool multilookRangePresent = NodeUtils::readMatFromH5(outputPath, "multilook_rg", auxiliary) && !auxiliary.empty();
+                    const bool multilookAzimuthPresent = NodeUtils::readMatFromH5(outputPath, "multilook_az", auxiliary) && !auxiliary.empty();
+                    if (!source1Present) image.missingDependencies.append(QStringLiteral("source_1"));
+                    if (!source2Present) image.missingDependencies.append(QStringLiteral("source_2"));
+                    if (!flatPhasePresent) image.missingDependencies.append(QStringLiteral("flat_phase_coefficient"));
+                    if (!rangeLengthPresent) image.missingDependencies.append(QStringLiteral("range_len"));
+                    if (!azimuthLengthPresent) image.missingDependencies.append(QStringLiteral("azimuth_len"));
+                    if (!multilookRangePresent) image.missingDependencies.append(QStringLiteral("multilook_rg"));
+                    if (!multilookAzimuthPresent) image.missingDependencies.append(QStringLiteral("multilook_az"));
+                    image.dependenciesComplete = image.missingDependencies.isEmpty();
+
+                    if (image.outputRead) {
+                        cv::Mat demDouble;
+                        dem.convertTo(demDouble, CV_64F);
+                        image.totalPixels = static_cast<qint64>(demDouble.total());
+                        for (int row = 0; row < demDouble.rows; ++row) {
+                            const double* values = demDouble.ptr<double>(row);
+                            for (int column = 0; column < demDouble.cols; ++column) {
+                                const double value = values[column];
+                                if (!std::isfinite(value)) {
+                                    continue;
+                                }
+                                ++image.finitePixels;
+                                image.minHeight = std::min(image.minHeight, value);
+                                image.maxHeight = std::max(image.maxHeight, value);
+                                image.sumHeight += value;
+                                image.sumSquaredHeight += value * value;
+                            }
+                        }
+                    }
+
+                    result.images.append(image);
+                }
+
+                result.success = !result.images.isEmpty();
+                if (!result.success) {
+                    result.errorMessage = QObject::tr("没有可用于诊断的 DEM 影像。");
+                }
+                return result;
+            });
+
+        auto* watcher = new QFutureWatcher<DemGenerationValidationResults>(this);
+        connect(watcher, &QFutureWatcher<DemGenerationValidationResults>::finished, this, [this, watcher]() {
+            if (m_isTimedOut) {
+                watcher->deleteLater();
+                return;
+            }
+
+            const DemGenerationValidationResults result = watcher->result();
+            m_loadingOverlay->stopLoading();
+            if (!result.success) {
+                m_statusTitle->setText(QObject::tr("诊断失败"));
+                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
+                m_statusDesc->setText(result.errorMessage);
+                watcher->deleteLater();
+                return;
+            }
+
+            m_compTable->clearComparison();
+            m_compTable->setEnabled(true);
+            if (result.hasRecordedMethod) {
+                m_compTable->addComparison(QObject::tr("反演方法"), demGenerationMethodName(result.expectedMethod),
+                    demGenerationMethodName(result.recordedMethod));
+            } else {
+                m_compTable->addDiagnostic(QObject::tr("反演方法"),
+                    QObject::tr("当前设置：%1；输出未记录（旧结果）").arg(demGenerationMethodName(result.expectedMethod)));
+            }
+            if (result.hasRecordedIterations) {
+                m_compTable->addComparison(QObject::tr("Newton 迭代次数"), QString::number(result.expectedIterations),
+                    QString::number(result.recordedIterations));
+            } else {
+                m_compTable->addDiagnostic(QObject::tr("Newton 迭代次数"),
+                    QObject::tr("当前设置：%1；输出未记录（旧结果）").arg(result.expectedIterations));
+            }
+
+            int foundOutputs = 0;
+            int dimensionsMatch = 0;
+            int completeDependencies = 0;
+            int invalidResults = 0;
+            qint64 totalPixels = 0;
+            qint64 finitePixels = 0;
+            double minimumHeight = std::numeric_limits<double>::infinity();
+            double maximumHeight = -std::numeric_limits<double>::infinity();
+            double sumHeight = 0.0;
+            double sumSquaredHeight = 0.0;
+            QStringList issues;
+
+            for (const DemGenerationImageDiagnostics& image : result.images) {
+                foundOutputs += image.outputFound ? 1 : 0;
+                const QString expectedSize = image.inputRead
+                    ? QStringLiteral("%1 x %2").arg(image.inputCols).arg(image.inputRows)
+                    : QObject::tr("输入 phase 不可读");
+                QString actualSize;
+                if (!image.outputFound) {
+                    actualSize = QObject::tr("缺失输出");
+                    issues.append(image.inputName + QObject::tr(": 缺失输出"));
+                    ++invalidResults;
+                } else if (!image.outputRead) {
+                    actualSize = QObject::tr("输出 dem 不可读");
+                    issues.append(image.inputName + QObject::tr(": 输出 dem 不可读"));
+                    ++invalidResults;
+                } else {
+                    actualSize = QStringLiteral("%1 x %2").arg(image.outputCols).arg(image.outputRows);
+                    if (!image.dimensionsMatch) {
+                        issues.append(image.inputName + QObject::tr(": 尺寸不匹配"));
+                        ++invalidResults;
+                    } else {
+                        ++dimensionsMatch;
+                    }
+                    if (image.finitePixels == 0) {
+                        issues.append(image.inputName + QObject::tr(": 无有效 DEM 像元"));
+                        ++invalidResults;
+                    }
+                }
+                m_compTable->addComparison(image.inputName, expectedSize, actualSize);
+
+                if (image.dependenciesComplete) {
+                    ++completeDependencies;
+                } else if (image.outputFound) {
+                    issues.append(image.inputName + QObject::tr(": 缺少 ") + image.missingDependencies.join(QStringLiteral(", ")));
+                    ++invalidResults;
+                }
+
+                if (image.outputRead) {
+                    totalPixels += image.totalPixels;
+                    finitePixels += image.finitePixels;
+                    minimumHeight = std::min(minimumHeight, image.minHeight);
+                    maximumHeight = std::max(maximumHeight, image.maxHeight);
+                    sumHeight += image.sumHeight;
+                    sumSquaredHeight += image.sumSquaredHeight;
+                }
+            }
+
+            m_compTable->addComparison(QObject::tr("输入 / 输出影像数"),
+                QString::number(result.images.size()), QString::number(foundOutputs));
+            const double validRatio = totalPixels > 0 ? 100.0 * finitePixels / totalPixels : 0.0;
+            m_validPixelsLabel->setText(totalPixels > 0
+                ? QObject::tr("%1 / %2 (%3%)").arg(finitePixels).arg(totalPixels).arg(QString::number(validRatio, 'f', 2))
+                : QObject::tr("无可读取的 DEM 像元"));
+            m_heightRangeLabel->setText(finitePixels > 0
+                ? QObject::tr("%1 ~ %2").arg(QString::number(minimumHeight, 'g', 7), QString::number(maximumHeight, 'g', 7))
+                : QObject::tr("无有效 DEM 像元"));
+            if (finitePixels > 0) {
+                const double meanHeight = sumHeight / finitePixels;
+                const double variance = std::max(0.0, sumSquaredHeight / finitePixels - meanHeight * meanHeight);
+                m_heightMomentsLabel->setText(QObject::tr("%1 / %2")
+                    .arg(QString::number(meanHeight, 'g', 7), QString::number(std::sqrt(variance), 'g', 7)));
+            } else {
+                m_heightMomentsLabel->setText(QObject::tr("无有效 DEM 像元"));
+            }
+            m_dimensionsLabel->setText(QObject::tr("%1 / %2 匹配").arg(dimensionsMatch).arg(result.images.size()));
+            m_dependenciesLabel->setText(QObject::tr("%1 / %2 齐全").arg(completeDependencies).arg(result.images.size()));
+            m_issuesLabel->setText(issues.isEmpty() ? QObject::tr("未发现缺失或尺寸异常") : issues.join(QStringLiteral("\n")));
+
+            const bool parametersMatch = result.metadataConsistent
+                && (!result.hasRecordedMethod || result.recordedMethod == result.expectedMethod)
+                && (!result.hasRecordedIterations || result.recordedIterations == result.expectedIterations);
+            if (invalidResults > 0 || finitePixels == 0) {
+                m_statusTitle->setText(QObject::tr("需要复查"));
+                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
+                m_statusDesc->setText(QObject::tr("发现输出缺失、尺寸异常、无效高程或下游关键数据不完整。请检查对应影像和处理日志。"));
+            } else if (!parametersMatch) {
+                m_statusTitle->setText(QObject::tr("参数与结果不一致"));
+                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
+                m_statusDesc->setText(QObject::tr("当前反演参数与输出 H5 中记录的参数不一致；修改参数后需要重新执行节点。"));
+            } else {
+                m_statusTitle->setText(QObject::tr("诊断完成"));
+                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
+                m_statusDesc->setText((!result.hasRecordedMethod || !result.hasRecordedIterations)
+                    ? QObject::tr("输出完整且数值可读。旧结果未记录反演参数，无法确认其与当前设置是否一致。")
+                    : QObject::tr("输出完整、尺寸匹配且高程数值可读。数值统计仅用于结果完整性诊断，不构成绝对高程精度评估。"));
+            }
+            watcher->deleteLater();
+        });
+        watcher->setFuture(future);
+    }
+
+    DemNode* m_node = nullptr;
+    QLabel* m_validPixelsLabel = nullptr;
+    QLabel* m_heightRangeLabel = nullptr;
+    QLabel* m_heightMomentsLabel = nullptr;
+    QLabel* m_dimensionsLabel = nullptr;
+    QLabel* m_dependenciesLabel = nullptr;
+    QLabel* m_issuesLabel = nullptr;
+};
+
+} // namespace
+
+::QWidget* DemNode::createValidationWidget(::QWidget* parent)
+{
+    return new DemGenerationValidationWidget(this, parent);
+}
+
 } // namespace QtNodes
diff --git a/DemWorker.cpp b/DemWorker.cpp
index c848b9ebebeb90b7f75ab0ff079b6ba9a1e9f57c..65baff60e8dfc0aa9ef3c726939093032b58172c 100644
--- a/DemWorker.cpp
+++ b/DemWorker.cpp
@@ -279,6 +279,8 @@ void DemWorker::Dem(int method, int times, QString save_path, QString project_na
             }
 
             NodeUtils::writeMatToH5(outputH5, "dem", phase_dem);
+            NodeUtils::writeScalarToH5(outputH5, "dem_generation_method", method);
+            NodeUtils::writeScalarToH5(outputH5, "dem_generation_iterations", times);
             
             string tmp_str;
             Mat tmp;
diff --git a/DenoiseNode.cpp b/DenoiseNode.cpp
index c6720f2da881a9e876f3499f746cf260c4ec9f9e..f1edff9882a988cadc168ff2c8bcd0189a1e924a 100644
--- a/DenoiseNode.cpp
+++ b/DenoiseNode.cpp
@@ -19,6 +19,8 @@
 #include <QMessageBox>
 #include <QTimer>
 #include <QtConcurrent/QtConcurrent>
+#include <algorithm>
+#include <cmath>
 #include "QtNodes/internal/NodeDetailWindow.hpp"
 
 namespace QtNodes {
@@ -952,33 +954,48 @@ private:
                     QObject::tr("正在读取输入与输出 H5 数据以进行参数及特征值校验。"),
                     QObject::tr("特征值分析"));
 
-        m_lblInWidth = createFeatureLabel();
-        m_lblOutWidth = createFeatureLabel();
-        m_lblInMean = createFeatureLabel();
-        m_lblOutMean = createFeatureLabel();
+        m_lblDiffMean = createFeatureLabel();
         m_lblDiffStd = createFeatureLabel();
-
-        m_featureLayout->addRow(createHeaderLabel(QObject::tr("输入图像行列数:")), m_lblInWidth);
-        m_featureLayout->addRow(createHeaderLabel(QObject::tr("输出图像行列数:")), m_lblOutWidth);
-        m_featureLayout->addRow(createHeaderLabel(QObject::tr("输入相位均值 (rad):")), m_lblInMean);
-        m_featureLayout->addRow(createHeaderLabel(QObject::tr("输出相位均值 (rad):")), m_lblOutMean);
-        m_featureLayout->addRow(createHeaderLabel(QObject::tr("差值相位标准差 (rad):")), m_lblDiffStd);
+        m_lblDiffResultant = createFeatureLabel();
+        m_lblGradientSummary = createFeatureLabel();
+        m_lblResidueSummary = createFeatureLabel();
+
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差圆均值 (rad):")), m_lblDiffMean);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差圆标准差 (rad):")), m_lblDiffStd);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差集中度 R (0-1):")), m_lblDiffResultant);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位梯度 RMS (输入 -> 输出):")), m_lblGradientSummary);
+        m_featureLayout->addRow(createHeaderLabel(QObject::tr("相位残差点密度 (输入 -> 输出):")), m_lblResidueSummary);
     }
 
+    struct PhaseQualityMetrics {
+        double gradientRms = 0.0;
+        double residueDensity = 0.0;
+        bool hasGradient = false;
+        bool hasResidueDensity = false;
+    };
+
     struct ValidationResults {
         bool success = false;
         QString errorMsg;
         // Compare values
         int expectedMethod = 1;
-        int actualMethod = 1;
+        int actualMethod = 0;
         int expectedPrefilter = 5;
-        int actualPrefilter = 5;
+        int actualPrefilter = 0;
+        int expectedSlopeWindow = 5;
+        int actualSlopeWindow = 0;
+        bool hasActualMethod = false;
+        bool hasActualPrefilter = false;
+        bool hasActualSlopeWindow = false;
         // Calculated features
         int inRows = 0, inCols = 0;
         int outRows = 0, outCols = 0;
-        double inMean = 0.0;
-        double outMean = 0.0;
-        double diffStd = 0.0;
+        double wrappedDiffMean = 0.0;
+        double wrappedDiffStd = 0.0;
+        double wrappedDiffResultant = 0.0;
+        bool hasWrappedDifference = false;
+        PhaseQualityMetrics inputQuality;
+        PhaseQualityMetrics outputQuality;
     };
 
     void startAsyncValidation() override
@@ -994,11 +1011,12 @@ private:
             m_compTable->clearComparison();
             m_compTable->setEnabled(false);
             
-            m_lblInWidth->setText(QObject::tr("未执行"));
-            m_lblOutWidth->setText(QObject::tr("未执行"));
-            m_lblInMean->setText(QObject::tr("未执行"));
-            m_lblOutMean->setText(QObject::tr("未执行"));
+            m_lblDiffMean->setText(QObject::tr("未执行"));
             m_lblDiffStd->setText(QObject::tr("未执行"));
+            m_lblDiffResultant->setText(QObject::tr("未执行"));
+            m_lblGradientSummary->setText(QObject::tr("未执行"));
+            m_lblResidueSummary->setText(QObject::tr("未执行"));
+
             return;
         }
 
@@ -1020,23 +1038,20 @@ private:
         // Settings to compare
         int expMethod = m_node->save()["method"].toInt(1);
         int expPrefilter = m_node->save()["prefilterWin"].toInt(5);
+        int expSlopeWindow = m_node->save()["slopeWin"].toInt(5);
 
         // Run validation in background
-        QFuture<ValidationResults> future = QtConcurrent::run([inH5, outH5, expMethod, expPrefilter]() {
+        QFuture<ValidationResults> future = QtConcurrent::run([inH5, outH5, expMethod, expPrefilter, expSlopeWindow]() {
             NodeUtils::Hdf5Locker locker;
             ValidationResults res;
             res.expectedMethod = expMethod;
             res.expectedPrefilter = expPrefilter;
+            res.expectedSlopeWindow = expSlopeWindow;
 
             // 1. Read metadata parameters from output H5
-            int actualPref = 5;
-            if (NodeUtils::readScalarFromH5(outH5, "multilook_rg", actualPref)) {
-                res.actualPrefilter = actualPref;
-            } else {
-                res.actualPrefilter = expPrefilter; // default if not found
-            }
-            
-            res.actualMethod = expMethod;
+            res.hasActualMethod = NodeUtils::readScalarFromH5(outH5, "denoise_method", res.actualMethod);
+            res.hasActualPrefilter = NodeUtils::readScalarFromH5(outH5, "denoise_slope_pre_win", res.actualPrefilter);
+            res.hasActualSlopeWindow = NodeUtils::readScalarFromH5(outH5, "denoise_slope_win", res.actualSlopeWindow);
             
             // 2. Read matrices
             cv::Mat inPhase, outPhase;
@@ -1050,18 +1065,110 @@ private:
                 res.outRows = outPhase.rows;
                 res.outCols = outPhase.cols;
 
-                // Compute statistics (mean)
-                cv::Scalar meanIn = cv::mean(inPhase);
-                cv::Scalar meanOut = cv::mean(outPhase);
-                res.inMean = meanIn[0];
-                res.outMean = meanOut[0];
-
-                // Compute phase difference standard deviation
-                cv::Mat diff;
-                cv::subtract(inPhase, outPhase, diff);
-                cv::Scalar diffMean, diffStd;
-                cv::meanStdDev(diff, diffMean, diffStd);
-                res.diffStd = diffStd[0];
+                const auto calculatePhaseQuality = [](const cv::Mat& phase) {
+                    const double pi = 3.14159265358979323846;
+                    const double twoPi = 2.0 * pi;
+                    const auto wrapDifference = [pi, twoPi](double delta) {
+                        if (delta > pi) {
+                            return delta - twoPi;
+                        }
+                        if (delta <= -pi) {
+                            return delta + twoPi;
+                        }
+                        return delta;
+                    };
+
+                    PhaseQualityMetrics metrics;
+                    double gradientSumSquares = 0.0;
+                    qint64 gradientCount = 0;
+                    for (int row = 0; row < phase.rows; ++row) {
+                        const float* values = phase.ptr<float>(row);
+                        const float* nextRow = row + 1 < phase.rows ? phase.ptr<float>(row + 1) : nullptr;
+                        for (int col = 0; col < phase.cols; ++col) {
+                            const double value = values[col];
+                            if (!std::isfinite(value)) {
+                                continue;
+                            }
+                            if (col + 1 < phase.cols && std::isfinite(values[col + 1])) {
+                                const double gradient = wrapDifference(static_cast<double>(values[col + 1]) - value);
+                                gradientSumSquares += gradient * gradient;
+                                ++gradientCount;
+                            }
+                            if (nextRow && std::isfinite(nextRow[col])) {
+                                const double gradient = wrapDifference(static_cast<double>(nextRow[col]) - value);
+                                gradientSumSquares += gradient * gradient;
+                                ++gradientCount;
+                            }
+                        }
+                    }
+                    if (gradientCount > 0) {
+                        metrics.gradientRms = std::sqrt(gradientSumSquares / gradientCount);
+                        metrics.hasGradient = true;
+                    }
+
+                    qint64 residueCount = 0;
+                    qint64 plaquetteCount = 0;
+                    for (int row = 0; row + 1 < phase.rows; ++row) {
+                        const float* top = phase.ptr<float>(row);
+                        const float* bottom = phase.ptr<float>(row + 1);
+                        for (int col = 0; col + 1 < phase.cols; ++col) {
+                            const double p00 = top[col];
+                            const double p01 = top[col + 1];
+                            const double p11 = bottom[col + 1];
+                            const double p10 = bottom[col];
+                            if (!std::isfinite(p00) || !std::isfinite(p01) || !std::isfinite(p11) || !std::isfinite(p10)) {
+                                continue;
+                            }
+
+                            const double closure = wrapDifference(p01 - p00)
+                                + wrapDifference(p11 - p01)
+                                + wrapDifference(p10 - p11)
+                                + wrapDifference(p00 - p10);
+                            if (std::abs(closure) > pi) {
+                                ++residueCount;
+                            }
+                            ++plaquetteCount;
+                        }
+                    }
+                    if (plaquetteCount > 0) {
+                        metrics.residueDensity = 100.0 * residueCount / plaquetteCount;
+                        metrics.hasResidueDensity = true;
+                    }
+                    return metrics;
+                };
+
+                res.inputQuality = calculatePhaseQuality(inPhase);
+                res.outputQuality = calculatePhaseQuality(outPhase);
+
+                if (inPhase.rows == outPhase.rows && inPhase.cols == outPhase.cols) {
+                    double sumSin = 0.0;
+                    double sumCos = 0.0;
+                    qint64 count = 0;
+
+                    for (int row = 0; row < inPhase.rows; ++row) {
+                        const float* inValues = inPhase.ptr<float>(row);
+                        const float* outValues = outPhase.ptr<float>(row);
+                        for (int col = 0; col < inPhase.cols; ++col) {
+                            const double inputValue = inValues[col];
+                            const double outputValue = outValues[col];
+                            if (!std::isfinite(inputValue) || !std::isfinite(outputValue)) {
+                                continue;
+                            }
+
+                            const double delta = outputValue - inputValue;
+                            sumSin += std::sin(delta);
+                            sumCos += std::cos(delta);
+                            ++count;
+                        }
+                    }
+
+                    if (count > 0) {
+                        res.wrappedDiffMean = std::atan2(sumSin, sumCos);
+                        res.wrappedDiffResultant = std::min(1.0, std::hypot(sumSin / count, sumCos / count));
+                        res.wrappedDiffStd = std::sqrt(-2.0 * std::log(std::max(res.wrappedDiffResultant, 1e-12)));
+                        res.hasWrappedDifference = true;
+                    }
+                }
             } else {
                 res.success = false;
                 res.errorMsg = QObject::tr("读取相位数据集失败，可能文件已损坏或格式不兼容。");
@@ -1086,18 +1193,45 @@ private:
                 m_compTable->setEnabled(true);
                 
                 QString methodStrExp = res.expectedMethod == 1 ? "Slope" : (res.expectedMethod == 2 ? "Goldstein" : "DL");
-                QString methodStrAct = res.actualMethod == 1 ? "Slope" : (res.actualMethod == 2 ? "Goldstein" : "DL");
+                QString methodStrAct = res.hasActualMethod
+                    ? (res.actualMethod == 1 ? "Slope" : (res.actualMethod == 2 ? "Goldstein" : (res.actualMethod == 3 ? "DL" : QObject::tr("未知"))))
+                    : QObject::tr("未记录（旧结果）");
                 m_compTable->addComparison(QObject::tr("滤波方法"), methodStrExp, methodStrAct);
-                m_compTable->addComparison(QObject::tr("平滑窗口大小"), QString::number(res.expectedPrefilter), QString::number(res.actualPrefilter));
+                if (res.expectedMethod == 1) {
+                    m_compTable->addComparison(QObject::tr("预滤波窗口大小"),
+                        QString::number(res.expectedPrefilter),
+                        res.hasActualPrefilter ? QString::number(res.actualPrefilter) : QObject::tr("未记录（旧结果）"));
+                    m_compTable->addComparison(QObject::tr("斜坡滤波窗口大小"),
+                        QString::number(res.expectedSlopeWindow),
+                        res.hasActualSlopeWindow ? QString::number(res.actualSlopeWindow) : QObject::tr("未记录（旧结果）"));
+                }
                 m_compTable->addComparison(QObject::tr("图像宽度 (列数)"), QString::number(res.inCols), QString::number(res.outCols));
                 m_compTable->addComparison(QObject::tr("图像高度 (行数)"), QString::number(res.inRows), QString::number(res.outRows));
 
                 // Update feature analysis labels
-                m_lblInWidth->setText(QString("%1 × %2").arg(res.inCols).arg(res.inRows));
-                m_lblOutWidth->setText(QString("%1 × %2").arg(res.outCols).arg(res.outRows));
-                m_lblInMean->setText(QString::number(res.inMean, 'f', 4));
-                m_lblOutMean->setText(QString::number(res.outMean, 'f', 4));
-                m_lblDiffStd->setText(QString::number(res.diffStd, 'f', 4));
+                m_lblDiffMean->setText(res.hasWrappedDifference
+                    ? QString::number(res.wrappedDiffMean, 'f', 4)
+                    : QObject::tr("图像尺寸不一致"));
+                m_lblDiffStd->setText(res.hasWrappedDifference
+                    ? QString::number(res.wrappedDiffStd, 'f', 4)
+                    : QObject::tr("图像尺寸不一致"));
+                m_lblDiffResultant->setText(res.hasWrappedDifference
+                    ? QString::number(res.wrappedDiffResultant, 'f', 4)
+                    : QObject::tr("图像尺寸不一致"));
+                const auto transitionText = [](double input, bool hasInput, double output, bool hasOutput,
+                    int precision, const QString& unitSuffix) {
+                    if (!hasInput || !hasOutput || input <= 0.0) {
+                        return QObject::tr("无有效数据");
+                    }
+                    const QString inputText = QString::number(input, 'f', precision) + unitSuffix;
+                    const QString outputText = QString::number(output, 'f', precision) + unitSuffix;
+                    const double reduction = 100.0 * (input - output) / input;
+                    return QString("%1 -> %2 (%3%)").arg(inputText).arg(outputText).arg(QString::number(reduction, 'f', 2));
+                };
+                m_lblGradientSummary->setText(transitionText(res.inputQuality.gradientRms, res.inputQuality.hasGradient,
+                    res.outputQuality.gradientRms, res.outputQuality.hasGradient, 4, QString()));
+                m_lblResidueSummary->setText(transitionText(res.inputQuality.residueDensity, res.inputQuality.hasResidueDensity,
+                    res.outputQuality.residueDensity, res.outputQuality.hasResidueDensity, 3, QStringLiteral("%")));
 
                 // Final status card
                 m_statusTitle->setText(QObject::tr("验证通过"));
@@ -1118,11 +1252,12 @@ private:
 private:
     DenoiseNode* m_node = nullptr;
     
-    QLabel* m_lblInWidth = nullptr;
-    QLabel* m_lblOutWidth = nullptr;
-    QLabel* m_lblInMean = nullptr;
-    QLabel* m_lblOutMean = nullptr;
+    QLabel* m_lblDiffMean = nullptr;
     QLabel* m_lblDiffStd = nullptr;
+    QLabel* m_lblDiffResultant = nullptr;
+    QLabel* m_lblGradientSummary = nullptr;
+    QLabel* m_lblResidueSummary = nullptr;
+
 };
 
 ::QWidget* DenoiseNode::createValidationWidget(::QWidget* parent)
diff --git a/DenoiseWorker.cpp b/DenoiseWorker.cpp
index 29972f43fe233d1886ddd1b2260f2097bf7b0e92..c36b02457a95914a6e702c66764251d2e6b21163 100644
--- a/DenoiseWorker.cpp
+++ b/DenoiseWorker.cpp
@@ -262,6 +262,9 @@ void DenoiseWorker::Denoise(QList<int> para, double alpha, QString save_path, QS
                     phase_filter.convertTo(phase_filter, CV_32F);
                 }
                 NodeUtils::writeMatToH5(filterPath, "phase", phase_filter);
+                NodeUtils::writeScalarToH5(filterPath, "denoise_method", method);
+                NodeUtils::writeScalarToH5(filterPath, "denoise_slope_pre_win", pre_win);
+                NodeUtils::writeScalarToH5(filterPath, "denoise_slope_win", slop_win);
                 
                 NodeUtils::readStringFromH5(phasePath, "source_1", tmp_str);
                 FC.write_str_to_h5(filterPath.toStdString().c_str(), "source_1", tmp_str.c_str());
@@ -377,6 +380,7 @@ void DenoiseWorker::Denoise(QList<int> para, double alpha, QString save_path, QS
                     phase_filter.convertTo(phase_filter, CV_32F);
                 }
                 NodeUtils::writeMatToH5(filterPath, "phase", phase_filter);
+                NodeUtils::writeScalarToH5(filterPath, "denoise_method", method);
                 
                 NodeUtils::readStringFromH5(phasePath, "source_1", tmp_str);
                 FC.write_str_to_h5(filterPath.toStdString().c_str(), "source_1", tmp_str.c_str());
@@ -489,6 +493,7 @@ void DenoiseWorker::Denoise(QList<int> para, double alpha, QString save_path, QS
                 /*写入h5*/
                 ret = FC.creat_new_h5(filterPath.toStdString().c_str());
                 NodeUtils::writeMatToH5(filterPath, "phase", phase_filter);
+                NodeUtils::writeScalarToH5(filterPath, "denoise_method", method);
                 
                 NodeUtils::readStringFromH5(phasePath, "source_1", tmp_str);
                 FC.write_str_to_h5(filterPath.toStdString().c_str(), "source_1", tmp_str.c_str());
diff --git a/InterferometricFormationNode.cpp b/InterferometricFormationNode.cpp
index e8511d942dba299e1351344b998cef69cba01e29..b6dc10d418f6d61807530f9004a099e76b650a59 100644
--- a/InterferometricFormationNode.cpp
+++ b/InterferometricFormationNode.cpp
@@ -7,6 +7,9 @@
 #include "icon_source.h"
 #include "InSARLogManager.h"
 #include "FormatConversion.h"
+#include <cmath>
+#include "ImageView.h"
+#include <QtNodes/internal/NodeDetailWindow.hpp>
 #include <QVBoxLayout>
 #include <QHBoxLayout>
 #include <QFormLayout>
@@ -887,8 +890,10 @@ void InterferometricFormationNode::onProcessingFinished()
 
         QFuture<void> future = QtConcurrent::run([h5Paths, jpgPaths, types]() {
             for (int i = 0; i < h5Paths.size(); ++i) {
-                // 已存在且有效的 JPG 跳过重生成
-                if (QFileInfo::exists(jpgPaths[i]) && QFileInfo(jpgPaths[i]).size() > 0) {
+                // 如果 JPG 存在、有效，且修改时间不早于 H5 文件，则跳过重生成
+                QFileInfo jpgInfo(jpgPaths[i]);
+                QFileInfo h5Info(h5Paths[i]);
+                if (jpgInfo.exists() && jpgInfo.size() > 0 && jpgInfo.lastModified() >= h5Info.lastModified()) {
                     continue;
                 }
                 NodeUtils::generateJpgPreviewFromH5(h5Paths[i], jpgPaths[i], types[i]);
@@ -1266,7 +1271,9 @@ bool InterferometricFormationNode::validateAndRestoreOutput()
     int totalExpected = expectedJpgPaths.size();
     for (int i = 0; i < totalExpected; ++i) {
         QString jpgPath = expectedJpgPaths[i];
-        if (QFile::exists(jpgPath)) {
+        QFileInfo jpgInfo(jpgPath);
+        QFileInfo h5Info(h5Paths[i]);
+        if (jpgInfo.exists() && jpgInfo.size() > 0 && jpgInfo.lastModified() >= h5Info.lastModified()) {
             existingJpgPaths.append(jpgPath);
         } else {
             // Find corresponding h5 path
@@ -1328,8 +1335,10 @@ bool InterferometricFormationNode::validateAndRestoreOutput()
 
         QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs, missingTypes]() {
             for (int i = 0; i < missingH5s.size(); ++i) {
-                // 已存在且有效的 JPG 跳过重生成
-                if (QFileInfo::exists(missingJpgs[i]) && QFileInfo(missingJpgs[i]).size() > 0) {
+                // 如果 JPG 存在、有效，且修改时间不早于 H5 文件，则跳过重生成
+                QFileInfo jpgInfo(missingJpgs[i]);
+                QFileInfo h5Info(missingH5s[i]);
+                if (jpgInfo.exists() && jpgInfo.size() > 0 && jpgInfo.lastModified() >= h5Info.lastModified()) {
                     continue;
                 }
                 NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], missingTypes[i]);
@@ -1405,4 +1414,465 @@ void InterferometricFormationNode::updateParameterWidgetsEnableState()
     if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(demEnabled);
 }
 
+// --------------------------------------------------------------------------------
+// InterferometricFormationEvalWidget - 干涉形成质量评估选项卡组件
+// --------------------------------------------------------------------------------
+
+struct InterfEvalThreadResult {
+    bool success;
+    float meanCoh;
+    float medianCoh;
+    float maxCoh;
+    float highCohPct;
+    QString errorMessage;
+};
+
+class InterferometricFormationEvalWidget : public QWidget
+{
+public:
+    explicit InterferometricFormationEvalWidget(InterferometricFormationNode* node, QWidget* parent = nullptr)
+        : QWidget(parent), m_node(node), m_hasResults(false)
+    {
+        // 界面布局
+        auto* mainLayout = new QHBoxLayout(this);
+        mainLayout->setContentsMargins(12, 12, 12, 12);
+        mainLayout->setSpacing(12);
+
+        // 左侧栏：评估参数与定量指标显示
+        auto* leftContainer = new QWidget();
+        auto* leftLayout = new QVBoxLayout(leftContainer);
+        leftLayout->setContentsMargins(0, 0, 0, 0);
+        leftLayout->setSpacing(10);
+
+        auto* selectionLayout = new QHBoxLayout();
+        auto* selLabel = new QLabel(tr("分析影像对:"));
+        selLabel->setStyleSheet("font-weight: bold;");
+        selectionLayout->addWidget(selLabel);
+
+        m_slaveCombo = new QComboBox();
+        selectionLayout->addWidget(m_slaveCombo, 1);
+        leftLayout->addLayout(selectionLayout);
+
+        // 状态评估卡片
+        m_statusCard = new QFrame();
+        m_statusCard->setFrameShape(QFrame::StyledPanel);
+        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
+        
+        auto* cardLayout = new QVBoxLayout(m_statusCard);
+        cardLayout->setContentsMargins(10, 8, 10, 8);
+        cardLayout->setSpacing(4);
+
+        m_statusCardTitle = new QLabel(tr("未评估"));
+        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
+        cardLayout->addWidget(m_statusCardTitle);
+
+        m_statusCardDesc = new QLabel(tr("请点击评估获取相干性及干涉质量诊断结果。"));
+        m_statusCardDesc->setWordWrap(true);
+        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
+        cardLayout->addWidget(m_statusCardDesc);
+
+        leftLayout->addWidget(m_statusCard);
+
+        // 定量指标统计表
+        auto* metricsFrame = new QFrame();
+        metricsFrame->setFrameShape(QFrame::StyledPanel);
+        bool isDark = NodeDetailWindow::isDarkTheme(this);
+        metricsFrame->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
+            .arg(isDark ? "#374151" : "#FFFFFF")
+            .arg(isDark ? "#4B5563" : "#E5E7EB"));
+        
+        auto* formLayout = new QFormLayout(metricsFrame);
+        formLayout->setContentsMargins(12, 12, 12, 12);
+        formLayout->setSpacing(10);
+        formLayout->setLabelAlignment(Qt::AlignLeft);
+
+        auto createValueLabel = [isDark]() {
+            auto* label = new QLabel("-");
+            label->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
+            return label;
+        };
+
+        m_meanCohLabel = createValueLabel();
+        m_medianCohLabel = createValueLabel();
+        m_maxCohLabel = createValueLabel();
+        m_highCohPctLabel = createValueLabel();
+        m_phaseNoiseLabel = createValueLabel();
+        m_enlLabel = createValueLabel();
+        m_flatEarthLabel = createValueLabel();
+        m_topoLabel = createValueLabel();
+
+        auto addFormRow = [formLayout, isDark](const QString& title, QWidget* valueWidget) {
+            auto* label = new QLabel(title);
+            label->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
+            formLayout->addRow(label, valueWidget);
+        };
+
+        addFormRow(tr("平均相干系数:"), m_meanCohLabel);
+        addFormRow(tr("中位相干系数:"), m_medianCohLabel);
+        addFormRow(tr("最高相干系数:"), m_maxCohLabel);
+        addFormRow(tr("高相干占比 (>0.5):"), m_highCohPctLabel);
+        addFormRow(tr("预估相位噪声 (标准差):"), m_phaseNoiseLabel);
+        addFormRow(tr("相干估算窗口 (视数):"), m_enlLabel);
+        addFormRow(tr("平地相位状态:"), m_flatEarthLabel);
+        addFormRow(tr("地形相位状态:"), m_topoLabel);
+
+        leftLayout->addWidget(metricsFrame);
+
+        m_statusLabel = new QLabel(tr("准备就绪。请选择影像对开始评估。"));
+        m_statusLabel->setWordWrap(true);
+        m_statusLabel->setStyleSheet(isDark ? "color: #9CA3AF; font-size: 11px;" : "color: #6B7280; font-size: 11px;");
+        leftLayout->addWidget(m_statusLabel);
+
+        leftLayout->addStretch(1);
+        mainLayout->addWidget(leftContainer, 4);
+
+        // 右侧栏：大图显示与双模选择
+        auto* rightContainer = new QWidget();
+        auto* rightLayout = new QVBoxLayout(rightContainer);
+        rightLayout->setContentsMargins(0, 0, 0, 0);
+        rightLayout->setSpacing(8);
+
+        auto* topBarLayout = new QHBoxLayout();
+        
+        m_evalBtn = new QPushButton(tr(" 执行质量评估 "));
+        m_evalBtn->setStyleSheet(
+            "QPushButton { background-color: #3B82F6; color: white; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
+            "QPushButton:hover { background-color: #2563EB; }"
+            "QPushButton:pressed { background-color: #1D4ED8; }"
+            "QPushButton:disabled { background-color: #9CA3AF; }"
+        );
+        topBarLayout->addWidget(m_evalBtn);
+        
+        topBarLayout->addStretch(1);
+
+        auto* viewModeLabel = new QLabel(tr("视图切换:"));
+        viewModeLabel->setStyleSheet("font-weight: bold;");
+        topBarLayout->addWidget(viewModeLabel);
+
+        m_visualModeCombo = new QComboBox();
+        m_visualModeCombo->addItem(tr("干涉相位 (Phase)"));
+        m_visualModeCombo->addItem(tr("相干系数 (Coherence)"));
+        topBarLayout->addWidget(m_visualModeCombo);
+
+        rightLayout->addLayout(topBarLayout);
+
+        m_imageView = new ImageView();
+        m_imageView->setStyleSheet(QString("border: 1px solid %1; background-color: %2;")
+            .arg(isDark ? "#4B5563" : "#D1D5DB")
+            .arg(isDark ? "#111827" : "#F3F4F6"));
+        rightLayout->addWidget(m_imageView, 1);
+
+        mainLayout->addWidget(rightContainer, 6);
+
+        // 初始化数据
+        updateAvailablePairs();
+
+        connect(m_slaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InterferometricFormationEvalWidget::onPairChanged);
+        connect(m_visualModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InterferometricFormationEvalWidget::updatePreviewImage);
+        connect(m_evalBtn, &QPushButton::clicked, this, &InterferometricFormationEvalWidget::startEvaluation);
+        connect(&m_watcher, &QFutureWatcher<InterfEvalThreadResult>::finished, this, &InterferometricFormationEvalWidget::onEvaluationFinished);
+
+        // 默认触发一次
+        if (m_slaveCombo->count() > 0) {
+            onPairChanged();
+        } else {
+            m_evalBtn->setEnabled(false);
+        }
+    }
+
+    ~InterferometricFormationEvalWidget() override
+    {
+        if (m_watcher.isRunning()) {
+            m_watcher.waitForFinished();
+        }
+    }
+
+private:
+    void updateAvailablePairs()
+    {
+        m_slaveCombo->blockSignals(true);
+        m_slaveCombo->clear();
+        m_h5PathsPhase.clear();
+        m_h5PathsCoh.clear();
+        m_jpgPathsPhase.clear();
+        m_jpgPathsCoh.clear();
+
+        QStringList previews = m_node->previewImagePaths();
+        for (const QString& jpgPath : previews) {
+            if (jpgPath.endsWith("_phase.jpg")) {
+                m_jpgPathsPhase.append(jpgPath);
+                QString h5Path = jpgPath;
+                h5Path.replace("_phase.jpg", ".h5");
+                m_h5PathsPhase.append(h5Path);
+            } else if (jpgPath.endsWith("_coh.jpg")) {
+                m_jpgPathsCoh.append(jpgPath);
+                QString h5Path = jpgPath;
+                h5Path.replace("_coh.jpg", ".h5");
+                m_h5PathsCoh.append(h5Path);
+            }
+        }
+
+        if (m_jpgPathsPhase.isEmpty()) {
+            m_slaveCombo->addItem(tr("无干涉对"));
+        } else {
+            for (const QString& jpg : m_jpgPathsPhase) {
+                QString name = QFileInfo(jpg).baseName();
+                name.replace("_phase", "");
+                m_slaveCombo->addItem(name);
+            }
+        }
+        m_slaveCombo->blockSignals(false);
+    }
+
+    void onPairChanged()
+    {
+        m_hasResults = false;
+        resetMetrics();
+        updatePreviewImage();
+    }
+
+    void updatePreviewImage()
+    {
+        int pairIdx = m_slaveCombo->currentIndex();
+        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
+            m_imageView->setImage(QImage());
+            return;
+        }
+
+        int viewMode = m_visualModeCombo->currentIndex();
+        QString pathToLoad;
+
+        if (viewMode == 0) { // Phase
+            pathToLoad = m_jpgPathsPhase[pairIdx];
+        } else { // Coherence
+            // 查找对应的相干系数图
+            QString baseName = QFileInfo(m_jpgPathsPhase[pairIdx]).baseName();
+            baseName.replace("_phase", "_coh");
+            for (const QString& cohPath : m_jpgPathsCoh) {
+                if (QFileInfo(cohPath).baseName() == baseName) {
+                    pathToLoad = cohPath;
+                    break;
+                }
+            }
+        }
+
+        if (!pathToLoad.isEmpty() && QFile::exists(pathToLoad)) {
+            QImage img(pathToLoad);
+            m_imageView->setImage(img);
+        } else {
+            m_imageView->setImage(QImage());
+        }
+    }
+
+    void resetMetrics()
+    {
+        m_meanCohLabel->setText("-");
+        m_medianCohLabel->setText("-");
+        m_maxCohLabel->setText("-");
+        m_highCohPctLabel->setText("-");
+        
+        m_statusCardTitle->setText(tr("未评估"));
+        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
+        m_statusCardDesc->setText(tr("请点击评估获取相干性及干涉质量诊断结果。"));
+        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
+        
+        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
+        m_statusLabel->setText(tr("准备就绪。请点击执行评估。"));
+    }
+
+    void startEvaluation()
+    {
+        int pairIdx = m_slaveCombo->currentIndex();
+        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
+            return;
+        }
+
+        // 相干系数数据存在于与之同名的 H5 文件中
+        QString cohH5Path = m_h5PathsPhase[pairIdx];
+
+        // 检查对应的 _coh.jpg 是否存在，以确认节点确实生成了相干系数
+        QString expectedCohJpg = m_jpgPathsPhase[pairIdx];
+        expectedCohJpg.replace("_phase.jpg", "_coh.jpg");
+        if (!m_jpgPathsCoh.contains(expectedCohJpg)) {
+            cohH5Path = ""; // 强行置空触发下方报错
+        }
+
+        if (cohH5Path.isEmpty() || !QFile::exists(cohH5Path)) {
+            m_statusLabel->setText(tr("无法评估：未找到该干涉对的相干系数成果文件。请检查节点是否勾选了“计算相干系数”。"));
+            return;
+        }
+
+        m_evalBtn->setEnabled(false);
+        m_slaveCombo->setEnabled(false);
+        m_statusLabel->setText(tr("正在读取 H5 文件并计算相干性统计信息，请稍候..."));
+
+        QFuture<InterfEvalThreadResult> future = QtConcurrent::run([cohH5Path]() {
+            InterfEvalThreadResult res;
+            res.success = false;
+            res.meanCoh = 0.0f;
+            res.medianCoh = 0.0f;
+            res.maxCoh = 0.0f;
+            res.highCohPct = 0.0f;
+
+            cv::Mat cohMat;
+            QString errMsg;
+            if (!NodeUtils::readMatFromH5(cohH5Path, "coherence", cohMat, CV_32F, &errMsg)) {
+                res.errorMessage = QStringLiteral("读取相干系数数据集失败: %1").arg(errMsg);
+                return res;
+            }
+
+            if (cohMat.empty()) {
+                res.errorMessage = QStringLiteral("相干系数矩阵为空。");
+                return res;
+            }
+
+            // 计算统计信息
+            // 为了计算中位数和占比，需要遍历所有有效像素
+            std::vector<float> validPixels;
+            // 预估大小，如果是超大矩阵，可以隔行采样
+            int step = 1;
+            if (cohMat.total() > 5000000) {
+                step = qMax(1, (int)(cohMat.total() / 5000000));
+            }
+            
+            float maxCoh = 0.0f;
+            double sumCoh = 0.0;
+            int highCount = 0;
+
+            if (cohMat.isContinuous()) {
+                const float* ptr = cohMat.ptr<float>();
+                int total = cohMat.total();
+                for (int i = 0; i < total; i += step) {
+                    float val = ptr[i];
+                    if (!std::isnan(val) && val >= 0.0f && val <= 1.0f) {
+                        validPixels.push_back(val);
+                        sumCoh += val;
+                        if (val > maxCoh) maxCoh = val;
+                        if (val > 0.5f) highCount++;
+                    }
+                }
+            } else {
+                for (int r = 0; r < cohMat.rows; r += step) {
+                    const float* ptr = cohMat.ptr<float>(r);
+                    for (int c = 0; c < cohMat.cols; c += step) {
+                        float val = ptr[c];
+                        if (!std::isnan(val) && val >= 0.0f && val <= 1.0f) {
+                            validPixels.push_back(val);
+                            sumCoh += val;
+                            if (val > maxCoh) maxCoh = val;
+                            if (val > 0.5f) highCount++;
+                        }
+                    }
+                }
+            }
+
+            if (validPixels.empty()) {
+                res.errorMessage = QStringLiteral("相干系数矩阵中没有有效数据。");
+                return res;
+            }
+
+            res.meanCoh = sumCoh / validPixels.size();
+            res.maxCoh = maxCoh;
+            res.highCohPct = (float)highCount / validPixels.size() * 100.0f;
+
+            size_t n = validPixels.size() / 2;
+            std::nth_element(validPixels.begin(), validPixels.begin() + n, validPixels.end());
+            res.medianCoh = validPixels[n];
+
+            res.success = true;
+            return res;
+        });
+
+        m_watcher.setFuture(future);
+    }
+
+    void onEvaluationFinished()
+    {
+        m_evalBtn->setEnabled(true);
+        m_slaveCombo->setEnabled(true);
+
+        InterfEvalThreadResult res = m_watcher.result();
+        if (!res.success) {
+            m_statusLabel->setText(tr("评估失败：%1").arg(res.errorMessage));
+            return;
+        }
+
+        m_hasResults = true;
+        m_statusLabel->setText(tr("评估完成。"));
+
+        m_meanCohLabel->setText(QString::number(res.meanCoh, 'f', 4));
+        m_medianCohLabel->setText(QString::number(res.medianCoh, 'f', 4));
+        m_maxCohLabel->setText(QString::number(res.maxCoh, 'f', 4));
+        m_highCohPctLabel->setText(QString("%1 %").arg(res.highCohPct, 0, 'f', 2));
+        
+        // 计算预估相位噪声
+        // 理论上，当完全失相干(y=0)时，相位服从[-180, 180]的均匀分布，其标准差约为 104° (即 180/sqrt(3))。
+        // 当完全相干(y=1)时，标准差为 0°。这里使用稳健的经验线性映射进行直观展示。
+        double phaseNoise = 104.0 * (1.0 - res.meanCoh);
+        if (phaseNoise < 0.0) phaseNoise = 0.0;
+        
+        m_phaseNoiseLabel->setText(QString(tr("约 %1°")).arg(phaseNoise, 0, 'f', 1));
+        
+        m_enlLabel->setText("5 x 5 (25 Looks)");
+        m_flatEarthLabel->setText(tr("[√] 已移除 (轨道辅助)"));
+        m_flatEarthLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #10B981;");
+        m_topoLabel->setText(tr("[√] 已移除 (DEM辅助)"));
+        m_topoLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #10B981;");
+
+        // 根据经验阈值更新诊断卡片
+        if (res.meanCoh > 0.4f && res.highCohPct > 30.0f) {
+            m_statusCardTitle->setText(tr("干涉质量良好"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #059669;"); // 绿色
+            m_statusCardDesc->setText(tr("整体相干性较高，干涉条纹预期清晰。符合后续相位解缠和形变提取要求。"));
+            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #10B981;");
+            m_statusCard->setStyleSheet("background-color: rgba(16, 185, 129, 0.1); border: 1px solid rgba(16, 185, 129, 0.3); border-radius: 4px;");
+        } else if (res.meanCoh >= 0.2f) {
+            m_statusCardTitle->setText(tr("相干性一般"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #D97706;"); // 橙色
+            m_statusCardDesc->setText(tr("存在一定的去相干（可能受植被覆盖、较长基线或时间跨度影响）。建议在后续节点适当增加滤波强度。"));
+            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #F59E0B;");
+            m_statusCard->setStyleSheet("background-color: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.3); border-radius: 4px;");
+        } else {
+            m_statusCardTitle->setText(tr("严重去相干"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #DC2626;"); // 红色
+            m_statusCardDesc->setText(tr("整体相干性极低，干涉相位可能完全被噪声掩盖。请检查输入影像的时空基线，或确保前置配准精度达标。"));
+            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #EF4444;");
+            m_statusCard->setStyleSheet("background-color: rgba(239, 68, 68, 0.1); border: 1px solid rgba(239, 68, 68, 0.3); border-radius: 4px;");
+        }
+    }
+
+    InterferometricFormationNode* m_node;
+    QComboBox* m_slaveCombo;
+    QComboBox* m_visualModeCombo;
+    QPushButton* m_evalBtn;
+    ImageView* m_imageView;
+
+    QFrame* m_statusCard;
+    QLabel* m_statusCardTitle;
+    QLabel* m_statusCardDesc;
+
+    QLabel* m_meanCohLabel;
+    QLabel* m_medianCohLabel;
+    QLabel* m_maxCohLabel;
+    QLabel* m_highCohPctLabel;
+    QLabel* m_phaseNoiseLabel;
+    QLabel* m_enlLabel;
+    QLabel* m_flatEarthLabel;
+    QLabel* m_topoLabel;
+    QLabel* m_statusLabel;
+
+    QStringList m_h5PathsPhase;
+    QStringList m_h5PathsCoh;
+    QStringList m_jpgPathsPhase;
+    QStringList m_jpgPathsCoh;
+
+    bool m_hasResults;
+    QFutureWatcher<InterfEvalThreadResult> m_watcher;
+};
+
+::QWidget* InterferometricFormationNode::createInterferometryWidget(::QWidget* parent)
+{
+    return new InterferometricFormationEvalWidget(this, parent);
+}
+
 } // namespace QtNodes
diff --git a/QtNodes/src/ExecutableNodeDelegateModel.cpp b/QtNodes/src/ExecutableNodeDelegateModel.cpp
index 67b5814164123fdaf36f0c5c2397855214fdc707..6d04b976281c0bfb828a34a8e11db5a4b8cd9af8 100644
--- a/QtNodes/src/ExecutableNodeDelegateModel.cpp
+++ b/QtNodes/src/ExecutableNodeDelegateModel.cpp
@@ -134,6 +134,7 @@ void ExecutableNodeDelegateModel::setInData(std::shared_ptr<NodeData> nodeData,
 
             // Let subclass do the automatic processing (sets output data if inputs are complete)
             // Mark as auto-triggered so executeProcessing() can skip overwrite popups
+            _startFailureMessage.clear();
             _isAutoTriggered = true;
             _deferAutomaticCompletion = false;
             processAutomatically();
@@ -148,6 +149,9 @@ void ExecutableNodeDelegateModel::setInData(std::shared_ptr<NodeData> nodeData,
             // or if it launched an asynchronous thread and is still Running,
             // we MUST NOT override its state with default completion logic!
             if (_state != ExecutionState::Running) {
+                if (!_startFailureMessage.isEmpty()) {
+                    Q_EMIT executionStartRejected(_startFailureMessage);
+                }
                 Q_EMIT executionStateChanged();
                 triggerVisualUpdate();
                 return;
diff --git a/QtNodes/src/NodeDetailWindow.cpp b/QtNodes/src/NodeDetailWindow.cpp
index b14303a7a8274109098ee28fb7cfa86b6aef0003..683aa4e9604d47dbbc1c6712ea616dbb2a5ff72e 100644
--- a/QtNodes/src/NodeDetailWindow.cpp
+++ b/QtNodes/src/NodeDetailWindow.cpp
@@ -1,5 +1,6 @@
 #include "QtNodes/internal/NodeDetailWindow.hpp"
 #include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
+#include <QBoxLayout>
 #include <QVBoxLayout>
 #include <QHBoxLayout>
 #include <QLabel>
@@ -18,6 +19,7 @@
 #include <QPixmap>
 #include <QStyle>
 #include <QSize>
+#include <QSizePolicy>
 #include <QDebug>
 #include <QFileInfo>
 #include "ImageView.h"
@@ -1875,6 +1877,29 @@ void ValidationComparisonTable::addComparison(const QString& name, const QString
     setItem(row, 3, item3);
 }
 
+void ValidationComparisonTable::addDiagnostic(const QString& name, const QString& value)
+{
+    const int row = rowCount();
+    insertRow(row);
+
+    auto* nameItem = new QTableWidgetItem(name);
+    auto* settingItem = new QTableWidgetItem(QStringLiteral("-"));
+    auto* valueItem = new QTableWidgetItem(value);
+    auto* conclusionItem = new QTableWidgetItem(QObject::tr("诊断"));
+
+    const bool isDark = NodeDetailWindow::isDarkTheme(this);
+    const QBrush textBrush = QColor(isDark ? "#D1D5DB" : "#374151");
+    nameItem->setForeground(textBrush);
+    settingItem->setForeground(textBrush);
+    valueItem->setForeground(textBrush);
+    conclusionItem->setForeground(QBrush(QColor(isDark ? "#60A5FA" : "#2563EB")));
+
+    setItem(row, 0, nameItem);
+    setItem(row, 1, settingItem);
+    setItem(row, 2, valueItem);
+    setItem(row, 3, conclusionItem);
+}
+
 void ValidationComparisonTable::applyThemeStyle()
 {
     bool isDark = NodeDetailWindow::isDarkTheme(this);
@@ -2045,7 +2070,8 @@ BaseValidationWidget::BaseValidationWidget(ExecutableNodeDelegateModel* node, QW
 {
 }
 
-void BaseValidationWidget::setupBaseUI(const QString& initialTitle, const QString& initialDesc, const QString& featureTitleText)
+void BaseValidationWidget::setupBaseUI(const QString& initialTitle, const QString& initialDesc, const QString& featureTitleText,
+    const QString& comparisonTitleText, bool stackContentVertically, bool scrollFeaturePanel)
 {
     bool isDark = NodeDetailWindow::isDarkTheme(this);
     QVBoxLayout* mainLayout = new QVBoxLayout(this);
@@ -2066,26 +2092,34 @@ void BaseValidationWidget::setupBaseUI(const QString& initialTitle, const QStrin
     m_statusTitle = new QLabel(initialTitle, m_statusCard);
     m_statusTitle->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(isDark ? "#60A5FA" : "#2563EB"));
     m_statusDesc = new QLabel(initialDesc, m_statusCard);
+    m_statusDesc->setWordWrap(true);
     m_statusDesc->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
 
     cardLayout->addWidget(m_statusTitle);
     cardLayout->addWidget(m_statusDesc);
     mainLayout->addWidget(m_statusCard);
 
-    // 2. Main content area (Split into Comparison Table & Feature Calculation)
-    QHBoxLayout* contentLayout = new QHBoxLayout();
+    // 2. Main content area. Most validators use columns; diagnostics-heavy pages can opt into stacking.
+    QBoxLayout* contentLayout = stackContentVertically
+        ? static_cast<QBoxLayout*>(new QVBoxLayout())
+        : static_cast<QBoxLayout*>(new QHBoxLayout());
     contentLayout->setSpacing(12);
 
     // Left column: Parameter comparison table
     QVBoxLayout* leftLayout = new QVBoxLayout();
     leftLayout->setSpacing(6);
-    QLabel* tableTitle = new QLabel(QObject::tr("物理参数比对"), this);
+    const QString tableTitleText = comparisonTitleText.isEmpty() ? QObject::tr("物理参数比对") : comparisonTitleText;
+    QLabel* tableTitle = new QLabel(tableTitleText, this);
     tableTitle->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
     leftLayout->addWidget(tableTitle);
 
     m_compTable = new ValidationComparisonTable(this);
+    m_compTable->setMinimumWidth(0);
+    m_compTable->setMinimumHeight(0);
+    m_compTable->setSizePolicy(QSizePolicy::Expanding,
+        stackContentVertically ? QSizePolicy::Ignored : QSizePolicy::Expanding);
     leftLayout->addWidget(m_compTable, 1);
-    contentLayout->addLayout(leftLayout, 3);
+    contentLayout->addLayout(leftLayout, stackContentVertically ? 3 : 2);
 
     // Right column: Feature analysis panel
     QVBoxLayout* rightLayout = new QVBoxLayout();
@@ -2095,6 +2129,10 @@ void BaseValidationWidget::setupBaseUI(const QString& initialTitle, const QStrin
     rightLayout->addWidget(fTitle);
 
     m_featureCard = new QFrame(this);
+    m_featureCard->setMinimumWidth(0);
+    m_featureCard->setMinimumHeight(0);
+    m_featureCard->setSizePolicy(stackContentVertically ? QSizePolicy::Expanding : QSizePolicy::Ignored,
+        QSizePolicy::Preferred);
     m_featureCard->setStyleSheet(isDark ?
         "QFrame { background-color: #1F2937; border: 1px solid #374151; border-radius: 4px; padding: 12px; }" :
         "QFrame { background-color: #FFFFFF; border: 1px solid #E5E7EB; border-radius: 4px; padding: 12px; }");
@@ -2103,9 +2141,23 @@ void BaseValidationWidget::setupBaseUI(const QString& initialTitle, const QStrin
     m_featureLayout->setContentsMargins(8, 8, 8, 8);
     m_featureLayout->setSpacing(10);
     m_featureLayout->setLabelAlignment(Qt::AlignRight);
-
-    rightLayout->addWidget(m_featureCard, 1);
-    contentLayout->addLayout(rightLayout, 2);
+    m_featureLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
+    m_featureLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
+
+    if (scrollFeaturePanel) {
+        m_featureScrollArea = new QScrollArea(this);
+        m_featureScrollArea->setWidgetResizable(true);
+        m_featureScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
+        m_featureScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
+        m_featureScrollArea->setMinimumHeight(120);
+        m_featureScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
+        m_featureScrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
+        m_featureScrollArea->setWidget(m_featureCard);
+        rightLayout->addWidget(m_featureScrollArea, 1);
+    } else {
+        rightLayout->addWidget(m_featureCard, 1);
+    }
+    contentLayout->addLayout(rightLayout, stackContentVertically ? 2 : 3);
 
     mainLayout->addLayout(contentLayout, 1);
 
@@ -2120,10 +2172,27 @@ void BaseValidationWidget::setupBaseUI(const QString& initialTitle, const QStrin
     connect(m_loadingOverlay, &ValidationLoadingOverlay::timeoutOccurred, this, &BaseValidationWidget::onTimeout);
 }
 
+QGridLayout* BaseValidationWidget::replaceFeatureFormWithGrid()
+{
+    delete m_featureLayout;
+    m_featureLayout = nullptr;
+
+    QGridLayout* featureGrid = new QGridLayout(m_featureCard);
+    featureGrid->setContentsMargins(8, 8, 8, 8);
+    featureGrid->setHorizontalSpacing(24);
+    featureGrid->setVerticalSpacing(12);
+    featureGrid->setColumnStretch(0, 1);
+    featureGrid->setColumnStretch(1, 1);
+    return featureGrid;
+}
+
 QLabel* BaseValidationWidget::createFeatureLabel()
 {
     bool isDark = NodeDetailWindow::isDarkTheme(this);
     QLabel* lbl = new QLabel(QObject::tr("正在计算..."), m_featureCard);
+    lbl->setWordWrap(true);
+    lbl->setMinimumWidth(0);
+    lbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
     lbl->setStyleSheet(QString("font-weight: 600; color: %1;").arg(isDark ? "#F3F4F6" : "#111827"));
     return lbl;
 }
@@ -2132,6 +2201,9 @@ QLabel* BaseValidationWidget::createHeaderLabel(const QString& text)
 {
     bool isDark = NodeDetailWindow::isDarkTheme(this);
     QLabel* lbl = new QLabel(text);
+    lbl->setWordWrap(true);
+    lbl->setMinimumWidth(0);
+    lbl->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
     lbl->setStyleSheet(QString("color: %1; font-weight: 500;").arg(isDark ? "#9CA3AF" : "#4B5563"));
     return lbl;
 }
diff --git a/S1TopsBackGeocodingNode.cpp b/S1TopsBackGeocodingNode.cpp
index ae2984260cc7c214bc6e789654498511592e373d..a02f34a75a5a0e003dde2e36c76e3a2227f61377 100644
--- a/S1TopsBackGeocodingNode.cpp
+++ b/S1TopsBackGeocodingNode.cpp
@@ -2501,9 +2501,10 @@ private:
                 inputLocker = std::make_unique<NodeUtils::Hdf5Locker>(inputMasterPath);
             }
 
-            EvalThreadResult threadRes;
+            EvalThreadResult threadRes{};
             threadRes.retCode = -1;
             for (int i = 0; i < 5; ++i) {
+                threadRes.results[i].structSize = sizeof(AlignmentResult);
                 threadRes.results[i].heatmap_rgb = nullptr;
                 threadRes.results[i].overlay_rgb = nullptr;
                 threadRes.results[i].imageWidth = 0;
@@ -2541,8 +2542,9 @@ private:
             // 计算配准前的 0 位移相干性
             if (threadRes.retCode == 0 && !inputMasterPath.isEmpty() && !inputSlavePath.isEmpty() &&
                 QFile::exists(inputMasterPath) && QFile::exists(inputSlavePath)) {
-                AlignmentResult inputRes[5];
+                AlignmentResult inputRes[5]{};
                 for (int i = 0; i < 5; ++i) {
+                    inputRes[i].structSize = sizeof(AlignmentResult);
                     inputRes[i].heatmap_rgb = nullptr;
                     inputRes[i].overlay_rgb = nullptr;
                     inputRes[i].imageWidth = 0;
@@ -2732,17 +2734,15 @@ private:
                 preCohValidCount++;
             }
             
-            int dy = std::abs(m_results[i].offsetY);
-            int dx = std::abs(m_results[i].offsetX);
+            double dy = std::abs(m_results[i].offsetY);
+            double dx = std::abs(m_results[i].offsetX);
             
             if (maxCorr < 0.15 || postCoh < 0.20) {
-                // 如果相关系数过低（低于 0.15）或相干性过低（低于 0.20），说明当前区域是噪声区，偏移量不具有置信度
-                // 此时忽略其对 FAILED 的统计贡献，防止噪声误导，默认认为物理对齐良好（由相干性判定主导）
                 perfectCount++;
             } else {
-                if (dy == 0 && dx == 0) {
+                if (dy < 0.01 && dx < 0.01) {
                     perfectCount++;
-                } else if (dy <= 2 && dx <= 2) {
+                } else if (dy <= 2.0 && dx <= 2.0) {
                     warningCount++;
                 } else {
                     failedCount++;
@@ -2772,7 +2772,7 @@ private:
                 QString("Assessment sample=%1, point=(%2,%3), coherence={pre=%4, post=%5, optimal=%6}, residualOffset=(%7,%8), correlation=%9, lowConfidence=%10")
                     .arg(i + 1).arg(m_points[i].x).arg(m_points[i].y)
                     .arg(m_inputCoherence[i], 0, 'f', 4).arg(m_results[i].coherenceZeroShift, 0, 'f', 4)
-                    .arg(m_results[i].coherenceOptimal, 0, 'f', 4).arg(m_results[i].offsetX).arg(m_results[i].offsetY)
+                    .arg(m_results[i].coherenceOptimal, 0, 'f', 4).arg(m_results[i].offsetX, 0, 'f', 2).arg(m_results[i].offsetY, 0, 'f', 2)
                     .arg(m_results[i].maxCorrelation, 0, 'f', 4)
                     .arg((m_results[i].maxCorrelation < 0.15 || m_results[i].coherenceZeroShift < 0.20) ? "true" : "false"),
                 "registration.assessment.sample");
diff --git a/S1TopsBackGeocodingWorker.cpp b/S1TopsBackGeocodingWorker.cpp
index 2fd52f0443111a3dca48a14e556866e4c0268127..f9b4a5e9729e8f3f4f2ca361345a6cfa44e51d0b 100644
--- a/S1TopsBackGeocodingWorker.cpp
+++ b/S1TopsBackGeocodingWorker.cpp
@@ -962,8 +962,9 @@ void S1TopsBackGeocodingWorker::S1_TOPS_BackGeocoding(
 				double offset_r = 0.0;
 				if (detectRet >= 0)
 				{
-					AlignmentResult res[5];
+					AlignmentResult res[5]{};
 					for (int k = 0; k < 5; ++k) {
+						res[k].structSize = sizeof(AlignmentResult);
 						res[k].heatmap_rgb = nullptr;
 						res[k].overlay_rgb = nullptr;
 					}
diff --git a/UnwrapNode.cpp b/UnwrapNode.cpp
index 95207b532a9c912898f9cd579ce3e19b93e2d375..133a2542d2650f2ddcb2ca5c026cceec5a36c4b5 100644
--- a/UnwrapNode.cpp
+++ b/UnwrapNode.cpp
@@ -10,17 +10,25 @@
 #include <QVBoxLayout>
 #include <QHBoxLayout>
 #include <QFormLayout>
+#include <QGridLayout>
 #include <QFile>
 #include <QFileInfo>
 #include <QRegularExpression>
 #include <QDir>
 #include <QApplication>
 #include <QDateTime>
+#include <QHash>
+#include <QRect>
 #include <QStandardItemModel>
 #include <QDebug>
 #include <QMessageBox>
 #include <QTimer>
 #include <QtConcurrent/QtConcurrent>
+#include <opencv2/imgproc.hpp>
+#include <algorithm>
+#include <cmath>
+#include <vector>
+#include "QtNodes/internal/NodeDetailWindow.hpp"
 
 namespace QtNodes {
 
@@ -746,4 +754,493 @@ void UnwrapNode::processAutomatically()
     }
 }
 
+namespace {
+
+QString unwrapMethodName(int method)
+{
+    switch (method) {
+    case 1: return QStringLiteral("SPD Guided");
+    case 2: return QStringLiteral("MCF");
+    case 3: return QStringLiteral("SNAPHU");
+    case 4: return QStringLiteral("Quality Guided MCF");
+    default: return QObject::tr("未知");
+    }
+}
+
+struct UnwrapImageDiagnostics
+{
+    QString name;
+    bool outputFound = false;
+    bool dimensionsMatch = false;
+    int inputRows = 0;
+    int inputCols = 0;
+    int outputRows = 0;
+    int outputCols = 0;
+    qint64 inputFinite = 0;
+    qint64 pairedFinite = 0;
+    qint64 outputFinite = 0;
+    double rewrapRmse = 0.0;
+    double rewrapP95 = 0.0;
+    bool hasRewrapMetrics = false;
+    int validComponentCount = 0;
+    qint64 largestValidComponentPixels = 0;
+    double largestValidComponentRatio = 0.0;
+    qint64 gradientComparedEdges = 0;
+    qint64 gradientRiskEdges = 0;
+    int gradientRiskComponentCount = 0;
+    qint64 largestGradientRiskPixels = 0;
+    QRect largestGradientRiskBounds;
+    bool hasSpatialMetrics = false;
+};
+
+struct UnwrapValidationResults
+{
+    bool success = false;
+    QString errorMessage;
+    int expectedMethod = 1;
+    double expectedThreshold = 0.2;
+    bool hasRecordedMethod = false;
+    int recordedMethod = 0;
+    bool hasRecordedThreshold = false;
+    double recordedThreshold = 0.0;
+    bool outputMetadataConsistent = true;
+    QList<UnwrapImageDiagnostics> images;
+};
+
+class UnwrapValidationWidget : public BaseValidationWidget
+{
+public:
+    UnwrapValidationWidget(UnwrapNode* node, QWidget* parent)
+        : BaseValidationWidget(node, parent)
+        , m_node(node)
+    {
+        setupUI();
+        startAsyncValidation();
+    }
+
+private:
+    void setupUI()
+    {
+        setupBaseUI(QObject::tr("正在诊断解缠结果..."),
+                    QObject::tr("正在读取全部输入与输出 H5 数据，检查完整性、重新缠绕一致性和空间风险线索。"),
+                    QObject::tr("解缠诊断汇总"),
+                    QObject::tr("解缠参数与结果诊断"), true, true);
+
+        m_coverageLabel = createFeatureLabel();
+        m_rewrapRmseLabel = createFeatureLabel();
+        m_rewrapP95Label = createFeatureLabel();
+        m_componentLabel = createFeatureLabel();
+        m_largestComponentLabel = createFeatureLabel();
+        m_gradientRiskLabel = createFeatureLabel();
+        m_riskRegionLabel = createFeatureLabel();
+        m_missingLabel = createFeatureLabel();
+        QGridLayout* featureGrid = replaceFeatureFormWithGrid();
+        const auto addMetric = [this, featureGrid](int row, int column, int columnSpan,
+            const QString& title, QLabel* value) {
+            QWidget* metric = new QWidget(m_featureCard);
+            QHBoxLayout* metricLayout = new QHBoxLayout(metric);
+            metricLayout->setContentsMargins(0, 0, 0, 0);
+            metricLayout->setSpacing(8);
+            QLabel* titleLabel = createHeaderLabel(title);
+            titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
+            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
+            metricLayout->addWidget(titleLabel, 2);
+            metricLayout->addWidget(value, 1);
+            featureGrid->addWidget(metric, row, column, 1, columnSpan);
+        };
+
+        addMetric(0, 0, 1, QObject::tr("配对有效像元覆盖率:"), m_coverageLabel);
+        addMetric(0, 1, 1, QObject::tr("重新缠绕圆差 RMSE (rad):"), m_rewrapRmseLabel);
+        addMetric(1, 0, 1, QObject::tr("重新缠绕圆差 P95 估计 (rad):"), m_rewrapP95Label);
+        addMetric(1, 1, 1, QObject::tr("有效输出连通域:"), m_componentLabel);
+        addMetric(2, 0, 1, QObject::tr("最大连通域占比:"), m_largestComponentLabel);
+        addMetric(2, 1, 1, QObject::tr("高梯度风险边比例 (|delta| > pi):"), m_gradientRiskLabel);
+        addMetric(3, 0, 2, QObject::tr("最大风险区域:"), m_riskRegionLabel);
+        addMetric(4, 0, 2, QObject::tr("缺失或尺寸异常结果:"), m_missingLabel);
+    }
+
+    void setNotExecutedState()
+    {
+        m_statusTitle->setText(QObject::tr("诊断不可用"));
+        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
+        m_statusDesc->setText(QObject::tr("请先成功执行解缠节点，再查看诊断结果。"));
+        m_compTable->clearComparison();
+        m_compTable->setEnabled(false);
+        m_coverageLabel->setText(QObject::tr("未执行"));
+        m_rewrapRmseLabel->setText(QObject::tr("未执行"));
+        m_rewrapP95Label->setText(QObject::tr("未执行"));
+        m_componentLabel->setText(QObject::tr("未执行"));
+        m_largestComponentLabel->setText(QObject::tr("未执行"));
+        m_gradientRiskLabel->setText(QObject::tr("未执行"));
+        m_riskRegionLabel->setText(QObject::tr("未执行"));
+        m_missingLabel->setText(QObject::tr("未执行"));
+    }
+
+    void startAsyncValidation() override
+    {
+        m_isTimedOut = false;
+        if (m_node->executionState() != ExecutionState::Completed) {
+            setNotExecutedState();
+            return;
+        }
+
+        const auto inputData = std::dynamic_pointer_cast<ImportedFileData>(m_node->getInputData(0));
+        const auto outputData = std::dynamic_pointer_cast<ImportedFileData>(m_node->outData(0));
+        if (!inputData || inputData->filePaths().isEmpty() || !outputData || outputData->filePaths().isEmpty()) {
+            m_statusTitle->setText(QObject::tr("诊断失败"));
+            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
+            m_statusDesc->setText(QObject::tr("未找到完整的输入或输出 H5 文件列表。"));
+            return;
+        }
+
+        const QStringList inputPaths = inputData->filePaths();
+        const QStringList outputPaths = outputData->filePaths();
+        const QJsonObject settings = m_node->save();
+        const int expectedMethod = settings.value("method").toInt(1);
+        const double expectedThreshold = settings.value("coherenceThreshold").toDouble(0.2);
+        m_loadingOverlay->startLoading(QObject::tr("正在计算全部影像的解缠诊断..."));
+
+        QFuture<UnwrapValidationResults> future = QtConcurrent::run([inputPaths, outputPaths, expectedMethod, expectedThreshold]() {
+            NodeUtils::Hdf5Locker locker;
+            UnwrapValidationResults result;
+            result.expectedMethod = expectedMethod;
+            result.expectedThreshold = expectedThreshold;
+
+            QHash<QString, QString> outputsByBaseName;
+            for (const QString& outputPath : outputPaths) {
+                outputsByBaseName.insert(QFileInfo(outputPath).baseName(), outputPath);
+            }
+
+            bool firstMetadata = true;
+
+            for (const QString& inputPath : inputPaths) {
+                UnwrapImageDiagnostics image;
+                image.name = QFileInfo(inputPath).baseName();
+                const QString outputPath = outputsByBaseName.value(image.name + QStringLiteral("_unwrapped"));
+                image.outputFound = !outputPath.isEmpty();
+
+                if (!image.outputFound) {
+                    result.images.append(image);
+                    continue;
+                }
+
+                int method = 0;
+                double threshold = 0.0;
+                const bool hasMethod = NodeUtils::readScalarFromH5(outputPath, "unwrap_method", method);
+                const bool hasThreshold = NodeUtils::readScalarFromH5(outputPath, "unwrap_coherence_threshold", threshold);
+                if (firstMetadata) {
+                    result.hasRecordedMethod = hasMethod;
+                    result.recordedMethod = method;
+                    result.hasRecordedThreshold = hasThreshold;
+                    result.recordedThreshold = threshold;
+                    firstMetadata = false;
+                } else if ((!hasMethod || !result.hasRecordedMethod || method != result.recordedMethod)
+                           || (!hasThreshold || !result.hasRecordedThreshold || std::abs(threshold - result.recordedThreshold) > 1e-9)) {
+                    result.outputMetadataConsistent = false;
+                }
+
+                cv::Mat inputPhase;
+                cv::Mat outputPhase;
+                if (!NodeUtils::readMatFromH5(inputPath, "phase", inputPhase, CV_32F)
+                    || !NodeUtils::readMatFromH5(outputPath, "phase", outputPhase, CV_32F)
+                    || inputPhase.empty() || outputPhase.empty()) {
+                    result.images.append(image);
+                    continue;
+                }
+
+                image.inputRows = inputPhase.rows;
+                image.inputCols = inputPhase.cols;
+                image.outputRows = outputPhase.rows;
+                image.outputCols = outputPhase.cols;
+                image.dimensionsMatch = inputPhase.size() == outputPhase.size();
+
+                double squaredResidualSum = 0.0;
+                const qint64 pixelCount = static_cast<qint64>(inputPhase.total());
+                constexpr qint64 maxResidualSamples = 200000;
+                const qint64 sampleStride = std::max<qint64>(1, (pixelCount + maxResidualSamples - 1) / maxResidualSamples);
+                std::vector<double> residualSamples;
+                residualSamples.reserve(static_cast<size_t>(std::min<qint64>((pixelCount + sampleStride - 1) / sampleStride, maxResidualSamples)));
+                for (int row = 0; row < inputPhase.rows; ++row) {
+                    const float* inputValues = inputPhase.ptr<float>(row);
+                    const float* outputValues = image.dimensionsMatch ? outputPhase.ptr<float>(row) : nullptr;
+                    for (int col = 0; col < inputPhase.cols; ++col) {
+                        const double inputValue = inputValues[col];
+                        if (!std::isfinite(inputValue)) {
+                            continue;
+                        }
+                        ++image.inputFinite;
+                        if (!outputValues || !std::isfinite(outputValues[col])) {
+                            continue;
+                        }
+
+                        const double residual = std::abs(std::atan2(std::sin(outputValues[col] - inputValue),
+                                                                    std::cos(outputValues[col] - inputValue)));
+                        squaredResidualSum += residual * residual;
+                        if (image.pairedFinite % sampleStride == 0) {
+                            residualSamples.push_back(residual);
+                        }
+                        ++image.pairedFinite;
+                    }
+                }
+
+                if (image.pairedFinite > 0) {
+                    image.hasRewrapMetrics = true;
+                    image.rewrapRmse = std::sqrt(squaredResidualSum / image.pairedFinite);
+                    if (!residualSamples.empty()) {
+                        const size_t p95Index = static_cast<size_t>(std::ceil(residualSamples.size() * 0.95)) - 1;
+                        std::nth_element(residualSamples.begin(), residualSamples.begin() + p95Index, residualSamples.end());
+                        image.rewrapP95 = residualSamples[p95Index];
+                    }
+                }
+
+                constexpr double highGradientThreshold = 3.14159265358979323846;
+                cv::Mat validOutputMask = cv::Mat::zeros(outputPhase.size(), CV_8U);
+                cv::Mat gradientRiskMask = cv::Mat::zeros(outputPhase.size(), CV_8U);
+                for (int row = 0; row < outputPhase.rows; ++row) {
+                    const float* outputValues = outputPhase.ptr<float>(row);
+                    uchar* validMaskValues = validOutputMask.ptr<uchar>(row);
+                    for (int col = 0; col < outputPhase.cols; ++col) {
+                        if (std::isfinite(outputValues[col])) {
+                            validMaskValues[col] = 255;
+                            ++image.outputFinite;
+                        }
+                    }
+                }
+
+                if (image.outputFinite > 0) {
+                    cv::Mat labels;
+                    cv::Mat stats;
+                    cv::Mat centroids;
+                    const int labelCount = cv::connectedComponentsWithStats(
+                        validOutputMask, labels, stats, centroids, 8, CV_32S);
+                    image.validComponentCount = std::max(0, labelCount - 1);
+                    for (int label = 1; label < labelCount; ++label) {
+                        const qint64 area = stats.at<int>(label, cv::CC_STAT_AREA);
+                        image.largestValidComponentPixels = std::max(image.largestValidComponentPixels, area);
+                    }
+                    image.largestValidComponentRatio = 100.0 * image.largestValidComponentPixels / image.outputFinite;
+                }
+
+                for (int row = 0; row < outputPhase.rows; ++row) {
+                    const float* currentValues = outputPhase.ptr<float>(row);
+                    const float* nextRowValues = row + 1 < outputPhase.rows ? outputPhase.ptr<float>(row + 1) : nullptr;
+                    uchar* riskValues = gradientRiskMask.ptr<uchar>(row);
+                    uchar* nextRiskValues = row + 1 < outputPhase.rows ? gradientRiskMask.ptr<uchar>(row + 1) : nullptr;
+                    for (int col = 0; col < outputPhase.cols; ++col) {
+                        if (!std::isfinite(currentValues[col])) {
+                            continue;
+                        }
+
+                        if (col + 1 < outputPhase.cols && std::isfinite(currentValues[col + 1])) {
+                            ++image.gradientComparedEdges;
+                            if (std::abs(static_cast<double>(currentValues[col + 1]) - currentValues[col]) > highGradientThreshold) {
+                                ++image.gradientRiskEdges;
+                                riskValues[col] = 255;
+                                riskValues[col + 1] = 255;
+                            }
+                        }
+
+                        if (nextRowValues && std::isfinite(nextRowValues[col])) {
+                            ++image.gradientComparedEdges;
+                            if (std::abs(static_cast<double>(nextRowValues[col]) - currentValues[col]) > highGradientThreshold) {
+                                ++image.gradientRiskEdges;
+                                riskValues[col] = 255;
+                                nextRiskValues[col] = 255;
+                            }
+                        }
+                    }
+                }
+
+                if (image.gradientRiskEdges > 0) {
+                    cv::Mat labels;
+                    cv::Mat stats;
+                    cv::Mat centroids;
+                    const int labelCount = cv::connectedComponentsWithStats(
+                        gradientRiskMask, labels, stats, centroids, 8, CV_32S);
+                    image.gradientRiskComponentCount = std::max(0, labelCount - 1);
+                    for (int label = 1; label < labelCount; ++label) {
+                        const qint64 area = stats.at<int>(label, cv::CC_STAT_AREA);
+                        if (area > image.largestGradientRiskPixels) {
+                            image.largestGradientRiskPixels = area;
+                            image.largestGradientRiskBounds = QRect(
+                                stats.at<int>(label, cv::CC_STAT_LEFT),
+                                stats.at<int>(label, cv::CC_STAT_TOP),
+                                stats.at<int>(label, cv::CC_STAT_WIDTH),
+                                stats.at<int>(label, cv::CC_STAT_HEIGHT));
+                        }
+                    }
+                }
+                image.hasSpatialMetrics = image.outputFinite > 0;
+                result.images.append(image);
+            }
+
+            result.success = !result.images.isEmpty();
+            if (!result.success) {
+                result.errorMessage = QObject::tr("没有可用于诊断的解缠影像。");
+            }
+            return result;
+        });
+
+        auto* watcher = new QFutureWatcher<UnwrapValidationResults>(this);
+        connect(watcher, &QFutureWatcher<UnwrapValidationResults>::finished, this, [this, watcher]() {
+            if (m_isTimedOut) {
+                watcher->deleteLater();
+                return;
+            }
+
+            const UnwrapValidationResults result = watcher->result();
+            m_loadingOverlay->stopLoading();
+            if (!result.success) {
+                m_statusTitle->setText(QObject::tr("诊断失败"));
+                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
+                m_statusDesc->setText(result.errorMessage);
+                watcher->deleteLater();
+                return;
+            }
+
+            m_compTable->clearComparison();
+            m_compTable->setEnabled(true);
+            const QString actualMethod = result.hasRecordedMethod ? unwrapMethodName(result.recordedMethod) : QObject::tr("未记录（旧结果）");
+            m_compTable->addComparison(QObject::tr("解缠算法"), unwrapMethodName(result.expectedMethod), actualMethod);
+            int outputCount = 0;
+            for (const UnwrapImageDiagnostics& image : result.images) {
+                outputCount += image.outputFound ? 1 : 0;
+            }
+            m_compTable->addComparison(QObject::tr("输入/输出影像数"), QString::number(result.images.size()), QString::number(outputCount));
+            if (result.expectedMethod == 4) {
+                const QString actualThreshold = result.hasRecordedThreshold
+                    ? QString::number(result.recordedThreshold, 'f', 3)
+                    : QObject::tr("未记录（旧结果）");
+                m_compTable->addComparison(QObject::tr("相干系数阈值"), QString::number(result.expectedThreshold, 'f', 3), actualThreshold);
+            }
+
+            qint64 inputFinite = 0;
+            qint64 pairedFinite = 0;
+            double sumSquaredResidual = 0.0;
+            qint64 gradientComparedEdges = 0;
+            qint64 gradientRiskEdges = 0;
+            int totalValidComponents = 0;
+            int maxValidComponents = 0;
+            double smallestLargestComponentRatio = 100.0;
+            const UnwrapImageDiagnostics* largestRiskImage = nullptr;
+            int missingOrInvalid = 0;
+            for (const UnwrapImageDiagnostics& image : result.images) {
+                const QString expected = QStringLiteral("%1 x %2").arg(image.inputCols).arg(image.inputRows);
+                QString actual;
+                if (!image.outputFound) {
+                    actual = QObject::tr("缺失输出");
+                    ++missingOrInvalid;
+                } else if (!image.dimensionsMatch) {
+                    actual = QObject::tr("尺寸不匹配: %1 x %2").arg(image.outputCols).arg(image.outputRows);
+                    ++missingOrInvalid;
+                } else if (!image.hasRewrapMetrics) {
+                    actual = QObject::tr("无有效配对像元");
+                    ++missingOrInvalid;
+                } else {
+                    actual = QStringLiteral("%1 x %2").arg(image.outputCols).arg(image.outputRows);
+                }
+                m_compTable->addComparison(image.name, expected, actual);
+
+                if (image.hasRewrapMetrics) {
+                    const double imageCoverage = image.inputFinite > 0 ? 100.0 * image.pairedFinite / image.inputFinite : 0.0;
+                    const double riskRatio = image.gradientComparedEdges > 0
+                        ? 100.0 * image.gradientRiskEdges / image.gradientComparedEdges : 0.0;
+                    m_compTable->addDiagnostic(image.name + QObject::tr(" 诊断"),
+                        QObject::tr("覆盖 %1%, RMSE %2 rad, P95 %3 rad, 连通域 %4, 最大占比 %5%, 高梯度风险 %6%")
+                            .arg(QString::number(imageCoverage, 'f', 2),
+                                 QString::number(image.rewrapRmse, 'g', 4),
+                                 QString::number(image.rewrapP95, 'g', 4),
+                                 QString::number(image.validComponentCount),
+                                 QString::number(image.largestValidComponentRatio, 'f', 2),
+                                 QString::number(riskRatio, 'f', 4)));
+                }
+
+                inputFinite += image.inputFinite;
+                pairedFinite += image.pairedFinite;
+                if (image.hasRewrapMetrics) {
+                    sumSquaredResidual += image.rewrapRmse * image.rewrapRmse * image.pairedFinite;
+                }
+                if (image.hasSpatialMetrics) {
+                    totalValidComponents += image.validComponentCount;
+                    maxValidComponents = std::max(maxValidComponents, image.validComponentCount);
+                    smallestLargestComponentRatio = std::min(smallestLargestComponentRatio, image.largestValidComponentRatio);
+                    gradientComparedEdges += image.gradientComparedEdges;
+                    gradientRiskEdges += image.gradientRiskEdges;
+                    if (image.largestGradientRiskPixels > 0
+                        && (!largestRiskImage || image.largestGradientRiskPixels > largestRiskImage->largestGradientRiskPixels)) {
+                        largestRiskImage = &image;
+                    }
+                }
+            }
+
+            const double coverage = inputFinite > 0 ? 100.0 * pairedFinite / inputFinite : 0.0;
+            m_coverageLabel->setText(inputFinite > 0 ? QString::number(coverage, 'f', 2) + QStringLiteral("%") : QObject::tr("无有效输入像元"));
+            m_rewrapRmseLabel->setText(pairedFinite > 0 ? QString::number(std::sqrt(sumSquaredResidual / pairedFinite), 'g', 5) : QObject::tr("无有效配对像元"));
+
+            double worstP95 = 0.0;
+            for (const UnwrapImageDiagnostics& image : result.images) {
+                if (image.hasRewrapMetrics) {
+                    worstP95 = std::max(worstP95, image.rewrapP95);
+                }
+            }
+            m_rewrapP95Label->setText(pairedFinite > 0 ? QString::number(worstP95, 'g', 5) + QObject::tr("（逐幅最大估计值）") : QObject::tr("无有效配对像元"));
+            m_componentLabel->setText(totalValidComponents > 0
+                ? QObject::tr("%1（逐幅最大 %2）").arg(totalValidComponents).arg(maxValidComponents)
+                : QObject::tr("无有效输出"));
+            m_largestComponentLabel->setText(totalValidComponents > 0
+                ? QString::number(smallestLargestComponentRatio, 'f', 2) + QObject::tr("%（逐幅最小值）")
+                : QObject::tr("无有效输出"));
+            m_gradientRiskLabel->setText(gradientComparedEdges > 0
+                ? QString::number(100.0 * gradientRiskEdges / gradientComparedEdges, 'f', 4) + QObject::tr("%（候选风险）")
+                : QObject::tr("无可比较边"));
+            if (largestRiskImage) {
+                const QRect& bounds = largestRiskImage->largestGradientRiskBounds;
+                QString imageName = largestRiskImage->name;
+                if (imageName.size() > 40) {
+                    imageName = imageName.left(18) + QStringLiteral("...") + imageName.right(18);
+                }
+                m_riskRegionLabel->setText(QObject::tr("%1\nx=%2, y=%3, %4 x %5 (%6 像元)")
+                    .arg(imageName)
+                    .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())
+                    .arg(largestRiskImage->largestGradientRiskPixels));
+            } else {
+                m_riskRegionLabel->setText(QObject::tr("未发现高梯度候选区域"));
+            }
+            m_missingLabel->setText(QString::number(missingOrInvalid) + QStringLiteral(" / ") + QString::number(result.images.size()));
+
+            const bool metadataMatch = result.outputMetadataConsistent
+                && (!result.hasRecordedMethod || result.recordedMethod == result.expectedMethod)
+                && (result.expectedMethod != 4 || !result.hasRecordedThreshold || std::abs(result.recordedThreshold - result.expectedThreshold) <= 1e-9);
+            if (missingOrInvalid == 0 && metadataMatch) {
+                m_statusTitle->setText(QObject::tr("诊断完成"));
+                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
+                m_statusDesc->setText(QObject::tr("全部结果已配对。重新缠绕一致性、连通域和高梯度仅用于诊断数值完整性与候选风险，不能单独证明不存在整数周模糊。"));
+            } else {
+                m_statusTitle->setText(QObject::tr("需要复查"));
+                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
+                m_statusDesc->setText(QObject::tr("发现缺失、尺寸异常或记录参数不一致的结果。请先检查对应影像和处理日志。"));
+            }
+            watcher->deleteLater();
+        });
+        watcher->setFuture(future);
+    }
+
+    UnwrapNode* m_node = nullptr;
+    QLabel* m_coverageLabel = nullptr;
+    QLabel* m_rewrapRmseLabel = nullptr;
+    QLabel* m_rewrapP95Label = nullptr;
+    QLabel* m_componentLabel = nullptr;
+    QLabel* m_largestComponentLabel = nullptr;
+    QLabel* m_gradientRiskLabel = nullptr;
+    QLabel* m_riskRegionLabel = nullptr;
+    QLabel* m_missingLabel = nullptr;
+};
+
+} // namespace
+
+::QWidget* UnwrapNode::createValidationWidget(::QWidget* parent)
+{
+    return new UnwrapValidationWidget(this, parent);
+}
+
 } // namespace QtNodes
diff --git a/UnwrapWorker.cpp b/UnwrapWorker.cpp
index c6e2577c89087f5390e41d3baaf0ce6e07cd1880..de0d938ba8d4a2ad46205ac760ba410790da882e 100644
--- a/UnwrapWorker.cpp
+++ b/UnwrapWorker.cpp
@@ -6,6 +6,7 @@
 #include "icon_source.h"
 #include "InSARLogManager.h"
 #include <QDir>
+#include <QFile>
 #include <QThread>
 #include <QElapsedTimer>
 #include <QCoreApplication>
@@ -268,6 +269,20 @@ void UnwrapWorker::Unwrap(int method, double coherence_threshold, QString save_p
         return true;
     };
 
+    auto writeOutputPhase = [&](int idx, Mat& phase_unwrap) -> bool {
+        if (phase_unwrap.type() != CV_32F) {
+            phase_unwrap.convertTo(phase_unwrap, CV_32F);
+        }
+
+        const QString& outputPath = absolute_unwrap_path.at(idx);
+        if (!NodeUtils::writeMatToH5(outputPath, "phase", phase_unwrap)) {
+            return false;
+        }
+
+        return NodeUtils::writeScalarToH5(outputPath, "unwrap_method", method)
+            && NodeUtils::writeScalarToH5(outputPath, "unwrap_coherence_threshold", coherence_threshold);
+    };
+
     if (method == 1)
     {
         for (int i = 0; i < image_number; i++)
@@ -298,11 +313,10 @@ void UnwrapWorker::Unwrap(int method, double coherence_threshold, QString save_p
                 }
                 return;
             }
-            // 存入磁盘前重新转换回单精度 float，以保持标准存储能效并防止文件臃肿
-            if (phase_unwrap.type() != CV_32F) {
-                phase_unwrap.convertTo(phase_unwrap, CV_32F);
+            if (!writeOutputPhase(i, phase_unwrap)) {
+                QFile::remove(absolute_unwrap_path.at(i));
+                continue;
             }
-            ret = NodeUtils::writeMatToH5(absolute_unwrap_path.at(i), "phase", phase_unwrap) ? 0 : -1;
             process_success[i] = true;
         }
     }
@@ -340,11 +354,10 @@ void UnwrapWorker::Unwrap(int method, double coherence_threshold, QString save_p
                 }
                 return;
             }
-            // 存入磁盘前重新转换回单精度 float，以保持标准存储能效并防止文件臃肿
-            if (phase_unwrap.type() != CV_32F) {
-                phase_unwrap.convertTo(phase_unwrap, CV_32F);
+            if (!writeOutputPhase(i, phase_unwrap)) {
+                QFile::remove(absolute_unwrap_path.at(i));
+                continue;
             }
-            ret = NodeUtils::writeMatToH5(absolute_unwrap_path.at(i), "phase", phase_unwrap) ? 0 : -1;
             process_success[i] = true;
         }
     }
@@ -379,11 +392,10 @@ void UnwrapWorker::Unwrap(int method, double coherence_threshold, QString save_p
                 }
                 return;
             }
-            // 存入磁盘前重新转换回单精度 float，以保持标准存储能效并防止文件臃肿
-            if (phase_unwrap.type() != CV_32F) {
-                phase_unwrap.convertTo(phase_unwrap, CV_32F);
+            if (!writeOutputPhase(i, phase_unwrap)) {
+                QFile::remove(absolute_unwrap_path.at(i));
+                continue;
             }
-            ret = NodeUtils::writeMatToH5(absolute_unwrap_path.at(i), "phase", phase_unwrap) ? 0 : -1;
             process_success[i] = true;
         }
     }
@@ -419,11 +431,10 @@ void UnwrapWorker::Unwrap(int method, double coherence_threshold, QString save_p
                 }
                 return;
             }
-            // 存入磁盘前重新转换回单精度 float，以保持标准存储能效并防止文件臃肿
-            if (phase_unwrap.type() != CV_32F) {
-                phase_unwrap.convertTo(phase_unwrap, CV_32F);
+            if (!writeOutputPhase(i, phase_unwrap)) {
+                QFile::remove(absolute_unwrap_path.at(i));
+                continue;
             }
-            ret = NodeUtils::writeMatToH5(absolute_unwrap_path.at(i), "phase", phase_unwrap) ? 0 : -1;
             process_success[i] = true;
         }
     }
diff --git a/include/CoregistrationNode.h b/include/CoregistrationNode.h
index 4945541076fc693b6dbb484abce8b0141b3bc963..18f1fa85e70456de7a0400c2c33275d8cee518fc 100644
--- a/include/CoregistrationNode.h
+++ b/include/CoregistrationNode.h
@@ -43,6 +43,12 @@ public:
     void createWidget();
     QStringList previewImagePaths() const override;
 
+    bool supportsInterferometry() const override { return true; }
+    ::QWidget* createInterferometryWidget(::QWidget* parent) override;
+    QStringList getOutputPaths() const { return m_outputImagePaths; }
+    int masterIndex() const { return m_masterIndex; }
+    bool defaultFirstMaster() const { return m_defaultFirstMaster; }
+
     // Execution
     void execute() override;
     void stopExecution() override;
diff --git a/include/CoregistrationWorker.h b/include/CoregistrationWorker.h
index ff8ecc9299178f7e0d4614cbbf744e28b5652875..7b06b187f6c2d414b7aaef72b98879f23fd897d7 100644
--- a/include/CoregistrationWorker.h
+++ b/include/CoregistrationWorker.h
@@ -29,6 +29,9 @@ signals:
 
 private:
     int Registration_copy(std::vector<std::string>& SAR_images, std::vector<std::string>& SAR_images_out, cv::Mat& offset_row_out, cv::Mat& offset_col_out, int Master_index, int interp_times, int blocksize);
+    static void ResampleSlaveInverseWithAffineOffset(const ComplexMat& slave, ComplexMat& out,
+        int outputRows, int outputCols, const cv::Mat& coefRows, const cv::Mat& coefCols,
+        double offsetX, double offsetY, double scaleX, double scaleY, CoregistrationWorker* worker);
     QString resolveOutputFileName(const QString& originalName) const;
 
     QString m_demPath;
diff --git a/include/DemNode.h b/include/DemNode.h
index 703a44a5add67818573badd1923031827cd784d2..ccdabb935c0e676140f75ce8462da7abcf4d6bda 100644
--- a/include/DemNode.h
+++ b/include/DemNode.h
@@ -46,6 +46,10 @@ public:
     QJsonObject save() const override;
     void load(QJsonObject const &json) override;
 
+    bool supportsValidation() const override { return true; }
+    ::QWidget* createValidationWidget(::QWidget* parent) override;
+    std::shared_ptr<ImportedFileData> inputDataForValidation() const { return m_inputData; }
+
     // ExecutableNodeDelegateModel interface implementation
     void setExecutionMode(ExecutionMode mode) override;
 
@@ -99,6 +103,7 @@ private:
     void updateWidgetSize();
     void onMethodChanged(int index);
     QString generateDefaultOutputName() const;
+    bool commitWidgetParametersForExecution();
     void executeProcessing();
 
     // Context helpers
diff --git a/include/InterferometricFormationNode.h b/include/InterferometricFormationNode.h
index 1503c69e66b372a926c4ff7902c008b4e7fb8887..a6c7cb8b097a2cb6d74c9ccefe9b81480fce518b 100644
--- a/include/InterferometricFormationNode.h
+++ b/include/InterferometricFormationNode.h
@@ -32,6 +32,7 @@ class InterferometricFormationNode : public ExecutableNodeDelegateModel
     Q_OBJECT
 
 public:
+    friend class InterferometricFormationEvalWidget;
     InterferometricFormationNode();
     ~InterferometricFormationNode();
 
@@ -59,6 +60,8 @@ protected:
     bool validateAndRestoreOutput() override;
     QStringList previewImagePaths() const override;
     bool prepareToStart() override;
+    bool supportsInterferometry() const override { return true; }
+    ::QWidget* createInterferometryWidget(::QWidget* parent) override;
 
 private:
     ::QWidget* _widget;
diff --git a/include/QtNodes/internal/NodeDetailWindow.hpp b/include/QtNodes/internal/NodeDetailWindow.hpp
index 05bd5d16889193e2b15f485681d23d78c89edf82..8cfb0c5ac89b3fa1ad6b33199a52fc41ab7b598a 100644
--- a/include/QtNodes/internal/NodeDetailWindow.hpp
+++ b/include/QtNodes/internal/NodeDetailWindow.hpp
@@ -8,6 +8,7 @@
 #include <QtWidgets/QLabel>
 #include <QtWidgets/QPushButton>
 #include <QtWidgets/QFormLayout>
+#include <QtWidgets/QGridLayout>
 #include <QtWidgets/QScrollArea>
 #include <QtWidgets/QFrame>
 #include <QtWidgets/QTableWidget>
@@ -479,6 +480,9 @@ public:
     /// @param expected Expected value (e.g. parameter setting)
     /// @param actual Actual value (e.g. extracted from H5)
     void addComparison(const QString& name, const QString& expected, const QString& actual);
+
+    /// Add a read-only diagnostic value that does not affect parameter comparison.
+    void addDiagnostic(const QString& name, const QString& value);
     
 private:
     void applyThemeStyle();
@@ -521,7 +525,10 @@ public:
     ~BaseValidationWidget() override = default;
 
 protected:
-    void setupBaseUI(const QString& initialTitle, const QString& initialDesc, const QString& featureTitleText);
+    void setupBaseUI(const QString& initialTitle, const QString& initialDesc, const QString& featureTitleText,
+        const QString& comparisonTitleText = QString(), bool stackContentVertically = false,
+        bool scrollFeaturePanel = false);
+    QGridLayout* replaceFeatureFormWithGrid();
     
     virtual void startAsyncValidation() = 0;
     
@@ -541,6 +548,7 @@ protected:
     ValidationComparisonTable* m_compTable = nullptr;
     QFrame* m_featureCard = nullptr;
     QFormLayout* m_featureLayout = nullptr;
+    QScrollArea* m_featureScrollArea = nullptr;
     
     ValidationLoadingOverlay* m_loadingOverlay = nullptr;
     
diff --git a/include/UnwrapNode.h b/include/UnwrapNode.h
index ce15fb32e5813e75fcfabd33d3a943de556fbba1..c1c5808a4cca81d3827fad8cd7b5907b0796fd77 100644
--- a/include/UnwrapNode.h
+++ b/include/UnwrapNode.h
@@ -48,6 +48,8 @@ public:
 
     // ExecutableNodeDelegateModel interface implementation
     void setExecutionMode(ExecutionMode mode) override;
+    bool supportsValidation() const override { return true; }
+    ::QWidget* createValidationWidget(::QWidget* parent) override;
 
 protected:
     bool validateAndRestoreOutput() override;
diff --git a/scratch_interf_eval.cpp b/scratch_interf_eval.cpp
new file mode 100644
index 0000000000000000000000000000000000000000..2c144e61fd4a76ff562651ac839c052d3b9399f8
--- /dev/null
+++ b/scratch_interf_eval.cpp
@@ -0,0 +1,436 @@
+// --------------------------------------------------------------------------------
+// InterferometricFormationEvalWidget - 干涉形成质量评估选项卡组件
+// --------------------------------------------------------------------------------
+
+struct InterfEvalThreadResult {
+    bool success;
+    float meanCoh;
+    float medianCoh;
+    float maxCoh;
+    float highCohPct;
+    QString errorMessage;
+};
+
+class InterferometricFormationEvalWidget : public QWidget
+{
+    Q_OBJECT
+public:
+    explicit InterferometricFormationEvalWidget(InterferometricFormationNode* node, QWidget* parent = nullptr)
+        : QWidget(parent), m_node(node), m_hasResults(false)
+    {
+        // 界面布局
+        auto* mainLayout = new QHBoxLayout(this);
+        mainLayout->setContentsMargins(12, 12, 12, 12);
+        mainLayout->setSpacing(12);
+
+        // 左侧栏：评估参数与定量指标显示
+        auto* leftContainer = new QWidget();
+        auto* leftLayout = new QVBoxLayout(leftContainer);
+        leftLayout->setContentsMargins(0, 0, 0, 0);
+        leftLayout->setSpacing(10);
+
+        auto* selectionLayout = new QHBoxLayout();
+        auto* selLabel = new QLabel(tr("分析影像对:"));
+        selLabel->setStyleSheet("font-weight: bold;");
+        selectionLayout->addWidget(selLabel);
+
+        m_slaveCombo = new QComboBox();
+        selectionLayout->addWidget(m_slaveCombo, 1);
+        leftLayout->addLayout(selectionLayout);
+
+        // 状态评估卡片
+        m_statusCard = new QFrame();
+        m_statusCard->setFrameShape(QFrame::StyledPanel);
+        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
+        
+        auto* cardLayout = new QVBoxLayout(m_statusCard);
+        cardLayout->setContentsMargins(10, 8, 10, 8);
+        cardLayout->setSpacing(4);
+
+        m_statusCardTitle = new QLabel(tr("未评估"));
+        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
+        cardLayout->addWidget(m_statusCardTitle);
+
+        m_statusCardDesc = new QLabel(tr("请点击评估获取相干性及干涉质量诊断结果。"));
+        m_statusCardDesc->setWordWrap(true);
+        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
+        cardLayout->addWidget(m_statusCardDesc);
+
+        leftLayout->addWidget(m_statusCard);
+
+        // 定量指标统计表
+        auto* metricsFrame = new QFrame();
+        metricsFrame->setFrameShape(QFrame::StyledPanel);
+        bool isDark = NodeDetailWindow::isDarkTheme(this);
+        metricsFrame->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
+            .arg(isDark ? "#374151" : "#FFFFFF")
+            .arg(isDark ? "#4B5563" : "#E5E7EB"));
+        
+        auto* formLayout = new QFormLayout(metricsFrame);
+        formLayout->setContentsMargins(12, 12, 12, 12);
+        formLayout->setSpacing(10);
+        formLayout->setLabelAlignment(Qt::AlignLeft);
+
+        auto createValueLabel = [isDark]() {
+            auto* label = new QLabel("-");
+            label->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
+            return label;
+        };
+
+        m_meanCohLabel = createValueLabel();
+        m_medianCohLabel = createValueLabel();
+        m_maxCohLabel = createValueLabel();
+        m_highCohPctLabel = createValueLabel();
+
+        auto addFormRow = [formLayout, isDark](const QString& title, QWidget* valueWidget) {
+            auto* label = new QLabel(title);
+            label->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
+            formLayout->addRow(label, valueWidget);
+        };
+
+        addFormRow(tr("平均相干系数:"), m_meanCohLabel);
+        addFormRow(tr("中位相干系数:"), m_medianCohLabel);
+        addFormRow(tr("最高相干系数:"), m_maxCohLabel);
+        addFormRow(tr("高相干占比 (>0.5):"), m_highCohPctLabel);
+
+        leftLayout->addWidget(metricsFrame);
+
+        m_statusLabel = new QLabel(tr("准备就绪。请选择影像对开始评估。"));
+        m_statusLabel->setWordWrap(true);
+        m_statusLabel->setStyleSheet(isDark ? "color: #9CA3AF; font-size: 11px;" : "color: #6B7280; font-size: 11px;");
+        leftLayout->addWidget(m_statusLabel);
+
+        leftLayout->addStretch(1);
+        mainLayout->addWidget(leftContainer, 4);
+
+        // 右侧栏：大图显示与双模选择
+        auto* rightContainer = new QWidget();
+        auto* rightLayout = new QVBoxLayout(rightContainer);
+        rightLayout->setContentsMargins(0, 0, 0, 0);
+        rightLayout->setSpacing(8);
+
+        auto* topBarLayout = new QHBoxLayout();
+        
+        m_evalBtn = new QPushButton(tr(" 执行质量评估 "));
+        m_evalBtn->setStyleSheet(
+            "QPushButton { background-color: #3B82F6; color: white; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
+            "QPushButton:hover { background-color: #2563EB; }"
+            "QPushButton:pressed { background-color: #1D4ED8; }"
+            "QPushButton:disabled { background-color: #9CA3AF; }"
+        );
+        topBarLayout->addWidget(m_evalBtn);
+        
+        topBarLayout->addStretch(1);
+
+        auto* viewModeLabel = new QLabel(tr("视图切换:"));
+        viewModeLabel->setStyleSheet("font-weight: bold;");
+        topBarLayout->addWidget(viewModeLabel);
+
+        m_visualModeCombo = new QComboBox();
+        m_visualModeCombo->addItem(tr("干涉相位 (Phase)"));
+        m_visualModeCombo->addItem(tr("相干系数 (Coherence)"));
+        topBarLayout->addWidget(m_visualModeCombo);
+
+        rightLayout->addLayout(topBarLayout);
+
+        m_imageView = new ImageView();
+        m_imageView->setStyleSheet(QString("border: 1px solid %1; background-color: %2;")
+            .arg(isDark ? "#4B5563" : "#D1D5DB")
+            .arg(isDark ? "#111827" : "#F3F4F6"));
+        rightLayout->addWidget(m_imageView, 1);
+
+        mainLayout->addWidget(rightContainer, 6);
+
+        // 初始化数据
+        updateAvailablePairs();
+
+        connect(m_slaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InterferometricFormationEvalWidget::onPairChanged);
+        connect(m_visualModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InterferometricFormationEvalWidget::updatePreviewImage);
+        connect(m_evalBtn, &QPushButton::clicked, this, &InterferometricFormationEvalWidget::startEvaluation);
+        connect(&m_watcher, &QFutureWatcher<InterfEvalThreadResult>::finished, this, &InterferometricFormationEvalWidget::onEvaluationFinished);
+
+        // 默认触发一次
+        if (m_slaveCombo->count() > 0) {
+            onPairChanged();
+        } else {
+            m_evalBtn->setEnabled(false);
+        }
+    }
+
+    ~InterferometricFormationEvalWidget() override
+    {
+        if (m_watcher.isRunning()) {
+            m_watcher.waitForFinished();
+        }
+    }
+
+private:
+    void updateAvailablePairs()
+    {
+        m_slaveCombo->blockSignals(true);
+        m_slaveCombo->clear();
+        m_h5PathsPhase.clear();
+        m_h5PathsCoh.clear();
+        m_jpgPathsPhase.clear();
+        m_jpgPathsCoh.clear();
+
+        QStringList previews = m_node->previewImagePaths();
+        for (const QString& jpgPath : previews) {
+            if (jpgPath.endsWith("_phase.jpg")) {
+                m_jpgPathsPhase.append(jpgPath);
+                QString h5Path = jpgPath;
+                h5Path.replace("_phase.jpg", ".h5");
+                m_h5PathsPhase.append(h5Path);
+            } else if (jpgPath.endsWith("_coh.jpg")) {
+                m_jpgPathsCoh.append(jpgPath);
+                QString h5Path = jpgPath;
+                h5Path.replace("_coh.jpg", ".h5");
+                m_h5PathsCoh.append(h5Path);
+            }
+        }
+
+        if (m_jpgPathsPhase.isEmpty()) {
+            m_slaveCombo->addItem(tr("无干涉对"));
+        } else {
+            for (const QString& jpg : m_jpgPathsPhase) {
+                QString name = QFileInfo(jpg).baseName();
+                name.replace("_phase", "");
+                m_slaveCombo->addItem(name);
+            }
+        }
+        m_slaveCombo->blockSignals(false);
+    }
+
+    void onPairChanged()
+    {
+        m_hasResults = false;
+        resetMetrics();
+        updatePreviewImage();
+    }
+
+    void updatePreviewImage()
+    {
+        int pairIdx = m_slaveCombo->currentIndex();
+        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
+            m_imageView->setImage(QImage());
+            return;
+        }
+
+        int viewMode = m_visualModeCombo->currentIndex();
+        QString pathToLoad;
+
+        if (viewMode == 0) { // Phase
+            pathToLoad = m_jpgPathsPhase[pairIdx];
+        } else { // Coherence
+            // 查找对应的相干系数图
+            QString baseName = QFileInfo(m_jpgPathsPhase[pairIdx]).baseName();
+            baseName.replace("_phase", "_coh");
+            for (const QString& cohPath : m_jpgPathsCoh) {
+                if (QFileInfo(cohPath).baseName() == baseName) {
+                    pathToLoad = cohPath;
+                    break;
+                }
+            }
+        }
+
+        if (!pathToLoad.isEmpty() && QFile::exists(pathToLoad)) {
+            QImage img(pathToLoad);
+            m_imageView->setImage(img);
+        } else {
+            m_imageView->setImage(QImage());
+        }
+    }
+
+    void resetMetrics()
+    {
+        m_meanCohLabel->setText("-");
+        m_medianCohLabel->setText("-");
+        m_maxCohLabel->setText("-");
+        m_highCohPctLabel->setText("-");
+        
+        m_statusCardTitle->setText(tr("未评估"));
+        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
+        m_statusCardDesc->setText(tr("请点击评估获取相干性及干涉质量诊断结果。"));
+        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
+        
+        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
+        m_statusLabel->setText(tr("准备就绪。请点击执行评估。"));
+    }
+
+    void startEvaluation()
+    {
+        int pairIdx = m_slaveCombo->currentIndex();
+        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
+            return;
+        }
+
+        // 查找对应的相干系数文件
+        QString baseName = QFileInfo(m_jpgPathsPhase[pairIdx]).baseName();
+        baseName.replace("_phase", "_coh");
+        QString cohH5Path;
+        for (const QString& cohPath : m_h5PathsCoh) {
+            if (QFileInfo(cohPath).baseName() == baseName) {
+                cohH5Path = cohPath;
+                break;
+            }
+        }
+
+        if (cohH5Path.isEmpty() || !QFile::exists(cohH5Path)) {
+            m_statusLabel->setText(tr("无法评估：未找到该干涉对的相干系数成果文件。请检查节点是否勾选了“计算相干系数”。"));
+            return;
+        }
+
+        m_evalBtn->setEnabled(false);
+        m_slaveCombo->setEnabled(false);
+        m_statusLabel->setText(tr("正在读取 H5 文件并计算相干性统计信息，请稍候..."));
+
+        QFuture<InterfEvalThreadResult> future = QtConcurrent::run([cohH5Path]() {
+            InterfEvalThreadResult res;
+            res.success = false;
+            res.meanCoh = 0.0f;
+            res.medianCoh = 0.0f;
+            res.maxCoh = 0.0f;
+            res.highCohPct = 0.0f;
+
+            cv::Mat cohMat;
+            QString errMsg;
+            if (!NodeUtils::readMatFromH5(cohH5Path, "coherence", cohMat, CV_32F, &errMsg)) {
+                res.errorMessage = QStringLiteral("读取相干系数数据集失败: %1").arg(errMsg);
+                return res;
+            }
+
+            if (cohMat.empty()) {
+                res.errorMessage = QStringLiteral("相干系数矩阵为空。");
+                return res;
+            }
+
+            // 计算统计信息
+            // 为了计算中位数和占比，需要遍历所有有效像素
+            std::vector<float> validPixels;
+            // 预估大小，如果是超大矩阵，可以隔行采样
+            int step = 1;
+            if (cohMat.total() > 5000000) {
+                step = qMax(1, (int)(cohMat.total() / 5000000));
+            }
+            
+            float maxCoh = 0.0f;
+            double sumCoh = 0.0;
+            int highCount = 0;
+
+            if (cohMat.isContinuous()) {
+                const float* ptr = cohMat.ptr<float>();
+                int total = cohMat.total();
+                for (int i = 0; i < total; i += step) {
+                    float val = ptr[i];
+                    if (!std::isnan(val) && val >= 0.0f && val <= 1.0f) {
+                        validPixels.push_back(val);
+                        sumCoh += val;
+                        if (val > maxCoh) maxCoh = val;
+                        if (val > 0.5f) highCount++;
+                    }
+                }
+            } else {
+                for (int r = 0; r < cohMat.rows; r += step) {
+                    const float* ptr = cohMat.ptr<float>(r);
+                    for (int c = 0; c < cohMat.cols; c += step) {
+                        float val = ptr[c];
+                        if (!std::isnan(val) && val >= 0.0f && val <= 1.0f) {
+                            validPixels.push_back(val);
+                            sumCoh += val;
+                            if (val > maxCoh) maxCoh = val;
+                            if (val > 0.5f) highCount++;
+                        }
+                    }
+                }
+            }
+
+            if (validPixels.empty()) {
+                res.errorMessage = QStringLiteral("相干系数矩阵中没有有效数据。");
+                return res;
+            }
+
+            res.meanCoh = sumCoh / validPixels.size();
+            res.maxCoh = maxCoh;
+            res.highCohPct = (float)highCount / validPixels.size() * 100.0f;
+
+            size_t n = validPixels.size() / 2;
+            std::nth_element(validPixels.begin(), validPixels.begin() + n, validPixels.end());
+            res.medianCoh = validPixels[n];
+
+            res.success = true;
+            return res;
+        });
+
+        m_watcher.setFuture(future);
+    }
+
+    void onEvaluationFinished()
+    {
+        m_evalBtn->setEnabled(true);
+        m_slaveCombo->setEnabled(true);
+
+        InterfEvalThreadResult res = m_watcher.result();
+        if (!res.success) {
+            m_statusLabel->setText(tr("评估失败：%1").arg(res.errorMessage));
+            return;
+        }
+
+        m_hasResults = true;
+        m_statusLabel->setText(tr("评估完成。"));
+
+        m_meanCohLabel->setText(QString::number(res.meanCoh, 'f', 4));
+        m_medianCohLabel->setText(QString::number(res.medianCoh, 'f', 4));
+        m_maxCohLabel->setText(QString::number(res.maxCoh, 'f', 4));
+        m_highCohPctLabel->setText(QString("%1 %").arg(res.highCohPct, 0, 'f', 2));
+
+        // 根据经验阈值更新诊断卡片
+        if (res.meanCoh > 0.4f && res.highCohPct > 30.0f) {
+            m_statusCardTitle->setText(tr("干涉质量良好"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #059669;"); // 绿色
+            m_statusCardDesc->setText(tr("整体相干性较高，干涉条纹预期清晰。符合后续相位解缠和形变提取要求。"));
+            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #10B981;");
+            m_statusCard->setStyleSheet("background-color: rgba(16, 185, 129, 0.1); border: 1px solid rgba(16, 185, 129, 0.3); border-radius: 4px;");
+        } else if (res.meanCoh >= 0.2f) {
+            m_statusCardTitle->setText(tr("相干性一般"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #D97706;"); // 橙色
+            m_statusCardDesc->setText(tr("存在一定的去相干（可能受植被覆盖、较长基线或时间跨度影响）。建议在后续节点适当增加滤波强度。"));
+            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #F59E0B;");
+            m_statusCard->setStyleSheet("background-color: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.3); border-radius: 4px;");
+        } else {
+            m_statusCardTitle->setText(tr("严重去相干"));
+            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #DC2626;"); // 红色
+            m_statusCardDesc->setText(tr("整体相干性极低，干涉相位可能完全被噪声掩盖。请检查输入影像的时空基线，或确保前置配准精度达标。"));
+            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #EF4444;");
+            m_statusCard->setStyleSheet("background-color: rgba(239, 68, 68, 0.1); border: 1px solid rgba(239, 68, 68, 0.3); border-radius: 4px;");
+        }
+    }
+
+    InterferometricFormationNode* m_node;
+    QComboBox* m_slaveCombo;
+    QComboBox* m_visualModeCombo;
+    QPushButton* m_evalBtn;
+    ImageView* m_imageView;
+
+    QFrame* m_statusCard;
+    QLabel* m_statusCardTitle;
+    QLabel* m_statusCardDesc;
+
+    QLabel* m_meanCohLabel;
+    QLabel* m_medianCohLabel;
+    QLabel* m_maxCohLabel;
+    QLabel* m_highCohPctLabel;
+    QLabel* m_statusLabel;
+
+    QStringList m_h5PathsPhase;
+    QStringList m_h5PathsCoh;
+    QStringList m_jpgPathsPhase;
+    QStringList m_jpgPathsCoh;
+
+    bool m_hasResults;
+    QFutureWatcher<InterfEvalThreadResult> m_watcher;
+};
+
+::QWidget* InterferometricFormationNode::createInterferometryWidget(::QWidget* parent)
+{
+    return new InterferometricFormationEvalWidget(this, parent);
+}
