#include "InSARLogManager.h"
#include "S1FrameMergeNode.h"
#include "S1FrameMergeWorker.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InterfaceManager.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "tinyxml.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace QtNodes {

S1FrameMergeNode::S1FrameMergeNode()
    : ExecutableNodeDelegateModel()
    , m_indexSpins{nullptr, nullptr}
    , m_outputNodeNameEdit(nullptr)
    , m_inputs{nullptr, nullptr}
    , m_outputData(nullptr)
    , m_imageInfoData(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
}

S1FrameMergeNode::~S1FrameMergeNode()
{
    m_remedyWatcher.cancel();
    m_remedyWatcher.waitForFinished();

    if (m_thread)
    {
        if (m_thread->isRunning())
        {
            m_thread->quit();
            m_thread->wait();
        }
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    else if (m_worker)
    {
        m_worker->deleteLater();
    }
    m_worker = nullptr;
}

unsigned int S1FrameMergeNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType S1FrameMergeNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "S1 Frame Data"};

    if (portIndex == 0)
        return NodeDataType{"imported_file", "S1 Merged Frame"};
    else
        return NodeDataType{"image_info", "Image Info"};
}

std::shared_ptr<NodeData> S1FrameMergeNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_imageInfoData;
}

bool S1FrameMergeNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return portType == PortType::Out;
}

QString S1FrameMergeNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return tr("成果 *");
        else if (portIndex == 1)
            return tr("预览 ?");
    }
    Q_UNUSED(portIndex);
    return QString();
}

bool S1FrameMergeNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void S1FrameMergeNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port >= 0 && port < 2)
    {
        m_inputs[port] = std::dynamic_pointer_cast<ImportedFileData>(data);

        // Generate default output name if both inputs connected and name not set
        if (m_inputs[0] && m_inputs[1] && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeName = generateDefaultOutputName();
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    // Delegate to base class to handle execution mode
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* S1FrameMergeNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject S1FrameMergeNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    QString nodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["outputNodeName"] = nodeName;

    modelJson["index1"] = m_index1;
    modelJson["index2"] = m_index2;

    return modelJson;
}

void S1FrameMergeNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined())
    {
        m_outputNodeName = vName.toString();
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    QJsonValue v1 = json["index1"];
    if (!v1.isUndefined())
    {
        m_index1 = v1.toInt();
        if (m_indexSpins[0])
            m_indexSpins[0]->setValue(m_index1);
    }

    QJsonValue v2 = json["index2"];
    if (!v2.isUndefined())
    {
        m_index2 = v2.toInt();
        if (m_indexSpins[1])
            m_indexSpins[1]->setValue(m_index2);
    }
}

void S1FrameMergeNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };



    // Image Index 1
    auto* index1Layout = new QHBoxLayout();
    QLabel* index1Label = new QLabel("Image Index 1:");
    index1Label->setFixedWidth(85);
    index1Layout->addWidget(index1Label);
    m_indexSpins[0] = new QSpinBox();
    m_indexSpins[0]->setMinimum(1);
    m_indexSpins[0]->setMaximum(100);
    m_indexSpins[0]->setValue(m_index1);
    connect(m_indexSpins[0], &QSpinBox::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_indexSpins[0]->value();
        if (m_index1 != val) {
            if (!confirmParameterChange()) {
                m_indexSpins[0]->setValue(m_index1);
                return;
            }
            m_index1 = val;
            invalidateNodeData();
        }
    });
    index1Layout->addWidget(m_indexSpins[0]);
    layout->addLayout(index1Layout);



    // Image Index 2
    auto* index2Layout = new QHBoxLayout();
    QLabel* index2Label = new QLabel("Image Index 2:");
    index2Label->setFixedWidth(85);
    index2Layout->addWidget(index2Label);
    m_indexSpins[1] = new QSpinBox();
    m_indexSpins[1]->setMinimum(1);
    m_indexSpins[1]->setMaximum(100);
    m_indexSpins[1]->setValue(m_index2);
    connect(m_indexSpins[1], &QSpinBox::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_indexSpins[1]->value();
        if (m_index2 != val) {
            if (!confirmParameterChange()) {
                m_indexSpins[1]->setValue(m_index2);
                return;
            }
            m_index2 = val;
            invalidateNodeData();
        }
    });
    index2Layout->addWidget(m_indexSpins[1]);
    layout->addLayout(index2Layout);

    // 目标节点名
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLabel->setFixedWidth(85);
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text();
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
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // 自愈与刷新
}

QString S1FrameMergeNode::generateDefaultOutputName() const
{
    if (m_inputs[0] && m_inputs[1])
    {
        QString node1 = m_inputs[0]->nodeName();
        QString node2 = m_inputs[1]->nodeName();
        return node1 + "_" + node2 + "_Merged";
    }
    return "FrameMerge_Output";
}

