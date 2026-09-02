#include "InterferometricFormationNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include "Utils.h"
#include <cmath>
#include "ImageView.h"
#include <QtNodes/internal/NodeDetailWindow.hpp>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QTimer>
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <limits>
#include <memory>
#include <QPointer>
#include <QSignalBlocker>

namespace QtNodes {

InterferometricFormationNode::InterferometricFormationNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_masterImageCombo(nullptr)
    , m_defaultMasterCheckBox(nullptr)
    , m_deflatCheckBox(nullptr)
    , m_topoRemovalCheckBox(nullptr)
    , m_coherenceCheckBox(nullptr)
    , m_winWLabel(nullptr)
    , m_winWEdit(nullptr)
    , m_winHLabel(nullptr)
    , m_winHEdit(nullptr)
    , m_multilookRgEdit(nullptr)
    , m_multilookAzEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_masterIndex(0)
    , m_useDefaultMaster(true)
    , m_isDeflat(true)
    , m_isTopoRemoval(true)
    , m_isCoherence(true)
    , m_winW(5)
    , m_winH(5)
    , m_multilookRg(1)
    , m_multilookAz(1)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    qRegisterMetaType<InterferogramFileResult>("InterferogramFileResult");
    setExecutionMode(ExecutionMode::Automatic);
    QPointer<InterferometricFormationNode> self(this);
    NodeUtils::registerResourceChangeCallback([self](const QString& resourceId, const QString& provenanceId, NodeUtils::ResourceChangeKind kind) {
        if (self) QTimer::singleShot(0, self.data(), [self]() { if (self) self->refreshAuxiliaryDemLabels(); });
        if (!self || self->m_preparedAuxiliaryDemBinding.resourceId != resourceId) return;
        if (kind == NodeUtils::ResourceChangeKind::ProvenanceAdded && provenanceId != self->m_preparedAuxiliaryDemBinding.pinnedProvenanceId) return;
        QTimer::singleShot(0, self.data(), [self]() {
            if (!self) return;
            self->m_preparedAuxiliaryDemBinding = NodeUtils::AuxiliaryDemBinding();
            self->m_preparedDemExecutionSnapshot = NodeUtils::DemExecutionSnapshot();
            self->m_demPath.clear();
            self->m_preparedDemPath.clear();
            self->m_preparedDemValidMaskPath.clear();
            self->m_demInputData.reset();
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

InterferometricFormationNode::~InterferometricFormationNode()
{
    m_remedyWatcher.disconnect(this);
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    if (m_thread && m_thread->parent() == this) {
        m_thread->setParent(nullptr);
    }
    if (m_workerThread) {
        m_workerThread->disconnect(this);
        m_workerThread->StopProcess();
    }
    bool workerStopped = true;
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        workerStopped = m_thread->wait(5000);
    }

    if (workerStopped) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("InterferometricFormationNode destroyed"), projectXml());
    } else if (m_thread) {
        const auto rollbackScheduled = std::make_shared<std::atomic_bool>(false);
        auto rollbackAfterWorkerStops = [transaction = m_outputTransaction, rollbackScheduled]() mutable {
            bool expected = false;
            if (!rollbackScheduled->compare_exchange_strong(expected, true)) {
                return;
            }
            NodeUtils::abandonOutputTransaction(transaction,
                                                QStringLiteral("InterferometricFormationNode destroyed after worker stopped"));
        };
        connect(m_thread, &QThread::finished, rollbackAfterWorkerStops);
        if (!m_thread->isRunning()) {
            rollbackAfterWorkerStops();
        }
        InSARLogManager::LogWarning("InterferometricFormationNode",
            "Worker thread did not stop within destructor timeout; rollback is deferred until the worker exits.");
    }
    m_workerThread = nullptr;
    m_thread = nullptr;
}

unsigned int InterferometricFormationNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType InterferometricFormationNode::dataType(PortType portType, PortIndex portIndex) const
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

bool InterferometricFormationNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString InterferometricFormationNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0)
            return QStringLiteral("输入图像");
        else
            return QStringLiteral("辅助地形 DEM ?");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool InterferometricFormationNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1)
        return true;
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

QList<QList<PortIndex>> InterferometricFormationNode::alternativeInputGroups() const
{
    return {};
}

void InterferometricFormationNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
        updateLabels();
        if (m_inputData && m_outputNodeName.isEmpty()) {
            m_outputNodeName = generateDefaultOutputName();
            if (m_outputNodeNameEdit) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
        }
        if (!m_inputData || m_inputData->filePaths().isEmpty()) {
            m_outputData.reset();
            m_imageInfoData.reset();
        }
    } else if (port == 1) {
        const auto auxiliary = std::dynamic_pointer_cast<AuxiliaryDemData>(data);
        m_auxiliaryDemEntityData = auxiliary;
        if (auxiliary) {
            m_auxiliaryDemReferenceData.reset();
            m_auxiliaryDemLabel.clear();
            if (m_demLabelCombo) m_demLabelCombo->setCurrentIndex(0);
        }
        m_demInputData = auxiliary
            ? std::make_shared<DEMFileData>(auxiliary->rasterPath(), auxiliary->nodeName(), auxiliary->identityH5Path())
            : std::dynamic_pointer_cast<DEMFileData>(data);
        if (auxiliary) m_demInputData->setProductDescriptor(auxiliary->productDescriptor());
        if (m_demInputData) {
            m_demPath = auxiliary ? auxiliary->rasterPath() : m_demInputData->filePath();
        } else {
            if (!isRestoring()) {
                m_demPath.clear();
            }
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);
    updateParameterWidgetsEnableState();
}

std::shared_ptr<NodeData> InterferometricFormationNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* InterferometricFormationNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject InterferometricFormationNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["masterIndex"] = m_masterIndex;
    modelJson["useDefaultMaster"] = m_useDefaultMaster;
    modelJson["isDeflat"] = m_deflatCheckBox ? m_deflatCheckBox->isChecked() : m_isDeflat;
    modelJson["isTopoRemoval"] = m_topoRemovalCheckBox ? m_topoRemovalCheckBox->isChecked() : m_isTopoRemoval;
    modelJson["isCoherence"] = m_coherenceCheckBox ? m_coherenceCheckBox->isChecked() : m_isCoherence;
    modelJson["winW"] = m_winWEdit ? m_winWEdit->text().toInt() : m_winW;
    modelJson["winH"] = m_winHEdit ? m_winHEdit->text().toInt() : m_winH;
    modelJson["multilookRg"] = m_multilookRgEdit ? m_multilookRgEdit->text().toInt() : m_multilookRg;
    modelJson["multilookAz"] = m_multilookAzEdit ? m_multilookAzEdit->text().toInt() : m_multilookAz;
    modelJson[QStringLiteral("auxiliaryDemLabel")] = m_auxiliaryDemLabel;
    modelJson[QStringLiteral("auxiliaryDemLegacyResourceId")] = m_legacyDemResourceId;
    modelJson[QStringLiteral("auxiliaryDemLegacyPinnedProvenanceId")] = m_legacyDemProvenanceId;
    modelJson[QStringLiteral("hasOutputExecutionSettings")] = m_hasOutputExecutionSettings;
    modelJson[QStringLiteral("outputIsDeflat")] = m_outputIsDeflat;
    modelJson[QStringLiteral("outputIsTopoRemoval")] = m_outputIsTopoRemoval;
    modelJson[QStringLiteral("outputIsCoherence")] = m_outputIsCoherence;
    modelJson[QStringLiteral("outputWinW")] = m_outputWinW;
    modelJson[QStringLiteral("outputWinH")] = m_outputWinH;
    modelJson[QStringLiteral("outputMultilookRg")] = m_outputMultilookRg;
    modelJson[QStringLiteral("outputMultilookAz")] = m_outputMultilookAz;

    return modelJson;
}

void InterferometricFormationNode::prepareForPaste(QJsonObject& json,
                                                   PasteContext& context) const
{
    ExecutableNodeDelegateModel::prepareForPaste(json, context);

    // These values describe the committed source artifact rather than the
    // clone's editable execution configuration.  Keeping them would make the
    // interferometry preview/evaluation tab interpret a future output as old.
    json.remove(QStringLiteral("hasOutputExecutionSettings"));
    json.remove(QStringLiteral("outputIsDeflat"));
    json.remove(QStringLiteral("outputIsTopoRemoval"));
    json.remove(QStringLiteral("outputIsCoherence"));
    json.remove(QStringLiteral("outputWinW"));
    json.remove(QStringLiteral("outputWinH"));
    json.remove(QStringLiteral("outputMultilookRg"));
    json.remove(QStringLiteral("outputMultilookAz"));
}

void InterferometricFormationNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vIndex = json["masterIndex"];
    if (!vIndex.isUndefined()) m_masterIndex = vIndex.toInt();

    QJsonValue vUseDefault = json["useDefaultMaster"];
    if (!vUseDefault.isUndefined()) m_useDefaultMaster = vUseDefault.toBool();

    QJsonValue vDeflat = json["isDeflat"];
    if (!vDeflat.isUndefined()) m_isDeflat = vDeflat.toBool();

    QJsonValue vTopo = json["isTopoRemoval"];
    if (!vTopo.isUndefined()) m_isTopoRemoval = vTopo.toBool();

    QJsonValue vCoherence = json["isCoherence"];
    if (!vCoherence.isUndefined()) m_isCoherence = vCoherence.toBool();

    QJsonValue vWinW = json["winW"];
    if (!vWinW.isUndefined()) m_winW = vWinW.toInt();

    QJsonValue vWinH = json["winH"];
    if (!vWinH.isUndefined()) m_winH = vWinH.toInt();

    QJsonValue vMultilookRg = json["multilookRg"];
    if (!vMultilookRg.isUndefined()) m_multilookRg = vMultilookRg.toInt();

    QJsonValue vMultilookAz = json["multilookAz"];
    if (!vMultilookAz.isUndefined()) m_multilookAz = vMultilookAz.toInt();

    m_auxiliaryDemLabel = json.value(QStringLiteral("auxiliaryDemLabel")).toString().trimmed();
    m_legacyDemResourceId = json.value(QStringLiteral("auxiliaryDemLegacyResourceId")).toString().trimmed();
    m_legacyDemProvenanceId = json.value(QStringLiteral("auxiliaryDemLegacyPinnedProvenanceId")).toString().trimmed();

    QJsonValue vHasOutputSettings = json[QStringLiteral("hasOutputExecutionSettings")];
    if (!vHasOutputSettings.isUndefined()) m_hasOutputExecutionSettings = vHasOutputSettings.toBool();
    QJsonValue vOutputDeflat = json[QStringLiteral("outputIsDeflat")];
    if (!vOutputDeflat.isUndefined()) m_outputIsDeflat = vOutputDeflat.toBool();
    QJsonValue vOutputTopo = json[QStringLiteral("outputIsTopoRemoval")];
    if (!vOutputTopo.isUndefined()) m_outputIsTopoRemoval = vOutputTopo.toBool();
    QJsonValue vOutputCoherence = json[QStringLiteral("outputIsCoherence")];
    if (!vOutputCoherence.isUndefined()) m_outputIsCoherence = vOutputCoherence.toBool();
    QJsonValue vOutputWinW = json[QStringLiteral("outputWinW")];
    if (!vOutputWinW.isUndefined()) m_outputWinW = vOutputWinW.toInt();
    QJsonValue vOutputWinH = json[QStringLiteral("outputWinH")];
    if (!vOutputWinH.isUndefined()) m_outputWinH = vOutputWinH.toInt();
    QJsonValue vOutputMultilookRg = json[QStringLiteral("outputMultilookRg")];
    if (!vOutputMultilookRg.isUndefined()) m_outputMultilookRg = vOutputMultilookRg.toInt();
    QJsonValue vOutputMultilookAz = json[QStringLiteral("outputMultilookAz")];
    if (!vOutputMultilookAz.isUndefined()) m_outputMultilookAz = vOutputMultilookAz.toInt();

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_defaultMasterCheckBox) m_defaultMasterCheckBox->setChecked(m_useDefaultMaster);
    if (m_deflatCheckBox) m_deflatCheckBox->setChecked(m_isDeflat);
    if (m_topoRemovalCheckBox) m_topoRemovalCheckBox->setChecked(m_isTopoRemoval);
    if (m_coherenceCheckBox) {
        m_coherenceCheckBox->setChecked(m_isCoherence);
        onCoherenceStateChanged(m_isCoherence ? Qt::Checked : Qt::Unchecked);
    }
    if (m_winWEdit) m_winWEdit->setText(QString::number(m_winW));
    if (m_winHEdit) m_winHEdit->setText(QString::number(m_winH));
    if (m_multilookRgEdit) m_multilookRgEdit->setText(QString::number(m_multilookRg));
    if (m_multilookAzEdit) m_multilookAzEdit->setText(QString::number(m_multilookAz));

    updateMasterImageCombo();
}

void InterferometricFormationNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void InterferometricFormationNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene);
        }
    };



    // 3. 默认首张图像为主图像
    auto* defaultMasterLayout = new QHBoxLayout();
    QLabel* defaultMasterLabel = new QLabel("默认首张图像为主图像");
    defaultMasterLayout->addWidget(defaultMasterLabel);
    m_defaultMasterCheckBox = new QCheckBox();
    m_defaultMasterCheckBox->setChecked(m_useDefaultMaster);
    connect(m_defaultMasterCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool val = (state == Qt::Checked);
        if (m_useDefaultMaster != val) {
            if (!confirmParameterChange()) {
                m_defaultMasterCheckBox->blockSignals(true);
                m_defaultMasterCheckBox->setChecked(m_useDefaultMaster);
                m_defaultMasterCheckBox->blockSignals(false);
                return;
            }
            m_useDefaultMaster = val;
            updateMasterImageCombo();
            invalidateNodeData();
        }
    });
    defaultMasterLayout->addWidget(m_defaultMasterCheckBox);
    layout->addLayout(defaultMasterLayout);

    // 4. 主图像选择 (Master Image)
    auto* masterImageLayout = new QHBoxLayout();
    QLabel* masterImageLabel = new QLabel("主图像选择");
    masterImageLabel->setFixedWidth(100);
    masterImageLayout->addWidget(masterImageLabel);
    m_masterImageCombo = new QComboBox();
    m_masterImageCombo->setEditable(false);
    m_masterImageCombo->setEnabled(!m_useDefaultMaster);
    connect(m_masterImageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        if (m_useDefaultMaster) return;
        int val = index - 1; // index 0 is placeholder
        if (m_masterIndex != val) {
            if (!confirmParameterChange()) {
                m_masterImageCombo->blockSignals(true);
                m_masterImageCombo->setCurrentIndex(m_masterIndex + 1);
                m_masterImageCombo->blockSignals(false);
                return;
            }
            m_masterIndex = val;
            invalidateNodeData();
        }
    });
    masterImageLayout->addWidget(m_masterImageCombo);
    layout->addLayout(masterImageLayout);

    // 5. 平地相位消除
    auto* deflatLayout = new QHBoxLayout();
    QLabel* deflatLabel = new QLabel("平地相位消除");
    deflatLayout->addWidget(deflatLabel);
    m_deflatCheckBox = new QCheckBox();
    m_deflatCheckBox->setChecked(m_isDeflat);
    connect(m_deflatCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool val = (state == Qt::Checked);
        if (m_isDeflat != val) {
            if (!confirmParameterChange()) {
                m_deflatCheckBox->blockSignals(true);
                m_deflatCheckBox->setChecked(m_isDeflat);
                m_deflatCheckBox->blockSignals(false);
                return;
            }
            m_isDeflat = val;
            invalidateNodeData();
            updateParameterWidgetsEnableState();
        }
    });
    deflatLayout->addWidget(m_deflatCheckBox);
    layout->addLayout(deflatLayout);

    // 6. 地形相位消除
    auto* topoRemovalLayout = new QHBoxLayout();
    QLabel* topoRemovalLabel = new QLabel("地形相位消除");
    topoRemovalLayout->addWidget(topoRemovalLabel);
    m_topoRemovalCheckBox = new QCheckBox();
    m_topoRemovalCheckBox->setChecked(m_isTopoRemoval);
    connect(m_topoRemovalCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool val = (state == Qt::Checked);
        if (m_isTopoRemoval != val) {
            if (!confirmParameterChange()) {
                m_topoRemovalCheckBox->blockSignals(true);
                m_topoRemovalCheckBox->setChecked(m_isTopoRemoval);
                m_topoRemovalCheckBox->blockSignals(false);
                return;
            }
            m_isTopoRemoval = val;
            invalidateNodeData();
            updateParameterWidgetsEnableState();
        }
    });
    topoRemovalLayout->addWidget(m_topoRemovalCheckBox);
    layout->addLayout(topoRemovalLayout);

    // 7. 计算相干系数
    auto* coherenceLayout = new QHBoxLayout();
    QLabel* coherenceLabel = new QLabel("计算相干系数");
    coherenceLayout->addWidget(coherenceLabel);
    m_coherenceCheckBox = new QCheckBox();
    m_coherenceCheckBox->setChecked(m_isCoherence);
    connect(m_coherenceCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool val = (state == Qt::Checked);
        if (m_isCoherence != val) {
            if (!confirmParameterChange()) {
                m_coherenceCheckBox->blockSignals(true);
                m_coherenceCheckBox->setChecked(m_isCoherence);
                m_coherenceCheckBox->blockSignals(false);
                return;
            }
            m_isCoherence = val;
            onCoherenceStateChanged(state);
            invalidateNodeData();
        }
    });
    coherenceLayout->addWidget(m_coherenceCheckBox);
    layout->addLayout(coherenceLayout);

    // 8. 相干估计窗口 W
    auto* winWLayout = new QHBoxLayout();
    m_winWLabel = new QLabel("相干估计窗口 W");
    m_winWLabel->setFixedWidth(100);
    winWLayout->addWidget(m_winWLabel);
    m_winWEdit = new QLineEdit();
    m_winWEdit->setText(QString::number(m_winW));
    connect(m_winWEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_winWEdit->text().toInt();
        if (m_winW != val) {
            if (!confirmParameterChange()) {
                m_winWEdit->setText(QString::number(m_winW));
                return;
            }
            m_winW = val;
            invalidateNodeData();
        }
    });
    winWLayout->addWidget(m_winWEdit);
    layout->addLayout(winWLayout);

    // 9. 相干估计窗口 H
    auto* winHLayout = new QHBoxLayout();
    m_winHLabel = new QLabel("相干估计窗口 H");
    m_winHLabel->setFixedWidth(100);
    winHLayout->addWidget(m_winHLabel);
    m_winHEdit = new QLineEdit();
    m_winHEdit->setText(QString::number(m_winH));
    connect(m_winHEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_winHEdit->text().toInt();
        if (m_winH != val) {
            if (!confirmParameterChange()) {
                m_winHEdit->setText(QString::number(m_winH));
                return;
            }
            m_winH = val;
            invalidateNodeData();
        }
    });
    winHLayout->addWidget(m_winHEdit);
    layout->addLayout(winHLayout);

    onCoherenceStateChanged(m_isCoherence ? Qt::Checked : Qt::Unchecked);

    // 10. 多视 rg 倍数
    auto* multRgLayout = new QHBoxLayout();
    QLabel* multRgLabel = new QLabel("多视向(Range)");
    multRgLabel->setFixedWidth(100);
    multRgLayout->addWidget(multRgLabel);
    m_multilookRgEdit = new QLineEdit();
    m_multilookRgEdit->setText(QString::number(m_multilookRg));
    connect(m_multilookRgEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_multilookRgEdit->text().toInt();
        if (m_multilookRg != val) {
            if (!confirmParameterChange()) {
                m_multilookRgEdit->setText(QString::number(m_multilookRg));
                return;
            }
            m_multilookRg = val;
            invalidateNodeData();
        }
    });
    multRgLayout->addWidget(m_multilookRgEdit);
    layout->addLayout(multRgLayout);

    // 11. 多视 az 倍数
    auto* multAzLayout = new QHBoxLayout();
    QLabel* multAzLabel = new QLabel("多视向(Azimuth)");
    multAzLabel->setFixedWidth(100);
    multAzLayout->addWidget(multAzLabel);
    m_multilookAzEdit = new QLineEdit();
    m_multilookAzEdit->setText(QString::number(m_multilookAz));
    connect(m_multilookAzEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_multilookAzEdit->text().toInt();
        if (m_multilookAz != val) {
            if (!confirmParameterChange()) {
                m_multilookAzEdit->setText(QString::number(m_multilookAz));
                return;
            }
            m_multilookAz = val;
            invalidateNodeData();
        }
    });
    multAzLayout->addWidget(m_multilookAzEdit);
    layout->addLayout(multAzLayout);

    // 12. 目标节点名
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLabel->setFixedWidth(100);
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text();
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
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    // 13. DEM路径
    auto* demLayout = new QHBoxLayout();
    m_demPathLabel = new QLabel("辅助 DEM");
    m_demPathLabel->setFixedWidth(100);
    m_demPathLabel->setStyleSheet("QLabel:disabled { color: #888888; }");
    
    m_demLabelCombo = new QComboBox();
    refreshAuxiliaryDemLabels();
    connect(m_demLabelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
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
        if (!m_auxiliaryDemLabel.isEmpty()) { m_auxiliaryDemEntityData.reset(); m_auxiliaryDemReferenceData.reset(); }
        invalidateNodeData();
        triggerVisualUpdate();
    });

    demLayout->addWidget(m_demPathLabel);
    demLayout->addWidget(m_demLabelCombo);
    layout->addLayout(demLayout);

    // Spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    updateLabels();
    updateParameterWidgetsEnableState();
}

void InterferometricFormationNode::onCoherenceStateChanged(int state)
{
    updateParameterWidgetsEnableState();
}

void InterferometricFormationNode::updateLabels()
{
    updateMasterImageCombo();
}

void InterferometricFormationNode::updateMasterImageCombo()
{
    if (!m_masterImageCombo) return;

    m_masterImageCombo->blockSignals(true);
    m_masterImageCombo->clear();
    m_masterImageCombo->setEnabled(!m_useDefaultMaster);

    if (!m_inputData) {
        if (m_useDefaultMaster) {
            m_masterImageCombo->addItem("自动选择首张图像...");
            m_masterIndex = 0;
        } else {
            m_masterImageCombo->addItem("请选择主图像...");
            m_masterIndex = -1;
        }
        m_masterImageCombo->setCurrentIndex(0);
        m_masterImageCombo->blockSignals(false);
        return;
    }

    QStandardItemModel* model = projectModel();
    if (!model) {
        m_masterImageCombo->blockSignals(false);
        return;
    }

    QList<QStandardItem*> foundProjects = model->findItems(projectName());
    if (foundProjects.isEmpty()) {
        m_masterImageCombo->blockSignals(false);
        return;
    }

    QStandardItem* projectItem = foundProjects.first();
    QString srcNode = m_inputData->nodeName();

    QStringList imageNames;
    for (int i = 0; i < projectItem->rowCount(); ++i)
    {
        QStandardItem* nodeItem = projectItem->child(i, 0);
        if (nodeItem && nodeItem->text() == srcNode)
        {
            for (int j = 0; j < nodeItem->rowCount(); ++j)
            {
                QStandardItem* imgItem = nodeItem->child(j, 0);
                if (imgItem)
                {
                    imageNames.append(imgItem->text());
                }
            }
            break;
        }
    }

    if (m_useDefaultMaster)
    {
        m_masterImageCombo->addItem("自动选择首张图像...");
        for (const QString& name : imageNames) {
            m_masterImageCombo->addItem(name);
        }
        m_masterIndex = 0; // First image
        m_masterImageCombo->setCurrentIndex(0);
    }
    else
    {
        m_masterImageCombo->addItem("请选择主图像...");
        for (const QString& name : imageNames) {
            m_masterImageCombo->addItem(name);
        }
        if (m_masterIndex >= 0 && m_masterIndex + 1 < m_masterImageCombo->count())
        {
            m_masterImageCombo->setCurrentIndex(m_masterIndex + 1);
        }
        else
        {
            m_masterIndex = -1;
            m_masterImageCombo->setCurrentIndex(0);
        }
    }

    m_masterImageCombo->blockSignals(false);
}

QString InterferometricFormationNode::generateDefaultOutputName() const
{
    if (m_inputData)
    {
        return m_inputData->nodeName() + "_interf";
    }
    return "干涉化结果";
}

bool InterferometricFormationNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;

    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
        return false;

    // Validate parameters
    bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
    int winW = m_winWEdit ? m_winWEdit->text().toInt(&ok1) : m_winW;
    int winH = m_winHEdit ? m_winHEdit->text().toInt(&ok2) : m_winH;
    int mRg = m_multilookRgEdit ? m_multilookRgEdit->text().toInt(&ok3) : m_multilookRg;
    int mAz = m_multilookAzEdit ? m_multilookAzEdit->text().toInt(&ok4) : m_multilookAz;

    if (m_isCoherence) {
        if (!ok1 || !ok2 || winW <= 0 || winH <= 0 || winW % 2 == 0 || winH % 2 == 0) {
            return false;
        }
    }
    if (!ok3 || !ok4 || mRg <= 0 || mAz <= 0) {
        return false;
    }

    int masterIdx = m_useDefaultMaster ? 0 : m_masterIndex;
    if (masterIdx < 0)
        return false;

    return true;
}

void InterferometricFormationNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);

    if (m_heartbeatTimer.isValid() && m_heartbeatTimer.elapsed() >= 30000) {
        TaskLogContext logContext;
        logContext.displayName = caption();
        InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelInfo,
                                      "InterferometricFormationNode",
                                      QStringLiteral("干涉形成处理中：%1（%2%）。").arg(message).arg(progress),
                                      LogTargets(LogTarget::UserProjectLog),
                                      QStringLiteral("heartbeat"), QStringLiteral("running"),
                                      m_executionTimer.isValid() ? m_executionTimer.elapsed() : -1);
        m_heartbeatTimer.restart();
    }
}

void InterferometricFormationNode::onProcessingFinished()
{
    const QString dstNode = m_preparedFileName;
    QStringList h5Paths;
    QStringList previewSourcePaths;
    QStringList jpgPaths;
    QStringList types;
    QList<InterferogramFileResult> committedResults;

    m_workerThread = nullptr;
    m_thread = nullptr;

    if (isAutomaticExecutionObsolete()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset();
        m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        discardObsoleteAutomaticExecution();
        return;
    }
    refreshAuxiliaryDemLabels();

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }
    if (m_isTopoRemoval && (m_auxiliaryDemEntityData || m_auxiliaryDemReferenceData)) {
        NodeUtils::AuxiliaryDemBinding currentBinding;
        QString bindingError;
        if (!NodeUtils::revalidateDemExecutionSnapshot(projectPath(), m_auxiliaryDemEntityData.get(),
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
        onError(QStringLiteral("Project XML context is unavailable for interferometric output commit."));
        return;
    }

    QStringList requiredDatasets;
    requiredDatasets.append(QStringLiteral("phase"));
    if (m_preparedIsCoherence) {
        requiredDatasets.append(QStringLiteral("coherence"));
    }
    if (!NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, requiredDatasets, &transactionError)) {
        onError(transactionError);
        return;
    }

    QStringList workerOutputPaths;
    for (const InterferogramFileResult& result : m_pendingInterferogramResults) {
        workerOutputPaths.append(result.h5Path);
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
    m_xmlDirty = false;
    for (InterferogramFileResult result : m_pendingInterferogramResults) {
        const QString h5FileName = QFileInfo(result.h5Path).fileName();
        result.h5Path = QDir(projectPath() + "/" + dstNode).absoluteFilePath(h5FileName);
        result.relativePath = QStringLiteral("/%1/%2").arg(dstNode, h5FileName);
        commitInterferogramResult(result);
        committedResults.append(result);
    }
    if (!m_xmlDirty) {
        onError(QStringLiteral("Interferometric output metadata was not produced."));
        return;
    }
    if (!NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }
    if (!NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    for (const InterferogramFileResult& result : committedResults) {
        publishInterferogramResultToProjectTree(result);
    }
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }
    captureOutputExecutionSettings();

    for (const QString& h5Path : h5Paths) {
        const QString baseName = QFileInfo(h5Path).baseName();
        previewSourcePaths.append(h5Path);
        jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + "_phase.jpg");
        types.append(QStringLiteral("phase"));
        if (m_preparedIsCoherence) {
            previewSourcePaths.append(h5Path);
            jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + "_coh.jpg");
            types.append(QStringLiteral("coherence"));
        }
    }
    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(
        m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);

    if (!previewSourcePaths.isEmpty())
    {
        startPreviewGeneration(previewSourcePaths, jpgPaths, types, h5Paths);
    }
    else
    {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);

        // Update UI state
        updateParameterWidgetsEnableState();

        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("InterferometricFormationNode", "executeProcessing completed (empty output list).");
        finishExecution();
    }
}

void InterferometricFormationNode::onInterferogramGenerated(const InterferogramFileResult& result)
{
    m_pendingInterferogramResults.append(result);
}

ProductInputContract InterferometricFormationNode::productInputContract(PortIndex portIndex) const
{
    ProductInputContract contract;
    contract.semanticId = portIndex == 0 ? QStringLiteral("interferometric.input.coregistered_complex_sar")
                                         : QStringLiteral("interferometric.input.auxiliary_terrain_dem");
    contract.optional = portIndex == 1;
    contract.allowedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("back_geocoded_complex_sar")
                      << QStringLiteral("coregistered_complex_sar")
                      << QStringLiteral("ionosphere_corrected_complex_sar")
                      << QStringLiteral("slc_stack")
                      << QStringLiteral("cropped_complex_sar")
        : QStringList() << QStringLiteral("auxiliary_terrain_dem");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract InterferometricFormationNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0 ? QStringLiteral("interferometric.output.interferogram")
                                         : QStringLiteral("interferometric.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList{QStringLiteral("interferogram")}
        : QStringList{QStringLiteral("preview")};
    return contract;
}

void InterferometricFormationNode::commitInterferogramResult(const InterferogramFileResult& result)
{
    XMLFile* xml = projectXml();
    if (!xml) {
        return;
    }

    xml->XMLFile_add_interferometric_phase(
        m_preparedFileName.toStdString().c_str(),
        result.phaseName.toStdString().c_str(),
        result.relativePath.toStdString().c_str(),
        result.masterName.toStdString().c_str(),
        "phase-1.0",
        result.offsetRow,
        result.offsetCol,
        result.isDeflat,
        result.isTopoRemoval,
        result.isCoherence,
        result.winWidth,
        result.winHeight,
        result.multilookRg,
        result.multilookAz);

    if (result.isCoherence) {
        if (xml->XMLFile_add_interferometric_phase(
            m_preparedFileName.toStdString().c_str(),
            result.cohName.toStdString().c_str(),
            result.relativePath.toStdString().c_str(),
            result.masterName.toStdString().c_str(),
            "coherence-1.0",
            result.offsetRow,
            result.offsetCol,
            result.isDeflat,
            result.isTopoRemoval,
            result.isCoherence,
            result.winWidth,
            result.winHeight,
            result.multilookRg,
            result.multilookAz) < 0 ||
            xml->XMLFile_set_coherence_semantics(
                m_preparedFileName.toStdString().c_str(),
                result.cohName.toStdString().c_str(),
                NodeUtils::CoherenceSemantics::kPhaseAxialR2) < 0) {
            return;
        }
    }
    m_xmlDirty = true;
}

void InterferometricFormationNode::publishInterferogramResultToProjectTree(
    const InterferogramFileResult& result)
{
    QStandardItemModel* model = projectModel();
    if (!model) {
        return;
    }

    QList<QStandardItem*> foundProjects = model->findItems(m_preparedProjectName);
    if (foundProjects.isEmpty()) {
        return;
    }

    QStandardItem* project = foundProjects.first();
    QStandardItem* interfNode = NodeUtils::findOrCreateProjectNode(
        project, m_preparedFileName, "phase-1.0");
    if (!interfNode) {
        return;
    }
    interfNode->setToolTip(m_preparedProjectName);

    const auto publishItem = [interfNode, &result](const QString& name, const QString& type) {
        QStandardItem* item = nullptr;
        for (int row = 0; row < interfNode->rowCount(); ++row) {
            QStandardItem* existing = interfNode->child(row, 0);
            if (existing && existing->text() == name) {
                item = existing;
                break;
            }
        }
        if (!item) {
            item = new QStandardItem(name);
            item->setToolTip(type);
            item->setIcon(QIcon(IMAGEDATA_ICON));
            interfNode->appendRow(item);
        }
        QStandardItem* pathItem = new QStandardItem(result.h5Path);
        pathItem->setToolTip(QFileInfo(result.h5Path).fileName());
        interfNode->setChild(item->row(), 1, pathItem);
    };

    publishItem(result.phaseName, QStringLiteral("phase"));
    if (result.isCoherence) {
        publishItem(result.cohName, QStringLiteral("coherence"));
    }
}

void InterferometricFormationNode::onError(const QString& error)
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
    }
    m_workerThread = nullptr;
    m_thread = nullptr;

    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    updateParameterWidgetsEnableState();
    TaskLogContext logContext;
    logContext.displayName = caption();
    InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelError,
                                  "InterferometricFormationNode",
                                  QStringLiteral("干涉形成失败：%1").arg(error),
                                  LogTargets(LogTarget::UserProjectLog) | LogTarget::DebugConsole,
                                  QStringLiteral("completed"), QStringLiteral("failed"),
                                  m_executionTimer.isValid() ? m_executionTimer.elapsed() : -1);
    setState(ExecutionState::Error);
}

void InterferometricFormationNode::onCancelled()
{
    m_workerThread = nullptr;
    m_thread = nullptr;
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    updateParameterWidgetsEnableState();
    TaskLogContext logContext;
    logContext.displayName = caption();
    InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelInfo,
                                  "InterferometricFormationNode", QStringLiteral("干涉形成任务已取消。"),
                                  LogTargets(LogTarget::UserProjectLog),
                                  QStringLiteral("completed"), QStringLiteral("cancelled"),
                                  m_executionTimer.isValid() ? m_executionTimer.elapsed() : -1);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void InterferometricFormationNode::onModelUpdated(QStandardItemModel* model)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* InterferometricFormationNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString InterferometricFormationNode::projectPath() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString InterferometricFormationNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* InterferometricFormationNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void InterferometricFormationNode::execute()
{
    executeProcessing();
}

void InterferometricFormationNode::stopExecution()
{
    if (m_workerThread) {
        m_workerThread->StopProcess();
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
    }
}

void InterferometricFormationNode::processAutomatically()
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
        // 直接连线已连接但上游 DEM 尚未产出时，同样属于可等待前置条件，
        // 与 planned 标签一致转入 Pending，避免自动流程误报执行失败。
        const bool waitingForDemProducer =
            !m_auxiliaryDemEntityData && hasActiveInputConnection(1);
        if (isPlannedLabel || waitingForDemProducer) {
            setStartFailureMessage(QString());
            setState(ExecutionState::Pending);
        } else {
            setLastErrorMessage(_startFailureMessage.isEmpty()
                ? QStringLiteral("自动执行前置条件无效，且辅助 DEM 标签不可等待。")
                : _startFailureMessage);
            setState(ExecutionState::Error);
        }
    }
}

