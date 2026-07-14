#include "InSARLogManager.h"
#include "AIRSATImportNode.h"
#include "AIRSATImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

AIRSATImportNode::AIRSATImportNode()
    : ImportNodeBase()
    , m_widget(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectLabel(nullptr)
    , m_dataFilePaths()
    , m_xmlFilePaths()
    , m_outputNodeName()
{
}

QWidget* AIRSATImportNode::createWidget()
{
    auto* widget = new QWidget();
    widget->setFixedWidth(300);
    m_widget = widget;

    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // Top section: file list
    auto* topSection = new QHBoxLayout();
    
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

    mainLayout->addLayout(topSection);

    // Project Name Badge
    m_projectLabel = createProjectBadge(projectName());
    mainLayout->addWidget(m_projectLabel);

    // Bottom section: configuration options
    auto* bottomSection = new QHBoxLayout();
    auto* configLayout = new QVBoxLayout();

    // Target node
    auto* nodeRow = new QHBoxLayout();
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName.isEmpty() ? "AIRSAT_Import" : m_outputNodeName);
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

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &AIRSATImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &AIRSATImportNode::onRemoveFilesClicked);

    return widget;
}

void AIRSATImportNode::executeImport()
{
    if (m_dataFilePaths.isEmpty())
    {
        onError("请至少添加一个数据文件。");
        return;
    }

    for (int i = 0; i < m_dataFilePaths.size(); ++i)
    {
        if (!QFileInfo::exists(m_dataFilePaths[i]))
        {
            onError("数据文件不存在：" + m_dataFilePaths[i]);
            return;
        }
        if (!QFileInfo::exists(m_xmlFilePaths[i]))
        {
            onError("参数XML文件不存在：" + m_xmlFilePaths[i]);
            return;
        }
    }

    std::vector<ImportTask> tasks;
    for (int i = 0; i < m_dataFilePaths.size(); ++i)
    {
        QString importName = generateOutputFileName(m_dataFilePaths[i]);
        if (importName.isEmpty())
        {
            onError("无法从数据文件生成输出文件名：" + m_dataFilePaths[i]);
            return;
        }
        ImportTask task;
        task.filename = importName;
        task.arguments = QStringList{ m_dataFilePaths[i], m_xmlFilePaths[i] };
        tasks.push_back(task);
    }

    auto* worker = new AIRSATImportWorker();
    startWorker(worker, tasks);
}

QStringList AIRSATImportNode::getExpectedOutputFilePaths() const
{
    QStringList expectedPaths;
    QString outputNodeName = getOutputNodeName();

    for (int i = 0; i < m_dataFilePaths.size(); ++i)
    {
        QString importName = generateOutputFileName(m_dataFilePaths[i]);
        if (!importName.isEmpty())
        {
            QString filePath = QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(importName);
            expectedPaths.append(filePath);
        }
    }

    return expectedPaths;
}

QString AIRSATImportNode::getOutputNodeName() const
{
    if (m_outputNodeNameEdit)
    {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (name.isEmpty())
        {
            return "AIRSAT_Import";
        }
        return name;
    }
    return m_outputNodeName.isEmpty() ? "AIRSAT_Import" : m_outputNodeName;
}

QString AIRSATImportNode::generateOutputFileName(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    return fileInfo.baseName();
}

void AIRSATImportNode::onAddFilesClicked()
{
    QString dataFile = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择数据文件 (Data File)"),
        QDir::currentPath(),
        tr("Data Files (*.tiff *.tif *.h5)")
    );
    if (dataFile.isEmpty()) return;

    QString xmlFile = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择参数 XML 文件 (Parameter XML File)"),
        QFileInfo(dataFile).absolutePath(),
        tr("XML Files (*.xml)")
    );
    if (xmlFile.isEmpty()) return;

    if (!m_dataFilePaths.contains(dataFile))
    {
        m_dataFilePaths.append(dataFile);
        m_xmlFilePaths.append(xmlFile);
        m_fileListWidget->addItem(QFileInfo(dataFile).fileName() + " | " + QFileInfo(xmlFile).fileName());
        
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
        updateWidgetSize();
    }
}

void AIRSATImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_dataFilePaths.removeAt(row);
            m_xmlFilePaths.removeAt(row);
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
        updateWidgetSize();
    }
}

void AIRSATImportNode::updateWidgetSize()
{
    if (m_widget) {
        m_widget->setFixedWidth(300);
        m_widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QJsonObject AIRSATImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    
    QJsonArray dataPathsArray;
    for (const QString &path : m_dataFilePaths)
        dataPathsArray.append(path);
    json["dataFilePaths"] = dataPathsArray;

    QJsonArray xmlPathsArray;
    for (const QString &path : m_xmlFilePaths)
        xmlPathsArray.append(path);
    json["xmlFilePaths"] = xmlPathsArray;

    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    return json;
}

void AIRSATImportNode::load(QJsonObject const &json)
{
    m_dataFilePaths.clear();
    QJsonArray dataPathsArray = json["dataFilePaths"].toArray();
    for (const QJsonValue &val : dataPathsArray)
        m_dataFilePaths.append(val.toString());

    m_xmlFilePaths.clear();
    QJsonArray xmlPathsArray = json["xmlFilePaths"].toArray();
    for (const QJsonValue &val : xmlPathsArray)
        m_xmlFilePaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("AIRSAT_Import");

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (int i = 0; i < m_dataFilePaths.size(); ++i) {
            m_fileListWidget->addItem(QFileInfo(m_dataFilePaths[i]).fileName() + " | " + QFileInfo(m_xmlFilePaths[i]).fileName());
        }
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
    
    updateWidgetSize();
}


} // namespace QtNodes
