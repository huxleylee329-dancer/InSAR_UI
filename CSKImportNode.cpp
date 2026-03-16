#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "CSKImportNode.h"
#include <QFileInfo>

namespace QtNodes {

CSKImportNode::CSKImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectCombo(nullptr)
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
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Top section: file list (8:2 stretch) - stretch 4
    auto* topSection = new QHBoxLayout();
    topSection->setStretch(0, 8);
    topSection->setStretch(1, 2);

    // Left side: file list widget
    m_fileListWidget = new QListWidget();
    topSection->addWidget(m_fileListWidget);

    // Right side: add/remove buttons
    auto* buttonLayout = new QVBoxLayout();
    QPushButton* addFiles = new QPushButton("添加");
    QPushButton* removeFiles = new QPushButton("移除");
    buttonLayout->addWidget(addFiles);
    buttonLayout->addWidget(removeFiles);
    topSection->addLayout(buttonLayout);

    mainLayout->addLayout(topSection, 4);

    // Bottom section: configuration options - stretch 4
    auto* bottomSection = new QHBoxLayout();
    auto* configLayout = new QVBoxLayout();

    // Target project [3:7]
    auto* projectRow = new QHBoxLayout();
    projectRow->setStretch(0, 3);
    projectRow->setStretch(1, 7);
    projectRow->addWidget(new QLabel("目标工程："));
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    if (!projectName().isEmpty())
    {
        m_projectCombo->addItem(projectName());
    }
    projectRow->addWidget(m_projectCombo);
    configLayout->addLayout(projectRow);

    // Target node [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("CSK_Batch_Import");
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    // CSK does not need polarization selection (auto-detect)

    // Progress bar row [5:5]
    auto* progressRow = new QHBoxLayout();
    progressRow->setStretch(0, 5);
    progressRow->setStretch(1, 5);
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    progressRow->addWidget(m_progressBar);
    progressRow->addStretch();
    configLayout->addLayout(progressRow);

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &CSKImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &CSKImportNode::onRemoveFilesClicked);

    return widget;
}

void CSKImportNode::executeImport()
{
    if (m_filePaths.isEmpty())
    {
        onError("请至少添加一个 H5 文件。");
        return;
    }

    for (const QString& path : m_filePaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("H5 文件不存在：" + path);
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
            onError("无法从 H5 文件生成输出文件名：" + filePath);
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
        return "CSK_Batch_Import";
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

void CSKImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        m_widget,
        "导入 COSMO-SkyMed 数据",
        QDir::currentPath(),
        "H5 文件 (*.h5)"
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
    Q_UNUSED(message);  // Ignore message
    m_progressBar->setValue(progress);
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

    m_progressBar->setValue(100);

    // Clean up thread (consistent with TSXBatchImportNode)
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
