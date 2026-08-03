#include "DemNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include "Utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QHash>
#include <QStandardItemModel>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include "QtNodes/internal/NodeDetailWindow.hpp"

namespace QtNodes {

DemNode::DemNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_methodCombo(nullptr)
    , m_timesLabel(nullptr)
    , m_timesEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_method(1) // default: Newton
    , m_times(20) // default iter_times
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    qRegisterMetaType<DemFileResult>("DemFileResult");
    setExecutionMode(ExecutionMode::Automatic);
}

DemNode::~DemNode()
{
    stopExecution();
    cleanupThreadResources();
}

unsigned int DemNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType DemNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "Imported File"};
    else
    {
        if (portIndex == 0)
            return NodeDataType{"dem_file", "DEM File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool DemNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString DemNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入相位");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool DemNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void DemNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_imageInfoData.reset();
    }
}

std::shared_ptr<NodeData> DemNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* DemNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject DemNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["method"] = m_method;
    modelJson["times"] = m_timesEdit ? m_timesEdit->text().toInt() : m_times;

    return modelJson;
}

void DemNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vMethod = json["method"];
    if (!vMethod.isUndefined()) m_method = vMethod.toInt();

    QJsonValue vTimes = json["times"];
    if (!vTimes.isUndefined()) m_times = vTimes.toInt();

    // SOP Rule 15: load parameters BEFORE triggering validateAndRestoreOutput in base load
    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_methodCombo) {
        m_methodCombo->setCurrentIndex(m_method - 1);
    }
    if (m_timesEdit) m_timesEdit->setText(QString::number(m_times));

    onMethodChanged(m_method - 1);
}

void DemNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void DemNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene);
        }
    };

    const int labelWidth = 100;



    // 3. DEM 方法
    auto* methodLayout = new QHBoxLayout();
    QLabel* methodLabel = new QLabel("DEM方法");
    methodLabel->setFixedWidth(labelWidth);
    methodLayout->addWidget(methodLabel);
    m_methodCombo = new QComboBox();
    m_methodCombo->setEditable(false);
    m_methodCombo->addItem("牛顿法");
    m_methodCombo->setCurrentIndex(m_method - 1);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        int val = index + 1;
        if (m_method != val) {
            if (!confirmParameterChange()) {
                m_methodCombo->blockSignals(true);
                m_methodCombo->setCurrentIndex(m_method - 1);
                m_methodCombo->blockSignals(false);
                return;
            }
            m_method = val;
            onMethodChanged(index);
            invalidateNodeData();
        }
    });
    methodLayout->addWidget(m_methodCombo);
    layout->addLayout(methodLayout);

    // 4. 迭代次数
    auto* timesLayout = new QHBoxLayout();
    m_timesLabel = new QLabel("迭代次数");
    m_timesLabel->setFixedWidth(labelWidth);
    timesLayout->addWidget(m_timesLabel);
    m_timesEdit = new QLineEdit();
    m_timesEdit->setText(QString::number(m_times));
    connect(m_timesEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        bool ok = false;
        int val = m_timesEdit->text().toInt(&ok);
        if (ok && val > 0 && m_times != val) {
            if (!confirmParameterChange()) {
                m_timesEdit->setText(QString::number(m_times));
                return;
            }
            m_times = val;
            invalidateNodeData();
        } else if (!ok || val <= 0) {
            QMessageBox::warning(nullptr, "Warning", QStringLiteral("迭代次数必须是正整数！"));
            m_timesEdit->setText(QString::number(m_times));
        }
    });
    timesLayout->addWidget(m_timesEdit);
    layout->addLayout(timesLayout);

    // 5. 目标节点
    auto* outputLayout = new QHBoxLayout();
    QLabel* outputLabel = new QLabel("目标节点");
    outputLabel->setFixedWidth(labelWidth);
    outputLayout->addWidget(outputLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    outputLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(outputLayout);

    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    onMethodChanged(m_method - 1);
}

void DemNode::onMethodChanged(int index)
{
    bool isNewton = (index == 0);

    if (m_timesLabel) m_timesLabel->setVisible(isNewton);
    if (m_timesEdit) m_timesEdit->setVisible(isNewton);

    updateWidgetSize();
}

