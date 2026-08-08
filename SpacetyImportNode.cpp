#include "InSARLogManager.h"
#include "SpacetyImportNode.h"
#include "SpacetyImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

SpacetyImportNode::SpacetyImportNode()
    : ImportNodeBase()
    , m_widget(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectLabel(nullptr)
    , m_spotlightCheckBox(nullptr)
    , m_dataFiles()
    , m_xmlFiles()
    , m_outputNodeName("Spacety_Batch_Import")
    , m_spotlightMode(false)
{
}

ProductOutputContract SpacetyImportNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("spacety_import.output.complex_sar")
        : QStringLiteral("spacety_import.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("complex_sar")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

QWidget* SpacetyImportNode::createWidget()
{
    auto* widget = new QWidget();
    widget->setFixedWidth(300);
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
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
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

    // Spotlight mode checkbox
    m_spotlightCheckBox = new QCheckBox("聚束模式测试");
    m_spotlightCheckBox->setChecked(m_spotlightMode);
    connect(m_spotlightCheckBox, &QCheckBox::toggled, this, [this, invalidateNodeData](bool checked) {
        if (m_spotlightMode != checked) {
            if (!confirmParameterChange()) {
                m_spotlightCheckBox->setChecked(m_spotlightMode);
                return;
            }
            m_spotlightMode = checked;
            invalidateNodeData();
        }
    });
    configLayout->addWidget(m_spotlightCheckBox);

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &SpacetyImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &SpacetyImportNode::onRemoveFilesClicked);

    return widget;
}

void SpacetyImportNode::executeImport()
{
    if (m_dataFiles.isEmpty() || m_xmlFiles.isEmpty())
    {
        onError("请至少添加一个数据集文件。");
        return;
    }

    for (int i = 0; i < m_dataFiles.size(); ++i)
    {
        if (!QFileInfo::exists(m_dataFiles[i]))
        {
            onError("数据文件不存在：" + m_dataFiles[i]);
            return;
        }
        if (i < m_xmlFiles.size() && !QFileInfo::exists(m_xmlFiles[i]))
        {
            onError("参数XML文件不存在：" + m_xmlFiles[i]);
            return;
        }
    }

    std::vector<ImportTask> tasks;
    for (int i = 0; i < m_dataFiles.size(); ++i)
    {
        QString dataFilePath = m_dataFiles[i];
        QString importName = generateOutputFileName(dataFilePath);
        if (importName.isEmpty())
        {
            onError("无法从数据文件生成输出文件名：" + dataFilePath);
            return;
        }

        ImportTask task;
        task.filename = importName;
        task.arguments = QStringList{ dataFilePath, i < m_xmlFiles.size() ? m_xmlFiles[i] : "", QString::number(m_spotlightMode ? 1 : 0) };
        tasks.push_back(task);
    }

    startWorker(new SpacetyImportWorker(), tasks);
}

QStringList SpacetyImportNode::getExpectedOutputFilePaths() const
{
    QStringList expectedPaths;
    QString outputNodeName = getOutputNodeName();
    for (int i = 0; i < m_dataFiles.size(); ++i)
    {
        QString importName = generateOutputFileName(m_dataFiles[i]);
        if (!importName.isEmpty())
        {
            expectedPaths.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
        }
    }
    return expectedPaths;
}

QString SpacetyImportNode::getOutputNodeName() const
{
    if (m_outputNodeNameEdit) {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (!name.isEmpty())
            return name;
    }
    return m_outputNodeName.isEmpty() ? "Spacety_Batch_Import" : m_outputNodeName;
}

QString SpacetyImportNode::generateOutputFileName(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    return fileInfo.baseName();
}

void SpacetyImportNode::onAddFilesClicked()
{
    QString dataFile = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择 Spacety 数据文件"),
        QDir::currentPath(),
        tr("数据文件 (*.tiff *.h5);;所有文件 (*)")
    );
    if (dataFile.isEmpty()) return;

    QString xmlFile = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择参数 XML 文件"),
        QFileInfo(dataFile).absolutePath(),
        tr("XML 文件 (*.xml);;所有文件 (*)")
    );
    if (xmlFile.isEmpty()) return;

    if (!m_dataFiles.contains(dataFile))
    {
        m_dataFiles.append(dataFile);
        m_xmlFiles.append(xmlFile);
        m_fileListWidget->addItem(QFileInfo(dataFile).fileName() + " | " + QFileInfo(xmlFile).fileName());

        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

void SpacetyImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_dataFiles.removeAt(row);
            if (row < m_xmlFiles.size()) {
                m_xmlFiles.removeAt(row);
            }
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

QJsonObject SpacetyImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray dataPathsArray;
    for (const QString &path : m_dataFiles)
        dataPathsArray.append(path);
    json["dataPaths"] = dataPathsArray;

    QJsonArray xmlPathsArray;
    for (const QString &path : m_xmlFiles)
        xmlPathsArray.append(path);
    json["xmlPaths"] = xmlPathsArray;

    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    json["spotlightMode"] = m_spotlightCheckBox ? m_spotlightCheckBox->isChecked() : m_spotlightMode;
    return json;
}

void SpacetyImportNode::load(QJsonObject const &json)
{
    m_dataFiles.clear();
    QJsonArray dataPathsArray = json["dataPaths"].toArray();
    for (const QJsonValue &val : dataPathsArray)
        m_dataFiles.append(val.toString());

    m_xmlFiles.clear();
    QJsonArray xmlPathsArray = json["xmlPaths"].toArray();
    for (const QJsonValue &val : xmlPathsArray)
        m_xmlFiles.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("Spacety_Batch_Import");
    m_spotlightMode = json["spotlightMode"].toBool(false);

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (int i = 0; i < m_dataFiles.size(); ++i) {
            QString dataName = QFileInfo(m_dataFiles[i]).fileName();
            QString xmlName = (i < m_xmlFiles.size()) ? QFileInfo(m_xmlFiles[i]).fileName() : QString();
            m_fileListWidget->addItem(dataName + " | " + xmlName);
        }
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    if (m_spotlightCheckBox)
        m_spotlightCheckBox->setChecked(m_spotlightMode);
}

} // namespace QtNodes
