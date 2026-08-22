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
#include <QMap>
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

ProductInputContract ExportKMLNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("export_kml.input.geocoded_raster");
    contract.allowedProductTypes = QStringList() << QStringLiteral("geocoded_raster");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract ExportKMLNode::productOutputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductOutputContract contract;
    contract.semanticId = QStringLiteral("export_kml.output.kml_export");
    contract.publishedProductTypes = QStringList() << QStringLiteral("kml_export");
    return contract;
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
    Q_UNUSED(portIndex);
    if (portType == PortType::In)
    {
        return tr("地理编码栅格");
    }

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
    ExecutableNodeDelegateModel::setInData(data, port);
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
        if (m_outputPathEdit)
        {
            m_outputPathEdit->setEnabled(isManual);
        }
        if (m_browseBtn)
        {
            m_browseBtn->setEnabled(isManual);
        }
        if (m_fileNameEdit)
        {
            m_fileNameEdit->setEnabled(isManual);
        }
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
        label->setStyleSheet("font-size: 11px;");
        row->addWidget(label);
        row->addWidget(fieldWidget);
        layout->addLayout(row);
    };

    // Input node display
    m_inputNodeLabel = new QLabel(QStringLiteral("未连接"), _widget);
    m_inputNodeLabel->setStyleSheet("font-size: 11px;");
    addFormRow(QStringLiteral("输入节点:"), m_inputNodeLabel);

    // Output directory selection row
    ::QWidget* pathWidget = new ::QWidget(_widget);
    QHBoxLayout* pathLayout = new QHBoxLayout(pathWidget);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(4);

    m_outputPathEdit = new QLineEdit(pathWidget);
    m_outputPathEdit->setPlaceholderText(QStringLiteral("存储路径"));
    m_outputPathEdit->setStyleSheet("font-size: 11px;");
    connect(m_outputPathEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputPath = text;
    });

    m_browseBtn = new QPushButton(QStringLiteral("浏览"), pathWidget);
    m_browseBtn->setFixedWidth(50);
    m_browseBtn->setStyleSheet("font-size: 11px;");
    connect(m_browseBtn, &QPushButton::clicked, this, &ExportKMLNode::onBrowseClicked);

    pathLayout->addWidget(m_outputPathEdit);
    pathLayout->addWidget(m_browseBtn);
    addFormRow(QStringLiteral("导出路径:"), pathWidget);

    // Export File Name
    m_fileNameEdit = new QLineEdit(_widget);
    m_fileNameEdit->setText(m_fileName);
    m_fileNameEdit->setStyleSheet("font-size: 11px;");
    connect(m_fileNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_fileName = text;
    });
    addFormRow(QStringLiteral("文件名:"), m_fileNameEdit);

    // Result Label
    m_resultLabel = new QLabel(_widget);
    m_resultLabel->setStyleSheet("font-size: 10px;");
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

    const ProductValidationResult inputValidation = validateBoundDescriptor(
        productInputContract(0), m_inputData->productDescriptor());
    if (!inputValidation.accepted) {
        setLastErrorMessage(inputValidation.reason);
        setState(ExecutionState::Error);
        return false;
    }
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_inputData->filePaths(),
                                         m_inputData->physicalProductDescriptor(), &identityError)) {
        setLastErrorMessage(identityError);
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
        if (!validateAndRestoreOutput()) {
            onError(QStringLiteral("Existing KML export is unavailable for semantic restoration."));
        }
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
    connect(m_worker, &ExportKMLWorker::cancelled, this, &ExportKMLNode::onCancelled);
    connect(m_worker, &ExportKMLWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &ExportKMLWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &ExportKMLWorker::cancelled, m_thread, &QThread::quit);

    // Update state and progress before starting
    setState(ExecutionState::Running);
    setProgress(0);
    deferAutomaticCompletion();
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

void ExportKMLNode::cleanUpThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_worker = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

void ExportKMLNode::processAutomatically()
{
    if (prepareToStart()) {
        execute();
    } else if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        setState(ExecutionState::Idle);
    }
}

void ExportKMLNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    m_resultLabel->setText(QString("%1%: %2").arg(progress).arg(message));
}

