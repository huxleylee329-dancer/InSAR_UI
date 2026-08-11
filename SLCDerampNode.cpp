#include "SLCDerampNode.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include <tinyxml.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QApplication>
#include <QComboBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>
#include <QPointer>
#include <QSignalBlocker>
#include <QFileDialog>
#include <atomic>

namespace QtNodes {

SLCDerampNode::SLCDerampNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_masterIndexLabel(nullptr)
    , m_masterIndex(1)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
    QPointer<SLCDerampNode> self(this);
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

SLCDerampNode::~SLCDerampNode()
{
    stopExecution();
}

unsigned int SLCDerampNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType SLCDerampNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
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

bool SLCDerampNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString SLCDerampNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0)
            return QStringLiteral("输入图像");
        if (portIndex == 1)
            return QStringLiteral("辅助地形 DEM ?");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool SLCDerampNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1)
        return true;
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

QList<QList<PortIndex>> SLCDerampNode::alternativeInputGroups() const
{
    return {};
}

std::shared_ptr<NodeData> SLCDerampNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_imageInfoData;
}

void SLCDerampNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

        if (!m_inputData || m_inputData->filePaths().isEmpty())
        {
            m_outputData.reset();
            m_imageInfoData.reset();
            setOutputData(0, nullptr);
            setOutputData(1, nullptr);
        }

        updateLabels();

        if (m_inputData && m_outputNodeName.isEmpty())
        {
            m_outputNodeName = generateDefaultOutputName();
            if (m_outputNodeNameEdit)
            {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
        }
    } else if (port == 1) {
        const auto auxiliary = std::dynamic_pointer_cast<AuxiliaryDemData>(data);
        m_auxiliaryDemEntityData = auxiliary;
        if (auxiliary) { m_auxiliaryDemReferenceData.reset(); m_auxiliaryDemLabel.clear(); if (m_demLabelCombo) m_demLabelCombo->setCurrentIndex(0); }
        m_demInputData = auxiliary
            ? std::make_shared<ImportedFileData>(auxiliary->rasterPath(), auxiliary->nodeName())
            : std::dynamic_pointer_cast<ImportedFileData>(data);
        if (auxiliary) m_demInputData->setProductDescriptor(auxiliary->productDescriptor());
        if (m_demInputData) {
            m_demPath = auxiliary ? auxiliary->rasterPath() : m_demInputData->filePath();
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);
    updateParameterWidgetsEnableState();
}

::QWidget* SLCDerampNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject SLCDerampNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    QString nodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["outputNodeName"] = nodeName;
    modelJson["masterIndex"] = m_masterIndex;
    modelJson[QStringLiteral("auxiliaryDemLabel")] = m_auxiliaryDemLabel;
    modelJson[QStringLiteral("auxiliaryDemLegacyResourceId")] = m_legacyDemResourceId;
    modelJson[QStringLiteral("auxiliaryDemLegacyPinnedProvenanceId")] = m_legacyDemProvenanceId;
    return modelJson;
}

void SLCDerampNode::load(QJsonObject const &json)
{
    QJsonValue v = json["outputNodeName"];
    if (!v.isUndefined())
    {
        m_outputNodeName = v.toString();
    }
    
    QJsonValue vIndex = json["masterIndex"];
    if (!vIndex.isUndefined())
    {
        m_masterIndex = vIndex.toInt();
    }
    m_auxiliaryDemLabel = json.value(QStringLiteral("auxiliaryDemLabel")).toString().trimmed();
    m_legacyDemResourceId = json.value(QStringLiteral("auxiliaryDemLegacyResourceId")).toString().trimmed();
    m_legacyDemProvenanceId = json.value(QStringLiteral("auxiliaryDemLegacyPinnedProvenanceId")).toString().trimmed();

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
    updateLabels();
    refreshAuxiliaryDemLabels();
}

void SLCDerampNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

QStringList SLCDerampNode::previewImagePaths() const
{
    QStringList existingPaths;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return existingPaths;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        for (const QString& h5Path : h5Paths) {
            const QFileInfo fi(h5Path);
            const QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                existingPaths.append(jpgPath);
            }
        }
    }
    return existingPaths;
}

