#include "SBASTimeSeriesNode.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include "Utils.h"
#include <QTimer>
#include <QJsonDocument>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QApplication>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace QtNodes {

SBASTimeSeriesNode::SBASTimeSeriesNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_spatialThreshEdit(nullptr)
    , m_temporalThreshEdit(nullptr)
    , m_temporalThreshLowEdit(nullptr)
    , m_multilookRgEdit(nullptr)
    , m_multilookAzEdit(nullptr)
    , m_unwrapMethodCombo(nullptr)
    , m_alphaEdit(nullptr)
    , m_coherenceThreshEdit(nullptr)
    , m_temporalCoherenceThreshEdit(nullptr)
    , m_refinementCohThreshEdit(nullptr)
    , m_refinementDefThreshEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_resultLabel(nullptr)
    , m_temporalThreshLow(0.0)
    , m_temporalThresh(200.0)
    , m_spatialThresh(500.0)
    , m_multilookRg(4)
    , m_multilookAz(4)
    , m_unwrapMethod(1)
    , m_alpha(0.8)
    , m_coherenceThresh(0.5)
    , m_temporalCoherenceThresh(0.6)
    , m_refinementCohThresh(0.8)
    , m_refinementDefThresh(0.01)
    , m_outputNodeName(QStringLiteral("SBAS_Series"))
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    qRegisterMetaType<SBASTimeSeriesResult>("SBASTimeSeriesResult");
    setExecutionMode(ExecutionMode::Automatic);
}

SBASTimeSeriesNode::~SBASTimeSeriesNode()
{
    if (m_worker) m_worker->StopProcess();
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

unsigned int SBASTimeSeriesNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType SBASTimeSeriesNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return NodeDataType{"imported_file", "Imported File"};
    } else {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"}; // H5 results
        else
            return NodeDataType{"image_info", "Image Info"}; // JPG preview
    }
}

bool SBASTimeSeriesNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString SBASTimeSeriesNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return QStringLiteral("输入图像");
    else {
        if (portIndex == 0)
            return tr("成果 *");
        else
            return tr("预览 ?");
    }
}

bool SBASTimeSeriesNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> SBASTimeSeriesNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_previewData;
}

void SBASTimeSeriesNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
    if (filePaths.isEmpty()) {
        m_outputData.reset();
        m_previewData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
    }

    updateLabels();
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* SBASTimeSeriesNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void SBASTimeSeriesNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void SBASTimeSeriesNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // SOP: Lock width

    QVBoxLayout* mainLayout = new QVBoxLayout(_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(4);

    // Row 1: Input node display
    QHBoxLayout* inputLayout = new QHBoxLayout();
    QLabel* inputTitleLabel = new QLabel(QStringLiteral("输入节点:"));
    inputTitleLabel->setFixedWidth(80); // SOP: Fixed label width
    m_inputNodeLabel = new QLabel(QStringLiteral("等待输入"));
    m_inputNodeLabel->setStyleSheet("color: gray;");
    inputLayout->addWidget(inputTitleLabel);
    inputLayout->addWidget(m_inputNodeLabel);
    mainLayout->addLayout(inputLayout);

    // Parameters Grouping
    auto addParamRow = [&](const QString& labelText, QWidget* editWidget) {
        QHBoxLayout* layout = new QHBoxLayout();
        QLabel* label = new QLabel(labelText);
        label->setFixedWidth(80); // SOP: Fixed label width
        layout->addWidget(label);
        layout->addWidget(editWidget);
        mainLayout->addLayout(layout);
    };

    m_spatialThreshEdit = new QLineEdit(QString::number(m_spatialThresh));
    addParamRow(QStringLiteral("空间基线阈值:"), m_spatialThreshEdit);

    m_temporalThreshEdit = new QLineEdit(QString::number(m_temporalThresh));
    addParamRow(QStringLiteral("时间基线上限:"), m_temporalThreshEdit);

    m_temporalThreshLowEdit = new QLineEdit(QString::number(m_temporalThreshLow));
    addParamRow(QStringLiteral("时间基线下限:"), m_temporalThreshLowEdit);

    m_multilookRgEdit = new QLineEdit(QString::number(m_multilookRg));
    addParamRow(QStringLiteral("距离向视数:"), m_multilookRgEdit);

    m_multilookAzEdit = new QLineEdit(QString::number(m_multilookAz));
    addParamRow(QStringLiteral("方位向视数:"), m_multilookAzEdit);

    m_unwrapMethodCombo = new QComboBox();
    m_unwrapMethodCombo->addItem(QStringLiteral("三角网 MCF"), 1);
    m_unwrapMethodCombo->addItem(QStringLiteral("规则网 SNAPHU"), 2);
    m_unwrapMethodCombo->addItem(QStringLiteral("规则网 MCF"), 3);
    m_unwrapMethodCombo->setCurrentIndex(m_unwrapMethod - 1);
    addParamRow(QStringLiteral("相位解缠方法:"), m_unwrapMethodCombo);

    m_alphaEdit = new QLineEdit(QString::number(m_alpha));
    addParamRow(QStringLiteral("解缠权重α:"), m_alphaEdit);

    m_coherenceThreshEdit = new QLineEdit(QString::number(m_coherenceThresh));
    addParamRow(QStringLiteral("相干性阈值:"), m_coherenceThreshEdit);

    m_temporalCoherenceThreshEdit = new QLineEdit(QString::number(m_temporalCoherenceThresh));
    addParamRow(QStringLiteral("时序相干阈值:"), m_temporalCoherenceThreshEdit);

    m_refinementCohThreshEdit = new QLineEdit(QString::number(m_refinementCohThresh));
    addParamRow(QStringLiteral("精炼相干阈值:"), m_refinementCohThreshEdit);

    m_refinementDefThreshEdit = new QLineEdit(QString::number(m_refinementDefThresh));
    addParamRow(QStringLiteral("精炼形变阈值:"), m_refinementDefThreshEdit);

    m_outputNodeNameEdit = new QLineEdit(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("鑷姩鐢熸垚鎴栨墜鍔ㄨ緭鍏?")); // SOP: standard placeholder
    addParamRow(QStringLiteral("目标节点名:"), m_outputNodeNameEdit);

    // Row: Result Display
    m_resultLabel = new QLabel(QStringLiteral("状态：等待计算"));
    m_resultLabel->setStyleSheet("QLabel { background-color: rgba(128, 128, 128, 20); border: 1px solid rgba(128, 128, 128, 80); border-radius: 4px; padding: 6px; font-size: 11px; line-height: 14px; }");
    m_resultLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_resultLabel->setWordWrap(true);
    mainLayout->addWidget(m_resultLabel);

    // Connect parameters changes
    connect(m_spatialThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_spatialThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_temporalThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_temporalThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_temporalThreshLowEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_temporalThreshLow = text.toDouble(); invalidateExecution();
    });
    connect(m_multilookRgEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_multilookRg = text.toInt(); invalidateExecution();
    });
    connect(m_multilookAzEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_multilookAz = text.toInt(); invalidateExecution();
    });
    connect(m_unwrapMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_unwrapMethod = m_unwrapMethodCombo->itemData(idx).toInt(); invalidateExecution();
    });
    connect(m_alphaEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_alpha = text.toDouble(); invalidateExecution();
    });
    connect(m_coherenceThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_coherenceThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_temporalCoherenceThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_temporalCoherenceThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_refinementCohThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_refinementCohThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_refinementDefThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_refinementDefThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text; invalidateExecution();
    });

    updateLabels();
}

void SBASTimeSeriesNode::updateLabels()
{
    if (!m_inputNodeLabel) return;

    if (m_inputData) {
        m_inputNodeLabel->setText(m_inputData->nodeName());
        m_inputNodeLabel->setStyleSheet("color: green; font-weight: bold;");
    } else {
        m_inputNodeLabel->setText(QStringLiteral("等待输入"));
        m_inputNodeLabel->setStyleSheet("color: gray;");
    }

    if (m_resultLabel) {
        QString h5Path = projectPath() + "/" + m_outputNodeName + "/SBAS_time_series.h5";
        if (QFileInfo::exists(h5Path)) {
            m_resultLabel->setText(QStringLiteral("状态：时序分析计算完成\n输出：%1").arg(QFileInfo(h5Path).fileName()));
        } else {
            m_resultLabel->setText(QStringLiteral("状态：等待计算"));
        }
    }
    updateWidgetSize();
}

void SBASTimeSeriesNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool SBASTimeSeriesNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;
    if (m_outputNodeName.isEmpty()) return false;
    return true;
}

bool SBASTimeSeriesNode::prepareToStart()
{
    if (!validateInputs()) {
        InSARLogManager::LogWarning("SBASTimeSeriesNode", "prepareToStart skipped: validateInputs failed.");
        return false;
    }

    QString outDir = projectPath() + "/" + m_outputNodeName;
    QString h5Path = outDir + "/SBAS_time_series.h5";
    QStringList pathsToCheck = QStringList() << h5Path;

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget),
            m_outputNodeName,
            pathsToCheck,
            nullptr
        );
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void SBASTimeSeriesNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        validateAndRestoreOutput();
        return;
    }

    // If Overwrite: clean project tree first to avoid tree duplicates (SOP rule 14)
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);

    executeProcessing();
}

void SBASTimeSeriesNode::executeProcessing()
{
    InSARLogManager::LogInfo("SBASTimeSeriesNode", "executeProcessing started.");
    stopExecution();
    m_xmlDirty = false;

    m_thread = new QThread(this);
    m_worker = new SBASTimeSeriesWorker();
    m_worker->moveToThread(m_thread);

    QString projPath = projectPath();
    QString projName = projectName();
    QString csvPath = projPath + "/" + m_outputNodeName + "/sbas_time_series.csv";
    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();

    connect(m_thread, &QThread::started, m_worker, [this, projPath, projName, csvPath, filePaths]() {
        m_worker->SBAS_time_series(
            m_temporalThreshLow,
            m_temporalThresh,
            m_spatialThresh,
            m_multilookRg,
            m_multilookAz,
            m_unwrapMethod,
            m_alpha,
            m_coherenceThresh,
            m_temporalCoherenceThresh,
            m_refinementCohThresh,
            m_refinementDefThresh,
            projPath,
            projName,
            m_outputNodeName,
            csvPath,
            filePaths
        );
    });

    connect(m_worker, &SBASTimeSeriesWorker::sbasGenerated, this, &SBASTimeSeriesNode::onSbasGenerated);
    connect(m_worker, &SBASTimeSeriesWorker::updateProcess, this, &SBASTimeSeriesNode::onProgressUpdate);
    connect(m_worker, &SBASTimeSeriesWorker::endProcess, this, &SBASTimeSeriesNode::onProcessingFinished);
    connect(m_worker, &SBASTimeSeriesWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &SBASTimeSeriesWorker::errorProcess, this, &SBASTimeSeriesNode::onError);
    connect(m_worker, &SBASTimeSeriesWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &SBASTimeSeriesWorker::cancelled, this, &SBASTimeSeriesNode::onCancelled);
    connect(m_worker, &SBASTimeSeriesWorker::cancelled, m_thread, &QThread::quit);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    setState(ExecutionState::Running);
    setProgress(0);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) {
            setState(ExecutionState::Running);
        }
    });

    deferAutomaticCompletion();
    m_thread->start();
}

void SBASTimeSeriesNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
}

void SBASTimeSeriesNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

void SBASTimeSeriesNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void SBASTimeSeriesNode::onError(const QString& error)
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("SBASTimeSeriesNode", "Error during SBAS analysis: " + error);
    setState(ExecutionState::Error);
    finishExecution();
}

void SBASTimeSeriesNode::onCancelled()
{
    InSARLogManager::LogInfo("SBASTimeSeriesNode", "SBAS time-series cancellation cleanup completed.");
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputData.reset();
    m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (m_resultLabel) m_resultLabel->setText(QStringLiteral("宸插彇娑?"));
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void SBASTimeSeriesNode::onProcessingFinished()
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    if (m_xmlDirty) {
        IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
        const QString xmlPath = NodeUtils::getProjectFilePath(_widget);
        XMLFile* xml = iface ? iface->projectXml() : nullptr;
        if (!xml || xmlPath.isEmpty() || xml->XMLFile_save(xmlPath.toStdString().c_str()) < 0) {
            onError(QStringLiteral("Failed to save project XML after SBAS analysis."));
            return;
        }
        m_xmlDirty = false;
    }

    InSARLogManager::LogInfo("SBASTimeSeriesNode", "executeProcessing completed.");
    QString h5Path = projectPath() + "/" + m_outputNodeName + "/SBAS_time_series.h5";
    m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, m_outputNodeName);
    setOutputData(0, m_outputData);

    // Refresh project tree (SOP Rule 14 helper)
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }

    setProgress(100);
    setState(ExecutionState::Running);

    // Asynchronously generate preview JPG (SOP rule 7)
    if (!generateStaticPreviewJpg(true)) {
        finishExecution();
    }

    updateLabels();
}

