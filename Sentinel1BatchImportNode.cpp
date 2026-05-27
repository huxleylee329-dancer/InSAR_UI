#include "InSARLogManager.h"

#include "Sentinel1BatchImportNode.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

namespace QtNodes {

Sentinel1BatchImportNode::Sentinel1BatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_projectCombo(nullptr)
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
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // Top section: file list (8:2 stretch)
    auto* topSection = new QHBoxLayout();
    topSection->setStretch(0, 8);
    topSection->setStretch(1, 2);

    auto* fileListLayout = new QVBoxLayout();
    fileListLayout->setContentsMargins(0, 0, 0, 0);
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
    fileListLayout->addWidget(m_fileListWidget);
    topSection->addLayout(fileListLayout);

    auto* buttonColLayout = new QVBoxLayout();
    buttonColLayout->setContentsMargins(0, 0, 0, 0);
    buttonColLayout->setSpacing(5);
    QPushButton* addFiles = new QPushButton("添加");
    QPushButton* removeFiles = new QPushButton("移除");
    buttonColLayout->addWidget(addFiles);
    buttonColLayout->addWidget(removeFiles);
    buttonColLayout->addStretch();
    topSection->addLayout(buttonColLayout);
    
    mainLayout->addLayout(topSection, 4);

    // Bottom section: configuration options
    auto* bottomSection = new QHBoxLayout();
    auto* configLayout = new QVBoxLayout();

    // Subswath row [3:7]
    auto* subswathRow = new QHBoxLayout();
    subswathRow->setStretch(0, 3);
    subswathRow->setStretch(1, 7);
    subswathRow->addWidget(new QLabel("子带选择："));
    m_subswathCombo = new QComboBox();
    m_subswathCombo->addItem("iw1");
    m_subswathCombo->addItem("iw2");
    m_subswathCombo->addItem("iw3");
    connect(m_subswathCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        if (m_subswath != text) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_subswathCombo);
                m_subswathCombo->setCurrentText(m_subswath);
                return;
            }
            m_subswath = text;
            invalidateNodeData();
        }
    });
    subswathRow->addWidget(m_subswathCombo);
    configLayout->addLayout(subswathRow);

    // Polarization row [3:7]
    auto* polRow = new QHBoxLayout();
    polRow->setStretch(0, 3);
    polRow->setStretch(1, 7);
    polRow->addWidget(new QLabel("极化方式："));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("vv");
    m_polarizationCombo->addItem("vh");
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

    // Target project row [3:7]
    auto* projectRow = new QHBoxLayout();
    projectRow->setStretch(0, 3);
    projectRow->setStretch(1, 7);
    projectRow->addWidget(new QLabel("目标工程："));
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);

    // Populate project list from model (Align with Workspace behavior)
    QStandardItemModel* model = projectModel();
    if (model && model->rowCount() > 0) {
        for (int i = 0; i < model->rowCount(); ++i) {
            auto item = model->item(i, 0);
            if (item) {
                m_projectCombo->addItem(item->text());
            }
        }
        // Set current project as default selection
        int index = m_projectCombo->findText(projectName());
        if (index >= 0) m_projectCombo->setCurrentIndex(index);
    } else {
        m_projectCombo->addItem("未打开项目");
    }

    projectRow->addWidget(m_projectCombo);
    configLayout->addLayout(projectRow);

    // Output node name row [3:7]
    auto* nameRow = new QHBoxLayout();
    nameRow->setStretch(0, 3);
    nameRow->setStretch(1, 7);
    nameRow->addWidget(new QLabel("目标节点名："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
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
    nameRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nameRow);

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onRemoveFilesClicked);

    return widget;
}

void Sentinel1BatchImportNode::executeImport()
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

    if (m_manifestPaths.isEmpty())
    {
        onError("请至少添加一个清单文件。");
        return;
    }

    for (const QString& manifestPath : m_manifestPaths)
    {
        if (!QFileInfo::exists(manifestPath))
        {
            onError("清单文件不存在：" + manifestPath);
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
        nullptr,
        tr("选择哨兵一号清单文件"),
        QDir::currentPath(),
        tr("清单文件 (*.manifest);;所有文件 (*)")
    );

    for (const QString& file : files)
    {
        if (!m_manifestPaths.contains(file))
        {
            m_manifestPaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());
            
            int outCount = nPorts(PortType::Out);
            for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
            invalidateExecution();
        }
    }
}

void Sentinel1BatchImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_manifestPaths.removeAt(row);
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

void Sentinel1BatchImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void Sentinel1BatchImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();

    m_importedFilePaths.clear();
    for (int i = 0; i < m_manifestPaths.size(); ++i) {
        QString importName = generateImportName(m_manifestPaths[i]);
        if (!importName.isEmpty()) {
            QString filePath = QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(importName);
            m_importedFilePaths.append(filePath);
        }
    }

    ImportNodeBase::onImportFinished();

    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }
}

void Sentinel1BatchImportNode::onThreadError(const QString& error)
{
    onError(error);

    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }
}

void Sentinel1BatchImportNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ImportNodeBase::setExecutionMode(mode);
}

void Sentinel1BatchImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
    }
}

QJsonObject Sentinel1BatchImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_manifestPaths)
        pathsArray.append(path);
    json["manifestPaths"] = pathsArray;
    json["subswath"] = m_subswath;
    json["polarization"] = m_polarization;
    json["outputNodeName"] = m_outputNodeName;
    return json;
}

void Sentinel1BatchImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_manifestPaths.clear();
    QJsonArray pathsArray = json["manifestPaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_manifestPaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("S1_Batch_Import");

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_manifestPaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    m_subswath = json["subswath"].toString("iw1");
    if (m_subswathCombo) {
        int idx = m_subswathCombo->findText(m_subswath);
        if (idx >= 0) m_subswathCombo->setCurrentIndex(idx);
    }

    m_polarization = json["polarization"].toString("vv");
    if (m_polarizationCombo) {
        int idx = m_polarizationCombo->findText(m_polarization);
        if (idx >= 0) m_polarizationCombo->setCurrentIndex(idx);
    }
}

bool Sentinel1BatchImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/";

    QDir dir(outputPath);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0) {
        m_importedFilePaths.clear();
        for (const QString &manifestPath : m_manifestPaths) {
            QFileInfo fi(manifestPath);
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