void DemNode::updateWidgetSize()
{
    if (_widget)
    {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString DemNode::generateDefaultOutputName() const
{
    if (m_inputData) {
        return m_inputData->nodeName() + "_Dem";
    }
    return "Dem_Data";
}

bool DemNode::validateInputs() const
{
    if (projectName().isEmpty()) {
        return false;
    }
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        return false;
    }

    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) {
        return false;
    }

    // Alphanumeric and underscore validation
    bool bFlag = dstNode.contains(QRegularExpression("^\\w+$"));
    if (!bFlag) {
        return false;
    }

    // Check parameters
    if (m_method == 1 && m_times <= 0) {
        return false;
    }

    return true;
}

bool DemNode::commitWidgetParametersForExecution()
{
    if (m_outputNodeNameEdit) {
        m_outputNodeName = m_outputNodeNameEdit->text().trimmed();
    }

    if (!m_timesEdit) {
        return true;
    }

    bool ok = false;
    const int times = m_timesEdit->text().toInt(&ok);
    if (!ok || times <= 0) {
        setStartFailureMessage(QStringLiteral("迭代次数必须是正整数。"));
        return false;
    }

    m_times = times;
    return true;
}

bool DemNode::prepareToStart()
{
    if (!commitWidgetParametersForExecution()) {
        return false;
    }

    if (m_outputNodeName.trimmed().isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    if (!validateInputs()) {
        setStartFailureMessage(QStringLiteral("请检查输入数据和输出配置是否完整。"));
        return false;
    }

    m_preparedDstNode = m_outputNodeName.trimmed();
    m_outputNodeName = m_preparedDstNode;
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }

    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();

    m_preparedMethod = m_method;
    m_preparedTimes = m_times;

    QStringList srcPaths = m_inputData->filePaths();
    m_preparedPhasePaths = srcPaths;

    // DEM inversion requires a complete, compatible interferometric phase contract.
    for (const QString& srcPath : srcPaths) {
        QString inputH5 = srcPath;
        if (QDir::isRelativePath(inputH5)) {
            inputH5 = m_preparedSavePath + "/" + inputH5;
        }
        QString phaseContractError;
        if (!NodeUtils::validateDemPhaseInput(inputH5, &phaseContractError)) {
            setLastErrorMessage(phaseContractError);
            setState(ExecutionState::Error);
            Q_EMIT executionError(phaseContractError);
            return false;
        }
    }

    // Precalculate output file paths for overwrite check
    m_preparedOutputPaths.clear();
    for (const QString& srcPath : srcPaths) {
        QFileInfo fi(srcPath);
        QString changeName = fi.baseName() + "_dem";
        m_preparedOutputPaths.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + changeName + ".h5");
    }

    // 自动触发时（上游数据更新），强制覆盖，保证数据链路一致性
    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void DemNode::executeProcessing()
{
    InSARLogManager::LogInfo("DemNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        
        m_outputNodeNameEdit->setEnabled(true);
        m_methodCombo->setEnabled(true);
        onMethodChanged(m_method - 1);

        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) {
            if (!m_remedyWatcher.isRunning()) {
                finishExecution();
            }
        } else {
            setState(ExecutionState::Error);
        }
        return;
    }

    setProgress(0);
    setState(ExecutionState::Running);
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode,
                                           m_preparedOutputPaths, m_preparedPhasePaths,
                                           m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_pendingDemResults.clear();
    m_xmlDirty = false;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;

    m_thread = new QThread();
    m_workerThread = new DemWorker();
    m_workerThread->moveToThread(m_thread);

    const QStringList phasePaths = m_preparedPhasePaths;
    QStringList phaseNames;
    for (const QString& phasePath : phasePaths) {
        phaseNames.append(QFileInfo(phasePath).baseName());
    }

    connect(this, &DemNode::startDem, m_workerThread, &DemWorker::Dem);
    connect(m_workerThread, &DemWorker::demFileGenerated, this, &DemNode::handleDemFileGenerated);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, [this, phaseNames, phasePaths, stagingNode]() {
        Q_EMIT startDem(m_preparedMethod, m_preparedTimes, m_preparedSavePath, stagingNode,
                        phaseNames, phasePaths);
    });
    connect(m_workerThread, &DemWorker::updateProcess, this, &DemNode::onProgressUpdate);
    connect(m_workerThread, &DemWorker::endProcess, this, &DemNode::onProcessingFinished);
    connect(m_workerThread, &DemWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &DemWorker::cancelled, this, &DemNode::onCancelled);
    connect(m_workerThread, &DemWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &DemWorker::errorProcess, this, &DemNode::onError);
    connect(m_workerThread, &DemWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &DemWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Dynamic recovery of Running state for Automatic execution mode (SOP Rule 5)
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
            setState(ExecutionState::Running);
    });

    // Disable inputs during execution
    m_outputNodeNameEdit->setEnabled(false);
    m_methodCombo->setEnabled(false);
    if (m_timesEdit) m_timesEdit->setEnabled(false);

    deferAutomaticCompletion();
    m_thread->start();
}

void DemNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void DemNode::onProcessingFinished()
{
    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;
    QList<DemFileResult> committedResults;
    const QString dstNode = m_preparedDstNode;

    // Clean up worker thread
    releaseFinishedThreadResources();

    if (discardObsoleteAutomaticExecution()) {
        m_previewGenerationPending = false;
        ++m_previewGenerationId;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
        return;
    }

    QString transactionError;
    if (!projectXml() || !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("dem"), &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("Project XML context is unavailable for DEM output commit.") : transactionError);
        return;
    }
    QStringList workerPaths;
    for (const DemFileResult& result : m_pendingDemResults) workerPaths.append(result.absoluteDemPath);
    if (!NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, workerPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    m_xmlDirty = false;
    for (DemFileResult result : m_pendingDemResults) {
        const QString fileName = QFileInfo(result.absoluteDemPath).fileName();
        result.absoluteDemPath = QDir(projectPath() + "/" + dstNode).absoluteFilePath(fileName);
        result.relativeDemPath = QStringLiteral("/%1/%2").arg(dstNode, fileName);
        commitDemResult(result); committedResults.append(result);
    }
    if (!m_xmlDirty || !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("DEM output metadata was not produced.") : transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    for (const DemFileResult& result : committedResults) publishDemResultToProjectTree(result);
    if (auto* iface = NodeUtils::getProjectContext(_widget)) iface->refreshProjectTree();
    for (const QString& h5Path : h5Paths) { jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg"); types.append(QStringLiteral("dem")); }

    m_outputData = std::make_shared<DEMFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    if (!h5Paths.isEmpty())
    {
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
        }
        m_previewGenerationPending = true;
        const quint64 previewGenerationId = ++m_previewGenerationId;
        QStringList previewJpgPaths;
        for (const QString& jpgPath : jpgPaths) {
            const QFileInfo info(jpgPath);
            previewJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
                QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
            QFile::remove(jpgPath);
        }

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, jpgPaths, previewJpgPaths, previewGenerationId]() {
            if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
                for (const QString& previewJpgPath : previewJpgPaths) {
                    QFile::remove(previewJpgPath);
                }
                return;
            }
            m_previewGenerationPending = false;
            if (discardObsoleteAutomaticExecution()) {
                ++m_previewGenerationId;
                m_outputData.reset();
                m_imageInfoData.reset();
                setOutputData(0, nullptr);
                setOutputData(1, nullptr);
                return;
            }

            QStringList validJpgPaths;
            for (int i = 0; i < jpgPaths.size() && i < previewJpgPaths.size(); ++i) {
                if (QFile::exists(previewJpgPaths[i]) &&
                    QFile::rename(previewJpgPaths[i], jpgPaths[i])) {
                    validJpgPaths.append(jpgPaths[i]);
                }
            }
            if (!validJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(validJpgPaths);
                setOutputData(1, m_imageInfoData);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
            }
            m_outputNodeNameEdit->setEnabled(true);
            m_methodCombo->setEnabled(true);
            if (m_timesEdit) m_timesEdit->setEnabled(true);
            onMethodChanged(m_method - 1);

            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("DemNode", "executeProcessing completed.");
            finishExecution();
        });

        QFuture<void> future = QtConcurrent::run([h5Paths, previewJpgPaths, types]() {
            for (int i = 0; i < h5Paths.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(h5Paths[i], previewJpgPaths[i], types[i]);
            }
        });
        m_remedyWatcher.setFuture(future);
    }
    else
    {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);

        m_outputNodeNameEdit->setEnabled(true);
        m_methodCombo->setEnabled(true);
        if (m_timesEdit) m_timesEdit->setEnabled(true);
        onMethodChanged(m_method - 1);

        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("DemNode", "executeProcessing completed (empty output list).");
        finishExecution();
    }
}

void DemNode::handleDemFileGenerated(const DemFileResult& result)
{
    m_pendingDemResults.append(result);
}

