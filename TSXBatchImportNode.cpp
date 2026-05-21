
#include "TSXBatchImportNode.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

TSXBatchImportNode::TSXBatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_polarizationCombo(nullptr)
    , m_projectCombo(nullptr)
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
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Top section: file list (8:2 stretch) - stretch 4
    auto* topSection = new QHBoxLayout();
    topSection->setStretch(0, 8);
    topSection->setStretch(1, 2);

    // Left side: file list widget
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
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
    m_outputNodeNameEdit->setText("TSX_Batch_Import");
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    // Polarization [3:7]
    auto* polRow = new QHBoxLayout();
    polRow->setStretch(0, 3);
    polRow->setStretch(1, 7);
    polRow->addWidget(new QLabel("极化方式："));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("HH");
    m_polarizationCombo->addItem("VV");
    polRow->addWidget(m_polarizationCombo);
    configLayout->addLayout(polRow);

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &TSXBatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &TSXBatchImportNode::onRemoveFilesClicked);

    return widget;
}

void TSXBatchImportNode::executeImport()
{
    if (m_xmlPaths.isEmpty())
    {
        onError("请至少添加一个 XML 文件。");
        return;
    }

    for (const QString& path : m_xmlPaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("XML 文件不存在：" + path);
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
            onError("无法从 XML 文件生成输出文件名：" + xmlPath);
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

void TSXBatchImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        nullptr,
        tr("导入 TerraSAR-X/TanDEM-X 数据"),
        QDir::currentPath(),
        tr("XML 文件 (*.xml)")
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
    Q_UNUSED(message);
    setProgress(progress);
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

void TSXBatchImportNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ImportNodeBase::setExecutionMode(mode);
}

void TSXBatchImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

QJsonObject TSXBatchImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_xmlPaths)
        pathsArray.append(path);
    json["xmlPaths"] = pathsArray;
    json["polarization"] = m_polarizationCombo ? m_polarizationCombo->currentText() : "HH";
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    return json;
}

void TSXBatchImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_xmlPaths.clear();
    QJsonArray pathsArray = json["xmlPaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_xmlPaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("TSX_Batch_Import");

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_xmlPaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    QString pol = json["polarization"].toString("HH");
    if (m_polarizationCombo) {
        int idx = m_polarizationCombo->findText(pol);
        if (idx >= 0) m_polarizationCombo->setCurrentIndex(idx);
    }
}

bool TSXBatchImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/";

    QDir dir(outputPath);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0) {
        m_importedFilePaths.clear();
        for (const QString &xmlPath : m_xmlPaths) {
            QFileInfo fi(xmlPath);
            QString importedPath = outputPath + fi.completeBaseName() + ".h5";
            if (QFile::exists(importedPath)) {
                m_importedFilePaths.append(importedPath);
            }
        }
        if (!m_importedFilePaths.isEmpty()) {
            auto outputData = std::make_shared<ImportedFileData>(outputPath, nodeName);
            setOutputData(0, outputData);
            return true;
        }
    }

    return false;
}

} // namespace QtNodes
