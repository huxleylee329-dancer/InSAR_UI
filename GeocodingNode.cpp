#include "GeocodingNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include "tinyxml.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QTimer>
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

GeocodingNode::GeocodingNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_typeCombo(nullptr)
    , m_multiRgLabel(nullptr)
    , m_multiRgSpin(nullptr)
    , m_multiAzLabel(nullptr)
    , m_multiAzSpin(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_type(1)       // default: 干涉产品
    , m_multiRg(1)    // default: 1
    , m_multiAz(1)    // default: 1
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    qRegisterMetaType<GeocodingFileResult>("GeocodingFileResult");
    setExecutionMode(ExecutionMode::Automatic);
}

GeocodingNode::~GeocodingNode()
{
    stopExecution();
}

unsigned int GeocodingNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType GeocodingNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"dem_file", "DEM File"};
    }
    else
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool GeocodingNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString GeocodingNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0)
            return QStringLiteral("待地理编码产品");
        else
            return QStringLiteral("辅助地形 DEM");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool GeocodingNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void GeocodingNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

        if (!m_inputData || m_inputData->filePaths().isEmpty()) {
            m_outputData.reset();
            m_imageInfoData.reset();
            setOutputData(0, nullptr);
            setOutputData(1, nullptr);
        } else {
            // Automatically determine type based on the level of the input node
            QString srcNode = m_inputData->nodeName();
            QStandardItemModel* model = projectModel();
            if (model) {
                QList<QStandardItem*> projects = model->findItems(projectName());
                QStandardItem* project = projects.isEmpty() ? nullptr : projects.first();
                if (project) {
                    for (int i = 0; i < project->rowCount(); i++) {
                        if (project->child(i, 0)->text() == srcNode) {
                            QString level = project->child(i, 1)->text();
                            if (level.contains("complex") || level.contains("amplitude")) {
                                m_type = 2; // SAR图像
                            } else {
                                m_type = 1; // 干涉产品
                            }
                            if (m_typeCombo) {
                                m_typeCombo->setCurrentIndex(m_type - 1);
                                onTypeChanged(m_type - 1);
                            }
                            break;
                        }
                    }
                }
            }
        }

        if (m_inputData && m_outputNodeName.isEmpty()) {
            m_outputNodeName = generateDefaultOutputName();
            if (m_outputNodeNameEdit) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
        }
    } else if (port == 1) {
        m_demInputData = std::dynamic_pointer_cast<DEMFileData>(data);
        if (m_demInputData) {
            m_demPath = m_demInputData->filePath();
            if (m_demPathEdit) m_demPathEdit->setText(m_demPath);
        } else {
            if (!isRestoring()) {
                m_demPath.clear();
                if (m_demPathEdit) {
                    m_demPathEdit->clear();
                }
            }
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);
    updateParameterWidgetsEnableState();
}

std::shared_ptr<NodeData> GeocodingNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* GeocodingNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject GeocodingNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["type"] = m_type;
    modelJson["multiRg"] = m_multiRgSpin ? m_multiRgSpin->value() : m_multiRg;
    modelJson["multiAz"] = m_multiAzSpin ? m_multiAzSpin->value() : m_multiAz;
    modelJson[QStringLiteral("demPath")] = m_demPath;

    return modelJson;
}

void GeocodingNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vType = json["type"];
    if (!vType.isUndefined()) m_type = vType.toInt();

    QJsonValue vMultiRg = json["multiRg"];
    if (!vMultiRg.isUndefined()) m_multiRg = vMultiRg.toInt();

    QJsonValue vMultiAz = json["multiAz"];
    if (!vMultiAz.isUndefined()) m_multiAz = vMultiAz.toInt();

    QJsonValue vDemPath = json[QStringLiteral("demPath")];
    if (!vDemPath.isUndefined()) m_demPath = vDemPath.toString();

    // Restore the saved execution state and validate committed output before
    // the graph performs its post-restore readiness pass.
    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_typeCombo) {
        m_typeCombo->setCurrentIndex(m_type - 1);
        onTypeChanged(m_type - 1);
    }
    if (m_multiRgSpin) m_multiRgSpin->setValue(m_multiRg);
    if (m_multiAzSpin) m_multiAzSpin->setValue(m_multiAz);
}

void GeocodingNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void GeocodingNode::createWidget()
{
    _widget = new QWidget();
    _widget->setFixedWidth(300); // SOP Rule 2: prevent size inflation

    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Type Row
    auto* typeRow = new QHBoxLayout();
    auto* typeLabel = new QLabel(QStringLiteral("处理类型:"));
    typeLabel->setFixedWidth(80); // SOP Rule 10: Fixed Label Width
    m_typeCombo = new QComboBox();
    m_typeCombo->addItem(QStringLiteral("干涉产品"));
    m_typeCombo->addItem(QStringLiteral("SAR图像"));
    m_typeCombo->setCurrentIndex(m_type - 1);
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(m_typeCombo);
    layout->addLayout(typeRow);

    // Multi Range Row
    auto* rgRow = new QHBoxLayout();
    m_multiRgLabel = new QLabel(QStringLiteral("距离多视数:"));
    m_multiRgLabel->setFixedWidth(80);
    m_multiRgSpin = new QSpinBox();
    m_multiRgSpin->setRange(1, 100);
    m_multiRgSpin->setValue(m_multiRg);
    rgRow->addWidget(m_multiRgLabel);
    rgRow->addWidget(m_multiRgSpin);
    layout->addLayout(rgRow);

    // Multi Azimuth Row
    auto* azRow = new QHBoxLayout();
    m_multiAzLabel = new QLabel(QStringLiteral("方位多视数:"));
    m_multiAzLabel->setFixedWidth(80);
    m_multiAzSpin = new QSpinBox();
    m_multiAzSpin->setRange(1, 100);
    m_multiAzSpin->setValue(m_multiAz);
    azRow->addWidget(m_multiAzLabel);
    azRow->addWidget(m_multiAzSpin);
    layout->addLayout(azRow);

    // Output Name Row
    auto* outRow = new QHBoxLayout();
    auto* outLabel = new QLabel(QStringLiteral("目标节点名:"));
    outLabel->setFixedWidth(80);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入")); // SOP Rule 11
    m_outputNodeNameEdit->setText(m_outputNodeName);
    outRow->addWidget(outLabel);
    outRow->addWidget(m_outputNodeNameEdit);
    layout->addLayout(outRow);

    // DEM Path Row
    auto* demRow = new QHBoxLayout();
    m_demPathLabel = new QLabel(QStringLiteral("DEM路径:"));
    m_demPathLabel->setFixedWidth(80);
    m_demPathLabel->setStyleSheet("QLabel:disabled { color: #888888; }");
    
    if (m_demPath.isEmpty()) {
        auto* iface = NodeUtils::getProjectContext(nullptr);
        if (iface) {
            m_demPath = NodeUtils::getGlobalDemPath(iface);
        }
    }
    
    m_demPathEdit = new QLineEdit();
    m_demPathEdit->setObjectName("demPathEdit");
    m_demPathEdit->setText(m_demPath);
    m_demPathEdit->setPlaceholderText(QStringLiteral("选择DEM数据 (*.h5, *.tiff)..."));
    m_demPathEdit->setStyleSheet(
        "QLineEdit:disabled {"
        "  background-color: rgba(120, 120, 120, 0.1);"
        "  color: #888888;"
        "  border: 1px dashed rgba(148, 163, 184, 0.2);"
        "}"
    );
    connect(m_demPathEdit, &QLineEdit::editingFinished, this, [this]() {
        QString text = m_demPathEdit->text().trimmed();
        if (m_demPath != text) {
            m_demPath = text;
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_demPath, true);
            }
        }
    });

    m_demBrowseBtn = new QPushButton(QStringLiteral("浏览..."));
    m_demBrowseBtn->setStyleSheet(
        "QPushButton:disabled {"
        "  background-color: rgba(120, 120, 120, 0.1);"
        "  color: #888888;"
        "  border: 1px dashed rgba(148, 163, 184, 0.2);"
        "}"
    );
    connect(m_demBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择DEM数据"), "", "DEM Files (*.h5 *.tiff *.tif)");
        if (!file.isEmpty()) {
            m_demPath = file;
            if (m_demPathEdit) m_demPathEdit->setText(m_demPath);
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_demPath, true);
            }
        }
    });

    demRow->addWidget(m_demPathLabel);
    demRow->addWidget(m_demPathEdit);
    demRow->addWidget(m_demBrowseBtn);
    layout->addLayout(demRow);

    // Connections
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GeocodingNode::onTypeChanged);
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text;
    });

    onTypeChanged(m_type - 1);
}