void DemNode::commitDemResult(const DemFileResult& result)
{
    XMLFile* xml = projectXml();
    if (xml) {
        xml->XMLFile_add_dem(m_preparedDstNode.toStdString().c_str(), result.demName.toStdString().c_str(),
            result.relativeDemPath.toStdString().c_str(), result.offsetRow, result.offsetCol, "Iteration", m_preparedTimes);
        m_xmlDirty = true;
    }
}

void DemNode::publishDemResultToProjectTree(const DemFileResult& result)
{
    QStandardItemModel* model = projectModel();
    if (!model) return;
    const QList<QStandardItem*> projects = model->findItems(m_preparedProjectName);
    if (projects.isEmpty()) return;
    QStandardItem* demNode = NodeUtils::findOrCreateProjectNode(projects.first(), m_preparedDstNode, "dem-1.0", FOLDER_ICON);
    if (demNode) NodeUtils::findOrCreateChildItem(demNode, result.demName, "dem", result.absoluteDemPath, IMAGEDATA_ICON);
}


void DemNode::cleanupThreadResources()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
    }
    m_workerThread = nullptr;
}

void DemNode::releaseFinishedThreadResources()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

void DemNode::onError(const QString& error)
{
    releaseFinishedThreadResources();
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_timesEdit) m_timesEdit->setEnabled(true);
    onMethodChanged(m_method - 1);

    setLastErrorMessage(error);
    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
}

void DemNode::onCancelled()
{
    releaseFinishedThreadResources();
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_timesEdit) m_timesEdit->setEnabled(true);
}

void DemNode::onModelUpdated(QStandardItemModel* model)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

bool DemNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        return false;
    }
    QStringList expectedJpgPaths;
    QStringList types;

    for (const QString& h5Path : h5Paths) {
        const QFileInfo info(h5Path);
        expectedJpgPaths.append(info.absolutePath() + "/" + info.baseName() + ".jpg");
        types.append("dem");
    }

    m_outputData = std::make_shared<DEMFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);
    if (executionState() != ExecutionState::Running) {
        Q_EMIT dataUpdated(0);
    }

    // Remedy missing JPG previews in background (SOP Rule 15)
    QStringList existingJpgPaths;
    QStringList missingH5s;
    QStringList missingJpgs;
    QStringList missingTypes;

    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) {
            existingJpgPaths.append(expectedJpgPaths[i]);
        } else {
            missingH5s.append(h5Paths[i]);
            missingJpgs.append(expectedJpgPaths[i]);
            missingTypes.append(types[i]);
        }
    }

    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
        setOutputData(1, m_imageInfoData);
        if (executionState() != ExecutionState::Running) {
            Q_EMIT dataUpdated(1);
        }
    } else {
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
        }
        m_previewGenerationPending = true;
        const quint64 previewGenerationId = ++m_previewGenerationId;
        QStringList previewJpgPaths;
        for (const QString& missingJpg : missingJpgs) {
            const QFileInfo info(missingJpg);
            previewJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
                QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
            QFile::remove(missingJpg);
        }

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Paths, expectedJpgPaths, missingJpgs, previewJpgPaths, previewGenerationId]() {
            if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
                for (const QString& previewJpgPath : previewJpgPaths) {
                    QFile::remove(previewJpgPath);
                }
                return;
            }
            m_previewGenerationPending = false;
            for (int i = 0; i < previewJpgPaths.size() && i < missingJpgs.size(); ++i) {
                if (QFile::exists(previewJpgPaths[i])) {
                    QFile::rename(previewJpgPaths[i], missingJpgs[i]);
                }
            }
            QStringList validJpgPaths;
            for (int i = 0; i < h5Paths.size() && i < expectedJpgPaths.size(); ++i) {
                if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) {
                    validJpgPaths.append(expectedJpgPaths[i]);
                }
            }
            if (!validJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(validJpgPaths);
                setOutputData(1, m_imageInfoData);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
            }
            InSARLogManager::LogInfo("DemNode", "validateAndRestoreOutput background rendering completed.");

            if (executionState() == ExecutionState::Running) {
                setProgress(100);
                finishExecution();
            } else {
                Q_EMIT dataUpdated(1);
            }
        });

        QFuture<void> future = QtConcurrent::run([missingH5s, previewJpgPaths, missingTypes]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(missingH5s[i], previewJpgPaths[i], missingTypes[i]);
            }
        });
        m_remedyWatcher.setFuture(future);
    }

    return true;
}

QStringList DemNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return list;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        for (const QString& h5Path : h5Paths) {
            const QFileInfo info(h5Path);
            QString jpgPath = info.absolutePath() + "/" + info.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                list.append(jpgPath);
            }
        }
    }
    return list;
}