bool InterferometricFormationNode::prepareToStart()
{
    QJsonObject inputGeometry;
    if (m_inputData) inputGeometry = NodeUtils::inputGeometryFromProductDescriptor(m_inputData->physicalProductDescriptor());
    if (m_isTopoRemoval && hasActiveInputConnection(1) && !m_auxiliaryDemLabel.isEmpty()) {
        setStartFailureMessage(QStringLiteral("辅助 DEM 不能同时使用直接连线和命名标签。"));
        return false;
    }
    if (m_isTopoRemoval && !m_auxiliaryDemEntityData && m_auxiliaryDemLabel.isEmpty()) {
        setStartFailureMessage(QStringLiteral("去地形 DEM 必须通过直接连线或工程标签提供。"));
        return false;
    }
    m_demPath.clear();
    m_demInputData.reset();
    if (m_isTopoRemoval && m_auxiliaryDemEntityData) {
        NodeUtils::AuxiliaryDemBinding binding;
        QString error;
        if (!NodeUtils::resolveAuxiliaryDemBinding(projectPath(), *m_auxiliaryDemEntityData, binding, &error, inputGeometry)) {
            setStartFailureMessage(error);
            return false;
        }
        m_preparedAuxiliaryDemBinding = binding;
        m_preparedDemExecutionSnapshot.binding = binding;
        m_preparedDemExecutionSnapshot.inputGeometry = inputGeometry;
        m_demPath = binding.rasterPath;
        m_demInputData = std::make_shared<DEMFileData>(binding.rasterPath, QStringLiteral("Auxiliary DEM"), binding.identityH5Path);
        m_demInputData->setProductDescriptor(m_auxiliaryDemEntityData->productDescriptor());
    }
    if (m_isTopoRemoval && !m_auxiliaryDemLabel.isEmpty()) {
        if (!m_legacyDemResourceId.isEmpty() && !m_legacyDemProvenanceId.isEmpty())
            NodeUtils::registerPendingAuxiliaryDemLabel({m_auxiliaryDemLabel, m_legacyDemResourceId, m_legacyDemProvenanceId});
        NodeUtils::AuxiliaryDemBinding binding;
        QString error;
        if (!NodeUtils::resolveAuxiliaryDemLabel(projectPath(), m_auxiliaryDemLabel, binding, &error, inputGeometry)) {
            setStartFailureMessage(error);
            return false;
        }
        m_preparedAuxiliaryDemBinding = binding;
        m_preparedDemExecutionSnapshot.binding = binding;
        m_preparedDemExecutionSnapshot.inputGeometry = inputGeometry;
        m_demPath = binding.rasterPath;
        m_auxiliaryDemReferenceData = std::make_shared<AuxiliaryDemReferenceData>(binding.resourceId, binding.pinnedProvenanceId, 1);
        m_demInputData = std::make_shared<DEMFileData>(binding.rasterPath, QStringLiteral("DEM Label"), binding.identityH5Path);
    }
    if (!validateInputs())
        return false;

    m_preparedDstNode = m_inputData->nodeName();
    m_preparedFileName = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedInputPaths.clear();

    m_preparedMasterIndex = m_useDefaultMaster ? 0 : m_masterIndex;
    m_preparedIsDeflat = m_isDeflat;
    m_preparedIsTopoRemoval = m_isTopoRemoval;
    m_preparedIsCoherence = m_isCoherence;
    m_preparedDemPath = m_demPath;
    m_preparedDemValidMaskPath.clear();
    m_preparedDemIdentityH5Path.clear();

    if (!m_preparedDemPath.isEmpty()) {
        if (!m_demInputData) {
            setStartFailureMessage(QStringLiteral("辅助 DEM 必须通过带已识别 descriptor 的输入端口提供。"));
            return false;
        }
        const ProductValidationResult demBinding = validateBoundDescriptor(
            productInputContract(1), m_demInputData->productDescriptor());
        if (!demBinding.accepted) {
            setStartFailureMessage(demBinding.reason);
            setLastErrorMessage(demBinding.reason);
            return false;
        }
        const QString demRasterPath = QDir::cleanPath(m_demInputData->filePath());
        const QString demIdentityH5Path = QDir::cleanPath(m_demInputData->identityH5Path());
        const QFileInfo demRasterInfo(demRasterPath);
        if (demRasterPath.isEmpty() ||
            !demRasterInfo.isFile() ||
            !demRasterInfo.isReadable() ||
            (demRasterInfo.suffix().compare(QStringLiteral("tif"), Qt::CaseInsensitive) != 0 &&
             demRasterInfo.suffix().compare(QStringLiteral("tiff"), Qt::CaseInsensitive) != 0)) {
            const QString reason = QStringLiteral("辅助 DEM 栅格文件不可读：%1").arg(demRasterPath);
            setStartFailureMessage(reason);
            setLastErrorMessage(reason);
            return false;
        }
        if (demIdentityH5Path.isEmpty()) {
            const QString reason = QStringLiteral("辅助 DEM 缺少配套 H5 身份文件。");
            setStartFailureMessage(reason);
            setLastErrorMessage(reason);
            return false;
        }

        const bool resourceBound = m_auxiliaryDemEntityData || m_auxiliaryDemReferenceData;
        if (m_isTopoRemoval && !resourceBound) {
            const QString reason = QStringLiteral(
                "去地形处理要求使用带可验证有效性 mask 的托管辅助 DEM；请重新生成或重新绑定 DEM。");
            setStartFailureMessage(reason);
            setLastErrorMessage(reason);
            return false;
        }
        if (!resourceBound) {
            QStringList demCommittedOutputs;
            QString demManifestError;
            if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_demInputData->nodeName(),
                                                         demCommittedOutputs, &demManifestError) ||
                !demCommittedOutputs.contains(QFileInfo(demIdentityH5Path).absoluteFilePath(), Qt::CaseInsensitive) ||
                !demCommittedOutputs.contains(QFileInfo(demRasterPath).absoluteFilePath(), Qt::CaseInsensitive)) {
                const QString reason = demManifestError.isEmpty()
                    ? QStringLiteral("辅助 DEM 的 TIFF 与 H5 不属于同一次已提交输出。")
                    : demManifestError;
                setStartFailureMessage(reason);
                setLastErrorMessage(reason);
                return false;
            }
        }

        if (resourceBound) {
            if (m_preparedAuxiliaryDemBinding.identityH5Path.isEmpty() ||
                demIdentityH5Path.compare(
                    QDir::cleanPath(m_preparedAuxiliaryDemBinding.identityH5Path),
                    Qt::CaseInsensitive) != 0) {
                const QString reason = QStringLiteral(
                    "已解析的辅助 DEM 身份文件与资源绑定不一致。");
                setStartFailureMessage(reason);
                setLastErrorMessage(reason);
                return false;
            }
            // The auxiliary-resource resolver already verifies identity.h5 via
            // the registry, generation metadata, descriptor, hashes and
            // provenance manifest. It is not a normal node transaction output.
        } else {
            QString demIdentityError;
            if (!NodeUtils::validateH5Identities(QStringList() << demIdentityH5Path,
                                                 m_demInputData->physicalProductDescriptor(),
                                                 &demIdentityError)) {
                setStartFailureMessage(demIdentityError);
                setLastErrorMessage(demIdentityError);
                return false;
            }
        }
        m_preparedDemPath = demRasterPath;
        m_preparedDemIdentityH5Path = demIdentityH5Path;
        m_preparedDemValidMaskPath = m_preparedAuxiliaryDemBinding.validMaskPath;
        const QFileInfo demMaskInfo(m_preparedDemValidMaskPath);
        if (m_isTopoRemoval && (m_preparedDemValidMaskPath.isEmpty() ||
                                !demMaskInfo.isFile() || !demMaskInfo.isReadable())) {
            const QString reason = QStringLiteral("托管辅助 DEM 缺少可读的有效性 mask。");
            setStartFailureMessage(reason);
            setLastErrorMessage(reason);
            return false;
        }
    }

    m_preparedWinW = m_winWEdit ? m_winWEdit->text().toInt() : m_winW;
    m_preparedWinH = m_winHEdit ? m_winHEdit->text().toInt() : m_winH;
    m_preparedMultilookRg = m_multilookRgEdit ? m_multilookRgEdit->text().toInt() : m_multilookRg;
    m_preparedMultilookAz = m_multilookAzEdit ? m_multilookAzEdit->text().toInt() : m_multilookAz;

    // Snapshot input H5 paths in the GUI thread. The worker only receives these
    // immutable values and never touches the project model or XML document.
    m_preparedOutputPaths.clear();
    QStandardItemModel* model = projectModel();
    if (model)
    {
        QList<QStandardItem*> foundProjects = model->findItems(m_preparedProjectName);
        if (!foundProjects.isEmpty())
        {
            QStandardItem* projectItem = foundProjects.first();
            for (int i = 0; i < projectItem->rowCount(); ++i)
            {
                QStandardItem* nodeItem = projectItem->child(i, 0);
                if (nodeItem && nodeItem->text() == m_preparedDstNode)
                {
                    for (int j = 0; j < nodeItem->rowCount(); ++j)
                    {
                        QStandardItem* pathItem = nodeItem->child(j, 1);
                        if (!pathItem)
                            continue;
                        QString inputPath = pathItem->text();
                        if (QDir::isRelativePath(inputPath)) {
                            inputPath = QDir(m_preparedSavePath).absoluteFilePath(inputPath);
                        }
                        m_preparedInputPaths.append(QDir::cleanPath(inputPath));
                    }

                    if (m_preparedMasterIndex >= 0 && m_preparedMasterIndex < m_preparedInputPaths.size())
                    {
                        const QString masterName = QFileInfo(m_preparedInputPaths.at(m_preparedMasterIndex)).baseName();
                        for (int j = 0; j < m_preparedInputPaths.size(); ++j)
                        {
                            if (j == m_preparedMasterIndex)
                                continue;
                            const QString slaveName = QFileInfo(m_preparedInputPaths.at(j)).baseName();
                            m_preparedOutputPaths.append(
                                m_preparedSavePath + "/" + m_preparedFileName + "/" +
                                masterName + "_" + slaveName + ".h5");
                        }
                    }
                    break;
                }
            }
        }
    }

    if (m_preparedInputPaths.isEmpty() ||
        m_preparedMasterIndex < 0 ||
        m_preparedMasterIndex >= m_preparedInputPaths.size()) {
        return false;
    }
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_preparedInputPaths, m_inputData->physicalProductDescriptor(),
                                         &identityError)) {
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }
    if (m_preparedOutputPaths.isEmpty()) {
        return false;
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedFileName, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void InterferometricFormationNode::executeProcessing()
{
    m_executionTimer.start();
    m_heartbeatTimer.start();
    TaskLogContext logContext;
    logContext.displayName = caption();
    InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelInfo,
                                  "InterferometricFormationNode",
                                  QStringLiteral("干涉形成任务开始：%1 幅输入影像。")
                                      .arg(m_inputData ? m_inputData->filePaths().size() : 0),
                                  LogTargets(LogTarget::UserProjectLog),
                                  QStringLiteral("starting"), QStringLiteral("running"));

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedFileName;
        
        updateParameterWidgetsEnableState();

        m_isExecuting = true;
        const ExecutionState fallbackState = lastWarningMessage().trimmed().isEmpty()
            ? ExecutionState::Completed
            : ExecutionState::Warning;

        setState(ExecutionState::Running);
        if (validateAndRestoreOutput()) {
            if (!isRestoringAsync()) {
                finalizeRestoredOutput(fallbackState, true);
            } else {
                setProgress(0);
                InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelInfo,
                                              "InterferometricFormationNode",
                                              QStringLiteral("干涉成果已恢复，正在后台生成缺失的预览图..."),
                                              LogTargets(LogTarget::UserProjectLog),
                                              QStringLiteral("running"), QStringLiteral("running"));
            }
        } else {
            m_isExecuting = false;
            // 失败时取消在途的异步补图任务，防止旧 watcher 完成时将 Error 状态洗回 Completed
            m_remedyWatcher.disconnect(this);
            if (m_remedyWatcher.isRunning()) {
                m_remedyWatcher.cancel();
            }
            setState(ExecutionState::Error);
        }
        return;
    }

    setProgress(0);
    setState(ExecutionState::Running);

    QString transactionError;
    QStringList transactionInputPaths = m_preparedInputPaths;
    if (!m_preparedDemIdentityH5Path.isEmpty()) {
        transactionInputPaths.append(m_preparedDemIdentityH5Path);
    }
    if (!m_preparedDemValidMaskPath.isEmpty()) {
        transactionInputPaths.append(m_preparedDemValidMaskPath);
    }
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedFileName,
                                           m_preparedOutputPaths, transactionInputPaths,
                                           m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> descriptorProvenance;
    descriptorProvenance.insert(QStringLiteral("producer"), name());
    descriptorProvenance.insert(QStringLiteral("output_port"),
                                productOutputContract(0).semanticId);
    const ProductDescriptor::Ptr inputDescriptor = m_inputData
        ? m_inputData->physicalProductDescriptor() : ProductDescriptor::Ptr();
    if (inputDescriptor) {
        const QMap<QString, QString> inputProvenance = inputDescriptor->provenance();
        if (inputProvenance.value(QStringLiteral("product_contract")) ==
            QStringLiteral("continuous_deburst_common_coverage_v1")) {
            const QStringList coverageFields = {
                QStringLiteral("partial_burst_coverage"),
                QStringLiteral("product_contract"),
                QStringLiteral("coverage_signature"),
                QStringLiteral("common_master_first_burst"),
                QStringLiteral("common_master_last_burst"),
                QStringLiteral("common_master_burst_count"),
                QStringLiteral("source_row_map_required"),
                QStringLiteral("geometry_reference_required")
            };
            for (const QString& field : coverageFields) {
                const QString value = inputProvenance.value(field).trimmed();
                if (value.isEmpty()) {
                    onError(QStringLiteral("共同 burst 输入缺少描述符 provenance：%1").arg(field));
                    return;
                }
                descriptorProvenance.insert(field, value);
            }
        }
    }
    const QJsonObject inputGeometry = m_inputData
        ? NodeUtils::inputGeometryFromProductDescriptor(m_inputData->physicalProductDescriptor())
        : QJsonObject();
    for (const QString& key : {QStringLiteral("minLon"), QStringLiteral("maxLon"),
                               QStringLiteral("minLat"), QStringLiteral("maxLat")}) {
        if (inputGeometry.value(key).isDouble()) {
            descriptorProvenance.insert(key, QString::number(inputGeometry.value(key).toDouble(), 'g', 17));
        }
    }
    const QString inputCrsWkt = inputGeometry.value(QStringLiteral("crsWkt")).toString().trimmed();
    if (!inputCrsWkt.isEmpty()) descriptorProvenance.insert(QStringLiteral("crsWkt"), inputCrsWkt);
    if (!NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction,
            ProductDescriptor::create(
                productOutputContract(0).publishedProductTypes.first(),
                                      QStringLiteral("sat-explorer-product"), 1,
                                      ProductState::Committed, name(), descriptorProvenance),
            &transactionError)) {
        onError(transactionError);
        return;
    }
    m_pendingInterferogramResults.clear();
    m_xmlDirty = false;
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    m_thread = new QThread();
    m_workerThread = new InterferometricFormationWorker();
    TaskLogContext workerLogContext = InSARLogManager::currentTaskContext();
    workerLogContext.nodeId = name();
    workerLogContext.displayName = caption();
    workerLogContext.scope = QStringLiteral("task");
    m_workerThread->setTaskLogContext(workerLogContext);
    m_workerThread->setDemValidMaskPath(m_preparedDemValidMaskPath);
    const QJsonObject demRequiredGeometry = m_preparedDemExecutionSnapshot.inputGeometry;
    if (demRequiredGeometry.value(QStringLiteral("minLon")).isDouble() &&
        demRequiredGeometry.value(QStringLiteral("maxLon")).isDouble() &&
        demRequiredGeometry.value(QStringLiteral("minLat")).isDouble() &&
        demRequiredGeometry.value(QStringLiteral("maxLat")).isDouble()) {
        m_workerThread->setDemRequiredBounds(
            demRequiredGeometry.value(QStringLiteral("minLon")).toDouble(),
            demRequiredGeometry.value(QStringLiteral("maxLon")).toDouble(),
            demRequiredGeometry.value(QStringLiteral("minLat")).toDouble(),
            demRequiredGeometry.value(QStringLiteral("maxLat")).toDouble());
    }
    m_workerThread->moveToThread(m_thread);

    connect(this, &InterferometricFormationNode::startInterferometric, m_workerThread, &InterferometricFormationWorker::InterferometricWithDem);
    connect(m_workerThread, &InterferometricFormationWorker::interferogramGenerated, this, &InterferometricFormationNode::onInterferogramGenerated);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, [this, stagingNode]() {
        Q_EMIT startInterferometric(m_preparedIsDeflat, m_preparedIsTopoRemoval, m_preparedIsCoherence, 
                                    m_preparedMasterIndex, m_preparedWinW, m_preparedWinH,
                                    m_preparedMultilookRg, m_preparedMultilookAz, 
                                    m_preparedSavePath, stagingNode,
                                    m_preparedInputPaths,
                                    m_preparedDemPath, true);
    });
    connect(m_workerThread, &InterferometricFormationWorker::updateProcess, this, &InterferometricFormationNode::onProgressUpdate);
    connect(m_workerThread, &InterferometricFormationWorker::endProcess, this, &InterferometricFormationNode::onProcessingFinished);
    connect(m_workerThread, &InterferometricFormationWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &InterferometricFormationWorker::errorProcess, this, &InterferometricFormationNode::onError);
    connect(m_workerThread, &InterferometricFormationWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &InterferometricFormationWorker::cancelled, this, &InterferometricFormationNode::onCancelled);
    connect(m_workerThread, &InterferometricFormationWorker::cancelled, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    deferAutomaticCompletion();
    m_thread->start();
    InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelInfo,
                                  "InterferometricFormationNode", QStringLiteral("干涉形成任务已提交。"),
                                  LogTargets(LogTarget::UserProjectLog),
                                  QStringLiteral("queued"), QStringLiteral("queued"));
    
    // Disable inputs UI
    updateParameterWidgetsEnableState();

    // Keep running state in automatic mode
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

