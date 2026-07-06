#include "include/OrbitRefinementNode.h"
#include "include/IApplicationInterface.h"
#include "include/WorkspaceUI.h"
#include "include/InterfaceManager.h"
#include "MainWindow.h"
#include "include/NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QFormLayout>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QTimer>
#include <QApplication>
#include <opencv2/opencv.hpp>
#include "tinyxml.h"

namespace QtNodes {

// 辅助函数：绘制控制点残差分布图并保存为 JPG
static void drawGcpResidualsPlot(const std::vector<GCPPoint>& gcps, double rmsRange, double rmsAzimuth, const QString& outputPath) {
    // 创建白色背景图像 (600x800, 3通道)
    cv::Mat plot = cv::Mat(600, 800, CV_8UC3, cv::Scalar(255, 255, 255));
    
    // 绘制标题
    cv::putText(plot, "GCP Residuals Distribution", cv::Point(50, 40), 
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 0), 2);
                
    // 绘制 RMS 及统计信息
    std::string rmsText = cv::format("RMS Range: %.4f m, RMS Azimuth: %.4f m (Points: %d)", 
                                     rmsRange, rmsAzimuth, (int)gcps.size());
    cv::putText(plot, rmsText, cv::Point(50, 70), 
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(100, 100, 100), 1);
                
    if (gcps.empty()) {
        cv::imwrite(outputPath.toStdString(), plot);
        return;
    }

    // 寻找控制点行列号的最大最小值以进行等比例缩放
    double minRow = 1e9, maxRow = -1e9;
    double minCol = 1e9, maxCol = -1e9;
    for (const auto& gcp : gcps) {
        if (gcp.row < minRow) minRow = gcp.row;
        if (gcp.row > maxRow) maxRow = gcp.row;
        if (gcp.col < minCol) minCol = gcp.col;
        if (gcp.col > maxCol) maxCol = gcp.col;
    }
    
    double rowSpan = maxRow - minRow;
    double colSpan = maxCol - minCol;
    if (rowSpan < 1.0) rowSpan = 1.0;
    if (colSpan < 1.0) colSpan = 1.0;
    
    // 绘图区边界限制
    int marginX = 100;
    int marginY = 120;
    int plotW = 800 - 2 * marginX;
    int plotH = 600 - 2 * marginY;
    
    // 绘制灰色外框
    cv::rectangle(plot, cv::Rect(marginX, marginY, plotW, plotH), cv::Scalar(220, 220, 220), 1);
    
    // 循环绘制每个 GCP 的物理位置与残差向量
    for (const auto& gcp : gcps) {
        // 映射坐标至绘图像素区
        int px = marginX + (int)((gcp.col - minCol) / colSpan * plotW);
        int py = marginY + (int)((gcp.row - minRow) / rowSpan * plotH);
        
        // 绘制控制点中心（绿色实心圆）
        cv::circle(plot, cv::Point(px, py), 4, cv::Scalar(0, 180, 0), -1);
        
        // 绘制残差向量线段（红色，放大50倍进行可视化展示）
        if (!std::isnan(gcp.residual_range) && !std::isnan(gcp.residual_azimuth)) {
            int vx = (int)(gcp.residual_range * 50.0);
            int vy = (int)(gcp.residual_azimuth * 50.0);
            
            // 向量长度超界截断保护
            if (vx > 100) vx = 100;
            if (vx < -100) vx = -100;
            if (vy > 100) vy = 100;
            if (vy < -100) vy = -100;
            
            cv::line(plot, cv::Point(px, py), cv::Point(px + vx, py + vy), cv::Scalar(0, 0, 240), 1);
            cv::circle(plot, cv::Point(px + vx, py + vy), 2, cv::Scalar(0, 0, 240), -1); // 箭头端点
        }
    }
    
