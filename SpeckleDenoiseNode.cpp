#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "SpeckleDenoiseNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
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

SpeckleDenoiseNode::SpeckleDenoiseNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputImageLabel(nullptr)
    , m_saveToProjectCheckBox(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_statusLabel(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
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

    if (m_inputData && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty()) {
        m_outputNodeNameEdit->setText(generateOutputFileName());
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
    layout->addWidget(m_inputImageLabel);

    m_saveToProjectCheckBox = new QCheckBox("保存到项目树");
    m_saveToProjectCheckBox->setChecked(true);
    connect(m_saveToProjectCheckBox, &QCheckBox::stateChanged, this, &SpeckleDenoiseNode::onSaveToProjectChanged);
    layout->addWidget(m_saveToProjectCheckBox);

    auto* nodeNameLayout = new QHBoxLayout();
    nodeNameLayout->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("输入节点名称");
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    onSaveToProjectChanged(m_saveToProjectCheckBox->checkState());
}

void SpeckleDenoiseNode::onSaveToProjectChanged(int state)
{
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setEnabled(state == Qt::Checked);
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
    qDebug() << "[SpeckleDenoiseNode] processAutomatically called. Current state:"
             << static_cast<int>(executionState()) << "isReady:" << isReady();

    // CRITICAL: Prevent duplicate execution - check if already processing
    // m_thread being non-null means execution is in progress
    if (m_thread || m_workerThread) {
        qDebug() << "[SpeckleDenoiseNode] SKIP - execution already in progress";
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

    // If widget was created and save to project is checked, need output node name
    if (m_saveToProjectCheckBox && m_saveToProjectCheckBox->isChecked()) {
        if (m_outputNodeNameEdit && m_outputNodeNameEdit->text().trimmed().isEmpty()) {
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

    QString inputPath = m_inputData->filePath();
    QString outputNodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : "Denoise";
    bool saveToProject = m_saveToProjectCheckBox ? m_saveToProjectCheckBox->isChecked() : true;
    QString projPath = projectPath();
    QString projName = projectName();
    QStandardItemModel* model = projectModel();
    QString outputPath = QDir::tempPath() + QString("/speckle_denoise_%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());

    qDebug() << "[SpeckleDenoiseNode] Starting with params:"
             << "\n  inputPath:" << inputPath
             << "\n  outputNodeName:" << outputNodeName
             << "\n  saveToProject:" << saveToProject
             << "\n  projPath:" << projPath
             << "\n  projName:" << projName
             << "\n  temp outputPath:" << outputPath;

    // Create thread
    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    // Connect signals - capture ALL values by value to avoid race conditions
    connect(m_thread, &QThread::started, [this, inputPath, outputPath, outputNodeName, projPath, projName, model, saveToProject]() {
        qDebug() << "[SpeckleDenoiseNode] Thread started, emitting startSpeckleDenoise";
        Q_EMIT startSpeckleDenoise(inputPath, outputPath, outputNodeName, projPath, projName, model, saveToProject);
    });
    connect(this, &SpeckleDenoiseNode::startSpeckleDenoise, m_workerThread, &MyThread::Speckle_Denoise, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::updateProcess, this, &SpeckleDenoiseNode::onProgressUpdate, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::endProcess, this, &SpeckleDenoiseNode::onProcessingFinished, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::errorProcess, this, &SpeckleDenoiseNode::onError, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::sendModel, this, &SpeckleDenoiseNode::onModelUpdated, Qt::UniqueConnection);

    // Start thread
    m_thread->start();
    qDebug() << "[SpeckleDenoiseNode] Thread started, worker created";
    m_outputNodeNameEdit->setEnabled(false);
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

    // Determine the result path
    if (m_saveToProjectCheckBox->isChecked()) {
        // Find the saved file path in project
        QString nodeName = m_outputNodeNameEdit->text().trimmed();
        QString projDirStr = projectPath();
        if (projDirStr.endsWith(".insar", Qt::CaseInsensitive)) {
            projDirStr = QFileInfo(projDirStr).absolutePath();
        }
        // Consistent with MyThread::Speckle_Denoise and import_GenericSAR: use nodeName as folder
        m_outputImagePath = projDirStr + "/" + nodeName + "/" + QFileInfo(m_inputData->filePath()).baseName() + "_denoised.png";
        qDebug() << "[SpeckleDenoiseNode] Output image path (using node folder):" << m_outputImagePath;
    } else {
        // Output path was set in executeProcessing (temp path)
    }

    m_outputData = std::make_shared<ImageInfoData>(m_outputImagePath);
    setOutputData(0, m_outputData);
    setOutputData(1, m_outputData);

    if (m_statusLabel) {
        m_statusLabel->setText("状态：完成");
    }

    m_outputNodeNameEdit->setEnabled(m_saveToProjectCheckBox->isChecked());
    m_saveToProjectCheckBox->setEnabled(true);

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
    m_outputNodeNameEdit->setEnabled(m_saveToProjectCheckBox->isChecked());
    m_saveToProjectCheckBox->setEnabled(true);

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
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* SpeckleDenoiseNode::projectModel() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectModel() : nullptr;
}

QString SpeckleDenoiseNode::projectPath() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectPath() : QString();
}

QString SpeckleDenoiseNode::projectName() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectName() : QString();
}

IApplicationInterface* SpeckleDenoiseNode::getProjectContext() const
{
    if (_widget)
    {
        QWidget* parent = _widget->parentWidget();
        while (parent)
        {
            auto* iface = dynamic_cast<IApplicationInterface*>(parent);
            if (iface) {
                return iface;
            }
            parent = parent->parentWidget();
        }
    }

    foreach(QWidget * widget, QApplication::topLevelWidgets()) {
        MainWindow* mainWin = qobject_cast<MainWindow*>(widget);
        if (mainWin && mainWin->interfaceManager()) {
            auto* iface = mainWin->interfaceManager()->currentInterface();
            if (iface) {
                return iface;
            }
        }
    }

    return nullptr;
}

QJsonObject SpeckleDenoiseNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    if (m_saveToProjectCheckBox)
        modelJson["saveToProject"] = m_saveToProjectCheckBox->isChecked();
    if (m_outputNodeNameEdit)
        modelJson["outputNodeName"] = m_outputNodeNameEdit->text();

    return modelJson;
}

void SpeckleDenoiseNode::load(QJsonObject const &json)
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
}

QString SpeckleDenoiseNode::generateOutputFileName() const
{
    if (!m_inputData || m_inputData->filePath().isEmpty()) {
        return QString();
    }

    QFileInfo fi(m_inputData->filePath());
    QString baseName = fi.completeBaseName();
    return QStringLiteral("%1_denoised").arg(baseName);
}

} // namespace QtNodes