void ExportKMLNode::onError(const QString& error)
{
    cleanUpThreadAndWorker();
    m_outputData.reset();
    setOutputData(0, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("ExportKMLNode", "Error during KML export: " + error);
    m_resultLabel->setText(QStringLiteral("失败: ") + error);
    setState(ExecutionState::Error);
    finishExecution();
}

void ExportKMLNode::onProcessingFinished()
{
    cleanUpThreadAndWorker();
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogInfo("ExportKMLNode", "executeProcessing completed.");
    m_resultLabel->setText(QStringLiteral("导出完成！"));

    QString kmlPath = m_outputPath + "/" + m_fileName + ".kml";
    if (!QFileInfo(kmlPath).isFile()) {
        onError(QStringLiteral("KML export worker completed without producing the expected file."));
        return;
    }
    m_outputData = std::make_shared<ImportedFileData>(kmlPath, m_fileName);
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), name());
    provenance.insert(QStringLiteral("output_port"), productOutputContract(0).semanticId);
    m_outputData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("kml_export"),
        productOutputContract(0).schemaId, productOutputContract(0).schemaVersion,
        productOutputContract(0).publishedState, name(), provenance));
    setOutputData(0, m_outputData);
    setState(ExecutionState::Running);
    finishExecution();
}

void ExportKMLNode::onCancelled()
{
    cleanUpThreadAndWorker();
    m_outputData.reset();
    setOutputData(0, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_resultLabel->setText(QStringLiteral("已取消"));
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

bool ExportKMLNode::validateAndRestoreOutput()
{
    QString kmlPath = m_outputPath + "/" + m_fileName + ".kml";
    if (QFile::exists(kmlPath))
    {
        m_outputData = std::make_shared<ImportedFileData>(kmlPath, m_fileName);
        QMap<QString, QString> provenance;
        provenance.insert(QStringLiteral("producer"), name());
        provenance.insert(QStringLiteral("output_port"), productOutputContract(0).semanticId);
        m_outputData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("kml_export"),
            productOutputContract(0).schemaId, productOutputContract(0).schemaVersion,
            productOutputContract(0).publishedState, name(), provenance));
        setOutputData(0, m_outputData);
        if (m_resultLabel) {
            m_resultLabel->setText(QStringLiteral("检测到已有导出文件，已恢复。"));
        }
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

QStringList ExportKMLNode::outputArtifactPathsForPaste(QJsonObject const& json) const
{
    const QString outputPath = json.value(QStringLiteral("outputPath")).toString().trimmed();
    const QString fileName = json.value(QStringLiteral("fileName")).toString().trimmed();
    if (outputPath.isEmpty() || fileName.isEmpty()) {
        return QStringList();
    }

    return QStringList() << QDir(outputPath).absoluteFilePath(
        fileName + QStringLiteral(".kml"));
}

void ExportKMLNode::prepareForPaste(QJsonObject& json,
                                    PasteContext& context) const
{
    ExecutableNodeDelegateModel::prepareForPaste(json, context);

    const QString outputPath = json.value(QStringLiteral("outputPath")).toString().trimmed();
    const QString sourceName = json.value(QStringLiteral("fileName")).toString().trimmed();
    if (outputPath.isEmpty() || sourceName.isEmpty()) {
        return;
    }

    const QDir outputDirectory(outputPath);
    const auto artifactReservationKey = [&outputDirectory](const QString& fileName) {
        return QDir::cleanPath(outputDirectory.absoluteFilePath(
            fileName + QStringLiteral(".kml"))).toCaseFolded();
    };
    const auto existsCaseInsensitive = [&outputDirectory](const QString& fileName) {
        const QString candidateFileName = fileName + QStringLiteral(".kml");
        if (QFileInfo::exists(outputDirectory.absoluteFilePath(candidateFileName))) {
            return true;
        }

        const QString caseFoldedCandidate = candidateFileName.toCaseFolded();
        const QStringList existingFiles = outputDirectory.entryList(QDir::Files | QDir::NoSymLinks);
        for (const QString& existingFile : existingFiles) {
            if (existingFile.toCaseFolded() == caseFoldedCandidate) {
                return true;
            }
        }
        return false;
    };
    const auto conflicts = [&context, &artifactReservationKey, &existsCaseInsensitive](
        const QString& candidate) {
        return context.reservedOutputArtifactPaths.contains(artifactReservationKey(candidate)) ||
            existsCaseInsensitive(candidate);
    };

    QString candidate = sourceName + QStringLiteral("_copy");
    int suffix = 2;
    while (conflicts(candidate)) {
        candidate = sourceName + QStringLiteral("_copy_%1").arg(suffix++);
    }

    json.insert(QStringLiteral("fileName"), candidate);
    context.reservedOutputArtifactPaths.insert(artifactReservationKey(candidate));
}

void ExportKMLNode::load(QJsonObject const& json)
{
    m_outputPath = json["outputPath"].toString();
    m_fileName = json["fileName"].toString();
    ExecutableNodeDelegateModel::load(json);

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

