#include "InSARLogManager.h"

#include "TSXBatchImportNode.h"
#include "TSXImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
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
    , m_projectLabel(nullptr)
    , m_xmlPaths()
    , m_outputNodeName()
    , m_polarization("HH")
{
}

ProductOutputContract TSXBatchImportNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("tsx_batch_import.output.complex_sar")
        : QStringLiteral("tsx_batch_import.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("complex_sar")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

QWidget* TSXBatchImportNode::createWidget()
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
    m_outputNodeNameEdit->setText("TSX_Batch_Import");
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

    // Polarization [3:7]
    auto* polRow = new QHBoxLayout();
    polRow->setStretch(0, 3);
    polRow->setStretch(1, 7);
    polRow->addWidget(new QLabel("极化方式："));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("HH");
    m_polarizationCombo->addItem("VV");
    connect(m_polarizationCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        if (m_polarization != text) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_polarizationCombo);
                m_polarizationCombo->setCurrentText(m_polarization);
                return;
            }
            m_polarization = text;
            invalidateNodeData();
        }
    });
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
    // Safety check: Ensure project is open
    auto* model = projectModel();
    QString path = projectPath();
    QString name = projectName();

    if (!model || path.isEmpty() || name.isEmpty())
    {
        onError("未检测到打开的项目，请先打开或新建一个项目。");
        return;
    }

    if (m_xmlPaths.isEmpty())
    {
        onError("请至少添加一个 XML 文件。");
        return;
    }

    for (const QString& xmlPath : m_xmlPaths)
    {
        if (!QFileInfo::exists(xmlPath))
        {
            onError("XML 文件不存在：" + xmlPath);
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

    QStringList pathsToCheck;
    for (const QString& importName : importNameList) {
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + importName + ".jpg");
    }

    // 因为已经在 prepareToStart() 中完成了存在性检查，这里直接读取 m_preparedOverwriteResult 并分支处理
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        setState(ExecutionState::Idle);
        return;
    } else if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        setProgress(100);
        onImportFinished();
        return;
    }


    // 构造 ImportTask 列表
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < originalFileList.size(); ++i) {
        ImportTask task;
        task.filename = importNameList[i];
        task.arguments = QStringList{ originalFileList[i], polarization };
        tasks.push_back(task);
    }

    // 启动 Worker
    auto* worker = new TSXImportWorker();
    startWorker(worker, tasks);
}

QStringList TSXBatchImportNode::getExpectedOutputFilePaths() const
{
    QString outputNodeName = getOutputNodeName();
    QStringList paths;
    for (const QString& xmlPath : m_xmlPaths) {
        QString importName = generateOutputFileName(xmlPath);
        if (!importName.isEmpty()) {
            paths.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
        }
    }
    return paths;
}

QString TSXBatchImportNode::getOutputNodeName() const
{
    if (m_outputNodeNameEdit)
    {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (name.isEmpty())
        {
            return "TSX_Batch_Import";
        }
        return name;
    }
    return m_outputNodeName.isEmpty() ? "TSX_Batch_Import" : m_outputNodeName;
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

            int outCount = nPorts(PortType::Out);
            for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
            invalidateExecution();
        }
    }
}

void TSXBatchImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_xmlPaths.removeAt(row);
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

QJsonObject TSXBatchImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_xmlPaths)
        pathsArray.append(path);
    json["xmlPaths"] = pathsArray;
    json["polarization"] = m_polarization;
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

    m_polarization = json["polarization"].toString("HH");
    if (m_polarizationCombo) {
        int idx = m_polarizationCombo->findText(m_polarization);
        if (idx >= 0) m_polarizationCombo->setCurrentIndex(idx);
    }
}

} // namespace QtNodes
