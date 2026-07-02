// GCPManagerNode.cpp
#include "include/GCPManagerNode.h"
#include "include/GCPAnnotationWidget.h"
#include "include/NodeUtils.h"
#include "include/IApplicationInterface.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QTimer>

namespace QtNodes {

GCPManagerNode::GCPManagerNode()
    : _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_btnOpenDialog(nullptr)
    , m_maxResidualEdit(nullptr)
    , m_sigmaThresholdEdit(nullptr)
    , m_minQualityCombo(nullptr)
    , m_statusLabel(nullptr)
    , m_db(new GCPDatabase(this))
    , m_maxResidual(10.0)
    , m_thresholdSigma(2.0)
    , m_minQuality(1)
    , m_outputNodeName("GCPResults")
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    createWidget();
    setState(ExecutionState::Pending);
}

GCPManagerNode::~GCPManagerNode()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

unsigned int GCPManagerNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 1;
    if (portType == PortType::Out) return 2;
    return 0;
}

NodeDataType GCPManagerNode::dataType(PortType portType, PortIndex portIndex) const
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

bool GCPManagerNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return true;
}

QString GCPManagerNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入影像 (H5)");
    }
    if (portType == PortType::Out) {
        if (portIndex == 0) return QStringLiteral("GCP数据 (H5)");
        if (portIndex == 1) return QStringLiteral("精度评估报告");
    }
    return QString();
}

std::shared_ptr<NodeData> GCPManagerNode::outData(PortIndex port)
{
    if (port == 0) return m_outputData;
    if (port == 1) return m_reportData;
    return nullptr;
}

void GCPManagerNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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
        m_reportData.reset();
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
    }
}

::QWidget* GCPManagerNode::embeddedWidget()
{
    return _widget;
}

void GCPManagerNode::createWidget()
{
    _widget = new ::QWidget();
    _widget->setFixedWidth(300); // 宽度锁定规范

    QVBoxLayout* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    QFormLayout* form = new QFormLayout();
    form->setSpacing(6);

    // 1. 上游节点名
    m_inputNodeLabel = new QLabel(QStringLiteral("等待输入"), _widget);
    form->addRow(QStringLiteral("上游影像:"), m_inputNodeLabel);

    // 2. 最大残差门限
    m_maxResidualEdit = new QLineEdit(QString::number(m_maxResidual, 'f', 1), _widget);
    form->addRow(QStringLiteral("最大残差 (米):"), m_maxResidualEdit);

    // 3. 粗差 Sigma 门限
    m_sigmaThresholdEdit = new QLineEdit(QString::number(m_thresholdSigma, 'f', 1), _widget);
    form->addRow(QStringLiteral("粗差阈值 (Sigma):"), m_sigmaThresholdEdit);

    // 4. 最小质量等级
    m_minQualityCombo = new QComboBox(_widget);
    m_minQualityCombo->addItem(QStringLiteral("低 (0)"), 0);
    m_minQualityCombo->addItem(QStringLiteral("中 (1)"), 1);
    m_minQualityCombo->addItem(QStringLiteral("高 (2)"), 2);
    m_minQualityCombo->setCurrentIndex(1); // 默认中等质量
    form->addRow(QStringLiteral("最小质量等级:"), m_minQualityCombo);

    layout->addLayout(form);

    // 5. 交互标注管理按钮
    m_btnOpenDialog = new QPushButton(QStringLiteral("标注与管理控制点"), _widget);
    m_btnOpenDialog->setMinimumHeight(30);
    layout->addWidget(m_btnOpenDialog);

    // 6. 状态显示
    m_statusLabel = new QLabel(QStringLiteral("状态：等待评估"), _widget);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // 绑定属性改变
    connect(m_maxResidualEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        bool ok;
        double val = text.toDouble(&ok);
        if (ok && val > 0.0) m_maxResidual = val;
    });

    connect(m_sigmaThresholdEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        bool ok;
        double val = text.toDouble(&ok);
        if (ok && val > 0.0) m_thresholdSigma = val;
    });

    connect(m_minQualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_minQuality = m_minQualityCombo->itemData(index).toInt();
    });

    connect(m_btnOpenDialog, &QPushButton::clicked, this, &GCPManagerNode::onOpenDialogClicked);
    
    // SOP 12: 尾部进行自愈刷新
    updateLabels();
    updateWidgetSize();
}

