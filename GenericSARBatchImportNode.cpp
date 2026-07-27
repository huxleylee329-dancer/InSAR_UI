#include "InSARLogManager.h"

#include "GenericSARBatchImportNode.h"
#include "IApplicationInterface.h"
#include <QFile>
#include <QJsonArray>
#include <QThreadPool>
#include "FormatConversion.h"

#include <QDir>
#include <QFileInfo>
#include "NodeUtils.h"

namespace QtNodes {

GenericSARBatchImportNode::GenericSARBatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectLabel(nullptr)
    , m_task(nullptr)
{
}

QWidget* GenericSARBatchImportNode::createWidget()
{
    auto* widget = new QWidget();
    widget->setFixedWidth(300);
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto invalidateNodeData = [this]() {
        setOutputData(0, nullptr);
        invalidateExecution();
    };

    // Top section: file list (8:2 stretch)
    auto* topSection = new QHBoxLayout();
    topSection->setStretch(0, 8);
    topSection->setStretch(1, 2);

    // Left side: file list widget
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
    for (const QString& file : m_imagePaths) {
        m_fileListWidget->addItem(QFileInfo(file).fileName());
    }
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

    // Bottom section: configuration options
    auto* bottomSection = new QHBoxLayout();
    auto* configLayout = new QVBoxLayout();

    // Node Row [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName.isEmpty() ? "GenericSAR_Batch_Import" : m_outputNodeName);
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
    mainLayout->addLayout(bottomSection, 4);

    connect(addFiles, &QPushButton::clicked,
            this, &GenericSARBatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked,
            this, &GenericSARBatchImportNode::onRemoveFilesClicked);

    return widget;
}

// ============================================================================
// 执行导入 - 构造 tasks 并启动 QThreadPool
// ============================================================================

void GenericSARBatchImportNode::executeImport()
{
    if (m_task)
    {
        return;
    }

    m_generatedOutputPaths.clear();
    m_outputPersistenceFailed = false;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        setProgress(100);
        onImportFinished();
        return;
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite) {
        NodeUtils::removeDataNodeFromProject(getProjectContext(), getOutputNodeName());
    }

    m_task = new GenericSARBatchImportTask(
        projectPath(),
        m_preparedOriginalFileList,
        m_preparedImportNameList,
        getOutputNodeName()
    );

    connect(m_task, &GenericSARBatchImportTask::updateProcess,
            this, &GenericSARBatchImportNode::onImportProgress, Qt::QueuedConnection);
    connect(m_task, &GenericSARBatchImportTask::endProcess, this, [this]() {
        m_task = nullptr;
        onImportFinished();
    }, Qt::QueuedConnection);
    connect(m_task, &GenericSARBatchImportTask::errorProcess, this, [this](const QString& error) {
        m_task = nullptr;
        onThreadError(error);
    }, Qt::QueuedConnection);
    connect(m_task, &GenericSARBatchImportTask::outputsGenerated,
            this, &GenericSARBatchImportNode::onOutputsGenerated, Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(m_task);
}

// ============================================================================
// prepareToStart - 验证参数并准备文件列表
// ============================================================================

bool GenericSARBatchImportNode::prepareToStart()
{
    auto* model = projectModel();
    QString path = projectPath();
    QString name = projectName();

    if (!model || path.isEmpty() || name.isEmpty())
    {
        onError("未检测到打开的项目，请先打开或新建一个项目。");
        return false;
    }

    if (m_imagePaths.isEmpty())
    {
        onError("璇疯嚦灏戞坊鍔犱竴涓?閫氱敤 SAR 鍥惧儚鏂囦欢銆?");
        return false;
    }

    if (m_task)
    {
        return false;
    }

    m_preparedOriginalFileList.clear();
    m_preparedImportNameList.clear();

    for (const QString& imagePath : m_imagePaths)
    {
        if (!QFileInfo::exists(imagePath))
        {
            onError("通用 SAR 图像文件不存在：" + imagePath);
            return false;
        }

        m_preparedOriginalFileList.push_back(imagePath);
        m_preparedImportNameList.push_back(generateImportName(imagePath));
    }

    QStringList pathsToCheck;
    for (size_t i = 0; i < m_preparedImportNameList.size(); ++i) {
        QString suffix = QFileInfo(m_preparedOriginalFileList[i]).suffix();
        if (suffix.isEmpty()) suffix = "h5";
        pathsToCheck.append(projectPath() + "/" + getOutputNodeName() + "/" + m_preparedImportNameList[i] + "." + suffix);
        pathsToCheck.append(projectPath() + "/" + getOutputNodeName() + "/" + m_preparedImportNameList[i] + ".jpg");
    }
    pathsToCheck.removeDuplicates();

    m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(getProjectContext(), getOutputNodeName(), pathsToCheck, nullptr);
    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

// ============================================================================
// getExpectedOutputFilePaths - 返回预期的 H5 路径列表
// ============================================================================

QStringList GenericSARBatchImportNode::getExpectedOutputFilePaths() const
{
    QStringList paths;
    for (const QString& imagePath : m_imagePaths) {
        QString importName = generateImportName(imagePath);
        QString suffix = QFileInfo(imagePath).suffix();
        if (suffix.isEmpty()) suffix = "h5";
        paths.append(projectPath() + "/" + getOutputNodeName() + "/" + importName + "." + suffix);
    }
    return paths;
}

QString GenericSARBatchImportNode::getOutputNodeName() const
{
    return m_outputNodeName.isEmpty() ? "GenericSAR_Batch_Import" : m_outputNodeName;
}

QString GenericSARBatchImportNode::generateImportName(const QString& imagePath) const
{
    QFileInfo fileInfo(imagePath);
    QString baseName = fileInfo.baseName();

    int pos = baseName.lastIndexOf('T');
    if (pos >= 8)
        return baseName.mid(pos - 8, 8);

    return baseName;
}

// ============================================================================
// 文件列表操作槽函数
// ============================================================================

void GenericSARBatchImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        nullptr,
        tr("选择通用 SAR 图像"),
        QDir::currentPath(),
        tr("Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff)")
    );