bool InterferometricFormationNode::isRestoringAsync() const
{
    return m_remedyWatcher.isRunning();
}

void InterferometricFormationNode::publishRestoredOutputs()
{
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
}

void InterferometricFormationNode::finalizeRestoredOutput(ExecutionState fallbackState, bool isExecuting)
{
    // 1. 幂等更新有效 JPG 路径与 ImageInfoData
    QStringList validJpgPaths = previewImagePaths();
    if (!validJpgPaths.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(validJpgPaths);
        setOutputData(1, m_imageInfoData);
    } else {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
    }

    // 2. 状态收口：若有 warning message，则强制为 Warning，否则采用 fallbackState
    ExecutionState finalState = fallbackState;
    if (!lastWarningMessage().isEmpty()) {
        finalState = ExecutionState::Warning;
    }
    setState(finalState);
    _progress = 100;
    _targetProgress = 100.0;

    // 3. UI 刷新与门控单次广播
    Q_EMIT progressUpdated(100);
    Q_EMIT executionStateChanged();
    triggerVisualUpdate();
    publishRestoredOutputs();

    // 4. 清除基类暂存状态
    clearPendingSavedState();

    // 5. 若处于执行生命周期中，调用 finishExecution 结束任务
    if (isExecuting) {
        m_isExecuting = false;
        finishExecution();
    }
}

bool InterferometricFormationNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList uniqueH5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, uniqueH5Paths)) {
        return false;
    }
    ProductDescriptor::Ptr descriptor;
    if (!NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), dstNode, descriptor)) {
        return false;
    }

    QStringList h5Paths;
    QStringList expectedJpgPaths;
    QStringList types;
    const bool outputHasCoherence = m_hasOutputExecutionSettings
        ? m_outputIsCoherence : m_isCoherence;

    for (const QString& h5Path : uniqueH5Paths) {
        QFileInfo fi(h5Path);
        QString baseName = fi.baseName();

        // Phase output
        h5Paths.append(h5Path);
        expectedJpgPaths.append(fi.absolutePath() + "/" + baseName + "_phase.jpg");
        types.append("phase");

        // Coherence output (if enabled)
        if (outputHasCoherence)
        {
            h5Paths.append(h5Path);
            expectedJpgPaths.append(fi.absolutePath() + "/" + baseName + "_coh.jpg");
            types.append("coherence");
        }
    }

    m_outputData = std::make_shared<ImportedFileData>(uniqueH5Paths, dstNode);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);

    // Background preview JPG generation check and fix
    QStringList existingJpgPaths;
    QStringList missingH5s;
    QStringList missingJpgs;
    QStringList missingTypes;

    int totalExpected = expectedJpgPaths.size();
    for (int i = 0; i < totalExpected; ++i) {
        QString jpgPath = expectedJpgPaths[i];
        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], jpgPath)) {
            existingJpgPaths.append(jpgPath);
        } else {
            missingH5s.append(h5Paths[i]);
            missingJpgs.append(jpgPath);
            missingTypes.append(types[i]);
        }
    }

    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
        setOutputData(1, m_imageInfoData);
    } else {
        m_remedyWatcher.disconnect(this);
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
            // 重入上下文双值捕获：优先继承工程打开恢复暂存的 _pendingSavedState，否则根据当前警告消息确定
            const bool wasExecuting = m_isExecuting;
            const ExecutionState targetFallbackState = pendingSavedState() != ExecutionState::Idle
                ? pendingSavedState()
                : (lastWarningMessage().trimmed().isEmpty() ? ExecutionState::Completed : ExecutionState::Warning);

            connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, wasExecuting, targetFallbackState]() {
                m_remedyWatcher.disconnect(this);
                QTimer::singleShot(0, this, [this, wasExecuting, targetFallbackState]() {
                    if (validateAndRestoreOutput()) {
                        if (!isRestoringAsync()) {
                            // 复查转为同步成功：主动调用收口辅助函数，防止永久 Running 卡死
                            finalizeRestoredOutput(targetFallbackState, wasExecuting);
                        }
                    } else {
                        if (wasExecuting) {
                            m_isExecuting = false;
                            setState(ExecutionState::Error);
                        }
                    }
                });
            });
            return true;
        }

        // 异步补图上下文双值捕获：优先继承工程打开恢复暂存的 _pendingSavedState，否则根据当前警告消息确定
        const bool wasExecuting = m_isExecuting;
        const ExecutionState targetFallbackState = pendingSavedState() != ExecutionState::Idle
            ? pendingSavedState()
            : (lastWarningMessage().trimmed().isEmpty() ? ExecutionState::Completed : ExecutionState::Warning);

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, missingH5s, missingJpgs, wasExecuting, targetFallbackState]() {
            m_remedyWatcher.disconnect(this);
            bool anyFailed = false;
            for (int i = 0; i < missingJpgs.size(); ++i) {
                if (!NodeUtils::isJpgPreviewCurrent(missingH5s[i], missingJpgs[i])) {
                    anyFailed = true;
                }
            }

            if (anyFailed) {
                setLastWarningMessage(QStringLiteral("Interferometric products were restored, but some preview images could not be generated."));
                InSARLogManager::LogWarning("InterferometricFormationNode", "Output recovery finished with warnings. Some preview images failed to generate.");
            }
            finalizeRestoredOutput(anyFailed ? ExecutionState::Warning : targetFallbackState, wasExecuting);
        });

        QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs, missingTypes]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
                if (NodeUtils::isJpgPreviewCurrent(missingH5s[i], missingJpgs[i])) {
                    continue;
                }
                NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], missingTypes[i]);
            }
        });
        m_remedyWatcher.setFuture(future);
    }

    return true;
}

QStringList InterferometricFormationNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return list;

    QStringList h5Paths;
    const bool outputHasCoherence = m_hasOutputExecutionSettings
        ? m_outputIsCoherence : m_isCoherence;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        for (const QString& h5Path : h5Paths) {
            const QFileInfo info(h5Path);
            const QString phaseJpg = info.absolutePath() + "/" + info.baseName() + "_phase.jpg";
            if (QFile::exists(phaseJpg)) {
                list.append(phaseJpg);
            }
            if (outputHasCoherence) {
                const QString cohJpg = info.absolutePath() + "/" + info.baseName() + "_coh.jpg";
                if (QFile::exists(cohJpg)) {
                    list.append(cohJpg);
                }
            }
        }
    }
    return list;
}

void InterferometricFormationNode::updateParameterWidgetsEnableState()
{
    bool isExec = m_thread && m_thread->isRunning();
    bool enableWidgets = !isExec;

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets);
    if (m_defaultMasterCheckBox) m_defaultMasterCheckBox->setEnabled(enableWidgets);
    if (m_masterImageCombo) m_masterImageCombo->setEnabled(enableWidgets && !m_useDefaultMaster);
    if (m_deflatCheckBox) m_deflatCheckBox->setEnabled(enableWidgets);
    if (m_topoRemovalCheckBox) m_topoRemovalCheckBox->setEnabled(enableWidgets);
    if (m_coherenceCheckBox) m_coherenceCheckBox->setEnabled(enableWidgets);

    bool coherenceEnabled = enableWidgets && m_isCoherence;
    if (m_winWLabel) m_winWLabel->setEnabled(coherenceEnabled);
    if (m_winWEdit) m_winWEdit->setEnabled(coherenceEnabled);
    if (m_winHLabel) m_winHLabel->setEnabled(coherenceEnabled);
    if (m_winHEdit) m_winHEdit->setEnabled(coherenceEnabled);

    if (m_multilookRgEdit) m_multilookRgEdit->setEnabled(enableWidgets);
    if (m_multilookAzEdit) m_multilookAzEdit->setEnabled(enableWidgets);

    bool hasDemConn = (m_demInputData != nullptr);
    bool demNeeded = m_isDeflat || m_isTopoRemoval;
    bool demEnabled = enableWidgets && demNeeded && !hasDemConn;

    if (m_demPathLabel) m_demPathLabel->setEnabled(demEnabled);
    if (m_demLabelCombo) m_demLabelCombo->setEnabled(demEnabled);
}

void InterferometricFormationNode::refreshAuxiliaryDemLabels()
{
    if (!m_demLabelCombo) return;
    const QSet<QString> declared = workflowDeclaredDemLabels();
    const QHash<QString, QString> producerNodeIds = workflowDemProducerNodeIdMap();
    QMap<QString, NodeUtils::AuxiliaryDemLabelBinding> labels;
    QString error;
    NodeUtils::loadAuxiliaryDemLabels(projectPath(), labels, &error);
    QSignalBlocker blocker(m_demLabelCombo);
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
    m_demLabelCombo->setCurrentIndex(index < 0 ? 0 : index);
}

void InterferometricFormationNode::captureOutputExecutionSettings()
{
    m_hasOutputExecutionSettings = true;
    m_outputIsDeflat = m_preparedIsDeflat;
    m_outputIsTopoRemoval = m_preparedIsTopoRemoval;
    m_outputIsCoherence = m_preparedIsCoherence;
    m_outputWinW = m_preparedWinW;
    m_outputWinH = m_preparedWinH;
    m_outputMultilookRg = m_preparedMultilookRg;
    m_outputMultilookAz = m_preparedMultilookAz;
}

