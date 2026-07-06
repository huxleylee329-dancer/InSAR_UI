#include "ExportKMLNode.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InSARLogManager.h"
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QRegularExpression>

namespace QtNodes {

ExportKMLNode::ExportKMLNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_outputPathEdit(nullptr)
    , m_browseBtn(nullptr)
    , m_fileNameEdit(nullptr)
    , m_resultLabel(nullptr)
    , m_fileName(QStringLiteral("sbas_deformation"))
    , m_worker(nullptr)
    , m_thread(nullptr)
{
}

ExportKMLNode::~ExportKMLNode()
{
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
    }
}

unsigned int ExportKMLNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 1; // Port 0: KML file path output
}

NodeDataType ExportKMLNode::dataType(PortType portType, PortIndex portIndex) const
{
    return ImportedFileData().type();
}

bool ExportKMLNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return true;
}

QString ExportKMLNode::portCaption(PortType portType, PortIndex portIndex) const
{
    return tr("成果 *");
}

std::shared_ptr<NodeData> ExportKMLNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    return m_outputData;
}

void ExportKMLNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData && !m_inputData->filePaths().isEmpty())
    {
        QFileInfo fi(m_inputData->filePath());
        QString upstreamNode = fi.absoluteDir().dirName();
        if (m_inputNodeLabel)
        {
            m_inputNodeLabel->setText(upstreamNode);
        }
        
        // Auto-fill output path default if empty
        if (m_outputPath.isEmpty())
        {
            m_outputPath = projectPath() + "/Export";
            if (m_outputPathEdit)
            {
                m_outputPathEdit->setText(m_outputPath);
            }
        }
    }
    else
    {
        if (m_inputNodeLabel)
        {
            m_inputNodeLabel->setText(QStringLiteral("未连接"));
        }
    }
    
    updateLabels();
}

::QWidget* ExportKMLNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void ExportKMLNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
    if (_widget)
    {
        bool isManual = (mode == ExecutionMode::Manual);
        m_outputPathEdit->setEnabled(isManual);
        m_browseBtn->setEnabled(isManual);
        m_fileNameEdit->setEnabled(isManual);
    }
}

void ExportKMLNode::createWidget()
{
    _widget = new ::QWidget();
    _widget->setFixedWidth(300);
    _widget->setStyleSheet("background-color: transparent;");

    QVBoxLayout* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(6);

    // Form row helper
    auto addFormRow = [&](const QString& labelText, ::QWidget* fieldWidget) {
        QHBoxLayout* row = new QHBoxLayout();
        QLabel* label = new QLabel(labelText, _widget);
        label->setFixedWidth(80);
        label->setStyleSheet("color: #E0E0E0; font-size: 11px;");
        row->addWidget(label);
        row->addWidget(fieldWidget);
        layout->addLayout(row);
    };

    // Input node display
    m_inputNodeLabel = new QLabel(QStringLiteral("未连接"), _widget);
    m_inputNodeLabel->setStyleSheet("color: #888888; font-size: 11px;");
    addFormRow(QStringLiteral("输入节点:"), m_inputNodeLabel);

    // Output directory selection row
    ::QWidget* pathWidget = new ::QWidget(_widget);
    QHBoxLayout* pathLayout = new QHBoxLayout(pathWidget);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(4);

    m_outputPathEdit = new QLineEdit(pathWidget);
    m_outputPathEdit->setPlaceholderText(QStringLiteral("存储路径"));
    m_outputPathEdit->setStyleSheet("color: white; background-color: #1F2937; border: 1px solid #4B5563; border-radius: 4px; padding: 2px; font-size: 11px;");
    connect(m_outputPathEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputPath = text;
    });

    m_browseBtn = new QPushButton(QStringLiteral("浏览"), pathWidget);
    m_browseBtn->setFixedWidth(50);
    m_browseBtn->setStyleSheet("QPushButton { background-color: #374151; color: white; border-radius: 4px; padding: 2px; font-size: 11px; }"
                               "QPushButton:hover { background-color: #4B5563; }");
    connect(m_browseBtn, &QPushButton::clicked, this, &ExportKMLNode::onBrowseClicked);

    pathLayout->addWidget(m_outputPathEdit);
    pathLayout->addWidget(m_browseBtn);
    addFormRow(QStringLiteral("导出路径:"), pathWidget);

    // Export File Name
    m_fileNameEdit = new QLineEdit(_widget);
    m_fileNameEdit->setText(m_fileName);
    m_fileNameEdit->setStyleSheet("color: white; background-color: #1F2937; border: 1px solid #4B5563; border-radius: 4px; padding: 2px; font-size: 11px;");
    connect(m_fileNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_fileName = text;
    });
    addFormRow(QStringLiteral("文件名:"), m_fileNameEdit);

    // Result Label
    m_resultLabel = new QLabel(_widget);
    m_resultLabel->setStyleSheet("color: #10B981; font-size: 10px;");
    m_resultLabel->setWordWrap(true);
    layout->addWidget(m_resultLabel);

    if (!m_outputPath.isEmpty())
    {
        m_outputPathEdit->setText(m_outputPath);
    }
    updateLabels();
}

