#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "S1FrameMergeNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InterfaceManager.h"
#include "NodeUtils.h"
#include <QApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>

namespace QtNodes {

S1FrameMergeNode::S1FrameMergeNode()
    : ExecutableNodeDelegateModel()
    , m_projectCombo(nullptr)
    , m_dataNodeCombo{nullptr, nullptr}
    , m_indexSpins{nullptr, nullptr}
    , m_outputNodeNameEdit(nullptr)
    , m_inputs{nullptr, nullptr}
    , m_outputData(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

S1FrameMergeNode::~S1FrameMergeNode()
{
    // Clean up worker thread
    if (m_workerThread)
    {
        if (m_thread && m_thread->isRunning())
        {
            m_workerThread->StopProcess();
        }
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

unsigned int S1FrameMergeNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;  // Two input ports
    else
        return 1;  // One output port
}

NodeDataType S1FrameMergeNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::Out)
        return NodeDataType{"imported_file", "S1 Merged Frame"};
    // Input ports accept ImportedFileData
    return NodeDataType{"imported_file", "S1 Frame Data"};
}

std::shared_ptr<NodeData> S1FrameMergeNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
}

void S1FrameMergeNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port >= 0 && port < 2)
    {
        m_inputs[port] = std::dynamic_pointer_cast<ImportedFileData>(data);
        updateLabels();

        // Generate default output name if both inputs connected and name not set
        if (m_inputs[0] && m_inputs[1] && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(generateDefaultOutputName());
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

    if (m_outputNodeNameEdit)
        modelJson["outputNodeName"] = m_outputNodeNameEdit->text();

    if (m_indexSpins[0])
        modelJson["index1"] = m_indexSpins[0]->value();
    if (m_indexSpins[1])
        modelJson["index2"] = m_indexSpins[1]->value();

    return modelJson;
}

void S1FrameMergeNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);

    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined())
    {
        if (m_outputNodeNameEdit)
            m_outputNodeNameEdit->setText(vName.toString());
    }

    QJsonValue v1 = json["index1"];
    if (!v1.isUndefined())
    {
        if (m_indexSpins[0])
            m_indexSpins[0]->setValue(v1.toInt());
    }

    QJsonValue v2 = json["index2"];
    if (!v2.isUndefined())
    {
        if (m_indexSpins[1])
            m_indexSpins[1]->setValue(v2.toInt());
    }
}

void S1FrameMergeNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setMinimumWidth(280);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    // 选择工程
    auto* projectLayout = new QHBoxLayout();
    QLabel* projectLabel = new QLabel("选择工程");
    projectLayout->addWidget(projectLabel);
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    m_projectCombo->addItem(projectName().isEmpty() ? "未打开项目" : projectName());
    projectLayout->addWidget(m_projectCombo);
    layout->addLayout(projectLayout);

    // 数据节点 1 [1:1]
    auto* dataNode1Layout = new QHBoxLayout();
    QLabel* dataNode1Label = new QLabel("数据节点 1");
    dataNode1Layout->addWidget(dataNode1Label);
    m_dataNodeCombo[0] = new QComboBox();
    m_dataNodeCombo[0]->setEditable(false);
    if (m_inputs[0])
    {
        QString nodeName = m_inputs[0]->nodeName();
        QString filePath = m_inputs[0]->filePath();
        QFileInfo fileInfo(filePath);
        m_dataNodeCombo[0]->addItem(QString("%1 (%2)").arg(nodeName).arg(fileInfo.fileName()));
    }
    else
    {
        m_dataNodeCombo[0]->addItem("等待输入");
    }
    dataNode1Layout->addWidget(m_dataNodeCombo[0]);
    layout->addLayout(dataNode1Layout);

    // Image Index 1
    auto* index1Layout = new QHBoxLayout();
    index1Layout->addWidget(new QLabel("Image Index 1:"));
    m_indexSpins[0] = new QSpinBox();
    m_indexSpins[0]->setMinimum(1);
    m_indexSpins[0]->setMaximum(100);
    m_indexSpins[0]->setValue(1);
    index1Layout->addWidget(m_indexSpins[0]);
    layout->addLayout(index1Layout);

    // 数据节点 2 [1:1]
    auto* dataNode2Layout = new QHBoxLayout();
    QLabel* dataNode2Label = new QLabel("数据节点 2");
    dataNode2Layout->addWidget(dataNode2Label);
    m_dataNodeCombo[1] = new QComboBox();
    m_dataNodeCombo[1]->setEditable(false);
    if (m_inputs[1])
    {
        QString nodeName = m_inputs[1]->nodeName();
        QString filePath = m_inputs[1]->filePath();
        QFileInfo fileInfo(filePath);
        m_dataNodeCombo[1]->addItem(QString("%1 (%2)").arg(nodeName).arg(fileInfo.fileName()));
    }
    else
    {
        m_dataNodeCombo[1]->addItem("等待输入");
    }
    dataNode2Layout->addWidget(m_dataNodeCombo[1]);
    layout->addLayout(dataNode2Layout);

    // Image Index 2
    auto* index2Layout = new QHBoxLayout();
    index2Layout->addWidget(new QLabel("Image Index 2:"));
    m_indexSpins[1] = new QSpinBox();
    m_indexSpins[1]->setMinimum(1);
    m_indexSpins[1]->setMaximum(100);
    m_indexSpins[1]->setValue(1);
    index2Layout->addWidget(m_indexSpins[1]);
    layout->addLayout(index2Layout);

    // 目标节点名
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("不要输入中文字符");
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
}