void InterferometricFormationNode::startPreviewGeneration(const QStringList& previewSourcePaths,
                                                          const QStringList& jpgPaths,
                                                          const QStringList& types,
                                                          const QStringList& outputPaths)
{
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.disconnect(this);
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, previewSourcePaths, jpgPaths, types, outputPaths]() {
            m_remedyWatcher.disconnect(this);
            startPreviewGeneration(previewSourcePaths, jpgPaths, types, outputPaths);
        });
        return;
    }

    m_remedyWatcher.disconnect(this);
    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, previewSourcePaths, jpgPaths, outputPaths]() {
        if (discardObsoleteAutomaticExecution()) {
            return;
        }

        QStringList validJpgPaths;
        bool anyFailed = false;
        for (int i = 0; i < jpgPaths.size(); ++i) {
            if (NodeUtils::isJpgPreviewCurrent(previewSourcePaths[i], jpgPaths[i])) {
                validJpgPaths.append(jpgPaths[i]);
            } else {
                anyFailed = true;
            }
        }

        if (!validJpgPaths.isEmpty()) {
            m_imageInfoData = std::make_shared<ImageInfoData>(validJpgPaths);
            setOutputData(1, m_imageInfoData);
        } else {
            m_imageInfoData.reset();
            setOutputData(1, nullptr);
        }

        updateParameterWidgetsEnableState();
        if (anyFailed) {
            setLastWarningMessage(QStringLiteral("Interferometric products were generated, but some preview images could not be generated."));
            TaskLogContext logContext;
            logContext.displayName = caption();
            InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelWarning,
                                          "InterferometricFormationNode",
                                          QStringLiteral("干涉形成完成，但部分预览图生成失败。"),
                                          LogTargets(LogTarget::UserProjectLog) | LogTarget::DebugConsole,
                                          QStringLiteral("completed"), QStringLiteral("completed_with_warnings"),
                                          m_executionTimer.isValid() ? m_executionTimer.elapsed() : -1);
            finishExecutionWithWarning();
        } else {
            setProgress(100);
            TaskLogContext logContext;
            logContext.displayName = caption();
            InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelInfo,
                                          "InterferometricFormationNode",
                                          QStringLiteral("干涉形成完成。输出：%1")
                                              .arg(outputPaths.join(QStringLiteral(", "))),
                                          LogTargets(LogTarget::UserProjectLog),
                                          QStringLiteral("completed"), QStringLiteral("completed"),
                                          m_executionTimer.isValid() ? m_executionTimer.elapsed() : -1);
            finishExecution();
        }
    });
    m_remedyWatcher.setFuture(QtConcurrent::run([previewSourcePaths, jpgPaths, types]() {
        for (int i = 0; i < previewSourcePaths.size(); ++i) {
            if (NodeUtils::isJpgPreviewCurrent(previewSourcePaths[i], jpgPaths[i])) {
                continue;
            }
            NodeUtils::generateJpgPreviewFromH5(previewSourcePaths[i], jpgPaths[i], types[i]);
        }
    }));
}

QVector<ParameterInfo> InterferometricFormationNode::getParameters() const
{
    QVector<ParameterInfo> params;

    int mlRg = m_multilookRg;
    int mlAz = m_multilookAz;
    bool isDeflat = m_isDeflat;
    bool isTopo = m_isTopoRemoval;
    bool isCoh = m_isCoherence;
    int winW = m_winW;
    int winH = m_winH;

    if (m_hasOutputExecutionSettings) {
        mlRg = m_outputMultilookRg;
        mlAz = m_outputMultilookAz;
        isDeflat = m_outputIsDeflat;
        isTopo = m_outputIsTopoRemoval;
        isCoh = m_outputIsCoherence;
        winW = m_outputWinW;
        winH = m_outputWinH;
    } else if (executionState() == ExecutionState::Running) {
        mlRg = m_preparedMultilookRg;
        mlAz = m_preparedMultilookAz;
        isDeflat = m_preparedIsDeflat;
        isTopo = m_preparedIsTopoRemoval;
        isCoh = m_preparedIsCoherence;
        winW = m_preparedWinW;
        winH = m_preparedWinH;
    }

    // 注意：IFNode 的 setParameter 为基类 no-op，所有参数卡片必须使用 FieldEditType::None 纯只读渲染，
    // 避免在 PropertyEditor 中生成无响应的编辑控件。
    ParameterInfo pMultilookRg;
    pMultilookRg.name = QStringLiteral("距离向视数");
    pMultilookRg.value = QString::number(mlRg);
    pMultilookRg.dataType = QStringLiteral("int");
    pMultilookRg.editType = FieldEditType::None;
    params.append(pMultilookRg);

    ParameterInfo pMultilookAz;
    pMultilookAz.name = QStringLiteral("方位向视数");
    pMultilookAz.value = QString::number(mlAz);
    pMultilookAz.dataType = QStringLiteral("int");
    pMultilookAz.editType = FieldEditType::None;
    params.append(pMultilookAz);

    ParameterInfo pDeflat;
    pDeflat.name = QStringLiteral("去平地相位");
    pDeflat.value = isDeflat ? QStringLiteral("开启") : QStringLiteral("关闭");
    pDeflat.dataType = QStringLiteral("bool");
    pDeflat.editType = FieldEditType::None;
    params.append(pDeflat);

    ParameterInfo pTopo;
    pTopo.name = QStringLiteral("去地形相位");
    pTopo.value = isTopo ? QStringLiteral("开启") : QStringLiteral("关闭");
    pTopo.dataType = QStringLiteral("bool");
    pTopo.editType = FieldEditType::None;
    params.append(pTopo);

    ParameterInfo pCoh;
    pCoh.name = QStringLiteral("二倍角相位集中度计算");
    pCoh.value = isCoh ? QStringLiteral("开启") : QStringLiteral("关闭");
    pCoh.dataType = QStringLiteral("bool");
    pCoh.editType = FieldEditType::None;
    params.append(pCoh);

    if (isCoh) {
        ParameterInfo pWinW;
        pWinW.name = QStringLiteral("相干窗口宽度");
        pWinW.value = QString::number(winW);
        pWinW.dataType = QStringLiteral("int");
        pWinW.editType = FieldEditType::None;
        params.append(pWinW);

        ParameterInfo pWinH;
        pWinH.name = QStringLiteral("相干窗口高度");
        pWinH.value = QString::number(winH);
        pWinH.dataType = QStringLiteral("int");
        pWinH.editType = FieldEditType::None;
        params.append(pWinH);
    }

    return params;
}

std::vector<QString> InterferometricFormationNode::processingInfo() const
{
    std::vector<QString> info;

    int mlRg = m_multilookRg;
    int mlAz = m_multilookAz;
    bool isDeflat = m_isDeflat;
    bool isTopo = m_isTopoRemoval;
    bool isCoh = m_isCoherence;
    int winW = m_winW;
    int winH = m_winH;
    const bool isCompletedOutput = m_hasOutputExecutionSettings;

    if (m_hasOutputExecutionSettings) {
        mlRg = m_outputMultilookRg;
        mlAz = m_outputMultilookAz;
        isDeflat = m_outputIsDeflat;
        isTopo = m_outputIsTopoRemoval;
        isCoh = m_outputIsCoherence;
        winW = m_outputWinW;
        winH = m_outputWinH;
    } else if (executionState() == ExecutionState::Running) {
        mlRg = m_preparedMultilookRg;
        mlAz = m_preparedMultilookAz;
        isDeflat = m_preparedIsDeflat;
        isTopo = m_preparedIsTopoRemoval;
        isCoh = m_preparedIsCoherence;
        winW = m_preparedWinW;
        winH = m_preparedWinH;
    }

    const QString suffix = isCompletedOutput ? QString() : QStringLiteral(" (当前配置)");
    info.push_back(QStringLiteral("输出节点：%1").arg(m_outputNodeName.isEmpty() ? QStringLiteral("未指定") : m_outputNodeName));
    info.push_back(QStringLiteral("多视设置：距离向 %1 视 x 方位向 %2 视%3").arg(mlRg).arg(mlAz).arg(suffix));
    info.push_back(QStringLiteral("去平地相位：%1%2")
        .arg(isDeflat ? QStringLiteral("开启 (轨道辅助)") : QStringLiteral("未开启"))
        .arg(suffix));
    
    if (isTopo) {
        QString demDesc = m_auxiliaryDemLabel.isEmpty() ? QStringLiteral("通过连线/自动匹配") : m_auxiliaryDemLabel;
        if (!m_demPath.isEmpty()) {
            demDesc += QStringLiteral(" (%1)").arg(QFileInfo(m_demPath).fileName());
        }
        info.push_back(QStringLiteral("去地形相位：开启 [DEM: %1]%2").arg(demDesc).arg(suffix));
    } else {
        info.push_back(QStringLiteral("去地形相位：未开启%1").arg(suffix));
    }

    if (isCoh) {
        info.push_back(QStringLiteral("相位集中度计算：开启 (窗口 %1 x %2, 名义样本数 %3)%4")
            .arg(winW).arg(winH).arg(winW * winH).arg(suffix));
    } else {
        info.push_back(QStringLiteral("相位集中度计算：未开启%1").arg(suffix));
    }

    if (m_outputData && !m_outputData->filePaths().isEmpty()) {
        info.push_back(QStringLiteral("干涉对生成数量：%1 对").arg(m_outputData->filePaths().size()));
    }
    return info;
}

QString InterferometricFormationNode::portBindingSummary(PortType portType, PortIndex portIndex) const
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

// --------------------------------------------------------------------------------
// InterferometricFormationEvalWidget - 干涉形成质量评估选项卡组件
// --------------------------------------------------------------------------------

struct InterfEvalThreadResult {
    bool success;
    float meanCoh;
    float medianCoh;
    float maxCoh;
    float highCohPct;
    bool hasSupportMetadata;
    float excludedSupportPct;
    int supportWindowRg;
    int supportWindowAz;
    // 该 coherence 数据集的语义标签（见 NodeUtils::kCoherenceSemantics*）。
    // 缺标签的存量文件为 legacy_unknown，不可假定为任一具体语义。
    QString semantics;
    QString errorMessage;
};

class InterferometricFormationEvalWidget : public QWidget
{
public:
    explicit InterferometricFormationEvalWidget(InterferometricFormationNode* node, QWidget* parent = nullptr)
        : QWidget(parent), m_node(node)
    {
        // 界面布局
        auto* mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);
        mainLayout->setSpacing(12);

        // 左侧栏：评估参数与定量指标显示
        auto* leftContainer = new QWidget();
        auto* leftLayout = new QVBoxLayout(leftContainer);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(10);

        auto* selectionLayout = new QHBoxLayout();
        auto* selLabel = new QLabel(tr("分析影像对:"));
        selLabel->setStyleSheet("font-weight: bold;");
        selectionLayout->addWidget(selLabel);

        m_slaveCombo = new QComboBox();
        selectionLayout->addWidget(m_slaveCombo, 1);
        leftLayout->addLayout(selectionLayout);

        // 状态评估卡片
        m_statusCard = new QFrame();
        m_statusCard->setFrameShape(QFrame::StyledPanel);
        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
        
        auto* cardLayout = new QVBoxLayout(m_statusCard);
        cardLayout->setContentsMargins(10, 8, 10, 8);
        cardLayout->setSpacing(4);

        m_statusCardTitle = new QLabel(tr("未评估"));
        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
        cardLayout->addWidget(m_statusCardTitle);

        m_statusCardDesc = new QLabel(tr("请点击评估获取相干性及干涉质量诊断结果。"));
        m_statusCardDesc->setWordWrap(true);
        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
        cardLayout->addWidget(m_statusCardDesc);

        leftLayout->addWidget(m_statusCard);