    bool changed = false;
    for (const QString& file : files)
    {
        if (!file.isEmpty() && !m_imagePaths.contains(file))
        {
            m_imagePaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());
            changed = true;
        }
    }

    if (changed)
    {
        if (!m_imagePaths.isEmpty()) {
            m_imageInfo = std::make_shared<ImageInfoData>(m_imagePaths);
            setOutputData(1, m_imageInfo);
        } else {
            m_imageInfo.reset();
            setOutputData(1, nullptr);
        }

        m_importedFilePaths.clear();
        setOutputData(0, nullptr);
        setState(ExecutionState::Idle);

        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
    }
}

void GenericSARBatchImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();

    bool changed = false;
    for (QListWidgetItem* item : selectedItems)
    {
        int row = m_fileListWidget->row(item);
        QString filePath = m_imagePaths.at(row);

        // --- 从工程树和XML中同步移除文件 ---
        QString importName = generateImportName(filePath);
        QString suffix = QFileInfo(filePath).suffix();
        QString importedPath = QString("%1/%2/%3.%4")
            .arg(projectPath())
            .arg(getOutputNodeName())
            .arg(importName)
            .arg(suffix);

        QStandardItemModel* model = projectModel();
        if (model && !projectPath().isEmpty() && !projectName().isEmpty()) {
            QList<QStandardItem*> projItems = model->findItems(projectName());
            if (!projItems.isEmpty()) {
                QStandardItem* projItem = projItems.first();
                for (int i = 0; i < projItem->rowCount(); ++i) {
                    QStandardItem* nodeItem = projItem->child(i);
                    if (nodeItem && nodeItem->text() == getOutputNodeName()) {
                        for (int j = 0; j < nodeItem->rowCount(); ++j) {
                            QStandardItem* pathItem = nodeItem->child(j, 1);
                            if (pathItem && pathItem->text() == importedPath) {
                                QStandardItem* fileItem = nodeItem->child(j, 0);
                                QString fileName = fileItem ? fileItem->text() : "";

                                auto* iface = getProjectContext();
                                if (iface && iface->projectXml()) {
                                    XMLFile* xml = iface->projectXml();
                                    xml->XMLFile_remove_node(getOutputNodeName().toStdString().c_str(),
                                                          fileName.toStdString().c_str(),
                                                          importedPath.toStdString().c_str());
                                    xml->XMLFile_save(iface->projectPath().toStdString().c_str());
                                } else {
                                    XMLFile xml;
                                    QString xmlPath = projectPath() + "/" + projectName();
                                    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) >= 0) {
                                        xml.XMLFile_remove_node(getOutputNodeName().toStdString().c_str(),
                                                              fileName.toStdString().c_str(),
                                                              importedPath.toStdString().c_str());
                                        xml.XMLFile_save(xmlPath.toStdString().c_str());
                                    }
                                }

                                if (QFile::exists(importedPath)) {
                                    QFile::remove(importedPath);
                                }

                                nodeItem->removeRow(j);

                                if (auto* iface = getProjectContext()) {
                                    iface->refreshProjectTree();
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }
        // --- 结束移除 ---

        m_imagePaths.removeAt(row);
        delete item;
        changed = true;
    }

    if (changed)
    {
        if (!m_imagePaths.isEmpty()) {
            m_imageInfo = std::make_shared<ImageInfoData>(m_imagePaths);
            setOutputData(1, m_imageInfo);
        } else {
            m_imageInfo.reset();
            setOutputData(1, nullptr);
        }

        m_importedFilePaths.clear();
        setOutputData(0, nullptr);
        setState(ExecutionState::Idle);

        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
    }
}

// ============================================================================
// save / load
// ============================================================================

QJsonObject GenericSARBatchImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_imagePaths)
        pathsArray.append(path);
    json["imagePaths"] = pathsArray;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    return json;
}

void GenericSARBatchImportNode::load(QJsonObject const &json)
{
    m_imagePaths.clear();
    QJsonArray pathsArray = json["imagePaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_imagePaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("GenericSAR_Batch_Import");

    if (!m_imagePaths.isEmpty()) {
        m_imageInfo = std::make_shared<ImageInfoData>(m_imagePaths);
    } else {
        m_imageInfo.reset();
    }

    ImportNodeBase::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString& file : m_imagePaths) {
            m_fileListWidget->addItem(QFileInfo(file).fileName());
        }
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    Q_EMIT dataUpdated(1);
}

// ============================================================================
// 槽函数实现（委托给基类）
// ============================================================================

void GenericSARBatchImportNode::onImportProgress(int progress, const QString& message)
{
    ImportNodeBase::onImportProgress(progress, message);
}

void GenericSARBatchImportNode::onImportFinished()
{
    if (m_outputPersistenceFailed) {
        onThreadError(QStringLiteral("Unable to save imported outputs to the project."));
        return;
    }

    m_importedFilePaths = m_generatedOutputPaths.isEmpty()
        ? getExpectedOutputFilePaths()
        : m_generatedOutputPaths;

    if (!m_importedFilePaths.isEmpty()) {
        m_imageInfo = std::make_shared<ImageInfoData>(m_importedFilePaths);
        setOutputData(0, m_imageInfo);
        setOutputData(1, m_imageInfo);
    }

    InSARLogManager::LogInfo(getOutputNodeName() + "Node", "execute completed.");

    finishExecution();

    m_task = nullptr;
}

void GenericSARBatchImportNode::onThreadError(const QString& error)
{
    ImportNodeBase::onThreadError(error);
}

void GenericSARBatchImportNode::onOutputsGenerated(const QString& dstNode,
                                                    const QStringList& outputNames,
                                                    const QStringList& outputPaths,
                                                    const QString& dataType,
                                                    const QString& satelliteFormat)
{
    ImportNodeBase::onOutputsGenerated(dstNode, outputNames, outputPaths, dataType, satelliteFormat);
}

NodeDataType GenericSARBatchImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"image_info", "Image Info"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool GenericSARBatchImportNode::validateAndRestoreOutput()
{
    QStringList expectedPaths = getExpectedOutputFilePaths();
    if (expectedPaths.isEmpty())
        return false;

    // Check file existence
    for (const QString& path : expectedPaths) {
        if (!QFile::exists(path)) {
            return false;
        }
    }

    m_importedFilePaths = expectedPaths;
    m_imageInfo = std::make_shared<ImageInfoData>(expectedPaths);
    setOutputData(0, m_imageInfo);
    setOutputData(1, m_imageInfo);
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    return true;
}

} // namespace QtNodes