void GCPManagerNode::initDatabase()
{
    if (projectDir().isEmpty()) return;

    // 推导项目专属 SQLite 路径
    QString projXmlPath = NodeUtils::getProjectContext(_widget)->projectPath();
    QString projBaseName = QFileInfo(projXmlPath).baseName();
    QString dbPath = projectDir() + "/" + projBaseName + "_gcp.db";

    if (!m_db->isOpen() || m_db->databasePath() != dbPath) {
        m_db->open(dbPath);
    }
}

void GCPManagerNode::updateLabels()
{
    if (!m_db || !m_db->isOpen()) {
        m_statusLabel->setText(QStringLiteral("状态：数据库未就绪"));
        updateWidgetSize();
        return;
    }

    int total = m_db->getGCPCount();
    int annotated = m_db->getAnnotatedCount();
    m_statusLabel->setText(QStringLiteral("控制点总数: %1\n已标注行列: %2").arg(total).arg(annotated));
    updateWidgetSize();
}

void GCPManagerNode::onOpenDialogClicked()
{
    if (!m_inputData) {
        QMessageBox::warning(_widget, QStringLiteral("提示"), QStringLiteral("请先连接上游影像输入端口！"));
        return;
    }

    initDatabase();
    
    // 打开标注管理大窗
    QString inputH5 = m_inputData->filePath();
    GCPAnnotationDialog dlg(inputH5, m_db, _widget);
    if (dlg.exec() == QDialog::Accepted) {
        updateLabels();
        invalidateNodeData(); // SOP 34: 标注改动后使先前结果失效，清空输出并通知下游
    }
}

void GCPManagerNode::execute()
{
    if (!m_inputData) {
        setState(ExecutionState::Error);
        Q_EMIT executionError(QStringLiteral("输入影像数据为空。"));
        return;
    }

    initDatabase();
    
    if (m_db->getAnnotatedCount() == 0) {
        setState(ExecutionState::Error);
        Q_EMIT executionError(QStringLiteral("当前数据库中没有已标注的影像控制点！请先点击标注。"));
        return;
    }

    setState(ExecutionState::Running);
    m_statusLabel->setText(QStringLiteral("状态：正在读取数据并执行评估..."));

    // 1. 创建子线程及 Worker
    m_thread = new QThread(this);
    m_worker = new GCPManagerWorker();
    m_worker->moveToThread(m_thread);

    // 2. 信号与槽连接
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &GCPManagerNode::startProcess, m_worker, &GCPManagerWorker::evaluate_gcps);
    
    connect(m_worker, &BaseWorker::updateProcess, this, &GCPManagerNode::onProgressUpdate);
    connect(m_worker, &BaseWorker::endProcess, this, &GCPManagerNode::onProcessingFinished);
    connect(m_worker, &BaseWorker::errorProcess, this, &GCPManagerNode::onError);
    
    // 安全解耦：计算完成时将内存点集同步发回，主线程批量入库
    connect(m_worker, &GCPManagerWorker::evaluationFinished, this, &GCPManagerNode::onEvaluationFinished);

    m_thread->start();

    // 3. 提取所有的控制点传给子线程 (完全不在子线程做 SQLite I/O)
    std::vector<GCPPoint> gcps = m_db->getGCPs();

    // 4. 发射开始信号
    Q_EMIT startProcess(
        projectDir(),
        QFileInfo(NodeUtils::getProjectContext(_widget)->projectPath()).baseName(),
        m_inputData->filePath(),
        getOutputH5Path(),
        gcps,
        m_thresholdSigma,
        m_minQuality
    );

    // SOP 15: 自动运行状态防重置回调
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) {
            setState(ExecutionState::Running);
        }
    });
}

void GCPManagerNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    setState(ExecutionState::Stopped);
    m_statusLabel->setText(QStringLiteral("状态：已停止"));
}

void GCPManagerNode::processAutomatically()
{
    // 如果是自动运行模式，直接拉起计算
    if (m_inputData) {
        start();
    }
}

bool GCPManagerNode::prepareToStart()
{
    if (!m_inputData) return false;
    
    // 创建输出目录
    QDir().mkpath(projectDir() + "/" + m_outputNodeName);
    return true;
}

void GCPManagerNode::onProgressUpdate(int progress, const QString& message)
{
    setProgress(progress);
    m_statusLabel->setText(QStringLiteral("进度：%1%\n%2").arg(progress).arg(message));
    updateWidgetSize();
}

void GCPManagerNode::onProcessingFinished()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
        m_worker = nullptr;
    }

    setState(ExecutionState::Completed);
    updateLabels();
    updateWidgetSize();
    
    // 发送端口数据更新广播
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    
    finishExecution();
}

void GCPManagerNode::onEvaluationFinished(const std::vector<GCPPoint>& updatedGcps, const GCPEvaluationResult& result, const QString& reportText)
{
    // 1. 在主线程中安全写库并使用事务保护
    if (m_db && m_db->isOpen()) {
        m_db->updateGCPs(updatedGcps);
    }

    // 2. 将报告文本写入本地 Txt 文件中
    QString reportPath = getReportTxtPath();
    QFile file(reportPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << reportText;
        file.close();
    }

    // 3. 构建端口输出对象
    m_outputData = std::make_shared<ImportedFileData>(getOutputH5Path(), m_outputNodeName);
    
    QMap<QString, QString> meta;
    meta["RMS_2D"] = QString::number(result.rms_residual_2d, 'f', 3) + " m";
    meta["Total_GCPs"] = QString::number(result.num_gcp_used + result.num_gcp_rejected);
    meta["Rejected_GCPs"] = QString::number(result.num_gcp_rejected);
    m_reportData = std::make_shared<ImageInfoData>(reportPath, meta);
}

void GCPManagerNode::onError(const QString& error)
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
        m_worker = nullptr;
    }

    setState(ExecutionState::Error);
    m_statusLabel->setText(QStringLiteral("评估失败：%1").arg(error));
    Q_EMIT executionError(error);
    updateWidgetSize();
}

bool GCPManagerNode::validateAndRestoreOutput()
{
    // 自包含恢复规范：无须依赖上游是否连接，只根据磁盘实体恢复
    QString outH5 = getOutputH5Path();
    QString reportTxt = getReportTxtPath();

    if (QFile::exists(outH5)) {
        m_outputData = std::make_shared<ImportedFileData>(outH5, m_outputNodeName);
        
        QMap<QString, QString> meta;
        if (QFile::exists(reportTxt)) {
            // 解析读取已生成的报告元数据（简单粗暴提取部分关键字，以做视觉恢复）
            QFile file(reportTxt);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                while (!in.atEnd()) {
                    QString line = in.readLine();
                    if (line.contains("2D RMS")) {
                        QStringList parts = line.split(':');
                        if (parts.size() > 1) meta["RMS_2D"] = parts[1].trimmed();
                    }
                }
                file.close();
            }
            m_reportData = std::make_shared<ImageInfoData>(reportTxt, meta);
        } else {
            m_reportData = std::make_shared<ImageInfoData>(outH5, meta);
        }
        
        setState(ExecutionState::Completed);
        initDatabase();
        updateLabels();
        return true;
    }

    return false;
}

QJsonObject GCPManagerNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["max_residual"] = m_maxResidual;
    json["threshold_sigma"] = m_thresholdSigma;
    json["min_quality"] = m_minQuality;
    json["output_node_name"] = m_outputNodeName;
    return json;
}

