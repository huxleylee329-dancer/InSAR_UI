#include "InSARLogManager.h"

#include "GenericSARImportNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "NodeUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QApplication>

namespace QtNodes {

GenericSARImportNode::GenericSARImportNode()
    : ImportNodeBase()
    , m_imageEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_projectLabel(nullptr)
    , m_task(nullptr)
{
    m_outputFileName = "{InputName}";
}

GenericSARImportNode::~GenericSARImportNode()
{
    if (m_task)
    {
        m_task->stop();
    }
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
                m_imageInfoData = std::make_shared<ImageInfoData>(m_imagePath);
                Q_EMIT dataUpdated(1);
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
            NodeUtils::removeDataNodeFromProject(getProjectContext(), m_outputNodeName);
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

    m_task = new GenericSARImportTask(
        m_imagePath,
        projectPath(),
        getOutputNodeName(),
        m_preparedOutputFileName,
        projectName(),
        projectModel()
    );

    connect(m_task, &GenericSARImportTask::updateProcess,
            this, &GenericSARImportNode::onImportProgress, Qt::QueuedConnection);
    connect(m_task, &GenericSARImportTask::endProcess,
            this, &GenericSARImportNode::onImportFinished, Qt::QueuedConnection);
    connect(m_task, &GenericSARImportTask::errorProcess,
            this, &GenericSARImportNode::onThreadError, Qt::QueuedConnection);
    connect(m_task, &GenericSARImportTask::sendModel,
            this, &GenericSARImportNode::onModelUpdated, Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(m_task);
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

    if (m_task)
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

    m_outputFileName = m_outputFileNameEdit->text().trimmed();
    if (m_outputFileName.isEmpty())
    {
        m_outputFileName = "{InputName}";
        m_outputFileNameEdit->setText(m_outputFileName);
    }

    m_preparedOutputFileName = m_outputFileName;
    QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
    m_preparedOutputFileName.replace(re, QFileInfo(m_imagePath).baseName());

    QString suffix = QFileInfo(m_imagePath).suffix();
    if (suffix.isEmpty()) suffix = "h5";
    QString outputPath = QString("%1/%2/%3.%4").arg(projectPath()).arg(outputNodeName).arg(m_preparedOutputFileName).arg(suffix);
    QString previewPath = QString("%1/%2/%3.jpg").arg(projectPath()).arg(outputNodeName).arg(m_preparedOutputFileName);

    m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(getProjectContext(), outputNodeName, {outputPath, previewPath}, nullptr);
    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

QStringList GenericSARImportNode::getImportedFilePaths() const
{
    QStringList paths;
    if (!m_importedFilePath.isEmpty()) {
        paths.append(m_importedFilePath);
    } else {
        QString suffix = QFileInfo(m_imagePath).suffix();
        if (suffix.isEmpty()) suffix = "h5"; // Fallback

        QString resolvedFileName = m_outputFileName;
        QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
        resolvedFileName.replace(re, QFileInfo(m_imagePath).baseName());

        paths.append(QString("%1/%2/%3.%4")
            .arg(projectPath())
            .arg(m_outputNodeName)
            .arg(resolvedFileName)
            .arg(suffix));
    }
    return paths;
}

QString GenericSARImportNode::getOutputNodeName() const
{
    return m_outputNodeName;
}

QStringList GenericSARImportNode::previewImagePaths() const
{
    if (!m_importedFilePath.isEmpty() && QFileInfo::exists(m_importedFilePath)) {
        return QStringList() << m_importedFilePath;
    }
    return QStringList();
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

    m_imageInfoData = std::make_shared<ImageInfoData>(m_imagePath);
    setOutputData(1, m_imageInfoData);
    
    m_importedFilePath.clear();
    setOutputData(0, nullptr);
    setState(ExecutionState::Idle);
    
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
}

void GenericSARImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void GenericSARImportNode::stopExecution()
{
    ImportNodeBase::stopExecution();
    if (m_task) m_task->stop();
}

void GenericSARImportNode::onImportFinished()
{
    QString suffix = QFileInfo(m_imagePath).suffix();
    if (suffix.isEmpty()) suffix = "h5";

    QString resolvedFileName = m_outputFileName;
    QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
    resolvedFileName.replace(re, QFileInfo(m_imagePath).baseName());

    m_importedFilePath = QString("%1/%2/%3.%4")
        .arg(projectPath())
        .arg(m_outputNodeName)
        .arg(resolvedFileName)
        .arg(suffix);

    // Port 0: 输出 ImageInfoData（导入后的路径），供下游处理节点使用
    if (!m_importedFilePath.isEmpty())
    {
        auto outputData = std::make_shared<ImageInfoData>(m_importedFilePath);
        setOutputData(0, outputData);
        Q_EMIT dataUpdated(0);
    }

    // Port 1: 预览输出（保持使用导入后的路径更统一）
    if (!m_importedFilePath.isEmpty())
    {
        m_imageInfoData = std::make_shared<ImageInfoData>(m_importedFilePath);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    }

    finishExecution();

    m_task = nullptr;
}

void GenericSARImportNode::onThreadError(const QString& error)
{
    onError(error);

    m_task = nullptr;
}

void GenericSARImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void GenericSARImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
    }
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

unsigned int GenericSARImportNode::nPorts(PortType portType) const
{
    // No input ports, two output ports (Port 0: Result, Port 1: Preview)
    if (portType == PortType::In)
        return 0;
    else
        return 2;
}

NodeDataType GenericSARImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return NodeDataType{"image_info", "Image Info"};
        else if (portIndex == 1)
            return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool GenericSARImportNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString GenericSARImportNode::portCaption(PortType portType, PortIndex portIndex) const
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

bool GenericSARImportNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    // Port 1 is optional
    if (portType == PortType::Out && portIndex == 1)
        return true;

    return false;
}

std::shared_ptr<NodeData> GenericSARImportNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

bool GenericSARImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString projDirStr = projectPath();

    QString suffix = QFileInfo(m_imagePath).suffix();
    if (suffix.isEmpty()) suffix = "h5";

    QString finalFileName = m_outputFileName;
    if (finalFileName.isEmpty()) {
        finalFileName = QFileInfo(m_imagePath).baseName();
    } else {
        QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
        finalFileName.replace(re, QFileInfo(m_imagePath).baseName());
    }

    QString outputPath = QString("%1/%2/%3.%4")
        .arg(projDirStr)
        .arg(nodeName)
        .arg(finalFileName)
        .arg(suffix);

    if (QFile::exists(outputPath)) {
        m_importedFilePath = outputPath;
        if (!m_importedFilePath.isEmpty()) {
            auto outputData = std::make_shared<ImageInfoData>(m_importedFilePath);
            setOutputData(0, outputData);
            m_imageInfoData = outputData;
            setOutputData(1, outputData);
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
            return true;
        }
    }

    return false;
}

} // namespace QtNodes
