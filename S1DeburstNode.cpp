#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "S1DeburstNode.h"
#include "NodeEditorWindow.h"
#include <QFileInfo>
#include <QRegularExpression>

namespace QtNodes {

S1DeburstNode::S1DeburstNode()
    : m_widget(nullptr)
    , m_projectCombo(nullptr)
    , m_dataNodeCombo(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_progressBar(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
    , m_isProcessing(false)
{
}

S1DeburstNode::~S1DeburstNode()
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

unsigned int S1DeburstNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;  // One input port
    else
        return 1;  // One output port
}

NodeDataType S1DeburstNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::Out)
        return NodeDataType{"imported_file", "S1 Deburst Data"};
    // Input ports accept ImportedFileData
    return NodeDataType{"imported_file", "S1 Burst Data"};
}

std::shared_ptr<NodeData> S1DeburstNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
}

void S1DeburstNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    updateLabels();

    // Generate default output name if not set
    if (m_inputData && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
    {
        m_outputNodeNameEdit->setText(generateDefaultOutputName());
    }
}

QWidget* S1DeburstNode::embeddedWidget()
{
    if (!m_widget)
    {
        createWidget();
    }
    return m_widget;
}

void S1DeburstNode::createWidget()
{
    m_widget = new QWidget();
    auto* layout = new QVBoxLayout(m_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    // 选择工程 [1:1]
    auto* projectLayout = new QHBoxLayout();
    QLabel* projectLabel = new QLabel("选择工程");
    projectLayout->addWidget(projectLabel);
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    m_projectCombo->addItem(projectName().isEmpty() ? "未打开项目" : projectName());
    projectLayout->addWidget(m_projectCombo);
    layout->addLayout(projectLayout);

    // 数据节点 [1:1]
    auto* dataNodeLayout = new QHBoxLayout();
    QLabel* dataNodeLabel = new QLabel("数据节点");
    dataNodeLayout->addWidget(dataNodeLabel);
    m_dataNodeCombo = new QComboBox();
    m_dataNodeCombo->setEditable(false);
    if (m_inputData)
    {
        QString nodeName = m_inputData->nodeName();
        QString filePath = m_inputData->filePath();
        QFileInfo fileInfo(filePath);
        m_dataNodeCombo->addItem(QString("%1 (%2)").arg(nodeName).arg(fileInfo.fileName()));
    }
    else
    {
        m_dataNodeCombo->addItem("等待输入");
    }
    dataNodeLayout->addWidget(m_dataNodeCombo);
    layout->addLayout(dataNodeLayout);

    // 目标节点名 [1:1]
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

void S1DeburstNode::updateLabels()
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

    // Update data node combo
    if (m_dataNodeCombo)
    {
        m_dataNodeCombo->clear();
        if (m_inputData)
        {
            QString nodeName = m_inputData->nodeName();
            QString filePath = m_inputData->filePath();
            QFileInfo fileInfo(filePath);
            m_dataNodeCombo->addItem(QString("%1 (%2)").arg(nodeName).arg(fileInfo.fileName()));
        }
        else
        {
            m_dataNodeCombo->addItem("等待输入");
        }
    }
}

QString S1DeburstNode::generateDefaultOutputName() const
{
    if (m_inputData)
    {
        QString nodeName = m_inputData->nodeName();
        return nodeName + "_Deburst";
    }
    return "burst拼接结果";
}

bool S1DeburstNode::validateInputs() const
{
    if (!m_inputData)
    {
        return false;
    }

    QString nodeName = m_inputData->nodeName();
    if (nodeName.isEmpty())
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


void S1DeburstNode::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_progressBar->setFormat(QString("%1：%2%").arg(message).arg(progress));
    m_progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void S1DeburstNode::onProcessingFinished()
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
    m_isProcessing = false;
    m_outputNodeNameEdit->setEnabled(true);
    m_progressBar->setValue(100);

    // Notify downstream nodes
    Q_EMIT dataUpdated(0);
}

void S1DeburstNode::onError(const QString& error)
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

    m_isProcessing = false;
    m_outputNodeNameEdit->setEnabled(true);
}

void S1DeburstNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

NodeEditorWindow* S1DeburstNode::getNodeEditorWindow() const
{
    if (!m_widget)
        return nullptr;

    QWidget* parent = m_widget->parentWidget();
    while (parent)
    {
        auto* editor = qobject_cast<NodeEditorWindow*>(parent);
        if (editor)
            return editor;
        parent = parent->parentWidget();
    }

    return nullptr;
}

QStandardItemModel* S1DeburstNode::projectModel() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectModel() : nullptr;
}

QString S1DeburstNode::projectPath() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectPath() : QString();
}

QString S1DeburstNode::projectName() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectName() : QString();
}

} // namespace QtNodes
