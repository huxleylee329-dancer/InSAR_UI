#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "MacaoImportNode.h"

#include <QFileInfo>

namespace QtNodes {

MacaoImportNode::MacaoImportNode()
    : ImportNodeBase()
    , m_imageEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_projectCombo(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

MacaoImportNode::~MacaoImportNode()
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

QWidget* MacaoImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* imageRow = new QHBoxLayout();
    imageRow->addWidget(new QLabel("Macaoͼ��"));
    m_imageEdit = new QLineEdit();
    connect(m_imageEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_imagePath = text; });
    QPushButton* browseButton = new QPushButton("���...");
    imageRow->addWidget(m_imageEdit);
    imageRow->addWidget(browseButton);
    layout->addLayout(imageRow);

    auto* projectRow = new QHBoxLayout();
    projectRow->addWidget(new QLabel("Ŀ�깤�̣�"));
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    if (!projectName().isEmpty())
        m_projectCombo->addItem(projectName());
    projectRow->addWidget(m_projectCombo);
    layout->addLayout(projectRow);

    auto* nodeRow = new QHBoxLayout();
    nodeRow->addWidget(new QLabel("Ŀ��ڵ㣺"));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText("Macao_Import");
    nodeRow->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeRow);

    auto* fileNameRow = new QHBoxLayout();
    fileNameRow->addWidget(new QLabel("Ŀ���ļ�����"));
    m_outputFileNameEdit = new QLineEdit();
    connect(m_outputFileNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_outputFileName = text; });
    fileNameRow->addWidget(m_outputFileNameEdit);
    layout->addLayout(fileNameRow);

    connect(browseButton, &QPushButton::clicked,
            this, &MacaoImportNode::onImageBrowseClicked);

    return widget;
}

void MacaoImportNode::executeImport()
{
    if (executionState() == ExecutionState::Running)
        return;

    m_imagePath = m_imageEdit->text().trimmed();
    if (m_imagePath.isEmpty())
    {
        onError("��ѡ��һ�� Macao ͼ���ļ���");
        return;
    }

    if (!QFileInfo::exists(m_imagePath))
    {
        onError("Macao ͼ���ļ������ڣ�" + m_imagePath);
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

    connect(this, &MacaoImportNode::startMacaoImport,
            m_workerThread, &MyThread::import_Macao);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &MacaoImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &MacaoImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &MacaoImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &MacaoImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startMacaoImport(
        m_imagePath,
        projectPath(),
        getOutputNodeName(),
        m_outputFileName,
        projectName(),
        projectModel()
    );
}

QString MacaoImportNode::getImportedFilePath() const
{
    if (!m_importedFilePath.isEmpty())
        return m_importedFilePath;

    return QString("%1/%2/%3.h5")
        .arg(projectPath())
        .arg(getOutputNodeName())
        .arg(m_outputFileName);
}

QString MacaoImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
        return "Macao_Import";

    return name;
}

void MacaoImportNode::onImageBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        _widget,
        "���� Macao ����",
        QFileInfo(m_imagePath).absolutePath(),
        "Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff)"
    );

    if (filePath.isEmpty())
        return;

    m_imageEdit->setText(filePath);

    if (m_outputFileNameEdit->text().trimmed().isEmpty())
        m_outputFileNameEdit->setText(QFileInfo(filePath).baseName());
}

void MacaoImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void MacaoImportNode::onImportFinished()
{
    m_importedFilePath = QString("%1/%2/%3.h5")
        .arg(projectPath())
        .arg(getOutputNodeName())
        .arg(m_outputFileName);

    ImportNodeBase::onImportFinished();

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

void MacaoImportNode::onThreadError(const QString& error)
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

void MacaoImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void MacaoImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

QJsonObject MacaoImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["imagePath"] = m_imagePath;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : QString();
    json["outputFileName"] = m_outputFileName;
    return json;
}

void MacaoImportNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);
    m_imagePath = json["imagePath"].toString();
    m_outputFileName = json["outputFileName"].toString();

    if (m_imageEdit) m_imageEdit->setText(m_imagePath);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(json["outputNodeName"].toString("Macao_Import"));
    if (m_outputFileNameEdit) m_outputFileNameEdit->setText(m_outputFileName);
}

} // namespace QtNodes
