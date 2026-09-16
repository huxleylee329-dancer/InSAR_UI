#include "InSARLogManager.h"
#include "ClutterSuppressionNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "ImportDataTypes.h"
#include "icon_source.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>

namespace QtNodes {

ClutterSuppressionNode::ClutterSuppressionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputImageLabel(nullptr)
    , m_saveToProjectCheckBox(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_saveToProject(true)
    , m_outputNodeName("ClutterSuppression")
    , m_outputFileName("{InputName}_clutter")
    , m_task(nullptr)
    , m_isExecuting(false)
{
    setExecutionMode(ExecutionMode::Automatic);
}

ClutterSuppressionNode::~ClutterSuppressionNode()
{
    stopExecution();
}

unsigned int ClutterSuppressionNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType ClutterSuppressionNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return NodeDataType{"image_info", "Image Info"};
}

ProductInputContract ClutterSuppressionNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("clutter_suppression.input.preview");
    contract.allowedProductTypes = QStringList() << QStringLiteral("preview")
                                                 << QStringLiteral("generic_sar_raster");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract ClutterSuppressionNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("clutter_suppression.output.suppressed_image")
        : QStringLiteral("clutter_suppression.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("clutter_suppressed_image")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

bool ClutterSuppressionNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString ClutterSuppressionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入图像");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else
            return QStringLiteral("预览 ?");
    }
}

bool ClutterSuppressionNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void ClutterSuppressionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    auto imageInfo = std::dynamic_pointer_cast<ImageInfoData>(data);
    if (!imageInfo) {
        const auto importedFiles = std::dynamic_pointer_cast<ImportedFileData>(data);
        if (importedFiles) {
            imageInfo = std::make_shared<ImageInfoData>(importedFiles->filePaths());
            imageInfo->setProductDescriptor(importedFiles->productDescriptor());
        }
    }
    m_inputData = imageInfo;

    if (m_inputImageLabel) {
        if (m_inputData && !m_inputData->filePath().isEmpty()) {
            QFileInfo fi(m_inputData->filePath());
            m_inputImageLabel->setText(fi.fileName());
        } else {
            m_inputImageLabel->setText("");
        }
    }

    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateOutputFileName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    if (m_inputData && m_outputFileName.isEmpty()) {
        m_outputFileName = "{InputName}_clutter";
        if (m_outputFileNameEdit) {
            m_outputFileNameEdit->setText(m_outputFileName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_outputImagePaths.clear();
        m_savedOutputFiles.clear();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
    }
    updateParameterWidgetsEnableState();
}

std::shared_ptr<NodeData> ClutterSuppressionNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

QWidget* ClutterSuppressionNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void ClutterSuppressionNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setMinimumWidth(260);

    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    m_saveToProjectCheckBox = new QCheckBox("保存到项目树");
    m_saveToProjectCheckBox->setChecked(m_saveToProject);
    connect(m_saveToProjectCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool newState = (state == Qt::Checked);
        if (m_saveToProject != newState) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_saveToProjectCheckBox);
                m_saveToProjectCheckBox->setChecked(m_saveToProject);
                return;
            }
            m_saveToProject = newState;
            onSaveToProjectChanged(state);
            invalidateNodeData();
        }
    });
    layout->addWidget(m_saveToProjectCheckBox);

    auto* methodLabel = new QLabel(
        QStringLiteral("工作流固定使用 BM3D；CFAR 请在独立窗口中人工选择。"));
    methodLabel->setWordWrap(true);
    layout->addWidget(methodLabel);

    auto* nodeNameLayout = new QHBoxLayout();
    nodeNameLayout->setStretch(0, 3);
    nodeNameLayout->setStretch(1, 7);
    nodeNameLayout->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText("输入节点名称");
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

    auto* fileNameLayout = new QHBoxLayout();
    fileNameLayout->setStretch(0, 3);
    fileNameLayout->setStretch(1, 7);
    fileNameLayout->addWidget(new QLabel("输出名规则："));
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setText(m_outputFileName);
    m_outputFileNameEdit->setPlaceholderText("支持 {InputName} 变量");
    connect(m_outputFileNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputFileNameEdit->text();
        if (m_outputFileName != text) {
            if (!confirmParameterChange()) {
                m_outputFileNameEdit->setText(m_outputFileName);
                return;
            }
            m_outputFileName = text;
            invalidateNodeData();
        }
    });
    fileNameLayout->addWidget(m_outputFileNameEdit);
    layout->addLayout(fileNameLayout);

    onSaveToProjectChanged(m_saveToProject ? Qt::Checked : Qt::Unchecked);
}

