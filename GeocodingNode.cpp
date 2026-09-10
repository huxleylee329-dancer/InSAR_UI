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
#include "QtNodes/internal/NodeDetailWindow.hpp"
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
#include <QPointer>
#include <cmath>

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
    QPointer<GeocodingNode> self(this);
    NodeUtils::registerResourceChangeCallback([self](const QString& resourceId, const QString& provenanceId, NodeUtils::ResourceChangeKind kind) {
        if (self) QTimer::singleShot(0, self.data(), [self]() { if (self) self->refreshAuxiliaryDemLabels(); });
        if (!self || self->m_preparedAuxiliaryDemBinding.resourceId != resourceId) return;
        if (kind == NodeUtils::ResourceChangeKind::ProvenanceAdded && provenanceId != self->m_preparedAuxiliaryDemBinding.pinnedProvenanceId) return;
        QTimer::singleShot(0, self.data(), [self]() {
            if (!self) return;
            self->m_preparedAuxiliaryDemBinding = NodeUtils::AuxiliaryDemBinding();
            self->m_preparedDemExecutionSnapshot = NodeUtils::DemExecutionSnapshot();
            self->m_preparedDemPath.clear();
            self->setProgress(0);
            self->setLastErrorMessage(QStringLiteral("辅助 DEM 资源已变化，需要重新准备。"));
            self->setState(ExecutionState::Pending);
        });
    });
    NodeUtils::registerAuxiliaryDemLabelTableChangedCallback([self]() {
        if (self) QTimer::singleShot(0, self.data(), [self]() { if (self) self->refreshAuxiliaryDemLabels(); });
    });
    NodeUtils::registerAuxiliaryDemLabelReboundCallback([self](const QString& label) {
        if (self && self->m_auxiliaryDemLabel == label) QTimer::singleShot(0, self.data(), [self]() { if (self) { self->m_preparedAuxiliaryDemBinding = NodeUtils::AuxiliaryDemBinding(); self->m_preparedDemExecutionSnapshot = NodeUtils::DemExecutionSnapshot(); self->setProgress(0); self->setState(ExecutionState::Pending); QMap<QString, NodeUtils::AuxiliaryDemLabelBinding> labels; const auto result = NodeUtils::loadAuxiliaryDemLabels(self->projectPath(), labels); const auto binding = labels.value(self->m_auxiliaryDemLabel); if (result && binding.mode == NodeUtils::AuxiliaryDemLabelMode::WorkflowOutput && !binding.isPlanned()) self->retryAutomaticExecution(); } });
    });
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
            return NodeDataType{"auxiliary_dem", "Auxiliary DEM"};
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
    if (portType == PortType::In && portIndex == 1)
        return true;
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

QList<QList<PortIndex>> GeocodingNode::alternativeInputGroups() const
{
    return {};
}

void GeocodingNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_insarDemInputData = std::dynamic_pointer_cast<InsarDemData>(data);
        if (m_insarDemInputData && !m_insarDemInputData->h5Paths().isEmpty()) {
            m_inputData = std::make_shared<ImportedFileData>(
                m_insarDemInputData->h5Paths(), QStringLiteral("DEM Generation"));
            m_inputData->setProductDescriptor(m_insarDemInputData->productDescriptor());
        } else {
            m_insarDemInputData.reset();
            m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
        }

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

        if (m_inputData && m_outputNodeName.trimmed().isEmpty()) {
            m_outputNodeName = generateDefaultOutputName();
            if (m_outputNodeNameEdit) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
        }
    } else if (port == 1) {
        m_auxiliaryDemInputData = std::dynamic_pointer_cast<AuxiliaryDemData>(data);
        if (m_auxiliaryDemInputData) {
            // A physical wire is authoritative for this input mode.  Drop a
            // previously resolved label reference so completion never sees
            // two bindings.
            m_auxiliaryDemReferenceData.reset();
            m_auxiliaryDemLabel.clear();
            if (m_demLabelCombo) m_demLabelCombo->setCurrentIndex(0);
        }
        if (m_auxiliaryDemInputData) {
            m_demPath = m_auxiliaryDemInputData->rasterPath();
        } else {
            if (!isRestoring()) {
                m_demPath.clear();
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
    modelJson[QStringLiteral("auxiliaryDemLabel")] = m_auxiliaryDemLabel;
    modelJson[QStringLiteral("auxiliaryDemLegacyResourceId")] = m_legacyDemResourceId;
    modelJson[QStringLiteral("auxiliaryDemLegacyPinnedProvenanceId")] = m_legacyDemProvenanceId;

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

    m_auxiliaryDemLabel = json.value(QStringLiteral("auxiliaryDemLabel")).toString().trimmed();
    m_legacyDemResourceId = json.value(QStringLiteral("auxiliaryDemLegacyResourceId")).toString().trimmed();
    m_legacyDemProvenanceId = json.value(QStringLiteral("auxiliaryDemLegacyPinnedProvenanceId")).toString().trimmed();

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
    refreshAuxiliaryDemLabels();
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

    // Label binding is a project-level alias to a managed resource.  The
    // resolved path is observable but never editable here.
    auto* demRow = new QHBoxLayout();
    m_demPathLabel = new QLabel(QStringLiteral("辅助 DEM:"));
    m_demPathLabel->setFixedWidth(80);
    m_demPathLabel->setStyleSheet("QLabel:disabled { color: #888888; }");
    
    m_demLabelCombo = new QComboBox();
    refreshAuxiliaryDemLabels();
    connect(m_demLabelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const QString nextLabel = index > 0 ? m_demLabelCombo->itemData(index).toString() : QString();
        if (!nextLabel.isEmpty() && hasActiveInputConnection(1)) {
            QMessageBox::warning(nullptr, QStringLiteral("辅助 DEM"),
                                 QStringLiteral("请先断开辅助 DEM 输入端口的直连，再选择工程标签。"));
            m_demLabelCombo->blockSignals(true);
            const int previousIndex = m_demLabelCombo->findData(m_auxiliaryDemLabel);
            m_demLabelCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
            m_demLabelCombo->blockSignals(false);
            return;
        }
        m_auxiliaryDemLabel = nextLabel;
        if (!m_auxiliaryDemLabel.isEmpty()) {
            // Selecting a label intentionally changes binding mode.  The
            // graph connection remains visible but cannot participate until
            // it is removed; prepareToStart still rejects that ambiguity.
            m_auxiliaryDemInputData.reset();
            m_auxiliaryDemReferenceData.reset();
        }
        m_demPath.clear();
        setProgress(0);
        setState(ExecutionState::Pending);
    });

    demRow->addWidget(m_demPathLabel);
    demRow->addWidget(m_demLabelCombo);
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
        QString sourceName = m_inputData->nodeName().trimmed();
        sourceName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
        sourceName.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
        if (!sourceName.isEmpty()) {
            return sourceName + QStringLiteral("_geocoded");
        }
    }
    return QStringLiteral("Geocoded");
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
    QJsonObject inputGeometry;
    if (m_inputData) inputGeometry = NodeUtils::inputGeometryFromProductDescriptor(m_inputData->physicalProductDescriptor());
    if (!validateInputs())
    {
        const QString outputName = m_outputNodeNameEdit
            ? m_outputNodeNameEdit->text().trimmed()
            : m_outputNodeName.trimmed();
        if (!m_inputData || m_inputData->filePaths().isEmpty()) {
            setStartFailureMessage(QStringLiteral("待地理编码产品尚未就绪。"));
        } else if (outputName.isEmpty()) {
            setStartFailureMessage(QStringLiteral("地理编码输出节点名为空。"));
        } else {
            setStartFailureMessage(QStringLiteral("地理编码输出节点名只能包含字母、数字或下划线：%1").arg(outputName));
        }
        return false;
    }

    const QString dstNode = m_outputNodeNameEdit
        ? m_outputNodeNameEdit->text().trimmed()
        : m_outputNodeName.trimmed();

    QString savePath = projectPath();

    m_preparedType = m_typeCombo ? m_typeCombo->currentIndex() + 1 : m_type;
    m_preparedMultiRg = m_multiRgSpin ? m_multiRgSpin->value() : m_multiRg;
    m_preparedMultiAz = m_multiAzSpin ? m_multiAzSpin->value() : m_multiAz;
    if (hasActiveInputConnection(1) && !m_auxiliaryDemLabel.isEmpty()) {
        setStartFailureMessage(QStringLiteral("辅助 DEM 不能同时使用直接连线和命名标签。"));
        return false;
    }
    if (!m_auxiliaryDemInputData && m_auxiliaryDemLabel.isEmpty()) {
        setStartFailureMessage(QStringLiteral("辅助 DEM 是必需输入，必须通过直接连线或工程标签提供。"));
        return false;
    }
    QString resolvedDemPath;
    if (m_auxiliaryDemInputData) {
        NodeUtils::AuxiliaryDemBinding binding;
        QString error;
        if (!NodeUtils::resolveAuxiliaryDemBinding(projectPath(), *m_auxiliaryDemInputData, binding, &error, inputGeometry)) {
            setStartFailureMessage(error);
            setLastErrorMessage(error);
            return false;
        }
        m_preparedAuxiliaryDemBinding = binding;
        m_preparedDemExecutionSnapshot.binding = binding;
        m_preparedDemExecutionSnapshot.inputGeometry = inputGeometry;
        resolvedDemPath = binding.rasterPath;
    } else if (!m_auxiliaryDemLabel.isEmpty()) {
        if (!m_legacyDemResourceId.isEmpty() && !m_legacyDemProvenanceId.isEmpty())
            NodeUtils::registerPendingAuxiliaryDemLabel({m_auxiliaryDemLabel, m_legacyDemResourceId, m_legacyDemProvenanceId});
        NodeUtils::AuxiliaryDemBinding binding;
        QString error;
        if (!NodeUtils::resolveAuxiliaryDemLabel(projectPath(), m_auxiliaryDemLabel, binding, &error, inputGeometry)) {
            setStartFailureMessage(error);
            setLastErrorMessage(error);
            return false;
        }
        m_preparedAuxiliaryDemBinding = binding;
        m_preparedDemExecutionSnapshot.binding = binding;
        m_preparedDemExecutionSnapshot.inputGeometry = inputGeometry;
        // Keep the resolved managed reference for completion-time snapshot
        // revalidation.  The label itself is not a path-bearing input.
        m_auxiliaryDemReferenceData = std::make_shared<AuxiliaryDemReferenceData>(
            binding.resourceId, binding.pinnedProvenanceId, 1);
        resolvedDemPath = binding.rasterPath;
    }
    if (resolvedDemPath.isEmpty() || !QFileInfo(resolvedDemPath).isFile()) {
        const QString demPathError = QStringLiteral("辅助 DEM 栅格文件不存在：%1")
                                        .arg(resolvedDemPath);
        setStartFailureMessage(demPathError);
        setLastErrorMessage(demPathError);
        return false;
    }
    m_preparedDemPath = resolvedDemPath;
    m_preparedDstNode = dstNode;

    m_preparedInputPaths = m_inputData->filePaths();
    if (m_insarDemInputData) {
        QString demResolveError;
        if (!resolveInsarDemProduct(m_insarDemInputData, &m_preparedInputPaths, &demResolveError)) {
            const QString error = demResolveError.isEmpty()
                ? QStringLiteral("InSAR DEM 产品不可执行：缺少有效 H5、manifest/runId 或几何 metadata。")
                : QStringLiteral("InSAR DEM 产品不可执行：%1").arg(demResolveError);
            setStartFailureMessage(error);
            setLastErrorMessage(error);
            return false;
        }
    }
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_preparedInputPaths, m_inputData->physicalProductDescriptor(),
                                         &identityError)) {
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }
    m_preparedProductLevel.clear();
    m_preparedMasterIndex = 0;

    const QString srcNode = m_inputData->nodeName();
    if (m_insarDemInputData) {
        // The generated DEM is a graph product, not a project-tree DataNode.
        // Its output type fixes the staged output dataset independently of the
        // display name assigned while adapting InsarDemData to ImportedFileData.
        m_preparedProductLevel = QStringLiteral("dem-1.0");
    } else {
        QStandardItemModel* model = projectModel();
        if (!model) {
            setStartFailureMessage(QStringLiteral("工程数据模型不可用，无法解析地理编码输入。"));
            return false;
        }
        QList<QStandardItem*> projects = model->findItems(projectName());
        if (projects.isEmpty()) {
            setStartFailureMessage(QStringLiteral("工程节点不可用，无法解析地理编码输入。"));
            return false;
        }

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
            setStartFailureMessage(QStringLiteral("无法在工程树中解析地理编码输入：%1").arg(srcNode));
            return false;
        }
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
            setStartFailureMessage(QStringLiteral("SAR 地理编码主图索引无效。"));
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
            m_auxiliaryDemInputData.reset();
            setState(ExecutionState::Error);
        }
        return;
    }

    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(savePath, dstNode, m_preparedOutputPaths,
                                           inputPaths, m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
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
    if (m_auxiliaryDemInputData || m_auxiliaryDemReferenceData) {
        NodeUtils::AuxiliaryDemBinding currentBinding;
        QString bindingError;
        if (!NodeUtils::revalidateDemExecutionSnapshot(projectPath(), m_auxiliaryDemInputData.get(),
                                                        m_auxiliaryDemReferenceData.get(), m_preparedDemExecutionSnapshot,
                                                        NodeUtils::inputGeometryFromProductDescriptor(
                                                            m_inputData ? m_inputData->physicalProductDescriptor() : nullptr),
                                                        currentBinding, &bindingError)) {
            NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete auxiliary DEM binding"), projectXml());
            setLastErrorMessage(bindingError);
            setState(ExecutionState::Error);
            return;
        }
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

bool GeocodingNode::resolveInsarDemProduct(const std::shared_ptr<InsarDemData>& data,
                                           QStringList* resolvedPaths,
                                           QString* errorMessage) const
{
    if (!data) return false;
    QStringList paths;
    if (!NodeUtils::resolveInsarDemProduct(*data, paths, errorMessage)) return false;
    if (resolvedPaths) *resolvedPaths = paths;
    return true;
}

ProductInputContract GeocodingNode::productInputContract(PortIndex portIndex) const
{
    ProductInputContract contract;
    if (portIndex == 0) {
        contract.semanticId = QStringLiteral("geocoding.input.radar_product");
        contract.allowedProductTypes = QStringList{QStringLiteral("unwrapped_phase"), QStringLiteral("insar_dem")};
    } else if (portIndex == 1) {
        contract.semanticId = QStringLiteral("geocoding.input.auxiliary_terrain_dem.entity");
        contract.optional = true;
        contract.allowedProductTypes = QStringList{QStringLiteral("auxiliary_terrain_dem")};
    } else {
        contract.semanticId = QStringLiteral("geocoding.input.auxiliary_terrain_dem.reference");
        contract.optional = true;
        contract.allowedProductTypes = QStringList{QStringLiteral("auxiliary_terrain_dem")};
    }
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
        if (xml->XMLFile_add_geocoding(result.dstNode.toStdString().c_str(), result.geocodeName.toStdString().c_str(),
                result.relativePath.toStdString().c_str(), result.rankLevel.toStdString().c_str()) < 0 ||
            (result.rankLevel == QStringLiteral("coherence-1.1") &&
             xml->XMLFile_set_coherence_semantics(
                 result.dstNode.toStdString().c_str(), result.geocodeName.toStdString().c_str(),
                 result.coherenceSemantics.toStdString().c_str()) < 0)) {
            return;
        }
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

    for (const QString& h5Path : h5Paths) {
        QString baseName = QFileInfo(h5Path).baseName();
        expectedJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + ".jpg");

        // Determine type dynamically from datasets
        QString type = "amplitude";
        {
            if (NodeUtils::probeH5DatasetMetadata(h5Path, QStringLiteral("phase"))) {
                type = "phase";
            } else if (NodeUtils::probeH5DatasetMetadata(h5Path, QStringLiteral("coherence"))) {
                type = "coherence";
            } else if (NodeUtils::probeH5DatasetMetadata(h5Path, QStringLiteral("dem"))) {
                type = "dem";
            } else if (NodeUtils::probeH5DatasetMetadata(h5Path, QStringLiteral("defomation_velocity"))) {
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
    if (_widget)
    {
        iface = NodeUtils::getProjectContext(_widget);
    }
    if (!iface)
    {
        for (QWidget* w : QApplication::topLevelWidgets())
        {
            if (auto* mainWin = qobject_cast<MainWindow*>(w))
            {
                if (mainWin->workspaceUI())
                {
                    iface = mainWin->workspaceUI();
                    break;
                }
                if (mainWin->interfaceManager())
                {
                    iface = mainWin->interfaceManager()->currentInterface();
                    if (iface)
                    {
                        break;
                    }
                }
            }
        }
    }
    if (iface)
    {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive))
        {
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
        QMap<QString, NodeUtils::AuxiliaryDemLabelBinding> labels;
        bool isPlannedLabel = false;
        if (!m_auxiliaryDemLabel.isEmpty() && NodeUtils::loadAuxiliaryDemLabels(projectPath(), labels)) {
            const auto labelBinding = labels.value(NodeUtils::normalizedDemLabel(m_auxiliaryDemLabel));
            if (labelBinding.isPlanned()) {
                isPlannedLabel = true;
            }
        }
        // A connected upstream product or DEM producer may publish its output
        // after this automatic attempt.  Keep the node Pending until the input
        // update retries preparation.
        const bool waitingForUpstreamProduct =
            !m_inputData && hasActiveInputConnection(0);
        const bool waitingForDemProducer =
            !m_auxiliaryDemInputData && hasActiveInputConnection(1);
        if (isPlannedLabel || waitingForUpstreamProduct || waitingForDemProducer) {
            setStartFailureMessage(QString());
            setState(ExecutionState::Pending);
        } else {
            setLastErrorMessage(_startFailureMessage.isEmpty()
                ? QStringLiteral("自动执行前置条件无效。")
                : _startFailureMessage);
            setState(ExecutionState::Error);
        }
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

    if (m_demLabelCombo) m_demLabelCombo->setEnabled(enableWidgets);
    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets);
}

void GeocodingNode::refreshAuxiliaryDemLabels()
{
    if (!m_demLabelCombo) return;
    const QSet<QString> declared = workflowDeclaredDemLabels();
    const QHash<QString, QString> producerNodeIds = workflowDemProducerNodeIdMap();
    QMap<QString, NodeUtils::AuxiliaryDemLabelBinding> labels;
    QString error;
    NodeUtils::loadAuxiliaryDemLabels(projectPath(), labels, &error);
    m_demLabelCombo->blockSignals(true);
    m_demLabelCombo->clear();
    m_demLabelCombo->addItem(QStringLiteral("不使用标签"), QString());
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        if (it.value().mode != NodeUtils::AuxiliaryDemLabelMode::WorkflowOutput) continue;
        const QString normalized = NodeUtils::normalizedDemLabel(it.value().label);
        if (declared.contains(normalized)) {
            const QString status = it.value().isPlanned() ? QStringLiteral("待生成") : QStringLiteral("就绪");
            const QString producerNode = producerNodeIds.value(it.value().producerIdentity);
            if (producerNode.isEmpty()) {
                m_demLabelCombo->addItem(QStringLiteral("@%1 (%2)").arg(it.value().label, status), it.value().label);
            } else {
                m_demLabelCombo->addItem(QStringLiteral("@%1 (%2, 节点 %3)").arg(it.value().label, status, producerNode), it.value().label);
            }
        } else if (NodeUtils::normalizedDemLabel(m_auxiliaryDemLabel) == normalized) {
            // 当前选中但无人声明的标签，保留显示并标记"无生产者"，避免选中项凭空消失
            m_demLabelCombo->addItem(QStringLiteral("@%1 (无生产者)").arg(it.value().label), it.value().label);
        }
    }
    if (!labels.isEmpty()) m_demLabelCombo->insertSeparator(m_demLabelCombo->count());
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        if (it.value().mode != NodeUtils::AuxiliaryDemLabelMode::FixedResource) continue;
        m_demLabelCombo->addItem(QStringLiteral("@%1 (已注册资源)").arg(it.value().label), it.value().label);
    }
    const int index = m_demLabelCombo->findData(m_auxiliaryDemLabel);
    m_demLabelCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_demLabelCombo->blockSignals(false);
}

QString GeocodingNode::portBindingSummary(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1) {
        if (!m_auxiliaryDemLabel.isEmpty()) {
            QMap<QString, NodeUtils::AuxiliaryDemLabelBinding> labels;
            QString error;
            if (NodeUtils::loadAuxiliaryDemLabels(projectPath(), labels, &error)) {
                const auto binding = labels.value(NodeUtils::normalizedDemLabel(m_auxiliaryDemLabel));
                if (!binding.label.isEmpty()) {
                    QString detail;
                    if (binding.mode == NodeUtils::AuxiliaryDemLabelMode::WorkflowOutput) {
                        const QString status = binding.isPlanned() ? QStringLiteral("待生成") : QStringLiteral("就绪");
                        const QString producerNode = workflowDemProducerNodeIdMap().value(binding.producerIdentity);
                        detail = producerNode.isEmpty()
                            ? status
                            : QStringLiteral("节点 %1，%2").arg(producerNode, status);
                    } else {
                        detail = QStringLiteral("已注册资源");
                    }
                    return QStringLiteral("已绑定标签 @%1（%2）").arg(binding.label, detail);
                }
            }
            const QString normalized = NodeUtils::normalizedDemLabel(m_auxiliaryDemLabel);
            return QStringLiteral("已绑定标签 @%1（待解析/未找到对应节点）").arg(normalized);
        }
    }
    return QString();
}

