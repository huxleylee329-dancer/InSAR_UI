#include "SpeckleDenoiseNode.h"
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
#include <QDebug>
#include <QMenu>
#include <QMessageBox>

#include "InSARLogManager.h"

namespace QtNodes {

SpeckleDenoiseNode::SpeckleDenoiseNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputImageLabel(nullptr)
    , m_saveToProjectCheckBox(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_saveToProject(true)
    , m_outputNodeName("Denoise")
    , m_outputFileName("{InputName}_denoised")
    , m_task(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

SpeckleDenoiseNode::~SpeckleDenoiseNode()
{
    stopExecution();


}

unsigned int SpeckleDenoiseNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType SpeckleDenoiseNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return NodeDataType{"image_info", "Image Info"};
}

bool SpeckleDenoiseNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString SpeckleDenoiseNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return "输入图像";
    } else {
        if (portIndex == 0)
            return "成果 *";
        else if (portIndex == 1)
            return "预览 ?";
    }
    return QString();
}

bool SpeckleDenoiseNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void SpeckleDenoiseNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImageInfoData>(data);

    if (!m_inputData || m_inputData->filePath().isEmpty()) {
        m_outputData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
    }

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
        m_outputFileName = "{InputName}_denoised";
        if (m_outputFileNameEdit) {
            m_outputFileNameEdit->setText(m_outputFileName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);
}

std::shared_ptr<NodeData> SpeckleDenoiseNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
}

QWidget* SpeckleDenoiseNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void SpeckleDenoiseNode::createWidget()
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

    // 目标文件名 [3:7] -> 输出名规则
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

QStringList SpeckleDenoiseNode::previewImagePaths() const
{
    QStringList validPaths;
    for (const QString& path : m_outputImagePaths) {
        if (QFileInfo::exists(path)) {
            validPaths << path;
        }
    }
    return validPaths;
}

void SpeckleDenoiseNode::onSaveToProjectChanged(int state)
{
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setEnabled(state == Qt::Checked);
    }
    if (m_outputFileNameEdit) {
        m_outputFileNameEdit->setEnabled(state == Qt::Checked);
    }
}

void SpeckleDenoiseNode::stopExecution()
{
    if (m_task)
    {
        m_task->stop();
    }
    setState(ExecutionState::Stopped);
}

void SpeckleDenoiseNode::processAutomatically()
{
    // CRITICAL: Prevent duplicate execution - check if already processing
    // m_thread being non-null means execution is in progress
    if (m_task) {
        return;
    }

    if (isReady()) {
        // Set placeholder output data BEFORE starting processing
        // This prevents base class from resetting state to Idle
        if (!m_outputData) {
            m_outputData = std::make_shared<ImageInfoData>("");
            setOutputData(0, m_outputData);
            setOutputData(1, m_outputData);

        }
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

bool SpeckleDenoiseNode::isReady() const
{
    if (!m_inputData || m_inputData->filePath().isEmpty()) {
        return false;
    }

    // Always require project context (denoise should only run with project open)
    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
    {
        return false;
    }

    // Use member variables for readiness check
    if (m_saveToProject) {
        if (m_outputNodeName.trimmed().isEmpty()) {
            return false;
        }
    }

    return true;
}

void SpeckleDenoiseNode::execute()
{
    executeProcessing();
}

void SpeckleDenoiseNode::executeProcessing()
{
	InSARLogManager::LogInfo("SpeckleDenoiseNode", "executeProcessing started.");
 
    // CRITICAL: Clean up existing threads FIRST - before any state change
    // This prevents duplicate execution if setInData is called multiple times
    if (m_task)
    {
        m_task->stop();
        m_task = nullptr;
    }

    if (!isReady()) {
        return;
    }

    setProgress(0);

    QStringList inputPaths = m_inputData->filePaths();
    QString outputNodeName = m_outputNodeName.trimmed().isEmpty() ? "Denoise" : m_outputNodeName.trimmed();
    QString baseFileName = m_outputFileName.trimmed();
    bool saveToProject = m_saveToProject;
    QString projPath = projectPath();
    QString projName = projectName();
    QStandardItemModel* model = projectModel();
    XMLFile* projectXmlPtr = projectXml();
    
    QStringList outputPaths;
    QStringList fileNames;
    for (int i = 0; i < inputPaths.size(); ++i) {
        outputPaths.append(QDir::tempPath() + QString("/speckle_denoise_%1_%2.jpg").arg(i).arg(QDateTime::currentMSecsSinceEpoch()));
        
        QString originalName = QFileInfo(inputPaths[i]).baseName();
        QString name = baseFileName;
        
        // Normalize full-width brackets (common typo in Chinese IME)
        QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
        name.replace(re, "{InputName}");
        
        if (name.contains("{InputName}")) {
            name.replace("{InputName}", originalName);
        } else {
            // Fallback if macro is deleted
            if (inputPaths.size() > 1) {
                name = QString("%1_%2").arg(baseFileName).arg(i + 1);
            }
        }
        
        // Failsafe for empty name
        if (name.trimmed().isEmpty()) {
            name = originalName + "_denoised";
        }
        
        fileNames.append(name);
    }
    
    // Store the resolved names and paths to member variables so onProcessingFinished can use them
    m_savedOutputFiles = fileNames;
    m_outputImagePaths = outputPaths;

    // 清理旧数据，防止反复执行导致数据累加
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), outputNodeName);

    m_task = new SpeckleDenoiseTask(inputPaths, outputPaths, outputNodeName, fileNames, projPath, projName, model, saveToProject, projectXmlPtr);

    connect(m_task, &SpeckleDenoiseTask::updateProcess, this, &SpeckleDenoiseNode::onProgressUpdate, Qt::QueuedConnection);
    connect(m_task, &SpeckleDenoiseTask::endProcess, this, &SpeckleDenoiseNode::onProcessingFinished, Qt::QueuedConnection);
    connect(m_task, &SpeckleDenoiseTask::errorProcess, this, &SpeckleDenoiseNode::onError, Qt::QueuedConnection);
    connect(m_task, &SpeckleDenoiseTask::sendModel, this, &SpeckleDenoiseNode::onModelUpdated, Qt::QueuedConnection);
    connect(m_task, &SpeckleDenoiseTask::askUserError, this, &SpeckleDenoiseNode::onAskUserError, Qt::BlockingQueuedConnection);

    QThreadPool::globalInstance()->start(m_task);

    m_outputNodeNameEdit->setEnabled(false);
    m_outputFileNameEdit->setEnabled(false);
    m_saveToProjectCheckBox->setEnabled(false);
}

void SpeckleDenoiseNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void SpeckleDenoiseNode::onProcessingFinished()
{


    // Determine the result path
    if (m_saveToProject) {
        m_outputImagePaths.clear(); // Only clear temp paths if we are saving to project
        
        // Find the saved file path in project
        QString nodeName = m_outputNodeName.trimmed();
        if (nodeName.isEmpty()) nodeName = "Denoise"; // Default consistent with executeProcessing
        
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

    } else {

    }

    m_outputData = std::make_shared<ImageInfoData>(m_outputImagePaths);
    setOutputData(0, m_outputData);
    setOutputData(1, m_outputData);

    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setEnabled(true);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(m_saveToProject);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setEnabled(m_saveToProject);

    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);

    m_task = nullptr;

    InSARLogManager::LogInfo("SpeckleDenoiseNode", "executeProcessing completed.");
    finishExecution();
}

void SpeckleDenoiseNode::onError(const QString& error)
{
    InSARLogManager::LogError("SpeckleDenoiseNode", error);
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    
    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setEnabled(true);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(m_saveToProject);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setEnabled(m_saveToProject);

    m_task = nullptr;

    // Clear placeholder output data on error
    m_outputData.reset();

}

void SpeckleDenoiseNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* SpeckleDenoiseNode::projectModel() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString SpeckleDenoiseNode::projectPath() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectPath() : QString();
}

QString SpeckleDenoiseNode::projectName() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* SpeckleDenoiseNode::projectXml() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}


QJsonObject SpeckleDenoiseNode::save() const
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

void SpeckleDenoiseNode::load(QJsonObject const &json)
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

QString SpeckleDenoiseNode::generateOutputFileName() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        return QString();
    }

    QFileInfo fi(m_inputData->filePaths().first());
    QString baseName = fi.completeBaseName();
    return QStringLiteral("%1_denoised").arg(baseName);
}

void SpeckleDenoiseNode::onAskUserError(const QString& message, bool* skip)
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        nullptr,
        QStringLiteral("错误"), // 错误
        message,
        QMessageBox::Yes | QMessageBox::No
    );
    *skip = (reply == QMessageBox::Yes);
}

bool SpeckleDenoiseNode::validateAndRestoreOutput()
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
            return true;
        }
    }

    // Fallback for older project files
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
        return true;
    }

    return false;
}

} // namespace QtNodes
