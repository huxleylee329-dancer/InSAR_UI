#include "IonosphericCorrectionNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "Utils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QStandardItemModel>
#include <QDebug>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

IonosphericCorrectionNode::IonosphericCorrectionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_subbandRatioSpin(nullptr)
    , m_filterStrengthSpin(nullptr)
    , m_outputTECCheck(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_subbandRatio(0.3)
    , m_filterStrength(1.0)
    , m_outputTEC(false)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

IonosphericCorrectionNode::~IonosphericCorrectionNode() { stopExecution(); }

unsigned int IonosphericCorrectionNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 2;
    else return 2;
}

NodeDataType IonosphericCorrectionNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In) return NodeDataType{"imported_file", "Imported File"};
    else {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported File"};
        else return NodeDataType{"image_info", "Image Info"};
    }
}

bool IonosphericCorrectionNode::portCaptionVisible(PortType, PortIndex) const { return true; }

QString IonosphericCorrectionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0) return QStringLiteral("配准SLC对");
        else return QStringLiteral("DEM ?");
    } else {
        if (portIndex == 0) return QStringLiteral("成果 *");
        else return QStringLiteral("预览 ?");
    }
    return QString();
}

bool IonosphericCorrectionNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1) return true;
    if (portType == PortType::Out && portIndex == 1) return true;
    return false;
}

void IonosphericCorrectionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    }
    ExecutableNodeDelegateModel::setInData(data, port);
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_imageInfoData.reset();
    }
}

std::shared_ptr<NodeData> IonosphericCorrectionNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* IonosphericCorrectionNode::embeddedWidget()
{
    if (!_widget) createWidget();
    return _widget;
}

QJsonObject IonosphericCorrectionNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["subbandRatio"] = m_subbandRatio;
    modelJson["filterStrength"] = m_filterStrength;
    modelJson["outputTEC"] = m_outputTEC;
    return modelJson;
}

void IonosphericCorrectionNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();
    QJsonValue vRatio = json["subbandRatio"];
    if (!vRatio.isUndefined()) m_subbandRatio = vRatio.toDouble();
    QJsonValue vStrength = json["filterStrength"];
    if (!vStrength.isUndefined()) m_filterStrength = vStrength.toDouble();
    QJsonValue vTEC = json["outputTEC"];
    if (!vTEC.isUndefined()) m_outputTEC = vTEC.toBool();

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_subbandRatioSpin) m_subbandRatioSpin->setValue(m_subbandRatio);
    if (m_filterStrengthSpin) m_filterStrengthSpin->setValue(m_filterStrength);
    if (m_outputTECCheck) m_outputTECCheck->setChecked(m_outputTEC);
}

void IonosphericCorrectionNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void IonosphericCorrectionNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* formLayout = new QFormLayout(_widget);
    formLayout->setContentsMargins(6, 6, 6, 6);
    formLayout->setSpacing(6);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) Q_EMIT _scene->modified(_scene);
    };

    // 子频带中心频率间隔比例
    m_subbandRatioSpin = new QDoubleSpinBox();
    m_subbandRatioSpin->setRange(0.1, 0.5);
    m_subbandRatioSpin->setSingleStep(0.05);
    m_subbandRatioSpin->setValue(m_subbandRatio);
    connect(m_subbandRatioSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, invalidateNodeData](double val) {
            if (qAbs(m_subbandRatio - val) > 1e-6) {
                if (!confirmParameterChange()) { m_subbandRatioSpin->setValue(m_subbandRatio); return; }
                m_subbandRatio = val;
                invalidateNodeData();
            }
        });

    // 滤波强度
    m_filterStrengthSpin = new QDoubleSpinBox();
    m_filterStrengthSpin->setRange(0.1, 5.0);
    m_filterStrengthSpin->setSingleStep(0.1);
    m_filterStrengthSpin->setValue(m_filterStrength);
    connect(m_filterStrengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, invalidateNodeData](double val) {
            if (qAbs(m_filterStrength - val) > 1e-6) {
                if (!confirmParameterChange()) { m_filterStrengthSpin->setValue(m_filterStrength); return; }
                m_filterStrength = val;
                invalidateNodeData();
            }
        });

    // 是否输出 TEC 估计图
    m_outputTECCheck = new QCheckBox("输出 TEC 估计图");
    m_outputTECCheck->setChecked(m_outputTEC);
    connect(m_outputTECCheck, &QCheckBox::toggled, this, [this, invalidateNodeData](bool checked) {
        if (m_outputTEC != checked) {
            if (!confirmParameterChange()) { m_outputTECCheck->setChecked(m_outputTEC); return; }
            m_outputTEC = checked;
            invalidateNodeData();
        }
    });

    // 目标节点名称
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) { m_outputNodeNameEdit->setText(m_outputNodeName); return; }
            NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });

    formLayout->addRow("子频带比例", m_subbandRatioSpin);
    formLayout->addRow("滤波强度", m_filterStrengthSpin);
    formLayout->addRow("", m_outputTECCheck);
    formLayout->addRow("目标节点", m_outputNodeNameEdit);
}

void IonosphericCorrectionNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString IonosphericCorrectionNode::generateDefaultOutputName() const
{
    if (m_inputData) return m_inputData->nodeName() + "_IonoCor";
    return "Ionospheric_Correction";
}

bool IonosphericCorrectionNode::validateInputs() const
{
    if (projectName().isEmpty()) return false;
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;
    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstNode.isEmpty()) return false;
    if (!dstNode.contains(QRegularExpression("^\\w+$"))) return false;
    return true;
}

bool IonosphericCorrectionNode::prepareToStart()
{
    if (!validateInputs()) return false;
    m_preparedDstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName() : m_outputNodeNameEdit->text().trimmed();
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedSubbandRatio = m_subbandRatioSpin ? m_subbandRatioSpin->value() : m_subbandRatio;
    m_preparedFilterStrength = m_filterStrengthSpin ? m_filterStrengthSpin->value() : m_filterStrength;
    m_preparedOutputTEC = m_outputTECCheck ? m_outputTECCheck->isChecked() : m_outputTEC;

    m_preparedOutputPaths.clear();
    for (const QString& srcPath : m_inputData->filePaths()) {
        QFileInfo fi(srcPath);
        m_preparedOutputPaths.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + fi.baseName() + "_iono.h5");
    }

    if (_isAutoTriggered) m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    else m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
        NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void IonosphericCorrectionNode::executeProcessing()
{
    InSARLogManager::LogInfo("IonosphericCorrectionNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        m_outputNodeNameEdit->setEnabled(true);
        m_subbandRatioSpin->setEnabled(true);
        m_filterStrengthSpin->setEnabled(true);
        m_outputTECCheck->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) finishExecution();
        else setState(ExecutionState::Error);
        return;
    }

    setProgress(0);
    setState(ExecutionState::Running);
    m_generatedOutputNames.clear();
    m_generatedOutputPaths.clear();
    m_preparedSlcPaths = m_inputData ? m_inputData->filePaths() : QStringList();
    m_preparedSlcNames.clear();
    for (const QString& path : m_preparedSlcPaths)
        m_preparedSlcNames.append(QFileInfo(path).baseName());
    if (m_preparedSlcPaths.isEmpty()) { onError(QStringLiteral("没有可处理的 SLC 影像")); return; }

    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode, m_preparedOutputPaths,
                                           m_preparedSlcPaths, m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    m_thread = new QThread();
    m_workerThread = new IonosphericCorrectionWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &IonosphericCorrectionNode::startCorrection, m_workerThread, &IonosphericCorrectionWorker::doCorrection);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, this, [this, stagingNode]() {
        Q_EMIT startCorrection(m_preparedSubbandRatio, m_preparedFilterStrength, m_preparedOutputTEC,
            m_preparedSavePath, m_preparedProjectName, m_preparedSrcNode, stagingNode,
            m_preparedSlcNames, m_preparedSlcPaths);
    });
    connect(m_workerThread, &IonosphericCorrectionWorker::updateProcess, this, &IonosphericCorrectionNode::onProgressUpdate);
    connect(m_workerThread, &IonosphericCorrectionWorker::outputsGenerated, this,
        [this](const QStringList& outputNames, const QStringList& outputPaths) {
            m_generatedOutputNames = outputNames;
            m_generatedOutputPaths = outputPaths;
        });
    connect(m_workerThread, &IonosphericCorrectionWorker::endProcess, this, &IonosphericCorrectionNode::onProcessingFinished);
    connect(m_workerThread, &IonosphericCorrectionWorker::errorProcess, this, &IonosphericCorrectionNode::onError);
    connect(m_workerThread, &IonosphericCorrectionWorker::cancelled, this, &IonosphericCorrectionNode::onCancelled);
    connect(m_workerThread, &IonosphericCorrectionWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &IonosphericCorrectionWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &IonosphericCorrectionWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &IonosphericCorrectionWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &IonosphericCorrectionWorker::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) setState(ExecutionState::Running);
    });

    m_outputNodeNameEdit->setEnabled(false);
    m_subbandRatioSpin->setEnabled(false);
    m_filterStrengthSpin->setEnabled(false);
    m_outputTECCheck->setEnabled(false);
    deferAutomaticCompletion();
    m_thread->start();
}

