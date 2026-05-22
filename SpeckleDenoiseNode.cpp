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

namespace QtNodes {

SpeckleDenoiseNode::SpeckleDenoiseNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputImageLabel(nullptr)
    , m_saveToProjectCheckBox(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_statusLabel(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_saveToProject(true)
    , m_thread(nullptr)
    , m_workerThread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

SpeckleDenoiseNode::~SpeckleDenoiseNode()
{
    stopExecution();

    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

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
        QFileInfo fi(m_inputData->filePath());
        m_outputFileName = fi.baseName();
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

    m_inputImageLabel = new QLabel("");
    m_inputImageLabel->setWordWrap(true);
    if (m_inputData && !m_inputData->filePath().isEmpty()) {
        QFileInfo fi(m_inputData->filePath());
        m_inputImageLabel->setText(fi.fileName());
    }
    layout->addWidget(m_inputImageLabel);

    m_saveToProjectCheckBox = new QCheckBox("保存到项目树");
    m_saveToProjectCheckBox->setChecked(m_saveToProject);
    connect(m_saveToProjectCheckBox, &QCheckBox::stateChanged, this, [this](int state) {
        m_saveToProject = (state == Qt::Checked);
        onSaveToProjectChanged(state);
    });
    layout->addWidget(m_saveToProjectCheckBox);

    auto* nodeNameLayout = new QHBoxLayout();
    nodeNameLayout->setStretch(0, 3);
    nodeNameLayout->setStretch(1, 7);
    nodeNameLayout->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText("输入节点名称");
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_outputNodeName = text; });
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    // 目标文件名 [3:7]
    auto* fileNameLayout = new QHBoxLayout();
    fileNameLayout->setStretch(0, 3);
    fileNameLayout->setStretch(1, 7);
    fileNameLayout->addWidget(new QLabel("目标文件名："));
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setText(m_outputFileName);
    m_outputFileNameEdit->setPlaceholderText("自动生成或手动输入");
    connect(m_outputFileNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_outputFileName = text; });
    fileNameLayout->addWidget(m_outputFileNameEdit);
    layout->addLayout(fileNameLayout);

    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    onSaveToProjectChanged(m_saveToProject ? Qt::Checked : Qt::Unchecked);
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
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
    setState(ExecutionState::Stopped);
}

void SpeckleDenoiseNode::processAutomatically()
{
    // CRITICAL: Prevent duplicate execution - check if already processing
    // m_thread being non-null means execution is in progress
    if (m_thread || m_workerThread) {
        return;
    }

    if (isReady()) {
        // Set placeholder output data BEFORE starting processing
        // This prevents base class from resetting state to Idle
        if (!m_outputData) {
            m_outputData = std::make_shared<ImageInfoData>("");
            setOutputData(0, m_outputData);
            setOutputData(1, m_outputData);
            qDebug() << "[SpeckleDenoiseNode] Set placeholder output data to prevent Idle reset";
        }
        executeProcessing();
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
    qDebug() << "[SpeckleDenoiseNode] executeProcessing START";

    // CRITICAL: Clean up existing threads FIRST - before any state change
    // This prevents duplicate execution if setInData is called multiple times
    if (m_thread || m_workerThread)
    {
        qDebug() << "[SpeckleDenoiseNode] Cleaning up existing threads";
        if (m_thread && m_thread->isRunning())
        {
            m_thread->quit();
            m_thread->wait();
        }
        if (m_thread) {
            m_thread->deleteLater();
            m_thread = nullptr;
        }
        if (m_workerThread) {
            m_workerThread->deleteLater();
            m_workerThread = nullptr;
        }
        // Disconnect all signals to prevent stale connections
        disconnect(this, &SpeckleDenoiseNode::startSpeckleDenoise, nullptr, nullptr);
    }

    if (!isReady()) {
        qDebug() << "[SpeckleDenoiseNode] Not ready, aborting";
        if (m_statusLabel) {
            m_statusLabel->setText("状态：未准备好");
        }
        return;
    }

    setProgress(0);
    if (m_statusLabel) {
        m_statusLabel->setText("状态：正在初始化...");
    }

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
        if (inputPaths.size() == 1) {
            fileNames.append(baseFileName);
        } else {
            QString name = baseFileName.isEmpty() ? QFileInfo(inputPaths[i]).baseName() + "_denoised" : QString("%1_%2").arg(baseFileName).arg(i+1);
            fileNames.append(name);
        }
    }

    qDebug() << "[SpeckleDenoiseNode] Starting with params:"
             << "\n  inputPaths:" << inputPaths
             << "\n  outputNodeName:" << outputNodeName
             << "\n  baseFileName:" << baseFileName
             << "\n  saveToProject:" << saveToProject
             << "\n  projPath:" << projPath
             << "\n  projName:" << projName
             << "\n  temp outputPaths count:" << outputPaths.size();

    // Create thread
    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    // Connect signals - capture ALL values by value to avoid race conditions
    connect(m_thread, &QThread::started, [this, inputPaths, outputPaths, outputNodeName, fileNames, projPath, projName, model, saveToProject, projectXmlPtr]() {
        qDebug() << "[SpeckleDenoiseNode] Thread started, emitting startSpeckleDenoise";
        Q_EMIT startSpeckleDenoise(inputPaths, outputPaths, outputNodeName, fileNames, projPath, projName, model, saveToProject, projectXmlPtr);
    });
    connect(this, &SpeckleDenoiseNode::startSpeckleDenoise, m_workerThread, &MyThread::Speckle_Denoise, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::updateProcess, this, &SpeckleDenoiseNode::onProgressUpdate, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::endProcess, this, &SpeckleDenoiseNode::onProcessingFinished, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::errorProcess, this, &SpeckleDenoiseNode::onError, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::sendModel, this, &SpeckleDenoiseNode::onModelUpdated, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::askUserError, this, &SpeckleDenoiseNode::onAskUserError, Qt::BlockingQueuedConnection);

    // Start thread
    m_thread->start();
    qDebug() << "[SpeckleDenoiseNode] Thread started, worker created";
    m_outputNodeNameEdit->setEnabled(false);
    m_outputFileNameEdit->setEnabled(false);
    m_saveToProjectCheckBox->setEnabled(false);
}

