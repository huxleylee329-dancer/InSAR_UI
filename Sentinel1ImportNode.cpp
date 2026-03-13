#include "Sentinel1ImportNode.h"
#include <QFileInfo>
#include <QRegularExpression>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

namespace QtNodes {

Sentinel1ImportNode::Sentinel1ImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_projectCombo(nullptr)
    , m_manifestEdit(nullptr)
    , m_podEdit(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_progressBar(nullptr)
    , m_manifestPath()
    , m_podPath()
    , m_importedFilePath()
    , m_outputFileName()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

Sentinel1ImportNode::~Sentinel1ImportNode()
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

QWidget* Sentinel1ImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 哨兵图像文件（.safe） + 浏览按钮 [3:5:2]
    auto* manifestLayout = new QHBoxLayout();
    manifestLayout->setStretch(0, 3);
    manifestLayout->setStretch(1, 5);
    manifestLayout->setStretch(2, 2);
    QLabel* manifestLabel = new QLabel("哨兵图像文件（.safe）");
    manifestLayout->addWidget(manifestLabel);
    m_manifestEdit = new QLineEdit();
    m_manifestEdit->setPlaceholderText("选择 .safe 目录中的 manifest 文件");
    manifestLayout->addWidget(m_manifestEdit);
    QPushButton* manifestBrowse = new QPushButton("浏览...");
    manifestLayout->addWidget(manifestBrowse);
    layout->addLayout(manifestLayout);

    // 精轨文件（可空缺） + 浏览按钮 [3:5:2]
    auto* podLayout = new QHBoxLayout();
    podLayout->setStretch(0, 3);
    podLayout->setStretch(1, 5);
    podLayout->setStretch(2, 2);
    QLabel* podLabel = new QLabel("精轨文件（可空缺）");
    podLayout->addWidget(podLabel);
    m_podEdit = new QLineEdit();
    m_podEdit->setPlaceholderText("可选，留空则不使用精轨文件");
    podLayout->addWidget(m_podEdit);
    QPushButton* podBrowse = new QPushButton("浏览...");
    podLayout->addWidget(podBrowse);
    layout->addLayout(podLayout);

    // 子带选择（subswath） [3:7]
    auto* subswathLayout = new QHBoxLayout();
    subswathLayout->setStretch(0, 3);
    subswathLayout->setStretch(1, 7);
    QLabel* subswathLabel = new QLabel("子带选择（subswath）");
    subswathLayout->addWidget(subswathLabel);
    m_subswathCombo = new QComboBox();
    m_subswathCombo->addItem("iw1");
    m_subswathCombo->addItem("iw2");
    m_subswathCombo->addItem("iw3");
    subswathLayout->addWidget(m_subswathCombo);
    layout->addLayout(subswathLayout);

    // 极化方式选择 [3:7]
    auto* polLayout = new QHBoxLayout();
    polLayout->setStretch(0, 3);
    polLayout->setStretch(1, 7);
    QLabel* polLabel = new QLabel("极化方式选择");
    polLayout->addWidget(polLabel);
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("vv");
    m_polarizationCombo->addItem("vh");
    polLayout->addWidget(m_polarizationCombo);
    layout->addLayout(polLayout);

    // 目标工程 [3:7]
    auto* projectLayout = new QHBoxLayout();
    projectLayout->setStretch(0, 3);
    projectLayout->setStretch(1, 7);
    QLabel* projectLabel = new QLabel("目标工程");
    projectLayout->addWidget(projectLabel);
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    m_projectCombo->setPlaceholderText("当前打开的项目");
    projectLayout->addWidget(m_projectCombo);
    layout->addLayout(projectLayout);

    // 目标节点名 [3:7]
    auto* nodeNameLayout = new QHBoxLayout();
    nodeNameLayout->setStretch(0, 3);
    nodeNameLayout->setStretch(1, 7);
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("自动生成或手动输入");
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    // 目标文件名 [3:7]
    auto* fileNameLayout = new QHBoxLayout();
    fileNameLayout->setStretch(0, 3);
    fileNameLayout->setStretch(1, 7);
    QLabel* fileNameLabel = new QLabel("目标文件名");
    fileNameLayout->addWidget(fileNameLabel);
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setPlaceholderText("自动生成或手动输入");
    fileNameLayout->addWidget(m_outputFileNameEdit);
    layout->addLayout(fileNameLayout);

    // Separator
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // Progress bar
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // Connect signals
    connect(manifestBrowse, &QPushButton::clicked, this, &Sentinel1ImportNode::onManifestBrowseClicked);
    connect(podBrowse, &QPushButton::clicked, this, &Sentinel1ImportNode::onPodBrowseClicked);
    connect(m_manifestEdit, &QLineEdit::textChanged, this, [this]() {
        QString autoName = generateOutputFileName();
        if (!autoName.isEmpty() && m_outputNodeNameEdit->text().isEmpty()) {
            m_outputNodeNameEdit->setText(autoName);
        }
        if (!autoName.isEmpty() && m_outputFileNameEdit->text().isEmpty()) {
            m_outputFileNameEdit->setText(autoName);
        }
    });

    // Set project name if available
    QString currentProject = projectName();
    if (!currentProject.isEmpty()) {
        m_projectCombo->addItem(currentProject);
    }

    return widget;
}

void Sentinel1ImportNode::executeImport()
{
    if (m_isProcessing)
        return;

    m_manifestPath = m_manifestEdit->text().trimmed();
    if (m_manifestPath.isEmpty())
    {
        onError("请选择清单文件");
        return;
    }

    if (!QFileInfo::exists(m_manifestPath))
    {
        onError("清单文件不存在：" + m_manifestPath);
        return;
    }

    m_podPath = m_podEdit->text().trimmed();
    if (!m_podPath.isEmpty() && !QFileInfo::exists(m_podPath))
    {
        onError("精轨文件不存在：" + m_podPath);
        return;
    }

    m_outputFileName = getOutputFileName();
    if (m_outputFileName.isEmpty())
    {
        onError("无法从清单文件生成输出文件名");
        return;
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &Sentinel1ImportNode::startImport,
            m_workerThread, &MyThread::import_sentinel);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &Sentinel1ImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &Sentinel1ImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &Sentinel1ImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &Sentinel1ImportNode::onModelUpdated);

