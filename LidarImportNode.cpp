#include "LidarImportNode.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

LidarImportNode::LidarImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectLabel(nullptr)
    , m_productTypeCombo(nullptr)
    , m_rhPercentileSpin(nullptr)
    , m_rhLabel(nullptr)
    , m_filePaths()
    , m_importedFilePaths()
    , m_outputNodeName("LiDAR_Import")
    , m_productType("GEDI L2A")
    , m_rhPercentile(100)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

LidarImportNode::~LidarImportNode()
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

QWidget* LidarImportNode::createWidget()
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

    // Top section: file list
    auto* topSection = new QHBoxLayout();
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(80);
    topSection->addWidget(m_fileListWidget, 8);

    auto* buttonLayout = new QVBoxLayout();
    QPushButton* addFiles = new QPushButton("添加");
    QPushButton* removeFiles = new QPushButton("移除");
    buttonLayout->addWidget(addFiles);
    buttonLayout->addWidget(removeFiles);
    topSection->addLayout(buttonLayout, 2);

    mainLayout->addLayout(topSection);

    // Project Name Badge
    m_projectLabel = createProjectBadge(projectName());
    mainLayout->addWidget(m_projectLabel);

    // Configuration Options
    auto* configLayout = new QVBoxLayout();

    // Product Type Row
    auto* typeRow = new QHBoxLayout();
    QLabel* typeLabel = new QLabel("数据类型：");
    typeLabel->setFixedWidth(80);
    m_productTypeCombo = new QComboBox();
    m_productTypeCombo->addItems({"GEDI L2A", "GEDI L2B", "ICESat-2 L3A"});
    int prodIdx = m_productTypeCombo->findText(m_productType);
    if (prodIdx != -1) m_productTypeCombo->setCurrentIndex(prodIdx);
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(m_productTypeCombo);
    configLayout->addLayout(typeRow);

    // RH Percentile Row
    auto* rhRow = new QHBoxLayout();
    m_rhLabel = new QLabel("RH百分位：");
    m_rhLabel->setFixedWidth(80);
    m_rhPercentileSpin = new QSpinBox();
    m_rhPercentileSpin->setRange(0, 100);
    m_rhPercentileSpin->setValue(m_rhPercentile);
    rhRow->addWidget(m_rhLabel);
    rhRow->addWidget(m_rhPercentileSpin);
    configLayout->addLayout(rhRow);

    // Target Node Row
    auto* nodeRow = new QHBoxLayout();
    QLabel* nodeLabel = new QLabel("目标节点：");
    nodeLabel->setFixedWidth(80);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName.isEmpty() ? "LiDAR_Import" : m_outputNodeName);
    nodeRow->addWidget(nodeLabel);
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    mainLayout->addLayout(configLayout);

    // Connect spinbox/combobox logic
    connect(m_productTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LidarImportNode::onProductTypeChanged);
    connect(m_rhPercentileSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, invalidateNodeData](int val) {
        if (m_rhPercentile != val) {
            m_rhPercentile = val;
            invalidateNodeData();
        }
    });

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

    // Connect buttons
    connect(addFiles, &QPushButton::clicked, this, &LidarImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &LidarImportNode::onRemoveFilesClicked);

    // Force setup UI state based on loaded indices
    onProductTypeChanged(m_productTypeCombo->currentIndex());

    return widget;
}

void LidarImportNode::onProductTypeChanged(int index)
{
    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    if (index == 0) {
        m_rhPercentileSpin->setEnabled(true);
        m_rhPercentileSpin->setValue(100);
        m_rhLabel->setEnabled(true);
    } else if (index == 1) {
        m_rhPercentileSpin->setEnabled(false);
        m_rhLabel->setEnabled(false);
    } else if (index == 2) {
        m_rhPercentileSpin->setEnabled(true);
        m_rhPercentileSpin->setValue(18);
        m_rhLabel->setEnabled(true);
    }
    
    QString newType = m_productTypeCombo->currentText();
    if (m_productType != newType) {
        m_productType = newType;
        invalidateNodeData();
    }
    m_rhPercentile = m_rhPercentileSpin->value();
}