void SBASTimeSeriesNode::onSbasGenerated(const SBASTimeSeriesResult& result)
{
    QStandardItemModel* model = nullptr;
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        model = iface->projectModel();
    }
    if (!model) return;

    QList<QStandardItem*> foundProjects = model->findItems(projectName());
    if (foundProjects.isEmpty()) return;
    QStandardItem* project = foundProjects[0];

    QStandardItem* sbasNode = NodeUtils::findOrCreateProjectNode(project, result.dstNode, "SBAS-1.0");
    if (sbasNode) {
        sbasNode->setToolTip(projectName());
        QStandardItem* itemImg = nullptr;
        for (int j = 0; j < sbasNode->rowCount(); j++) {
            if (sbasNode->child(j, 0)->text() == "SBAS_time_series") {
                itemImg = sbasNode->child(j, 0);
                break;
            }
        }
        if (!itemImg) {
            QStandardItem* sbasNameItem = new QStandardItem("SBAS_time_series");
            sbasNameItem->setToolTip("SBAS");
            QStandardItem* sbasPathItem = new QStandardItem(result.timesSeriesH5Path);
            sbasNameItem->setIcon(QIcon(IMAGEDATA_ICON));
            sbasNode->appendRow(sbasNameItem);
            sbasNode->setChild(sbasNode->rowCount() - 1, 1, sbasPathItem);
        } else {
            sbasNode->setChild(itemImg->row(), 1, new QStandardItem(result.timesSeriesH5Path));
        }
    }

    XMLFile* xml = iface ? iface->projectXml() : nullptr;
    if (!xml) {
        onError(QStringLiteral("Project XML is unavailable while publishing SBAS output."));
        return;
    }
    xml->XMLFile_add_SBAS(
        result.dstNode.toStdString().c_str(),
        "SBAS_time_series",
        result.relativePath.toStdString().c_str());
    m_xmlDirty = true;
}

bool SBASTimeSeriesNode::validateAndRestoreOutput()
{
    QString h5Path = projectPath() + "/" + m_outputNodeName + "/SBAS_time_series.h5";
    if (QFileInfo::exists(h5Path)) {
        m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, m_outputNodeName);
        setOutputData(0, m_outputData);
        
        // Asynchronously restore/generate JPG preview if missing (SOP rule 7)
        generateStaticPreviewJpg();

        setState(ExecutionState::Completed);
        setProgress(100);
        updateLabels();
        Q_EMIT dataUpdated(0);
        return true;
    }
    return false;
}

bool SBASTimeSeriesNode::generateStaticPreviewJpg(bool completeExecution)
{
    QString outDir = projectPath() + "/" + m_outputNodeName;
    QString h5Path = outDir + "/SBAS_time_series.h5";
    QString jpgPath = outDir + "/SBAS_time_series.jpg";

    if (!QFileInfo::exists(h5Path)) return false;

    // Use QFutureWatcher to do background JPG generation without blocking UI thread (SOP Pitfall 7)
    QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, jpgPath, completeExecution]() {
        if (discardObsoleteAutomaticExecution()) {
            watcher->deleteLater();
            return;
        }

        if (QFileInfo::exists(jpgPath)) {
            m_previewData = std::make_shared<ImageInfoData>(jpgPath);
            setOutputData(1, m_previewData);
            if (!completeExecution) {
                Q_EMIT dataUpdated(1);
            }
        } else if (completeExecution) {
            m_previewData.reset();
            setOutputData(1, nullptr);
        }
        watcher->deleteLater();
        if (completeExecution) {
            finishExecution();
        }
    });

    watcher->setFuture(QtConcurrent::run([h5Path, jpgPath]() {
        NodeUtils::Hdf5Locker locker;
        FormatConversion FC;
        Utils util;
        Mat defomation_velocity, mask;
        int ret = (NodeUtils::readMatFromH5(h5Path, "defomation_velocity", defomation_velocity, CV_64F) &&
                   NodeUtils::readMatFromH5(h5Path, "mask", mask)) ? 0 : -1;
        if (ret == 0 && !defomation_velocity.empty()) {
            util.savephase_white(jpgPath.toStdString().c_str(), "jet", defomation_velocity, mask);
        }
    }));
    return true;
}

