#include "SBASReferenceReselectionNode.h"
#include "FormatConversion.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include "reselection_view.h"
#include "tinyxml.h"
#include <QTimer>
#include <QJsonDocument>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QApplication>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <cmath>
#include <limits>

namespace QtNodes {

namespace {
const char* const kSbasProvenanceDataset = "sbas_rebuild_provenance";

bool isSafeProjectRelativePath(const QString& path)
{
    const QString cleaned = QDir::cleanPath(path);
    return !cleaned.isEmpty() && !QDir::isAbsolutePath(cleaned) && cleaned != QStringLiteral("..") &&
           !cleaned.startsWith(QStringLiteral("../"));
}

bool loadSbasRebuildProvenance(const QString& h5Path, const QString& projectRoot,
                               const QString& committedRunId, QStringList& sourceInputs,
                               SBASRebuildParameters& parameters,
                               QJsonArray* validatedInputFingerprints, QString* errorMessage)
{
    sourceInputs.clear();
    if (validatedInputFingerprints) *validatedInputFingerprints = QJsonArray();
    std::string rawProvenance;
    if (!NodeUtils::readStringFromH5(h5Path, QString::fromLatin1(kSbasProvenanceDataset),
                                     rawProvenance, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("SBAS output has no rebuild provenance. Please rerun SBAS Time Series.");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonObject provenance = QJsonDocument::fromJson(
        QByteArray::fromStdString(rawProvenance), &parseError).object();
    if (parseError.error != QJsonParseError::NoError || provenance.value(QStringLiteral("version")).toInt() != 2 ||
        provenance.value(QStringLiteral("transactionRunId")).toString() != committedRunId) {
        if (errorMessage) *errorMessage = QStringLiteral("SBAS rebuild provenance is missing, malformed, or does not match its committed manifest. Please rerun SBAS Time Series.");
        return false;
    }

    const QJsonObject values = provenance.value(QStringLiteral("parameters")).toObject();
    const QJsonArray inputs = provenance.value(QStringLiteral("inputs")).toArray();
    if (values.isEmpty() || inputs.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("SBAS rebuild provenance is incomplete. Please rerun SBAS Time Series.");
        return false;
    }
    const auto readFiniteNumber = [&values](const char* name, double& output) {
        const QJsonValue value = values.value(QString::fromLatin1(name));
        if (!value.isDouble()) return false;
        output = value.toDouble();
        return std::isfinite(output);
    };
    const auto readPositiveInt = [&readFiniteNumber](const char* name, int& output) {
        double value = 0.0;
        if (!readFiniteNumber(name, value) || value < 1.0 || std::floor(value) != value ||
            value > static_cast<double>(std::numeric_limits<int>::max())) {
            return false;
        }
        output = static_cast<int>(value);
        return true;
    };
    if (!readFiniteNumber("temporalThreshLow", parameters.temporalThreshLow) ||
        !readFiniteNumber("temporalThresh", parameters.temporalThresh) ||
        !readFiniteNumber("spatialThresh", parameters.spatialThresh) ||
        !readPositiveInt("multilookRg", parameters.multilookRg) ||
        !readPositiveInt("multilookAz", parameters.multilookAz) ||
        !readPositiveInt("unwrapMethod", parameters.unwrapMethod) ||
        !readFiniteNumber("alpha", parameters.alpha) ||
        !readFiniteNumber("coherenceThresh", parameters.coherenceThresh) ||
        !readFiniteNumber("temporalCoherenceThresh", parameters.temporalCoherenceThresh) ||
        !readFiniteNumber("refinementCohThresh", parameters.refinementCohThresh) ||
        !readFiniteNumber("refinementDefThresh", parameters.refinementDefThresh) ||
        parameters.temporalThreshLow < 0.0 || parameters.temporalThresh <= 0.0 ||
        parameters.temporalThresh < parameters.temporalThreshLow || parameters.spatialThresh <= 0.0 ||
        parameters.unwrapMethod < 1 || parameters.unwrapMethod > 3 || parameters.alpha < 0.0 ||
        parameters.alpha > 1.0 || parameters.coherenceThresh < 0.0 || parameters.coherenceThresh > 1.0 ||
        parameters.temporalCoherenceThresh < 0.0 || parameters.temporalCoherenceThresh > 1.0 ||
        parameters.refinementCohThresh < 0.0 || parameters.refinementCohThresh > 1.0 ||
        parameters.refinementDefThresh < 0.0) {
        if (errorMessage) *errorMessage = QStringLiteral("SBAS rebuild provenance has incomplete or invalid processing parameters. Please rerun SBAS Time Series.");
        return false;
    }

    const QDir root(projectRoot);
    for (const QJsonValue& value : inputs) {
        const QJsonObject input = value.toObject();
        const QString relativePath = input.value(QStringLiteral("path")).toString();
        if (!isSafeProjectRelativePath(relativePath)) {
            if (errorMessage) *errorMessage = QStringLiteral("SBAS rebuild provenance contains an unsafe input path. Please rerun SBAS Time Series.");
            return false;
        }
        const QJsonArray actualFingerprints = NodeUtils::fingerprintInputPaths(
            QStringList() << root.absoluteFilePath(relativePath));
        if (actualFingerprints.size() != 1) {
            if (errorMessage) *errorMessage = QStringLiteral("SBAS provenance input fingerprint could not be captured: %1. Please rerun SBAS Time Series.").arg(relativePath);
            return false;
        }
        const QJsonObject actual = actualFingerprints.first().toObject();
        if (actual.value(QStringLiteral("size")).toDouble(-1.0) < 0.0 ||
            actual.value(QStringLiteral("modifiedMs")).toDouble(-1.0) < 0.0) {
            if (errorMessage) *errorMessage = QStringLiteral("SBAS provenance input is missing: %1. Please rerun SBAS Time Series.").arg(relativePath);
            return false;
        }
        const QJsonValue expectedSize = input.value(QStringLiteral("size"));
        const QJsonValue expectedModified = input.value(QStringLiteral("modifiedMs"));
        if (!expectedSize.isDouble() || !expectedModified.isDouble() ||
            actual.value(QStringLiteral("size")).toDouble() != expectedSize.toDouble() ||
            actual.value(QStringLiteral("modifiedMs")).toDouble() != expectedModified.toDouble()) {
            if (errorMessage) *errorMessage = QStringLiteral("SBAS provenance input fingerprint changed: %1. Please rerun SBAS Time Series.").arg(relativePath);
            return false;
        }
        sourceInputs.append(actual.value(QStringLiteral("path")).toString());
        if (validatedInputFingerprints) {
            validatedInputFingerprints->append(actual);
        }
    }
    return true;
}

bool loadCurrentCommittedSbasInputs(const QString& projectRoot, const QString& inputH5,
                                    QStringList& sourceInputs, SBASRebuildParameters& parameters,
                                    QJsonArray* capturedFingerprints, QString* errorMessage)
{
    const QJsonArray inputH5BeforeRead = NodeUtils::fingerprintInputPaths(QStringList() << inputH5);
    const QString upstreamNode = QFileInfo(inputH5).absoluteDir().dirName();
    QStringList upstreamOutputs;
    QString committedRunId;
    QJsonArray sourceInputsAtValidation;
    if (!NodeUtils::loadCommittedOutputManifest(projectRoot, upstreamNode, upstreamOutputs, errorMessage) ||
        upstreamOutputs.size() != 1 ||
        QDir::cleanPath(QFileInfo(upstreamOutputs.first()).absoluteFilePath()).compare(
            QDir::cleanPath(QFileInfo(inputH5).absoluteFilePath()), Qt::CaseInsensitive) != 0 ||
        !NodeUtils::loadCommittedOutputManifestRunId(projectRoot, upstreamNode, committedRunId, errorMessage) ||
        !loadSbasRebuildProvenance(inputH5, projectRoot, committedRunId,
                                   sourceInputs, parameters, &sourceInputsAtValidation, errorMessage)) {
        return false;
    }

    QStringList transactionInputs = sourceInputs;
    transactionInputs.prepend(inputH5);
    QJsonArray inputsAtValidation = inputH5BeforeRead;
    for (const QJsonValue& value : sourceInputsAtValidation) inputsAtValidation.append(value);
    const QJsonArray inputsAfterRead = NodeUtils::fingerprintInputPaths(transactionInputs);
    if (QJsonDocument(inputsAtValidation).toJson(QJsonDocument::Compact) !=
        QJsonDocument(inputsAfterRead).toJson(QJsonDocument::Compact)) {
        if (errorMessage) *errorMessage = QStringLiteral("SBAS input changed while its committed provenance was being read. Please rerun SBAS Time Series.");
        return false;
    }
    if (capturedFingerprints) *capturedFingerprints = inputsAfterRead;
    return true;
}

bool copySbasRebuildProvenance(const QString& sourceH5, const QString& stagedH5,
                               const QString& transactionRunId, QString* errorMessage)
{
    std::string rawProvenance;
    if (!NodeUtils::readStringFromH5(sourceH5, QString::fromLatin1(kSbasProvenanceDataset),
                                     rawProvenance, errorMessage)) {
        return false;
    }
    QJsonParseError parseError;
    QJsonObject provenance = QJsonDocument::fromJson(
        QByteArray::fromStdString(rawProvenance), &parseError).object();
    if (parseError.error != QJsonParseError::NoError || provenance.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("SBAS rebuild provenance became unreadable during reselection.");
        return false;
    }
    provenance.insert(QStringLiteral("parentSbasTransactionRunId"),
                      provenance.value(QStringLiteral("transactionRunId")).toString());
    provenance.insert(QStringLiteral("transactionRunId"), transactionRunId);
    return NodeUtils::writeStringToH5(stagedH5, QString::fromLatin1(kSbasProvenanceDataset),
                                      QJsonDocument(provenance).toJson(QJsonDocument::Compact).toStdString(),
                                      errorMessage);
}
}

SBASReferenceReselectionNode::SBASReferenceReselectionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_refPointLabel(nullptr)
    , m_gcpLabel(nullptr)
    , m_selectBtn(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_resultLabel(nullptr)
    , m_refRow(-1)
    , m_refCol(-1)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
}

SBASReferenceReselectionNode::~SBASReferenceReselectionNode()
{
    ++m_executionGeneration;
    stopExecution();
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
    }
    rollbackOutputTransaction(QStringLiteral("node destroyed"));
}

unsigned int SBASReferenceReselectionNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2; // Port 0: H5 output, Port 1: JPG preview
}

NodeDataType SBASReferenceReselectionNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        return ImportedFileData().type();
    }
    else
    {
        if (portIndex == 0)
            return ImportedFileData().type();
        else
            return ImageInfoData().type();
    }
}

