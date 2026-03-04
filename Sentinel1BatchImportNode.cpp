#include "Sentinel1BatchImportNode.h"
#include <QFileInfo>
#include <QRegularExpression>

namespace QtNodes {

Sentinel1BatchImportNode::Sentinel1BatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_importButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_manifestPaths()
    , m_importedFilePaths()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

Sentinel1BatchImportNode::~Sentinel1BatchImportNode()
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

QWidget* Sentinel1BatchImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Output node name
    layout->addWidget(new QLabel("Output Node Name:"));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("S1_Batch_Import");
    layout->addWidget(m_outputNodeNameEdit);

    // File list
    layout->addWidget(new QLabel("Manifest Files:"));
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

    // Subswath selection
    layout->addWidget(new QLabel("Subswath:"));
    m_subswathCombo = new QComboBox();
    m_subswathCombo->addItem("iw1");
    m_subswathCombo->addItem("iw2");
    m_subswathCombo->addItem("iw3");
    layout->addWidget(m_subswathCombo);

    // Polarization selection
    layout->addWidget(new QLabel("Polarization:"));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("vv");
    m_polarizationCombo->addItem("vh");
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
    connect(addFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onRemoveFilesClicked);
    connect(m_importButton, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onImportButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onStopButtonClicked);

    return widget;
}

void Sentinel1BatchImportNode::executeImport()
{
    if (m_manifestPaths.isEmpty())
    {
        onError("Please add at least one manifest file.");
        return;
    }

    for (const QString& path : m_manifestPaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("Manifest file does not exist: " + path);
            return;
        }
    }

    QString subswath = m_subswathCombo->currentText();
    QString pol = m_polarizationCombo->currentText();

    std::vector<QString> originalNameList;
    std::vector<QString> importNameList;

    QRegularExpression dateRegex(R"(\d{8})");

    for (const QString& manifestPath : m_manifestPaths)
    {
        QFileInfo fileInfo(manifestPath);
        QString fileName = fileInfo.fileName();

        QRegularExpressionMatch match = dateRegex.match(fileName);
        if (match.hasMatch())
        {
            QString date = match.captured(0);
            originalNameList.push_back(manifestPath);
            importNameList.push_back(QString("%1_%2%3").arg(date).arg(subswath).arg(pol));
        }
        else
        {
            onError("Could not extract date from manifest: " + manifestPath);
            return;
        }
    }

    QString outputNodeName = getOutputNodeName();

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &Sentinel1BatchImportNode::startBatchImport,
            m_workerThread, &MyThread::import_sentinel_patch);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &Sentinel1BatchImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &Sentinel1BatchImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &Sentinel1BatchImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &Sentinel1BatchImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startBatchImport(
        originalNameList,
        importNameList,
        subswath,
        pol,
        projectPath(),
        outputNodeName,
        projectName(),
        projectModel()
    );
}

QString Sentinel1BatchImportNode::getImportedFilePath() const
{
    if (!m_importedFilePaths.isEmpty())
    {
        return m_importedFilePaths.first();
    }

    QString outputNodeName = getOutputNodeName();
    return QString("%1/%2/").arg(projectPath()).arg(outputNodeName);
}

QString Sentinel1BatchImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "S1_Batch_Import";
    }
    return name;
}

void Sentinel1BatchImportNode::onImportButtonClicked()
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

void Sentinel1BatchImportNode::onStopButtonClicked()
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

void Sentinel1BatchImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        m_widget,
        "Select Sentinel-1 Manifest Files",
        QDir::currentPath(),
        "Manifest Files (*.manifest);;All Files (*)"
    );

    for (const QString& file : files)
    {
        if (!m_manifestPaths.contains(file))
        {
            m_manifestPaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());
        }
    }
}

void Sentinel1BatchImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    for (QListWidgetItem* item : selectedItems)
    {
        int row = m_fileListWidget->row(item);
        m_manifestPaths.removeAt(row);
        delete item;
    }
}

void Sentinel1BatchImportNode::onImportProgress(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
}

void Sentinel1BatchImportNode::onImportFinished()
{
    QString subswath = m_subswathCombo->currentText();
    QString pol = m_polarizationCombo->currentText();
    QString outputNodeName = getOutputNodeName();

    m_importedFilePaths.clear();
    for (int i = 0; i < m_manifestPaths.size(); ++i)
    {
        QFileInfo fileInfo(m_manifestPaths[i]);
        QString fileName = fileInfo.fileName();

        QRegularExpression dateRegex(R"(\d{8})");
        QRegularExpressionMatch match = dateRegex.match(fileName);
        if (match.hasMatch())
        {
            QString date = match.captured(0);
            QString importName = QString("%1_%2%3").arg(date).arg(subswath).arg(pol);
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

void Sentinel1BatchImportNode::onThreadError(const QString& error)
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

void Sentinel1BatchImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