        // 定量指标统计表
        auto* metricsFrame = new QFrame();
        metricsFrame->setFrameShape(QFrame::StyledPanel);
        bool isDark = NodeDetailWindow::isDarkTheme(this);
        metricsFrame->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
            .arg(isDark ? "#374151" : "#FFFFFF")
            .arg(isDark ? "#4B5563" : "#E5E7EB"));
        
        auto* formLayout = new QFormLayout(metricsFrame);
        formLayout->setContentsMargins(12, 12, 12, 12);
        formLayout->setSpacing(10);
        formLayout->setLabelAlignment(Qt::AlignLeft);

        auto createValueLabel = [isDark]() {
            auto* label = new QLabel("-");
            label->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
            return label;
        };

        m_meanCohLabel = createValueLabel();
        m_medianCohLabel = createValueLabel();
        m_maxCohLabel = createValueLabel();
        m_highCohPctLabel = createValueLabel();
        m_enlLabel = createValueLabel();
        m_flatEarthLabel = createValueLabel();
        m_topoLabel = createValueLabel();

        auto addFormRow = [formLayout, isDark](const QString& title, QWidget* valueWidget) {
            auto* label = new QLabel(title);
            label->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
            formLayout->addRow(label, valueWidget);
        };

        // 生成可后续改写文本的行标题
        auto makeTitleLabel = [isDark](const QString& title) {
            auto* label = new QLabel(title);
            label->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
            return label;
        };

        // 指标行标题在评估完成后按 H5 中的 coherence_semantics 标签动态改写，
        // 因为同名数据集可能是 gamma、R1 或 R2，量纲不同且不可换算。
        m_meanTitleLabel = makeTitleLabel(tr("平均值:"));
        m_medianTitleLabel = makeTitleLabel(tr("中位值:"));
        m_maxTitleLabel = makeTitleLabel(tr("最高值:"));
        formLayout->addRow(m_meanTitleLabel, m_meanCohLabel);
        formLayout->addRow(m_medianTitleLabel, m_medianCohLabel);
        formLayout->addRow(m_maxTitleLabel, m_maxCohLabel);
        addFormRow(tr("高值占比 (>0.5):"), m_highCohPctLabel);
        addFormRow(tr("集中度估算窗口:"), m_enlLabel);
        addFormRow(tr("平地相位状态:"), m_flatEarthLabel);
        addFormRow(tr("地形相位状态:"), m_topoLabel);

        leftLayout->addWidget(metricsFrame);

        m_statusLabel = new QLabel(tr("准备就绪。请选择影像对开始评估。"));
        m_statusLabel->setWordWrap(true);
        m_statusLabel->setStyleSheet(isDark ? "color: #9CA3AF; font-size: 11px;" : "color: #6B7280; font-size: 11px;");
        leftLayout->addWidget(m_statusLabel);

        leftLayout->addStretch(1);
        mainLayout->addWidget(leftContainer, 4);

        // 右侧栏：大图显示与双模选择
        auto* rightContainer = new QWidget();
        auto* rightLayout = new QVBoxLayout(rightContainer);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(8);

        auto* topBarLayout = new QHBoxLayout();
        
        m_evalBtn = new QPushButton(tr(" 执行质量评估 "));
        m_evalBtn->setStyleSheet(
            "QPushButton { background-color: #3B82F6; color: white; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: #2563EB; }"
            "QPushButton:pressed { background-color: #1D4ED8; }"
            "QPushButton:disabled { background-color: #9CA3AF; }"
        );
        topBarLayout->addWidget(m_evalBtn);
        
        topBarLayout->addStretch(1);

        auto* viewModeLabel = new QLabel(tr("视图切换:"));
        viewModeLabel->setStyleSheet("font-weight: bold;");
        topBarLayout->addWidget(viewModeLabel);

        m_visualModeCombo = new QComboBox();
        m_visualModeCombo->addItem(tr("干涉相位 (Phase)"));
        m_visualModeCombo->addItem(tr("相干系数 (Coherence)"));
        topBarLayout->addWidget(m_visualModeCombo);

        rightLayout->addLayout(topBarLayout);

        m_imageView = new ImageView();
        m_imageView->setStyleSheet(QString("border: 1px solid %1; background-color: %2;")
            .arg(isDark ? "#4B5563" : "#D1D5DB")
            .arg(isDark ? "#111827" : "#F3F4F6"));
        rightLayout->addWidget(m_imageView, 1);

        mainLayout->addWidget(rightContainer, 6);

        // 初始化数据
        updateAvailablePairs();

        connect(m_slaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InterferometricFormationEvalWidget::onPairChanged);
        connect(m_visualModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InterferometricFormationEvalWidget::updatePreviewImage);
        connect(m_evalBtn, &QPushButton::clicked, this, &InterferometricFormationEvalWidget::startEvaluation);
        connect(&m_watcher, &QFutureWatcher<InterfEvalThreadResult>::finished, this, &InterferometricFormationEvalWidget::onEvaluationFinished);

        // 默认触发一次
        if (m_slaveCombo->count() > 0) {
            onPairChanged();
        } else {
            m_evalBtn->setEnabled(false);
        }
    }

    ~InterferometricFormationEvalWidget() override
    {
        m_watcher.cancel();
    }