void GeocodingNode::onTypeChanged(int index)
{
    m_type = index + 1;
    updateParameterWidgetsEnableState();
}

QString GeocodingNode::generateDefaultOutputName() const
{
    if (m_inputData && !m_inputData->nodeName().isEmpty()) {
        return m_inputData->nodeName() + "_geocoded";
    }
    return "Geocoded";
}

bool GeocodingNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;

    QString outputName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName.trimmed();
    if (outputName.isEmpty())
        return false;

    // Check if the output node name contains invalid characters
    bool bFlag = outputName.contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
        return false;

    return true;
}

bool GeocodingNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    QString dstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text().trimmed();

    QString savePath = projectPath();

    m_preparedType = m_typeCombo ? m_typeCombo->currentIndex() + 1 : m_type;
    m_preparedMultiRg = m_multiRgSpin ? m_multiRgSpin->value() : m_multiRg;
    m_preparedMultiAz = m_multiAzSpin ? m_multiAzSpin->value() : m_multiAz;
    if (!m_demInputData) {
        setStartFailureMessage(QStringLiteral("辅助 DEM 是必需输入，必须通过带已识别 descriptor 的输入端口提供。"));
        return false;
    }
    const ProductValidationResult demBinding = validateBoundDescriptor(
        productInputContract(1), m_demInputData->productDescriptor());
    if (!demBinding.accepted) {
        setStartFailureMessage(demBinding.reason);
        setLastErrorMessage(demBinding.reason);
        return false;
    }
    if (!QFileInfo(m_demInputData->filePath()).isFile()) {
        const QString demPathError = QStringLiteral("辅助 DEM 栅格文件不存在：%1")
                                        .arg(m_demInputData->filePath());
        setStartFailureMessage(demPathError);
        setLastErrorMessage(demPathError);
        return false;
    }
    m_preparedDemPath = m_demInputData->filePath();

    m_preparedDstNode = dstNode;

    m_preparedInputPaths = m_inputData->filePaths();
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_preparedInputPaths, m_inputData->physicalProductDescriptor(),
                                         &identityError)) {
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }
    m_preparedProductLevel.clear();
    m_preparedMasterIndex = 0;

    QStandardItemModel* model = projectModel();
    if (!model) {
        return false;
    }
    QList<QStandardItem*> projects = model->findItems(projectName());
    if (projects.isEmpty()) {
        return false;
    }

    const QString srcNode = m_inputData->nodeName();
    QStandardItem* project = projects.first();
    QStandardItem* sourceNode = nullptr;
    for (int i = 0; i < project->rowCount(); ++i) {
        if (project->child(i, 0) && project->child(i, 0)->text() == srcNode) {
            sourceNode = project->child(i, 0);
            if (project->child(i, 1)) {
                m_preparedProductLevel = project->child(i, 1)->text();
            }
            break;
        }
    }
    if (!sourceNode || (m_preparedType == 1 && m_preparedProductLevel.isEmpty())) {
        return false;
    }

    if (m_preparedType == 2) {
        XMLFile* xml = projectXml();
        TiXmlElement* dataNode = nullptr;
        if (xml && xml->find_node_with_attribute("DataNode", "name", srcNode.toStdString().c_str(), dataNode) == 0 && dataNode) {
            TiXmlElement* masterElement = nullptr;
            if (xml->_find_node(dataNode, "master_image", masterElement) == 0 && masterElement && masterElement->GetText()) {
                bool ok = false;
                const int xmlMasterIndex = QString::fromUtf8(masterElement->GetText()).toInt(&ok);
                if (ok && xmlMasterIndex > 0) {
                    m_preparedMasterIndex = xmlMasterIndex - 1;
                }
            }
        }
        if (m_preparedMasterIndex < 0 || m_preparedMasterIndex >= m_preparedInputPaths.size()) {
            return false;
        }
    }

    m_preparedOutputPaths.clear();
    for (const QString& srcPath : m_preparedInputPaths) {
        QFileInfo fi(srcPath);
        QString changeName = fi.baseName() + "_geocoded";
        m_preparedOutputPaths.append(savePath + "/" + dstNode + "/" + changeName + ".h5");
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), dstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void GeocodingNode::executeProcessing()
{
    InSARLogManager::LogInfo("GeocodingNode", "executeProcessing started.");

    setProgress(0);

    QString dstNode = m_preparedDstNode;
    QString savePath = projectPath();
    QStringList inputPaths = m_preparedInputPaths;
    QString productLevel = m_preparedProductLevel;
    int masterIndex = m_preparedMasterIndex;
    QString preparedDemPath = m_preparedDemPath;

    int type = m_preparedType;
    int multiRg = m_preparedMultiRg;
    int multiAz = m_preparedMultiAz;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = dstNode;
        
        updateParameterWidgetsEnableState();

        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) {
            finishExecution();
        } else {
            setState(ExecutionState::Error);
        }
        return;
    }

    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(savePath, dstNode, m_preparedOutputPaths,
                                           inputPaths, m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> descriptorProvenance;
    descriptorProvenance.insert(QStringLiteral("producer"), name());
    descriptorProvenance.insert(QStringLiteral("output_port"),
                                QStringLiteral("geocoding.output.geocoded_raster"));
    if (!NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction, ProductDescriptor::create(
                QStringLiteral("geocoded_raster"), QStringLiteral("sat-explorer-product"), 1,
                ProductState::Committed, name(), descriptorProvenance), &transactionError)) {
        onError(transactionError);
        return;
    }
    m_pendingGeocodingResults.clear();
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    m_xmlDirty = false;

    m_thread = new QThread();
    m_workerThread = new GeocodingWorker();
    m_workerThread->moveToThread(m_thread);

    if (preparedDemPath.isEmpty()) {
        connect(this, &GeocodingNode::startGeocoding, m_workerThread, &GeocodingWorker::Geocoding);
        const QString stagingNode = m_outputTransaction.stagingName;
        connect(m_thread, &QThread::started, [this, type, multiRg, multiAz, savePath, inputPaths, productLevel, masterIndex, stagingNode]() {
            Q_EMIT startGeocoding(type, multiRg, multiAz, savePath, inputPaths, productLevel, masterIndex, stagingNode);
        });
    } else {
        connect(this, &GeocodingNode::startGeocodingWithDem, m_workerThread, &GeocodingWorker::GeocodingWithDem);
        const QString stagingNode = m_outputTransaction.stagingName;
        connect(m_thread, &QThread::started, [this, type, multiRg, multiAz, savePath, inputPaths, productLevel, masterIndex, stagingNode, preparedDemPath]() {
            Q_EMIT startGeocodingWithDem(type, multiRg, multiAz, savePath, inputPaths, productLevel, masterIndex, stagingNode, preparedDemPath);
        });
    }

    connect(m_workerThread, &GeocodingWorker::geocodingGenerated, this, &GeocodingNode::onGeocodingGenerated);
    connect(m_workerThread, &GeocodingWorker::updateProcess, this, &GeocodingNode::onProgressUpdate);
    connect(m_workerThread, &GeocodingWorker::endProcess, this, &GeocodingNode::onProcessingFinished);
    connect(m_workerThread, &GeocodingWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &GeocodingWorker::cancelled, this, &GeocodingNode::onCancelled);
    connect(m_workerThread, &GeocodingWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &GeocodingWorker::errorProcess, this, &GeocodingNode::onError);
    connect(m_workerThread, &GeocodingWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &GeocodingWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &GeocodingWorker::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Dynamic recovery of Running state for Automatic execution mode (SOP Rule 5)
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
            setState(ExecutionState::Running);
    });

    // Disable inputs during execution
    updateParameterWidgetsEnableState();

    deferAutomaticCompletion();
    m_thread->start();
}

void GeocodingNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void GeocodingNode::onProcessingFinished()
{
    const QString dstNode = m_preparedDstNode;
    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;
    QList<GeocodingFileResult> committedResults;

    cleanUpThreadAndWorker();
    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        return;
    }

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }

    QString transactionError;
    if (!projectXml()) {
        onError(QStringLiteral("Project XML context is unavailable for geocoding output commit."));
        return;
    }
    QString outputDataset;
    QString previewType;
    if (m_preparedType == 2) {
        outputDataset = QStringLiteral("amplitude");
        previewType = QStringLiteral("amplitude");
    } else if (m_preparedProductLevel.startsWith(QStringLiteral("phase-"))) {
        outputDataset = QStringLiteral("phase");
        previewType = QStringLiteral("phase");
    } else if (m_preparedProductLevel == QStringLiteral("coherence-1.0")) {
        outputDataset = QStringLiteral("coherence");
        previewType = QStringLiteral("coherence");
    } else if (m_preparedProductLevel == QStringLiteral("dem-1.0")) {
        outputDataset = QStringLiteral("dem");
        previewType = QStringLiteral("dem");
    } else if (m_preparedProductLevel == QStringLiteral("SBAS-1.0")) {
        outputDataset = QStringLiteral("defomation_velocity");
        previewType = QStringLiteral("SBAS");
    } else {
        onError(QStringLiteral("Unsupported geocoding product level for staged output validation: %1")
                    .arg(m_preparedProductLevel));
        return;
    }
    if (!NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << outputDataset, &transactionError)) {
        onError(transactionError);
        return;
    }
    QStringList workerOutputPaths;
    for (const GeocodingFileResult& result : m_pendingGeocodingResults) {
        workerOutputPaths.append(result.geocodePath);
    }
    if (!NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, workerOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError)) {
        onError(transactionError);
        return;
    }

    if (!NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    for (GeocodingFileResult result : m_pendingGeocodingResults) {
        const QString fileName = QFileInfo(result.geocodePath).fileName();
        result.dstNode = dstNode;
        result.geocodePath = QDir(projectPath() + "/" + dstNode).absoluteFilePath(fileName);
        result.relativePath = QStringLiteral("/%1/%2").arg(dstNode, fileName);
        commitGeocodingResult(result);
        committedResults.append(result);
    }
    if (!m_xmlDirty) {
        onError(QStringLiteral("Geocoding output metadata was not produced."));
        return;
    }
    XMLFile* xml = projectXml();
    const QString xmlPath = NodeUtils::getProjectFilePath(_widget);
    if (!NodeUtils::saveProjectXmlAtomically(xml, xmlPath, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_xmlDirty = false;
    if (!NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    for (const GeocodingFileResult& result : committedResults) {
        publishGeocodingResultToProjectTree(result);
    }
    if (auto* iface = NodeUtils::getProjectContext(_widget)) iface->refreshProjectTree();
    for (const QString& h5Path : h5Paths) {
        const QString baseName = QFileInfo(h5Path).baseName();
        jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + ".jpg");
        types.append(previewType);
    }

    // Clean up worker thread
    updateParameterWidgetsEnableState();

    m_outputNodeName = dstNode;
    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    // Generate JPG previews asynchronously (SOP Rule 7)
    if (!h5Paths.isEmpty()) {
        startPreviewGeneration(h5Paths, jpgPaths, types, h5Paths, jpgPaths, true);
    } else {
        setState(ExecutionState::Running);
        setProgress(100);
        finishExecution();
    }
}

void GeocodingNode::onCancelled()
{
    InSARLogManager::LogInfo("GeocodingNode", "Geocoding cancellation cleanup completed.");
    cleanUpThreadAndWorker();
    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic cancellation"), projectXml());
        return;
    }

    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    updateParameterWidgetsEnableState();
}

void GeocodingNode::onError(const QString& error)
{
    InSARLogManager::LogError("GeocodingNode", "executeProcessing failed: " + error);
    Q_EMIT executionError(error);

    cleanUpThreadAndWorker();

    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic error"), projectXml());
        return;
    }

    updateParameterWidgetsEnableState();

    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    setState(ExecutionState::Error);
}

