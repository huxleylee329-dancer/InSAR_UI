#include "InSARLogManager.h"
#include "HTHTImportNode.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>

namespace QtNodes {

HTHTImportNode::HTHTImportNode()
    : ImportNodeBase()
    , m_dataEdit(nullptr)
    , m_xmlEdit(nullptr)
    , m_modeCombo(nullptr)
    , m_fileListWidget(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_projectLabel(nullptr)
    , m_dataFiles()
    , m_xmlFiles()
    , m_modes()
    , m_importedFilePaths()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    m_outputNodeName = "HTHT_Import";
}

HTHTImportNode::~HTHTImportNode()
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
}

QWidget* HTHTImportNode::createWidget()
{
    auto* widget = new QWidget();
    widget->setFixedWidth(300); // CRITICAL: lock layout width

    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // Project Name Badge
    m_projectLabel = createProjectBadge(projectName());
    mainLayout->addWidget(m_projectLabel);

    // Row 1: Data file row
    auto* dataRow = new QHBoxLayout();
    dataRow->addWidget(new QLabel("数据文件："), 3);
    m_dataEdit = new QLineEdit();
    m_dataEdit->setPlaceholderText("选择.tiff/.h5");
    QPushButton* dataBrowseBtn = new QPushButton("...");
    dataBrowseBtn->setFixedWidth(30);
    dataRow->addWidget(m_dataEdit, 7);
    dataRow->addWidget(dataBrowseBtn, 0);
    mainLayout->addLayout(dataRow);

    // Row 2: XML file row
    auto* xmlRow = new QHBoxLayout();
    xmlRow->addWidget(new QLabel("参数文件："), 3);
    m_xmlEdit = new QLineEdit();
    m_xmlEdit->setPlaceholderText("选择.xml");
    QPushButton* xmlBrowseBtn = new QPushButton("...");
    xmlBrowseBtn->setFixedWidth(30);
    xmlRow->addWidget(m_xmlEdit, 7);
    xmlRow->addWidget(xmlBrowseBtn, 0);
    mainLayout->addLayout(xmlRow);

    // Row 3: Mode & buttons row
    auto* optRow = new QHBoxLayout();
    optRow->addWidget(new QLabel("模式："), 2);
    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("单星", 0);
    m_modeCombo->addItem("多星", 1);
    optRow->addWidget(m_modeCombo, 4);

    QPushButton* addBtn = new QPushButton("添加");
    QPushButton* removeBtn = new QPushButton("移除");
    optRow->addWidget(addBtn, 2);
    optRow->addWidget(removeBtn, 2);
    mainLayout->addLayout(optRow);

    // List Widget
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(80);
    mainLayout->addWidget(m_fileListWidget);

    // Target node row
    auto* nodeRow = new QHBoxLayout();
    nodeRow->addWidget(new QLabel("目标节点："), 3);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName.isEmpty() ? "HTHT_Import" : m_outputNodeName);
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
    nodeRow->addWidget(m_outputNodeNameEdit, 7);
    mainLayout->addLayout(nodeRow);

    // Connections
    connect(dataBrowseBtn, &QPushButton::clicked, this, &HTHTImportNode::onDataBrowseClicked);
    connect(xmlBrowseBtn, &QPushButton::clicked, this, &HTHTImportNode::onXmlBrowseClicked);
    connect(addBtn, &QPushButton::clicked, this, &HTHTImportNode::onAddTaskClicked);
    connect(removeBtn, &QPushButton::clicked, this, &HTHTImportNode::onRemoveTaskClicked);

    return widget;
}