QStringList ClutterSuppressionNode::previewImagePaths() const
{
    QStringList validPaths;
    for (const QString& path : m_outputImagePaths) {
        if (QFileInfo::exists(path)) {
            validPaths << path;
        }
    }
    return validPaths;
}

void ClutterSuppressionNode::onSaveToProjectChanged(int state)
{
    m_saveToProject = (state == Qt::Checked);
    updateParameterWidgetsEnableState();
}

void ClutterSuppressionNode::stopExecution()
{
    if (m_task)
    {
        m_task->stop();
        return;
    }
}

void ClutterSuppressionNode::processAutomatically()
{
    if (m_task) {
        deferAutomaticCompletion();
        return;
    }

    if (isReady()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

bool ClutterSuppressionNode::isReady() const
{
    if (!m_inputData || m_inputData->filePath().isEmpty()) {
        return false;
    }

    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
    {
        return false;
    }

    if (!m_saveToProject || m_outputNodeName.trimmed().isEmpty()) {
        return false;
    }

    return true;
}

void ClutterSuppressionNode::execute()
{
    executeProcessing();
}

void ClutterSuppressionNode::executeProcessing()
{
	InSARLogManager::LogInfo("ClutterSuppressionNode", "executeProcessing started.");
    if (m_task)
    {
        m_task->stop();
        deferAutomaticCompletion();
        return;
    }

    if (!m_saveToProject) {
        onError(QStringLiteral("Clutter suppression results must be saved to the project before publication."));
        return;
    }

    if (!isReady()) {
        return;
    }

    setProgress(0);

    QStringList inputPaths = m_inputData->filePaths();
    QString outputNodeName = m_outputNodeName.trimmed().isEmpty() ? "ClutterSuppression" : m_outputNodeName.trimmed();
    QString baseFileName = m_outputFileName.trimmed();
    bool saveToProject = m_saveToProject;
    QString projPath = projectPath();
    QStringList outputPaths;
    QStringList fileNames;
    for (int i = 0; i < inputPaths.size(); ++i) {
        outputPaths.append(QDir::tempPath() + QString("/clutter_suppression_%1_%2.jpg").arg(i).arg(QDateTime::currentMSecsSinceEpoch()));
        
        QString originalName = QFileInfo(inputPaths[i]).baseName();
        QString name = baseFileName;
        
        QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
        name.replace(re, "{InputName}");
        
        if (name.contains("{InputName}")) {
            name.replace("{InputName}", originalName);
        } else {
            if (inputPaths.size() > 1) {
                name = QString("%1_%2").arg(baseFileName).arg(i + 1);
            }
        }
        
        if (name.trimmed().isEmpty()) {
            name = originalName + "_clutter";
        }
        
        fileNames.append(name);
    }
    
    m_savedOutputFiles = fileNames;
    m_preparedOutputPaths.clear();
    m_generatedOutputPaths.clear();
    if (saveToProject) {
        QString outputRoot = projPath;
        if (outputRoot.endsWith(".insar", Qt::CaseInsensitive)) {
            outputRoot = QFileInfo(outputRoot).absolutePath();
        }
        for (const QString& fileName : fileNames) {
            m_preparedOutputPaths.append(QDir(outputRoot).absoluteFilePath(
                outputNodeName + "/" + QFileInfo(fileName).completeBaseName() + ".png"));
        }

        QString transactionError;
        if (!NodeUtils::beginOutputTransaction(outputRoot, outputNodeName, m_preparedOutputPaths,
                                               inputPaths, m_outputTransaction, &transactionError, nullptr,
                                               NodeUtils::getProjectFilePath(_widget))) {
            onError(transactionError);
            return;
        }
        m_outputTransaction.executionRevision = executionRevision();
        QMap<QString, QString> descriptorProvenance;
        descriptorProvenance.insert(QStringLiteral("producer"), name());
        descriptorProvenance.insert(QStringLiteral("output_port"),
                                    QStringLiteral("clutter_suppression.output.suppressed_image"));
        if (!NodeUtils::setOutputTransactionProductDescriptor(
                m_outputTransaction, ProductDescriptor::create(
                    QStringLiteral("clutter_suppressed_image"), QStringLiteral("sat-explorer-product"), 1,
                    ProductState::Committed, name(), descriptorProvenance), &transactionError)) {
            onError(transactionError);
            return;
        }
        outputPaths.clear();
        for (const QString& finalPath : m_preparedOutputPaths) {
            outputPaths.append(QDir(outputRoot).absoluteFilePath(
                m_outputTransaction.stagingName + "/" + QFileInfo(finalPath).fileName()));
        }
    }
    m_outputImagePaths = outputPaths;

    m_isExecuting = true;

    ClutterSuppressionParameters workflowParameters;
    workflowParameters.method = ClutterSuppressionMethod::BM3D;
    auto* task = new BM3DEnhancementTask(
        EnhancementType::ClutterSuppression,
        inputPaths,
        outputPaths,
        !saveToProject,
        workflowParameters);
    m_task = task;

    setState(ExecutionState::Running);
    deferAutomaticCompletion();

    connect(task, &BM3DEnhancementTask::updateProcess, this, &ClutterSuppressionNode::onProgressUpdate, Qt::QueuedConnection);
    connect(task, &BM3DEnhancementTask::endProcess, this, &ClutterSuppressionNode::onProcessingFinished, Qt::QueuedConnection);
    connect(task, &BM3DEnhancementTask::errorProcess, this, &ClutterSuppressionNode::onError, Qt::QueuedConnection);
    connect(task, &BM3DEnhancementTask::cancelled, this, &ClutterSuppressionNode::onCancelled, Qt::QueuedConnection);
    connect(task, &BM3DEnhancementTask::askUserError, this, &ClutterSuppressionNode::onAskUserError, Qt::QueuedConnection);
    connect(task, &BM3DEnhancementTask::outputsGenerated, this,
        [this](const QStringList& outputPaths) { m_generatedOutputPaths = outputPaths; }, Qt::QueuedConnection);

    // The runnable is not auto-deleted by QThreadPool.  Delete it on its QObject
    // affinity thread after the node has processed the terminal signal.
    connect(task, &BM3DEnhancementTask::endProcess,
            task, &QObject::deleteLater, Qt::QueuedConnection);
    connect(task, &BM3DEnhancementTask::errorProcess,
            task, &QObject::deleteLater, Qt::QueuedConnection);
    connect(task, &BM3DEnhancementTask::cancelled,
            task, &QObject::deleteLater, Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(task);
    updateParameterWidgetsEnableState();
}

void ClutterSuppressionNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void ClutterSuppressionNode::onProcessingFinished()
{
    m_task = nullptr;
    m_isExecuting = false;
    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset();
        m_outputImagePaths.clear();
        m_generatedOutputPaths.clear();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        return;
    }

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }

    if (m_saveToProject) {
        QString transactionError;
        QStringList finalPaths;
        if (!projectXml() ||
            !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
            !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
            !NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
            !NodeUtils::prepareOutputTransactionMetadataCommit(
                m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
            onError(transactionError.isEmpty()
                        ? QStringLiteral("Clutter suppression output transaction validation failed.")
                        : transactionError);
            return;
        }

        auto* iface = NodeUtils::getProjectContext(_widget);
        const QString nodeName = m_outputNodeName.trimmed().isEmpty() ? QStringLiteral("ClutterSuppression") : m_outputNodeName.trimmed();
        NodeUtils::removeDataNodeFromProject(iface, nodeName, false, false);
        for (int i = 0; i < finalPaths.size(); ++i) {
            const QString relativePath = QString("/%1/%2").arg(nodeName, QFileInfo(finalPaths[i]).fileName());
            if (projectXml()->XMLFile_add_origin(nodeName.toStdString().c_str(),
                                                 m_savedOutputFiles[i].toStdString().c_str(),
                                                 relativePath.toStdString().c_str(),
                                                 "ClutterSuppression") < 0) {
                onError(QStringLiteral("Unable to update clutter suppression project metadata."));
                return;
            }
        }
        if (!NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
            !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
            onError(transactionError.isEmpty()
                        ? QStringLiteral("Clutter suppression output metadata was not produced.")
                        : transactionError);
            return;
        }
        NodeUtils::removeDataNodeFromProjectTree(iface, nodeName);
        publishResultsToProjectTree(m_savedOutputFiles, finalPaths);
        m_outputImagePaths = finalPaths;
    } else {
        m_outputImagePaths = m_generatedOutputPaths;
        if (m_outputImagePaths.isEmpty()) {
            onError(QStringLiteral("BM3D处理未生成可发布的输出。"));
            return;
        }
    }

    m_outputData = std::make_shared<ImageInfoData>(m_outputImagePaths);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);
    auto previewData = std::make_shared<ImageInfoData>(m_outputImagePaths);
    QMap<QString, QString> previewProvenance;
    previewProvenance.insert(QStringLiteral("producer"), name());
    previewProvenance.insert(QStringLiteral("output_port"),
                             QStringLiteral("clutter_suppression.output.preview"));
    previewData->setProductDescriptor(ProductDescriptor::create(
        QStringLiteral("preview"), QStringLiteral("sat-explorer-product"), 1,
        ProductState::Committed, name(), previewProvenance));
    setOutputData(1, previewData);

    m_isExecuting = false;
    updateParameterWidgetsEnableState();

    InSARLogManager::LogInfo("ClutterSuppressionNode", "executeProcessing completed.");
    finishExecution();
}

void ClutterSuppressionNode::onError(const QString& error)
{
    m_task = nullptr;
    m_isExecuting = false;
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_generatedOutputPaths.clear();
    m_outputData.reset();
    m_outputImagePaths.clear();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    
    m_isExecuting = false;
    updateParameterWidgetsEnableState();

}

void ClutterSuppressionNode::onCancelled()
{
    m_task = nullptr;
    m_isExecuting = false;
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    if (!m_saveToProject) {
        for (const QString& outputPath : m_outputImagePaths) QFile::remove(outputPath);
    }
    m_outputImagePaths.clear();
    m_outputData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }
    updateParameterWidgetsEnableState();
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void ClutterSuppressionNode::publishResultsToProjectTree(const QStringList& outputNames,
                                                         const QStringList& outputPaths)
{
    QStandardItemModel* model = projectModel();
    if (!model) return;

    QStandardItem* projectItem = model->findItems(projectName()).isEmpty() ? nullptr : model->findItems(projectName()).first();
    if (!projectItem) return;

    const QString nodeName = m_outputNodeName.trimmed().isEmpty() ? QStringLiteral("ClutterSuppression") : m_outputNodeName.trimmed();

    QStandardItem* dataNode = new QStandardItem(nodeName);
    dataNode->setIcon(QIcon(FOLDER_ICON));
    projectItem->appendRow(dataNode);
    for (int i = 0; i < outputPaths.size(); ++i) {
        QStandardItem* nameItem = new QStandardItem(outputNames[i]);
        nameItem->setIcon(QIcon(IMAGEDATA_ICON));
        nameItem->setToolTip(QStringLiteral("image"));
        QStandardItem* pathItem = new QStandardItem(outputPaths[i]);
        dataNode->appendRow({ nameItem, pathItem });
    }

    // Refresh tree
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* ClutterSuppressionNode::projectModel() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString ClutterSuppressionNode::projectPath() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectPath() : QString();
}

QString ClutterSuppressionNode::projectName() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* ClutterSuppressionNode::projectXml() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

QJsonObject ClutterSuppressionNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["saveToProject"] = m_saveToProject;
    modelJson["outputNodeName"] = m_outputNodeName;
    modelJson["outputFileName"] = m_outputFileName;

    QJsonArray outputFiles;
    if (m_outputData) {
        for (const QString& path : m_outputData->filePaths()) {
            outputFiles.append(QFileInfo(path).fileName());
        }
    }
    modelJson["outputFiles"] = outputFiles;

    return modelJson;
}

void ClutterSuppressionNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_saveToProject = json["saveToProject"].toBool(true);
    m_outputNodeName = json["outputNodeName"].toString();
    m_outputFileName = json["outputFileName"].toString();

    m_savedOutputFiles.clear();
    if (json.contains("outputFiles")) {
        QJsonArray arr = json["outputFiles"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            m_savedOutputFiles.append(arr[i].toString());
        }
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_saveToProjectCheckBox) {
        m_saveToProjectCheckBox->setChecked(m_saveToProject);
    }
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }
    if (m_outputFileNameEdit) {
        m_outputFileNameEdit->setText(m_outputFileName);
    }
}

QString ClutterSuppressionNode::generateOutputFileName() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        return QString();
    }

    QFileInfo fi(m_inputData->filePaths().first());
    QString baseName = fi.completeBaseName();
    return QStringLiteral("%1_clutter").arg(baseName);
}

void ClutterSuppressionNode::onAskUserError(quint64 requestId, const QString& message)
{
    bool skip = false;
    if (isAutomaticExecutionObsolete() || executionState() != ExecutionState::Running) {
        if (m_task) {
            m_task->resolveErrorDecision(requestId, skip);
        }
        return;
    }
    QMessageBox::StandardButton reply = QMessageBox::question(
        nullptr,
        QStringLiteral("错误"), // 错误
        message,
        QMessageBox::Yes | QMessageBox::No
    );
    skip = (reply == QMessageBox::Yes);
    if (m_task) {
        m_task->resolveErrorDecision(requestId, skip);
    }
}

