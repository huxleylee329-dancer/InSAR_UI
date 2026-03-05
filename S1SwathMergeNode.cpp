#include "S1SwathMergeNode.h"
#include "NodeEditorWindow.h"
#include <QFileInfo>

namespace QtNodes {

S1SwathMergeNode::S1SwathMergeNode()
    : m_widget(nullptr)
    , m_inputLabels{nullptr, nullptr, nullptr}
    , m_indexSpins{nullptr, nullptr, nullptr}
    , m_outputNodeNameEdit(nullptr)
    , m_processButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
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
        return NodeDataType{"imported_file", "Imported File"};
    // Input ports accept ImportedFileData
    return NodeDataType{"imported_file", "Imported File"};
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
        updateInputLabels();

        // Enable/disable process button based on input availability
        bool allInputsConnected = (m_inputs[0] != nullptr) &&
                                 (m_inputs[1] != nullptr) &&
                                 (m_inputs[2] != nullptr);
        m_processButton->setEnabled(allInputsConnected);
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
    auto* layout = new QVBoxLayout(m_widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Input data info - Input 1 (IW1)
    layout->addWidget(new QLabel("IW1 Input:"));
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

    // Input data info - Input 2 (IW2)
    layout->addWidget(new QLabel("IW2 Input:"));
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

    // Input data info - Input 3 (IW3)
    layout->addWidget(new QLabel("IW3 Input:"));
    m_inputLabels[2] = new QLabel("No input");
    m_inputLabels[2]->setWordWrap(true);
    m_inputLabels[2]->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 3px; border-radius: 2px; }");
    layout->addWidget(m_inputLabels[2]);

    // Image index 3
    auto* index3Layout = new QHBoxLayout();
    index3Layout->addWidget(new QLabel("Image Index 3:"));
    m_indexSpins[2] = new QSpinBox();
    m_indexSpins[2]->setMinimum(1);
    m_indexSpins[2]->setMaximum(100);
    m_indexSpins[2]->setValue(1);
    index3Layout->addWidget(m_indexSpins[2]);
    index3Layout->addStretch();
    layout->addLayout(index3Layout);

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
    connect(m_processButton, &QPushButton::clicked, this, &S1SwathMergeNode::onProcessButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &S1SwathMergeNode::onStopButtonClicked);
}

void S1SwathMergeNode::updateInputLabels()
{
    const char* labels[] = {"IW1", "IW2", "IW3"};
    for (int i = 0; i < 3; ++i)
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
            m_inputLabels[i]->setText("No " + QString(labels[i]) + " input");
        }
    }

    // Generate default output name if all inputs connected and name not set
    if (m_inputs[0] && m_inputs[1] && m_inputs[2] && m_outputNodeNameEdit->text().isEmpty())
    {
        m_outputNodeNameEdit->setText(generateDefaultOutputName());
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

void S1SwathMergeNode::onProcessButtonClicked()
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
    QString node3 = m_inputs[2]->nodeName();
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();

    // Create worker thread
    m_workerThread = new MyThread();
    m_thread = new QThread();
    m_workerThread->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1SwathMergeNode::startSwathMerge,
            m_workerThread, &MyThread::S1_swath_merge);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &S1SwathMergeNode::onProgressUpdate);
    connect(m_workerThread, &MyThread::endProcess,
            this, &S1SwathMergeNode::onProcessingFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &S1SwathMergeNode::onError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &S1SwathMergeNode::onModelUpdated);

    // Prepare UI
    m_processButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_statusLabel->setText("Processing...");
    m_progressBar->setValue(0);

    // Start processing
    int index1 = m_indexSpins[0]->value();
    int index2 = m_indexSpins[1]->value();
    int index3 = m_indexSpins[2]->value();

    Q_EMIT startSwathMerge(index1, index2, index3, projectName, node1, node2, node3, dstNode, model);

    m_thread->start();
}

void S1SwathMergeNode::onStopButtonClicked()
{
    if (m_workerThread && m_thread && m_thread->isRunning())
    {
        m_workerThread->StopProcess();
        m_statusLabel->setText("Stopping...");
    }
}

void S1SwathMergeNode::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
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
    m_processButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_statusLabel->setText("Processing completed successfully!");
    m_progressBar->setValue(100);

    // Notify downstream nodes
    Q_EMIT dataUpdated(0);
}

void S1SwathMergeNode::onError(const QString& error)
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

void S1SwathMergeNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

NodeEditorWindow* S1SwathMergeNode::getNodeEditorWindow() const
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