ProductInputContract SLCDerampNode::productInputContract(PortIndex portIndex) const
{
    ProductInputContract contract;
    if (portIndex == 0) {
        contract.semanticId = QStringLiteral("slc_deramp.input.back_geocoded_complex_sar");
        contract.allowedProductTypes = QStringList() << QStringLiteral("back_geocoded_complex_sar");
    } else {
        contract.semanticId = QStringLiteral("slc_deramp.input.auxiliary_terrain_dem");
        contract.optional = true;
        contract.allowedProductTypes = QStringList() << QStringLiteral("auxiliary_terrain_dem");
    }
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract SLCDerampNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("slc_deramp.output.deramped_complex_sar")
        : QStringLiteral("slc_deramp.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("deramped_complex_sar")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

std::vector<QString> SLCDerampNode::processingInfo() const
{
    return m_processingStatus.isEmpty()
        ? std::vector<QString>()
        : std::vector<QString>{m_processingStatus};
}

bool SLCDerampNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        if (!h5Paths.isEmpty()) {
            ProductDescriptor::Ptr descriptor;
            QString identityError;
            if (!NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), dstNode, descriptor, &identityError) ||
                !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
                !NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) {
                return false;
            }
            // 恢复 Port 0 数据
            m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
            m_outputData->setProductDescriptor(descriptor);
            setOutputData(0, m_outputData);

            // 恢复 Port 1 预览数据 & 后台异步补救缺失的 JPG
            QStringList existingJpgPaths;
            QStringList missingH5s;
            QStringList missingJpgs;
            QStringList allJpgPaths;

            for (const QString& h5Path : h5Paths) {
                QFileInfo fi(h5Path);
                QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
                allJpgPaths.append(jpgPath);

                if (NodeUtils::isJpgPreviewCurrent(h5Path, jpgPath)) {
                    existingJpgPaths.append(jpgPath);
                } else {
                    missingH5s.append(h5Path);
                    missingJpgs.append(jpgPath);
                }
            }

            if (!existingJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
                setOutputData(1, m_imageInfoData);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
            }

            // 异步补录 JPG 预览
            if (!missingH5s.isEmpty()) {
                m_remedyWatcher.disconnect();
                if (m_remedyWatcher.isRunning()) {
                    m_remedyWatcher.cancel();
                }
                m_previewGenerationPending = true;
                const quint64 previewGenerationId = ++m_previewGenerationId;
                QStringList previewJpgPaths;
                for (const QString& missingJpg : missingJpgs) {
                    const QFileInfo info(missingJpg);
                    previewJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
                        QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
                    QFile::remove(missingJpg);
                }

                connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                        [this, h5Paths, allJpgPaths, missingJpgs, previewJpgPaths, previewGenerationId]() {
                    if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
                        for (const QString& previewJpgPath : previewJpgPaths) {
                            QFile::remove(previewJpgPath);
                        }
                        return;
                    }
                    m_previewGenerationPending = false;
                    for (int i = 0; i < previewJpgPaths.size() && i < missingJpgs.size(); ++i) {
                        if (QFile::exists(previewJpgPaths[i])) {
                            QFile::rename(previewJpgPaths[i], missingJpgs[i]);
                        }
                    }
                    InSARLogManager::LogInfo("SLCDerampNode", "Remedy preview generation finished. Updating Port 1.");
                    QStringList currentJpgPaths;
                    for (int i = 0; i < h5Paths.size() && i < allJpgPaths.size(); ++i) {
                        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], allJpgPaths[i])) currentJpgPaths.append(allJpgPaths[i]);
                    }
                    m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
                    setOutputData(1, m_imageInfoData);
                    if (executionState() == ExecutionState::Running) {
                        setProgress(100);
                        finishExecution();
                    } else {
                        Q_EMIT dataUpdated(1);
                    }
                });

                QFuture<void> future = QtConcurrent::run([missingH5s, previewJpgPaths]() {
                    for (int i = 0; i < missingH5s.size(); ++i) {
                        NodeUtils::generateJpgPreviewFromH5(missingH5s[i], previewJpgPaths[i], "complex");
                    }
                });
                m_remedyWatcher.setFuture(future);
            }

            // 恢复左侧树标准项目模型 (Standard Item Model Tree View)
            QStandardItemModel* projModelPtr = projectModel();
            if (projModelPtr) {
                QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
                if (!foundProjects.isEmpty()) {
                    QStandardItem* projectItem = foundProjects.first();

                    // 1. 查找或建立 deramp 根节点
                    QStandardItem* derampItem = nullptr;
                    for (int i = 0; i < projectItem->rowCount(); i++) {
                        if (projectItem->child(i, 0)->text() == dstNode) {
                            derampItem = projectItem->child(i, 0);
                            break;
                        }
                    }

                    if (!derampItem) {
                        derampItem = new QStandardItem(dstNode);
                        derampItem->setToolTip(projectName());
                        int insert = 0;
                        for (; insert < projectItem->rowCount(); insert++) {
                            if (projectItem->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-2.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-3.0") == 0)
                                continue;
                            else
                                break;
                        }
                        derampItem->setIcon(QIcon(FOLDER_ICON));
                        projectItem->insertRow(insert, derampItem);
                        QStandardItem* derampRank = new QStandardItem("complex-3.0");
                        projectItem->setChild(insert, 1, derampRank);
                    }

                    // 2. 补全下属图像节点
                    for (const QString& h5Path : h5Paths) {
                        QFileInfo fileinfo(h5Path);
                        QString deramp_name = fileinfo.baseName();

                        QStandardItem* item_img = nullptr;
                        for (int j = 0; j < derampItem->rowCount(); j++) {
                            if (derampItem->child(j, 0)->text() == deramp_name) {
                                item_img = derampItem->child(j, 0);
                                break;
                            }
                        }

                        if (!item_img) {
                            QStandardItem* deramp_images_name = new QStandardItem(deramp_name);
                            deramp_images_name->setToolTip("complex");
                            QStandardItem* deramp_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                            deramp_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                            derampItem->appendRow(deramp_images_name);
                            derampItem->setChild(derampItem->rowCount() - 1, 1, deramp_images_path);
                        } else {
                            derampItem->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                        }
                    }
                }
            }

            return true;
        }
    }
    return false;
}

void SLCDerampNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // 锁宽以避 Bug (SOP 3)
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene);
        }
    };

    // 目标节点名 QHBoxLayout (SOP 10, 固定标签宽度对齐)
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLabel->setFixedWidth(80);
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("自动生成或手动输入");
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

    // 主图像序号展示 QLabel
    m_masterIndexLabel = new QLabel("主图像序号: [未连接]");
    layout->addWidget(m_masterIndexLabel);

    auto* demLayout = new QHBoxLayout();
    m_demPathLabel = new QLabel(QStringLiteral("辅助 DEM"));
    m_demPathLabel->setFixedWidth(80);
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
    });
    demLayout->addWidget(m_demPathLabel);
    demLayout->addWidget(m_demLabelCombo);
    layout->addLayout(demLayout);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // SOP: Widget创建尾部延迟装载自愈参数
    updateLabels();
    updateParameterWidgetsEnableState();
}

void SLCDerampNode::updateLabels()
{
    if (!m_inputData) {
        if (m_masterIndexLabel)
            m_masterIndexLabel->setText("主图像序号: [未连接]");
        m_masterIndex = 1;
        return;
    }

    // 从上游节点的 XML 自愈检索主图像序号
    int masterIndex = 1;
    XMLFile* xml = projectXml();
    if (xml) {
        TiXmlElement* root = nullptr;
        xml->get_root(root);
        if (root) {
            QString srcNode = m_inputData->nodeName();
            for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                const char* nameAttr = p->Attribute("name");
                if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == srcNode) {
                    TiXmlElement* params = p->FirstChildElement("Data_Processing_Parameters");
                    if (params) {
                        TiXmlElement* masterImgElem = params->FirstChildElement("master_image");
                        if (masterImgElem && masterImgElem->GetText()) {
                            masterIndex = QString(masterImgElem->GetText()).toInt();
                        }
                    }
                    break;
                }
            }
        }
    }
    m_masterIndex = masterIndex;
    if (m_masterIndexLabel)
        m_masterIndexLabel->setText(QStringLiteral("主图像序号: %1").arg(m_masterIndex));

    updateWidgetSize();
}

void SLCDerampNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

void SLCDerampNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    m_processingStatus = message;
    setProgress(progress);
    triggerVisualUpdate();
}