ProductInputContract GeocodingNode::productInputContract(PortIndex portIndex) const
{
    ProductInputContract contract;
    contract.semanticId = portIndex == 0 ? QStringLiteral("geocoding.input.radar_product")
                                         : QStringLiteral("geocoding.input.auxiliary_terrain_dem");
    contract.optional = false;
    contract.allowedProductTypes = portIndex == 0
        ? QStringList{QStringLiteral("unwrapped_phase"), QStringLiteral("insar_dem")}
        : QStringList{QStringLiteral("auxiliary_terrain_dem")};
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract GeocodingNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0 ? QStringLiteral("geocoding.output.geocoded_raster")
                                         : QStringLiteral("geocoding.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList{QStringLiteral("geocoded_raster")} : QStringList{QStringLiteral("preview")};
    return contract;
}

void GeocodingNode::onGeocodingGenerated(const GeocodingFileResult& result)
{
    m_pendingGeocodingResults.append(result);
}

void GeocodingNode::commitGeocodingResult(const GeocodingFileResult& result)
{
    XMLFile* xml = projectXml();
    if (xml) {
        xml->XMLFile_add_geocoding(result.dstNode.toStdString().c_str(), result.geocodeName.toStdString().c_str(),
            result.relativePath.toStdString().c_str(), result.rankLevel.toStdString().c_str());
        m_xmlDirty = true;
    }
}

void GeocodingNode::publishGeocodingResultToProjectTree(const GeocodingFileResult& result)
{
    QStandardItemModel* model = projectModel();
    if (!model) return;

    QList<QStandardItem*> foundProjects = model->findItems(projectName());
    if (foundProjects.isEmpty()) return;
    QStandardItem* project = foundProjects[0];

    QStandardItem* geocodeNode = NodeUtils::findOrCreateProjectNode(project, result.dstNode, result.rankLevel);
    if (geocodeNode) {
        geocodeNode->setToolTip(projectName());
        QStandardItem* itemImg = nullptr;
        for (int j = 0; j < geocodeNode->rowCount(); j++) {
            if (geocodeNode->child(j, 0)->text() == result.geocodeName) {
                itemImg = geocodeNode->child(j, 0);
                break;
            }
        }
        if (!itemImg) {
            QStandardItem* geocodeNameItem = new QStandardItem(result.geocodeName);
            if (result.rankLevel == "coherence-1.0") geocodeNameItem->setToolTip("coherence");
            else if (result.rankLevel.startsWith("phase")) geocodeNameItem->setToolTip("phase");
            else if (result.rankLevel == "dem-1.0") geocodeNameItem->setToolTip("dem");
            else if (result.rankLevel == "SBAS-1.0") geocodeNameItem->setToolTip("SBAS");
            else geocodeNameItem->setToolTip("amplitude");

            QStandardItem* geocodePathItem = new QStandardItem(result.geocodePath);
            geocodeNameItem->setIcon(QIcon(IMAGEDATA_ICON));
            geocodeNode->appendRow(geocodeNameItem);
            geocodeNode->setChild(geocodeNode->rowCount() - 1, 1, geocodePathItem);
        } else {
            geocodeNode->setChild(itemImg->row(), 1, new QStandardItem(result.geocodePath));
        }
    }

}

bool GeocodingNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList h5Paths;
    ProductDescriptor::Ptr descriptor;
    QString identityError;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths) ||
        !NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), dstNode, descriptor, &identityError) ||
        !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
        !NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) return false;
    QStringList expectedJpgPaths;
    QStringList types;

    FormatConversion FC;
    cv::Mat dummy;
    for (const QString& h5Path : h5Paths) {
        QString baseName = QFileInfo(h5Path).baseName();
        expectedJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + ".jpg");

        // Determine type dynamically from datasets
        QString type = "amplitude";
        {
            NodeUtils::Hdf5Locker locker;
            if (NodeUtils::readMatFromH5(h5Path, "phase", dummy)) {
                type = "phase";
            } else if (NodeUtils::readMatFromH5(h5Path, "coherence", dummy)) {
                type = "coherence";
            } else if (NodeUtils::readMatFromH5(h5Path, "dem", dummy)) {
                type = "dem";
            } else if (NodeUtils::readMatFromH5(h5Path, "defomation_velocity", dummy)) {
                type = "SBAS";
            }
        }
        types.append(type);
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    // Remedy missing JPG previews in background (SOP Rule 15)
    QStringList existingJpgPaths;
    QStringList missingH5s;
    QStringList missingJpgs;
    QStringList missingTypes;

    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) {
            existingJpgPaths.append(expectedJpgPaths[i]);
        } else {
            missingH5s.append(h5Paths[i]);
            missingJpgs.append(expectedJpgPaths[i]);
            missingTypes.append(types[i]);
        }
    }

    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    } else {
        startPreviewGeneration(missingH5s, missingJpgs, missingTypes, h5Paths, expectedJpgPaths, false);
    }

    return true;
}

