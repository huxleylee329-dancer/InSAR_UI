#include "InSARLogManager.h"

#include "GenericSARBatchImportNode.h"
#include "IApplicationInterface.h"
#include <QFile>
#include <QJsonArray>
#include <QDebug>
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

GenericSARBatchImportNode::~GenericSARBatchImportNode()
{
    if (m_task)
    {
        m_task->stop();
    }
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
    m_outputNodeNameEdit->setText("GenericSAR_Batch_Import");
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

void GenericSARBatchImportNode::executeImport()
{
    if (m_task)
    {
        return;
    }

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
        getOutputNodeName(),
        projectName(),
        projectModel()
    );

    connect(m_task, &GenericSARBatchImportTask::updateProcess,
            this, &GenericSARBatchImportNode::onImportProgress, Qt::QueuedConnection);
    connect(m_task, &GenericSARBatchImportTask::endProcess,
            this, &GenericSARBatchImportNode::onImportFinished, Qt::QueuedConnection);
    connect(m_task, &GenericSARBatchImportTask::errorProcess,
            this, &GenericSARBatchImportNode::onThreadError, Qt::QueuedConnection);
    connect(m_task, &GenericSARBatchImportTask::sendModel,
            this, &GenericSARBatchImportNode::onModelUpdated, Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(m_task);
}

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
        onError("请至少添加一个 通用 SAR 图像文件。");
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

QStringList GenericSARBatchImportNode::getImportedFilePaths() const
{
    return m_importedFilePaths;
}

QString GenericSARBatchImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
        return "GenericSAR_Batch_Import";

    return name;
}

QStringList GenericSARBatchImportNode::previewImagePaths() const
{
    QStringList existingPaths;
    for (const QString& path : m_importedFilePaths) {
        if (QFileInfo::exists(path)) {
            existingPaths << path;
        }
    }
    return existingPaths;
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
            m_imageInfoData = std::make_shared<ImageInfoData>(m_imagePaths);
            setOutputData(1, m_imageInfoData);
        } else {
            m_imageInfoData.reset();
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
                                
                                XMLFile* xml = new XMLFile();
                                QString xmlPath = projectPath() + "/" + projectName();
                                if (xml->XMLFile_load(xmlPath.toStdString().c_str()) >= 0) {
                                    xml->XMLFile_remove_node(getOutputNodeName().toStdString().c_str(), 
                                                          fileName.toStdString().c_str(), 
                                                          importedPath.toStdString().c_str());
                                    xml->XMLFile_save(xmlPath.toStdString().c_str());
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
            m_imageInfoData = std::make_shared<ImageInfoData>(m_imagePaths);
            setOutputData(1, m_imageInfoData);
        } else {
            m_imageInfoData.reset();
            setOutputData(1, nullptr);
        }
        
        m_importedFilePaths.clear();
        setOutputData(0, nullptr);
        setState(ExecutionState::Idle);
        
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
    }
}

void GenericSARBatchImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void GenericSARBatchImportNode::stopExecution()
{
    ImportNodeBase::stopExecution();
    if (m_task) m_task->stop();
}

void GenericSARBatchImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();

    m_importedFilePaths.clear();
    for (const QString& imagePath : m_imagePaths)
    {
        QString importName = generateImportName(imagePath);
        QString suffix = QFileInfo(imagePath).suffix();
        QString filePath = QString("%1/%2/%3.%4")
            .arg(projectPath(), outputNodeName, importName, suffix);

        m_importedFilePaths.append(filePath);
    }

    m_imageInfoData = std::make_shared<ImageInfoData>(m_importedFilePaths);
    setOutputData(0, m_imageInfoData);
    Q_EMIT dataUpdated(0);

    setOutputData(1, m_imageInfoData);
    Q_EMIT dataUpdated(1);

    finishExecution();

    m_task = nullptr;
}

void GenericSARBatchImportNode::onThreadError(const QString& error)
{
    onError(error);

    m_task = nullptr;
}

void GenericSARBatchImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void GenericSARBatchImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
    }
}

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
        m_imageInfoData = std::make_shared<ImageInfoData>(m_imagePaths);
    } else {
        m_imageInfoData.reset();
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

unsigned int GenericSARBatchImportNode::nPorts(PortType portType) const
{
    if (portType == PortType::Out)
        return 2;
    return 0;
}

NodeDataType GenericSARBatchImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return NodeDataType{"image_info", "Image Info"};
        else if (portIndex == 1)
            return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool GenericSARBatchImportNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString GenericSARBatchImportNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return tr("成果 *");
        else if (portIndex == 1)
            return tr("预览 ?");
    }
    return QString();
}

bool GenericSARBatchImportNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;

    return false;
}

std::shared_ptr<NodeData> GenericSARBatchImportNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

bool GenericSARBatchImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();

    if (nodeName.isEmpty()) {
        return false;
    }

    QString outputPath = projectPath() + "/" + nodeName + "/";

    QDir dir(outputPath);
    bool dirExists = dir.exists();
    int fileCount = dirExists ? dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() : 0;

    if (dirExists && fileCount > 0) {
        m_importedFilePaths.clear();
        for (const QString &imagePath : m_imagePaths) {
            QString importName = generateImportName(imagePath);
            QString suffix = QFileInfo(imagePath).suffix();
            QString importedPath = outputPath + importName + "." + suffix;
            bool fileExists = QFile::exists(importedPath);
            if (fileExists) {
                m_importedFilePaths.append(importedPath);
            }
        }
        if (!m_importedFilePaths.isEmpty()) {
            m_imageInfoData = std::make_shared<ImageInfoData>(m_importedFilePaths);
            setOutputData(0, m_imageInfoData);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
            return true;
        }
    }

    return false;
}

} // namespace QtNodes