void SLCDerampNode::onProcessingFinished()
{
    QStringList h5Paths;
    const QString dstNode = m_preparedDstNode;
    releaseFinishedThreadAndWorker();

    if (isAutomaticExecutionObsolete()) {
        m_previewGenerationPending = false;
        ++m_previewGenerationId;
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset();
        m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        discardObsoleteAutomaticExecution();
        return;
    }

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }
    if (m_auxiliaryDemEntityData || m_auxiliaryDemReferenceData) {
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
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
                                             QStringList() << QStringLiteral("s_re") << QStringLiteral("s_im"),
                                             &transactionError)) {
        onError(transactionError.isEmpty()
                    ? QStringLiteral("Project XML context is unavailable for SLC deramp output commit.")
                    : transactionError);
        return;
    }

    if (!NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    m_xmlDirty = false;
    if (!commitResultsToProjectXml(h5Paths, m_pendingOriginNames) || !m_xmlDirty ||
        !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty()
                    ? QStringLiteral("SLC deramp output metadata was not produced.")
                    : transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    publishResultsToProjectTree(h5Paths, m_pendingOriginNames);
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);

    QStringList jpgPaths;
    for (const QString& h5Path : h5Paths) {
        const QFileInfo fi(h5Path);
        jpgPaths.append(fi.absolutePath() + "/" + fi.baseName() + ".jpg");
    }

    if (jpgPaths.isEmpty()) {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        updateParameterWidgetsEnableState();
        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("SLCDerampNode", "executeProcessing completed (empty output list).");
        finishExecution();
        return;
    }

    m_remedyWatcher.disconnect();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    m_previewGenerationPending = true;
    const quint64 previewGenerationId = ++m_previewGenerationId;
    onProgressUpdate(90, QStringLiteral("正在生成预览图..."));
    QStringList previewJpgPaths;
    for (const QString& jpgPath : jpgPaths) {
        const QFileInfo info(jpgPath);
        previewJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
            QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
        QFile::remove(jpgPath);
    }
    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, jpgPaths, previewJpgPaths, previewGenerationId]() {
        if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
            for (const QString& previewJpgPath : previewJpgPaths) {
                QFile::remove(previewJpgPath);
            }
            return;
        }
        m_previewGenerationPending = false;
        if (discardObsoleteAutomaticExecution()) {
            ++m_previewGenerationId;
            m_outputData.reset();
            m_imageInfoData.reset();
            setOutputData(0, nullptr);
            setOutputData(1, nullptr);
            return;
        }

        QStringList validJpgPaths;
        for (int i = 0; i < jpgPaths.size() && i < previewJpgPaths.size(); ++i) {
            if (QFile::exists(previewJpgPaths[i]) &&
                QFile::rename(previewJpgPaths[i], jpgPaths[i])) {
                validJpgPaths.append(jpgPaths[i]);
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
        setState(ExecutionState::Running);
        m_processingStatus = QStringLiteral("SLC Deramp 已完成。");
        setProgress(100);
        triggerVisualUpdate();
        InSARLogManager::LogInfo("SLCDerampNode", "executeProcessing completed.");
        finishExecution();
    });
    const auto previewProgress = std::make_shared<std::atomic<int>>(90);
    const QPointer<SLCDerampNode> nodeGuard(this);
    QFuture<void> future = QtConcurrent::run([h5Paths, previewJpgPaths, previewGenerationId, previewProgress, nodeGuard]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5WithProgress(
                h5Paths[i], previewJpgPaths[i], "complex",
                [nodeGuard, previewGenerationId, previewProgress, i, total = h5Paths.size()](int completedRows, int totalRows) {
                    if (!nodeGuard || total <= 0 || totalRows <= 0) {
                        return;
                    }

                    const int localProgress = qBound(0, completedRows * 100 / totalRows, 100);
                    // Reserve 100% for the future completion handler, including its atomic rename.
                    const int candidate = 90 + (9 * (i * 100 + localProgress)) / (total * 100);
                    int previous = previewProgress->load(std::memory_order_relaxed);
                    while (candidate > previous &&
                           !previewProgress->compare_exchange_weak(previous, candidate,
                                                                   std::memory_order_relaxed,
                                                                   std::memory_order_relaxed)) {
                    }
                    if (candidate <= previous) {
                        return;
                    }

                    QMetaObject::invokeMethod(nodeGuard.data(),
                        [nodeGuard, previewGenerationId, candidate, i, total]() {
                            if (!nodeGuard || !nodeGuard->m_previewGenerationPending ||
                                nodeGuard->m_previewGenerationId != previewGenerationId) {
                                return;
                            }
                            nodeGuard->onProgressUpdate(
                                candidate,
                                QStringLiteral("正在生成预览图：%1/%2").arg(i + 1).arg(total));
                        },
                        Qt::QueuedConnection);
                });
        }
    });
    m_remedyWatcher.setFuture(future);
}

