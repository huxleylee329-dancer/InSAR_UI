#include "TSXImportNode.h"
#include <QFileInfo>

namespace QtNodes {

TSXImportNode::TSXImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_xmlEdit(nullptr)
    , m_polarizationCombo(nullptr)
    , m_importButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_xmlPath()
    , m_importedFilePath()
    , m_outputFileName()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

TSXImportNode::~TSXImportNode()
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

QWidget* TSXImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Output node name
    layout->addWidget(new QLabel("Output Node Name:"));
    m_outputNodeNameEdit = new QLineEdit();
    layout->addWidget(m_outputNodeNameEdit);

    // XML file selection
    layout->addWidget(new QLabel("XML File:"));
    auto* xmlLayout = new QHBoxLayout();
    m_xmlEdit = new QLineEdit();
    QPushButton* xmlBrowse = new QPushButton("Browse...");
    xmlLayout->addWidget(m_xmlEdit);
    xmlLayout->addWidget(xmlBrowse);
    layout->addLayout(xmlLayout);

    // Polarization selection
    layout->addWidget(new QLabel("Polarization:"));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("HH");
    m_polarizationCombo->addItem("VV");
    layout->addWidget(m_polarizationCombo);

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
    m_importButton = new QPushButton("Import");
    m_stopButton = new QPushButton("Stop");
    m_stopButton->setEnabled(false);
    buttonLayout->addWidget(m_importButton);
    buttonLayout->addWidget(m_stopButton);
    layout->addLayout(buttonLayout);

    // Connect signals
    connect(xmlBrowse, &QPushButton::clicked, this, &TSXImportNode::onXmlBrowseClicked);
    connect(m_importButton, &QPushButton::clicked, this, &TSXImportNode::onImportButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &TSXImportNode::onStopButtonClicked);

    return widget;
}

void TSXImportNode::executeImport()
{
    if (m_isProcessing)
        return;

    m_xmlPath = m_xmlEdit->text().trimmed();
    if (m_xmlPath.isEmpty())
    {
        onError("Please select an XML file.");
        return;
    }

    if (!QFileInfo::exists(m_xmlPath))
    {
        onError("XML file does not exist: " + m_xmlPath);
        return;
    }

    m_outputFileName = generateOutputFileName();
    if (m_outputFileName.isEmpty())
    {
        onError("Could not generate output file name from XML file.");
        return;
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &TSXImportNode::startTSXImport,
            m_workerThread, &MyThread::import_TSX);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &TSXImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &TSXImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &TSXImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &TSXImportNode::onModelUpdated);

    m_thread->start();

    QString polarization = m_polarizationCombo->currentText();
    QString outputNodeName = getOutputNodeName();

    Q_EMIT startTSXImport(
        polarization,
        m_xmlPath,
        projectPath(),
        outputNodeName,
        m_outputFileName,
        projectName(),
        projectModel()
    );
}

QString TSXImportNode::getImportedFilePath() const
{
    if (m_importedFilePath.isEmpty())
    {
        QString outputNodeName = getOutputNodeName();
        return QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(m_outputFileName);
    }
    return m_importedFilePath;
}

QString TSXImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return generateOutputFileName();
    }
    return name;
}

QString TSXImportNode::generateOutputFileName() const
{
    QFileInfo fileInfo(m_xmlPath);
    QString baseName = fileInfo.baseName();

    // Find the last "T" in the filename and extract 8 characters before it
    int pos = baseName.lastIndexOf('T');
    if (pos >= 8)
    {
        return baseName.mid(pos - 8, 8);
    }

    return QString();
}

void TSXImportNode::onXmlBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        m_widget,
        "Select TerraSAR-X XML File",
        QFileInfo(m_xmlPath).absolutePath(),
        "XML Files (*.xml);;All Files (*)"
    );

    if (!filePath.isEmpty())
    {
        m_xmlEdit->setText(filePath);

        QString autoName = generateOutputFileName();
        if (!autoName.isEmpty() && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(autoName);
        }
    }
}

void TSXImportNode::onImportButtonClicked()
{
    if (m_isProcessing)
        return;

    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
    {
        QMessageBox::warning(m_widget, "Error", "No project is currently open. Please open a project first.");
        return;
    }

    QString nodeName = getOutputNodeName();
    if (nodeName.isEmpty())
    {
        QMessageBox::warning(m_widget, "Error", "Please enter an output node name.");
        return;
    }

    m_isProcessing = true;
    m_canStop = true;
    m_importButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText("Starting import...");

    executeImport();
}

void TSXImportNode::onStopButtonClicked()
{
    if (!m_isProcessing || !m_canStop)
        return;

    m_statusLabel->setText("Stopping...");
    m_canStop = false;
    m_statusLabel->setText("Import stopped");
    m_isProcessing = false;
    m_importButton->setEnabled(true);
    m_stopButton->setEnabled(false);
}

void TSXImportNode::onImportProgress(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
}

void TSXImportNode::onImportFinished()
{
    QString outputPath = projectPath() + "/" + getOutputNodeName() + "/" + m_outputFileName + ".h5";
    m_importedFilePath = outputPath;

    ImportNodeBase::onImportFinished();

    m_statusLabel->setText("Import completed successfully!");
    m_progressBar->setValue(100);
    m_importButton->setEnabled(true);
    m_stopButton->setEnabled(false);

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

void TSXImportNode::onThreadError(const QString& error)
{
    onError(error);

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

void TSXImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
