#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "ClutterSuppressionNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <QDebug>

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

    m_saveToProjectCheckBox = new QCheckBox("保存到项目树");
    m_saveToProjectCheckBox->setChecked(true);
    connect(m_saveToProjectCheckBox, &QCheckBox::stateChanged, this, &ClutterSuppressionNode::onSaveToProjectChanged);
    layout->addWidget(m_saveToProjectCheckBox);

    auto* nodeNameLayout = new QHBoxLayout();
    nodeNameLayout->setStretch(0, 3);
    nodeNameLayout->setStretch(1, 7);
    nodeNameLayout->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("输入节点名称");
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    auto* fileNameLayout = new QHBoxLayout();
    fileNameLayout->setStretch(0, 3);
    fileNameLayout->setStretch(1, 7);
    fileNameLayout->addWidget(new QLabel("目标文件名："));
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setPlaceholderText("自动生成或手动输入");
    connect(m_outputFileNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_outputFileName = text; });
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
    qDebug() << "[ClutterSuppressionNode] processAutomatically called. Current state:"
             << static_cast<int>(executionState()) << "isReady:" << isReady();

    if (m_thread || m_workerThread) {
        qDebug() << "[ClutterSuppressionNode] SKIP - execution already in progress";
        return;
    }

    if (isReady()) {
        if (!m_outputData) {
            m_outputData = std::make_shared<ImageInfoData>("");
            setOutputData(0, m_outputData);
            setOutputData(1, m_outputData);
            qDebug() << "[ClutterSuppressionNode] Set placeholder output data to prevent Idle reset";
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
    qDebug() << "[ClutterSuppressionNode] executeProcessing START";

    if (m_thread || m_workerThread)
    {
        qDebug() << "[ClutterSuppressionNode] Cleaning up existing threads";
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
        qDebug() << "[ClutterSuppressionNode] Not ready, aborting";
        if (m_statusLabel) {
            m_statusLabel->setText("状态：未准备好");
        }
        return;
    }

    setProgress(0);
    if (m_statusLabel) {
        m_statusLabel->setText("状态：正在初始化...");
    }

    QString inputPath = m_inputData->filePath();
    QString outputNodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : "ClutterSuppression";
    QString outputFileName = m_outputFileNameEdit ? m_outputFileNameEdit->text().trimmed() : QString();
    bool saveToProject = m_saveToProjectCheckBox ? m_saveToProjectCheckBox->isChecked() : true;
    QString projPath = projectPath();
    QString projName = projectName();
    QStandardItemModel* model = projectModel();
    XMLFile* projectXmlPtr = projectXml();
    QString outputPath = QDir::tempPath() + QString("/clutter_suppression_%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());

    qDebug() << "[ClutterSuppressionNode] Starting with params:"
             << "\n  inputPath:" << inputPath
             << "\n  outputNodeName:" << outputNodeName
             << "\n  outputFileName:" << outputFileName
             << "\n  saveToProject:" << saveToProject
             << "\n  projPath:" << projPath
             << "\n  projName:" << projName
             << "\n  temp outputPath:" << outputPath;

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::started, [this, inputPath, outputPath, outputNodeName, outputFileName, projPath, projName, model, saveToProject, projectXmlPtr]() {
        qDebug() << "[ClutterSuppressionNode] Thread started, emitting startClutterSuppression";
        Q_EMIT startClutterSuppression(inputPath, outputPath, outputNodeName, outputFileName, projPath, projName, model, saveToProject, projectXmlPtr);
    });
    connect(this, &ClutterSuppressionNode::startClutterSuppression, m_workerThread, &MyThread::Clutter_Suppression, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::updateProcess, this, &ClutterSuppressionNode::onProgressUpdate, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::endProcess, this, &ClutterSuppressionNode::onProcessingFinished, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::errorProcess, this, &ClutterSuppressionNode::onError, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::sendModel, this, &ClutterSuppressionNode::onModelUpdated, Qt::UniqueConnection);

    m_thread->start();
    qDebug() << "[ClutterSuppressionNode] Thread started, worker created";
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
    qDebug() << "[ClutterSuppressionNode] onProcessingFinished called";

    if (m_saveToProjectCheckBox->isChecked()) {
        QString nodeName = m_outputNodeNameEdit->text().trimmed();
        QString projDirStr = projectPath();
        if (projDirStr.endsWith(".insar", Qt::CaseInsensitive)) {
            projDirStr = QFileInfo(projDirStr).absolutePath();
        }
        QString finalFileName;
        if (m_outputFileName.isEmpty()) {
            finalFileName = QFileInfo(m_inputData->filePath()).baseName() + "_clutter.png";
        } else {
            if (QFileInfo(m_outputFileName).suffix().isEmpty()) {
                finalFileName = m_outputFileName + ".png";
            } else {
                finalFileName = m_outputFileName;
            }
        }
        m_outputImagePath = projDirStr + "/" + nodeName + "/" + finalFileName;
        qDebug() << "[ClutterSuppressionNode] Output image path:" << m_outputImagePath;
    }

    m_outputData = std::make_shared<ImageInfoData>(m_outputImagePath);
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

    qDebug() << "[ClutterSuppressionNode] Processing finished, calling finishExecution";
    finishExecution();
}

void ClutterSuppressionNode::onError(const QString& error)
{
    qDebug() << "[ClutterSuppressionNode] onError called:" << error;
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
    qDebug() << "[ClutterSuppressionNode] Error handling complete";
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

    if (m_saveToProjectCheckBox)
        modelJson["saveToProject"] = m_saveToProjectCheckBox->isChecked();
    if (m_outputNodeNameEdit)
        modelJson["outputNodeName"] = m_outputNodeNameEdit->text();
    if (m_outputFileNameEdit)
        modelJson["outputFileName"] = m_outputFileNameEdit->text();

    return modelJson;
}

void ClutterSuppressionNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);

    if (m_saveToProjectCheckBox) {
        QJsonValue v = json["saveToProject"];
        if (!v.isUndefined()) {
            m_saveToProjectCheckBox->setChecked(v.toBool(true));
        }
    }

    if (m_outputNodeNameEdit) {
        QJsonValue v = json["outputNodeName"];
        if (!v.isUndefined()) {
            m_outputNodeNameEdit->setText(v.toString());
        }
    }

    if (m_outputFileNameEdit) {
        QJsonValue v = json["outputFileName"];
        if (!v.isUndefined()) {
            m_outputFileNameEdit->setText(v.toString());
            m_outputFileName = v.toString();
        }
    }
}

QString ClutterSuppressionNode::generateOutputFileName() const
{
    if (!m_inputData || m_inputData->filePath().isEmpty()) {
        return QString();
    }

    QFileInfo fi(m_inputData->filePath());
    QString baseName = fi.completeBaseName();
    return QStringLiteral("%1_clutter").arg(baseName);
}

} // namespace QtNodes