void HTHTImportNode::executeImport()
{
    if (m_dataFiles.isEmpty())
    {
        onError("请至少添加一个数据文件任务。");
        return;
    }

    std::vector<QString> dataFilesVec;
    std::vector<QString> xmlFilesVec;
    std::vector<int> modesVec;
    std::vector<QString> importNameList;

    for (int i = 0; i < m_dataFiles.size(); ++i)
    {
        QString df = m_dataFiles[i];
        QString xf = i < m_xmlFiles.size() ? m_xmlFiles[i] : "";
        int m = i < m_modes.size() ? m_modes[i] : 0;

        if (!QFileInfo::exists(df))
        {
            onError("数据文件不存在：" + df);
            return;
        }
        if (!QFileInfo::exists(xf))
        {
            onError("参数 XML 文件不存在：" + xf);
            return;
        }

        QFileInfo fi(df);
        dataFilesVec.push_back(df);
        xmlFilesVec.push_back(xf);
        modesVec.push_back(m);
        importNameList.push_back(fi.baseName());
    }

    QString outputNodeName = getOutputNodeName();

    QStringList pathsToCheck;
    for (const QString& importName : importNameList) {
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
    }

    auto overwriteRes = NodeUtils::checkAndPromptOverwrite(getProjectContext(), outputNodeName, pathsToCheck, nullptr);
    if (overwriteRes == NodeUtils::OverwriteResult::Cancel) {
        setState(ExecutionState::Idle);
        return;
    } else if (overwriteRes == NodeUtils::OverwriteResult::LoadExisting) {
        setProgress(100);
        onImportFinished();
        return;
    } else if (overwriteRes == NodeUtils::OverwriteResult::Overwrite) {
        NodeUtils::removeDataNodeFromProject(getProjectContext(), outputNodeName);
    }

    m_thread = new QThread(this);
    m_workerThread = new HTHTImportWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &HTHTImportNode::startHTHTImport,
            m_workerThread, &HTHTImportWorker::import_HTHT_patch);
    connect(m_workerThread, &HTHTImportWorker::updateProcess,
            this, &HTHTImportNode::onImportProgress);
    connect(m_workerThread, &HTHTImportWorker::endProcess,
            this, &HTHTImportNode::onImportFinished);
    connect(m_workerThread, &HTHTImportWorker::errorProcess,
            this, &HTHTImportNode::onThreadError);
    connect(m_workerThread, &HTHTImportWorker::sendModel,
            this, &HTHTImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startHTHTImport(
        projectPath(),
        dataFilesVec,
        xmlFilesVec,
        modesVec,
        importNameList,
        outputNodeName,
        projectName(),
        projectModel()
    );
}

void HTHTImportNode::stopExecution()
{
    m_stopRequested = true;
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
    }
}

QStringList HTHTImportNode::getImportedFilePaths() const
{
    return m_importedFilePaths;
}

NodeDataType HTHTImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
        return NodeDataType{"imported_file", "Imported Files"};
    return NodeDataType();
}

QString HTHTImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "HTHT_Import";
    }
    return name;
}

bool HTHTImportNode::validateAndRestoreOutput()
{
    if (m_importedFilePaths.isEmpty()) return false;
    for (const QString& path : m_importedFilePaths)
    {
        if (!QFileInfo::exists(path)) return false;
    }
    return true;
}

void HTHTImportNode::onDataBrowseClicked()
{
    QString filename = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择数据文件"),
        QDir::currentPath(),
        tr("Data Files (*.tiff *.tif *.h5)")
    );
    if (!filename.isEmpty())
    {
        m_dataEdit->setText(filename);
        if (m_outputNodeNameEdit->text() == "HTHT_Import" || m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(QFileInfo(filename).baseName() + "_HTHT");
            m_outputNodeName = QFileInfo(filename).baseName() + "_HTHT";
        }
    }
}

void HTHTImportNode::onXmlBrowseClicked()
{
    QString filename = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择参数 XML 文件"),
        QDir::currentPath(),
        tr("XML Files (*.xml)")
    );
    if (!filename.isEmpty())
    {
        m_xmlEdit->setText(filename);
    }
}