bool SBASReferenceReselectionNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return true;
}

QString SBASReferenceReselectionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        return tr("成果 *");
    }
    else
    {
        if (portIndex == 0)
            return tr("成果 *");
        else
            return tr("预览 ?");
    }
}

bool SBASReferenceReselectionNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> SBASReferenceReselectionNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_previewData;
}

void SBASReferenceReselectionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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
        
        // Auto-generate output name if empty or default placeholder
        if (m_outputNodeNameEdit && (m_outputNodeNameEdit->text().isEmpty() || m_outputNodeNameEdit->text() == QStringLiteral("自动生成或手动输入")))
        {
            m_outputNodeName = upstreamNode + "_Reselect";
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }
    else
    {
        if (m_inputNodeLabel)
        {
            m_inputNodeLabel->setText(QStringLiteral("未连接"));
        }
        m_outputNodeName.clear();
        if (m_outputNodeNameEdit)
        {
            m_outputNodeNameEdit->setText(QStringLiteral("自动生成或手动输入"));
        }
    }
    
    updateLabels();
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* SBASReferenceReselectionNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void SBASReferenceReselectionNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
    if (_widget)
    {
        bool isManual = (mode == ExecutionMode::Manual);
        m_selectBtn->setEnabled(isManual);
        m_outputNodeNameEdit->setEnabled(isManual);
    }
}