    // 绘制向量比例图例
    cv::line(plot, cv::Point(marginX, 550), cv::Point(marginX + 50, 550), cv::Scalar(0, 0, 240), 1);
    cv::putText(plot, "50 px = 1.0 m", cv::Point(marginX + 60, 555), 
                cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(100, 100, 100), 1);
    
    cv::imwrite(outputPath.toStdString(), plot);
}

OrbitRefinementNode::OrbitRefinementNode()
    : _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_masterIndexSpin(nullptr)
    , m_polyDegreeCombo(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_statusLabel(nullptr)
    , m_masterIndex(1)
    , m_polyDegree(2)
    , m_outputNodeName("OrbitRefined")
    , m_db(new GCPDatabase(this))
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    createWidget();
    setState(ExecutionState::Pending);
}

OrbitRefinementNode::~OrbitRefinementNode()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

unsigned int OrbitRefinementNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 1;
    if (portType == PortType::Out) return 2;
    return 0;
}

NodeDataType OrbitRefinementNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return ImportedFileData().type();
    }
    if (portType == PortType::Out) {
        if (portIndex == 0) return ImportedFileData().type();
        if (portIndex == 1) return ImageInfoData().type();
    }
    return NodeDataType();
}

std::shared_ptr<NodeData> OrbitRefinementNode::outData(PortIndex port)
{
    if (port == 0) return m_outputData;
    if (port == 1) return m_imageInfoData;
    return nullptr;
}

void OrbitRefinementNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData) {
        m_inputNodeLabel->setText(m_inputData->nodeName());
        initDatabase();
        updateLabels();
        
        if (executionState() == ExecutionState::Pending || executionState() == ExecutionState::Idle) {
            setState(ExecutionState::Idle);
        }
    } else {
        m_inputNodeLabel->setText(QStringLiteral("等待输入"));
        setState(ExecutionState::Pending);
        invalidateExecution();
        m_outputData.reset();
        m_imageInfoData.reset();
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
    }
}

bool OrbitRefinementNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return true;
}

QString OrbitRefinementNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入影像 (H5)");
    }
    if (portType == PortType::Out) {
        if (portIndex == 0) return QStringLiteral("精炼影像 (H5)");
        if (portIndex == 1) return QStringLiteral("残差分布图 (JPG)");
    }
    return QString();
}

bool OrbitRefinementNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1) {
        return true; // 预览图端口对下游核心处理而言是可选的
    }
    return false;
}

::QWidget* OrbitRefinementNode::embeddedWidget()
{
    return _widget;
}

QJsonObject OrbitRefinementNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["masterIndex"] = m_masterIndex;
    root["polyDegree"] = m_polyDegree;
    root["outputNodeName"] = m_outputNodeName;
    return root;
}

void OrbitRefinementNode::load(QJsonObject const &json)
{
    // 严格遵循加载顺序控制：先解析特有参数，再调用基类 load() (SOP 15)
    m_masterIndex = json["masterIndex"].toInt(1);
    m_polyDegree = json["polyDegree"].toInt(2);
    m_outputNodeName = json["outputNodeName"].toString(QStringLiteral("OrbitRefined"));

    ExecutableNodeDelegateModel::load(json);

    updateLabels();
    updateWidgetSize();
}

void OrbitRefinementNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

QStringList OrbitRefinementNode::previewImagePaths() const
{
    // 快速扫描，仅执行本地文件存在性检查，不得在 UI 线程同步读取 H5
    QStringList paths;
    QString dstNode = m_outputNodeName.trimmed();
    if (!dstNode.isEmpty()) {
        QString outputPath = projectPath() + "/" + dstNode + "/";
        QString residualJpg = outputPath + "gcp_residuals_" + QString::number(_nodeId) + ".jpg";
        if (QFile::exists(residualJpg)) {
            paths.append(residualJpg);
        }
    }
    return paths;
}

