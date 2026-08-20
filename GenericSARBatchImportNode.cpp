#include "InSARLogManager.h"

#include "GenericSARBatchImportNode.h"
#include "IApplicationInterface.h"
#include <QFile>
#include <QJsonArray>
#include <QThreadPool>
#include "FormatConversion.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include "NodeUtils.h"

namespace QtNodes {

GenericSARBatchImportNode::GenericSARBatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectLabel(nullptr)
{
}

GenericSARBatchImportNode::~GenericSARBatchImportNode()
{
    if (m_cancellationToken) {
        m_cancellationToken->store(true, std::memory_order_relaxed);
    }
}

unsigned int GenericSARBatchImportNode::nPorts(PortType portType) const
{
    return portType == PortType::In ? 0 : 2;
}

ProductOutputContract GenericSARBatchImportNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    if (portIndex == 0) {
        contract.semanticId = QStringLiteral("generic_sar_batch_import.output.generic_sar_raster");
        contract.publishedProductTypes = QStringList() << QStringLiteral("generic_sar_raster");
    } else if (portIndex == 1) {
        contract.semanticId = QStringLiteral("generic_sar_batch_import.output.preview");
        contract.publishedProductTypes = QStringList() << QStringLiteral("preview");
    }
    return contract;
}

void GenericSARBatchImportNode::stopExecution()
{
    if (m_cancellationToken) {
        m_cancellationToken->store(true, std::memory_order_relaxed);
    }
    ImportNodeBase::stopExecution();
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
    if (m_cancellationToken)
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
        if (!m_semanticTransactionActive) {
            NodeUtils::removeDataNodeFromProject(getProjectContext(), getOutputNodeName());
        }
    }

    m_cancellationToken = std::make_shared<std::atomic_bool>(false);
    auto* task = new GenericSARBatchImportTask(
        projectPath(),
        m_preparedOriginalFileList,
        m_preparedImportNameList,
        m_semanticTransactionActive ? m_outputTransaction.stagingName : getOutputNodeName(),
        m_cancellationToken
    );
    // The task owns this copy until its terminal signal is handled after node destruction.
    const auto terminalTransaction = std::make_shared<NodeUtils::OutputTransaction>(m_outputTransaction);
    const auto terminalFinalized = std::make_shared<std::atomic_bool>(false);
    const QPointer<GenericSARBatchImportNode> nodeGuard(this);
    const auto finalizeAfterNodeDestruction = [nodeGuard, terminalTransaction, terminalFinalized](const QString& reason) {
        if (nodeGuard || terminalFinalized->exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        NodeUtils::abandonOutputTransaction(*terminalTransaction, reason);
    };

    connect(task, &GenericSARBatchImportTask::updateProcess,
            this, &GenericSARBatchImportNode::onImportProgress, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::endProcess,
            this, &GenericSARBatchImportNode::onImportFinished, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::errorProcess,
            this, &GenericSARBatchImportNode::onThreadError, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::cancelled,
            this, &GenericSARBatchImportNode::onImportCancelled, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::outputsGenerated,
            this, &GenericSARBatchImportNode::onOutputsGenerated, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::endProcess, task,
            [finalizeAfterNodeDestruction]() {
                finalizeAfterNodeDestruction(QStringLiteral("generic batch import node was destroyed before completion"));
            }, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::errorProcess, task,
            [finalizeAfterNodeDestruction](const QString& error) {
                finalizeAfterNodeDestruction(error);
            }, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::cancelled, task,
            [finalizeAfterNodeDestruction]() {
                finalizeAfterNodeDestruction(QStringLiteral("generic batch import node was destroyed during cancellation"));
            }, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::endProcess,
            task, &QObject::deleteLater, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::errorProcess,
            task, &QObject::deleteLater, Qt::QueuedConnection);
    connect(task, &GenericSARBatchImportTask::cancelled,
            task, &QObject::deleteLater, Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(task);
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
        onError("请至少添加一个 通用 SAR 图像文件。");
        return false;
    }

    if (m_cancellationToken)
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
        if (!isSupportedGenericSarRasterPath(imagePath))
        {
            onError("通用 SAR 导入仅支持 JPG、PNG、BMP、TIF 或 TIFF 栅格文件。");
            return false;
        }

        m_preparedOriginalFileList.push_back(imagePath);
        m_preparedImportNameList.push_back(generateImportName(imagePath));
    }

    QStringList pathsToCheck;
    for (size_t i = 0; i < m_preparedImportNameList.size(); ++i) {
        const QString suffix = QFileInfo(m_preparedOriginalFileList[i]).suffix();
        pathsToCheck.append(projectPath() + "/" + getOutputNodeName() + "/" + m_preparedImportNameList[i] + "." + suffix);
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
        if (!isSupportedGenericSarRasterPath(imagePath)) return {};
        QString importName = generateImportName(imagePath);
        const QString suffix = QFileInfo(imagePath).suffix();
        paths.append(projectPath() + "/" + getOutputNodeName() + "/" + importName + "." + suffix);
    }
    return paths;
}

QStringList GenericSARBatchImportNode::getExpectedPreviewFilePaths() const
{
    return getExpectedOutputFilePaths();
}

QStringList GenericSARBatchImportNode::transactionInputPaths() const
{
    QStringList paths;
    for (const QString& path : m_preparedOriginalFileList) {
        paths.append(path);
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
        m_importedFilePaths.clear();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
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
        m_imagePaths.removeAt(row);
        delete m_fileListWidget->takeItem(row);
        changed = true;
    }

    if (changed)
    {
        m_importedFilePaths.clear();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
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

    ImportNodeBase::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString& file : m_imagePaths) {
            m_fileListWidget->addItem(QFileInfo(file).fileName());
        }
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

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
    ImportNodeBase::onImportFinished();
    m_cancellationToken.reset();
}

void GenericSARBatchImportNode::onThreadError(const QString& error)
{
    ImportNodeBase::onThreadError(error);
    m_cancellationToken.reset();
}

void GenericSARBatchImportNode::onImportCancelled()
{
    ImportNodeBase::onImportCancelled();
    m_cancellationToken.reset();
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
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported File"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool GenericSARBatchImportNode::validateAndRestoreOutput()
{
    if (!ImportNodeBase::validateAndRestoreOutput()) {
        return false;
    }
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    return true;
}

} // namespace QtNodes
