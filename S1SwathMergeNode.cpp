#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "S1SwathMergeNode.h"
#include "WorkflowUI.h"
#include <QFileInfo>

namespace QtNodes {

S1SwathMergeNode::S1SwathMergeNode()
    : m_widget(nullptr)
    , m_projectCombo(nullptr)
    , m_dataNodeCombo{nullptr, nullptr, nullptr}
    , m_indexSpins{nullptr, nullptr, nullptr}
    , m_outputNodeNameEdit(nullptr)
    , m_progressBar(nullptr)
    , m_inputs{nullptr, nullptr, nullptr}
    , m_outputData(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

S1SwathMergeNode::~S1SwathMergeNode()
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

    // Note: m_widget is owned by QtNodes QGraphicsProxyWidget, do not delete here
}

unsigned int S1SwathMergeNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 3;  // Three input ports (IW1, IW2, IW3)
    else
        return 1;  // One output port
}

NodeDataType S1SwathMergeNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::Out)
        return NodeDataType{"imported_file", "S1 Merged Swath"};
    // Input ports accept ImportedFileData
    return NodeDataType{"imported_file", "S1 Swath Data"};
}

std::shared_ptr<NodeData> S1SwathMergeNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
}

void S1SwathMergeNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port >= 0 && port < 3)
    {
        m_inputs[port] = std::dynamic_pointer_cast<ImportedFileData>(data);
        updateLabels();

        // Generate default output name if all inputs connected and name not set
        if (m_inputs[0] && m_inputs[1] && m_inputs[2] && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(generateDefaultOutputName());
        }
    }
}

QWidget* S1SwathMergeNode::embeddedWidget()
{
    if (!m_widget)
    {
        createWidget();
    }
    return m_widget;
}

void S1SwathMergeNode::createWidget()
{
    m_widget = new QWidget();
    m_widget->setObjectName("NodeEmbeddedWidget");
    m_widget->setMinimumWidth(280);
    auto* layout = new QVBoxLayout(m_widget);
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

    // 数据节点 IW1 [1:1]
    auto* dataNode1Layout = new QHBoxLayout();
    QLabel* dataNode1Label = new QLabel("IW1 数据节点");
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

    // 数据节点 IW2 [1:1]
    auto* dataNode2Layout = new QHBoxLayout();
    QLabel* dataNode2Label = new QLabel("IW2 数据节点");
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

    // 数据节点 IW3 [1:1]
    auto* dataNode3Layout = new QHBoxLayout();
    QLabel* dataNode3Label = new QLabel("IW3 数据节点");
    dataNode3Layout->addWidget(dataNode3Label);
    m_dataNodeCombo[2] = new QComboBox();
    m_dataNodeCombo[2]->setEditable(false);
    if (m_inputs[2])
    {
        QString nodeName = m_inputs[2]->nodeName();
        QString filePath = m_inputs[2]->filePath();
        QFileInfo fileInfo(filePath);
        m_dataNodeCombo[2]->addItem(QString("%1 (%2)").arg(nodeName).arg(fileInfo.fileName()));
    }
    else
    {
        m_dataNodeCombo[2]->addItem("等待输入");
    }
    dataNode3Layout->addWidget(m_dataNodeCombo[2]);
    layout->addLayout(dataNode3Layout);

    // Image Index 3
    auto* index3Layout = new QHBoxLayout();
    index3Layout->addWidget(new QLabel("Image Index 3:"));
    m_indexSpins[2] = new QSpinBox();
    m_indexSpins[2]->setMinimum(1);
    m_indexSpins[2]->setMaximum(100);
    m_indexSpins[2]->setValue(1);
    index3Layout->addWidget(m_indexSpins[2]);
    layout->addLayout(index3Layout);

    // 目标节点名
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("不要输入中文字符");
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    // Spacer
    layout->addSpacing(6);

    // Separator
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // Progress bar
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    layout->addWidget(m_progressBar);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
}

void S1SwathMergeNode::updateLabels()
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
    for (int i = 0; i < 3; ++i)
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
    m_progressBar->setValue(progress);
    m_progressBar->setFormat(QString("%1：%2%").arg(message).arg(progress));
    m_progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void S1SwathMergeNode::onProcessingFinished()
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
    m_progressBar->setValue(100);

    // Notify downstream nodes
    Q_EMIT dataUpdated(0);
}

void S1SwathMergeNode::onError(const QString& error)
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
}

void S1SwathMergeNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

WorkflowUI* S1SwathMergeNode::getNodeEditorWindow() const
{
    if (!m_widget)
        return nullptr;

    QWidget* parent = m_widget->parentWidget();
    while (parent)
    {
        auto* editor = qobject_cast<WorkflowUI*>(parent);
        if (editor)
            return editor;
        parent = parent->parentWidget();
    }

    return nullptr;
}

QStandardItemModel* S1SwathMergeNode::projectModel() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectModel() : nullptr;
}

QString S1SwathMergeNode::projectPath() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectPath() : QString();
}

QString S1SwathMergeNode::projectName() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectName() : QString();
}

} // namespace QtNodes
