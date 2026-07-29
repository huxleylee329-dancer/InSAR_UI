#include "InSARLogManager.h"
#include "S1SwathMergeNode.h"
#include "S1SwathMergeWorker.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InterfaceManager.h"
#include "NodeUtils.h"
#include "tinyxml.h"
#include "icon_source.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace QtNodes {

S1SwathMergeNode::S1SwathMergeNode()
    : ExecutableNodeDelegateModel()
    , m_indexSpins{nullptr, nullptr, nullptr}
    , m_outputNodeNameEdit(nullptr)
    , m_inputs{nullptr, nullptr, nullptr}
    , m_outputData(nullptr)
    , m_imageInfoData(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
}

S1SwathMergeNode::~S1SwathMergeNode()
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

unsigned int S1SwathMergeNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 3;  // Three input ports (IW1, IW2, IW3)
    else
        return 2;  // Two output ports: 0 result, 1 preview
}

NodeDataType S1SwathMergeNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "S1 Swath Data"};

    if (portIndex == 0)
        return NodeDataType{"imported_file", "S1 Merged Swath"};
    else
        return NodeDataType{"image_info", "Image Info"};
}

std::shared_ptr<NodeData> S1SwathMergeNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_imageInfoData;
}

bool S1SwathMergeNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return portType == PortType::Out;
}

QString S1SwathMergeNode::portCaption(PortType portType, PortIndex portIndex) const
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

bool S1SwathMergeNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void S1SwathMergeNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port >= 0 && port < 3)
    {
        m_inputs[port] = std::dynamic_pointer_cast<ImportedFileData>(data);

        // Generate default output name if all inputs connected and name not set
        if (m_inputs[0] && m_inputs[1] && m_inputs[2] && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeName = generateDefaultOutputName();
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    // Delegate to base class to handle execution mode
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* S1SwathMergeNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject S1SwathMergeNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    QString nodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["outputNodeName"] = nodeName;

    modelJson["index1"] = m_index1;
    modelJson["index2"] = m_index2;
    modelJson["index3"] = m_index3;

    return modelJson;
}

void S1SwathMergeNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load() (会调用 validateAndRestoreOutput)
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined())
    {
        m_outputNodeName = vName.toString();
    }

    QJsonValue v1 = json["index1"];
    if (!v1.isUndefined())
    {
        m_index1 = v1.toInt();
    }

    QJsonValue v2 = json["index2"];
    if (!v2.isUndefined())
    {
        m_index2 = v2.toInt();
    }

    QJsonValue v3 = json["index3"];
    if (!v3.isUndefined())
    {
        m_index3 = v3.toInt();
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    if (m_indexSpins[0])
        m_indexSpins[0]->setValue(m_index1);
    if (m_indexSpins[1])
        m_indexSpins[1]->setValue(m_index2);
    if (m_indexSpins[2])
        m_indexSpins[2]->setValue(m_index3);
}

void S1SwathMergeNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // 严格尺寸规范，防膨胀 Bug
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };



    // Image Index 1
    auto* index1Layout = new QHBoxLayout();
    QLabel* index1Label = new QLabel("Image Index 1:");
    index1Label->setFixedWidth(90);
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
    index2Label->setFixedWidth(90);
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



    // Image Index 3
    auto* index3Layout = new QHBoxLayout();
    QLabel* index3Label = new QLabel("Image Index 3:");
    index3Label->setFixedWidth(90);
    index3Layout->addWidget(index3Label);
    m_indexSpins[2] = new QSpinBox();
    m_indexSpins[2]->setMinimum(1);
    m_indexSpins[2]->setMaximum(100);
    m_indexSpins[2]->setValue(m_index3);
    connect(m_indexSpins[2], &QSpinBox::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_indexSpins[2]->value();
        if (m_index3 != val) {
            if (!confirmParameterChange()) {
                m_indexSpins[2]->setValue(m_index3);
                return;
            }
            m_index3 = val;
            invalidateNodeData();
        }
    });
    index3Layout->addWidget(m_indexSpins[2]);
    layout->addLayout(index3Layout);

    // 目标节点名
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLabel->setFixedWidth(90);
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入")); // 瑙勮寖鍗犱綅绗︽枃瀛?"
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

    // 尾部自愈刷新
}