void SpeckleDenoiseNode::onProgressUpdate(int progress, const QString& message)
{
    setProgress(progress);
    if (m_statusLabel) {
        m_statusLabel->setText("状态：" + message);
    }
}

void SpeckleDenoiseNode::onProcessingFinished()
{
    qDebug() << "[SpeckleDenoiseNode] onProcessingFinished called";

    m_outputImagePaths.clear();
    // Determine the result path
    if (m_saveToProject) {
        // Find the saved file path in project
        QString nodeName = m_outputNodeName.trimmed();
        if (nodeName.isEmpty()) nodeName = "Denoise"; // Default consistent with executeProcessing

        QString projDirStr = projectPath();
        if (projDirStr.endsWith(".insar", Qt::CaseInsensitive)) {
            projDirStr = QFileInfo(projDirStr).absolutePath();
        }
        
        QStringList inputPaths = m_inputData->filePaths();
        QString baseFileName = m_outputFileName.trimmed();
        for (int i = 0; i < inputPaths.size(); ++i) {
            QString finalFileName;
            if (inputPaths.size() == 1) {
                if (baseFileName.isEmpty()) {
                    finalFileName = QFileInfo(inputPaths[i]).baseName() + "_denoised.png";
                } else {
                    finalFileName = baseFileName.endsWith(".png") ? baseFileName : baseFileName + ".png";
                }
            } else {
                QString name = baseFileName.isEmpty() ? QFileInfo(inputPaths[i]).baseName() + "_denoised" : QString("%1_%2").arg(baseFileName).arg(i+1);
                finalFileName = name + ".png";
            }
            m_outputImagePaths.append(projDirStr + "/" + nodeName + "/" + finalFileName);
        }
        qDebug() << "[SpeckleDenoiseNode] Output image paths (using node folder):" << m_outputImagePaths;
    } else {
        // Output path was set in executeProcessing (temp path)
    }

    m_outputData = std::make_shared<ImageInfoData>(m_outputImagePaths);
    setOutputData(0, m_outputData);
    setOutputData(1, m_outputData);

    if (m_statusLabel) {
        m_statusLabel->setText("状态：完成");
    }

    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setEnabled(true);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(m_saveToProject);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setEnabled(m_saveToProject);

    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);

    // Clean up threads
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    qDebug() << "[SpeckleDenoiseNode] Processing finished, calling finishExecution";
    finishExecution();
}

void SpeckleDenoiseNode::onError(const QString& error)
{
    qDebug() << "[SpeckleDenoiseNode] onError called:" << error;
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    if (m_statusLabel) {
        m_statusLabel->setText("状态：错误 - " + error);
    }
    
    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setEnabled(true);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(m_saveToProject);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setEnabled(m_saveToProject);

    // Clean up threads
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    // Clear placeholder output data on error
    m_outputData.reset();
    qDebug() << "[SpeckleDenoiseNode] Error handling complete";
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

    return modelJson;
}

void SpeckleDenoiseNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_saveToProject = json["saveToProject"].toBool(true);
    m_outputNodeName = json["outputNodeName"].toString();
    m_outputFileName = json["outputFileName"].toString();

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

    QString finalFileName;
    if (m_outputFileName.isEmpty()) {
        // 需要输入数据才能计算默认文件名，返回false让用户手动执行
        return false;
    } else {
        if (QFileInfo(m_outputFileName).suffix().isEmpty()) {
            finalFileName = m_outputFileName + ".png";
        } else {
            finalFileName = m_outputFileName;
        }
    }

    QString outputPath = projDirStr + "/" + nodeName + "/" + finalFileName;

    if (QFile::exists(outputPath)) {
        m_outputData = std::make_shared<ImageInfoData>(QStringList() << outputPath);
        setOutputData(0, m_outputData);
        setOutputData(1, m_outputData);
        return true;
    }

    return false;
}

} // namespace QtNodes