std::vector<QString> GeocodingNode::processingInfo() const
{
    std::vector<QString> info;

    // 1. 输入影像数
    const int inCount = m_inputData ? m_inputData->filePaths().size() : 0;
    info.push_back(QStringLiteral("输入影像数：%1 景").arg(inCount));

    // 2. 输出 H5 清单与数量
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty() && m_outputNodeNameEdit) {
        dstNode = m_outputNodeNameEdit->text().trimmed();
    }
    QStringList h5Paths;
    if (!dstNode.isEmpty()) {
        NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths);
    }
    if (h5Paths.isEmpty() && m_outputData) {
        h5Paths = m_outputData->filePaths();
    }

    if (!h5Paths.isEmpty()) {
        QString fileListStr;
        const int showMax = 3;
        for (int i = 0; i < qMin(h5Paths.size(), showMax); ++i) {
            if (i > 0) fileListStr += QStringLiteral(", ");
            fileListStr += QFileInfo(h5Paths[i]).fileName();
        }
        if (h5Paths.size() > showMax) {
            fileListStr += QStringLiteral(" 等 %1 个文件").arg(h5Paths.size());
        }
        info.push_back(QStringLiteral("输出 H5 成果：%1 景 (%2)").arg(h5Paths.size()).arg(fileListStr));
    } else {
        info.push_back(QStringLiteral("输出 H5 成果：0 景 (尚未生成)"));
    }

    // 3. 预览 JPG 是否齐全
    const QStringList jpgs = previewImagePaths();
    if (!h5Paths.isEmpty()) {
        info.push_back(QStringLiteral("预览图生成状态：%1 / %2 景")
            .arg(jpgs.size()).arg(h5Paths.size()));
    }

    // 4. 辅助 DEM 实际解析路径与绑定情况
    QString demInfo;
    if (!m_preparedDemPath.isEmpty()) {
        demInfo = QFileInfo(m_preparedDemPath).fileName();
    } else if (!m_demPath.isEmpty()) {
        demInfo = QFileInfo(m_demPath).fileName();
    }
    if (!m_auxiliaryDemLabel.isEmpty()) {
        if (demInfo.isEmpty()) {
            info.push_back(QStringLiteral("辅助 DEM 绑定：标签 @%1").arg(m_auxiliaryDemLabel));
        } else {
            info.push_back(QStringLiteral("辅助 DEM 绑定：标签 @%1 (%2)").arg(m_auxiliaryDemLabel, demInfo));
        }
    } else if (!demInfo.isEmpty()) {
        info.push_back(QStringLiteral("辅助 DEM 文件：%1").arg(demInfo));
    } else {
        info.push_back(QStringLiteral("辅助 DEM 状态：未绑定"));
    }

    // 5. 执行状态及错误/告警
    QString stateStr;
    switch (executionState()) {
    case ExecutionState::Idle:      stateStr = QStringLiteral("空闲"); break;
    case ExecutionState::Pending:   stateStr = QStringLiteral("等待执行"); break;
    case ExecutionState::Running:   stateStr = QStringLiteral("正在执行 (%1%)").arg(progress()); break;
    case ExecutionState::Completed: stateStr = QStringLiteral("执行完成"); break;
    case ExecutionState::Stopped:   stateStr = QStringLiteral("已停止"); break;
    case ExecutionState::Warning:   stateStr = QStringLiteral("警告"); break;
    case ExecutionState::Error:     stateStr = QStringLiteral("错误"); break;
    case ExecutionState::Disabled:  stateStr = QStringLiteral("已禁用"); break;
    default:                        stateStr = QStringLiteral("未知"); break;
    }
    info.push_back(QStringLiteral("节点状态：%1").arg(stateStr));

    if (!lastErrorMessage().isEmpty()) {
        info.push_back(QStringLiteral("错误信息：%1").arg(lastErrorMessage()));
    }
    if (!lastWarningMessage().isEmpty()) {
        info.push_back(QStringLiteral("告警信息：%1").arg(lastWarningMessage()));
    }

    return info;
}