void SBASReferenceReselectionNode::createWidget()
{
    _widget = new ::QWidget();
    _widget->setFixedWidth(300);
    _widget->setStyleSheet("background-color: transparent;");

    QVBoxLayout* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(6);

    // Helper lambda to add parameter rows
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

    // Selected Reference Point display
    m_refPointLabel = new QLabel(QStringLiteral("未选择"), _widget);
    m_refPointLabel->setStyleSheet("color: #E0E0E0; font-size: 11px;");
    addFormRow(QStringLiteral("参考点:"), m_refPointLabel);

    // Selected GCPs count display
    m_gcpLabel = new QLabel(QStringLiteral("0 GCPs"), _widget);
    m_gcpLabel->setStyleSheet("color: #E0E0E0; font-size: 11px;");
    addFormRow(QStringLiteral("GCP数量:"), m_gcpLabel);

    // Select Button
    m_selectBtn = new QPushButton(QStringLiteral("选择参考点与GCP"), _widget);
    m_selectBtn->setStyleSheet("QPushButton { background-color: #3B82F6; color: white; border-radius: 4px; padding: 4px 8px; font-size: 11px; }"
                               "QPushButton:hover { background-color: #2563EB; }"
                               "QPushButton:disabled { background-color: #4B5563; color: #9CA3AF; }");
    connect(m_selectBtn, &QPushButton::clicked, this, &SBASReferenceReselectionNode::onSelectClicked);
    layout->addWidget(m_selectBtn);

    // Output node name
    m_outputNodeNameEdit = new QLineEdit(_widget);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    m_outputNodeNameEdit->setText(QStringLiteral("自动生成或手动输入"));
    m_outputNodeNameEdit->setStyleSheet("color: white; background-color: #1F2937; border: 1px solid #4B5563; border-radius: 4px; padding: 2px; font-size: 11px;");
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text;
        updateWidgetSize();
    });
    addFormRow(QStringLiteral("输出节点:"), m_outputNodeNameEdit);

    // Progress/Result label
    m_resultLabel = new QLabel(_widget);
    m_resultLabel->setStyleSheet("color: #10B981; font-size: 10px;");
    m_resultLabel->setWordWrap(true);
    layout->addWidget(m_resultLabel);

    updateLabels();
}

