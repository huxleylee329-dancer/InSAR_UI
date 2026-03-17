#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "TSXImportNode.h"
#include <QFileInfo>

namespace QtNodes {

TSXImportNode::TSXImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_xmlEdit(nullptr)
    , m_polarizationCombo(nullptr)
    , m_projectCombo(nullptr)
    , m_progressBar(nullptr)
    , m_xmlPath()
    , m_importedFilePath()
    , m_outputFileName()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

TSXImportNode::~TSXImportNode()
{
    // Clean up worker thread
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

    // Note: m_widget is owned by QtNodes QGraphicsProxyWidget, do not delete here
}

QWidget* TSXImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // XML file row: Label:LineEdit:Button
    auto* xmlRow = new QHBoxLayout();
    xmlRow->addWidget(new QLabel("TSX/TDX图像（.xml）："));
    m_xmlEdit = new QLineEdit();
    QPushButton* xmlBrowse = new QPushButton("浏览...");
    xmlRow->addWidget(m_xmlEdit);
    xmlRow->addWidget(xmlBrowse);
    layout->addLayout(xmlRow);

    // Target project row [3:7]
    auto* projectRow = new QHBoxLayout();
    projectRow->setStretch(0, 3);
    projectRow->setStretch(1, 7);
    projectRow->addWidget(new QLabel("目标工程："));
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    if (!projectName().isEmpty())
    {
        m_projectCombo->addItem(projectName());
    }
    projectRow->addWidget(m_projectCombo);
    layout->addLayout(projectRow);

    // Target node row [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    nodeRow->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeRow);

    // Output filename row [3:7]
    auto* filenameRow = new QHBoxLayout();
    filenameRow->setStretch(0, 3);
    filenameRow->setStretch(1, 7);
    filenameRow->addWidget(new QLabel("目标文件名："));
    m_outputFileNameEdit = new QLineEdit();
    filenameRow->addWidget(m_outputFileNameEdit);
    layout->addLayout(filenameRow);

    // Polarization row [3:7]
    auto* polRow = new QHBoxLayout();
    polRow->setStretch(0, 3);
    polRow->setStretch(1, 7);
    polRow->addWidget(new QLabel("极化方式："));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("HH");
    m_polarizationCombo->addItem("VV");
    polRow->addWidget(m_polarizationCombo);
    layout->addLayout(polRow);

    // Progress bar row
    auto* progressRow = new QHBoxLayout();
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    progressRow->addWidget(m_progressBar);
    layout->addLayout(progressRow);

    // Connect signals
    connect(xmlBrowse, &QPushButton::clicked, this, &TSXImportNode::onXmlBrowseClicked);

    return widget;
}

void TSXImportNode::executeImport()
{
    if (m_isProcessing)
        return;

    m_xmlPath = m_xmlEdit->text().trimmed();
    if (m_xmlPath.isEmpty())
    {
        onError("请选择一个 XML 文件。");
        return;
    }

    if (!QFileInfo::exists(m_xmlPath))
    {
        onError("XML 文件不存在：" + m_xmlPath);
        return;
    }

    // Get output filename from edit box, or generate if empty
    m_outputFileName = m_outputFileNameEdit->text().trimmed();
    if (m_outputFileName.isEmpty())
    {
        m_outputFileName = generateOutputFileName();
        if (m_outputFileName.isEmpty())
        {
            onError("无法从 XML 文件生成输出文件名。");
            return;
        }
        m_outputFileNameEdit->setText(m_outputFileName);
    }

    if (m_outputFileName.isEmpty())
    {
        onError("无法从 XML 文件生成输出文件名。");
        return;
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &TSXImportNode::startTSXImport,
            m_workerThread, &MyThread::import_TSX);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &TSXImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &TSXImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &TSXImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &TSXImportNode::onModelUpdated);

    m_thread->start();

    QString polarization = m_polarizationCombo->currentText();
    QString outputNodeName = getOutputNodeName();

    Q_EMIT startTSXImport(
        polarization,
        m_xmlPath,
        projectPath(),
        outputNodeName,
        m_outputFileName,
        projectName(),
        projectModel()
    );
}

QString TSXImportNode::getImportedFilePath() const
{
    if (m_importedFilePath.isEmpty())
    {
        QString outputNodeName = getOutputNodeName();
        return QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(m_outputFileName);
    }
    return m_importedFilePath;
}

QString TSXImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return generateOutputFileName();
    }
    return name;
}

QString TSXImportNode::generateOutputFileName() const
{
    QFileInfo fileInfo(m_xmlPath);
    QString baseName = fileInfo.baseName();

    // Find the last "T" in the filename and extract 8 characters before it
    int pos = baseName.lastIndexOf('T');
    if (pos >= 8)
    {
        return baseName.mid(pos - 8, 8);
    }

    return QString();
}

void TSXImportNode::onXmlBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        m_widget,
        "导入 TerraSAR-X/TanDEM-X 数据",
        QFileInfo(m_xmlPath).absolutePath(),
        "XML 文件 (*.xml)"
    );

    if (!filePath.isEmpty())
    {
        m_xmlEdit->setText(filePath);

        QString autoName = generateOutputFileName();
        if (!autoName.isEmpty() && m_outputFileNameEdit->text().isEmpty())
        {
            m_outputFileNameEdit->setText(autoName);
        }
    }
}

void TSXImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    m_progressBar->setValue(progress);
}

void TSXImportNode::onImportFinished()
{
    QString outputPath = projectPath() + "/" + getOutputNodeName() + "/" + m_outputFileName + ".h5";
    m_importedFilePath = outputPath;

    ImportNodeBase::onImportFinished();

    m_progressBar->setValue(100);

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

void TSXImportNode::onThreadError(const QString& error)
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

void TSXImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