private:
    void setCoherenceVisualModeEnabled(bool enabled)
    {
        auto* itemModel = qobject_cast<QStandardItemModel*>(m_visualModeCombo->model());
        if (itemModel && itemModel->rowCount() >= 2) {
            if (QStandardItem* cohItem = itemModel->item(1)) {
                cohItem->setEnabled(enabled);
            }
        }
    }

    void updateAvailablePairs()
    {
        m_slaveCombo->blockSignals(true);
        m_slaveCombo->clear();
        m_h5PathsPhase.clear();
        m_jpgPathsPhase.clear();
        m_jpgPathsCoh.clear();

        // 与 Idle / Pending / Error 等状态处理方式一致：仅已完成（或警告）节点可展示可评估成果，
        // 状态变化后不再暴露旧的已提交干涉对预览。
        const ExecutionState state = m_node->executionState();
        if (state != ExecutionState::Completed && state != ExecutionState::Warning) {
            setCoherenceVisualModeEnabled(false);
            m_slaveCombo->addItem(tr("无干涉对"));
            m_evalBtn->setEnabled(false);
            m_imageView->setImage(QImage());
            m_slaveCombo->blockSignals(false);
            return;
        }

        QStringList previews = m_node->previewImagePaths();
        for (const QString& jpgPath : previews) {
            if (jpgPath.endsWith("_phase.jpg")) {
                m_jpgPathsPhase.append(jpgPath);
                QString h5Path = jpgPath;
                h5Path.replace("_phase.jpg", ".h5");
                m_h5PathsPhase.append(h5Path);
            } else if (jpgPath.endsWith("_coh.jpg")) {
                m_jpgPathsCoh.append(jpgPath);
            }
        }

        if (m_jpgPathsPhase.isEmpty()) {
            m_slaveCombo->addItem(tr("无干涉对"));
            setCoherenceVisualModeEnabled(false);
            m_evalBtn->setEnabled(false);
            m_imageView->setImage(QImage());
        } else {
            for (const QString& jpg : m_jpgPathsPhase) {
                QString name = QFileInfo(jpg).baseName();
                name.replace("_phase", "");
                m_slaveCombo->addItem(name);
            }
        }
        m_slaveCombo->blockSignals(false);

        if (m_slaveCombo->count() > 0 && !m_jpgPathsPhase.isEmpty()) {
            onPairChanged();
        }
    }

    void onPairChanged()
    {
        resetMetrics();
        const ExecutionState st = m_node->executionState();
        if (st != ExecutionState::Completed && st != ExecutionState::Warning) {
            setCoherenceVisualModeEnabled(false);
            m_evalBtn->setEnabled(false);
            updatePreviewImage();
            return;
        }

        int pairIdx = m_slaveCombo->currentIndex();
        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
            setCoherenceVisualModeEnabled(false);
            m_evalBtn->setEnabled(false);
            updatePreviewImage();
            return;
        }

        // 检查当前选中的干涉对是否存在对应的相干图
        QString expectedCohJpg = m_jpgPathsPhase[pairIdx];
        expectedCohJpg.replace("_phase.jpg", "_coh.jpg");
        bool hasCurrentPairCoh = m_jpgPathsCoh.contains(expectedCohJpg);

        if (hasCurrentPairCoh) {
            setCoherenceVisualModeEnabled(true);
            m_evalBtn->setEnabled(true);
            m_statusLabel->setText(tr("准备就绪。请点击执行评估。"));
        } else {
            // 当前对无相干图：若处于相干模式则安全切回相位模式
            if (m_visualModeCombo->currentIndex() == 1) {
                m_visualModeCombo->blockSignals(true);
                m_visualModeCombo->setCurrentIndex(0);
                m_visualModeCombo->blockSignals(false);
            }
            setCoherenceVisualModeEnabled(false);
            m_evalBtn->setEnabled(false);
            m_statusLabel->setText(tr("当前选中的干涉对未包含相干系数数据（未启用或未生成相干系数）。"));
        }

        updatePreviewImage();
    }

    void updatePreviewImage()
    {
        int pairIdx = m_slaveCombo->currentIndex();
        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
            m_imageView->setImage(QImage());
            return;
        }

        int viewMode = m_visualModeCombo->currentIndex();
        QString pathToLoad;

        if (viewMode == 0) { // Phase
            pathToLoad = m_jpgPathsPhase[pairIdx];
        } else { // Coherence
            // 查找对应的相干系数图
            QString expectedCohJpg = m_jpgPathsPhase[pairIdx];
            expectedCohJpg.replace("_phase.jpg", "_coh.jpg");
            if (m_jpgPathsCoh.contains(expectedCohJpg)) {
                pathToLoad = expectedCohJpg;
            }
        }

        if (!pathToLoad.isEmpty() && QFile::exists(pathToLoad)) {
            QImage img(pathToLoad);
            m_imageView->setImage(img);
        } else {
            m_imageView->setImage(QImage());
        }
    }

    void resetMetrics()
    {
        m_meanTitleLabel->setText(tr("平均值:"));
        m_medianTitleLabel->setText(tr("中位值:"));
        m_maxTitleLabel->setText(tr("最高值:"));
        m_meanCohLabel->setText("-");
        m_medianCohLabel->setText("-");
        m_maxCohLabel->setText("-");
        m_highCohPctLabel->setText("-");
        m_enlLabel->setText("-");
        m_flatEarthLabel->setText("-");
        m_flatEarthLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #6B7280;");
        m_topoLabel->setText("-");
        m_topoLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #6B7280;");
        
        m_statusCardTitle->setText(tr("未评估"));
        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
        m_statusCardDesc->setText(tr("请点击评估获取相干性及干涉质量诊断结果。"));
        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
        
        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
        m_statusLabel->setText(tr("准备就绪。请点击执行评估。"));
    }

    void startEvaluation()
    {
        int pairIdx = m_slaveCombo->currentIndex();
        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
            return;
        }

        // 相干系数数据存在于与之同名的 H5 文件中
        QString cohH5Path = m_h5PathsPhase[pairIdx];

        // 检查对应的 _coh.jpg 是否存在，以确认节点确实生成了相干系数
        QString expectedCohJpg = m_jpgPathsPhase[pairIdx];
        expectedCohJpg.replace("_phase.jpg", "_coh.jpg");
        if (!m_jpgPathsCoh.contains(expectedCohJpg)) {
            cohH5Path = ""; // 强行置空触发下方报错
        }

        if (cohH5Path.isEmpty() || !QFile::exists(cohH5Path)) {
            m_statusLabel->setText(tr("无法评估：未找到该干涉对的相干系数成果文件。请检查节点是否勾选了“计算相干系数”。"));
            return;
        }

        m_evalBtn->setEnabled(false);
        m_slaveCombo->setEnabled(false);
        m_statusLabel->setText(tr("正在读取 H5 文件并计算相干性统计信息，请稍候..."));

        QFuture<InterfEvalThreadResult> future = QtConcurrent::run([cohH5Path]() {
            InterfEvalThreadResult res;
            res.success = false;
            res.meanCoh = 0.0f;
            res.medianCoh = 0.0f;
            res.maxCoh = 0.0f;
            res.highCohPct = 0.0f;
            res.hasSupportMetadata = false;
            res.excludedSupportPct = 0.0f;
            res.supportWindowRg = 0;
            res.supportWindowAz = 0;
            // 读取语义标签：存量文件无标签时退回 legacy_unknown，不假定为任何具体语义
            if (!NodeUtils::readCoherenceSemantics(cohH5Path, res.semantics, nullptr)) {
                res.semantics = QString::fromLatin1(NodeUtils::CoherenceSemantics::kLegacyUnknown);
            }

            cv::Mat cohMat;
            QString errMsg;
            if (!NodeUtils::readMatFromH5(cohH5Path, "coherence", cohMat, CV_32F, &errMsg)) {
                res.errorMessage = QStringLiteral("读取相干系数数据集失败: %1").arg(errMsg);
                return res;
            }

            if (cohMat.empty()) {
                res.errorMessage = QStringLiteral("相干系数矩阵为空。");
                return res;
            }

            cv::Mat validSampleCount;
            int supportWindowRg = 0;
            int supportWindowAz = 0;
            const bool supportCountRead = NodeUtils::readMatFromH5(
                cohH5Path,
                QString::fromLatin1(NodeUtils::CoherenceSupport::kValidSampleCountDataset),
                validSampleCount,
                CV_32S,
                nullptr);
            const bool supportWindowRgRead = NodeUtils::readScalarFromH5(
                cohH5Path,
                QString::fromLatin1(NodeUtils::CoherenceSupport::kWindowRangeDataset),
                supportWindowRg);
            const bool supportWindowAzRead = NodeUtils::readScalarFromH5(
                cohH5Path,
                QString::fromLatin1(NodeUtils::CoherenceSupport::kWindowAzimuthDataset),
                supportWindowAz);
            const bool hasAnySupportMetadata = supportCountRead || supportWindowRgRead || supportWindowAzRead;
            if (hasAnySupportMetadata &&
                (!supportCountRead || !supportWindowRgRead || !supportWindowAzRead)) {
                res.errorMessage = QStringLiteral("相干性有效样本支持元数据不完整。");
                return res;
            }
            if (hasAnySupportMetadata) {
                if (validSampleCount.empty() || validSampleCount.size() != cohMat.size() ||
                    supportWindowRg < 1 || supportWindowAz < 1 ||
                    supportWindowRg > std::numeric_limits<int>::max() / supportWindowAz) {
                    res.errorMessage = QStringLiteral("相干性有效样本支持元数据无效或尺寸不匹配。");
                    return res;
                }
                res.hasSupportMetadata = true;
                res.supportWindowRg = supportWindowRg;
                res.supportWindowAz = supportWindowAz;
            }

            // 计算统计信息
            // 为了计算中位数和占比，需要遍历所有有效像素
            std::vector<float> validPixels;
            // 预估大小，如果是超大矩阵，可以隔行采样
            int step = 1;
            if (cohMat.total() > 5000000) {
                step = qMax(1, (int)(cohMat.total() / 5000000));
            }
            
            float maxCoh = 0.0f;
            double sumCoh = 0.0;
            long long highCount = 0;
            long long sampledCount = 0;
            long long excludedSupportCount = 0;
            const int requiredSupport = res.hasSupportMetadata
                ? res.supportWindowRg * res.supportWindowAz : 0;

            if (cohMat.isContinuous() &&
                (!res.hasSupportMetadata || validSampleCount.isContinuous())) {
                const float* ptr = cohMat.ptr<float>();
                const int* supportPtr = res.hasSupportMetadata
                    ? validSampleCount.ptr<int>() : nullptr;
                int total = cohMat.total();
                for (int i = 0; i < total; i += step) {
                    ++sampledCount;
                    if (supportPtr != nullptr && supportPtr[i] != requiredSupport) {
                        ++excludedSupportCount;
                        continue;
                    }
                    float val = ptr[i];
                    if (std::isfinite(val) && val >= 0.0f && val <= 1.0f) {
                        validPixels.push_back(val);
                        sumCoh += val;
                        if (val > maxCoh) maxCoh = val;
                        if (val > 0.5f) highCount++;
                    }
                }
            } else {
                // 非连续矩阵按“全局线性下标”抽样，保持与连续分支一致的 1/step 抽样比例
                // （若行、列各自按 step 跳跃，实际抽样比例会退化为 1/step^2）
                size_t linearIdx = 0;
                for (int r = 0; r < cohMat.rows; ++r) {
                    const float* ptr = cohMat.ptr<float>(r);
                    const int* supportPtr = res.hasSupportMetadata
                        ? validSampleCount.ptr<int>(r) : nullptr;
                    int c = (int)((step - (linearIdx % step)) % step);
                    for (; c < cohMat.cols; c += step) {
                        ++sampledCount;
                        if (supportPtr != nullptr && supportPtr[c] != requiredSupport) {
                            ++excludedSupportCount;
                            continue;
                        }
                        float val = ptr[c];
                        if (std::isfinite(val) && val >= 0.0f && val <= 1.0f) {
                            validPixels.push_back(val);
                            sumCoh += val;
                            if (val > maxCoh) maxCoh = val;
                            if (val > 0.5f) highCount++;
                        }
                    }
                    linearIdx += (size_t)cohMat.cols;
                }
            }

            if (validPixels.empty()) {
                res.errorMessage = QStringLiteral("相干系数矩阵中没有有效数据。");
                return res;
            }

            res.meanCoh = sumCoh / validPixels.size();
            res.maxCoh = maxCoh;
            res.highCohPct = (float)highCount / validPixels.size() * 100.0f;
            if (res.hasSupportMetadata && sampledCount > 0) {
                res.excludedSupportPct = static_cast<float>(excludedSupportCount) /
                    static_cast<float>(sampledCount) * 100.0f;
            }

            size_t n = validPixels.size() / 2;
            std::nth_element(validPixels.begin(), validPixels.begin() + n, validPixels.end());
            res.medianCoh = validPixels[n];

            res.success = true;
            return res;
        });

        m_watcher.setFuture(future);
    }

    void onEvaluationFinished()
    {
        m_evalBtn->setEnabled(true);
        m_slaveCombo->setEnabled(true);

        InterfEvalThreadResult res = m_watcher.result();
        if (!res.success) {
            m_statusLabel->setText(tr("评估失败：%1").arg(res.errorMessage));
            return;
        }

        m_statusLabel->setText(tr("评估完成。"));

        m_meanCohLabel->setText(QString::number(res.meanCoh, 'f', 4));
        m_medianCohLabel->setText(QString::number(res.medianCoh, 'f', 4));
        m_maxCohLabel->setText(QString::number(res.maxCoh, 'f', 4));
        m_highCohPctLabel->setText(QString("%1 %").arg(res.highCohPct, 0, 'f', 2));

        // 按 H5 中的语义标签决定指标名称：coherence 数据集可能是 gamma、R1 或 R2，
        // 三者量纲不同且不可换算，必须按标签展示，不能沿用固定文案
        const QString metricName = NodeUtils::coherenceSemanticsDisplayName(res.semantics);
        m_meanTitleLabel->setText(tr("平均%1:").arg(metricName));
        m_medianTitleLabel->setText(tr("中位%1:").arg(metricName));
        m_maxTitleLabel->setText(tr("最高%1:").arg(metricName));

        if (res.hasSupportMetadata) {
            const int samples = res.supportWindowRg * res.supportWindowAz;
            m_enlLabel->setText(QString(tr("%1 x %2 (名义样本数 %3)"))
                .arg(res.supportWindowRg)
                .arg(res.supportWindowAz)
                .arg(samples));
        } else if (m_node->m_hasOutputExecutionSettings) {
            const int looks = m_node->m_outputWinW * m_node->m_outputWinH;
            // 窗口像元数仅为名义样本数，不等同于有效视数（ENL）
            m_enlLabel->setText(QString(tr("%1 x %2 (名义样本数 %3)"))
                .arg(m_node->m_outputWinW)
                .arg(m_node->m_outputWinH)
                .arg(looks));
        } else {
            m_enlLabel->setText(tr("未记录"));
        }

        if (!m_node->m_hasOutputExecutionSettings) {
            m_flatEarthLabel->setText(tr("未记录"));
            m_topoLabel->setText(tr("未记录"));
            m_flatEarthLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #6B7280;");
            m_topoLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #6B7280;");
        } else {
            m_flatEarthLabel->setText(m_node->m_outputIsDeflat
                ? tr("[√] 已移除 (轨道辅助)") : tr("未移除"));
            m_flatEarthLabel->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1;")
                .arg(m_node->m_outputIsDeflat ? "#10B981" : "#6B7280"));
            m_topoLabel->setText(m_node->m_outputIsTopoRemoval
                ? tr("[√] 已移除 (DEM辅助)") : tr("未移除"));
            m_topoLabel->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1;")
                .arg(m_node->m_outputIsTopoRemoval ? "#10B981" : "#6B7280"));
        }

        const bool isAxialR2 =
            res.semantics == QString::fromLatin1(NodeUtils::CoherenceSemantics::kPhaseAxialR2);
        const bool isCircularR1 =
            res.semantics == QString::fromLatin1(NodeUtils::CoherenceSemantics::kPhaseCircularR1);
        const bool isComplexGamma =
            res.semantics == QString::fromLatin1(NodeUtils::CoherenceSemantics::kComplexGamma);

        QString processingState;
        if (!m_node->m_hasOutputExecutionSettings) {
            processingState = tr("平地和地形相位处理状态未记录");
        } else {
            processingState = tr("平地相位%1，地形相位%2")
                .arg(m_node->m_outputIsDeflat ? tr("已移除") : tr("未移除"))
                .arg(m_node->m_outputIsTopoRemoval ? tr("已移除") : tr("未移除"));
        }

        const QString supportState = res.hasSupportMetadata
            ? tr("统计已排除 %1% 样本支持不足的估算窗口")
                .arg(res.excludedSupportPct, 0, 'f', 2)
            : tr("文件未记录有效样本支持数，零填充可能影响最高值和高值占比");
        const QString assessmentContext = tr("%1；%2").arg(processingState, supportState);

        // 旧阈值仅属于当前 R2 口径。R1、gamma 与未知口径没有经过生产数据标定，
        // 因此只展示统计值，不复用 R2 的等级或给出处理建议。
        if (!isAxialR2) {
            m_statusCardTitle->setText(isCircularR1
                ? tr("圆统计集中度")
                : (isComplexGamma ? tr("复相干系数统计") : tr("指标语义未标注")));
            if (isCircularR1) {
                m_statusCardDesc->setText(tr("当前数据为 2π 周期的一阶相位集中度 R₁。尚未建立生产数据分级阈值，仅展示统计值。%1。").arg(assessmentContext));
            } else if (isComplexGamma) {
                m_statusCardDesc->setText(tr("当前数据为主辅 SLC 的归一化复相干系数 γ。当前面板尚未获得有效视数，暂不进行质量分级。%1。").arg(assessmentContext));
            } else {
                m_statusCardDesc->setText(tr("该 coherence 数据集缺少可识别的语义标签，无法确定数值口径或应用质量阈值。%1。").arg(assessmentContext));
            }
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #4B5563;");
            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #6B7280;");
            m_statusCard->setStyleSheet("background-color: rgba(107, 114, 128, 0.08); border: 1px solid rgba(107, 114, 128, 0.25); border-radius: 4px;");
        } else if (res.meanCoh > 0.4f && res.highCohPct > 30.0f) {
            m_statusCardTitle->setText(tr("相位集中度较高"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #059669;"); // 绿色
            m_statusCardDesc->setText(tr("二倍角残余相位在估算窗口内较为集中。该 R₂ 等级仅沿用原口径作相对比较，不等价于相干系数。%1。").arg(assessmentContext));
            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #10B981;");
            m_statusCard->setStyleSheet("background-color: rgba(16, 185, 129, 0.1); border: 1px solid rgba(16, 185, 129, 0.3); border-radius: 4px;");
        } else if (res.meanCoh >= 0.2f) {
            m_statusCardTitle->setText(tr("相位集中度中等"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #D97706;"); // 橙色
            m_statusCardDesc->setText(tr("二倍角残余相位集中度中等。该 R₂ 等级仅沿用原口径作相对比较，不等价于相干系数。%1。").arg(assessmentContext));
            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #F59E0B;");
            m_statusCard->setStyleSheet("background-color: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.3); border-radius: 4px;");
        } else {
            m_statusCardTitle->setText(tr("相位集中度较低"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #DC2626;"); // 红色
            m_statusCardDesc->setText(tr("二倍角残余相位在估算窗口内较为分散。该 R₂ 等级仅沿用原口径作相对比较，不等价于相干系数。%1。").arg(assessmentContext));
            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #EF4444;");
            m_statusCard->setStyleSheet("background-color: rgba(239, 68, 68, 0.1); border: 1px solid rgba(239, 68, 68, 0.3); border-radius: 4px;");
        }
    }

    InterferometricFormationNode* m_node;
    QComboBox* m_slaveCombo;
    QComboBox* m_visualModeCombo;
    QPushButton* m_evalBtn;
    ImageView* m_imageView;

    QFrame* m_statusCard;
    QLabel* m_statusCardTitle;
    QLabel* m_statusCardDesc;

    QLabel* m_meanTitleLabel;
    QLabel* m_medianTitleLabel;
    QLabel* m_maxTitleLabel;
    QLabel* m_meanCohLabel;
    QLabel* m_medianCohLabel;
    QLabel* m_maxCohLabel;
    QLabel* m_highCohPctLabel;
    QLabel* m_enlLabel;
    QLabel* m_flatEarthLabel;
    QLabel* m_topoLabel;
    QLabel* m_statusLabel;

    QStringList m_h5PathsPhase;
    QStringList m_jpgPathsPhase;
    QStringList m_jpgPathsCoh;

    QFutureWatcher<InterfEvalThreadResult> m_watcher;
};

::QWidget* InterferometricFormationNode::createInterferometryWidget(::QWidget* parent)
{
    return new InterferometricFormationEvalWidget(this, parent);
}

} // namespace QtNodes