void SLCDerampNode::onError(const QString& error)
{
    InSARLogManager::LogError("SLCDerampNode", "Execution error: " + error);
    releaseFinishedThreadAndWorker();
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    updateParameterWidgetsEnableState();
    setLastErrorMessage(error);
    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
}

void SLCDerampNode::onResultsReceived(
    const QString& dstNode,
    const QStringList& h5Paths,
    const QStringList& originNames,
    const QString& savePath,
    const QString& projectName)
{
    Q_UNUSED(dstNode);
    Q_UNUSED(savePath);
    Q_UNUSED(projectName);
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    if (h5Paths.isEmpty() || h5Paths.size() != originNames.size()) {
        return;
    }
    m_generatedOutputPaths = h5Paths;
    m_pendingOriginNames = originNames;
}

bool SLCDerampNode::commitResultsToProjectXml(const QStringList& h5Paths, const QStringList& originNames)
{
    if (h5Paths.isEmpty() || h5Paths.size() != originNames.size()) {
        return false;
    }

    XMLFile* xml = projectXml();
    if (!xml)
    {
        return false;
    }

    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (!root)
    {
        return false;
    }

    bool xmlModified = false;
    for (int i = 0; i < h5Paths.size(); i++)
    {
        QFileInfo fileinfo(h5Paths.at(i));
        QString relativePath = QString("/%1/%2").arg(m_preparedDstNode).arg(fileinfo.fileName());

        // 查找或新建 DataNode
        TiXmlElement* dataNodeElem = nullptr;
        for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement())
        {
            const char* nameAttr = p->Attribute("name");
            if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == m_preparedDstNode)
            {
                dataNodeElem = p;
                break;
            }
        }

        if (!dataNodeElem)
        {
            // 新建 DataNode
            dataNodeElem = new TiXmlElement("DataNode");
            dataNodeElem->SetAttribute("name", m_preparedDstNode.toStdString().c_str());
            dataNodeElem->SetAttribute("data_count", "1");
            dataNodeElem->SetAttribute("data_processing", "SLC_deramp");
            dataNodeElem->SetAttribute("rank", "complex-3.0");

            int index = 1;
            TiXmlElement* root_child = root->FirstChildElement();
            if (root_child) root_child = root_child->NextSiblingElement(); // skip project_info

            TiXmlElement* insertBeforeNode = nullptr;
            for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++)
            {
                const char* rankAttr = p->Attribute("rank");
                if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 ||
                                 strcmp(rankAttr, "complex-1.0") == 0 ||
                                 strcmp(rankAttr, "complex-2.0") == 0 ||
                                 strcmp(rankAttr, "complex-3.0") == 0))
                    continue;
                else { insertBeforeNode = p; break; }
            }
            dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

            TiXmlElement* dataElem = new TiXmlElement("Data");
            TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
            dataNameNode->LinkEndChild(new TiXmlText((originNames.at(i) + "_deramp").toStdString().c_str()));
            dataElem->LinkEndChild(dataNameNode);
            TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
            dataRankNode->LinkEndChild(new TiXmlText("complex-3.0"));
            dataElem->LinkEndChild(dataRankNode);
            TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
            dataIndexNode->LinkEndChild(new TiXmlText("1"));
            dataElem->LinkEndChild(dataIndexNode);
            TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
            dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
            dataElem->LinkEndChild(dataPathNode);
            dataNodeElem->LinkEndChild(dataElem);

            TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");
            TiXmlElement* masterImageElem = new TiXmlElement("master_image");
            masterImageElem->LinkEndChild(new TiXmlText(QString::number(m_preparedMasterIndex).toStdString().c_str()));
            paramsElem->LinkEndChild(masterImageElem);
            dataNodeElem->LinkEndChild(paramsElem);

            if (insertBeforeNode)
            {
                root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                delete dataNodeElem;
                for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement())
                {
                    index++;
                    p->SetAttribute("index", QString::number(index).toStdString().c_str());
                }
            }
            else
            {
                root->LinkEndChild(dataNodeElem);
            }
            xmlModified = true;
        }
        else
        {
            // DataNode 已存在，追加 Data 子节点
            const char* countAttr = dataNodeElem->Attribute("data_count");
            int count = countAttr ? QString(countAttr).toInt() : 0;
            count++;
            dataNodeElem->SetAttribute("data_count", QString::number(count).toStdString().c_str());

            TiXmlElement* lastChildNode = dataNodeElem->LastChild() ? dataNodeElem->LastChild()->ToElement() : nullptr;

            TiXmlElement* dataElem = new TiXmlElement("Data");
            TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
            dataNameNode->LinkEndChild(new TiXmlText((originNames.at(i) + "_deramp").toStdString().c_str()));
            dataElem->LinkEndChild(dataNameNode);
            TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
            dataRankNode->LinkEndChild(new TiXmlText("complex-3.0"));
            dataElem->LinkEndChild(dataRankNode);
            TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
            dataIndexNode->LinkEndChild(new TiXmlText(QString::number(count).toStdString().c_str()));
            dataElem->LinkEndChild(dataIndexNode);
            TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
            dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
            dataElem->LinkEndChild(dataPathNode);

            if (lastChildNode)
            {
                dataNodeElem->InsertBeforeChild(lastChildNode, *dataElem);
                delete dataElem;
            }
            else
            {
                dataNodeElem->LinkEndChild(dataElem);
            }
            xmlModified = true;
        }
    }

    m_xmlDirty = xmlModified;
    return xmlModified;
}