void GCPManagerNode::load(QJsonObject const& json)
{
    // SOP 15: 先于基类解析特有参数，防止 validateAndRestoreOutput 在路径恢复时读取空值
    if (json.contains("max_residual")) m_maxResidual = json["max_residual"].toDouble();
    if (json.contains("threshold_sigma")) m_thresholdSigma = json["threshold_sigma"].toDouble();
    if (json.contains("min_quality")) m_minQuality = json["min_quality"].toInt();
    if (json.contains("output_node_name")) m_outputNodeName = json["output_node_name"].toString();

    ExecutableNodeDelegateModel::load(json);

    // 回显 UI
    if (m_maxResidualEdit) m_maxResidualEdit->setText(QString::number(m_maxResidual, 'f', 1));
    if (m_sigmaThresholdEdit) m_sigmaThresholdEdit->setText(QString::number(m_thresholdSigma, 'f', 1));
    if (m_minQualityCombo) {
        int idx = m_minQualityCombo->findData(m_minQuality);
        if (idx >= 0) m_minQualityCombo->setCurrentIndex(idx);
    }
    updateWidgetSize();
}

QVector<ParameterInfo> GCPManagerNode::getParameters() const
{
    QVector<ParameterInfo> params;
    params.append({QStringLiteral("最大残差 (m)"), "double", QString::number(m_maxResidual, 'f', 1), FieldEditType::Text});
    params.append({QStringLiteral("粗差阈值 (Sigma)"), "double", QString::number(m_thresholdSigma, 'f', 1), FieldEditType::Text});
    params.append({QStringLiteral("最小质量等级"), "int", QString::number(m_minQuality), FieldEditType::Text});
    params.append({QStringLiteral("输出目录名"), "string", m_outputNodeName, FieldEditType::Text});
    return params;
}

void GCPManagerNode::setParameter(const QString& paramName, const QString& value)
{
    bool ok;
    bool modified = false;
    if (paramName == QStringLiteral("最大残差 (m)")) {
        double val = value.toDouble(&ok);
        if (ok && val > 0.0 && val != m_maxResidual) {
            m_maxResidual = val;
            if (m_maxResidualEdit) m_maxResidualEdit->setText(value);
            modified = true;
        }
    } 
    else if (paramName == QStringLiteral("粗差阈值 (Sigma)")) {
        double val = value.toDouble(&ok);
        if (ok && val > 0.0 && val != m_thresholdSigma) {
            m_thresholdSigma = val;
            if (m_sigmaThresholdEdit) m_sigmaThresholdEdit->setText(value);
            modified = true;
        }
    } 
    else if (paramName == QStringLiteral("最小质量等级")) {
        int val = value.toInt(&ok);
        if (ok && (val >= 0 && val <= 2) && val != m_minQuality) {
            m_minQuality = val;
            if (m_minQualityCombo) {
                int idx = m_minQualityCombo->findData(val);
                if (idx >= 0) m_minQualityCombo->setCurrentIndex(idx);
            }
            modified = true;
        }
    } 
    else if (paramName == QStringLiteral("输出目录名")) {
        QString trimmed = value.trimmed();
        if (trimmed != m_outputNodeName) {
            m_outputNodeName = trimmed;
            modified = true;
        }
    }

    if (modified) {
        invalidateNodeData();
    }
}

void GCPManagerNode::invalidateNodeData()
{
    invalidateExecution();
    m_outputData.reset();
    m_reportData.reset();
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    updateWidgetSize();
}

void GCPManagerNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString GCPManagerNode::getOutputH5Path() const
{
    return projectDir() + "/" + m_outputNodeName + "/GCPResults.h5";
}

QString GCPManagerNode::getReportTxtPath() const
{
    return projectDir() + "/" + m_outputNodeName + "/GCPResults_report.txt";
}

QString GCPManagerNode::projectDir() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        QString path = iface->projectPath();
        if (path.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(path).absolutePath();
        }
        return path;
    }
    return QString();
}

} // namespace QtNodes