bool ClutterSuppressionNode::validateAndRestoreOutput()
{
    if (!m_saveToProject)
        return false;

    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputRoot = projectPath();
    if (outputRoot.endsWith(".insar", Qt::CaseInsensitive)) {
        outputRoot = QFileInfo(outputRoot).absolutePath();
    }
    QStringList outputPaths;
    ProductDescriptor::Ptr descriptor;
    QString descriptorError;
    if (!NodeUtils::loadCommittedOutputManifest(outputRoot, nodeName, outputPaths) ||
        !NodeUtils::loadCommittedOutputProductDescriptor(outputRoot, nodeName, descriptor, &descriptorError) ||
        !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted) {
        return false;
    }

    m_outputImagePaths = outputPaths;
    m_savedOutputFiles.clear();
    for (const QString& outputPath : outputPaths) {
        m_savedOutputFiles.append(QFileInfo(outputPath).completeBaseName());
    }
    m_outputData = std::make_shared<ImageInfoData>(outputPaths);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);
    auto previewData = std::make_shared<ImageInfoData>(outputPaths);
    QMap<QString, QString> previewProvenance;
    previewProvenance.insert(QStringLiteral("producer"), name());
    previewProvenance.insert(QStringLiteral("output_port"),
                             QStringLiteral("clutter_suppression.output.preview"));
    previewData->setProductDescriptor(ProductDescriptor::create(
        QStringLiteral("preview"), QStringLiteral("sat-explorer-product"), 1,
        ProductState::Committed, name(), previewProvenance));
    setOutputData(1, previewData);
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    return true;
}

void ClutterSuppressionNode::updateParameterWidgetsEnableState()
{
    bool hasInput = (m_inputData && !m_inputData->filePaths().isEmpty());
    bool isExec = m_isExecuting;
    bool enableWidgets = hasInput && !isExec;

    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setEnabled(enableWidgets);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets && m_saveToProject);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setEnabled(enableWidgets && m_saveToProject);
}

} // namespace QtNodes