void IonosphericCorrectionNode::onProgressUpdate(int progress, const QString& message) { Q_UNUSED(message); if (!isAutomaticExecutionObsolete()) setProgress(progress); }

void IonosphericCorrectionNode::onProcessingFinished()
{
    const QString dstNode = m_preparedDstNode;
    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;

    cleanUpThreadAndWorker();

    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        return;
    }

    QString transactionError;
    QStringList requiredDatasets;
    requiredDatasets.append(QStringLiteral("complex"));
    if (m_preparedOutputTEC) {
        requiredDatasets.append(QStringLiteral("tec_estimate"));
    }
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, requiredDatasets, &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("电离层校正输出验收失败") : transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    for (const QString& h5Path : h5Paths) {
        const QString name = QFileInfo(h5Path).baseName();
        const QString relativePath = QString("/%1/%2.h5").arg(dstNode, name);
        projectXml()->XMLFile_add_unwrap(dstNode.toStdString().c_str(), name.toStdString().c_str(),
            relativePath.toStdString().c_str(), 0, 0, "Ionospheric_Correction", 0);
    }
    if (!NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    persistOutputToProject(dstNode, h5Paths);
    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);
    for (const QString& h5Path : h5Paths) {
        jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg");
        types.append("complex");
    }

    startPreviewGeneration(h5Paths, jpgPaths, types, h5Paths, jpgPaths, true);
}

void IonosphericCorrectionNode::onError(const QString& error)
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    cleanUpThreadAndWorker();
    if (discardObsoleteAutomaticExecution()) return;
    m_outputNodeNameEdit->setEnabled(true); m_subbandRatioSpin->setEnabled(true);
    m_filterStrengthSpin->setEnabled(true); m_outputTECCheck->setEnabled(true);
    m_outputData.reset(); m_imageInfoData.reset();
    setOutputData(0, nullptr); setOutputData(1, nullptr);
    setLastErrorMessage(error);
    setState(ExecutionState::Error);
}

void IonosphericCorrectionNode::onCancelled()
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    cleanUpThreadAndWorker();
    if (discardObsoleteAutomaticExecution()) return;

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    m_outputNodeNameEdit->setEnabled(true); m_subbandRatioSpin->setEnabled(true);
    m_filterStrengthSpin->setEnabled(true); m_outputTECCheck->setEnabled(true);
}

bool IonosphericCorrectionNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return false;
    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) return false;
    QStringList expectedJpgPaths, types;
    for (const QString& h5Path : h5Paths) {
        expectedJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg");
        types.append("complex");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    QStringList missingH5s, missingJpgs, missingTypes, existingJpgs;
    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) existingJpgs.append(expectedJpgPaths[i]);
        else { missingH5s.append(h5Paths[i]); missingJpgs.append(expectedJpgPaths[i]); missingTypes.append(types[i]); }
    }
    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgs);
        setOutputData(1, m_imageInfoData); Q_EMIT dataUpdated(1);
    } else {
        startPreviewGeneration(missingH5s, missingJpgs, missingTypes, h5Paths, expectedJpgPaths, false);
    }
    return true;
}