QString GeocodingNode::expectedOutputProductLevel() const
{
    const int curType = m_typeCombo ? m_typeCombo->currentIndex() + 1 : m_type;
    if (curType == 2) {
        return QStringLiteral("amplitude-1.1");
    }

    QString inLevel = m_preparedProductLevel;
    if (inLevel.isEmpty() && m_insarDemInputData) {
        inLevel = QStringLiteral("dem-1.0");
    }
    if (inLevel.isEmpty() && m_inputData) {
        const QString srcNode = m_inputData->nodeName();
        QStandardItemModel* model = projectModel();
        if (model) {
            QList<QStandardItem*> projects = model->findItems(projectName());
            if (!projects.isEmpty()) {
                QStandardItem* project = projects.first();
                for (int i = 0; i < project->rowCount(); ++i) {
                    if (project->child(i, 0) && project->child(i, 0)->text() == srcNode) {
                        if (project->child(i, 1)) {
                            inLevel = project->child(i, 1)->text();
                        }
                        break;
                    }
                }
            }
        }
    }

    if (inLevel == QStringLiteral("phase-1.0")) return QStringLiteral("phase-1.1");
    if (inLevel == QStringLiteral("phase-2.0")) return QStringLiteral("phase-2.1");
    if (inLevel == QStringLiteral("phase-3.0")) return QStringLiteral("phase-3.1");
    if (inLevel == QStringLiteral("coherence-1.0")) return QStringLiteral("coherence-1.1");
    if (inLevel == QStringLiteral("dem-1.0")) return QStringLiteral("dem-1.1");
    if (inLevel == QStringLiteral("SBAS-1.0")) return QStringLiteral("SBAS-1.1");

    if (!inLevel.isEmpty() && inLevel.endsWith(QStringLiteral("-1.0"))) {
        return inLevel.left(inLevel.size() - 4) + QStringLiteral("-1.1");
    }
    return QString();
}