void HTHTImportNode::onAddTaskClicked()
{
    QString data_file = m_dataEdit->text().trimmed();
    QString xml_file = m_xmlEdit->text().trimmed();
    if (data_file.isEmpty() || xml_file.isEmpty())
    {
        QMessageBox::warning(nullptr, "Warning", QStringLiteral("请选择数据文件和参数 XML 文件！"));
        return;
    }

    int mode = m_modeCombo->currentData().toInt();
    QString modeStr = m_modeCombo->currentText();

    if (!m_dataFiles.contains(data_file))
    {
        m_dataFiles.append(data_file);
        m_xmlFiles.append(xml_file);
        m_modes.append(mode);

        QString itemText = QString("%1 | XML: %2 | %3")
            .arg(QFileInfo(data_file).fileName())
            .arg(QFileInfo(xml_file).fileName())
            .arg(modeStr);
        m_fileListWidget->addItem(itemText);

        m_dataEdit->clear();
        m_xmlEdit->clear();

        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

void HTHTImportNode::onRemoveTaskClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_dataFiles.removeAt(row);
            if (row < m_xmlFiles.size()) m_xmlFiles.removeAt(row);
            if (row < m_modes.size()) m_modes.removeAt(row);
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

void HTHTImportNode::onImportProgress(int progress, const QString& message)
{
    onProgressUpdate(progress, message);
}

void HTHTImportNode::onImportFinished()
{
    m_importedFilePaths.clear();
    QString outputNodeName = getOutputNodeName();
    for (const QString& df : m_dataFiles)
    {
        QFileInfo fi(df);
        QString filename = fi.baseName();
        QString h5_path = projectPath() + "/" + outputNodeName + "/" + filename + ".h5";
        m_importedFilePaths.append(h5_path);
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

void HTHTImportNode::onThreadError(const QString& error)
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

void HTHTImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void HTHTImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext())
    {
        iface->refreshProjectTree();
    }
}

QJsonObject HTHTImportNode::save() const
{
    QJsonObject modelJson = ImportNodeBase::save();

    QJsonArray dataFilesArray;
    for (const QString& f : m_dataFiles) dataFilesArray.append(f);
    modelJson["dataFiles"] = dataFilesArray;

    QJsonArray xmlFilesArray;
    for (const QString& f : m_xmlFiles) xmlFilesArray.append(f);
    modelJson["xmlFiles"] = xmlFilesArray;

    QJsonArray modesArray;
    for (int m : m_modes) modesArray.append(m);
    modelJson["modes"] = modesArray;

    modelJson["outputNodeName"] = m_outputNodeName;

    QJsonArray importedArray;
    for (const QString& f : m_importedFilePaths) importedArray.append(f);
    modelJson["importedFilePaths"] = importedArray;

    return modelJson;
}

void HTHTImportNode::load(QJsonObject const &json)
{
    ImportNodeBase::load(json);

    m_dataFiles.clear();
    QJsonArray dataArray = json["dataFiles"].toArray();
    for (QJsonValueRef val : dataArray) m_dataFiles.append(val.toString());

    m_xmlFiles.clear();
    QJsonArray xmlArray = json["xmlFiles"].toArray();
    for (QJsonValueRef val : xmlArray) m_xmlFiles.append(val.toString());

    m_modes.clear();
    QJsonArray modesArray = json["modes"].toArray();
    for (QJsonValueRef val : modesArray) m_modes.append(val.toInt());

    m_outputNodeName = json["outputNodeName"].toString();
    if (m_outputNodeNameEdit)
    {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }

    m_importedFilePaths.clear();
    QJsonArray importedArray = json["importedFilePaths"].toArray();
    for (QJsonValueRef val : importedArray) m_importedFilePaths.append(val.toString());

    if (m_fileListWidget)
    {
        m_fileListWidget->clear();
        for (int i = 0; i < m_dataFiles.size(); ++i)
        {
            QString df = m_dataFiles[i];
            QString xf = i < m_xmlFiles.size() ? m_xmlFiles[i] : "";
            int m = i < m_modes.size() ? m_modes[i] : 0;
            QString modeStr = (m == 1) ? "多星" : "单星";
            m_fileListWidget->addItem(QString("%1 | XML: %2 | %3")
                .arg(QFileInfo(df).fileName())
                .arg(QFileInfo(xf).fileName())
                .arg(modeStr));
        }
    }
}

} // namespace QtNodes