bool S1FrameMergeNode::validateInputs() const
{
    if (!m_inputs[0] || !m_inputs[1])
    {
        return false;
    }

    QString node1 = m_inputs[0]->nodeName();
    QString node2 = m_inputs[1]->nodeName();
    if (node1.isEmpty() || node2.isEmpty())
    {
        return false;
    }

    // Validate project context
    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
    {
        return false;
    }

    return true;
}

void S1FrameMergeNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void S1FrameMergeNode::onProcessingFinished()
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        m_previewGenerationPending = false;
        ++m_previewGenerationId;
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
        }
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
        return;
    }

    QString transactionError;
    QStringList h5Paths;
    if (!projectXml() || !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("s_re") << QStringLiteral("s_im"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, QStringList() << m_generatedOutputPath, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }

    const QString dstNode = m_preparedDstNode;
    const QString finalPath = h5Paths.value(0);
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    m_xmlDirty = projectXml()->XMLFile_add_origin(dstNode.toStdString().c_str(), m_pendingOutputName.toStdString().c_str(),
        QString("/%1/%2").arg(dstNode, QFileInfo(finalPath).fileName()).toStdString().c_str(), "sentinel") >= 0;
    if (!m_xmlDirty || !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("Frame merge output metadata was not produced.") : transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    publishResultToProjectTree(finalPath, m_pendingOutputName);

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    QStringList jpgPaths;
    for (const QString& h5Path : h5Paths) {
        const QFileInfo info(h5Path);
        jpgPaths.append(info.absolutePath() + "/" + info.baseName() + ".jpg");
    }
    if (jpgPaths.isEmpty()) {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        if (m_outputNodeNameEdit)
            m_outputNodeNameEdit->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        finishExecution();
        return;
    }

    m_remedyWatcher.disconnect();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    m_previewGenerationPending = true;
    const quint64 previewGenerationId = ++m_previewGenerationId;
    QStringList temporaryJpgPaths;
    for (const QString& jpgPath : jpgPaths) {
        const QFileInfo info(jpgPath);
        temporaryJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
            QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
        QFile::remove(jpgPath);
    }
    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, jpgPaths, temporaryJpgPaths, previewGenerationId]() {
        if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
            for (const QString& temporaryJpgPath : temporaryJpgPaths) {
                QFile::remove(temporaryJpgPath);
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
        for (int i = 0; i < jpgPaths.size() && i < temporaryJpgPaths.size(); ++i) {
            if (QFile::exists(temporaryJpgPaths[i]) &&
                QFile::rename(temporaryJpgPaths[i], jpgPaths[i])) {
                validJpgPaths.append(jpgPaths[i]);
            }
        }
        m_imageInfoData = validJpgPaths.isEmpty()
            ? nullptr : std::make_shared<ImageInfoData>(validJpgPaths);
        setOutputData(1, m_imageInfoData);
        if (m_outputNodeNameEdit)
            m_outputNodeNameEdit->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("S1FrameMergeNode", "executeProcessing completed.");
        finishExecution();
    });
    QFuture<void> future = QtConcurrent::run([h5Paths, temporaryJpgPaths]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], temporaryJpgPaths[i], "complex");
        }
    });
    m_remedyWatcher.setFuture(future);
}

void S1FrameMergeNode::onError(const QString& error)
{
    // Clean up thread
    m_worker = nullptr;
    m_thread = nullptr;
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

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setEnabled(true);
    setLastErrorMessage(error);
    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
    InSARLogManager::LogError("S1FrameMergeNode", "Error during frame merge: " + error);
}

void S1FrameMergeNode::onCancelled()
{
    InSARLogManager::LogInfo("S1FrameMergeNode", "Frame merge cancellation cleanup completed.");
    m_worker = nullptr;
    m_thread = nullptr;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }
    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void S1FrameMergeNode::onResultReceived(
    const QString& dstNode,
    const QString& filename,
    const QString& mergedH5Path,
    const QString& savePath,
    const QString& projectName)
{
    Q_UNUSED(dstNode);
    Q_UNUSED(savePath);
    Q_UNUSED(projectName);
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    m_generatedOutputPath = mergedH5Path;
    m_pendingOutputName = filename;
}