    m_thread->start();

    QString subswath = m_subswathCombo->currentText();
    QString polarization = m_polarizationCombo->currentText();
    QString outputNodeName = getOutputNodeName();

    Q_EMIT startImport(
        m_podPath,
        m_manifestPath,
        subswath,
        polarization,
        projectPath(),
        outputNodeName,
        m_outputFileName,
        projectName(),
        projectModel()
    );
}

QString Sentinel1ImportNode::getImportedFilePath() const
{
    if (m_importedFilePath.isEmpty())
    {
        QString outputNodeName = getOutputNodeName();
        QString subswath = m_subswathCombo->currentText();
        QString pol = m_polarizationCombo->currentText();
        return QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(subswath + "_" + pol);
    }
    return m_importedFilePath;
}

QString Sentinel1ImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return generateOutputFileName();
    }
    return name;
}

QString Sentinel1ImportNode::getOutputFileName() const
{
    QString fileName = m_outputFileNameEdit->text().trimmed();
    if (fileName.isEmpty())
    {
        return generateOutputFileName();
    }
    return fileName;
}

QString Sentinel1ImportNode::generateOutputFileName() const
{
    QFileInfo fileInfo(m_manifestPath);
    QString fileName = fileInfo.fileName();

    QRegularExpression dateRegex(R"(\d{8})");
    QRegularExpressionMatch match = dateRegex.match(fileName);
    if (match.hasMatch())
    {
        QString date = match.captured(0);
        QString subswath = m_subswathCombo->currentText();
        QString pol = m_polarizationCombo->currentText();
        return QString("%1_%2%3").arg(date).arg(subswath).arg(pol);
    }

    return QString();
}

void Sentinel1ImportNode::onManifestBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        m_widget,
        "选择哨兵一号清单文件",
        QFileInfo(m_manifestPath).absolutePath(),
        "清单文件 (*.manifest);;所有文件 (*)"
    );

    if (!filePath.isEmpty())
    {
        m_manifestEdit->setText(filePath);

        QString autoName = generateOutputFileName();
        if (!autoName.isEmpty() && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(autoName);
        }
        if (!autoName.isEmpty() && m_outputFileNameEdit->text().isEmpty())
        {
            m_outputFileNameEdit->setText(autoName);
        }
    }
}

void Sentinel1ImportNode::onPodBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        m_widget,
        "选择精轨文件",
        QFileInfo(m_podPath).absolutePath(),
        "精轨文件 (*.EOF *.eofs);;所有文件 (*)"
    );

    if (!filePath.isEmpty())
    {
        m_podEdit->setText(filePath);
    }
}

void Sentinel1ImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    m_progressBar->setValue(progress);
}

void Sentinel1ImportNode::onImportFinished()
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

void Sentinel1ImportNode::onThreadError(const QString& error)
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

void Sentinel1ImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

} // namespace QtNodes