void S1FrameMergeNode::updateLabels()
{
    // Update project combo
    if (m_projectCombo)
    {
        QString projName = projectName();
        if (!projName.isEmpty())
        {
            if (m_projectCombo->count() == 0 || m_projectCombo->itemText(0) != projName)
            {
                m_projectCombo->clear();
                m_projectCombo->addItem(projName);
            }
        }
    }

    // Update data node combos
    for (int i = 0; i < 2; ++i)
    {
        if (m_dataNodeCombo[i])
        {
            m_dataNodeCombo[i]->clear();
            if (m_inputs[i])
            {
                QString nodeName = m_inputs[i]->nodeName();
                QString filePath = m_inputs[i]->filePath();
                QFileInfo fileInfo(filePath);
                m_dataNodeCombo[i]->addItem(QString("%1 (%2)").arg(nodeName).arg(fileInfo.fileName()));
            }
            else
            {
                m_dataNodeCombo[i]->addItem("等待输入");
            }
        }
    }
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
    setProgress(progress);
}

void S1FrameMergeNode::onProcessingFinished()
{
    // Create output data
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    QString outputPath = projectPath() + "/" + dstNode + "/";
    m_outputData = std::make_shared<ImportedFileData>(outputPath, dstNode);

    // Clean up thread
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

    // Update UI
    m_outputNodeNameEdit->setEnabled(true);

    // Notify base class that we're finished
    setProgress(100);
    finishExecution();
    Q_EMIT dataUpdated(0);
}

void S1FrameMergeNode::onError(const QString& error)
{
    // Clean up thread
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

    m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Error);
}

void S1FrameMergeNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

QStandardItemModel* S1FrameMergeNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString S1FrameMergeNode::projectPath() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectPath() : QString();
}

QString S1FrameMergeNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

void S1FrameMergeNode::execute()
{
    executeProcessing();
}

void S1FrameMergeNode::stopExecution()
{
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
}

void S1FrameMergeNode::processAutomatically()
{
    // In automatic mode, if inputs are valid, execute
    if (validateInputs())
    {
        executeProcessing();
    }
}

void S1FrameMergeNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void S1FrameMergeNode::executeProcessing()
{
    if (!validateInputs())
        return;

    setProgress(0);
    setState(ExecutionState::Running);

    // Prepare processing
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    QString project = projectName();
    QString node1 = m_inputs[0]->nodeName();
    QString node2 = m_inputs[1]->nodeName();
    int index1 = m_indexSpins[0]->value();
    int index2 = m_indexSpins[1]->value();

    // Create thread
    m_thread = new QThread();
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1FrameMergeNode::startFrameMerge, m_workerThread, &MyThread::S1_frame_merge);
    connect(m_thread, &QThread::started, [this, index1, index2, project, node1, node2, dstNode]() {
        Q_EMIT startFrameMerge(index1, index2, project, node1, node2, dstNode, projectModel());
    });
    connect(m_workerThread, &MyThread::updateProcess, this, &S1FrameMergeNode::onProgressUpdate);
    connect(m_workerThread, &MyThread::endProcess, this, &S1FrameMergeNode::onProcessingFinished);
    connect(m_workerThread, &MyThread::errorProcess, this, &S1FrameMergeNode::onError);
    connect(m_workerThread, &MyThread::sendModel, this, &S1FrameMergeNode::onModelUpdated);
    connect(m_workerThread, &MyThread::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
    m_thread->start();
    m_outputNodeNameEdit->setEnabled(false);
}

} // namespace QtNodes
