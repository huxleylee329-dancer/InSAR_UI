#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "ALOS2ImportNode.h"
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

ALOS2ImportNode::ALOS2ImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectCombo(nullptr)
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
    m_outputNodeNameEdit->setText("ALOS2_Batch_Import");
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &ALOS2ImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &ALOS2ImportNode::onRemoveFilesClicked);

    return widget;
}

void ALOS2ImportNode::executeImport()
{
    if (m_imgPaths.isEmpty())
    {
        onError("请至少添加一个 IMG 文件。");
        return;
    }

    for (const QString& path : m_imgPaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("IMG 文件不存在：" + path);
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
            onError("无法从 IMG 文件生成输出文件名：" + imgPath);
            return;
        }
        imgFileList.push_back(imgPath);

        QString ledPath = generateLEDPath(imgPath);
        if (!QFileInfo::exists(ledPath))
        {
            onError("IMG 文件对应的 LED 文件未找到：" + imgPath);
            return;
        }
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
        return "ALOS2_Batch_Import";
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
    // LED: LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx (same base structure as IMG)
    // LED files are in the same directory with "LED" prefix instead of "IMG"
    // The numbering part is at position 34-36 (001)
    // Actually, looking at ALOS-2 naming, LED files are usually:
    // LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx (same base structure as IMG)
    // LED files are in the same directory with "LED" prefix instead of "IMG"
    // The number is at position 34-36 (001)
    // Get the directory
    QDir dir = imgFileInfo.absoluteDir();
    // Construct LED file name: LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx
    // Remove "IMG-" and add "LED-"
    QString ledBaseName = "LED-" + imgBaseName.mid(3);  // Remove "IMG-" and add "LED-"
    QString ledPath = dir.filePath(ledBaseName + ".LED");
    return ledPath;
}

void ALOS2ImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        nullptr,
        tr("导入 ALOS-2 数据"),
        QDir::currentPath(),
        tr("IMG 文件 (*.IMG *.img)")
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
    Q_UNUSED(message);  // Ignore message
    setProgress(progress);
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

void ALOS2ImportNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ImportNodeBase::setExecutionMode(mode);
}

void ALOS2ImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

QJsonObject ALOS2ImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_imgPaths)
        pathsArray.append(path);
    json["imgPaths"] = pathsArray;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : QStringLiteral("ALOS2_Batch_Import");
    return json;
}

void ALOS2ImportNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);
    m_imgPaths.clear();
    QJsonArray pathsArray = json["imgPaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_imgPaths.append(val.toString());

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_imgPaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(json["outputNodeName"].toString("ALOS2_Batch_Import"));
}

} // namespace QtNodes