void SBASReferenceReselectionNode::onSelectClicked()
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
    {
        QMessageBox::warning(nullptr, QStringLiteral("警告"), QStringLiteral("请先连接输入节点！"));
        return;
    }

    QString image_path = m_inputData->filePath();
    QFileInfo fileinfo(image_path);
    QString image_name = fileinfo.completeBaseName();
    QString jpg_path = fileinfo.absolutePath() + "/SBAS_time_series.jpg";

    if (!QFile::exists(jpg_path))
    {
        // Try to generate it synchronously
        Utils util;
        FormatConversion FC;
        Mat defomation_velocity, mask;
        int ret = -1;
        ret = (NodeUtils::readMatFromH5(image_path, "defomation_velocity", defomation_velocity, CV_64F) &&
               NodeUtils::readMatFromH5(image_path, "mask", mask)) ? 0 : -1;
        if (ret == 0)
        {
            util.savephase_white(jpg_path.toStdString().c_str(), "jet", defomation_velocity, mask);
        }
    }

    if (!QFile::exists(jpg_path))
    {
        QMessageBox::warning(nullptr, QStringLiteral("错误"), QStringLiteral("无法生成预览图以进行选择，请确认输入数据是否完整！"));
        return;
    }

    reselection_view_Window* Pre_wnd = new reselection_view_Window();
    Pre_wnd->View->setPixmap(jpg_path);
    Pre_wnd->View->SetH5Path(image_path);
    connect(Pre_wnd, &reselection_view_Window::send_coordinate, this, &SBASReferenceReselectionNode::onCoordinatesSelected);
    Pre_wnd->show();
    Pre_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
}

void SBASReferenceReselectionNode::onCoordinatesSelected(int ref_row, int ref_col, QList<QPoint> plist)
{
    m_refRow = ref_row;
    m_refCol = ref_col;
    m_GCPs = plist;
    
    updateLabels();
}

void SBASReferenceReselectionNode::updateLabels()
{
    if (m_refPointLabel)
    {
        if (m_refRow >= 0 && m_refCol >= 0)
            m_refPointLabel->setText(QString("Row: %1, Col: %2").arg(m_refRow).arg(m_refCol));
        else
            m_refPointLabel->setText(QStringLiteral("未选择"));
    }
    if (m_gcpLabel)
    {
        m_gcpLabel->setText(QString("%1 GCPs").arg(m_GCPs.size()));
    }
    
    updateWidgetSize();
}

