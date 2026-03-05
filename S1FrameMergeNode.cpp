#include "S1FrameMergeNode.h"
#include "NodeEditorWindow.h"
#include <QFileInfo>

namespace QtNodes {

S1FrameMergeNode::S1FrameMergeNode()
    : m_widget(nullptr)
    , m_inputLabels{nullptr, nullptr}
    , m_indexSpins{nullptr, nullptr}
    , m_outputNodeNameEdit(nullptr)
    , m_processButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
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

    // Note: m_widget is owned by QtNodes QGraphicsProxyWidget, do not delete here
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
        return NodeDataType{"imported_file", "Imported File"};
    // Input ports accept ImportedFileData
    return NodeDataType{"imported_file", "Imported File"};
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
        updateInputLabels();

        // Enable/disable process button based on input availability
        bool allInputsConnected = (m_inputs[0] != nullptr) && (m_inputs[1] != nullptr);
        m_processButton->setEnabled(allInputsConnected);
    }
}

QWidget* S1FrameMergeNode::embeddedWidget()
{
    if (!m_widget)
    {
        createWidget();
    }
    return m_widget;
}

void S1FrameMergeNode::createWidget()
{
    m_widget = new QWidget();
    auto* layout = new QVBoxLayout(m_widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Input data info - Input 1
    layout->addWidget(new QLabel("Input 1:"));
    m_inputLabels[0] = new QLabel("No input");
    m_inputLabels[0]->setWordWrap(true);
    m_inputLabels[0]->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 3px; border-radius: 2px; }");
    layout->addWidget(m_inputLabels[0]);

    // Image index 1
    auto* index1Layout = new QHBoxLayout();
    index1Layout->addWidget(new QLabel("Image Index 1:"));
    m_indexSpins[0] = new QSpinBox();
    m_indexSpins[0]->setMinimum(1);
    m_indexSpins[0]->setMaximum(100);
    m_indexSpins[0]->setValue(1);
    index1Layout->addWidget(m_indexSpins[0]);
    index1Layout->addStretch();
    layout->addLayout(index1Layout);

    // Input data info - Input 2
    layout->addWidget(new QLabel("Input 2:"));
    m_inputLabels[1] = new QLabel("No input");
    m_inputLabels[1]->setWordWrap(true);
    m_inputLabels[1]->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 3px; border-radius: 2px; }");
    layout->addWidget(m_inputLabels[1]);

    // Image index 2
    auto* index2Layout = new QHBoxLayout();
    index2Layout->addWidget(new QLabel("Image Index 2:"));
    m_indexSpins[1] = new QSpinBox();
    m_indexSpins[1]->setMinimum(1);
    m_indexSpins[1]->setMaximum(100);
    m_indexSpins[1]->setValue(1);
    index2Layout->addWidget(m_indexSpins[1]);
    index2Layout->addStretch();
    layout->addLayout(index2Layout);

    // Output node name
    layout->addWidget(new QLabel("Output Node Name:"));
    m_outputNodeNameEdit = new QLineEdit();
    layout->addWidget(m_outputNodeNameEdit);

    // Separator
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // Progress bar
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    // Status label
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    m_processButton = new QPushButton("Process");
    m_processButton->setEnabled(false);
    m_stopButton = new QPushButton("Stop");
    m_stopButton->setEnabled(false);
    buttonLayout->addWidget(m_processButton);
    buttonLayout->addWidget(m_stopButton);
    layout->addLayout(buttonLayout);

    // Connect signals
    connect(m_processButton, &QPushButton::clicked, this, &S1FrameMergeNode::onProcessButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &S1FrameMergeNode::onStopButtonClicked);
}

void S1FrameMergeNode::updateInputLabels()
{
    for (int i = 0; i < 2; ++i)
    {
        if (m_inputs[i] && m_inputLabels[i])
        {
            QString nodeName = m_inputs[i]->nodeName();
            QString filePath = m_inputs[i]->filePath();
            QFileInfo fileInfo(filePath);

            QString text = QString("<b>Node:</b> %1<br><b>File:</b> %2")
                .arg(nodeName)
                .arg(fileInfo.fileName());

            m_inputLabels[i]->setText(text);
        }
        else if (m_inputLabels[i])
        {
            m_inputLabels[i]->setText(QString("No input %1").arg(i + 1));
        }
    }

    // Generate default output name if both inputs connected and name not set
    if (m_inputs[0] && m_inputs[1] && m_outputNodeNameEdit->text().isEmpty())
    {
        m_outputNodeNameEdit->setText(generateDefaultOutputName());
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

void S1FrameMergeNode::onProcessButtonClicked()
{
    if (!validateInputs())
    {
        m_statusLabel->setText("Error: Invalid inputs or no project open");
        return;
    }

    // Get project context
    QString projectName = this->projectName();
    QStandardItemModel* model = projectModel();
    QString node1 = m_inputs[0]->nodeName();
    QString node2 = m_inputs[1]->nodeName();
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();

    // Create worker thread
    m_workerThread = new MyThread();
    m_thread = new QThread();
    m_workerThread->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1FrameMergeNode::startFrameMerge,
            m_workerThread, &MyThread::S1_frame_merge);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &S1FrameMergeNode::onProgressUpdate);
    connect(m_workerThread, &MyThread::endProcess,
            this, &S1FrameMergeNode::onProcessingFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &S1FrameMergeNode::onError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &S1FrameMergeNode::onModelUpdated);

    // Prepare UI
    m_processButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_statusLabel->setText("Processing...");
    m_progressBar->setValue(0);

    // Start processing
    int index1 = m_indexSpins[0]->value();
    int index2 = m_indexSpins[1]->value();

    Q_EMIT startFrameMerge(index1, index2, projectName, node1, node2, dstNode, model);

    m_thread->start();
}

void S1FrameMergeNode::onStopButtonClicked()
{
    if (m_workerThread && m_thread && m_thread->isRunning())
    {
        m_workerThread->StopProcess();
        m_statusLabel->setText("Stopping...");
    }
}

void S1FrameMergeNode::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
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
    m_processButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_statusLabel->setText("Processing completed successfully!");
    m_progressBar->setValue(100);

    // Notify downstream nodes
    Q_EMIT dataUpdated(0);
}

void S1FrameMergeNode::onError(const QString& error)
{
    m_statusLabel->setText("Error: " + error);

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

    m_processButton->setEnabled(true);
    m_stopButton->setEnabled(false);
}

void S1FrameMergeNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

NodeEditorWindow* S1FrameMergeNode::getNodeEditorWindow() const
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

QStandardItemModel* S1FrameMergeNode::projectModel() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectModel() : nullptr;
}

QString S1FrameMergeNode::projectPath() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectPath() : QString();
}

QString S1FrameMergeNode::projectName() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectName() : QString();
}

} // namespace QtNodes