QString GeocodingNode::effectiveAuxiliaryDemFileName() const
{
    if (!m_preparedDemPath.isEmpty()) {
        return QFileInfo(m_preparedDemPath).fileName();
    }
    if (!m_preparedAuxiliaryDemBinding.rasterPath.isEmpty()) {
        return QFileInfo(m_preparedAuxiliaryDemBinding.rasterPath).fileName();
    }
    if (!m_demPath.isEmpty()) {
        return QFileInfo(m_demPath).fileName();
    }
    if (!m_auxiliaryDemLabel.isEmpty()) {
        NodeUtils::AuxiliaryDemBinding binding;
        if (NodeUtils::resolveAuxiliaryDemLabel(projectPath(), m_auxiliaryDemLabel, binding)) {
            if (!binding.rasterPath.isEmpty()) {
                return QFileInfo(binding.rasterPath).fileName();
            }
        }
    }
    return QString();
}

QString GeocodingNode::validationCacheKey() const
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty() && m_outputNodeNameEdit) {
        dstNode = m_outputNodeNameEdit->text().trimmed();
    }
    QStringList outPaths;
    if (!dstNode.isEmpty()) {
        NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, outPaths);
    }
    if (outPaths.isEmpty() && m_outputData) {
        outPaths = m_outputData->filePaths();
    }
    if (outPaths.isEmpty()) {
        return QString();
    }

    const auto fileFingerprint = [](const QString& path) {
        const QFileInfo info(path);
        if (!info.exists()) {
            return QStringLiteral("missing");
        }
        return QString::number(info.size()) + QLatin1Char('_')
            + QString::number(info.lastModified().toMSecsSinceEpoch());
    };

    QString key;
    for (const QString& outPath : outPaths) {
        key += outPath + QLatin1Char('@') + fileFingerprint(outPath) + QLatin1Char('|');
    }

    const int curType = m_typeCombo ? m_typeCombo->currentIndex() + 1 : m_type;
    const int curRg = m_multiRgSpin ? m_multiRgSpin->value() : m_multiRg;
    const int curAz = m_multiAzSpin ? m_multiAzSpin->value() : m_multiAz;
    const QString expLevel = expectedOutputProductLevel();
    const QString expDem = effectiveAuxiliaryDemFileName();

    key += QLatin1String("##") + QString::number(curType)
        + QLatin1String("##") + QString::number(curRg)
        + QLatin1String("##") + QString::number(curAz)
        + QLatin1String("##") + expLevel
        + QLatin1String("##") + expDem;
    return key;
}

bool GeocodingNode::loadValidationCache(const QString& key, GeocodingValidationResults& results) const
{
    const auto it = m_validationCache.constFind(key);
    if (it == m_validationCache.constEnd()) {
        return false;
    }
    results = it.value();
    return true;
}

void GeocodingNode::storeValidationCache(const QString& key, const GeocodingValidationResults& results)
{
    if (!key.isEmpty()) {
        m_validationCache.insert(key, results);
    }
}

// ==========================================
// GeocodingValidationWidget Implementation
// ==========================================

class GeocodingValidationWidget : public BaseValidationWidget
{
public:
    GeocodingValidationWidget(GeocodingNode* node, QWidget* parent)
        : BaseValidationWidget(node, parent)
        , m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }

    ~GeocodingValidationWidget() override = default;

