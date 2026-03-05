#include "CSKImportNode.h"
#include <QFileInfo>

namespace QtNodes {

CSKImportNode::CSKImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_importButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_filePaths()
    , m_importedFilePaths()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

CSKImportNode::~CSKImportNode()
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

QWidget* CSKImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Output node name
    layout->addWidget(new QLabel("Output Node Name:"));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("CSK_Import");
    layout->addWidget(m_outputNodeNameEdit);

    // File list
    layout->addWidget(new QLabel("CSK Files (.h5):"));
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
    connect(addFiles, &QPushButton::clicked, this, &CSKImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &CSKImportNode::onRemoveFilesClicked);
    connect(m_importButton, &QPushButton::clicked, this, &CSKImportNode::onImportButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &CSKImportNode::onStopButtonClicked);

    return widget;
}

void CSKImportNode::executeImport()
{
    if (m_filePaths.isEmpty())
    {
        onError("Please add at least one CSK file.");
        return;
    }

    for (const QString& path : m_filePaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("CSK file does not exist: " + path);
            return;
        }
    }

    std::vector<QString> originalFileList;
    std::vector<QString> importNameList;

    for (const QString& filePath : m_filePaths)
    {
        QString importName = generateOutputFileName(filePath);
        if (importName.isEmpty())
        {
            onError("Could not generate output file name from: " + filePath);
            return;
        }
        originalFileList.push_back(filePath);
        importNameList.push_back(importName);
    }

    QString outputNodeName = getOutputNodeName();

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &CSKImportNode::startCSKImport,
            m_workerThread, &MyThread::import_CSK_patch);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &CSKImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &CSKImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &CSKImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &CSKImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startCSKImport(
        projectPath(),
        originalFileList,
        importNameList,
        outputNodeName,
        projectName(),
        projectModel()
    );
}

QString CSKImportNode::getImportedFilePath() const
{
    if (!m_importedFilePaths.isEmpty())
    {
        return m_importedFilePaths.first();
    }

    QString outputNodeName = getOutputNodeName();
    return QString("%1/%2/").arg(projectPath()).arg(outputNodeName);
}

QString CSKImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "CSK_Import";
    }
    return name;
}

QString CSKImportNode::generateOutputFileName(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    QString baseName = fileInfo.baseName();

    // CSK file name is 82 characters
    // Position 29-30: Polarization (2 chars)
    // Position 37-44: Date (8 chars)
    // Format: date_polarization
    if (baseName.length() == 82)
    {
        QString polarization = baseName.mid(29, 2);
        QString date = baseName.mid(37, 8);
        return date + "_" + polarization;
    }

    return QString();
}

void CSKImportNode::onImportButtonClicked()
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

void CSKImportNode::onStopButtonClicked()
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

void CSKImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        m_widget,
        "Select COSMO-SkyMed Files",
        QDir::currentPath(),
        "CSK Files (*.h5);;All Files (*)"
    );

    for (const QString& file : files)
    {
        if (!m_filePaths.contains(file))
        {
            m_filePaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());
        }
    }
}

void CSKImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    for (QListWidgetItem* item : selectedItems)
    {
        int row = m_fileListWidget->row(item);
        m_filePaths.removeAt(row);
        delete item;
    }
}

void CSKImportNode::onImportProgress(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
}

void CSKImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();
    m_importedFilePaths.clear();
    for (int i = 0; i < m_filePaths.size(); ++i)
    {
        QString importName = generateOutputFileName(m_filePaths[i]);
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

void CSKImportNode::onThreadError(const QString& error)
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

void CSKImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