void GeocodingNode::startPreviewGeneration(const QStringList& h5Paths,
                                           const QStringList& generatedJpgPaths,
                                           const QStringList& types,
                                           const QStringList& resultH5Paths,
                                           const QStringList& resultJpgPaths,
                                           bool completeExecution)
{
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.disconnect(this);
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Paths, generatedJpgPaths, types, resultH5Paths, resultJpgPaths, completeExecution]() {
            m_remedyWatcher.disconnect(this);
            startPreviewGeneration(h5Paths, generatedJpgPaths, types, resultH5Paths, resultJpgPaths, completeExecution);
        });
        return;
    }

    m_remedyWatcher.disconnect(this);
    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, resultH5Paths, resultJpgPaths, completeExecution]() {
        QStringList currentJpgPaths;
        for (int i = 0; i < resultH5Paths.size() && i < resultJpgPaths.size(); ++i) {
            if (NodeUtils::isJpgPreviewCurrent(resultH5Paths[i], resultJpgPaths[i])) currentJpgPaths.append(resultJpgPaths[i]);
        }
        if (completeExecution) {
            if (discardObsoleteAutomaticExecution()) return;
            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
            setOutputData(1, m_imageInfoData);
            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("GeocodingNode", "Processing preview generation completed.");
            finishExecution();
        } else {
            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
            InSARLogManager::LogInfo("GeocodingNode", "validateAndRestoreOutput background rendering completed.");
        }
    });
    m_remedyWatcher.setFuture(QtConcurrent::run([h5Paths, generatedJpgPaths, types]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], generatedJpgPaths[i], types[i]);
        }
    }));
}

