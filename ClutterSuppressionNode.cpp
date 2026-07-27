#include "InSARLogManager.h"
#include "ClutterSuppressionNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
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
    m_inputData = std::dynamic_pointer_cast<ImageInfoData>(data);

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

    if (m_saveToProject) {
        if (m_outputNodeName.trimmed().isEmpty()) {
            return false;
        }
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

    if (!isReady()) {
        return;
    }

    setProgress(0);

    QStringList inputPaths = m_inputData->filePaths();
    QString outputNodeName = m_outputNodeName.trimmed().isEmpty() ? "ClutterSuppression" : m_outputNodeName.trimmed();
    QString baseFileName = m_outputFileName.trimmed();
    bool saveToProject = m_saveToProject;
    QString projPath = projectPath();
    QString projName = projectName();
    
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
    m_outputImagePaths = outputPaths;

    // 清理旧数据，防止反复执行导致数据累加
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), outputNodeName);

    m_isExecuting = true;
    m_task = new BM3DEnhancementTask(EnhancementType::ClutterSuppression, inputPaths, outputPaths, outputNodeName, fileNames, projPath, projName, saveToProject);

    setState(ExecutionState::Running);
    deferAutomaticCompletion();

    connect(m_task, &BM3DEnhancementTask::updateProcess, this, &ClutterSuppressionNode::onProgressUpdate, Qt::QueuedConnection);
    connect(m_task, &BM3DEnhancementTask::endProcess, this, &ClutterSuppressionNode::onProcessingFinished, Qt::QueuedConnection);
    connect(m_task, &BM3DEnhancementTask::errorProcess, this, &ClutterSuppressionNode::onError, Qt::QueuedConnection);
    connect(m_task, &BM3DEnhancementTask::cancelled, this, &ClutterSuppressionNode::onCancelled, Qt::QueuedConnection);
    connect(m_task, &BM3DEnhancementTask::askUserError, this, &ClutterSuppressionNode::onAskUserError, Qt::QueuedConnection);
    connect(m_task, &BM3DEnhancementTask::saveImageToProjectRequested, this, &ClutterSuppressionNode::onSaveImageToProjectRequested, Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(m_task);
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
        return;
    }

    if (m_saveToProject) {
        m_outputImagePaths.clear();
        
        QString nodeName = m_outputNodeName.trimmed();
        if (nodeName.isEmpty()) nodeName = "ClutterSuppression";
        
        QString projDirStr = projectPath();
        if (projDirStr.endsWith(".insar", Qt::CaseInsensitive)) {
            projDirStr = QFileInfo(projDirStr).absolutePath();
        }
        
        for (int i = 0; i < m_savedOutputFiles.size(); ++i) {
            QString finalFileName = m_savedOutputFiles[i];
            if (!finalFileName.endsWith(".png", Qt::CaseInsensitive)) {
                finalFileName += ".png";
            }
            m_outputImagePaths.append(projDirStr + "/" + nodeName + "/" + finalFileName);
        }
    }

    m_outputData = std::make_shared<ImageInfoData>(m_outputImagePaths);
    setOutputData(0, m_outputData);
    setOutputData(1, m_outputData);

    m_isExecuting = false;
    updateParameterWidgetsEnableState();

    InSARLogManager::LogInfo("ClutterSuppressionNode", "executeProcessing completed.");
    finishExecution();
}

void ClutterSuppressionNode::onError(const QString& error)
{
    m_task = nullptr;
    m_isExecuting = false;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    
    m_isExecuting = false;
    updateParameterWidgetsEnableState();

    m_outputData.reset();
    m_outputImagePaths.clear();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
}