void ExportKMLNode::onBrowseClicked()
{
    QString dir = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("选择导出路径"), m_outputPath);
    if (!dir.isEmpty())
    {
        m_outputPath = dir;
        m_outputPathEdit->setText(dir);
    }
}

void ExportKMLNode::updateLabels()
{
    updateWidgetSize();
}

void ExportKMLNode::updateWidgetSize()
{
    if (_widget)
    {
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool ExportKMLNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;
    if (m_outputPath.isEmpty())
        return false;
    if (m_fileName.isEmpty())
        return false;
        
    // Character checks from original Export_KML dialog
    bool bFlag = m_outputPath.contains(QRegularExpression("^[\\n\\w:.\\()-/]+$"));
    if (!bFlag) return false;
    bFlag = m_fileName.contains(QRegularExpression("^\\w+$"));
    if (!bFlag) return false;

    return true;
}

bool ExportKMLNode::prepareToStart()
{
    if (!validateInputs())
    {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
        setState(ExecutionState::Error);
        return false;
    }

    if (isAutoTriggered()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        return true;
    }

    QString kmlPath = m_outputPath + "/" + m_fileName + ".kml";
    QStringList pathsToCheck = QStringList() << kmlPath;

    m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
        NodeUtils::getProjectContext(_widget),
        m_fileName,
        pathsToCheck,
        _widget
    );

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
        return false;
    }

    return true;
}

void ExportKMLNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        validateAndRestoreOutput();
        return;
    }

    executeProcessing();
}

void ExportKMLNode::executeProcessing()
{
    InSARLogManager::LogInfo("ExportKMLNode", "executeProcessing started.");
    // Ensure any previous execution is stopped
    stopExecution();
    m_resultLabel->setText(QStringLiteral("正在开始导出..."));

    QDir dir(m_outputPath);
    if (!dir.exists())
    {
        dir.mkpath(m_outputPath);
    }

    m_worker = new ExportKMLWorker();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    connect(this, &ExportKMLNode::startProcess, m_worker, [this]() {
        m_worker->exportKML(
            m_inputData->filePath(),
            m_outputPath,
            m_fileName
        );
    });

    connect(m_worker, &ExportKMLWorker::updateProcess, this, &ExportKMLNode::onProgressUpdate);
    connect(m_worker, &ExportKMLWorker::endProcess, this, &ExportKMLNode::onProcessingFinished);
    connect(m_worker, &ExportKMLWorker::errorProcess, this, &ExportKMLNode::onError);

    // Update state and progress before starting
    setState(ExecutionState::Running);
    setProgress(0);
    m_thread->start();
    Q_EMIT startProcess();
}

void ExportKMLNode::stopExecution()
{
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

void ExportKMLNode::processAutomatically()
{
    if (prepareToStart()) {
        execute();
    } else {
        setState(ExecutionState::Idle);
    }
}

void ExportKMLNode::onProgressUpdate(int progress, const QString& message)
{
    m_resultLabel->setText(QString("%1%: %2").arg(progress).arg(message));
}

void ExportKMLNode::onError(const QString& error)
{
    InSARLogManager::LogError("ExportKMLNode", "Error during KML export: " + error);
    m_resultLabel->setText(QStringLiteral("失败: ") + error);
    setState(ExecutionState::Error);
    finishExecution();
    stopExecution();
}

void ExportKMLNode::onProcessingFinished()
{
    InSARLogManager::LogInfo("ExportKMLNode", "executeProcessing completed.");
    m_resultLabel->setText(QStringLiteral("导出完成！"));

    QString kmlPath = m_outputPath + "/" + m_fileName + ".kml";
    m_outputData = std::make_shared<ImportedFileData>(kmlPath, m_fileName);
    setState(ExecutionState::Running);
    finishExecution();
    Q_EMIT dataUpdated(0);
    stopExecution();
}

bool ExportKMLNode::validateAndRestoreOutput()
{
    QString kmlPath = m_outputPath + "/" + m_fileName + ".kml";
    if (QFile::exists(kmlPath))
    {
        m_outputData = std::make_shared<ImportedFileData>(kmlPath, m_fileName);
        m_resultLabel->setText(QStringLiteral("检测到已有导出文件，已恢复。"));
        setState(ExecutionState::Completed);
        Q_EMIT dataUpdated(0);
        return true;
    }
    return false;
}

QJsonObject ExportKMLNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["outputPath"] = m_outputPath;
    json["fileName"] = m_fileName;
    return json;
}

void ExportKMLNode::load(QJsonObject const& json)
{
    ExecutableNodeDelegateModel::load(json);
    m_outputPath = json["outputPath"].toString();
    m_fileName = json["fileName"].toString();

    if (m_outputPathEdit)
    {
        m_outputPathEdit->setText(m_outputPath);
    }
    if (m_fileNameEdit)
    {
        m_fileNameEdit->setText(m_fileName);
    }
    updateLabels();
}

QString ExportKMLNode::projectPath() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface)
    {
        return iface->projectPath();
    }
    return QString();
}

} // namespace QtNodes