void S1FrameMergeNode::publishResultToProjectTree(const QString& h5Path, const QString& filename)
{
    QStandardItemModel* model = projectModel();
    const QList<QStandardItem*> projects = model ? model->findItems(projectName()) : QList<QStandardItem*>();
    if (projects.isEmpty()) {
        InSARLogManager::LogError("S1FrameMergeNode", "Project tree root was not found.");
        return;
    }

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), m_preparedDstNode.isEmpty() ? m_outputNodeName : m_preparedDstNode,
        "complex-0.0", FOLDER_ICON);
    if (!outputNode) {
        InSARLogManager::LogError("S1FrameMergeNode", "Unable to create frame merge output node.");
        return;
    }
    outputNode->setToolTip(m_preparedProjectName.isEmpty() ? projectName() : m_preparedProjectName);
    QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
        outputNode, filename, "complex", h5Path, IMAGEDATA_ICON);
    if (imageItem) {
        outputNode->setChild(imageItem->row(), 1, new QStandardItem(h5Path));
    }
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* S1FrameMergeNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString S1FrameMergeNode::projectPath() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString S1FrameMergeNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* S1FrameMergeNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void S1FrameMergeNode::execute()
{
    executeProcessing();
}

void S1FrameMergeNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
        if (m_thread && m_thread->isRunning()) {
            m_thread->requestInterruption();
        }
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
    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void S1FrameMergeNode::processAutomatically()
{
    // In automatic mode, if inputs are valid, execute
    if (prepareToStart())
    {
        executeProcessing();
    }
    else
    {
        setState(ExecutionState::Idle);
    }
}

void S1FrameMergeNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

