#include "PhaseElevationRegressionNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "Utils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QStandardItemModel>
#include <QDebug>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

PhaseElevationRegressionNode::PhaseElevationRegressionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_polyOrderCombo(nullptr)
    , m_windowSizeEdit(nullptr)
    , m_coherenceThreshSpin(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_polyOrder(1)
    , m_windowSize(0)
    , m_coherenceThresh(0.3)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

PhaseElevationRegressionNode::~PhaseElevationRegressionNode()
{
    stopExecution();
}

unsigned int PhaseElevationRegressionNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2; // Port 0: 成果, Port 1: 预览（可选）
}

NodeDataType PhaseElevationRegressionNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return NodeDataType{"imported_file", "Imported File"};
    } else {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool PhaseElevationRegressionNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString PhaseElevationRegressionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("干涉图");
    } else {
        if (portIndex == 0) return QStringLiteral("成果 *");
        else return QStringLiteral("预览 ?");
    }
    return QString();
}

bool PhaseElevationRegressionNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1) return true;
    return false;
}

void PhaseElevationRegressionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port != 0) return;
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_imageInfoData.reset();
    }
}

ProductInputContract PhaseElevationRegressionNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("phase_elevation_regression.input.interferogram");
    contract.allowedProductTypes = QStringList() << QStringLiteral("interferogram");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract PhaseElevationRegressionNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("phase_elevation_regression.output.regressed_interferogram")
        : QStringLiteral("phase_elevation_regression.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("interferogram")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

std::shared_ptr<NodeData> PhaseElevationRegressionNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* PhaseElevationRegressionNode::embeddedWidget()
{
    if (!_widget) createWidget();
    return _widget;
}

QJsonObject PhaseElevationRegressionNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["polyOrder"] = m_polyOrder;
    modelJson["windowSize"] = m_windowSize;
    modelJson["coherenceThresh"] = m_coherenceThresh;
    return modelJson;
}

void PhaseElevationRegressionNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();
    QJsonValue vPoly = json["polyOrder"];
    if (!vPoly.isUndefined()) m_polyOrder = vPoly.toInt();
    QJsonValue vWin = json["windowSize"];
    if (!vWin.isUndefined()) m_windowSize = vWin.toInt();
    QJsonValue vCoh = json["coherenceThresh"];
    if (!vCoh.isUndefined()) m_coherenceThresh = vCoh.toDouble();

    // SOP Rule 15: 先解析参数，再调用基类 load（会触发 validateAndRestoreOutput）
    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_polyOrderCombo) m_polyOrderCombo->setCurrentIndex(m_polyOrder - 1);
    if (m_windowSizeEdit) m_windowSizeEdit->setText(QString::number(m_windowSize));
    if (m_coherenceThreshSpin) m_coherenceThreshSpin->setValue(m_coherenceThresh);
}

void PhaseElevationRegressionNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void PhaseElevationRegressionNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* formLayout = new QFormLayout(_widget);
    formLayout->setContentsMargins(6, 6, 6, 6);
    formLayout->setSpacing(6);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) Q_EMIT _scene->modified(_scene);
    };

    // 多项式阶数
    m_polyOrderCombo = new QComboBox();
    m_polyOrderCombo->setEditable(false);
    m_polyOrderCombo->addItem("1 - 线性");
    m_polyOrderCombo->addItem("2 - 二次");
    m_polyOrderCombo->setCurrentIndex(m_polyOrder - 1);
    connect(m_polyOrderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, invalidateNodeData](int index) {
            int val = index + 1;
            if (m_polyOrder != val) {
                if (!confirmParameterChange()) {
                    m_polyOrderCombo->blockSignals(true);
                    m_polyOrderCombo->setCurrentIndex(m_polyOrder - 1);
                    m_polyOrderCombo->blockSignals(false);
                    return;
                }
                m_polyOrder = val;
                invalidateNodeData();
            }
        });

    // 滑动窗口大小
    m_windowSizeEdit = new QLineEdit();
    m_windowSizeEdit->setText(QString::number(m_windowSize));
    m_windowSizeEdit->setPlaceholderText("0 = 全局回归");
    connect(m_windowSizeEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_windowSizeEdit->text().toInt();
        if (m_windowSize != val) {
            if (!confirmParameterChange()) {
                m_windowSizeEdit->setText(QString::number(m_windowSize));
                return;
            }
            m_windowSize = val;
            invalidateNodeData();
        }
    });

    // 相干性阈值
    m_coherenceThreshSpin = new QDoubleSpinBox();
    m_coherenceThreshSpin->setRange(0.0, 1.0);
    m_coherenceThreshSpin->setSingleStep(0.05);
    m_coherenceThreshSpin->setValue(m_coherenceThresh);
    connect(m_coherenceThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, invalidateNodeData](double val) {
            if (qAbs(m_coherenceThresh - val) > 1e-6) {
                if (!confirmParameterChange()) {
                    m_coherenceThreshSpin->setValue(m_coherenceThresh);
                    return;
                }
                m_coherenceThresh = val;
                invalidateNodeData();
            }
        });

    // 目标节点名称
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });

    formLayout->addRow("回归阶数", m_polyOrderCombo);
    formLayout->addRow("滑动窗口", m_windowSizeEdit);
    formLayout->addRow("相干阈值", m_coherenceThreshSpin);
    formLayout->addRow("目标节点", m_outputNodeNameEdit);
}

void PhaseElevationRegressionNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString PhaseElevationRegressionNode::generateDefaultOutputName() const
{
    if (m_inputData) {
        return m_inputData->nodeName() + "_AtmosReg";
    }
    return "Atmospheric_Regression";
}

bool PhaseElevationRegressionNode::validateInputs() const
{
    if (projectName().isEmpty()) return false;
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;

    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstNode.isEmpty()) return false;
    if (!dstNode.contains(QRegularExpression("^\\w+$"))) return false;

    return true;
}

bool PhaseElevationRegressionNode::prepareToStart()
{
    if (!validateInputs()) return false;

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

    m_preparedDstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName() : m_outputNodeNameEdit->text().trimmed();
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedPolyOrder = m_polyOrder;
    m_preparedWindowSize = m_windowSizeEdit ? m_windowSizeEdit->text().toInt() : m_windowSize;
    m_preparedCoherenceThresh = m_coherenceThreshSpin ? m_coherenceThreshSpin->value() : m_coherenceThresh;
    m_preparedPhasePaths = m_inputData ? m_inputData->filePaths() : QStringList();
    m_preparedPhaseNames.clear();
    m_preparedOutputPaths.clear();
    for (const QString& phasePath : m_preparedPhasePaths) {
        const QString outputName = QFileInfo(phasePath).baseName() + QStringLiteral("_atmos");
        m_preparedPhaseNames.append(QFileInfo(phasePath).baseName());
        m_preparedOutputPaths.append(QDir(m_preparedSavePath).absoluteFilePath(
            m_preparedDstNode + "/" + outputName + ".h5"));
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void PhaseElevationRegressionNode::executeProcessing()
{
    InSARLogManager::LogInfo("PhaseElevationRegressionNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        m_outputNodeNameEdit->setEnabled(true);
        m_polyOrderCombo->setEnabled(true);
        m_windowSizeEdit->setEnabled(true);
        m_coherenceThreshSpin->setEnabled(true);

        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) {
            finishExecution();
        } else {
            onError(QStringLiteral("加载已提交的回归校正输出失败"));
        }
        return;
    }

    setProgress(0);
    setState(ExecutionState::Running);
    if (m_preparedPhasePaths.isEmpty()) { onError(QStringLiteral("没有可校正的干涉图")); return; }
    m_generatedOutputNames.clear(); m_generatedOutputPaths.clear();
    m_generatedOffsetRows.clear(); m_generatedOffsetCols.clear();
    m_xmlDirty = false;

    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode,
                                           m_preparedOutputPaths, m_preparedPhasePaths,
                                           m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), name());
    provenance.insert(QStringLiteral("output_port"), productOutputContract(0).semanticId);
    if (!NodeUtils::setOutputTransactionProductDescriptor(m_outputTransaction,
            ProductDescriptor::create(QStringLiteral("interferogram"),
                productOutputContract(0).schemaId, productOutputContract(0).schemaVersion,
                productOutputContract(0).publishedState, name(), provenance), &transactionError)) {
        onError(transactionError);
        return;
    }

    m_thread = new QThread();
    m_workerThread = new PhaseElevationRegressionWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &PhaseElevationRegressionNode::startRegression, m_workerThread,
            static_cast<void (PhaseElevationRegressionWorker::*)(int, int, double, QString, QStringList, QStringList)>(
                &PhaseElevationRegressionWorker::doRegression));
    connect(m_thread, &QThread::started, this, [this]() {
        if (!m_thread || !m_workerThread || !m_thread->isRunning()) return;
        Q_EMIT startRegression(m_preparedPolyOrder, m_preparedWindowSize, m_preparedCoherenceThresh,
            QDir(m_preparedSavePath).absoluteFilePath(m_outputTransaction.stagingName),
            m_preparedPhaseNames, m_preparedPhasePaths);
    });
    connect(m_workerThread, &PhaseElevationRegressionWorker::updateProcess, this, &PhaseElevationRegressionNode::onProgressUpdate);
    connect(m_workerThread, &PhaseElevationRegressionWorker::outputsGenerated, this,
        [this](const QStringList& names, const QStringList& paths,
               const QList<int>& rows, const QList<int>& cols) {
            m_generatedOutputNames = names; m_generatedOutputPaths = paths;
            m_generatedOffsetRows = rows; m_generatedOffsetCols = cols;
        });
    connect(m_workerThread, &PhaseElevationRegressionWorker::endProcess, this, &PhaseElevationRegressionNode::onProcessingFinished);
    connect(m_workerThread, &PhaseElevationRegressionWorker::errorProcess, this, &PhaseElevationRegressionNode::onError);
    connect(m_workerThread, &PhaseElevationRegressionWorker::cancelled, this, &PhaseElevationRegressionNode::onCancelled);
    connect(m_workerThread, &PhaseElevationRegressionWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &PhaseElevationRegressionWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &PhaseElevationRegressionWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &PhaseElevationRegressionWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &PhaseElevationRegressionWorker::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
            setState(ExecutionState::Running);
    });

    m_outputNodeNameEdit->setEnabled(false);
    m_polyOrderCombo->setEnabled(false);
    m_windowSizeEdit->setEnabled(false);
    m_coherenceThreshSpin->setEnabled(false);

    deferAutomaticCompletion();
    m_thread->start();
}

void PhaseElevationRegressionNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) return;
    setProgress(progress);
}

void PhaseElevationRegressionNode::onProcessingFinished()
{
    const QString dstNode = m_preparedDstNode;
    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;

    releaseFinishedThreadAndWorker();

    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset();
        m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        return;
    }

    if (m_generatedOutputPaths.isEmpty() ||
        m_generatedOutputPaths.size() != m_preparedOutputPaths.size() ||
        m_generatedOutputNames.size() != m_preparedOutputPaths.size() ||
        m_generatedOffsetRows.size() != m_preparedOutputPaths.size() ||
        m_generatedOffsetCols.size() != m_preparedOutputPaths.size()) {
        onError(QStringLiteral("回归校正未生成完整输出结果"));
        return;
    }

    for (int i = 0; i < m_preparedOutputPaths.size(); ++i) {
        if (QFileInfo(m_generatedOutputPaths[i]).fileName() != QFileInfo(m_preparedOutputPaths[i]).fileName() ||
            m_generatedOutputNames[i] != QFileInfo(m_preparedOutputPaths[i]).completeBaseName()) {
            onError(QStringLiteral("回归校正输出名称与事务清单不一致"));
            return;
        }
    }

    QString transactionError;
    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("phase"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError.isEmpty()
                    ? QStringLiteral("回归校正输出事务校验失败")
                    : transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    if (!commitResultsToProjectXml(dstNode, h5Paths, m_generatedOutputNames,
                                  m_generatedOffsetRows, m_generatedOffsetCols) ||
        !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty()
                    ? QStringLiteral("回归校正输出元数据提交失败")
                    : transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    publishResultsToProjectTree(dstNode, h5Paths, m_generatedOutputNames);
    m_outputNodeName = dstNode;
    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(
        m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);

    for (const QString& h5Path : h5Paths) {
        jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg");
        types.append("phase");
    }
    startPreviewGeneration(h5Paths, jpgPaths, types, h5Paths, jpgPaths, true);
}

void PhaseElevationRegressionNode::onError(const QString& error)
{
    releaseFinishedThreadAndWorker();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    if (discardObsoleteAutomaticExecution()) return;

    m_outputNodeNameEdit->setEnabled(true);
    m_polyOrderCombo->setEnabled(true);
    m_windowSizeEdit->setEnabled(true);
    m_coherenceThreshSpin->setEnabled(true);

    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
}

void PhaseElevationRegressionNode::onCancelled()
{
    releaseFinishedThreadAndWorker();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) return;

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    m_outputNodeNameEdit->setEnabled(true);
    m_polyOrderCombo->setEnabled(true);
    m_windowSizeEdit->setEnabled(true);
    m_coherenceThreshSpin->setEnabled(true);
}

bool PhaseElevationRegressionNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return false;

    QStringList h5Paths;
    QStringList expectedJpgPaths;
    QStringList types;
    ProductDescriptor::Ptr descriptor;
    QString identityError;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths) ||
        !NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), dstNode,
                                                          descriptor, &identityError) ||
        !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
        !NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) {
        return false;
    }

    for (const QString& h5Path : h5Paths) {
        const QString baseName = QFileInfo(h5Path).baseName();
        expectedJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);

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
        QMap<QString, QString> previewProvenance;
        previewProvenance.insert(QStringLiteral("producer"), name());
        previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
        m_imageInfoData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
            productOutputContract(1).schemaId, productOutputContract(1).schemaVersion,
            productOutputContract(1).publishedState, name(), previewProvenance));
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    } else {
        startPreviewGeneration(missingH5s, missingJpgs, missingTypes, h5Paths, expectedJpgPaths, false);
    }

    return true;
}

