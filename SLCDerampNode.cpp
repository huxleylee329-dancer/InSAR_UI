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
#include <QStandardItemModel>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>

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
        else
            return QStringLiteral("DEM *");
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
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
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
        m_demInputData = std::dynamic_pointer_cast<ImportedFileData>(data);
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
    modelJson[QStringLiteral("demPath")] = m_demPath;
    modelJson["isDeflat"] = m_deflatCheckBox ? m_deflatCheckBox->isChecked() : m_isDeflat;
    modelJson["isTopoRemoval"] = m_topoRemovalCheckBox ? m_topoRemovalCheckBox->isChecked() : m_isTopoRemoval;

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

    QJsonValue vDemPath = json[QStringLiteral("demPath")];
    if (!vDemPath.isUndefined())
    {
        m_demPath = vDemPath.toString();
    }

    QJsonValue vDeflat = json["isDeflat"];
    if (!vDeflat.isUndefined())
    {
        m_isDeflat = vDeflat.toBool();
    }

    QJsonValue vTopo = json["isTopoRemoval"];
    if (!vTopo.isUndefined())
    {
        m_isTopoRemoval = vTopo.toBool();
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_deflatCheckBox)
        m_deflatCheckBox->setChecked(m_isDeflat);
    if (m_topoRemovalCheckBox)
        m_topoRemovalCheckBox->setChecked(m_isTopoRemoval);
        
    updateLabels();
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

bool SLCDerampNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        if (!h5Paths.isEmpty()) {
            // 恢复 Port 0 数据
            m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
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

    auto* deflatLayout = new QHBoxLayout();
    deflatLayout->addWidget(new QLabel("平地相位消除"));
    m_deflatCheckBox = new QCheckBox();
    m_deflatCheckBox->setChecked(m_isDeflat);
    connect(m_deflatCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        const bool value = (state == Qt::Checked);
        if (m_isDeflat == value) {
            return;
        }
        if (!confirmParameterChange()) {
            m_deflatCheckBox->blockSignals(true);
            m_deflatCheckBox->setChecked(m_isDeflat);
            m_deflatCheckBox->blockSignals(false);
            return;
        }
        m_isDeflat = value;
        invalidateNodeData();
    });
    deflatLayout->addWidget(m_deflatCheckBox);
    layout->addLayout(deflatLayout);

    auto* topoRemovalLayout = new QHBoxLayout();
    topoRemovalLayout->addWidget(new QLabel("地形相位消除"));
    m_topoRemovalCheckBox = new QCheckBox();
    m_topoRemovalCheckBox->setChecked(m_isTopoRemoval);
    connect(m_topoRemovalCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        const bool value = (state == Qt::Checked);
        if (m_isTopoRemoval == value) {
            return;
        }
        if (!confirmParameterChange()) {
            m_topoRemovalCheckBox->blockSignals(true);
            m_topoRemovalCheckBox->setChecked(m_isTopoRemoval);
            m_topoRemovalCheckBox->blockSignals(false);
            return;
        }
        m_isTopoRemoval = value;
        invalidateNodeData();
    });
    topoRemovalLayout->addWidget(m_topoRemovalCheckBox);
    layout->addLayout(topoRemovalLayout);

    // DEM Path Row
    auto* demLayout = new QHBoxLayout();
    m_demPathLabel = new QLabel("DEM路径");
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
    connect(m_demPathEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_demPathEdit->text().trimmed();
        if (m_demPath != text) {
            m_demPath = text;
            invalidateNodeData();
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
    connect(m_demBrowseBtn, &QPushButton::clicked, this, [this, invalidateNodeData]() {
        QString file = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择DEM数据"), "", "DEM Files (*.h5 *.tiff *.tif)");
        if (!file.isEmpty()) {
            m_demPath = file;
            if (m_demPathEdit) m_demPathEdit->setText(m_demPath);
            invalidateNodeData();
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_demPath, true);
            }
        }
    });

    demLayout->addWidget(m_demPathLabel);
    demLayout->addWidget(m_demPathEdit);
    demLayout->addWidget(m_demBrowseBtn);
    layout->addLayout(demLayout);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // SOP: Widget创建尾部延迟装载自愈参数
    updateLabels();
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
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
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
        setProgress(100);
        InSARLogManager::LogInfo("SLCDerampNode", "executeProcessing completed.");
        finishExecution();
    });
    QFuture<void> future = QtConcurrent::run([h5Paths, previewJpgPaths]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], previewJpgPaths[i], "complex");
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
    if (!m_demInputData || m_demPath.isEmpty() || !demInfo.isFile() || !demInfo.isReadable())
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
        } else if (!m_demInputData) {
            setStartFailureMessage(QStringLiteral("请连接 DEM 输入端口。"));
        } else if (m_demPath.isEmpty()) {
            setStartFailureMessage(QStringLiteral("DEM 输入路径为空。"));
        } else {
            const QFileInfo demInfo(m_demPath);
            if (!demInfo.isFile()) {
                setStartFailureMessage(QStringLiteral("DEM 输入文件不存在或不是常规文件：%1").arg(m_demPath));
            } else if (!demInfo.isReadable()) {
                setStartFailureMessage(QStringLiteral("DEM 输入文件不可读：%1").arg(m_demPath));
            } else {
                setStartFailureMessage(QStringLiteral("SLC Deramp 输入参数无效。"));
            }
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
    m_preparedMasterIndex = m_masterIndex;
    m_preparedIsDeflat = m_isDeflat;
    m_preparedIsTopoRemoval = m_isTopoRemoval;
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

    m_preparedDemPath = m_demPath;

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void SLCDerampNode::executeProcessing()
{
    InSARLogManager::LogInfo("SLCDerampNode", "executeProcessing started.");

    // Retrieve input and output settings
    QString dstNode = m_preparedDstNode;
    QString dstProject = m_preparedProjectName;
    QString savePath = m_preparedSavePath;
    QString preparedDemPath = m_preparedDemPath;
    const QStringList inputPaths = m_preparedInputPaths;
    const int masterIndex = m_preparedMasterIndex;
    const bool isDeflat = m_preparedIsDeflat;
    const bool isTopoRemoval = m_preparedIsTopoRemoval;

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

    setProgress(0);
    setState(ExecutionState::Running);
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode,
                                           m_preparedOutputPaths, m_preparedInputPaths,
                                           m_outputTransaction, &transactionError)) {
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
    connect(this, &SLCDerampNode::startDeramp, m_worker, &SLCDerampWorker::SLC_deramp_with_dem);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, [this, masterIndex, dstProject, savePath, stagingNode, inputPaths, preparedDemPath, isDeflat, isTopoRemoval]() {
        Q_EMIT startDeramp(masterIndex, dstProject, savePath, stagingNode, inputPaths, preparedDemPath,
                           isDeflat, isTopoRemoval);
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
        setState(ExecutionState::Idle);
    }
}

void SLCDerampNode::updateParameterWidgetsEnableState()
{
    bool isExec = m_thread && m_thread->isRunning();
    bool enableWidgets = !isExec;

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets);
    if (m_deflatCheckBox) m_deflatCheckBox->setEnabled(enableWidgets);
    if (m_topoRemovalCheckBox) m_topoRemovalCheckBox->setEnabled(enableWidgets);

    bool hasDemConn = (m_demInputData != nullptr);
    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !hasDemConn);
    if (m_demPathEdit) m_demPathEdit->setEnabled(enableWidgets && !hasDemConn);
    if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(enableWidgets && !hasDemConn);
}

} // namespace QtNodes