void LidarImportNode::executeImport()
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

    std::vector<QString> originalFileList;
    std::vector<QString> importNameList;

    for (const QString& path : m_filePaths)
    {
        QFileInfo fileInfo(path);
        originalFileList.push_back(path);
        importNameList.push_back(fileInfo.baseName());
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
    m_workerThread = new LidarImportWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &LidarImportNode::startLidarImport,
            m_workerThread, &LidarImportWorker::import_Lidar_patch);
    connect(m_workerThread, &LidarImportWorker::updateProcess,
            this, &LidarImportNode::onImportProgress);
    connect(m_workerThread, &LidarImportWorker::endProcess,
            this, &LidarImportNode::onImportFinished);
    connect(m_workerThread, &LidarImportWorker::errorProcess,
            this, &LidarImportNode::onThreadError);
    connect(m_workerThread, &LidarImportWorker::sendModel,
            this, &LidarImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startLidarImport(
        projectPath(),
        originalFileList,
        importNameList,
        m_productTypeCombo->currentText(),
        m_rhPercentileSpin->value(),
        outputNodeName,
        projectName(),
        projectModel()
    );
}

void LidarImportNode::stopExecution()
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

QStringList LidarImportNode::getImportedFilePaths() const
{
    return m_importedFilePaths;
}

unsigned int LidarImportNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 0;
    return 2;
}

NodeDataType LidarImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported Files"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool LidarImportNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString LidarImportNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return tr("成果 *");
        if (portIndex == 1) return tr("预览 ?");
    }
    return QString();
}

bool LidarImportNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out && portIndex == 1;
}

std::shared_ptr<NodeData> LidarImportNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

QStringList LidarImportNode::previewImagePaths() const
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

QString LidarImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "LiDAR_Import";
    }
    return name;
}

void LidarImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        nullptr,
        tr("导入 LiDAR 数据"),
        QDir::currentPath(),
        tr("HDF5 文件 (*.h5)")
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
    updateWidgetSize();
}

void LidarImportNode::onRemoveFilesClicked()
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
    updateWidgetSize();
}

void LidarImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void LidarImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();
    m_importedFilePaths.clear();
    for (int i = 0; i < m_filePaths.size(); ++i)
    {
        QString importName = QFileInfo(m_filePaths[i]).baseName();
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

void LidarImportNode::onThreadError(const QString& error)
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

void LidarImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void LidarImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
    }
}

QJsonObject LidarImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_filePaths)
        pathsArray.append(path);
    json["filePaths"] = pathsArray;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    json["productType"] = m_productTypeCombo ? m_productTypeCombo->currentText() : m_productType;
    json["rhPercentile"] = m_rhPercentileSpin ? m_rhPercentileSpin->value() : m_rhPercentile;
    return json;
}

void LidarImportNode::load(QJsonObject const &json)
{
    m_filePaths.clear();
    QJsonArray pathsArray = json["filePaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_filePaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("LiDAR_Import");
    m_productType = json["productType"].toString("GEDI L2A");
    m_rhPercentile = json["rhPercentile"].toInt(100);

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_filePaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    if (m_productTypeCombo) {
        int idx = m_productTypeCombo->findText(m_productType);
        if (idx != -1) {
            m_productTypeCombo->setCurrentIndex(idx);
        }
    }

    if (m_rhPercentileSpin) {
        m_rhPercentileSpin->setValue(m_rhPercentile);
    }
}

bool LidarImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/";

    QDir dir(outputPath);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0) {
        QStringList importedFiles;
        for (const QString &path : m_filePaths) {
            QString importName = QFileInfo(path).baseName();
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

            // Port 1 预览恢复（LiDAR 使用 "dem" 类型）
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
                        NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "dem");
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

void LidarImportNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

} // namespace QtNodes