void SLCDerampNode::publishResultsToProjectTree(const QStringList& h5Paths, const QStringList& originNames)
{
    Q_UNUSED(originNames);
    QStandardItemModel* model = projectModel();
    const QList<QStandardItem*> projects = model ? model->findItems(m_preparedProjectName)
                                                  : QList<QStandardItem*>();
    if (projects.isEmpty()) {
        return;
    }

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), m_preparedDstNode, "complex-3.0", FOLDER_ICON);
    if (!outputNode) {
        return;
    }
    outputNode->setToolTip(m_preparedProjectName);
    for (const QString& h5Path : h5Paths) {
        const QString outputName = QFileInfo(h5Path).baseName();
        NodeUtils::findOrCreateChildItem(outputNode, outputName, "complex", h5Path, IMAGEDATA_ICON);
    }
}

bool SLCDerampNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
    {
        return false;
    }

    const QFileInfo demInfo(m_demPath);
    if (m_demPath.isEmpty() || !demInfo.isFile() || !demInfo.isReadable())
    {
        return false;
    }

    QString nodeName = m_inputData->nodeName();
    if (nodeName.isEmpty())
    {
        return false;
    }

    QString dstName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstName.isEmpty())
    {
        return false;
    }

    return true;
}

QString SLCDerampNode::generateDefaultOutputName() const
{
    if (m_inputData)
    {
        QString nodeName = m_inputData->nodeName();
        return nodeName + "_Deramp";
    }
    return "deramp结果";
}