void SBASReferenceReselectionNode::updateWidgetSize()
{
    if (_widget)
    {
        _widget->adjustSize();
        // Ensure parent graphics proxy widget updates bounds
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool SBASReferenceReselectionNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;
    if (m_refRow < 0 || m_refCol < 0)
        return false;
    if (m_GCPs.isEmpty())
        return false;
    if (m_outputNodeName.isEmpty() || m_outputNodeName == QStringLiteral("自动生成或手动输入"))
        return false;
    return true;
}

bool SBASReferenceReselectionNode::prepareToStart()
{
    if (!validateInputs())
    {
        return false;
    }

    m_hasPreparedProvenance = false;
    m_preparedProjectRoot = projectPath();
    m_preparedProjectName = projectName();
    if (m_preparedProjectRoot.isEmpty() || m_inputData->filePaths().size() != 1) {
        if (m_resultLabel) m_resultLabel->setText(QStringLiteral("输入必须是唯一的已提交 SBAS H5 成果。"));
        return false;
    }
    m_preparedInputH5 = m_inputData->filePaths().first();
    QString provenanceError;
    if (!loadCurrentCommittedSbasInputs(m_preparedProjectRoot, m_preparedInputH5,
                                        m_preparedSourceInputs, m_preparedParameters, nullptr, &provenanceError)) {
        if (m_resultLabel) m_resultLabel->setText(provenanceError.isEmpty()
            ? QStringLiteral("SBAS 输入不具备可重建 provenance，请重新运行 SBAS Time Series。") : provenanceError);
        InSARLogManager::LogWarning("SBASReferenceReselectionNode", provenanceError);
        return false;
    }
    m_hasPreparedProvenance = true;

    const QString h5Path = QDir(m_preparedProjectRoot).absoluteFilePath(
        m_outputNodeName + "/SBAS_time_series.h5");
    m_preparedOutputPaths = QStringList() << h5Path;

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget),
            m_outputNodeName,
            m_preparedOutputPaths,
            nullptr
        );
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void SBASReferenceReselectionNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        validateAndRestoreOutput();
        return;
    }

    executeProcessing();
}