QString S1SwathMergeNode::generateDefaultOutputName() const
{
    if (m_inputs[0] && m_inputs[1] && m_inputs[2])
    {
        QString node1 = m_inputs[0]->nodeName();
        QString node2 = m_inputs[1]->nodeName();
        QString node3 = m_inputs[2]->nodeName();
        return node1 + "_" + node2 + "_" + node3 + "_Merged";
    }
    return "SwathMerge_Output";
}

bool S1SwathMergeNode::validateInputs() const
{
    if (!m_inputs[0] || !m_inputs[1] || !m_inputs[2])
    {
        return false;
    }

    QString node1 = m_inputs[0]->nodeName();
    QString node2 = m_inputs[1]->nodeName();
    QString node3 = m_inputs[2]->nodeName();
    if (node1.isEmpty() || node2.isEmpty() || node3.isEmpty())
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

void S1SwathMergeNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void S1SwathMergeNode::onProcessingFinished()
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        m_previewGenerationPending = false;
        ++m_previewGenerationId;
        if (m_remedyWatcher.isRunning()) m_remedyWatcher.cancel();
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
        return;
    }

    QString transactionError;
    QStringList h5Paths;
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("phase"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, QStringList() << m_generatedOutputPath, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }

    const QString dstNode = m_preparedDstNode;
    const QString finalPath = h5Paths.value(0);
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    m_xmlDirty = projectXml()->XMLFile_add_interferometric_phase(dstNode.toStdString().c_str(), m_pendingOutputName.toStdString().c_str(),
        QString("/%1/%2").arg(dstNode, QFileInfo(finalPath).fileName()).toStdString().c_str(),
        "unknown", "phase-1.0", 0, 0, 0, 0, 0, 0, 0, 0, 0) >= 0;
    if (!m_xmlDirty || !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("Swath merge output metadata was not produced.") : transactionError);
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
        if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        finishExecution();
        return;
    }

    m_remedyWatcher.disconnect();
    if (m_remedyWatcher.isRunning()) m_remedyWatcher.cancel();
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
            for (const QString& temporaryJpgPath : temporaryJpgPaths) QFile::remove(temporaryJpgPath);
            return;
        }
        m_previewGenerationPending = false;
        if (discardObsoleteAutomaticExecution()) {
            ++m_previewGenerationId;
            m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
            return;
        }
        QStringList validJpgPaths;
        for (int i = 0; i < jpgPaths.size() && i < temporaryJpgPaths.size(); ++i) {
            if (QFile::exists(temporaryJpgPaths[i]) && QFile::rename(temporaryJpgPaths[i], jpgPaths[i]))
                validJpgPaths.append(jpgPaths[i]);
        }
        m_imageInfoData = validJpgPaths.isEmpty() ? nullptr : std::make_shared<ImageInfoData>(validJpgPaths);
        setOutputData(1, m_imageInfoData);
        if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("S1SwathMergeNode", "executeProcessing completed.");
        finishExecution();
    });
    QFuture<void> future = QtConcurrent::run([h5Paths, temporaryJpgPaths]() {
        for (int i = 0; i < h5Paths.size(); ++i)
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], temporaryJpgPaths[i], "phase");
    });
    m_remedyWatcher.setFuture(future);
}


void S1SwathMergeNode::onError(const QString& error)
{
    m_worker = nullptr;
    m_thread = nullptr;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) m_remedyWatcher.cancel();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(true);
    setLastErrorMessage(error);
    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
}