private:
    void setupUI()
    {
        setupBaseUI(QObject::tr("正在验证地理编码数据..."),
                    QObject::tr("正在读取输出 H5 成果以校验地理范围、多视参数、产品级别及图像有效性。"),
                    QObject::tr("地理编码与图像特征诊断"),
                    QObject::tr("参数比对与验证"));

        m_lblLonRange = createFeatureLabel();
        m_lblLatRange = createFeatureLabel();
        m_lblLonSpan = createFeatureLabel();
        m_lblLatSpan = createFeatureLabel();
        m_lblDimensions = createFeatureLabel();
        m_lblValidPixels = createFeatureLabel();
        m_lblDatasetSummary = createFeatureLabel();

        m_featureLayout->addRow(createHeaderLabel(QObject::tr("经度覆盖范围 [西, 东]:")), m_lblLonRange);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("纬度覆盖范围 [南, 北]:")), m_lblLatRange);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("经度跨度 (度):")), m_lblLonSpan);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("纬度跨度 (度):")), m_lblLatSpan);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("栅格尺寸 (行 x 列):")), m_lblDimensions);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("有效像元比例 (非空/有效值):")), m_lblValidPixels);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("输出成果与数据集:")), m_lblDatasetSummary);
    }

    void setNotExecutedState()
    {
        m_statusTitle->setText(QObject::tr("验证不可用"));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
        m_statusDesc->setText(QObject::tr("请先成功执行 Geocoding 节点生成成果后再查看验证结果。"));

        m_compTable->clearComparison();
        m_compTable->setEnabled(false);

        m_lblLonRange->setText(QObject::tr("未执行"));
        m_lblLatRange->setText(QObject::tr("未执行"));
        m_lblLonSpan->setText(QObject::tr("未执行"));
        m_lblLatSpan->setText(QObject::tr("未执行"));
        m_lblDimensions->setText(QObject::tr("未执行"));
        m_lblValidPixels->setText(QObject::tr("未执行"));
        m_lblDatasetSummary->setText(QObject::tr("未执行"));
    }

    void startAsyncValidation() override
    {
        const quint64 currentEpoch = ++m_validationEpoch;
        if (m_cancelToken) {
            m_cancelToken->store(true);
        }
        m_cancelToken = std::make_shared<std::atomic_bool>(false);
        auto cancelToken = m_cancelToken;

        m_isTimedOut = false;

        if (m_node->executionState() != ExecutionState::Completed) {
            setNotExecutedState();
            return;
        }

        // 获取输出路径
        QString dstNode = m_node->save().value("outputNodeName").toString().trimmed();
        if (dstNode.isEmpty()) {
            dstNode = m_node->name();
        }
        QStringList outPaths;
        if (!dstNode.isEmpty()) {
            NodeUtils::loadCommittedOutputManifest(m_node->projectPath(), dstNode, outPaths);
        }
        if (outPaths.isEmpty()) {
            auto outData = std::dynamic_pointer_cast<ImportedFileData>(m_node->outData(0));
            if (outData) {
                outPaths = outData->filePaths();
            }
        }

        if (outPaths.isEmpty()) {
            m_statusTitle->setText(QObject::tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未找到有效的地理编码输出 H5 文件列表。"));
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);

            m_lblLonRange->setText(QObject::tr("未生成"));
            m_lblLatRange->setText(QObject::tr("未生成"));
            m_lblLonSpan->setText(QObject::tr("未生成"));
            m_lblLatSpan->setText(QObject::tr("未生成"));
            m_lblDimensions->setText(QObject::tr("未生成"));
            m_lblValidPixels->setText(QObject::tr("未生成"));
            m_lblDatasetSummary->setText(QObject::tr("未生成"));
            return;
        }

        m_loadingOverlay->startLoading(QObject::tr("正在读取地理编码成果并解析地理坐标与特征值..."));

        // 读取期望参数
        const QJsonObject nodeSave = m_node->save();
        const int expType = nodeSave.value("type").toInt(1);
        const int expMultiRg = nodeSave.value("multiRg").toInt(1);
        const int expMultiAz = nodeSave.value("multiAz").toInt(1);
        const QString expProductLevel = m_node->expectedOutputProductLevel();
        const QString expDemName = m_node->effectiveAuxiliaryDemFileName();

        // 尝试从缓存加载
        const QString cacheKey = m_node->validationCacheKey();
        GeocodingValidationResults cached;
        if (!cacheKey.isEmpty() && m_node->loadValidationCache(cacheKey, cached)) {
            applyValidationResults(cached);
            return;
        }

        // 异步运行验证逻辑
        QFuture<GeocodingValidationResults> future = QtConcurrent::run([outPaths, expType, expMultiRg, expMultiAz, expProductLevel, expDemName, cancelToken]() {
            GeocodingValidationResults res;
            res.expectedType = expType;
            res.expectedMultiRg = expMultiRg;
            res.expectedMultiAz = expMultiAz;
            res.expectedProductLevel = expProductLevel;
            res.expectedDemName = expDemName;
            res.hasExpectedDemName = !expDemName.isEmpty();
            res.totalFiles = outPaths.size();

            int typeFiles = 0;
            int multiRgFiles = 0;
            int multiAzFiles = 0;
            int productLevelFiles = 0;
            int demNameFiles = 0;

            double sumValidPercent = 0.0;
            int validPercentCount = 0;
            bool firstGeo = true;
            bool allBoundsOk = true;

            const QStringList datasetCandidates = {
                QStringLiteral("phase"),
                QStringLiteral("coherence"),
                QStringLiteral("dem"),
                QStringLiteral("defomation_velocity"),
                QStringLiteral("amplitude")
            };

            for (const QString& h5Path : outPaths) {
                if (cancelToken && cancelToken->load()) {
                    return res;
                }

                GeocodingFileDiagnostics diag;
                diag.fileName = QFileInfo(h5Path).fileName();

                double lonEast = 0.0, lonWest = 0.0, latNorth = 0.0, latSouth = 0.0;
                int fileType = 0, multiRg = 0, multiAz = 0;
                std::string prodLevel, demName;
                QString readErr;

                bool hasGeo = false;
                bool hasType = false, hasRg = false, hasAz = false, hasLevel = false, hasDem = false;
                cv::Mat mat;
                QString foundDataset;

                {
                    NodeUtils::Hdf5Locker locker;
                    hasGeo = NodeUtils::readScalarFromH5(h5Path, "lon_east", lonEast, &readErr) &&
                             NodeUtils::readScalarFromH5(h5Path, "lon_west", lonWest, &readErr) &&
                             NodeUtils::readScalarFromH5(h5Path, "lat_north", latNorth, &readErr) &&
                             NodeUtils::readScalarFromH5(h5Path, "lat_south", latSouth, &readErr);

                    hasType = NodeUtils::readScalarFromH5(h5Path, "geocode_type", fileType, &readErr);
                    hasRg = NodeUtils::readScalarFromH5(h5Path, "multi_rg", multiRg, &readErr);
                    hasAz = NodeUtils::readScalarFromH5(h5Path, "multi_az", multiAz, &readErr);
                    hasLevel = NodeUtils::readStringFromH5(h5Path, "product_level", prodLevel, &readErr);
                    hasDem = NodeUtils::readStringFromH5(h5Path, "geocode_dem", demName, &readErr);

                    for (const QString& dname : datasetCandidates) {
                        if (NodeUtils::probeH5DatasetMetadata(h5Path, dname)) {
                            foundDataset = dname;
                            NodeUtils::readMatFromH5(h5Path, dname, mat, -1, &readErr);
                            break;
                        }
                    }
                }

                diag.hasGeoBounds = hasGeo;
                if (hasGeo) {
                    diag.lonEast = lonEast;
                    diag.lonWest = lonWest;
                    diag.latNorth = latNorth;
                    diag.latSouth = latSouth;

                    // 检查经纬度值域合法性 (-180~180, -90~90) 以及 east > west, north > south
                    diag.geoBoundsValid = (lonWest >= -180.0 && lonEast <= 180.0 && lonEast > lonWest &&
                                           latSouth >= -90.0 && latNorth <= 90.0 && latNorth > latSouth);
                    if (!diag.geoBoundsValid) {
                        allBoundsOk = false;
                    }

                    if (firstGeo) {
                        res.minLonWest = lonWest;
                        res.maxLonEast = lonEast;
                        res.minLatSouth = latSouth;
                        res.maxLatNorth = latNorth;
                        firstGeo = false;
                    } else {
                        res.minLonWest = qMin(res.minLonWest, lonWest);
                        res.maxLonEast = qMax(res.maxLonEast, lonEast);
                        res.minLatSouth = qMin(res.minLatSouth, latSouth);
                        res.maxLatNorth = qMax(res.maxLatNorth, latNorth);
                    }
                } else {
                    allBoundsOk = false;
                }

                diag.hasType = hasType;
                diag.fileType = fileType;
                if (hasType) {
                    if (typeFiles == 0) res.actualType = fileType;
                    else if (fileType != res.actualType) res.typeInconsistent = true;
                    ++typeFiles;
                }

                diag.hasMultiRg = hasRg;
                diag.fileMultiRg = multiRg;
                if (hasRg) {
                    if (multiRgFiles == 0) res.actualMultiRg = multiRg;
                    else if (multiRg != res.actualMultiRg) res.multiRgInconsistent = true;
                    ++multiRgFiles;
                }

                diag.hasMultiAz = hasAz;
                diag.fileMultiAz = multiAz;
                if (hasAz) {
                    if (multiAzFiles == 0) res.actualMultiAz = multiAz;
                    else if (multiAz != res.actualMultiAz) res.multiAzInconsistent = true;
                    ++multiAzFiles;
                }

                diag.hasProductLevel = hasLevel;
                diag.fileProductLevel = QString::fromStdString(prodLevel);
                if (hasLevel) {
                    if (productLevelFiles == 0) res.actualProductLevel = diag.fileProductLevel;
                    else if (diag.fileProductLevel != res.actualProductLevel) res.productLevelInconsistent = true;
                    ++productLevelFiles;
                }

                diag.hasDemName = hasDem;
                diag.fileDemName = QString::fromStdString(demName);
                if (hasDem) {
                    if (demNameFiles == 0) res.actualDemName = diag.fileDemName;
                    ++demNameFiles;
                }

                diag.primaryDataset = foundDataset;
                diag.datasetFound = !foundDataset.isEmpty() && !mat.empty();

                if (diag.datasetFound) {
                    diag.rows = mat.rows;
                    diag.cols = mat.cols;
                    if (res.outRows == 0 && res.outCols == 0) {
                        res.outRows = mat.rows;
                        res.outCols = mat.cols;
                    }

                    // 有效像元采样统计：仅将 NaN/Inf、-9999 及 amplitude 模式下的 0 视为空值，相位/相干系数/形变下的 0 均属于合法像元
                    int totalSampled = 0;
                    int validSampled = 0;
                    int stepRow = qMax(1, mat.rows / 300);
                    int stepCol = qMax(1, mat.cols / 300);
                    for (int r = 0; r < mat.rows; r += stepRow) {
                        for (int c = 0; c < mat.cols; c += stepCol) {
                            ++totalSampled;
                            double val = 0.0;
                            if (mat.type() == CV_32F) val = mat.at<float>(r, c);
                            else if (mat.type() == CV_64F) val = mat.at<double>(r, c);
                            else if (mat.type() == CV_8U) val = mat.at<uchar>(r, c);
                            else if (mat.type() == CV_16U) val = mat.at<ushort>(r, c);
                            else if (mat.type() == CV_16S) val = mat.at<short>(r, c);
                            else if (mat.type() == CV_32S) val = mat.at<int>(r, c);

                            if (std::isfinite(val) && val != -9999.0 && val != -99999.0 && (diag.primaryDataset != QStringLiteral("amplitude") || val > 0.0)) {
                                ++validSampled;
                            }
                        }
                    }
                    if (totalSampled > 0) {
                        diag.validPixelPercent = 100.0 * validSampled / totalSampled;
                        sumValidPercent += diag.validPixelPercent;
                        ++validPercentCount;
                    }
                    ++res.validFiles;
                }

                res.fileDiagnostics.append(diag);
            }

            res.hasActualType = (typeFiles > 0);
            res.hasActualMultiRg = (multiRgFiles > 0);
            res.hasActualMultiAz = (multiAzFiles > 0);
            res.hasActualProductLevel = (productLevelFiles > 0);
            res.hasActualDemName = (demNameFiles > 0);
            res.hasAnyLegacyResults = (!res.hasActualType || !res.hasActualMultiRg || !res.hasActualProductLevel);
            res.allGeoBoundsValid = allBoundsOk && (!firstGeo);

            if (validPercentCount > 0) {
                res.avgValidPixelPercent = sumValidPercent / validPercentCount;
            }

            if (res.validFiles == 0) {
                res.success = false;
                res.errorMsg = QObject::tr("未能成功解析任何地理编码输出 H5 的数据集。");
            } else {
                res.success = true;
            }

            return res;
        });

        auto* watcher = new QFutureWatcher<GeocodingValidationResults>(this);
        connect(watcher, &QFutureWatcher<GeocodingValidationResults>::finished, this, [this, watcher, cacheKey, currentEpoch]() {
            if (m_validationEpoch != currentEpoch || m_isTimedOut) {
                watcher->deleteLater();
                return;
            }
            GeocodingValidationResults res = watcher->result();
            if (!cacheKey.isEmpty() && res.success) {
                m_node->storeValidationCache(cacheKey, res);
            }
            applyValidationResults(res);
            watcher->deleteLater();
        });

        watcher->setFuture(future);
    }

    void applyValidationResults(const GeocodingValidationResults& res)
    {
        m_loadingOverlay->stopLoading();

        if (!res.success) {
            m_statusTitle->setText(QObject::tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(res.errorMsg.isEmpty() ? QObject::tr("地理编码输出数据读取或校验失败。") : res.errorMsg);
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);
            return;
        }

        // 填充参数比对表
        m_compTable->clearComparison();
        m_compTable->setEnabled(true);

        const auto paramActualText = [](bool hasValue, const QString& value, bool inconsistent, bool mismatch, bool& verifiable) {
            verifiable = hasValue;
            if (!hasValue) {
                return QObject::tr("未记录（旧结果）");
            }
            if (inconsistent) {
                return value + QObject::tr("（多文件值不一致）");
            }
            if (mismatch) {
                return value + QObject::tr("（与期望不一致）");
            }
            return value;
        };

        // 1. 处理类型
        const QString expTypeStr = (res.expectedType == 1) ? QObject::tr("干涉产品 (1)") : QObject::tr("SAR图像 (2)");
        QString actTypeStr;
        bool typeVerifiable = false;
        if (res.hasActualType) {
            typeVerifiable = true;
            actTypeStr = (res.actualType == 1) ? QObject::tr("干涉产品 (1)") : ((res.actualType == 2) ? QObject::tr("SAR图像 (2)") : QObject::tr("未知 (%1)").arg(res.actualType));
            if (res.typeInconsistent) {
                actTypeStr += QObject::tr("（多文件值不一致）");
            } else if (res.actualType != res.expectedType) {
                actTypeStr += QObject::tr("（与期望不一致）");
            }
        } else {
            actTypeStr = QObject::tr("未记录（旧结果）");
        }
        m_compTable->addComparison(QObject::tr("处理类型"), expTypeStr, actTypeStr, typeVerifiable);

        // 2. 距离多视数 / 3. 方位多视数
        if (res.expectedType == 2) {
            // SAR 图像模式：由本节点设置控制多视
            bool multiRgVerifiable = false;
            m_compTable->addComparison(QObject::tr("距离多视数"),
                QString::number(res.expectedMultiRg),
                paramActualText(res.hasActualMultiRg, QString::number(res.actualMultiRg),
                    res.multiRgInconsistent, res.actualMultiRg != res.expectedMultiRg, multiRgVerifiable),
                multiRgVerifiable);

            bool multiAzVerifiable = false;
            m_compTable->addComparison(QObject::tr("方位多视数"),
                QString::number(res.expectedMultiAz),
                paramActualText(res.hasActualMultiAz, QString::number(res.actualMultiAz),
                    res.multiAzInconsistent, res.actualMultiAz != res.expectedMultiAz, multiAzVerifiable),
                multiAzVerifiable);
        } else {
            // 干涉产品模式：多视参数继承自上游干涉输入，做诊断展示
            bool multiRgVerifiable = false;
            m_compTable->addComparison(QObject::tr("距离多视数 (继承输入)"),
                QObject::tr("继承输入"),
                paramActualText(res.hasActualMultiRg, QString::number(res.actualMultiRg),
                    res.multiRgInconsistent, false, multiRgVerifiable),
                multiRgVerifiable);

            bool multiAzVerifiable = false;
            m_compTable->addComparison(QObject::tr("方位多视数 (继承输入)"),
                QObject::tr("继承输入"),
                paramActualText(res.hasActualMultiAz, QString::number(res.actualMultiAz),
                    res.multiAzInconsistent, false, multiAzVerifiable),
                multiAzVerifiable);
        }

        // 4. 输出产品级别 (真实比对)
        bool levelVerifiable = false;
        const QString expLevelStr = res.expectedProductLevel.isEmpty() ? QObject::tr("自动推导") : res.expectedProductLevel;
        const bool levelMismatch = !res.expectedProductLevel.isEmpty() && res.hasActualProductLevel && (res.actualProductLevel != res.expectedProductLevel);
        m_compTable->addComparison(QObject::tr("输出产品级别"),
            expLevelStr,
            paramActualText(res.hasActualProductLevel, res.actualProductLevel,
                res.productLevelInconsistent, levelMismatch, levelVerifiable),
            levelVerifiable);

        // 5. 辅助 DEM 文件名称 (真实比对)
        if (res.hasExpectedDemName || res.hasActualDemName) {
            bool demVerifiable = false;
            const QString expDemStr = res.hasExpectedDemName ? res.expectedDemName : QObject::tr("未指定");
            const bool demMismatch = res.hasExpectedDemName && res.hasActualDemName && (res.actualDemName != res.expectedDemName);
            m_compTable->addComparison(QObject::tr("辅助 DEM 文件"),
                expDemStr,
                paramActualText(res.hasActualDemName, res.actualDemName,
                    false, demMismatch, demVerifiable),
                demVerifiable);
        }

        // 填充特征诊断区
        if (res.allGeoBoundsValid) {
            m_lblLonRange->setText(QStringLiteral("[%1°, %2°]").arg(res.minLonWest, 0, 'f', 4).arg(res.maxLonEast, 0, 'f', 4));
            m_lblLatRange->setText(QStringLiteral("[%1°, %2°]").arg(res.minLatSouth, 0, 'f', 4).arg(res.maxLatNorth, 0, 'f', 4));
            m_lblLonSpan->setText(QStringLiteral("%1°").arg(res.maxLonEast - res.minLonWest, 0, 'f', 4));
            m_lblLatSpan->setText(QStringLiteral("%1°").arg(res.maxLatNorth - res.minLatSouth, 0, 'f', 4));
        } else {
            m_lblLonRange->setText(QObject::tr("异常或未解析"));
            m_lblLatRange->setText(QObject::tr("异常或未解析"));
            m_lblLonSpan->setText(QObject::tr("异常或未解析"));
            m_lblLatSpan->setText(QObject::tr("异常或未解析"));
        }

        m_lblDimensions->setText(QStringLiteral("%1 行 x %2 列").arg(res.outRows).arg(res.outCols));
        m_lblValidPixels->setText(QStringLiteral("%1%").arg(res.avgValidPixelPercent, 0, 'f', 2));

        QString primaryDsets;
        for (const auto& diag : res.fileDiagnostics) {
            if (diag.datasetFound && !primaryDsets.contains(diag.primaryDataset)) {
                if (!primaryDsets.isEmpty()) primaryDsets += QStringLiteral(", ");
                primaryDsets += diag.primaryDataset;
            }
        }
        m_lblDatasetSummary->setText(QStringLiteral("共 %1 个成果文件，主数据集: %2")
            .arg(res.totalFiles).arg(primaryDsets.isEmpty() ? QObject::tr("无") : primaryDsets));

        // 最终状态卡片判定
        const bool typeMismatch = (res.hasActualType && res.actualType != res.expectedType) || res.typeInconsistent;
        const bool multiRgMismatch = (res.expectedType == 2) && ((res.hasActualMultiRg && res.actualMultiRg != res.expectedMultiRg) || res.multiRgInconsistent);
        const bool multiAzMismatch = (res.expectedType == 2) && ((res.hasActualMultiAz && res.actualMultiAz != res.expectedMultiAz) || res.multiAzInconsistent);
        const bool levelMismatchFlag = (!res.expectedProductLevel.isEmpty() && res.hasActualProductLevel && res.actualProductLevel != res.expectedProductLevel) || res.productLevelInconsistent;
        const bool demMismatchFlag = (res.hasExpectedDemName && res.hasActualDemName && res.actualDemName != res.expectedDemName);

        const bool hasMismatch = typeMismatch || multiRgMismatch || multiAzMismatch || levelMismatchFlag || demMismatchFlag;

        if (!res.allGeoBoundsValid) {
            m_statusTitle->setText(QObject::tr("验证警告：地理坐标范围异常"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
            m_statusDesc->setText(QObject::tr("成果输出 H5 中的地理范围超出有效经纬度区间或东西/南北顺序倒置，请检查输入坐标参数与 DEM 范围。"));
        } else if (hasMismatch) {
            m_statusTitle->setText(QObject::tr("验证警告：参数与当前设置不完全一致"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
            m_statusDesc->setText(QObject::tr("输出成果中记录的元数据参数与节点当前设置存在差异，可能是参数修改后尚未重新运行。"));
        } else if (res.hasAnyLegacyResults) {
            m_statusTitle->setText(QObject::tr("验证通过（旧成果兼容）"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
            m_statusDesc->setText(QObject::tr("输出成果结构与地理坐标范围校验正常；部分元数据属性未记录（兼容早期旧成果）。"));
        } else {
            m_statusTitle->setText(QObject::tr("验证通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
            m_statusDesc->setText(QObject::tr("所有地理编码输出文件参数匹配，地理范围合法，数据集与图像特征校验正常。"));
        }
    }

private:
    GeocodingNode* m_node = nullptr;

    QLabel* m_lblLonRange = nullptr;
    QLabel* m_lblLatRange = nullptr;
    QLabel* m_lblLonSpan = nullptr;
    QLabel* m_lblLatSpan = nullptr;
    QLabel* m_lblDimensions = nullptr;
    QLabel* m_lblValidPixels = nullptr;
    QLabel* m_lblDatasetSummary = nullptr;
};

::QWidget* GeocodingNode::createValidationWidget(::QWidget* parent)
{
    return new GeocodingValidationWidget(this, parent);
}

} // namespace QtNodes