bool SLCDerampNode::prepareToStart()
{
    QJsonObject inputGeometry;
    if (m_inputData) inputGeometry = NodeUtils::inputGeometryFromProductDescriptor(m_inputData->physicalProductDescriptor());
    if (hasActiveInputConnection(1) && !m_auxiliaryDemLabel.isEmpty()) {
        setStartFailureMessage(QStringLiteral("辅助 DEM 不能同时使用直接连线和命名标签。"));
        return false;
    }
    if (!m_auxiliaryDemEntityData && m_auxiliaryDemLabel.isEmpty()) {
        setStartFailureMessage(QStringLiteral("SLCDeramp DEM 必须通过直接连线或工程标签提供。"));
        return false;
    }
    if (m_auxiliaryDemEntityData) {
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
        m_demInputData = std::make_shared<ImportedFileData>(binding.rasterPath, QStringLiteral("Auxiliary DEM"));
        m_demInputData->setProductDescriptor(m_auxiliaryDemEntityData->productDescriptor());
    }
    if (!m_auxiliaryDemLabel.isEmpty()) {
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
        m_demInputData = std::make_shared<ImportedFileData>(binding.rasterPath, QStringLiteral("DEM Label"));
    }
    const QString configuredOutputName = m_outputNodeNameEdit
        ? m_outputNodeNameEdit->text().trimmed()
        : m_outputNodeName.trimmed();
    if (configuredOutputName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    } else {
        m_outputNodeName = configuredOutputName;
    }

    if (!validateInputs())
    {
        if (!m_inputData || m_inputData->filePaths().isEmpty()) {
            setStartFailureMessage(QStringLiteral("请连接输入图像端口。"));
        } else if (m_demPath.isEmpty()) {
            setStartFailureMessage(QStringLiteral("DEM 输入路径为空。"));
        } else {
            const QFileInfo demInfo(m_demPath);
            setStartFailureMessage(demInfo.isFile()
                ? QStringLiteral("DEM 输入文件不可读：%1").arg(m_demPath)
                : QStringLiteral("DEM 输入文件不存在或不是常规文件：%1").arg(m_demPath));
        }
        return false;
    }

    QString dstNode = m_outputNodeName.trimmed();
    m_preparedDstNode = dstNode;
    m_outputNodeName = m_preparedDstNode;
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedInputPaths = m_inputData->filePaths();
    for (QString& inputPath : m_preparedInputPaths) {
        if (QDir::isRelativePath(inputPath)) {
            inputPath = QDir(m_preparedSavePath).absoluteFilePath(inputPath);
        }
    }
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_preparedInputPaths,
                                         m_inputData->physicalProductDescriptor(), &identityError)) {
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }
    m_preparedTransactionInputPaths = m_preparedInputPaths;
    m_preparedMasterIndex = m_masterIndex;
    m_preparedDemPath = m_demPath;
    if (QDir::isRelativePath(m_preparedDemPath)) {
        m_preparedDemPath = QDir(m_preparedSavePath).absoluteFilePath(m_preparedDemPath);
    }
    if (QFileInfo(m_preparedDemPath).suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0) {
        if (!m_demInputData || m_demInputData->filePath() != m_demPath ||
            !NodeUtils::validateH5Identity(m_preparedDemPath, m_demInputData->physicalProductDescriptor(), nullptr, &identityError)) {
            setStartFailureMessage(identityError.isEmpty()
                ? QStringLiteral("H5 DEM must be supplied through a descriptor-bound input port.")
                : identityError);
            return false;
        }
        m_preparedTransactionInputPaths.append(m_preparedDemPath);
    }
    m_preparedOutputPaths.clear();
    for (const QString& inputPath : m_preparedInputPaths) {
        m_preparedOutputPaths.append(QDir(m_preparedSavePath).absoluteFilePath(
            m_preparedDstNode + "/" + QFileInfo(inputPath).baseName() + "_deramp.h5"));
    }

    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void SLCDerampNode::executeProcessing()
{
    InSARLogManager::LogInfo("SLCDerampNode", "executeProcessing started.");

    // Retrieve input and output settings
    QString dstNode = m_preparedDstNode;
    QString dstProject = m_preparedProjectName;
    QString savePath = m_preparedSavePath;
    const QStringList inputPaths = m_preparedInputPaths;
    const int masterIndex = m_preparedMasterIndex;
    const QString demPath = m_preparedDemPath;

    m_outputNodeName = dstNode;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting)
    {
        if (validateAndRestoreOutput()) {
            setState(ExecutionState::Running);
            setProgress(100);
            if (!m_remedyWatcher.isRunning()) {
                finishExecution();
            }
            return;
        }
        else
        {
            setState(ExecutionState::Error);
            return;
        }
    }

    m_processingStatus = QStringLiteral("正在准备 SLC Deramp 任务...");
    setProgress(0);
    triggerVisualUpdate();
    setState(ExecutionState::Running);
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode,
                                           m_preparedOutputPaths, m_preparedTransactionInputPaths,
                                           m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> descriptorProvenance;
    descriptorProvenance.insert(QStringLiteral("producer"), name());
    descriptorProvenance.insert(QStringLiteral("output_port"),
                                QStringLiteral("slc_deramp.output.deramped_complex_sar"));
    if (!NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction, ProductDescriptor::create(
                QStringLiteral("deramped_complex_sar"), QStringLiteral("sat-explorer-product"), 1,
                ProductState::Committed, name(), descriptorProvenance), &transactionError)) {
        onError(transactionError);
        return;
    }
    m_generatedOutputPaths.clear();
    m_pendingOriginNames.clear();
    m_xmlDirty = false;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;

    // Create thread and worker
    m_thread = new QThread();
    m_worker = new SLCDerampWorker();
    m_worker->moveToThread(m_thread);

    // Connect signals
    connect(this, &SLCDerampNode::startDeramp, m_worker, &SLCDerampWorker::SLC_deramp);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, [this, masterIndex, dstProject, savePath, stagingNode, inputPaths, demPath]() {
        Q_EMIT startDeramp(masterIndex, dstProject, savePath, stagingNode, inputPaths, demPath);
    });
    connect(m_worker, &SLCDerampWorker::updateProcess, this, &SLCDerampNode::onProgressUpdate);
    connect(m_worker, &SLCDerampWorker::endProcess, this, &SLCDerampNode::onProcessingFinished);
    connect(m_worker, &SLCDerampWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &SLCDerampWorker::cancelled, this, &SLCDerampNode::onCancelled);
    connect(m_worker, &SLCDerampWorker::cancelled, m_thread, &QThread::quit);
    connect(m_worker, &SLCDerampWorker::errorProcess, this, &SLCDerampNode::onError);
    connect(m_worker, &SLCDerampWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &SLCDerampWorker::sendResults, this, &SLCDerampNode::onResultsReceived);
    connect(m_worker, &SLCDerampWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
    deferAutomaticCompletion();
    m_thread->start();
    updateParameterWidgetsEnableState();

    // QTimer::singleShot 强行把状态设回 Running，规避基类 setInData 复写 (SOP 5)
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

QStandardItemModel* SLCDerampNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString SLCDerampNode::projectPath() const
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

QString SLCDerampNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* SLCDerampNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void SLCDerampNode::execute()
{
    executeProcessing();
}

void SLCDerampNode::stopExecution()
{
    if (m_worker)
    {
        m_worker->StopProcess();
        return;
    }

    if (!m_previewGenerationPending) {
        return;
    }

    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    updateParameterWidgetsEnableState();
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void SLCDerampNode::onCancelled()
{
    releaseFinishedThreadAndWorker();
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    updateParameterWidgetsEnableState();
}

void SLCDerampNode::releaseFinishedThreadAndWorker()
{
    QPointer<QThread> thread = m_thread;
    m_thread = nullptr;
    m_worker = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

void SLCDerampNode::processAutomatically()
{
    if (prepareToStart())
    {
        executeProcessing();
    }
    else
    {
        setState(ExecutionState::Pending);
    }
}

void SLCDerampNode::updateParameterWidgetsEnableState()
{
    bool isExec = m_thread && m_thread->isRunning();
    bool enableWidgets = !isExec;

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets);
    const bool demConnected = m_demInputData != nullptr;
    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !demConnected);
    if (m_demLabelCombo) m_demLabelCombo->setEnabled(enableWidgets && !demConnected);
}

void SLCDerampNode::refreshAuxiliaryDemLabels()
{
    if (!m_demLabelCombo) return;
    QMap<QString, NodeUtils::AuxiliaryDemLabelBinding> labels;
    QString error;
    NodeUtils::loadAuxiliaryDemLabels(projectPath(), labels, &error);
    QSignalBlocker blocker(m_demLabelCombo);
    m_demLabelCombo->clear();
    m_demLabelCombo->addItem(QStringLiteral("不使用标签"), QString());
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        if (it.value().mode != NodeUtils::AuxiliaryDemLabelMode::WorkflowOutput) continue;
        const QString status = it.value().isPlanned() ? QStringLiteral("待生成") : QStringLiteral("就绪");
        m_demLabelCombo->addItem(QStringLiteral("@%1 (%2, 来源 %3)").arg(it.value().label, status, it.value().producerIdentity.left(8)), it.value().label);
    }
    if (!labels.isEmpty()) m_demLabelCombo->insertSeparator(m_demLabelCombo->count());
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        if (it.value().mode != NodeUtils::AuxiliaryDemLabelMode::FixedResource) continue;
        m_demLabelCombo->addItem(QStringLiteral("@%1 (已注册资源)").arg(it.value().label), it.value().label);
    }
    const int index = m_demLabelCombo->findData(m_auxiliaryDemLabel);
    m_demLabelCombo->setCurrentIndex(index < 0 ? 0 : index);
}

} // namespace QtNodes

