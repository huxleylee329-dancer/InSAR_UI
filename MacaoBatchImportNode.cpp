#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "MacaoBatchImportNode.h"

#include <QDir>
#include <QFileInfo>

namespace QtNodes {

MacaoBatchImportNode::MacaoBatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectCombo(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

MacaoBatchImportNode::~MacaoBatchImportNode()
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

QWidget* MacaoBatchImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto* topSection = new QHBoxLayout();

    m_fileListWidget = new QListWidget();
    topSection->addWidget(m_fileListWidget);

    auto* buttonLayout = new QVBoxLayout();
    QPushButton* addFiles = new QPushButton("添加");
    QPushButton* removeFiles = new QPushButton("移除");
    buttonLayout->addWidget(addFiles);
    buttonLayout->addWidget(removeFiles);
    topSection->addLayout(buttonLayout);

    mainLayout->addLayout(topSection);

    auto* projectRow = new QHBoxLayout();
    projectRow->addWidget(new QLabel("目标工程："));
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    if (!projectName().isEmpty())
        m_projectCombo->addItem(projectName());
    projectRow->addWidget(m_projectCombo);
    mainLayout->addLayout(projectRow);

    auto* nodeRow = new QHBoxLayout();
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("Macao_Batch_Import");
    nodeRow->addWidget(m_outputNodeNameEdit);
    mainLayout->addLayout(nodeRow);

    connect(addFiles, &QPushButton::clicked,
            this, &MacaoBatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked,
            this, &MacaoBatchImportNode::onRemoveFilesClicked);

    return widget;
}

void MacaoBatchImportNode::executeImport()
{
    if (m_imagePaths.isEmpty())
    {
        onError("请至少添加一个 Macao 图像文件。");
        return;
    }

    std::vector<QString> originalFileList;
    std::vector<QString> importNameList;

    for (const QString& imagePath : m_imagePaths)
    {
        if (!QFileInfo::exists(imagePath))
        {
            onError("Macao 图像文件不存在：" + imagePath);
            return;
        }

        originalFileList.push_back(imagePath);
        importNameList.push_back(generateImportName(imagePath));
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &MacaoBatchImportNode::startMacaoBatchImport,
            m_workerThread, &MyThread::import_Macao_patch);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &MacaoBatchImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &MacaoBatchImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &MacaoBatchImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &MacaoBatchImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startMacaoBatchImport(
        projectPath(),
        originalFileList,
        importNameList,
        getOutputNodeName(),
        projectName(),
        projectModel()
    );
}

QString MacaoBatchImportNode::getImportedFilePath() const
{
    if (!m_importedFilePaths.isEmpty())
        return m_importedFilePaths.first();

    return QString("%1/%2/")
        .arg(projectPath())
        .arg(getOutputNodeName());
}

QString MacaoBatchImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
        return "Macao_Batch_Import";

    return name;
}

QString MacaoBatchImportNode::generateImportName(const QString& imagePath) const
{
    QFileInfo fileInfo(imagePath);
    QString baseName = fileInfo.baseName();

    int pos = baseName.lastIndexOf('T');
    if (pos >= 8)
        return baseName.mid(pos - 8, 8);

    return baseName;
}

void MacaoBatchImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        _widget,
        "导入 Macao 数据",
        QDir::currentPath(),
        "Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff)"
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

void MacaoBatchImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();

    for (QListWidgetItem* item : selectedItems)
    {
        int row = m_fileListWidget->row(item);
        m_imagePaths.removeAt(row);
        delete item;
    }
}

void MacaoBatchImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void MacaoBatchImportNode::onImportFinished()
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

void MacaoBatchImportNode::onThreadError(const QString& error)
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

void MacaoBatchImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void MacaoBatchImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
