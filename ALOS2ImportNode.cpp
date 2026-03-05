#include "ALOS2ImportNode.h"
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

ALOS2ImportNode::ALOS2ImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_importButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_imgPaths()
    , m_importedFilePaths()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

ALOS2ImportNode::~ALOS2ImportNode()
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

QWidget* ALOS2ImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Output node name
    layout->addWidget(new QLabel("Output Node Name:"));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("ALOS2_Import");
    layout->addWidget(m_outputNodeNameEdit);

    // File list
    layout->addWidget(new QLabel("IMG Files:"));
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
    connect(addFiles, &QPushButton::clicked, this, &ALOS2ImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &ALOS2ImportNode::onRemoveFilesClicked);
    connect(m_importButton, &QPushButton::clicked, this, &ALOS2ImportNode::onImportButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &ALOS2ImportNode::onStopButtonClicked);

    return widget;
}

void ALOS2ImportNode::executeImport()
{
    if (m_imgPaths.isEmpty())
    {
        onError("Please add at least one IMG file.");
        return;
    }

    for (const QString& path : m_imgPaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("IMG file does not exist: " + path);
            return;
        }
    }

    std::vector<QString> imgFileList;
    std::vector<QString> ledFileList;
    std::vector<QString> importNameList;

    for (const QString& imgPath : m_imgPaths)
    {
        QString importName = generateOutputFileName(imgPath);
        if (importName.isEmpty())
        {
            onError("Could not generate output file name from IMG: " + imgPath);
            return;
        }

        QString ledPath = generateLEDPath(imgPath);
        if (!QFileInfo::exists(ledPath))
        {
            onError("LED file not found for IMG: " + imgPath);
            return;
        }

        imgFileList.push_back(imgPath);
        ledFileList.push_back(ledPath);
        importNameList.push_back(importName);
    }

    QString outputNodeName = getOutputNodeName();

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &ALOS2ImportNode::startALOS2Import,
            m_workerThread, &MyThread::import_ALOS2_patch);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &ALOS2ImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &ALOS2ImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &ALOS2ImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &ALOS2ImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startALOS2Import(
        projectPath(),
        imgFileList,
        ledFileList,
        importNameList,
        outputNodeName,
        projectName(),
        projectModel()
    );
}

QString ALOS2ImportNode::getImportedFilePath() const
{
    if (!m_importedFilePaths.isEmpty())
    {
        return m_importedFilePaths.first();
    }

    QString outputNodeName = getOutputNodeName();
    return QString("%1/%2/").arg(projectPath()).arg(outputNodeName);
}

QString ALOS2ImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "ALOS2_Import";
    }
    return name;
}

QString ALOS2ImportNode::generateOutputFileName(const QString& imgPath) const
{
    QFileInfo fileInfo(imgPath);
    QString baseName = fileInfo.baseName();

    // ALOS-2 IMG file name is 39 characters
    // Position 4-5: Polarization (2 chars)
    // Position 22-27: Date (6 chars, without century)
    // Format: 20 + date + "_" + polarization
    if (baseName.length() == 39)
    {
        QString polarization = baseName.mid(4, 2);
        QString date = "20" + baseName.mid(22, 6);
        return date + "_" + polarization;
    }

    return QString();
}

QString ALOS2ImportNode::generateLEDPath(const QString& imgPath) const
{
    QFileInfo imgFileInfo(imgPath);
    QString imgBaseName = imgFileInfo.baseName();

    // LED file has same numbering as IMG file
    // IMG: ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx (39 chars)
    // LED: LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx (same except LED prefix)
    // The numbering part is after the last '-' and before the file extension
    // Actually, looking at ALOS-2 naming, LED files are usually:
    // LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx (same base structure as IMG)
    // LED files are in the same directory with "LED" prefix instead of "IMG"

    if (imgBaseName.length() == 39)
    {
        // The number is at position 34-36 (001)
        QString number = imgBaseName.mid(34, 3);
        // Get the directory
        QDir dir = imgFileInfo.absoluteDir();
        // Construct LED file name: LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx
        QString ledBaseName = "LED-" + imgBaseName.mid(3);  // Remove "IMG-" and add "LED-"
        QString ledPath = dir.filePath(ledBaseName + ".LED");
        return ledPath;
    }

    return QString();
}

void ALOS2ImportNode::onImportButtonClicked()
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

void ALOS2ImportNode::onStopButtonClicked()
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

void ALOS2ImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        m_widget,
        "Select ALOS-2 IMG Files",
        QDir::currentPath(),
        "IMG Files (*.IMG *.img);;All Files (*)"
    );

    for (const QString& file : files)
    {
        if (!m_imgPaths.contains(file))
        {
            m_imgPaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());
        }
    }
}

void ALOS2ImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    for (QListWidgetItem* item : selectedItems)
    {
        int row = m_fileListWidget->row(item);
        m_imgPaths.removeAt(row);
        delete item;
    }
}

void ALOS2ImportNode::onImportProgress(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
}

void ALOS2ImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();
    m_importedFilePaths.clear();
    for (int i = 0; i < m_imgPaths.size(); ++i)
    {
        QString importName = generateOutputFileName(m_imgPaths[i]);
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

void ALOS2ImportNode::onThreadError(const QString& error)
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

void ALOS2ImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
