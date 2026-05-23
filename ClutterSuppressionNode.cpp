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
    , m_statusLabel(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_thread(nullptr)
    , m_workerThread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

ClutterSuppressionNode::~ClutterSuppressionNode()
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
        return "输入图像";
    } else {
        if (portIndex == 0)
            return "成果 *";
        else if (portIndex == 1)
            return "预览 ?";
    }
    return QString();
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

    if (m_inputData && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty()) {
        m_outputNodeNameEdit->setText(generateOutputFileName());
    }

    if (m_inputData && m_outputFileNameEdit && m_outputFileNameEdit->text().isEmpty()) {
        QFileInfo fi(m_inputData->filePath());
        m_outputFileNameEdit->setText(fi.baseName());
        m_outputFileName = fi.baseName();
    }

    ExecutableNodeDelegateModel::setInData(data, port);
}

std::shared_ptr<NodeData> ClutterSuppressionNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
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

    m_inputImageLabel = new QLabel("");
    m_inputImageLabel->setWordWrap(true);
    layout->addWidget(m_inputImageLabel);

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
    m_outputNodeNameEdit->setPlaceholderText("输入节点名称");
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

    auto* fileNameLayout = new QHBoxLayout();
    fileNameLayout->setStretch(0, 3);
    fileNameLayout->setStretch(1, 7);
    fileNameLayout->addWidget(new QLabel("目标文件名："));
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setPlaceholderText("自动生成或手动输入");
    m_outputFileNameEdit->setText(m_outputFileName);
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

    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    onSaveToProjectChanged(m_saveToProjectCheckBox->checkState());
}

void ClutterSuppressionNode::onSaveToProjectChanged(int state)
{
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setEnabled(state == Qt::Checked);
    }
    if (m_outputFileNameEdit) {
        m_outputFileNameEdit->setEnabled(state == Qt::Checked);
    }
}

void ClutterSuppressionNode::stopExecution()
{
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
    setState(ExecutionState::Stopped);
}