QStandardItemModel* DemNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString DemNode::projectPath() const
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
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString DemNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* DemNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void DemNode::execute()
{
    executeProcessing();
}

void DemNode::stopExecution()
{
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
        return;
    }

    if (!m_previewGenerationPending) {
        return;
    }

    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_timesEdit) m_timesEdit->setEnabled(true);
    onMethodChanged(m_method - 1);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void DemNode::processAutomatically()
{
    if (prepareToStart())
    {
        executeProcessing();
    }
    else
    {
        setState(ExecutionState::Idle);
    }
}

namespace {

QString demGenerationMethodName(int method)
{
    switch (method) {
    case 1: return QObject::tr("Newton 迭代反演");
    default: return QObject::tr("未知方法 (%1)").arg(method);
    }
}

struct DemGenerationImageDiagnostics
{
    QString inputName;
    bool outputFound = false;
    bool inputRead = false;
    bool outputRead = false;
    bool dimensionsMatch = false;
    int inputRows = 0;
    int inputCols = 0;
    int outputRows = 0;
    int outputCols = 0;
    bool hasRecordedMethod = false;
    int recordedMethod = 0;
    bool hasRecordedIterations = false;
    int recordedIterations = 0;
    bool dependenciesComplete = false;
    QStringList missingDependencies;
    qint64 totalPixels = 0;
    qint64 finitePixels = 0;
    double minHeight = std::numeric_limits<double>::infinity();
    double maxHeight = -std::numeric_limits<double>::infinity();
    double sumHeight = 0.0;
    double sumSquaredHeight = 0.0;
};

struct DemGenerationValidationResults
{
    bool success = false;
    QString errorMessage;
    int expectedMethod = 1;
    int expectedIterations = 20;
    bool hasRecordedMethod = false;
    int recordedMethod = 0;
    bool hasRecordedIterations = false;
    int recordedIterations = 0;
    bool metadataConsistent = true;
    QList<DemGenerationImageDiagnostics> images;
};

class DemGenerationValidationWidget : public BaseValidationWidget
{
public:
    DemGenerationValidationWidget(DemNode* node, QWidget* parent)
        : BaseValidationWidget(node, parent)
        , m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }

private:
    void setupUI()
    {
        setupBaseUI(QObject::tr("正在诊断 DEM 反演结果..."),
                    QObject::tr("正在核对输出完整性、参数记录、几何尺寸和高程数值有效性。"),
                    QObject::tr("DEM 反演诊断汇总"),
                    QObject::tr("DEM 参数与结果诊断"));

        m_validPixelsLabel = createFeatureLabel();
        m_heightRangeLabel = createFeatureLabel();
        m_heightMomentsLabel = createFeatureLabel();
        m_dimensionsLabel = createFeatureLabel();
        m_dependenciesLabel = createFeatureLabel();
        m_issuesLabel = createFeatureLabel();

        m_featureLayout->addRow(createHeaderLabel(QObject::tr("有效 DEM 像元:")), m_validPixelsLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("高程数值范围:")), m_heightRangeLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("均值 / 标准差:")), m_heightMomentsLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("尺寸匹配结果:")), m_dimensionsLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("下游关键数据:")), m_dependenciesLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缺失或异常结果:")), m_issuesLabel);
    }

    void setNotExecutedState()
    {
        m_statusTitle->setText(QObject::tr("诊断不可用"));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
        m_statusDesc->setText(QObject::tr("请先成功执行 DEM Generation 节点，再查看诊断结果。"));
        m_compTable->clearComparison();
        m_compTable->setEnabled(false);
        m_validPixelsLabel->setText(QObject::tr("未执行"));
        m_heightRangeLabel->setText(QObject::tr("未执行"));
        m_heightMomentsLabel->setText(QObject::tr("未执行"));
        m_dimensionsLabel->setText(QObject::tr("未执行"));
        m_dependenciesLabel->setText(QObject::tr("未执行"));
        m_issuesLabel->setText(QObject::tr("未执行"));
    }

    void startAsyncValidation() override
    {
        m_isTimedOut = false;
        if (m_node->executionState() != ExecutionState::Completed) {
            setNotExecutedState();
            return;
        }

        const auto inputData = m_node->inputDataForValidation();
        const auto outputData = std::dynamic_pointer_cast<ImportedFileData>(m_node->outData(0));
        if (!inputData || inputData->filePaths().isEmpty() || !outputData || outputData->filePaths().isEmpty()) {
            m_statusTitle->setText(QObject::tr("诊断失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未找到完整的输入相位或输出 DEM H5 文件列表。"));
            return;
        }

        const QJsonObject settings = m_node->save();
        const QStringList inputPaths = inputData->filePaths();
        const QStringList outputPaths = outputData->filePaths();
        const int expectedMethod = settings.value("method").toInt(1);
        const int expectedIterations = settings.value("times").toInt(20);
        m_loadingOverlay->startLoading(QObject::tr("正在读取全部 DEM 结果并计算数值统计..."));

        QFuture<DemGenerationValidationResults> future = QtConcurrent::run(
            [inputPaths, outputPaths, expectedMethod, expectedIterations]() {
                NodeUtils::Hdf5Locker locker;
                DemGenerationValidationResults result;
                result.expectedMethod = expectedMethod;
                result.expectedIterations = expectedIterations;

                QHash<QString, QString> outputsByBaseName;
                for (const QString& outputPath : outputPaths) {
                    outputsByBaseName.insert(QFileInfo(outputPath).baseName(), outputPath);
                }

                for (const QString& inputPath : inputPaths) {
                    DemGenerationImageDiagnostics image;
                    image.inputName = QFileInfo(inputPath).baseName();
                    const QString outputPath = outputsByBaseName.value(image.inputName + QStringLiteral("_dem"));
                    image.outputFound = !outputPath.isEmpty();
                    if (!image.outputFound) {
                        result.images.append(image);
                        continue;
                    }

                    cv::Mat inputPhase;
                    cv::Mat dem;
                    image.inputRead = NodeUtils::readMatFromH5(inputPath, "phase", inputPhase) && !inputPhase.empty();
                    image.outputRead = NodeUtils::readMatFromH5(outputPath, "dem", dem) && !dem.empty();
                    if (image.inputRead) {
                        image.inputRows = inputPhase.rows;
                        image.inputCols = inputPhase.cols;
                    }
                    if (image.outputRead) {
                        image.outputRows = dem.rows;
                        image.outputCols = dem.cols;
                    }
                    image.dimensionsMatch = image.inputRead && image.outputRead && inputPhase.size() == dem.size();

                    image.hasRecordedMethod = NodeUtils::readScalarFromH5(
                        outputPath, "dem_generation_method", image.recordedMethod);
                    image.hasRecordedIterations = NodeUtils::readScalarFromH5(
                        outputPath, "dem_generation_iterations", image.recordedIterations);
                    if (image.hasRecordedMethod) {
                        if (!result.hasRecordedMethod) {
                            result.hasRecordedMethod = true;
                            result.recordedMethod = image.recordedMethod;
                        } else if (result.recordedMethod != image.recordedMethod) {
                            result.metadataConsistent = false;
                        }
                    }
                    if (image.hasRecordedIterations) {
                        if (!result.hasRecordedIterations) {
                            result.hasRecordedIterations = true;
                            result.recordedIterations = image.recordedIterations;
                        } else if (result.recordedIterations != image.recordedIterations) {
                            result.metadataConsistent = false;
                        }
                    }

                    std::string source;
                    cv::Mat auxiliary;
                    const bool source1Present = NodeUtils::readStringFromH5(outputPath, "source_1", source);
                    const bool source2Present = NodeUtils::readStringFromH5(outputPath, "source_2", source);
                    const bool flatPhasePresent = NodeUtils::readMatFromH5(outputPath, "flat_phase_coefficient", auxiliary) && !auxiliary.empty();
                    const bool rangeLengthPresent = NodeUtils::readMatFromH5(outputPath, "range_len", auxiliary) && !auxiliary.empty();
                    const bool azimuthLengthPresent = NodeUtils::readMatFromH5(outputPath, "azimuth_len", auxiliary) && !auxiliary.empty();
                    const bool multilookRangePresent = NodeUtils::readMatFromH5(outputPath, "multilook_rg", auxiliary) && !auxiliary.empty();
                    const bool multilookAzimuthPresent = NodeUtils::readMatFromH5(outputPath, "multilook_az", auxiliary) && !auxiliary.empty();
                    if (!source1Present) image.missingDependencies.append(QStringLiteral("source_1"));
                    if (!source2Present) image.missingDependencies.append(QStringLiteral("source_2"));
                    if (!flatPhasePresent) image.missingDependencies.append(QStringLiteral("flat_phase_coefficient"));
                    if (!rangeLengthPresent) image.missingDependencies.append(QStringLiteral("range_len"));
                    if (!azimuthLengthPresent) image.missingDependencies.append(QStringLiteral("azimuth_len"));
                    if (!multilookRangePresent) image.missingDependencies.append(QStringLiteral("multilook_rg"));
                    if (!multilookAzimuthPresent) image.missingDependencies.append(QStringLiteral("multilook_az"));
                    image.dependenciesComplete = image.missingDependencies.isEmpty();

                    if (image.outputRead) {
                        cv::Mat demDouble;
                        dem.convertTo(demDouble, CV_64F);
                        image.totalPixels = static_cast<qint64>(demDouble.total());
                        for (int row = 0; row < demDouble.rows; ++row) {
                            const double* values = demDouble.ptr<double>(row);
                            for (int column = 0; column < demDouble.cols; ++column) {
                                const double value = values[column];
                                if (!std::isfinite(value)) {
                                    continue;
                                }
                                ++image.finitePixels;
                                image.minHeight = std::min(image.minHeight, value);
                                image.maxHeight = std::max(image.maxHeight, value);
                                image.sumHeight += value;
                                image.sumSquaredHeight += value * value;
                            }
                        }
                    }

                    result.images.append(image);
                }

                result.success = !result.images.isEmpty();
                if (!result.success) {
                    result.errorMessage = QObject::tr("没有可用于诊断的 DEM 影像。");
                }
                return result;
            });

        auto* watcher = new QFutureWatcher<DemGenerationValidationResults>(this);
        connect(watcher, &QFutureWatcher<DemGenerationValidationResults>::finished, this, [this, watcher]() {
            if (m_isTimedOut) {
                watcher->deleteLater();
                return;
            }

            const DemGenerationValidationResults result = watcher->result();
            m_loadingOverlay->stopLoading();
            if (!result.success) {
                m_statusTitle->setText(QObject::tr("诊断失败"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                m_statusDesc->setText(result.errorMessage);
                watcher->deleteLater();
                return;
            }

            m_compTable->clearComparison();
            m_compTable->setEnabled(true);
            if (result.hasRecordedMethod) {
                m_compTable->addComparison(QObject::tr("反演方法"), demGenerationMethodName(result.expectedMethod),
                    demGenerationMethodName(result.recordedMethod));
            } else {
                m_compTable->addDiagnostic(QObject::tr("反演方法"),
                    QObject::tr("当前设置：%1；输出未记录（旧结果）").arg(demGenerationMethodName(result.expectedMethod)));
            }
            if (result.hasRecordedIterations) {
                m_compTable->addComparison(QObject::tr("Newton 迭代次数"), QString::number(result.expectedIterations),
                    QString::number(result.recordedIterations));
            } else {
                m_compTable->addDiagnostic(QObject::tr("Newton 迭代次数"),
                    QObject::tr("当前设置：%1；输出未记录（旧结果）").arg(result.expectedIterations));
            }

            int foundOutputs = 0;
            int dimensionsMatch = 0;
            int completeDependencies = 0;
            int invalidResults = 0;
            qint64 totalPixels = 0;
            qint64 finitePixels = 0;
            double minimumHeight = std::numeric_limits<double>::infinity();
            double maximumHeight = -std::numeric_limits<double>::infinity();
            double sumHeight = 0.0;
            double sumSquaredHeight = 0.0;
            QStringList issues;

            for (const DemGenerationImageDiagnostics& image : result.images) {
                foundOutputs += image.outputFound ? 1 : 0;
                const QString expectedSize = image.inputRead
                    ? QStringLiteral("%1 x %2").arg(image.inputCols).arg(image.inputRows)
                    : QObject::tr("输入 phase 不可读");
                QString actualSize;
                if (!image.outputFound) {
                    actualSize = QObject::tr("缺失输出");
                    issues.append(image.inputName + QObject::tr(": 缺失输出"));
                    ++invalidResults;
                } else if (!image.outputRead) {
                    actualSize = QObject::tr("输出 dem 不可读");
                    issues.append(image.inputName + QObject::tr(": 输出 dem 不可读"));
                    ++invalidResults;
                } else {
                    actualSize = QStringLiteral("%1 x %2").arg(image.outputCols).arg(image.outputRows);
                    if (!image.dimensionsMatch) {
                        issues.append(image.inputName + QObject::tr(": 尺寸不匹配"));
                        ++invalidResults;
                    } else {
                        ++dimensionsMatch;
                    }
                    if (image.finitePixels == 0) {
                        issues.append(image.inputName + QObject::tr(": 无有效 DEM 像元"));
                        ++invalidResults;
                    }
                }
                m_compTable->addComparison(image.inputName, expectedSize, actualSize);

                if (image.dependenciesComplete) {
                    ++completeDependencies;
                } else if (image.outputFound) {
                    issues.append(image.inputName + QObject::tr(": 缺少 ") + image.missingDependencies.join(QStringLiteral(", ")));
                    ++invalidResults;
                }

                if (image.outputRead) {
                    totalPixels += image.totalPixels;
                    finitePixels += image.finitePixels;
                    minimumHeight = std::min(minimumHeight, image.minHeight);
                    maximumHeight = std::max(maximumHeight, image.maxHeight);
                    sumHeight += image.sumHeight;
                    sumSquaredHeight += image.sumSquaredHeight;
                }
            }

            m_compTable->addComparison(QObject::tr("输入 / 输出影像数"),
                QString::number(result.images.size()), QString::number(foundOutputs));
            const double validRatio = totalPixels > 0 ? 100.0 * finitePixels / totalPixels : 0.0;
            m_validPixelsLabel->setText(totalPixels > 0
                ? QObject::tr("%1 / %2 (%3%)").arg(finitePixels).arg(totalPixels).arg(QString::number(validRatio, 'f', 2))
                : QObject::tr("无可读取的 DEM 像元"));
            m_heightRangeLabel->setText(finitePixels > 0
                ? QObject::tr("%1 ~ %2").arg(QString::number(minimumHeight, 'g', 7), QString::number(maximumHeight, 'g', 7))
                : QObject::tr("无有效 DEM 像元"));
            if (finitePixels > 0) {
                const double meanHeight = sumHeight / finitePixels;
                const double variance = std::max(0.0, sumSquaredHeight / finitePixels - meanHeight * meanHeight);
                m_heightMomentsLabel->setText(QObject::tr("%1 / %2")
                    .arg(QString::number(meanHeight, 'g', 7), QString::number(std::sqrt(variance), 'g', 7)));
            } else {
                m_heightMomentsLabel->setText(QObject::tr("无有效 DEM 像元"));
            }
            m_dimensionsLabel->setText(QObject::tr("%1 / %2 匹配").arg(dimensionsMatch).arg(result.images.size()));
            m_dependenciesLabel->setText(QObject::tr("%1 / %2 齐全").arg(completeDependencies).arg(result.images.size()));
            m_issuesLabel->setText(issues.isEmpty() ? QObject::tr("未发现缺失或尺寸异常") : issues.join(QStringLiteral("\n")));

            const bool parametersMatch = result.metadataConsistent
                && (!result.hasRecordedMethod || result.recordedMethod == result.expectedMethod)
                && (!result.hasRecordedIterations || result.recordedIterations == result.expectedIterations);
            if (invalidResults > 0 || finitePixels == 0) {
                m_statusTitle->setText(QObject::tr("需要复查"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                m_statusDesc->setText(QObject::tr("发现输出缺失、尺寸异常、无效高程或下游关键数据不完整。请检查对应影像和处理日志。"));
            } else if (!parametersMatch) {
                m_statusTitle->setText(QObject::tr("参数与结果不一致"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                m_statusDesc->setText(QObject::tr("当前反演参数与输出 H5 中记录的参数不一致；修改参数后需要重新执行节点。"));
            } else {
                m_statusTitle->setText(QObject::tr("诊断完成"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                m_statusDesc->setText((!result.hasRecordedMethod || !result.hasRecordedIterations)
                    ? QObject::tr("输出完整且数值可读。旧结果未记录反演参数，无法确认其与当前设置是否一致。")
                    : QObject::tr("输出完整、尺寸匹配且高程数值可读。数值统计仅用于结果完整性诊断，不构成绝对高程精度评估。"));
            }
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }

    DemNode* m_node = nullptr;
    QLabel* m_validPixelsLabel = nullptr;
    QLabel* m_heightRangeLabel = nullptr;
    QLabel* m_heightMomentsLabel = nullptr;
    QLabel* m_dimensionsLabel = nullptr;
    QLabel* m_dependenciesLabel = nullptr;
    QLabel* m_issuesLabel = nullptr;
};

} // namespace

::QWidget* DemNode::createValidationWidget(::QWidget* parent)
{
    return new DemGenerationValidationWidget(this, parent);
}

} // namespace QtNodes