void IonosphericCorrectionNode::persistOutputToProject(const QString& outputNodeName,
                                                        const QStringList& h5Paths)
{
    if (auto* model = projectModel()) {
        const QList<QStandardItem*> projects = model->findItems(projectName());
        if (!projects.isEmpty()) {
            QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
                projects.first(), outputNodeName, "complex-1.5", FOLDER_ICON);
            for (const QString& h5Path : h5Paths) {
                const QString name = QFileInfo(h5Path).baseName();
                NodeUtils::findOrCreateChildItem(outputNode, name, "complex", h5Path, IMAGEDATA_ICON);
            }
            if (auto* iface = NodeUtils::getProjectContext(_widget)) {
                iface->refreshProjectTree();
            }
        }
    }
}

void IonosphericCorrectionNode::startPreviewGeneration(const QStringList& h5Paths,
                                                       const QStringList& generatedJpgPaths,
                                                       const QStringList& types,
                                                       const QStringList& resultH5Paths,
                                                       const QStringList& resultJpgPaths,
                                                        bool completeExecution)
{
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.disconnect(this);
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Paths, generatedJpgPaths, types, resultH5Paths, resultJpgPaths, completeExecution]() {
            m_remedyWatcher.disconnect(this);
            startPreviewGeneration(h5Paths, generatedJpgPaths, types, resultH5Paths, resultJpgPaths, completeExecution);
        });
        return;
    }

    m_remedyWatcher.disconnect(this);
    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, resultH5Paths, resultJpgPaths, completeExecution]() {
        QStringList currentJpgPaths;
        for (int i = 0; i < resultH5Paths.size() && i < resultJpgPaths.size(); ++i) {
            if (NodeUtils::isJpgPreviewCurrent(resultH5Paths[i], resultJpgPaths[i])) currentJpgPaths.append(resultJpgPaths[i]);
        }
        if (completeExecution && discardObsoleteAutomaticExecution()) return;
        m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
        setOutputData(1, m_imageInfoData);
        if (completeExecution) {
            m_outputNodeNameEdit->setEnabled(true); m_subbandRatioSpin->setEnabled(true);
            m_filterStrengthSpin->setEnabled(true); m_outputTECCheck->setEnabled(true);
            setState(ExecutionState::Running); setProgress(100); finishExecution();
        } else {
            Q_EMIT dataUpdated(1);
        }
    });
    QFuture<void> future = QtConcurrent::run([h5Paths, generatedJpgPaths, types]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], generatedJpgPaths[i], types[i]);
        }
    });
    m_remedyWatcher.setFuture(future);
}

QStringList IonosphericCorrectionNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return list;
    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) return list;
    for (const QString& h5Path : h5Paths) {
        const QString jpg = QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg";
        if (QFile::exists(jpg)) list.append(jpg);
    }
    return list;
}

QStandardItemModel* IonosphericCorrectionNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString IonosphericCorrectionNode::projectPath() const
{
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
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) return QFileInfo(fullPath).absolutePath();
        return fullPath;
    }
    return QString();
}

QString IonosphericCorrectionNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* IonosphericCorrectionNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void IonosphericCorrectionNode::cleanUpThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;

    if (!thread)
        return;

    if (thread->isRunning()) {
        thread->quit();
        thread->wait();
    }
}

void IonosphericCorrectionNode::releaseFinishedThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

void IonosphericCorrectionNode::execute() { executeProcessing(); }

void IonosphericCorrectionNode::stopExecution()
{
    if (m_thread && m_thread->isRunning())
        m_thread->requestInterruption();
    cleanUpThreadAndWorker();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset(); m_imageInfoData.reset();
    setOutputData(0, nullptr); setOutputData(1, nullptr);
}

void IonosphericCorrectionNode::processAutomatically()
{
    if (prepareToStart()) executeProcessing();
    else setState(ExecutionState::Idle);
}

} // namespace QtNodes