bool S1FrameMergeNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    QString dstNode = m_outputNodeNameEdit && !m_outputNodeNameEdit->text().isEmpty()
        ? m_outputNodeNameEdit->text()
        : (m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName);
    dstNode = dstNode.trimmed();
    if (dstNode.isEmpty())
        return false;
    m_preparedDstNode = dstNode;
    m_outputNodeName = dstNode;
    if (m_outputNodeNameEdit && m_outputNodeNameEdit->text() != dstNode)
        m_outputNodeNameEdit->setText(dstNode);
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();

    m_preparedInputPaths.clear();
    m_preparedOutputPaths.clear();

    QStringList pathsToCheck;
    const QStringList inputPaths1 = m_inputs[0]->filePaths();
    const QStringList inputPaths2 = m_inputs[1]->filePaths();
    if (m_index1 < 1 || m_index2 < 1 || inputPaths1.size() < m_index1 || inputPaths2.size() < m_index2)
        return false;
    const QString h5Path1 = inputPaths1[m_index1 - 1];
    const QString h5Path2 = inputPaths2[m_index2 - 1];
    if (h5Path1.isEmpty() || h5Path2.isEmpty())
        return false;
    const QString outputBaseName = QFileInfo(h5Path1).baseName() + "_" + QFileInfo(h5Path2).baseName();
    m_preparedInputPaths = QStringList() << h5Path1 << h5Path2;
    m_preparedOutputPaths = QStringList() << QDir(m_preparedSavePath).absoluteFilePath(
        dstNode + "/" + outputBaseName + ".h5");
    pathsToCheck = m_preparedOutputPaths;

    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    if (!pathsToCheck.isEmpty())
    {
        auto ctx = NodeUtils::getProjectContext(_widget);
        if (_isAutoTriggered) {
            m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        } else {
            m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(ctx, dstNode, pathsToCheck, nullptr);
        }
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void S1FrameMergeNode::executeProcessing()
{
    InSARLogManager::LogInfo("S1FrameMergeNode", "executeProcessing started.");

    // Prepare processing
    QString dstNode = m_preparedDstNode;
    m_outputNodeName = dstNode;
    if (m_outputNodeNameEdit && m_outputNodeNameEdit->text() != dstNode)
        m_outputNodeNameEdit->setText(dstNode);
    const QString savePath = m_preparedSavePath;
    const QString project = m_preparedProjectName;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting)
    {
        m_outputNodeName = dstNode;
        if (validateAndRestoreOutput())
        {
            setState(ExecutionState::Running);
            setProgress(100);
            if (!m_remedyWatcher.isRunning()) {
                finishExecution();
            }
            return;
        }
        else
        {
            setState(ExecutionState::Error);
            return;
        }
    }
    setProgress(0);
    setState(ExecutionState::Running);
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(savePath, dstNode, m_preparedOutputPaths,
                                           m_preparedInputPaths, m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_generatedOutputPath.clear();
    m_pendingOutputName.clear();
    m_xmlDirty = false;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;

    // Create thread
    m_thread = new QThread();
    m_worker = new S1FrameMergeWorker();
    m_worker->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1FrameMergeNode::startFrameMerge, m_worker, &S1FrameMergeWorker::S1_frame_merge);
    const QString stagingNode = m_outputTransaction.stagingName;
    const QString firstH5Path = m_preparedInputPaths.value(0);
    const QString secondH5Path = m_preparedInputPaths.value(1);
    connect(m_thread, &QThread::started, [this, project, savePath, stagingNode, firstH5Path, secondH5Path]() {
        Q_EMIT startFrameMerge(project, savePath, stagingNode, firstH5Path, secondH5Path);
    });
    connect(m_worker, &S1FrameMergeWorker::updateProcess, this, &S1FrameMergeNode::onProgressUpdate);
    connect(m_worker, &S1FrameMergeWorker::endProcess, this, &S1FrameMergeNode::onProcessingFinished);
    connect(m_worker, &S1FrameMergeWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &S1FrameMergeWorker::errorProcess, this, &S1FrameMergeNode::onError);
    connect(m_worker, &S1FrameMergeWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &S1FrameMergeWorker::cancelled, this, &S1FrameMergeNode::onCancelled);
    connect(m_worker, &S1FrameMergeWorker::cancelled, m_thread, &QThread::quit);
    connect(m_worker, &S1FrameMergeWorker::sendResult, this, &S1FrameMergeNode::onResultReceived);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
    deferAutomaticCompletion();
    m_thread->start();
    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setEnabled(false);

    // 在下一个事件循环中强行将状态重置为 Running，防止基类 setInData 在 Automatic 模式下将其强行设为 Idle
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

bool S1FrameMergeNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths) || h5Paths.isEmpty()) {
        return false;
    }

    m_preparedDstNode = dstNode;
    m_preparedProjectName = projectName();
    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    QStringList existingJpgPaths;
    QStringList missingH5Paths;
    QStringList finalJpgPaths;
    for (const QString& h5Path : h5Paths) {
        const QFileInfo info(h5Path);
        const QString jpgPath = info.absolutePath() + "/" + info.baseName() + ".jpg";
        finalJpgPaths.append(jpgPath);
        if (NodeUtils::isJpgPreviewCurrent(h5Path, jpgPath)) {
            existingJpgPaths.append(jpgPath);
        } else {
            missingH5Paths.append(h5Path);
        }
    }
    m_imageInfoData = existingJpgPaths.isEmpty()
        ? nullptr : std::make_shared<ImageInfoData>(existingJpgPaths);
    setOutputData(1, m_imageInfoData);

    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    for (const QString& h5Path : h5Paths) {
        publishResultToProjectTree(h5Path, QFileInfo(h5Path).baseName());
    }

    if (!missingH5Paths.isEmpty()) {
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
        }
        m_previewGenerationPending = true;
        const quint64 previewGenerationId = ++m_previewGenerationId;
        QStringList temporaryJpgPaths;
        QStringList missingFinalJpgPaths;
        for (int i = 0; i < h5Paths.size(); ++i) {
            if (!NodeUtils::isJpgPreviewCurrent(h5Paths[i], finalJpgPaths[i])) {
                const QFileInfo info(finalJpgPaths[i]);
                temporaryJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
                    QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
                missingFinalJpgPaths.append(finalJpgPaths[i]);
                QFile::remove(finalJpgPaths[i]);
            }
        }
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Paths, finalJpgPaths, temporaryJpgPaths, missingFinalJpgPaths, previewGenerationId]() {
            if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
                for (const QString& temporaryJpgPath : temporaryJpgPaths) {
                    QFile::remove(temporaryJpgPath);
                }
                return;
            }
            m_previewGenerationPending = false;
            for (int i = 0; i < temporaryJpgPaths.size() && i < missingFinalJpgPaths.size(); ++i) {
                if (QFile::exists(temporaryJpgPaths[i])) {
                    QFile::rename(temporaryJpgPaths[i], missingFinalJpgPaths[i]);
                }
            }
            QStringList currentJpgPaths;
            for (int i = 0; i < h5Paths.size() && i < finalJpgPaths.size(); ++i) {
                if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], finalJpgPaths[i])) {
                    currentJpgPaths.append(finalJpgPaths[i]);
                }
            }
            m_imageInfoData = currentJpgPaths.isEmpty()
                ? nullptr : std::make_shared<ImageInfoData>(currentJpgPaths);
            setOutputData(1, m_imageInfoData);
            if (executionState() == ExecutionState::Running) {
                setProgress(100);
                finishExecution();
            } else {
                Q_EMIT dataUpdated(1);
            }
        });
        QFuture<void> future = QtConcurrent::run([missingH5Paths, temporaryJpgPaths]() {
            for (int i = 0; i < missingH5Paths.size() && i < temporaryJpgPaths.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(missingH5Paths[i], temporaryJpgPaths[i], "complex");
            }
        });
        m_remedyWatcher.setFuture(future);
    }

    return true;

}

QStringList S1FrameMergeNode::previewImagePaths() const
{
    QStringList existingPaths;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return existingPaths;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        for (const QString& h5Path : h5Paths) {
            const QFileInfo info(h5Path);
            const QString jpgPath = info.absolutePath() + "/" + info.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                existingPaths.append(jpgPath);
            }
        }
    }
    return existingPaths;
}

} // namespace QtNodes

