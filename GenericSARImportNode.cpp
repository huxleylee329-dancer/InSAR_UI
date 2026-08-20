#include "InSARLogManager.h"

#include "GenericSARImportNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "NodeUtils.h"
#include "ImportDataTypes.h"

#include <QFile>
#include <QFileInfo>
#include <QApplication>
#include <QPointer>

namespace QtNodes {

GenericSARImportNode::GenericSARImportNode()
    : ImportNodeBase()
    , m_imageEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_projectLabel(nullptr)
{
    m_outputFileName = "{InputName}";
}

GenericSARImportNode::~GenericSARImportNode()
{
    if (m_cancellationToken) {
        m_cancellationToken->store(true, std::memory_order_relaxed);
    }
}

unsigned int GenericSARImportNode::nPorts(PortType portType) const
{
    return portType == PortType::In ? 0 : 2;
}

ProductOutputContract GenericSARImportNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    if (portIndex == 0) {
        contract.semanticId = QStringLiteral("generic_sar_import.output.generic_sar_raster");
        contract.publishedProductTypes = QStringList() << QStringLiteral("generic_sar_raster");
    } else if (portIndex == 1) {
        contract.semanticId = QStringLiteral("generic_sar_import.output.preview");
        contract.publishedProductTypes = QStringList() << QStringLiteral("preview");
    }
    return contract;
}

void GenericSARImportNode::stopExecution()
{
    if (m_cancellationToken) {
        m_cancellationToken->store(true, std::memory_order_relaxed);
    }
    ImportNodeBase::stopExecution();
}

QWidget* GenericSARImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        setOutputData(0, nullptr);
        invalidateExecution();
    };

    // Project Name Badge
    m_projectLabel = createProjectBadge(projectName());
    layout->addWidget(m_projectLabel);

    // 通用 SAR 图像 + 浏览按钮 [3:7:0]
    auto* imageRow = new QHBoxLayout();
    imageRow->addWidget(new QLabel("通用 SAR 图像："), 3);
    m_imageEdit = new QLineEdit();
    m_imageEdit->setPlaceholderText("选择通用 SAR 图像文件");
    connect(m_imageEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_imageEdit->text();
        if (m_imagePath != text) {
            if (!confirmParameterChange()) {
                m_imageEdit->setText(m_imagePath);
                return;
            }
            m_imagePath = text;
            invalidateNodeData();
            if (!m_imagePath.isEmpty() && QFileInfo::exists(m_imagePath))
            {
            }
        }
    });
    QPushButton* browseButton = new QPushButton("浏览...");
    imageRow->addWidget(m_imageEdit, 7);
    imageRow->addWidget(browseButton, 0);
    layout->addLayout(imageRow);

    // 目标节点 [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->addWidget(new QLabel("目标节点："), 3);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText("手动输入目标节点名称");
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
    nodeRow->addWidget(m_outputNodeNameEdit, 7);
    layout->addLayout(nodeRow);

    // 目标文件名 [3:7]
    auto* fileNameRow = new QHBoxLayout();
    fileNameRow->addWidget(new QLabel("目标文件名："), 3);
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setText(m_outputFileName);
    m_outputFileNameEdit->setPlaceholderText("自动生成或手动输入");
    connect(m_outputFileNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputFileNameEdit->text();
        if (m_outputFileName != text) {
            if (!confirmParameterChange()) {
                m_outputFileNameEdit->setText(m_outputFileName);
                return;
            }
            m_outputFileName = text;
            invalidateNodeData();
        }
    });
    fileNameRow->addWidget(m_outputFileNameEdit, 7);
    layout->addLayout(fileNameRow);

    connect(browseButton, &QPushButton::clicked,
            this, &GenericSARImportNode::onImageBrowseClicked);

    return widget;
}