QStringList GeocodingNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return list;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        for (const QString& h5Path : h5Paths) {
            const QFileInfo info(h5Path);
            const QString jpgPath = info.absolutePath() + "/" + info.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                list.append(jpgPath);
            }
        }
    }
    return list;
}

QStandardItemModel* GeocodingNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString GeocodingNode::projectPath() const
{
    IApplicationInterface* iface = nullptr;
    if (_widget) iface = NodeUtils::getProjectContext(_widget);
    if (!iface) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* mainWin = qobject_cast<MainWindow*>(w)) {
                if (mainWin->workspaceUI()) { iface = mainWin->workspaceUI(); break; }
                if (mainWin->interfaceManager()) { iface = mainWin->interfaceManager()->currentInterface(); if (iface) break; }
            }
        }
    }
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString GeocodingNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* GeocodingNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void GeocodingNode::execute()
{
    executeProcessing();
}

void GeocodingNode::stopExecution()
{
    if (m_workerThread) {
        m_workerThread->StopProcess();
    }
    cleanUpThreadAndWorker();
}

void GeocodingNode::cleanUpThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning())
        thread->quit();
}

void GeocodingNode::processAutomatically()
{
    if (prepareToStart())
    {
        executeProcessing();
    }
    else
    {
        setState(ExecutionState::Idle);
    }
}

void GeocodingNode::updateParameterWidgetsEnableState()
{
    bool isExec = m_thread && m_thread->isRunning();
    bool enableWidgets = !isExec;

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets);
    if (m_typeCombo) m_typeCombo->setEnabled(enableWidgets);

    bool isInterfero = (m_type == 1);
    if (m_multiRgSpin) m_multiRgSpin->setEnabled(enableWidgets && !isInterfero);
    if (m_multiAzSpin) m_multiAzSpin->setEnabled(enableWidgets && !isInterfero);
    if (m_multiRgLabel) m_multiRgLabel->setEnabled(enableWidgets && !isInterfero);
    if (m_multiAzLabel) m_multiAzLabel->setEnabled(enableWidgets && !isInterfero);

    bool hasDemConn = (m_demInputData != nullptr);
    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !hasDemConn);
    if (m_demPathEdit) m_demPathEdit->setEnabled(enableWidgets && !hasDemConn);
    if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(enableWidgets && !hasDemConn);
}

} // namespace QtNodes