void SBASReferenceReselectionNode::executeProcessing()
{
    InSARLogManager::LogInfo("SBASReferenceReselectionNode", "executeProcessing started.");
    const quint64 executionGeneration = ++m_executionGeneration;
    stopExecution();
    m_pendingResult = SBASReferenceReselectionResult();
    QString provenanceError;
    QJsonArray preflightFingerprints;
    if (!loadCurrentCommittedSbasInputs(m_preparedProjectRoot, m_preparedInputH5,
                                        m_preparedSourceInputs, m_preparedParameters,
                                        &preflightFingerprints, &provenanceError)) {
        onError(provenanceError.isEmpty()
            ? QStringLiteral("SBAS input changed before reselection could start. Please rerun SBAS Time Series.")
            : provenanceError);
        return;
    }
    QStringList transactionInputs = m_preparedSourceInputs;
    transactionInputs.prepend(m_preparedInputH5);
    QString transactionError;
    if (!m_hasPreparedProvenance ||
        !NodeUtils::beginOutputTransaction(m_preparedProjectRoot, m_outputNodeName,
                                           m_preparedOutputPaths, transactionInputs,
                                           m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    if (QJsonDocument(preflightFingerprints).toJson(QJsonDocument::Compact) !=
        QJsonDocument(m_outputTransaction.inputFingerprints).toJson(QJsonDocument::Compact)) {
        rollbackOutputTransaction(QStringLiteral("input changed before transaction start"));
        onError(QStringLiteral("SBAS inputs changed while starting reference reselection. Please rerun SBAS Time Series."));
        return;
    }
    if (m_resultLabel) m_resultLabel->setText(QStringLiteral("正在从 provenance 重建 SBAS 数据..."));

    m_worker = new SBASReferenceReselectionWorker();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    SBASReferenceReselectionWorker* const worker = m_worker;
    const QString projectRoot = m_preparedProjectRoot;
    const QString stagingNode = m_outputTransaction.stagingName;
    const QStringList sourceInputs = m_preparedSourceInputs;
    const SBASRebuildParameters parameters = m_preparedParameters;
    const int refRow = m_refRow;
    const int refCol = m_refCol;
    const QList<QPoint> gcps = m_GCPs;
    connect(m_thread, &QThread::started, m_worker,
            [worker, projectRoot, stagingNode, sourceInputs, parameters, refRow, refCol, gcps]() {
        worker->SBAS_reference_reselection(
            projectRoot, stagingNode, sourceInputs, parameters, refRow, refCol, gcps
        );
    });

    connect(m_worker, &SBASReferenceReselectionWorker::reselectionGenerated, this,
            [this, executionGeneration](const SBASReferenceReselectionResult& result) {
        if (executionGeneration != m_executionGeneration) return;
        QString provenanceError;
        if (!copySbasRebuildProvenance(m_preparedInputH5, result.outputH5Path,
                                       m_outputTransaction.runId, &provenanceError)) {
            ++m_executionGeneration;
            onError(QStringLiteral("Failed to preserve SBAS rebuild provenance: %1").arg(provenanceError));
            return;
        }
        m_pendingResult = result;
    });
    connect(m_worker, &SBASReferenceReselectionWorker::updateProcess, this,
            [this, executionGeneration](int progress, const QString& message) {
        if (executionGeneration == m_executionGeneration) onProgressUpdate(progress, message);
    });
    connect(m_worker, &SBASReferenceReselectionWorker::endProcess, this,
            [this, executionGeneration]() {
        if (executionGeneration == m_executionGeneration) onProcessingFinished();
    });
    connect(m_worker, &SBASReferenceReselectionWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &SBASReferenceReselectionWorker::errorProcess, this,
            [this, executionGeneration](const QString& error) {
        if (executionGeneration == m_executionGeneration) onError(error);
    });
    connect(m_worker, &SBASReferenceReselectionWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &SBASReferenceReselectionWorker::cancelled, this,
            [this, executionGeneration]() {
        if (executionGeneration == m_executionGeneration) onCancelled();
    });
    connect(m_worker, &SBASReferenceReselectionWorker::cancelled, m_thread, &QThread::quit);

    deferAutomaticCompletion();
    setState(ExecutionState::Running);
    setProgress(0);
    m_thread->start();
}

void SBASReferenceReselectionNode::stopExecution()
{
    if (m_worker)
    {
        m_worker->StopProcess();
        if (m_resultLabel) m_resultLabel->setText(QStringLiteral("已请求取消..."));
    }
}

void SBASReferenceReselectionNode::processAutomatically()
{
    if (prepareToStart()) {
        execute();
    } else {
        setState(ExecutionState::Idle);
    }
}

void SBASReferenceReselectionNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    if (m_resultLabel) m_resultLabel->setText(QString("%1%: %2").arg(progress).arg(message));
}

void SBASReferenceReselectionNode::onError(const QString& error)
{
    m_thread = nullptr;
    m_worker = nullptr;
    rollbackOutputTransaction(error);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("SBASReferenceReselectionNode", "Error during SBAS reselection: " + error);
    if (m_resultLabel) m_resultLabel->setText(QStringLiteral("失败: ") + error);
    setState(ExecutionState::Error);
    
}

void SBASReferenceReselectionNode::onCancelled()
{
    InSARLogManager::LogInfo("SBASReferenceReselectionNode", "Reference reselection cancellation completed.");
    m_thread = nullptr;
    m_worker = nullptr;
    rollbackOutputTransaction(QStringLiteral("cancelled"));
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    if (m_resultLabel) m_resultLabel->setText(QStringLiteral("已取消"));
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void SBASReferenceReselectionNode::onProcessingFinished()
{
    m_thread = nullptr;
    m_worker = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    QString transactionError;
    if (!commitOutputTransaction(&transactionError)) {
        onError(transactionError);
        return;
    }

    InSARLogManager::LogInfo("SBASReferenceReselectionNode", "executeProcessing completed.");
    if (m_resultLabel) m_resultLabel->setText(QStringLiteral("重新计算完成，生成预览图..."));

    generateStaticPreviewJpg();
}

void SBASReferenceReselectionNode::generateStaticPreviewJpg()
{
    QStringList outputs;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, outputs) || outputs.size() != 1) {
        onError(QStringLiteral("Committed SBAS reselection output is unavailable for preview."));
        return;
    }
    QString h5Path = outputs.first();
    QString outDir = QFileInfo(h5Path).absolutePath();
    QString jpgPath = outDir + "/SBAS_time_series.jpg";

    QFutureWatcher<bool>* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, h5Path, jpgPath]() {
        if (discardObsoleteAutomaticExecution()) {
            watcher->deleteLater();
            return;
        }

        bool ok = watcher->result();
        watcher->deleteLater();

        if (ok)
        {
            m_outputData = std::make_shared<ImportedFileData>(h5Path, m_outputNodeName);
            m_previewData = std::make_shared<ImageInfoData>(jpgPath);
            setOutputData(0, m_outputData);
            setOutputData(1, m_previewData);
            if (m_resultLabel) m_resultLabel->setText(QStringLiteral("计算并生成预览完成！"));
            setState(ExecutionState::Running);

            finishExecution();
        }
        else
        {
            m_previewData.reset();
            setOutputData(1, nullptr);
            if (m_resultLabel) m_resultLabel->setText(QStringLiteral("成果已提交；预览图生成失败。"));
            setState(ExecutionState::Running);
            finishExecution();
        }
    });

    QFuture<bool> future = QtConcurrent::run([h5Path, jpgPath]() -> bool {
        Utils util;
        FormatConversion FC;
        Mat defomation_velocity, mask;
        int ret = (NodeUtils::readMatFromH5(h5Path, "defomation_velocity", defomation_velocity, CV_64F) &&
                   NodeUtils::readMatFromH5(h5Path, "mask", mask)) ? 0 : -1;
        if (ret == 0)
        {
            util.savephase_white(jpgPath.toStdString().c_str(), "jet", defomation_velocity, mask);
            return true;
        }
        return false;
    });

    watcher->setFuture(future);
}