bool PhaseElevationRegressionNode::commitResultsToProjectXml(const QString& outputNodeName,
                                                              const QStringList& h5Paths,
                                                              const QStringList& outputNames,
                                                              const QList<int>& offsetRows,
                                                              const QList<int>& offsetCols)
{
    XMLFile* xml = projectXml();
    if (!xml || h5Paths.size() != outputNames.size() ||
        h5Paths.size() != offsetRows.size() || h5Paths.size() != offsetCols.size())
        return false;

    for (int i = 0; i < h5Paths.size(); ++i) {
        const QString relativePath = QString("/%1/%2").arg(outputNodeName, QFileInfo(h5Paths[i]).fileName());
        xml->XMLFile_add_unwrap(outputNodeName.toStdString().c_str(), outputNames[i].toStdString().c_str(),
            relativePath.toStdString().c_str(), offsetRows[i], offsetCols[i],
            "PhaseElevationRegression", 0);
    }
    m_xmlDirty = true;
    return true;
}

void PhaseElevationRegressionNode::publishResultsToProjectTree(const QString& outputNodeName,
                                                                const QStringList& h5Paths,
                                                                const QStringList& outputNames)
{
    QStandardItemModel* model = projectModel();
    if (!model || h5Paths.size() != outputNames.size()) return;

    const QList<QStandardItem*> projects = model->findItems(projectName());
    if (projects.isEmpty()) return;

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), outputNodeName, "phase-2.5", FOLDER_ICON);
    if (!outputNode) return;
    for (int i = 0; i < h5Paths.size(); ++i) {
        NodeUtils::findOrCreateChildItem(outputNode, outputNames[i], "phase", h5Paths[i], IMAGEDATA_ICON);
    }
    if (auto* iface = NodeUtils::getProjectContext(_widget)) iface->refreshProjectTree();
}

void PhaseElevationRegressionNode::startPreviewGeneration(const QStringList& h5Paths,
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
        if (completeExecution && discardObsoleteAutomaticExecution()) {
            NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                                QStringLiteral("obsolete automatic execution"), projectXml());
            m_outputData.reset();
            m_imageInfoData.reset();
            setOutputData(0, nullptr);
            setOutputData(1, nullptr);
            return;
        }
        m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
        QMap<QString, QString> previewProvenance;
        previewProvenance.insert(QStringLiteral("producer"), name());
        previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
        m_imageInfoData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
            productOutputContract(1).schemaId, productOutputContract(1).schemaVersion,
            productOutputContract(1).publishedState, name(), previewProvenance));
        setOutputData(1, m_imageInfoData);
        if (completeExecution) {
            m_outputNodeNameEdit->setEnabled(true);
            m_polyOrderCombo->setEnabled(true);
            m_windowSizeEdit->setEnabled(true);
            m_coherenceThreshSpin->setEnabled(true);
            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("PhaseElevationRegressionNode", "处理完成.");
            finishExecution();
        } else {
            Q_EMIT dataUpdated(1);
        }
    });
    QFuture<void> future = QtConcurrent::run([h5Paths, generatedJpgPaths, types]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], generatedJpgPaths[i], types[i]);
        }
    });
    m_remedyWatcher.setFuture(future);
}

QStringList PhaseElevationRegressionNode::previewImagePaths() const
{
    return m_imageInfoData ? m_imageInfoData->filePaths() : QStringList();
}

QStandardItemModel* PhaseElevationRegressionNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString PhaseElevationRegressionNode::projectPath() const
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

QString PhaseElevationRegressionNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* PhaseElevationRegressionNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void PhaseElevationRegressionNode::cleanUpThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (!thread)
        return;
    thread->quit();
    if (thread->isRunning()) {
        thread->wait();
    }
}

void PhaseElevationRegressionNode::releaseFinishedThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

void PhaseElevationRegressionNode::execute()
{
    executeProcessing();
}

void PhaseElevationRegressionNode::stopExecution()
{
    if (m_workerThread || m_thread) {
        if (m_workerThread) {
            m_workerThread->disconnect(this);
        }
        if (m_thread) {
            m_thread->requestInterruption();
        }
        cleanUpThreadAndWorker();
        onCancelled();
        return;
    }

    if (!m_remedyWatcher.isRunning()) return;

    m_remedyWatcher.disconnect(this);
    m_remedyWatcher.cancel();
    NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                        QStringLiteral("preview generation cancelled"), projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) return;

    m_outputNodeNameEdit->setEnabled(true);
    m_polyOrderCombo->setEnabled(true);
    m_windowSizeEdit->setEnabled(true);
    m_coherenceThreshSpin->setEnabled(true);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void PhaseElevationRegressionNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

} // namespace QtNodes

