#include "InSARLogManager.h"
#include "SpacetyImportNode.h"
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
    , m_importedFilePaths()
    , m_outputNodeName("Spacety_Batch_Import")
    , m_spotlightMode(false)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

SpacetyImportNode::~SpacetyImportNode()
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

QWidget* SpacetyImportNode::createWidget()
{
    m_widget = new QWidget();
    m_widget->setFixedWidth(300);
    auto* mainLayout = new QVBoxLayout(m_widget);
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

    return m_widget;
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

    std::vector<QString> dataFileList;
    std::vector<QString> xmlFileList;
    std::vector<QString> importNameList;

    for (int i = 0; i < m_dataFiles.size(); ++i)
    {
        QString dataFilePath = m_dataFiles[i];
        QString importName = generateOutputFileName(dataFilePath);
        if (importName.isEmpty())
        {
            onError("无法从数据文件生成输出文件名：" + dataFilePath);
            return;
        }
        dataFileList.push_back(dataFilePath);
        xmlFileList.push_back(i < m_xmlFiles.size() ? m_xmlFiles[i] : "");
        importNameList.push_back(importName);
    }

    QString outputNodeName = getOutputNodeName();

    QStringList pathsToCheck;
    for (const QString& importName : importNameList) {
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + importName + ".jpg");
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
    m_workerThread = new SpacetyImportWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &SpacetyImportNode::startSpacetyImport,
            m_workerThread, &SpacetyImportWorker::import_Spacety_patch);
    connect(m_workerThread, &SpacetyImportWorker::updateProcess,
            this, &SpacetyImportNode::onImportProgress);
    connect(m_workerThread, &SpacetyImportWorker::endProcess,
            this, &SpacetyImportNode::onImportFinished);
    connect(m_workerThread, &SpacetyImportWorker::errorProcess,
            this, &SpacetyImportNode::onThreadError);
    connect(m_workerThread, &SpacetyImportWorker::sendModel,
            this, &SpacetyImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startSpacetyImport(
        projectPath(),
        dataFileList,
        xmlFileList,
        importNameList,
        outputNodeName,
        projectName(),
        projectModel(),
        m_spotlightMode
    );
}

void SpacetyImportNode::stopExecution()
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

QStringList SpacetyImportNode::getImportedFilePaths() const
{
    return m_importedFilePaths;
}

unsigned int SpacetyImportNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 0;
    return 2;
}

NodeDataType SpacetyImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported Files"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool SpacetyImportNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString SpacetyImportNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return tr("成果 *");
        if (portIndex == 1) return tr("预览 ?");
    }
    return QString();
}

bool SpacetyImportNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out && portIndex == 1;
}

std::shared_ptr<NodeData> SpacetyImportNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

QStringList SpacetyImportNode::previewImagePaths() const
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

QString SpacetyImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "Spacety_Batch_Import";
    }
    return name;
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

void SpacetyImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void SpacetyImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();
    m_importedFilePaths.clear();
    for (int i = 0; i < m_dataFiles.size(); ++i)
    {
        QString importName = generateOutputFileName(m_dataFiles[i]);
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

void SpacetyImportNode::onThreadError(const QString& error)
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

void SpacetyImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void SpacetyImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
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

bool SpacetyImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/";

    QDir dir(outputPath);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0) {
        QStringList importedFiles;
        for (const QString &path : m_dataFiles) {
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

} // namespace QtNodes