bool SBASReferenceReselectionNode::commitOutputTransaction(QString* errorMessage)
{
    if (m_pendingResult.outputH5Path.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("SBAS reselection worker did not return a staging output.");
        return false;
    }

    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    XMLFile* xml = iface ? iface->projectXml() : nullptr;
    const QString xmlPath = NodeUtils::getProjectFilePath(_widget);
    if (!xml || xmlPath.isEmpty() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, errorMessage) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
            QStringList() << QStringLiteral("mask") << QStringLiteral("defomation_velocity")
                          << QStringLiteral("deformation_time_series")
                          << QString::fromLatin1(kSbasProvenanceDataset), errorMessage) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths,
                                                QStringList() << m_pendingResult.outputH5Path, errorMessage)) {
        return false;
    }

    QStringList finalPaths;
    if (!NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, errorMessage) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, xml, xmlPath, errorMessage)) {
        return false;
    }

    TiXmlElement* root = nullptr;
    if (xml->get_root(root) < 0 || !root) {
        if (errorMessage) *errorMessage = QStringLiteral("Unable to read project XML root for SBAS reselection output.");
        return false;
    }
    for (TiXmlElement* element = root->FirstChildElement(); element != nullptr; ) {
        const char* name = element->Attribute("name");
        if (name && m_outputNodeName == QString::fromUtf8(name)) {
            TiXmlElement* toRemove = element;
            element = element->NextSiblingElement();
            root->RemoveChild(toRemove);
        } else {
            element = element->NextSiblingElement();
        }
    }
    const QString relativePath = QStringLiteral("/%1/SBAS_time_series.h5").arg(m_outputNodeName);
    if (xml->XMLFile_add_SBAS(m_outputNodeName.toStdString().c_str(), "SBAS_time_series",
                              relativePath.toStdString().c_str()) < 0 ||
        !NodeUtils::saveProjectXmlAtomically(xml, xmlPath, errorMessage) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Unable to commit SBAS reselection output metadata.");
        }
        return false;
    }

    if (iface && iface->projectModel() && !finalPaths.isEmpty()) {
        NodeUtils::removeDataNodeFromProjectTree(iface, m_outputNodeName);
        const QList<QStandardItem*> projects = iface->projectModel()->findItems(m_preparedProjectName);
        if (!projects.isEmpty()) {
            QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
                projects.first(), m_outputNodeName, "SBAS-1.0", FOLDER_ICON);
            if (outputNode) {
                outputNode->setToolTip(m_preparedProjectName);
                NodeUtils::findOrCreateChildItem(outputNode, "SBAS_time_series", "SBAS",
                                                  finalPaths.first(), IMAGEDATA_ICON);
            }
        }
        iface->refreshProjectTree();
    }
    m_outputData = std::make_shared<ImportedFileData>(QStringList() << finalPaths.first(), m_outputNodeName);
    setOutputData(0, m_outputData);
    return true;
}

