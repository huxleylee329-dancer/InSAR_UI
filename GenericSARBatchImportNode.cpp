#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "GenericSARBatchImportNode.h"
#include <QFile>
#include <QJsonArray>

#include <QDir>
#include <QFileInfo>

namespace QtNodes {

GenericSARBatchImportNode::GenericSARBatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectCombo(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

GenericSARBatchImportNode::~GenericSARBatchImportNode()
{
    if (m_workerThread)
    {
        if (m_thread && m_thread->isRunning())
            m_workerThread->StopProcess();

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
}

QWidget* GenericSARBatchImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Top section: file list (8:2 stretch)
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

    // Bottom section: configuration options
    auto* bottomSection = new QHBoxLayout();
    auto* configLayout = new QVBoxLayout();

    // Project Row [3:7]
    auto* projectRow = new QHBoxLayout();
    projectRow->setStretch(0, 3);
    projectRow->setStretch(1, 7);
    projectRow->addWidget(new QLabel("项目名称："));
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    if (!projectName().isEmpty())
        m_projectCombo->addItem(projectName());
    projectRow->addWidget(m_projectCombo);
    configLayout->addLayout(projectRow);

    // Node Row [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("GenericSAR_Batch_Import");
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    connect(addFiles, &QPushButton::clicked,
            this, &GenericSARBatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked,
            this, &GenericSARBatchImportNode::onRemoveFilesClicked);

    return widget;
}

void GenericSARBatchImportNode::executeImport()
{
    if (m_imagePaths.isEmpty())
    {
        onError("请至少添加一个 通用 SAR 图像文件。");
        return;
    }

    std::vector<QString> originalFileList;
    std::vector<QString> importNameList;

    for (const QString& imagePath : m_imagePaths)
    {
        if (!QFileInfo::exists(imagePath))
        {
            onError("通用 SAR 图像文件不存在：" + imagePath);
            return;
        }

        originalFileList.push_back(imagePath);
        importNameList.push_back(generateImportName(imagePath));
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &GenericSARBatchImportNode::startGenericSARBatchImport,
            m_workerThread, &MyThread::import_GenericSAR_patch);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &GenericSARBatchImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &GenericSARBatchImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &GenericSARBatchImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &GenericSARBatchImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startGenericSARBatchImport(
        projectPath(),
        originalFileList,
        importNameList,
        getOutputNodeName(),
        projectName(),
        projectModel()
    );
}

QString GenericSARBatchImportNode::getImportedFilePath() const
{
    if (!m_importedFilePaths.isEmpty())
        return m_importedFilePaths.first();

    return QString("%1/%2/")
        .arg(projectPath())
        .arg(getOutputNodeName());
}

QString GenericSARBatchImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
        return "GenericSAR_Batch_Import";

    return name;
}

QString GenericSARBatchImportNode::generateImportName(const QString& imagePath) const
{
    QFileInfo fileInfo(imagePath);
    QString baseName = fileInfo.baseName();

    int pos = baseName.lastIndexOf('T');
    if (pos >= 8)
        return baseName.mid(pos - 8, 8);

    return baseName;
}

void GenericSARBatchImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        nullptr,
        tr("选择通用 SAR 图像"),
        QDir::currentPath(),
        tr("Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff)")
    );

    for (const QString& file : files)
    {
        if (!file.isEmpty() && !m_imagePaths.contains(file))
        {
            m_imagePaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());
        }
    }
}

void GenericSARBatchImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();

    for (QListWidgetItem* item : selectedItems)
    {
        int row = m_fileListWidget->row(item);
        m_imagePaths.removeAt(row);
        delete item;
    }
}

void GenericSARBatchImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void GenericSARBatchImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();

    m_importedFilePaths.clear();
    for (const QString& imagePath : m_imagePaths)
    {
        QString importName = generateImportName(imagePath);
        QString filePath = QString("%1/%2/%3.h5")
            .arg(projectPath())
            .arg(outputNodeName)
            .arg(importName);

        m_importedFilePaths.append(filePath);
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

void GenericSARBatchImportNode::onThreadError(const QString& error)
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

void GenericSARBatchImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void GenericSARBatchImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

QJsonObject GenericSARBatchImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_imagePaths)
        pathsArray.append(path);
    json["imagePaths"] = pathsArray;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    return json;
}

void GenericSARBatchImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_imagePaths.clear();
    QJsonArray pathsArray = json["imagePaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_imagePaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("GenericSAR_Batch_Import");

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_imagePaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
}

bool GenericSARBatchImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/";

    QDir dir(outputPath);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0) {
        m_importedFilePaths.clear();
        for (const QString &imagePath : m_imagePaths) {
            QFileInfo fi(imagePath);
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