bool OrbitRefinementNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return false;

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (!dir.exists()) return false;

    QStringList filters;
    filters << "*.h5";
    QStringList h5Files = dir.entryList(filters, QDir::Files);
    if (h5Files.isEmpty()) return false;

    // 1. 恢复 Port 0 轨道精炼后的影像绝对文件列表
    QStringList h5Paths;
    for (const QString& file : h5Files) {
        h5Paths.append(dir.absoluteFilePath(file));
    }
    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    // 2. 恢复 Port 1 的残差图预览 (沙盒化 ID 文件名)
    QString residualJpg = outputPath + "gcp_residuals_" + QString::number(_nodeId) + ".jpg";
    if (QFile::exists(residualJpg)) {
        m_imageInfoData = std::make_shared<ImageInfoData>(QStringList() << residualJpg);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    } else {
        // 若残差图已丢失，在工程加载复原时仅进行数据端口清空，
        // 在 UI 状态栏展示提示：“残差图已丢失，重新运行以计算生成”，避免异步加载卡顿 (SOP 7)
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        Q_EMIT dataUpdated(1);
    }

    // 3. 自愈重建左侧项目树节点
    QStandardItemModel* model = projectModel();
    if (model) {
        QStandardItem* projectItem = model->findItems(projectName())[0];
        if (projectItem) {
            QStandardItem* refNodeItem = nullptr;
            for (int i = 0; i < projectItem->rowCount(); i++) {
                if (projectItem->child(i, 0)->text() == dstNode) {
                    refNodeItem = projectItem->child(i, 0);
                    break;
                }
            }

            if (!refNodeItem) {
                refNodeItem = new QStandardItem(dstNode);
                refNodeItem->setIcon(QIcon(FOLDER_ICON));
                projectItem->appendRow(refNodeItem);
                
                // 从上游复制 Rank 等级
                QString sensorTag = "complex-1.0";
                QString srcNode = m_inputData ? m_inputData->nodeName() : "";
                for (int i = 0; i < projectItem->rowCount(); i++) {
                    if (projectItem->child(i, 0)->text() == srcNode) {
                        sensorTag = projectItem->child(i, 1)->text();
                        break;
                    }
                }
                projectItem->setChild(projectItem->rowCount() - 1, 1, new QStandardItem(sensorTag));
            }

            for (const QString& file : h5Files) {
                QFileInfo fileinfo(dir.absoluteFilePath(file));
                QString imgName = fileinfo.baseName();
                QStandardItem* imgItem = nullptr;
                for (int j = 0; j < refNodeItem->rowCount(); j++) {
                    if (refNodeItem->child(j, 0)->text() == imgName) {
                        imgItem = refNodeItem->child(j, 0);
                        break;
                    }
                }

                if (!imgItem) {
                    QStandardItem* childName = new QStandardItem(imgName);
                    childName->setIcon(QIcon(IMAGEDATA_ICON));
                    childName->setToolTip("complex");
                    QStandardItem* childPath = new QStandardItem(fileinfo.absoluteFilePath());
                    refNodeItem->appendRow(childName);
                    refNodeItem->setChild(refNodeItem->rowCount() - 1, 1, childPath);
                }
            }
        }
    }

    return true;
}

void OrbitRefinementNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // 严格锁定 Widget 宽度规范 (SOP 3)

    QVBoxLayout* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    QFormLayout* form = new QFormLayout();
    form->setSpacing(6);

    auto addFormRow = [form](const QString& labelText, QWidget* field) {
        QLabel* label = new QLabel(labelText);
        label->setFixedWidth(80); // 固定标签宽度，纵向完美对齐 (SOP 10)
        form->addRow(label, field);
    };

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        int outCount = nPorts(PortType::Out);
        for (int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene); // 触发项目脏状态 (SOP 23)
        }
    };

    // 1. 上游影像
    m_inputNodeLabel = new QLabel(QStringLiteral("等待输入"));
    addFormRow(QStringLiteral("上游影像:"), m_inputNodeLabel);

    // 2. 参考影像
    m_masterIndexSpin = new QSpinBox();
    m_masterIndexSpin->setRange(1, 99);
    m_masterIndexSpin->setValue(m_masterIndex);
    connect(m_masterIndexSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, invalidateNodeData](int val) {
        if (m_masterIndex != val) {
            m_masterIndex = val;
            invalidateNodeData();
        }
    });
    addFormRow(QStringLiteral("参考影像:"), m_masterIndexSpin);

    // 3. 多项式阶数
    m_polyDegreeCombo = new QComboBox();
    m_polyDegreeCombo->addItem(QStringLiteral("1 阶 (线性)"), 1);
    m_polyDegreeCombo->addItem(QStringLiteral("2 阶 (二次)"), 2);
    m_polyDegreeCombo->setCurrentIndex(m_polyDegree - 1);
    connect(m_polyDegreeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        int val = m_polyDegreeCombo->itemData(index).toInt();
        if (m_polyDegree != val) {
            m_polyDegree = val;
            invalidateNodeData();
        }
    });
    addFormRow(QStringLiteral("多项式阶:"), m_polyDegreeCombo);

    // 4. 目标节点名
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入")); // 占位符对齐规范 (SOP 11)
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    addFormRow(QStringLiteral("目标节点:"), m_outputNodeNameEdit);

    layout->addLayout(form);

    // 5. 状态及参数提示
    m_statusLabel = new QLabel(QStringLiteral("状态：等待计算"));
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // Widget创建的最尾部强制进行一次自愈刷新，填充数据 (SOP 12)
    updateLabels();
}

void OrbitRefinementNode::updateLabels()
{
    if (!m_inputData) {
        m_inputNodeLabel->setText(QStringLiteral("等待输入"));
        m_statusLabel->setText(QStringLiteral("状态：等待输入连线"));
        return;
    }

    m_inputNodeLabel->setText(m_inputData->nodeName());
    
    // 如果没有计算过，检查数据库中的点数
    if (executionState() != ExecutionState::Running) {
        initDatabase();
        if (m_db->isOpen()) {
            int count = m_db->getAnnotatedCount();
            m_statusLabel->setText(QStringLiteral("有效已标注控制点数：%1 个").arg(count));
        } else {
            m_statusLabel->setText(QStringLiteral("状态：等待计算"));
        }
    }

    updateWidgetSize();
}

void OrbitRefinementNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated(); // 发射框架重绘信号 (SOP 18)
    }
}

void OrbitRefinementNode::initDatabase()
{
    QString xmlPath = projectPath() + "/" + projectName();
    QString projBaseName = QFileInfo(xmlPath).baseName();
    QString dbPath = projectPath() + "/" + projBaseName + "_gcp.db";

    if (!m_db->isOpen() || m_db->databasePath() != dbPath) {
        m_db->open(dbPath);
    }
}

void OrbitRefinementNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void OrbitRefinementNode::onProcessingFinished()
{
    QString dstNode = m_outputNodeNameEdit->text().trimmed();
    if (dstNode.isEmpty()) dstNode = generateDefaultOutputName();
    QString outputPath = projectPath() + "/" + dstNode + "/";

    // 1. 扫描输出目录，构建输出影像数据对象 (Port 0)
    QStringList h5Paths;
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);
        h5Files.sort();
        for (const QString& file : h5Files) {
            h5Paths.append(dir.absoluteFilePath(file));
        }
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    // 2. 从数据库读取点信息并离线绘制残差图 (Port 1)
    initDatabase();
    if (m_db->isOpen() && !h5Paths.isEmpty()) {
        std::vector<GCPPoint> gcps = m_db->getAnnotatedGCPs();
        
        // 读取 H5 元数据提取最终计算出的 RMS 残差值
        double rmsRange = 0.0, rmsAzimuth = 0.0;
        FormatConversion FC;
        FC.read_double_from_h5(h5Paths.at(0).toStdString().c_str(), "orbit_refinement_rms_range", &rmsRange);
        FC.read_double_from_h5(h5Paths.at(0).toStdString().c_str(), "orbit_refinement_rms_azimuth", &rmsAzimuth);
        
        QString residualJpg = outputPath + "gcp_residuals_" + QString::number(_nodeId) + ".jpg";
        drawGcpResidualsPlot(gcps, rmsRange, rmsAzimuth, residualJpg);

        if (QFile::exists(residualJpg)) {
            m_imageInfoData = std::make_shared<ImageInfoData>(QStringList() << residualJpg);
            setOutputData(1, m_imageInfoData);
        } else {
            m_imageInfoData.reset();
            setOutputData(1, nullptr);
        }
    }

    // 3. 清理线程与 Worker
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    // 开启 GUI 参数编辑
    m_outputNodeNameEdit->setEnabled(true);
    m_masterIndexSpin->setEnabled(true);
    m_polyDegreeCombo->setEnabled(true);

    setState(ExecutionState::Running);
    setProgress(100);
    InSARLogManager::LogInfo("OrbitRefinementNode", "轨道精炼计算及绘图完成。");
    
    finishExecution();
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    
    // 更新标签显示
    updateLabels();
}

void OrbitRefinementNode::onError(const QString& error)
{
    InSARLogManager::LogError("OrbitRefinementNode", "Worker 执行出错：" + error);
    m_statusLabel->setText(QStringLiteral("计算出错：%1").arg(error));

    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    m_outputNodeNameEdit->setEnabled(true);
    m_masterIndexSpin->setEnabled(true);
    m_polyDegreeCombo->setEnabled(true);

    setState(ExecutionState::Error);
}

bool OrbitRefinementNode::validateInputs() const
{
    if (!m_inputData) return false;
    QString srcNode = m_inputData->nodeName();
    if (srcNode.isEmpty()) return false;
    
    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstNode.isEmpty()) return false;

    return true;
}

QString OrbitRefinementNode::generateDefaultOutputName() const
{
    if (m_inputData) {
        return m_inputData->nodeName() + "_Refined";
    }
    return "OrbitRefined";
}

bool OrbitRefinementNode::prepareToStart()
{
    if (!validateInputs()) {
        return false;
    }

    QString dstNode = m_outputNodeNameEdit->text().trimmed();
    if (dstNode.isEmpty()) dstNode = generateDefaultOutputName();
    m_preparedDstNode = dstNode;

    QString savePath = projectPath();

    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString outputPath = savePath + "/" + dstNode + "/";
    QDir outputDir(outputPath);
    if (outputDir.exists()) {
        QStringList filters;
        filters << "*.h5";
        QStringList existingH5 = outputDir.entryList(filters, QDir::Files);
        if (!existingH5.isEmpty()) {
            if (_isAutoTriggered) {
                m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
            } else {
                m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
                    NodeUtils::getProjectContext(_widget),
                    caption(),
                    existingH5,
                    nullptr
                );
            }
        }
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void OrbitRefinementNode::executeProcessing()
{
    InSARLogManager::LogInfo("OrbitRefinementNode", "executeProcessing 开始。");

    QString srcNode = m_inputData->nodeName();
    QString dstNode = m_preparedDstNode;
    m_outputNodeName = dstNode;

    QString savePath = projectPath();
    QString projName = projectName();

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (validateAndRestoreOutput()) {
            setState(ExecutionState::Running);
            setProgress(100);
            finishExecution();
            return;
        } else {
            setState(ExecutionState::Error);
            return;
        }
    }

    setProgress(0);

    // 2. 覆盖运行前，清理工程 XML 的旧记录和左侧树视图以避影分身 (SOP 14)
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode);

    // 3. 创建线程与 Worker
    m_thread = new QThread();
    m_worker = new OrbitRefinementWorker();
    m_worker->moveToThread(m_thread);

    // 4. 连接信号槽
    connect(m_thread, &QThread::started, [this, savePath, projName, dstNode]() {
        initDatabase();
        m_worker->refine_orbit(
            savePath + "/" + projName,
            projName,
            dstNode,
            m_inputData->filePaths(),
            m_db->databasePath(),
            m_masterIndex,
            m_polyDegree
        );
    });

    connect(m_worker, &OrbitRefinementWorker::updateProcess, this, &OrbitRefinementNode::onProgressUpdate);
    connect(m_worker, &OrbitRefinementWorker::endProcess, this, &OrbitRefinementNode::onProcessingFinished);
    connect(m_worker, &OrbitRefinementWorker::errorProcess, this, &OrbitRefinementNode::onError);
    
    // 自愈刷新项目树
    connect(m_worker, &OrbitRefinementWorker::sendModel, this, &OrbitRefinementNode::onModelUpdated);
    
    // 采用复用全局 XML 句柄 + 原生 TinyXML 写入以避崩溃 (SOP 9)
    connect(m_worker, &OrbitRefinementWorker::sendResults, this, &OrbitRefinementNode::onResultsReceived);

    connect(m_worker, &OrbitRefinementWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // 启动线程
    m_thread->start();
    
    // 锁定界面参数修改
    m_outputNodeNameEdit->setEnabled(false);
    m_masterIndexSpin->setEnabled(false);
    m_polyDegreeCombo->setEnabled(false);

    // QTimer::singleShot 强行把状态设回 Running，规避基类 setInData 复写 (SOP 5)
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) {
            setState(ExecutionState::Running);
        }
    });
}