void SBASReferenceReselectionNode::rollbackOutputTransaction(const QString& reason)
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, iface ? iface->projectXml() : nullptr);
}

bool SBASReferenceReselectionNode::validateAndRestoreOutput()
{
    QStringList outputPaths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, outputPaths) && outputPaths.size() == 1)
    {
        const QString h5Path = outputPaths.first();
        const QString jpgPath = QFileInfo(h5Path).absolutePath() + "/SBAS_time_series.jpg";
        m_outputData = std::make_shared<ImportedFileData>(h5Path, m_outputNodeName);
        setOutputData(0, m_outputData);
        m_resultLabel->setText(QStringLiteral("已恢复现有成果。"));
        
        // Retrieve ref_row and ref_col from H5 if possible
        Mat ref_i, ref_j;
        const bool read_ok = (NodeUtils::readMatFromH5(h5Path, "ref_row", ref_i) &&
                              NodeUtils::readMatFromH5(h5Path, "ref_col", ref_j));
        if (read_ok)
        {
            m_refRow = ref_i.at<int>(0, 0);
            m_refCol = ref_j.at<int>(0, 0);
            // GCP list can't be easily retrieved from H5, but ref row/col is updated.
        }

        updateLabels();

        if (QFile::exists(jpgPath))
        {
            m_previewData = std::make_shared<ImageInfoData>(jpgPath);
            setOutputData(1, m_previewData);
            setState(ExecutionState::Completed);
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
        }
        else
        {
            generateStaticPreviewJpg();
        }
        return true;
    }
    return false;
}

QStringList SBASReferenceReselectionNode::previewImagePaths() const
{
    QStringList outputs;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, outputs) || outputs.size() != 1) {
        return QStringList();
    }
    QStringList paths;
    QString jpgPath = QFileInfo(outputs.first()).absolutePath() + "/SBAS_time_series.jpg";
    if (QFile::exists(jpgPath))
    {
        paths.append(jpgPath);
    }
    return paths;
}

QJsonObject SBASReferenceReselectionNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["refRow"] = m_refRow;
    json["refCol"] = m_refCol;
    json["outputNodeName"] = m_outputNodeName;
    
    QJsonArray gcpArray;
    for (const QPoint& pt : m_GCPs)
    {
        QJsonObject ptJson;
        ptJson["x"] = pt.x();
        ptJson["y"] = pt.y();
        gcpArray.append(ptJson);
    }
    json["GCPs"] = gcpArray;

    return json;
}

void SBASReferenceReselectionNode::load(QJsonObject const& json)
{
    ExecutableNodeDelegateModel::load(json);
    m_refRow = json["refRow"].toInt(-1);
    m_refCol = json["refCol"].toInt(-1);
    m_outputNodeName = json["outputNodeName"].toString();
    
    m_GCPs.clear();
    QJsonArray gcpArray = json["GCPs"].toArray();
    for (int i = 0; i < gcpArray.size(); ++i)
    {
        QJsonObject ptJson = gcpArray[i].toObject();
        m_GCPs.append(QPoint(ptJson["x"].toInt(), ptJson["y"].toInt()));
    }

    if (m_outputNodeNameEdit)
    {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }
    updateLabels();
}

QString SBASReferenceReselectionNode::projectPath() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface)
    {
        return iface->projectPath();
    }
    return QString();
}

QString SBASReferenceReselectionNode::projectName() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface)
    {
        return iface->projectName();
    }
    return QString();
}

} // namespace QtNodes


