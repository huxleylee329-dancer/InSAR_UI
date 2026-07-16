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
            return QStringLiteral("DEM ?");
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

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
        
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

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*_deramp.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);
        for (const QString& h5File : h5Files) {
            QString h5Path = dir.absoluteFilePath(h5File);
            QFileInfo fi(h5Path);
            QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
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

    QString outputPath = projectPath() + "/" + dstNode + "/";

    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*_deramp.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);

        if (!h5Files.isEmpty()) {
            // 恢复 Port 0 数据
            QStringList h5Paths;
            for (const QString& h5File : h5Files) {
                h5Paths.append(dir.absoluteFilePath(h5File));
            }
            h5Paths.sort();
            m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
            setOutputData(0, m_outputData);
            Q_EMIT dataUpdated(0);

            // 恢复 Port 1 预览数据 & 后台异步补救缺失的 JPG
            QStringList existingJpgPaths;
            QStringList missingH5s;
            QStringList missingJpgs;
            QStringList allJpgPaths;

            for (const QString& h5File : h5Files) {
                QString h5Path = dir.absoluteFilePath(h5File);
                QFileInfo fi(h5Path);
                QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
                allJpgPaths.append(jpgPath);

                if (QFile::exists(jpgPath)) {
                    existingJpgPaths.append(jpgPath);
                } else {
                    missingH5s.append(h5Path);
                    missingJpgs.append(jpgPath);
                }
            }

            if (!existingJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
                setOutputData(1, m_imageInfoData);
                Q_EMIT dataUpdated(1);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
                Q_EMIT dataUpdated(1);
            }

            // 异步补录 JPG 预览
            if (!missingH5s.isEmpty()) {
                m_remedyWatcher.cancel();
                m_remedyWatcher.waitForFinished();
                m_remedyWatcher.disconnect();

                connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, allJpgPaths]() {
                    InSARLogManager::LogInfo("SLCDerampNode", "Remedy preview generation finished. Updating Port 1.");
                    m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
                    setOutputData(1, m_imageInfoData);
                    Q_EMIT dataUpdated(1);
                });

                QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
                    for (int i = 0; i < missingH5s.size(); ++i) {
                        NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
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
                    for (const QString& h5File : h5Files) {
                        QString h5Path = dir.absoluteFilePath(h5File);
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
    setProgress(progress);
}

void SLCDerampNode::onProcessingFinished()
{
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    QString outputPath = projectPath() + "/" + dstNode + "/";

    QStringList h5Paths;
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*_deramp.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files | QDir::NoSymLinks);
        h5Files.sort();
        for (const QString& h5File : h5Files) {
            h5Paths.append(dir.absoluteFilePath(h5File));
        }
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    // 收集生成的预览图
    QStringList jpgPaths;
    for (const QString& h5Path : h5Paths) {
        QFileInfo fi(h5Path);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        if (QFile::exists(jpgPath)) {
            jpgPaths.append(jpgPath);
        }
    }

    if (!jpgPaths.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
        setOutputData(1, m_imageInfoData);
    } else {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
    }

    // Clean up thread
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (m_worker)
    {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    // Update UI
    updateParameterWidgetsEnableState();

    setState(ExecutionState::Running);
    setProgress(100);
    InSARLogManager::LogInfo("SLCDerampNode", "executeProcessing completed.");
    finishExecution();
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
}

void SLCDerampNode::onError(const QString& error)
{
    InSARLogManager::LogError("SLCDerampNode", "Execution error: " + error);
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (m_worker)
    {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    updateParameterWidgetsEnableState();
    setState(ExecutionState::Error);
}

void SLCDerampNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

void SLCDerampNode::onResultsReceived(
    const QString& dstNode,
    const QStringList& h5Paths,
    const QStringList& originNames)
{
    // 用全局 XML 句柄 + 原生 TinyXML 写入，绕过外部 DLL 接口以避崩溃 (SOP 9)
    XMLFile* xml = projectXml();
    if (!xml)
    {
        InSARLogManager::LogError("SLCDerampNode", "onResultsReceived: projectXml() is null, skipping XML write.");
        return;
    }

    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (!root)
    {
        InSARLogManager::LogError("SLCDerampNode", "onResultsReceived: XML root is null, skipping XML write.");
        return;
    }

    bool xmlModified = false;
    for (int i = 0; i < h5Paths.size(); i++)
    {
        QFileInfo fileinfo(h5Paths.at(i));
        QString relativePath = QString("/%1/%2").arg(dstNode).arg(fileinfo.fileName());

        // 查找或新建 DataNode
        TiXmlElement* dataNodeElem = nullptr;
        for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement())
        {
            const char* nameAttr = p->Attribute("name");
            if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode)
            {
                dataNodeElem = p;
                break;
            }
        }

        if (!dataNodeElem)
        {
            // 新建 DataNode
            dataNodeElem = new TiXmlElement("DataNode");
            dataNodeElem->SetAttribute("name", dstNode.toStdString().c_str());
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
            masterImageElem->LinkEndChild(new TiXmlText(QString::number(m_masterIndex).toStdString().c_str()));
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

    if (xmlModified)
    {
        QString xmlPath = projectPath() + "/" + projectName();
        xml->XMLFile_save(xmlPath.toStdString().c_str());
        InSARLogManager::LogInfo("SLCDerampNode", "onResultsReceived: XML saved via native TinyXML.");
    }
}

bool SLCDerampNode::validateInputs() const
{
    if (!m_inputData)
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
    if (!validateInputs())
    {
        return false;
    }

    QString dstNode = m_outputNodeNameEdit->text().trimmed();
    if (dstNode.isEmpty()) dstNode = generateDefaultOutputName();
    m_preparedDstNode = dstNode;

    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir outputDir(outputPath);
    if (outputDir.exists()) {
        QStringList filters;
        filters << "*_deramp.h5";
        QStringList existingH5 = outputDir.entryList(filters, QDir::Files);
        if (!existingH5.isEmpty()) {
            if (_isAutoTriggered) {
                m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
            } else {
                m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
                    NodeUtils::getProjectContext(_widget),
                    caption(),
                    existingH5,
                    nullptr
                );
            }
        }
    }

    m_preparedDemPath = m_demPath;

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void SLCDerampNode::executeProcessing()
{
    InSARLogManager::LogInfo("SLCDerampNode", "executeProcessing started.");

    // Retrieve input and output settings
    QString srcNode = m_inputData->nodeName();
    QString dstNode = m_preparedDstNode;
    QString dstProject = projectName();
    QString savePath = projectPath();
    QString preparedDemPath = m_preparedDemPath;

    m_outputNodeName = dstNode;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting)
    {
        if (validateAndRestoreOutput()) {
            setState(ExecutionState::Running);
            setProgress(100);
            finishExecution();
            return;
        }
        else
        {
            setState(ExecutionState::Error);
            return;
        }
    }

    setProgress(0);

    // 2. 覆盖运行前，清理工程 XML 的旧记录 and 左侧树视图以避影分身 (SOP 14)
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode);

    // Create thread and worker
    m_thread = new QThread();
    m_worker = new SLCDerampWorker();
    m_worker->moveToThread(m_thread);

    // Connect signals
    connect(this, &SLCDerampNode::startDeramp, m_worker, &SLCDerampWorker::SLC_deramp_with_dem);
    connect(m_thread, &QThread::started, [this, dstProject, srcNode, dstNode, preparedDemPath]() {
        Q_EMIT startDeramp(m_masterIndex, dstProject, srcNode, dstNode, projectModel(), preparedDemPath);
    });
    connect(m_worker, &SLCDerampWorker::updateProcess, this, &SLCDerampNode::onProgressUpdate);
    connect(m_worker, &SLCDerampWorker::endProcess, this, &SLCDerampNode::onProcessingFinished);
    connect(m_worker, &SLCDerampWorker::errorProcess, this, &SLCDerampNode::onError);
    connect(m_worker, &SLCDerampWorker::sendModel, this, &SLCDerampNode::onModelUpdated);
    connect(m_worker, &SLCDerampWorker::sendResults, this, &SLCDerampNode::onResultsReceived);
    connect(m_worker, &SLCDerampWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
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
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
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

    bool hasDemConn = (m_demInputData != nullptr);
    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !hasDemConn);
    if (m_demPathEdit) m_demPathEdit->setEnabled(enableWidgets && !hasDemConn);
    if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(enableWidgets && !hasDemConn);
}

} // namespace QtNodes
