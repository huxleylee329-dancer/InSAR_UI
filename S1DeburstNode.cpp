#include "S1DeburstNode.h"
#include "NodeEditorWindow.h"
#include <QFileInfo>

namespace QtNodes {

S1DeburstNode::S1DeburstNode()
    : m_widget(nullptr)
    , m_inputLabel(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_processButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
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
        return NodeDataType{"imported_file", "Imported File"};
    // Input ports accept ImportedFileData
    return NodeDataType{"imported_file", "Imported File"};
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
    updateInputLabel();

    // Enable/disable process button based on input availability
    bool hasInput = (m_inputData != nullptr);
    m_processButton->setEnabled(hasInput);
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
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Input data info
    layout->addWidget(new QLabel("Input Data:"));
    m_inputLabel = new QLabel("No input");
    m_inputLabel->setWordWrap(true);
    m_inputLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 3px; border-radius: 2px; }");
    layout->addWidget(m_inputLabel);

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
    connect(m_processButton, &QPushButton::clicked, this, &S1DeburstNode::onProcessButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &S1DeburstNode::onStopButtonClicked);
}

void S1DeburstNode::updateInputLabel()
{
    if (m_inputData)
    {
        QString nodeName = m_inputData->nodeName();
        QString filePath = m_inputData->filePath();
        QFileInfo fileInfo(filePath);

        QString text = QString("<b>Node:</b> %1<br><b>File:</b> %2")
            .arg(nodeName)
            .arg(fileInfo.fileName());

        m_inputLabel->setText(text);

        // Generate default output name if not set
        if (m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(generateDefaultOutputName());
        }
    }
    else
    {
        m_inputLabel->setText("No input");
    }
}

QString S1DeburstNode::generateDefaultOutputName() const
{
    if (m_inputData)
    {
        QString nodeName = m_inputData->nodeName();
        return nodeName + "_Deburst";
    }
    return "Deburst_Output";
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

void S1DeburstNode::onProcessButtonClicked()
{
    if (!validateInputs())
    {
        m_statusLabel->setText("Error: Invalid inputs or no project open");
        return;
    }

    // Get project context
    QString savePath = projectPath();
    QString dstProject = projectName();
    QStandardItemModel* model = projectModel();
    QString srcNode = m_inputData->nodeName();
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();

    // Create worker thread
    m_workerThread = new MyThread();
    m_thread = new QThread();
    m_workerThread->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1DeburstNode::startDeburst,
            m_workerThread, &MyThread::S1_Deburst);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &S1DeburstNode::onProgressUpdate);
    connect(m_workerThread, &MyThread::endProcess,
            this, &S1DeburstNode::onProcessingFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &S1DeburstNode::onError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &S1DeburstNode::onModelUpdated);

    // Prepare UI
    m_processButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_statusLabel->setText("Processing...");
    m_progressBar->setValue(0);

    // Start processing
    Q_EMIT startDeburst(savePath, dstProject, srcNode, dstNode, model);

    m_thread->start();
}

void S1DeburstNode::onStopButtonClicked()
{
    if (m_workerThread && m_thread && m_thread->isRunning())
    {
        m_workerThread->StopProcess();
        m_statusLabel->setText("Stopping...");
    }
}

void S1DeburstNode::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
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
    m_processButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_statusLabel->setText("Processing completed successfully!");
    m_progressBar->setValue(100);

    // Notify downstream nodes
    Q_EMIT dataUpdated(0);
}

void S1DeburstNode::onError(const QString& error)
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