QJsonObject SBASTimeSeriesNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["temporalThreshLow"] = m_temporalThreshLow;
    root["temporalThresh"] = m_temporalThresh;
    root["spatialThresh"] = m_spatialThresh;
    root["multilookRg"] = m_multilookRg;
    root["multilookAz"] = m_multilookAz;
    root["unwrapMethod"] = m_unwrapMethod;
    root["alpha"] = m_alpha;
    root["coherenceThresh"] = m_coherenceThresh;
    root["temporalCoherenceThresh"] = m_temporalCoherenceThresh;
    root["refinementCohThresh"] = m_refinementCohThresh;
    root["refinementDefThresh"] = m_refinementDefThresh;
    root["outputNodeName"] = m_outputNodeName;
    return root;
}

void SBASTimeSeriesNode::load(QJsonObject const& json)
{
    m_temporalThreshLow = json["temporalThreshLow"].toDouble(0.0);
    m_temporalThresh = json["temporalThresh"].toDouble(200.0);
    m_spatialThresh = json["spatialThresh"].toDouble(500.0);
    m_multilookRg = json["multilookRg"].toInt(4);
    m_multilookAz = json["multilookAz"].toInt(4);
    m_unwrapMethod = json["unwrapMethod"].toInt(1);
    m_alpha = json["alpha"].toDouble(0.8);
    m_coherenceThresh = json["coherenceThresh"].toDouble(0.5);
    m_temporalCoherenceThresh = json["temporalCoherenceThresh"].toDouble(0.6);
    m_refinementCohThresh = json["refinementCohThresh"].toDouble(0.8);
    m_refinementDefThresh = json["refinementDefThresh"].toDouble(0.01);
    m_outputNodeName = json["outputNodeName"].toString(QStringLiteral("SBAS_Series"));

    // SOP rule 15: Must call base load last
    ExecutableNodeDelegateModel::load(json);

    updateLabels();
}

QStringList SBASTimeSeriesNode::previewImagePaths() const
{
    QString jpgPath = projectPath() + "/" + m_outputNodeName + "/SBAS_time_series.jpg";
    if (QFileInfo::exists(jpgPath)) {
        return QStringList() << jpgPath;
    }
    return QStringList();
}

QString SBASTimeSeriesNode::projectPath() const
{
    IApplicationInterface* iface = nullptr;
    if (_widget) {
        iface = NodeUtils::getProjectContext(_widget);
    }
    if (!iface) {
        for (QWidget* topLevelWidget : QApplication::topLevelWidgets()) {
            MainWindow* mainWin = qobject_cast<MainWindow*>(topLevelWidget);
            if (mainWin) {
                if (mainWin->workspaceUI()) {
                    iface = mainWin->workspaceUI();
                    break;
                }
                if (mainWin->interfaceManager()) {
                    iface = mainWin->interfaceManager()->currentInterface();
                    if (iface) {
                        break;
                    }
                }
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

QString SBASTimeSeriesNode::projectName() const
{
    IApplicationInterface* iface = nullptr;
    if (_widget) {
        iface = NodeUtils::getProjectContext(_widget);
    }
    if (!iface) {
        for (QWidget* topLevelWidget : QApplication::topLevelWidgets()) {
            MainWindow* mainWin = qobject_cast<MainWindow*>(topLevelWidget);
            if (mainWin) {
                if (mainWin->workspaceUI()) {
                    iface = mainWin->workspaceUI();
                    break;
                }
            }
        }
    }

    if (iface) {
        return QFileInfo(iface->projectPath()).fileName();
    }
    return QString();
}

} // namespace QtNodes