void ClutterSuppressionNode::processAutomatically()
{
    if (m_thread || m_workerThread) {
        return;
    }

    if (isReady()) {
        if (!m_outputData) {
            m_outputData = std::make_shared<ImageInfoData>("");
            setOutputData(0, m_outputData);
            setOutputData(1, m_outputData);
        }
        executeProcessing();
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

    if (m_saveToProjectCheckBox && m_saveToProjectCheckBox->isChecked()) {
        if (m_outputNodeNameEdit && m_outputNodeNameEdit->text().trimmed().isEmpty()) {
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
    if (m_thread || m_workerThread)
    {
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
        disconnect(this, &ClutterSuppressionNode::startClutterSuppression, nullptr, nullptr);
    }

    if (!isReady()) {
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
    QString outputNodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : "ClutterSuppression";
    QString baseFileName = m_outputFileNameEdit ? m_outputFileNameEdit->text().trimmed() : QString();
    bool saveToProject = m_saveToProjectCheckBox ? m_saveToProjectCheckBox->isChecked() : true;
    QString projPath = projectPath();
    QString projName = projectName();
    QStandardItemModel* model = projectModel();
    XMLFile* projectXmlPtr = projectXml();
    
    QStringList outputPaths;
    QStringList fileNames;
    for (int i = 0; i < inputPaths.size(); ++i) {
        outputPaths.append(QDir::tempPath() + QString("/clutter_suppression_%1_%2.jpg").arg(i).arg(QDateTime::currentMSecsSinceEpoch()));
        if (inputPaths.size() == 1) {
            fileNames.append(baseFileName);
        } else {
            QString name = baseFileName.isEmpty() ? QFileInfo(inputPaths[i]).baseName() + "_clutter" : QString("%1_%2").arg(baseFileName).arg(i+1);
            fileNames.append(name);
        }
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::started, [this, inputPaths, outputPaths, outputNodeName, fileNames, projPath, projName, model, saveToProject, projectXmlPtr]() {
        Q_EMIT startClutterSuppression(inputPaths, outputPaths, outputNodeName, fileNames, projPath, projName, model, saveToProject, projectXmlPtr);
    });
    connect(this, &ClutterSuppressionNode::startClutterSuppression, m_workerThread, &MyThread::Clutter_Suppression, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::updateProcess, this, &ClutterSuppressionNode::onProgressUpdate, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::endProcess, this, &ClutterSuppressionNode::onProcessingFinished, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::errorProcess, this, &ClutterSuppressionNode::onError, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::sendModel, this, &ClutterSuppressionNode::onModelUpdated, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::askUserError, this, &ClutterSuppressionNode::onAskUserError, Qt::BlockingQueuedConnection);

    m_thread->start();
    m_outputNodeNameEdit->setEnabled(false);
    m_outputFileNameEdit->setEnabled(false);
    m_saveToProjectCheckBox->setEnabled(false);
}

void ClutterSuppressionNode::onProgressUpdate(int progress, const QString& message)
{
    setProgress(progress);
    if (m_statusLabel) {
        m_statusLabel->setText("状态：" + message);
    }
}

void ClutterSuppressionNode::onProcessingFinished()
{
    m_outputImagePaths.clear();
    if (m_saveToProjectCheckBox->isChecked()) {
        QString nodeName = m_outputNodeNameEdit->text().trimmed();
        QString projDirStr = projectPath();
        if (projDirStr.endsWith(".insar", Qt::CaseInsensitive)) {
            projDirStr = QFileInfo(projDirStr).absolutePath();
        }
        QStringList inputPaths = m_inputData->filePaths();
        QString baseFileName = m_outputFileNameEdit ? m_outputFileNameEdit->text().trimmed() : QString();
        for (int i = 0; i < inputPaths.size(); ++i) {
            QString finalFileName;
            if (inputPaths.size() == 1) {
                if (baseFileName.isEmpty()) {
                    finalFileName = QFileInfo(inputPaths[i]).baseName() + "_clutter.png";
                } else {
                    finalFileName = baseFileName.endsWith(".png") ? baseFileName : baseFileName + ".png";
                }
            } else {
                QString name = baseFileName.isEmpty() ? QFileInfo(inputPaths[i]).baseName() + "_clutter" : QString("%1_%2").arg(baseFileName).arg(i+1);
                finalFileName = name + ".png";
            }
            m_outputImagePaths.append(projDirStr + "/" + nodeName + "/" + finalFileName);
        }
    }

    m_outputData = std::make_shared<ImageInfoData>(m_outputImagePaths);
    setOutputData(0, m_outputData);
    setOutputData(1, m_outputData);

    if (m_statusLabel) {
        m_statusLabel->setText("状态：完成");
    }

    m_outputNodeNameEdit->setEnabled(m_saveToProjectCheckBox->isChecked());
    m_outputFileNameEdit->setEnabled(m_saveToProjectCheckBox->isChecked());
    m_saveToProjectCheckBox->setEnabled(true);

    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);

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

    finishExecution();
}

void ClutterSuppressionNode::onError(const QString& error)
{
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    if (m_statusLabel) {
        m_statusLabel->setText("状态：错误 - " + error);
    }
    m_outputNodeNameEdit->setEnabled(m_saveToProjectCheckBox->isChecked());
    m_outputFileNameEdit->setEnabled(m_saveToProjectCheckBox->isChecked());
    m_saveToProjectCheckBox->setEnabled(true);

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

    m_outputData.reset();
}

void ClutterSuppressionNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
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

    bool saveToProject = m_saveToProjectCheckBox ? m_saveToProjectCheckBox->isChecked() : m_saveToProject;
    QString nodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    QString fileName = m_outputFileNameEdit ? m_outputFileNameEdit->text() : m_outputFileName;

    modelJson["saveToProject"] = saveToProject;
    modelJson["outputNodeName"] = nodeName;
    modelJson["outputFileName"] = fileName;

    return modelJson;
}

void ClutterSuppressionNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    QJsonValue v = json["saveToProject"];
    if (!v.isUndefined()) {
        m_saveToProject = v.toBool(true);
    }
    v = json["outputNodeName"];
    if (!v.isUndefined()) {
        m_outputNodeName = v.toString();
    }
    v = json["outputFileName"];
    if (!v.isUndefined()) {
        m_outputFileName = v.toString();
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

void ClutterSuppressionNode::onAskUserError(const QString& message, bool* skip)
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        nullptr,
        QStringLiteral("错误"), // 错误
        message,
        QMessageBox::Yes | QMessageBox::No
    );
    *skip = (reply == QMessageBox::Yes);
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