void OrbitRefinementNode::onResultsReceived(
    const QString& dstNode,
    const QStringList& h5Paths,
    const QStringList& originNames
)
{
    XMLFile* xml = projectXml();
    if (!xml) {
        InSARLogManager::LogError("OrbitRefinementNode", "onResultsReceived: projectXml() 为空，跳过 XML 写入。");
        return;
    }

    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (!root) {
        InSARLogManager::LogError("OrbitRefinementNode", "onResultsReceived: XML 根节点为空，跳过 XML 写入。");
        return;
    }

    // 从上游 DataNode 查询传感器类型和级别
    QString srcNode = m_inputData ? m_inputData->nodeName() : "";
    QString sensor = "unknown";
    QString srcRank = "complex-1.0";
    for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
        const char* nameAttr = p->Attribute("name");
        if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == srcNode) {
            const char* rankAttr = p->Attribute("rank");
            if (rankAttr) srcRank = QString(rankAttr);
            
            TiXmlElement* img = p->FirstChildElement("image");
            if (img && img->Attribute("sensor")) {
                sensor = img->Attribute("sensor");
            }
            break;
        }
    }

    bool xmlModified = false;
    for (int i = 0; i < h5Paths.size(); i++) {
        QFileInfo fileinfo(h5Paths.at(i));
        QString relativePath = QString("/%1/%2").arg(dstNode).arg(fileinfo.fileName());

        // 查找或新建 DataNode
        TiXmlElement* dataNodeElem = nullptr;
        for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
            const char* nameAttr = p->Attribute("name");
            if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
                dataNodeElem = p;
                break;
            }
        }

        if (!dataNodeElem) {
            // 新建 DataNode
            dataNodeElem = new TiXmlElement("DataNode");
            dataNodeElem->SetAttribute("name", dstNode.toStdString().c_str());
            dataNodeElem->SetAttribute("data_count", "1");
            dataNodeElem->SetAttribute("data_processing", "OrbitRefinement");
            dataNodeElem->SetAttribute("rank", srcRank.toStdString().c_str());

            int index = 1;
            TiXmlElement* root_child = root->FirstChildElement();
            if (root_child) root_child = root_child->NextSiblingElement(); // 跳过 project_info

            TiXmlElement* insertBeforeNode = nullptr;
            for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++) {
                const char* rankAttr = p->Attribute("rank");
                if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 ||
                                 strcmp(rankAttr, "complex-1.0") == 0 ||
                                 strcmp(rankAttr, "complex-2.0") == 0 ||
                                 strcmp(rankAttr, "complex-3.0") == 0)) {
                    continue;
                } else {
                    insertBeforeNode = p;
                    break;
                }
            }
            dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

            TiXmlElement* dataElem = new TiXmlElement("Data");
            TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
            dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
            dataElem->LinkEndChild(dataNameNode);
            TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
            dataRankNode->LinkEndChild(new TiXmlText(srcRank.toStdString().c_str()));
            dataElem->LinkEndChild(dataRankNode);
            TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
            dataIndexNode->LinkEndChild(new TiXmlText("1"));
            dataElem->LinkEndChild(dataIndexNode);
            TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
            dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
            dataElem->LinkEndChild(dataPathNode);
            dataNodeElem->LinkEndChild(dataElem);

            TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");
            TiXmlElement* masterImageElem = new TiXmlElement("master_image");
            masterImageElem->LinkEndChild(new TiXmlText(QString::number(m_masterIndex).toStdString().c_str()));
            paramsElem->LinkEndChild(masterImageElem);
            
            TiXmlElement* polyDegreeElem = new TiXmlElement("poly_degree");
            polyDegreeElem->LinkEndChild(new TiXmlText(QString::number(m_polyDegree).toStdString().c_str()));
            paramsElem->LinkEndChild(polyDegreeElem);

            dataNodeElem->LinkEndChild(paramsElem);

            if (insertBeforeNode) {
                root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                delete dataNodeElem;
                for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement()) {
                    index++;
                    p->SetAttribute("index", QString::number(index).toStdString().c_str());
                }
            } else {
                root->LinkEndChild(dataNodeElem);
            }
            xmlModified = true;
        } else {
            // DataNode 已存在，追加 Data 子节点
            const char* countAttr = dataNodeElem->Attribute("data_count");
            int count = countAttr ? QString(countAttr).toInt() : 0;
            count++;
            dataNodeElem->SetAttribute("data_count", QString::number(count).toStdString().c_str());

            TiXmlElement* lastChildNode = dataNodeElem->LastChild() ? dataNodeElem->LastChild()->ToElement() : nullptr;

            TiXmlElement* dataElem = new TiXmlElement("Data");
            dataElem->SetAttribute("sensor", sensor.toStdString().c_str());
            TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
            dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
            dataElem->LinkEndChild(dataNameNode);
            TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
            dataRankNode->LinkEndChild(new TiXmlText(srcRank.toStdString().c_str()));
            dataElem->LinkEndChild(dataRankNode);
            TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
            dataIndexNode->LinkEndChild(new TiXmlText(QString::number(count).toStdString().c_str()));
            dataElem->LinkEndChild(dataIndexNode);
            TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
            dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
            dataElem->LinkEndChild(dataPathNode);

            if (lastChildNode) {
                dataNodeElem->InsertBeforeChild(lastChildNode, *dataElem);
                delete dataElem;
            } else {
                dataNodeElem->LinkEndChild(dataElem);
            }
            xmlModified = true;
        }
    }

    if (xmlModified) {
        QString xmlPath = projectPath() + "/" + projectName();
        xml->XMLFile_save(xmlPath.toStdString().c_str());
        InSARLogManager::LogInfo("OrbitRefinementNode", "onResultsReceived: XML 成功通过全局句柄保存。");
    }

    // 触发项目树刷新
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

void OrbitRefinementNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* OrbitRefinementNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString OrbitRefinementNode::projectPath() const
{
    // 多级回溯，防止 Widget 未挂载早期时 Context 为空
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

    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString OrbitRefinementNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* OrbitRefinementNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void OrbitRefinementNode::execute()
{
    executeProcessing();
}

void OrbitRefinementNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void OrbitRefinementNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

} // namespace QtNodes
