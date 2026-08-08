#include "InSARLogManager.h"

#include "CSKImportNode.h"
#include "CSKImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

CSKImportNode::CSKImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectLabel(nullptr)
    , m_importButton(nullptr)
    , m_stopButton(nullptr)
    , m_filePaths()
    , m_outputNodeName()
{
}

ProductOutputContract CSKImportNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("csk_import.output.complex_sar")
        : QStringLiteral("csk_import.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("complex_sar")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

QWidget* CSKImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

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

    // Project Name Badge
    m_projectLabel = createProjectBadge(projectName());
    mainLayout->addWidget(m_projectLabel);

    // Bottom section: configuration options - stretch 4
    auto* bottomSection = new QHBoxLayout();
    auto* configLayout = new QVBoxLayout();

    // Target node [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName.isEmpty() ? "CSK_Batch_Import" : m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            NodeUtils::removeDataNodeFromProject(getProjectContext(), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    // CSK does not need polarization selection (auto-detect)

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

    std::vector<ImportTask> tasks;
    for (const QString& filePath : m_filePaths)
    {
        QString importName = generateOutputFileName(filePath);
        if (importName.isEmpty())
        {
            onError("无法从 H5 文件生成输出文件名：" + filePath);
            return;
        }
        ImportTask task;
        task.filename = importName;
        task.arguments = QStringList{ filePath };
        tasks.push_back(task);
    }

    startWorker(new CSKImportWorker(), tasks);
}

QStringList CSKImportNode::getExpectedOutputFilePaths() const
{
    QStringList expectedPaths;
    QString outputNodeName = getOutputNodeName();
    for (const QString& filePath : m_filePaths)
    {
        QString importName = generateOutputFileName(filePath);
        if (!importName.isEmpty())
        {
            expectedPaths.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
        }
    }
    return expectedPaths;
}

QString CSKImportNode::getOutputNodeName() const
{
    if (m_outputNodeNameEdit) {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (!name.isEmpty())
            return name;
    }
    return m_outputNodeName.isEmpty() ? "CSK_Batch_Import" : m_outputNodeName;
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
        nullptr,
        tr("导入 COSMO-SkyMed 数据"),
        QDir::currentPath(),
        tr("H5 文件 (*.h5)")
    );

    for (const QString& file : files)
    {
        if (!m_filePaths.contains(file))
        {
            m_filePaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());

            int outCount = nPorts(PortType::Out);
            for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
            invalidateExecution();
        }
    }
}

void CSKImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_filePaths.removeAt(row);
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

QJsonObject CSKImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_filePaths)
        pathsArray.append(path);
    json["filePaths"] = pathsArray;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    return json;
}

void CSKImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_filePaths.clear();
    QJsonArray pathsArray = json["filePaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_filePaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("CSK_Batch_Import");

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_filePaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
}

} // namespace QtNodes