void S1SwathMergeNode::onCancelled()
{
    InSARLogManager::LogInfo("S1SwathMergeNode", "Swath merge cancellation cleanup completed.");
    m_worker = nullptr;
    m_thread = nullptr;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) m_remedyWatcher.cancel();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void S1SwathMergeNode::onResultReceived(
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

void S1SwathMergeNode::publishResultToProjectTree(const QString& h5Path, const QString& filename)
{
    QStandardItemModel* model = projectModel();
    const QList<QStandardItem*> projects = model ? model->findItems(projectName()) : QList<QStandardItem*>();
    if (projects.isEmpty()) {
        InSARLogManager::LogError("S1SwathMergeNode", "Project tree root was not found.");
        return;
    }

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), m_preparedDstNode.isEmpty() ? m_outputNodeName : m_preparedDstNode, "phase-1.0", FOLDER_ICON);
    if (!outputNode) {
        InSARLogManager::LogError("S1SwathMergeNode", "Unable to create swath merge output node.");
        return;
    }
    outputNode->setToolTip(m_preparedProjectName.isEmpty() ? projectName() : m_preparedProjectName);
    QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
        outputNode, filename, "phase", h5Path, IMAGEDATA_ICON);
    if (imageItem) {
        outputNode->setChild(imageItem->row(), 1, new QStandardItem(h5Path));
    }
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* S1SwathMergeNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString S1SwathMergeNode::projectPath() const
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

QString S1SwathMergeNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* S1SwathMergeNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void S1SwathMergeNode::execute()
{
    executeProcessing();
}

void S1SwathMergeNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
        if (m_thread && m_thread->isRunning()) m_thread->requestInterruption();
        return;
    }
    if (!m_previewGenerationPending) return;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) m_remedyWatcher.cancel();
    m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) return;
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void S1SwathMergeNode::processAutomatically()
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

void S1SwathMergeNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

