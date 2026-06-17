#include "InSARLogManager.h"
#include "AIRSATImportNode.h"
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
    , m_importedFilePaths()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

AIRSATImportNode::~AIRSATImportNode()
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

    QString outputNodeName = getOutputNodeName();

    QStringList pathsToCheck;
    for (const auto& task : tasks) {
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + task.filename + ".h5");
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + task.filename + ".jpg");
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
    m_workerThread = new AIRSATImportWorker();
    m_workerThread->moveToThread(m_thread);

    connect(m_workerThread, &AIRSATImportWorker::updateProcess,
            this, &AIRSATImportNode::onImportProgress);
    connect(m_workerThread, &AIRSATImportWorker::endProcess,
            this, &AIRSATImportNode::onImportFinished);
    connect(m_workerThread, &AIRSATImportWorker::errorProcess,
            this, &AIRSATImportNode::onThreadError);
    connect(m_workerThread, &AIRSATImportWorker::sendModel,
            this, &AIRSATImportNode::onModelUpdated);

    m_thread->start();

    QMetaObject::invokeMethod(m_workerThread, "import_patch",
        Q_ARG(QString, projectPath()),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, outputNodeName),
        Q_ARG(QString, projectName()),
        Q_ARG(QStandardItemModel*, projectModel()));
}

void AIRSATImportNode::stopExecution()
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

QStringList AIRSATImportNode::getImportedFilePaths() const
{
    return m_importedFilePaths;
}

unsigned int AIRSATImportNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 0;
    return 2;
}

NodeDataType AIRSATImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported Files"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool AIRSATImportNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString AIRSATImportNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return tr("成果 *");
        if (portIndex == 1) return tr("预览 ?");
    }
    return QString();
}

bool AIRSATImportNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out && portIndex == 1;
}

std::shared_ptr<NodeData> AIRSATImportNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

QStringList AIRSATImportNode::previewImagePaths() const
{
    QStringList jpgPaths;
    for (const QString& h5Path : m_importedFilePaths) {
        QFileInfo fi(h5Path);
        QString jpg = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        if (QFileInfo::exists(jpg))
            jpgPaths.append(jpg);
    }
    return jpgPaths;
}

QString AIRSATImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "AIRSAT_Import";
    }
    return name;
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

void AIRSATImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void AIRSATImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();
    m_importedFilePaths.clear();
    for (int i = 0; i < m_dataFilePaths.size(); ++i)
    {
        QString importName = generateOutputFileName(m_dataFilePaths[i]);
        if (!importName.isEmpty())
        {
            QString filePath = QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(importName);
            m_importedFilePaths.append(filePath);
        }
    }

    ImportNodeBase::onImportFinished();

    // 双路输出：Port 1 预览
    if (!m_importedFilePaths.isEmpty()) {
        QStringList jpgPaths;
        for (const QString& h5Path : m_importedFilePaths) {
            QFileInfo fi(h5Path);
            jpgPaths.append(fi.absolutePath() + "/" + fi.baseName() + ".jpg");
        }
        m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    }

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

void AIRSATImportNode::onThreadError(const QString& error)
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

void AIRSATImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
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

bool AIRSATImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/";

    QDir dir(outputPath);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0) {
        QStringList importedFiles;
        for (const QString &path : m_dataFilePaths) {
            QString importName = generateOutputFileName(path);
            QString importedPath = outputPath + importName + ".h5";
            if (QFile::exists(importedPath)) {
                importedFiles.append(importedPath);
            }
        }
        if (!importedFiles.isEmpty()) {
            m_importedFilePaths = importedFiles;
            auto outputData = std::make_shared<ImportedFileData>(importedFiles, nodeName);
            setOutputData(0, outputData);
            Q_EMIT dataUpdated(0);

            // Port 1 预览恢复
            QStringList allJpgPaths;
            QStringList missingH5s, missingJpgs;

            for (const QString& h5Path : importedFiles) {
                QFileInfo fi(h5Path);
                QString jpg = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
                allJpgPaths.append(jpg);
                if (!QFileInfo::exists(jpg)) {
                    missingH5s.append(h5Path);
                    missingJpgs.append(jpg);
                }
            }

            if (!missingH5s.isEmpty()) {
                m_remedyWatcher.cancel();
                m_remedyWatcher.waitForFinished();
                m_remedyWatcher.disconnect();

                connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                        [this, allJpgPaths]() {
                    m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
                    setOutputData(1, m_imageInfoData);
                    Q_EMIT dataUpdated(1);
                });

                QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
                    for (int i = 0; i < missingH5s.size(); ++i)
                        NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
                });
                m_remedyWatcher.setFuture(future);
            } else {
                m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
                setOutputData(1, m_imageInfoData);
                Q_EMIT dataUpdated(1);
            }

            return true;
        }
    }

    return false;
}

void AIRSATImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

} // namespace QtNodes
