#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "GenericSARImportNode.h"

#include <QFileInfo>

namespace QtNodes {

GenericSARImportNode::GenericSARImportNode()
    : ImportNodeBase()
    , m_imageEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_projectCombo(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

GenericSARImportNode::~GenericSARImportNode()
{
    if (m_workerThread)
    {
        if (m_thread && m_thread->isRunning())
            m_workerThread->StopProcess();

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

QWidget* GenericSARImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 通用 SAR 图像 + 浏览按钮 [3:5:2]
    auto* imageRow = new QHBoxLayout();
    imageRow->setStretch(0, 3);
    imageRow->setStretch(1, 5);
    imageRow->setStretch(2, 2);
    imageRow->addWidget(new QLabel("通用 SAR 图像："));
    m_imageEdit = new QLineEdit();
    m_imageEdit->setPlaceholderText("选择通用 SAR 图像文件");
    connect(m_imageEdit, &QLineEdit::textChanged, this, [this](const QString& text) { 
        m_imagePath = text; 
        if (!m_imagePath.isEmpty() && QFileInfo::exists(m_imagePath))
        {
            m_imageInfoData = std::make_shared<ImageInfoData>(m_imagePath);
            Q_EMIT dataUpdated(1);
        }
    });
    QPushButton* browseButton = new QPushButton("浏览...");
    imageRow->addWidget(m_imageEdit);
    imageRow->addWidget(browseButton);
    layout->addLayout(imageRow);

    // 项目名称 [3:7]
    auto* projectRow = new QHBoxLayout();
    projectRow->setStretch(0, 3);
    projectRow->setStretch(1, 7);
    projectRow->addWidget(new QLabel("项目名称："));
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
    layout->addLayout(projectRow);

    // 目标节点 [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("手动输入目标节点名称");
    nodeRow->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeRow);

    // 目标文件名 [3:7]
    auto* fileNameRow = new QHBoxLayout();
    fileNameRow->setStretch(0, 3);
    fileNameRow->setStretch(1, 7);
    fileNameRow->addWidget(new QLabel("目标文件名："));
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setPlaceholderText("自动生成或手动输入");
    connect(m_outputFileNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_outputFileName = text; });
    fileNameRow->addWidget(m_outputFileNameEdit);
    layout->addLayout(fileNameRow);

    connect(browseButton, &QPushButton::clicked,
            this, &GenericSARImportNode::onImageBrowseClicked);

    return widget;
}

void GenericSARImportNode::executeImport()
{
    if (executionState() == ExecutionState::Running)
        return;

    // Safety check: Ensure project is open
    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
    {
        onError("未检测到打开的项目，请先打开或新建一个项目。");
        return;
    }

    if (getOutputNodeName().isEmpty())
    {
        onError("目标节点名不能为空！");
        return;
    }

    m_imagePath = m_imageEdit->text().trimmed();
    if (m_imagePath.isEmpty())
    {
        onError("请选择一个 通用 SAR 图像文件。");
        return;
    }

    if (!QFileInfo::exists(m_imagePath))
    {
        onError("通用 SAR 图像文件不存在：" + m_imagePath);
        return;
    }

    m_outputFileName = m_outputFileNameEdit->text().trimmed();
    if (m_outputFileName.isEmpty())
    {
        m_outputFileName = QFileInfo(m_imagePath).baseName();
        m_outputFileNameEdit->setText(m_outputFileName);
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &GenericSARImportNode::startGenericSARImport,
            m_workerThread, &MyThread::import_GenericSAR);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &GenericSARImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &GenericSARImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &GenericSARImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &GenericSARImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startGenericSARImport(
        m_imagePath,
        projectPath(),
        getOutputNodeName(),
        m_outputFileName,
        projectName(),
        projectModel()
    );
}

QString GenericSARImportNode::getImportedFilePath() const
{
    if (!m_importedFilePath.isEmpty())
        return m_importedFilePath;

    return QString("%1/%2/%3.h5")
        .arg(projectPath())
        .arg(getOutputNodeName())
        .arg(m_outputFileName);
}

QString GenericSARImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    return name;
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

    if (m_outputFileNameEdit->text().trimmed().isEmpty())
        m_outputFileNameEdit->setText(QFileInfo(filePath).baseName());
}

void GenericSARImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void GenericSARImportNode::onImportFinished()
{
    m_importedFilePath = QString("%1/%2/%3.h5")
        .arg(projectPath())
        .arg(getOutputNodeName())
        .arg(m_outputFileName);

    // 1. Process primary InSAR output (Port 0)
    ImportNodeBase::onImportFinished();

    // 2. Process independent preview output (Port 1)
    if (!m_imagePath.isEmpty())
    {
        // Pass the original image path as preview info
        m_imageInfoData = std::make_shared<ImageInfoData>(m_imagePath);
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

void GenericSARImportNode::onThreadError(const QString& error)
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

void GenericSARImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void GenericSARImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

QJsonObject GenericSARImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["imagePath"] = m_imagePath;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : QString();
    json["outputFileName"] = m_outputFileName;
    return json;
}

void GenericSARImportNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);
    m_imagePath = json["imagePath"].toString();
    m_outputFileName = json["outputFileName"].toString();

    if (m_imageEdit) m_imageEdit->setText(m_imagePath);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(json["outputNodeName"].toString());
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
            return NodeDataType{"imported_file", "Imported File"};
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
    if (port == 0)
        return ImportNodeBase::outData(0);
    else if (port == 1)
        return m_imageInfoData;

    return nullptr;
}

} // namespace QtNodes