bool S1SwathMergeNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    QString dstNode = m_outputNodeNameEdit && !m_outputNodeNameEdit->text().isEmpty()
        ? m_outputNodeNameEdit->text()
        : (m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName);
    dstNode = dstNode.trimmed();
    if (dstNode.isEmpty()) return false;
    m_preparedDstNode = dstNode;
    m_outputNodeName = dstNode;
    if (m_outputNodeNameEdit && m_outputNodeNameEdit->text() != dstNode) m_outputNodeNameEdit->setText(dstNode);
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedInputPaths.clear();
    m_preparedOutputPaths.clear();

    const QStringList inputPaths1 = m_inputs[0]->filePaths();
    const QStringList inputPaths2 = m_inputs[1]->filePaths();
    const QStringList inputPaths3 = m_inputs[2]->filePaths();
    if (m_index1 < 1 || m_index2 < 1 || m_index3 < 1 || inputPaths1.size() < m_index1 ||
        inputPaths2.size() < m_index2 || inputPaths3.size() < m_index3) return false;
    const QString firstH5Path = inputPaths1[m_index1 - 1];
    const QString secondH5Path = inputPaths2[m_index2 - 1];
    const QString thirdH5Path = inputPaths3[m_index3 - 1];
    if (firstH5Path.isEmpty() || secondH5Path.isEmpty() || thirdH5Path.isEmpty()) return false;
    m_preparedInputPaths = QStringList() << firstH5Path << secondH5Path << thirdH5Path;
    m_preparedOutputPaths = QStringList() << QDir(m_preparedSavePath).absoluteFilePath(dstNode + "/merged_phase.h5");

    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    auto ctx = NodeUtils::getProjectContext(_widget);
    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(ctx, dstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void S1SwathMergeNode::executeProcessing()
{
    InSARLogManager::LogInfo("S1SwathMergeNode", "executeProcessing started.");

    QString dstNode = m_preparedDstNode;
    const QString savePath = m_preparedSavePath;
    const QString project = m_preparedProjectName;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting)
    {
        m_outputNodeName = dstNode;
        if (validateAndRestoreOutput())
        {
            setState(ExecutionState::Running);
            setProgress(100);
            if (m_remedyWatcher.isRunning()) {
                deferAutomaticCompletion();
            } else {
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
    m_outputNodeName = dstNode;
    m_generatedOutputPath.clear();
    m_pendingOutputName.clear();
    m_xmlDirty = false;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;

    // Create thread
    m_thread = new QThread();
    m_worker = new S1SwathMergeWorker();
    m_worker->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1SwathMergeNode::startSwathMerge, m_worker, &S1SwathMergeWorker::S1_swath_merge);
    const QString stagingNode = m_outputTransaction.stagingName;
    const QString firstH5Path = m_preparedInputPaths.value(0);
    const QString secondH5Path = m_preparedInputPaths.value(1);
    const QString thirdH5Path = m_preparedInputPaths.value(2);
    connect(m_thread, &QThread::started, [this, project, savePath, stagingNode, firstH5Path, secondH5Path, thirdH5Path]() {
        Q_EMIT startSwathMerge(project, savePath, stagingNode, firstH5Path, secondH5Path, thirdH5Path);
    });
    connect(m_worker, &S1SwathMergeWorker::updateProcess, this, &S1SwathMergeNode::onProgressUpdate);
    connect(m_worker, &S1SwathMergeWorker::endProcess, this, &S1SwathMergeNode::onProcessingFinished);
    connect(m_worker, &S1SwathMergeWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &S1SwathMergeWorker::errorProcess, this, &S1SwathMergeNode::onError);
    connect(m_worker, &S1SwathMergeWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &S1SwathMergeWorker::cancelled, this, &S1SwathMergeNode::onCancelled);
    connect(m_worker, &S1SwathMergeWorker::cancelled, m_thread, &QThread::quit);
    connect(m_worker, &S1SwathMergeWorker::sendResult, this, &S1SwathMergeNode::onResultReceived);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
    deferAutomaticCompletion();
    m_thread->start();
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(false);

    // 在下一个事件循环中强行将状态重置为 Running，防止基类 setInData 在 Automatic 模式下将其强行设为 Idle
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

bool S1SwathMergeNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths) || h5Paths.isEmpty()) return false;
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
        if (NodeUtils::isJpgPreviewCurrent(h5Path, jpgPath)) existingJpgPaths.append(jpgPath);
        else missingH5Paths.append(h5Path);
    }
    m_imageInfoData = existingJpgPaths.isEmpty() ? nullptr : std::make_shared<ImageInfoData>(existingJpgPaths);
    setOutputData(1, m_imageInfoData);
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    for (const QString& h5Path : h5Paths) publishResultToProjectTree(h5Path, QFileInfo(h5Path).baseName());

    if (!missingH5Paths.isEmpty()) {
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) m_remedyWatcher.cancel();
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
                for (const QString& temporaryJpgPath : temporaryJpgPaths) QFile::remove(temporaryJpgPath);
                return;
            }
            m_previewGenerationPending = false;
            for (int i = 0; i < temporaryJpgPaths.size() && i < missingFinalJpgPaths.size(); ++i) {
                if (QFile::exists(temporaryJpgPaths[i])) QFile::rename(temporaryJpgPaths[i], missingFinalJpgPaths[i]);
            }
            QStringList currentJpgPaths;
            for (int i = 0; i < h5Paths.size() && i < finalJpgPaths.size(); ++i) {
                if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], finalJpgPaths[i])) currentJpgPaths.append(finalJpgPaths[i]);
            }
            m_imageInfoData = currentJpgPaths.isEmpty() ? nullptr : std::make_shared<ImageInfoData>(currentJpgPaths);
            setOutputData(1, m_imageInfoData);
            if (executionState() == ExecutionState::Running) {
                setProgress(100);
                finishExecution();
            } else {
                Q_EMIT dataUpdated(1);
            }
        });
        QFuture<void> future = QtConcurrent::run([missingH5Paths, temporaryJpgPaths]() {
            for (int i = 0; i < missingH5Paths.size() && i < temporaryJpgPaths.size(); ++i)
                NodeUtils::generateJpgPreviewFromH5(missingH5Paths[i], temporaryJpgPaths[i], "phase");
        });
        m_remedyWatcher.setFuture(future);
    }
    return true;

}

QStringList S1SwathMergeNode::previewImagePaths() const
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