void GenericSARImportNode::executeImport()
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
    auto* task = new GenericSARImportTask(
        m_imagePath,
        projectPath(),
        m_semanticTransactionActive ? m_outputTransaction.stagingName : getOutputNodeName(),
        m_preparedOutputFileName,
        m_cancellationToken
    );
    // The task owns this copy until its terminal signal is handled after node destruction.
    const auto terminalTransaction = std::make_shared<NodeUtils::OutputTransaction>(m_outputTransaction);
    const auto terminalFinalized = std::make_shared<std::atomic_bool>(false);
    const QPointer<GenericSARImportNode> nodeGuard(this);
    const auto finalizeAfterNodeDestruction = [nodeGuard, terminalTransaction, terminalFinalized](const QString& reason) {
        if (nodeGuard || terminalFinalized->exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        NodeUtils::abandonOutputTransaction(*terminalTransaction, reason);
    };

    connect(task, &GenericSARImportTask::updateProcess,
            this, &GenericSARImportNode::onImportProgress, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::endProcess,
            this, &GenericSARImportNode::onImportFinished, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::errorProcess,
            this, &GenericSARImportNode::onThreadError, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::cancelled,
            this, &GenericSARImportNode::onImportCancelled, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::outputsGenerated,
            this, &GenericSARImportNode::onOutputsGenerated, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::endProcess, task,
            [finalizeAfterNodeDestruction]() {
                finalizeAfterNodeDestruction(QStringLiteral("generic import node was destroyed before completion"));
            }, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::errorProcess, task,
            [finalizeAfterNodeDestruction](const QString& error) {
                finalizeAfterNodeDestruction(error);
            }, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::cancelled, task,
            [finalizeAfterNodeDestruction]() {
                finalizeAfterNodeDestruction(QStringLiteral("generic import node was destroyed during cancellation"));
            }, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::endProcess,
            task, &QObject::deleteLater, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::errorProcess,
            task, &QObject::deleteLater, Qt::QueuedConnection);
    connect(task, &GenericSARImportTask::cancelled,
            task, &QObject::deleteLater, Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(task);
}

bool GenericSARImportNode::prepareToStart()
{
    auto* model = projectModel();
    QString path = projectPath();
    QString name = projectName();

    if (!model || path.isEmpty() || name.isEmpty())
    {
        onError("未检测到打开的项目，请先打开或新建一个项目。");
        return false;
    }

    if (m_cancellationToken)
    {
        return false;
    }

    QString outputNodeName = getOutputNodeName();
    if (outputNodeName.isEmpty())
    {
        onError("目标节点名不能为空！");
        return false;
    }

    m_imagePath = m_imageEdit->text().trimmed();
    if (m_imagePath.isEmpty())
    {
        onError("请选择一个 通用 SAR 图像文件。");
        return false;
    }

    if (!QFileInfo::exists(m_imagePath))
    {
        onError("通用 SAR 图像文件不存在：" + m_imagePath);
        return false;
    }
    if (!isSupportedGenericSarRasterPath(m_imagePath))
    {
        onError("通用 SAR 导入仅支持 JPG、PNG、BMP、TIF 或 TIFF 栅格文件。");
        return false;
    }

    m_outputFileName = m_outputFileNameEdit->text().trimmed();
    if (m_outputFileName.isEmpty())
    {
        m_outputFileName = "{InputName}";
        m_outputFileNameEdit->setText(m_outputFileName);
    }

    m_preparedOutputFileName = m_outputFileName;
    QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
    m_preparedOutputFileName.replace(re, QFileInfo(m_imagePath).baseName());

    const QString suffix = QFileInfo(m_imagePath).suffix();
    QString outputPath = QString("%1/%2/%3.%4").arg(projectPath()).arg(outputNodeName).arg(m_preparedOutputFileName).arg(suffix);
    m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(getProjectContext(), outputNodeName, {outputPath}, nullptr);
    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

QStringList GenericSARImportNode::getExpectedOutputFilePaths() const
{
    if (!m_imagePath.isEmpty() && !m_outputNodeName.isEmpty()) {
        if (!isSupportedGenericSarRasterPath(m_imagePath)) return {};
        const QString suffix = QFileInfo(m_imagePath).suffix();

        QString resolvedFileName = m_outputFileName;
        QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
        resolvedFileName.replace(re, QFileInfo(m_imagePath).baseName());

        return { QString("%1/%2/%3.%4")
            .arg(projectPath())
            .arg(m_outputNodeName)
            .arg(resolvedFileName)
            .arg(suffix) };
    }
    return {};
}

QStringList GenericSARImportNode::getExpectedPreviewFilePaths() const
{
    return getExpectedOutputFilePaths();
}

QStringList GenericSARImportNode::transactionInputPaths() const
{
    return m_imagePath.isEmpty() ? QStringList() : QStringList() << m_imagePath;
}

QString GenericSARImportNode::getOutputNodeName() const
{
    return m_outputNodeName;
}

void GenericSARImportNode::onImageBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择通用 SAR 图像"),
        QFileInfo(m_imagePath).absolutePath(),
        tr("Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff)")
    );

    if (filePath.isEmpty())
        return;

    m_imageEdit->setText(filePath);
    m_imagePath = filePath;

    if (m_outputFileNameEdit->text().trimmed().isEmpty())
        m_outputFileNameEdit->setText("{InputName}");

    m_importedFilePaths.clear();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    setState(ExecutionState::Idle);

    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
}

void GenericSARImportNode::onImportFinished()
{
    ImportNodeBase::onImportFinished();
    m_cancellationToken.reset();
}

void GenericSARImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void GenericSARImportNode::onThreadError(const QString& error)
{
    ImportNodeBase::onThreadError(error);
    m_cancellationToken.reset();
}

void GenericSARImportNode::onImportCancelled()
{
    ImportNodeBase::onImportCancelled();
    m_cancellationToken.reset();
}

void GenericSARImportNode::onOutputsGenerated(const QString& dstNode,
                                               const QStringList& outputNames,
                                               const QStringList& outputPaths,
                                               const QString& dataType,
                                               const QString& satelliteFormat)
{
    ImportNodeBase::onOutputsGenerated(dstNode, outputNames, outputPaths, dataType, satelliteFormat);
}

QJsonObject GenericSARImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["imagePath"] = m_imagePath;
    json["outputNodeName"] = m_outputNodeName;
    json["outputFileName"] = m_outputFileName;
    return json;
}

void GenericSARImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_imagePath = json["imagePath"].toString();
    m_outputNodeName = json["outputNodeName"].toString();
    m_outputFileName = json["outputFileName"].toString();

    ExecutableNodeDelegateModel::load(json);

    if (m_imageEdit) m_imageEdit->setText(m_imagePath);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setText(m_outputFileName);
}

NodeDataType GenericSARImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported File"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool GenericSARImportNode::validateAndRestoreOutput()
{
    if (!ImportNodeBase::validateAndRestoreOutput()) {
        return false;
    }
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    return true;
}

} // namespace QtNodes
