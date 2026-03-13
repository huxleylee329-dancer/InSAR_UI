#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

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
    , m_projectCombo(nullptr)
    , m_progressBar(nullptr)
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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // File list row [8:2]
    auto* fileListRow = new QHBoxLayout();
    fileListRow->setStretch(0, 8);  // QListWidget 占 8 份
    fileListRow->setStretch(1, 2);  // 按钮占 2 份

    auto* fileListLayout = new QVBoxLayout();
    fileListLayout->setContentsMargins(0, 0, 0, 0);
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
    fileListLayout->addWidget(m_fileListWidget);
    fileListRow->addLayout(fileListLayout);

    auto* buttonColLayout = new QVBoxLayout();
    buttonColLayout->setContentsMargins(0, 0, 0, 0);
    buttonColLayout->setSpacing(5);
    QPushButton* addFiles = new QPushButton("添加");
    QPushButton* removeFiles = new QPushButton("移除");
    buttonColLayout->addWidget(addFiles);
    buttonColLayout->addWidget(removeFiles);
    buttonColLayout->addStretch();
    fileListRow->addLayout(buttonColLayout);
    layout->addLayout(fileListRow);

    // Subswath row [3:7]
    auto* subswathRow = new QHBoxLayout();
    subswathRow->setStretch(0, 3);
    subswathRow->setStretch(1, 7);
    subswathRow->addWidget(new QLabel("子带选择："));
    m_subswathCombo = new QComboBox();
    m_subswathCombo->addItem("iw1");
    m_subswathCombo->addItem("iw2");
    m_subswathCombo->addItem("iw3");
    subswathRow->addWidget(m_subswathCombo);
    layout->addLayout(subswathRow);

    // Polarization row [3:7]
    auto* polRow = new QHBoxLayout();
    polRow->setStretch(0, 3);
    polRow->setStretch(1, 7);
    polRow->addWidget(new QLabel("极化方式："));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("vv");
    m_polarizationCombo->addItem("vh");
    polRow->addWidget(m_polarizationCombo);
    layout->addLayout(polRow);

    // Target project row [3:7]
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
    layout->addLayout(projectRow);

    // Output node name row [3:7]
    auto* nameRow = new QHBoxLayout();
    nameRow->setStretch(0, 3);
    nameRow->setStretch(1, 7);
    nameRow->addWidget(new QLabel("目标节点名："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("S1_Batch_Import");
    nameRow->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nameRow);

    // Progress bar row [5:5]
    auto* progressRow = new QHBoxLayout();
    progressRow->setStretch(0, 5);
    progressRow->setStretch(1, 5);
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    progressRow->addWidget(m_progressBar);
    progressRow->addStretch();
    layout->addLayout(progressRow);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onRemoveFilesClicked);

    return widget;
}

void Sentinel1BatchImportNode::executeImport()
{
    if (m_manifestPaths.isEmpty())
    {
        onError("请至少添加一个清单文件。");
        return;
    }

    for (const QString& path : m_manifestPaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("清单文件不存在：" + path);
            return;
        }
    }

    std::vector<QString> originalNameList;
    std::vector<QString> importNameList;

    for (const QString& manifestPath : m_manifestPaths)
    {
        QString importName = generateImportName(manifestPath);
        if (importName.isEmpty())
        {
            onError("无法从清单文件提取日期：" + manifestPath);
            return;
        }
        originalNameList.push_back(manifestPath);
        importNameList.push_back(importName);
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

    QString subswath = m_subswathCombo->currentText();
    QString pol = m_polarizationCombo->currentText();

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

QString Sentinel1BatchImportNode::generateImportName(const QString& manifestPath) const
{
    QFileInfo fileInfo(manifestPath);
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

void Sentinel1BatchImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        m_widget,
        "选择哨兵一号清单文件",
        QDir::currentPath(),
        "清单文件 (*.manifest);;所有文件 (*)"
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
    Q_UNUSED(message);
    m_progressBar->setValue(progress);
}

void Sentinel1BatchImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();

    m_importedFilePaths.clear();
    for (int i = 0; i < m_manifestPaths.size(); ++i)
    {
        QString importName = generateImportName(m_manifestPaths[i]);
        if (!importName.isEmpty())
        {
            QString filePath = QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(importName);
            m_importedFilePaths.append(filePath);
        }
    }

    ImportNodeBase::onImportFinished();

    m_progressBar->setValue(100);

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
