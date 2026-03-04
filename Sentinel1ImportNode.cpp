#include "Sentinel1ImportNode.h"
#include <QFileInfo>
#include <QRegularExpression>

namespace QtNodes {

Sentinel1ImportNode::Sentinel1ImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_manifestEdit(nullptr)
    , m_podEdit(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_importButton(nullptr)
    , m_stopButton(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_manifestPath()
    , m_podPath()
    , m_importedFilePath()
    , m_outputFileName()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

Sentinel1ImportNode::~Sentinel1ImportNode()
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

QWidget* Sentinel1ImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Output node name
    layout->addWidget(new QLabel("Output Node Name:"));
    m_outputNodeNameEdit = new QLineEdit();
    layout->addWidget(m_outputNodeNameEdit);

    // Manifest file selection
    layout->addWidget(new QLabel("Manifest File:"));
    auto* manifestLayout = new QHBoxLayout();
    m_manifestEdit = new QLineEdit();
    QPushButton* manifestBrowse = new QPushButton("Browse...");
    manifestLayout->addWidget(m_manifestEdit);
    manifestLayout->addWidget(manifestBrowse);
    layout->addLayout(manifestLayout);

    // POD file selection
    layout->addWidget(new QLabel("POD File (Optional):"));
    auto* podLayout = new QHBoxLayout();
    m_podEdit = new QLineEdit();
    QPushButton* podBrowse = new QPushButton("Browse...");
    podLayout->addWidget(m_podEdit);
    podLayout->addWidget(podBrowse);
    layout->addLayout(podLayout);

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
    auto* buttonLayout = new QHBoxLayout();
    m_importButton = new QPushButton("Import");
    m_stopButton = new QPushButton("Stop");
    m_stopButton->setEnabled(false);
    buttonLayout->addWidget(m_importButton);
    buttonLayout->addWidget(m_stopButton);
    layout->addLayout(buttonLayout);

    // Connect signals
    connect(manifestBrowse, &QPushButton::clicked, this, &Sentinel1ImportNode::onManifestBrowseClicked);
    connect(podBrowse, &QPushButton::clicked, this, &Sentinel1ImportNode::onPodBrowseClicked);
    connect(m_importButton, &QPushButton::clicked, this, &Sentinel1ImportNode::onImportButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &Sentinel1ImportNode::onStopButtonClicked);

    return widget;
}

void Sentinel1ImportNode::executeImport()
{
    if (m_isProcessing)
        return;

    m_manifestPath = m_manifestEdit->text().trimmed();
    if (m_manifestPath.isEmpty())
    {
        onError("Please select a manifest file.");
        return;
    }

    if (!QFileInfo::exists(m_manifestPath))
    {
        onError("Manifest file does not exist: " + m_manifestPath);
        return;
    }

    m_podPath = m_podEdit->text().trimmed();
    if (!m_podPath.isEmpty() && !QFileInfo::exists(m_podPath))
    {
        onError("POD file does not exist: " + m_podPath);
        return;
    }

    m_outputFileName = generateOutputFileName();
    if (m_outputFileName.isEmpty())
    {
        onError("Could not generate output file name from manifest.");
        return;
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &Sentinel1ImportNode::startImport,
            m_workerThread, &MyThread::import_sentinel);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &Sentinel1ImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &Sentinel1ImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &Sentinel1ImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &Sentinel1ImportNode::onModelUpdated);

    m_thread->start();

    QString subswath = m_subswathCombo->currentText();
    QString polarization = m_polarizationCombo->currentText();
    QString outputNodeName = getOutputNodeName();

    Q_EMIT startImport(
        m_podPath,
        m_manifestPath,
        subswath,
        polarization,
        projectPath(),
        outputNodeName,
        m_outputFileName,
        projectName(),
        projectModel()
    );
}

QString Sentinel1ImportNode::getImportedFilePath() const
{
    if (m_importedFilePath.isEmpty())
    {
        QString outputNodeName = getOutputNodeName();
        QString subswath = m_subswathCombo->currentText();
        QString pol = m_polarizationCombo->currentText();
        return QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(subswath + "_" + pol);
    }
    return m_importedFilePath;
}

QString Sentinel1ImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return generateOutputFileName();
    }
    return name;
}

QString Sentinel1ImportNode::generateOutputFileName() const
{
    QFileInfo fileInfo(m_manifestPath);
    QString fileName = fileInfo.fileName();

    QRegularExpression dateRegex(R"(\d{8})");
    QRegularExpressionMatch match = dateRegex.match(fileName);
    if (match.hasMatch())
    {
        QString date = match.captured(0);
        QString subswath = m_subswathCombo->currentText();
        QString pol = m_polarizationCombo->currentText();
        return QString("%1_%2%3").arg(date).arg(subswath).arg(pol);
    }

    return QString();
}

void Sentinel1ImportNode::onManifestBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        m_widget,
        "Select Sentinel-1 Manifest File",
        QFileInfo(m_manifestPath).absolutePath(),
        "Manifest Files (*.manifest);;All Files (*)"
    );

    if (!filePath.isEmpty())
    {
        m_manifestEdit->setText(filePath);

        QString autoName = generateOutputFileName();
        if (!autoName.isEmpty() && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(autoName);
        }
    }
}

void Sentinel1ImportNode::onPodBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        m_widget,
        "Select POD File",
        QFileInfo(m_podPath).absolutePath(),
        "POD Files (*.EOF *.eofs);;All Files (*)"
    );

    if (!filePath.isEmpty())
    {
        m_podEdit->setText(filePath);
    }
}

void Sentinel1ImportNode::onImportButtonClicked()
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

void Sentinel1ImportNode::onStopButtonClicked()
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

void Sentinel1ImportNode::onImportProgress(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
}

void Sentinel1ImportNode::onImportFinished()
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

void Sentinel1ImportNode::onThreadError(const QString& error)
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

void Sentinel1ImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