void ClutterSuppressionNode::onCancelled()
{
    m_task = nullptr;
    m_isExecuting = false;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    for (const QString& outputPath : m_outputImagePaths) QFile::remove(outputPath);
    if (m_saveToProject) {
        const QString nodeName = m_outputNodeName.trimmed().isEmpty() ? QStringLiteral("ClutterSuppression") : m_outputNodeName.trimmed();
        QString outputRoot = projectPath();
        if (outputRoot.endsWith(".insar", Qt::CaseInsensitive)) {
            outputRoot = QFileInfo(outputRoot).absolutePath();
        }
        QDir(outputRoot + "/" + nodeName).removeRecursively();
    }
    NodeUtils::removeDataNodeFromProject(
        NodeUtils::getProjectContext(_widget),
        m_outputNodeName.trimmed().isEmpty() ? QStringLiteral("ClutterSuppression") : m_outputNodeName.trimmed());
    m_outputImagePaths.clear();
    m_outputData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    updateParameterWidgetsEnableState();
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void ClutterSuppressionNode::onSaveImageToProjectRequested(
    const QString& projectName,
    const QString& nodeName,
    const QString& displayName,
    const QString& finalPath,
    const QString& tag,
    const QString& finalFileName
)
{
    if (isAutomaticExecutionObsolete()) return;

    QStandardItemModel* model = projectModel();
    if (!model) return;

    QStandardItem* projectItem = model->findItems(projectName).isEmpty() ? nullptr : model->findItems(projectName).first();
    if (!projectItem) return;

    QStandardItem* dataNode = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        if (projectItem->child(i, 0)->text() == nodeName) {
            dataNode = projectItem->child(i, 0);
            break;
        }
    }
    if (!dataNode) {
        dataNode = new QStandardItem(nodeName);
        dataNode->setIcon(QIcon(FOLDER_ICON));
        projectItem->appendRow(dataNode);
    }

    QStandardItem* item_img = nullptr;
    for (int j = 0; j < dataNode->rowCount(); j++) {
        if (dataNode->child(j, 0)->text() == displayName) {
            item_img = dataNode->child(j, 0);
            break;
        }
    }

    if (!item_img) {
        QStandardItem* nameItem = new QStandardItem(displayName);
        nameItem->setIcon(QIcon(IMAGEDATA_ICON));
        nameItem->setToolTip(QStringLiteral("image"));
        QStandardItem* pathItem = new QStandardItem(finalPath);
        dataNode->appendRow({ nameItem, pathItem });

        // Update XML for persistence
        if (auto* iface = NodeUtils::getProjectContext(_widget)) {
            QString relativePath = "/" + nodeName + "/" + finalFileName;
            NodeUtils::addOriginNodeToProjectXml(iface, nodeName, displayName, relativePath, tag);
        }
    } else {
        dataNode->setChild(item_img->row(), 1, new QStandardItem(finalPath));
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

    QString projDirStr = projectPath();
    if (projDirStr.endsWith(".insar", Qt::CaseInsensitive)) {
        projDirStr = QFileInfo(projDirStr).absolutePath();
    }

    if (!m_savedOutputFiles.isEmpty()) {
        QStringList validPaths;
        for (const QString& fileName : m_savedOutputFiles) {
            QString finalFileName = fileName;
            if (!finalFileName.endsWith(".png", Qt::CaseInsensitive)) {
                finalFileName += ".png";
            }
            QString outputPath = projDirStr + "/" + nodeName + "/" + finalFileName;
            if (QFile::exists(outputPath)) {
                validPaths.append(outputPath);
            }
        }

        if (!validPaths.isEmpty() && validPaths.size() == m_savedOutputFiles.size()) {
            m_outputImagePaths = validPaths;
            m_outputData = std::make_shared<ImageInfoData>(validPaths);
            setOutputData(0, m_outputData);
            setOutputData(1, m_outputData);
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
            return true;
        }
    }

    QString finalFileName;
    if (m_outputFileName.isEmpty()) {
        return false;
    } else {
        QString resolvedFileName = m_outputFileName;
        if (m_inputData && !m_inputData->filePaths().isEmpty()) {
            QString originalName = QFileInfo(m_inputData->filePaths().first()).baseName();
            QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
            resolvedFileName.replace(re, originalName);
        }
        if (QFileInfo(resolvedFileName).suffix().isEmpty()) {
            finalFileName = resolvedFileName + ".png";
        } else {
            finalFileName = resolvedFileName;
        }
    }

    QString outputPath = projDirStr + "/" + nodeName + "/" + finalFileName;

    if (QFile::exists(outputPath)) {
        m_outputImagePaths.clear();
        m_outputImagePaths.append(outputPath);
        m_outputData = std::make_shared<ImageInfoData>(QStringList() << outputPath);
        setOutputData(0, m_outputData);
        setOutputData(1, m_outputData);
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
        return true;
    }

    return false;
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
