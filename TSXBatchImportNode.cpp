#include "TSXBatchImportNode.h"
#include <QFileInfo>

namespace QtNodes {

TSXBatchImportNode::TSXBatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_polarizationCombo(nullptr)
    , m_importButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_xmlPaths()
    , m_importedFilePaths()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

TSXBatchImportNode::~TSXBatchImportNode()
{
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

QWidget* TSXBatchImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Output node name
    layout->addWidget(new QLabel("Output Node Name:"));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("TSX_Batch_Import");
    layout->addWidget(m_outputNodeNameEdit);

    // File list
    layout->addWidget(new QLabel("XML Files:"));
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
    layout->addWidget(m_fileListWidget);

    // Add/Remove buttons
    auto* buttonLayout = new QHBoxLayout();
    QPushButton* addFiles = new QPushButton("Add Files");
    QPushButton* removeFiles = new QPushButton("Remove");
    buttonLayout->addWidget(addFiles);
    buttonLayout->addWidget(removeFiles);
    layout->addLayout(buttonLayout);

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
    auto* importButtonLayout = new QHBoxLayout();
    m_importButton = new QPushButton("Import");
    m_stopButton = new QPushButton("Stop");
    m_stopButton->setEnabled(false);
    importButtonLayout->addWidget(m_importButton);
    importButtonLayout->addWidget(m_stopButton);
    layout->addLayout(importButtonLayout);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &TSXBatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &TSXBatchImportNode::onRemoveFilesClicked);
    connect(m_importButton, &QPushButton::clicked, this, &TSXBatchImportNode::onImportButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &TSXBatchImportNode::onStopButtonClicked);

    return widget;
}

void TSXBatchImportNode::executeImport()
{
    if (m_xmlPaths.isEmpty())
    {
        onError("Please add at least one XML file.");
        return;
    }

    for (const QString& path : m_xmlPaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("XML file does not exist: " + path);
            return;
        }
    }

    std::vector<QString> originalFileList;
    std::vector<QString> importNameList;

    for (const QString& xmlPath : m_xmlPaths)
    {
        QString importName = generateOutputFileName(xmlPath);
        if (importName.isEmpty())
        {
            onError("Could not generate output file name from XML: " + xmlPath);
            return;
        }
        originalFileList.push_back(xmlPath);
        importNameList.push_back(importName);
    }

    QString polarization = m_polarizationCombo->currentText();
    QString outputNodeName = getOutputNodeName();

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &TSXBatchImportNode::startTSXBatchImport,
            m_workerThread, &MyThread::import_TSX_patch);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &TSXBatchImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &TSXBatchImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &TSXBatchImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &TSXBatchImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startTSXBatchImport(
        polarization,
        projectPath(),
        originalFileList,
        importNameList,
        outputNodeName,
        projectName(),
        projectModel()
    );
}

QString TSXBatchImportNode::getImportedFilePath() const
{
    if (!m_importedFilePaths.isEmpty())
    {
        return m_importedFilePaths.first();
    }

    QString outputNodeName = getOutputNodeName();
    return QString("%1/%2/").arg(projectPath()).arg(outputNodeName);
}

QString TSXBatchImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "TSX_Batch_Import";
    }
    return name;
}

QString TSXBatchImportNode::generateOutputFileName(const QString& xmlPath) const
{
    QFileInfo fileInfo(xmlPath);
    QString baseName = fileInfo.baseName();

    // Find the last "T" in the filename and extract 8 characters before it
    int pos = baseName.lastIndexOf('T');
    if (pos >= 8)
    {
        return baseName.mid(pos - 8, 8);
    }

    return QString();
}

void TSXBatchImportNode::onImportButtonClicked()
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

void TSXBatchImportNode::onStopButtonClicked()
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

void TSXBatchImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        m_widget,
        "Select TerraSAR-X XML Files",
        QDir::currentPath(),
        "XML Files (*.xml);;All Files (*)"
    );

    for (const QString& file : files)
    {
        if (!m_xmlPaths.contains(file))
        {
            m_xmlPaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());
        }
    }
}

void TSXBatchImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    for (QListWidgetItem* item : selectedItems)
    {
        int row = m_fileListWidget->row(item);
        m_xmlPaths.removeAt(row);
        delete item;
    }
}

void TSXBatchImportNode::onImportProgress(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
}

void TSXBatchImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();
    m_importedFilePaths.clear();
    for (int i = 0; i < m_xmlPaths.size(); ++i)
    {
        QString importName = generateOutputFileName(m_xmlPaths[i]);
        if (!importName.isEmpty())
        {
            QString filePath = QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(importName);
            m_importedFilePaths.append(filePath);
        }
    }

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

void TSXBatchImportNode::onThreadError(const QString& error)
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

void TSXBatchImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
