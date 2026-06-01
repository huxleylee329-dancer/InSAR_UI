#include "InSARLogManager.h"
#include "S1TopsBackGeocodingNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InterfaceManager.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "FormatConversion.h"
#include "tinyxml.h"
#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>

namespace QtNodes {

S1TopsBackGeocodingNode::S1TopsBackGeocodingNode()
    : ExecutableNodeDelegateModel()
    , m_projectCombo(nullptr)
    , m_dataNodeCombo(nullptr)
    , m_masterImageCombo(nullptr)
    , m_defaultMasterCheckBox(nullptr)
    , m_esdCheckBox(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_masterIndex(1)
    , m_useDefaultMaster(true)
    , m_bESD(true)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

S1TopsBackGeocodingNode::~S1TopsBackGeocodingNode()
{
    // Clean up remedy watcher
    m_remedyWatcher.cancel();
    m_remedyWatcher.waitForFinished();

    // Clean up worker thread
    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_thread)
    {
        if (m_thread->isRunning())
        {
            m_thread->quit();
            m_thread->wait();
        }
        m_thread->deleteLater();
        m_thread = nullptr;
    }
}

unsigned int S1TopsBackGeocodingNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;  // One input port (Sentinel-1 SLC data)
    else
        return 2;  // Two output ports (0: Results, 1: Preview)
}

NodeDataType S1TopsBackGeocodingNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "S1 Back-Geocoded Data"};
        else if (portIndex == 1)
            return NodeDataType{"image_info", "Image Info"};
    }
    Q_UNUSED(portIndex);
    return NodeDataType{"imported_file", "S1 SLC Data"};
}

bool S1TopsBackGeocodingNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return portType == PortType::Out;
}

QString S1TopsBackGeocodingNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return tr("成果 *");
        else if (portIndex == 1)
            return tr("预览 ?");
    }
    Q_UNUSED(portIndex);
    return QString();
}

bool S1TopsBackGeocodingNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> S1TopsBackGeocodingNode::outData(PortIndex port)
{
    if (port == 0)
    {
        return m_outputData;
    }
    else if (port == 1)
    {
        return m_imageInfoData;
    }
    return nullptr;
}

void S1TopsBackGeocodingNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    updateLabels();

    // Generate default output name if not set
    if (m_inputData && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
    {
        m_outputNodeNameEdit->setText(generateDefaultOutputName());
    }

    // Delegate to base class to handle execution mode
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* S1TopsBackGeocodingNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject S1TopsBackGeocodingNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    QString nodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["outputNodeName"] = nodeName;
    modelJson["masterIndex"] = m_masterIndex;
    modelJson["useDefaultMaster"] = m_useDefaultMaster;
    modelJson["bESD"] = m_bESD;

    return modelJson;
}

void S1TopsBackGeocodingNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined())
    {
        m_outputNodeName = vName.toString();
    }

    QJsonValue vIndex = json["masterIndex"];
    if (!vIndex.isUndefined())
    {
        m_masterIndex = vIndex.toInt();
    }

    QJsonValue vUseDefault = json["useDefaultMaster"];
    if (!vUseDefault.isUndefined())
    {
        m_useDefaultMaster = vUseDefault.toBool();
    }

    QJsonValue vEsd = json["bESD"];
    if (!vEsd.isUndefined())
    {
        m_bESD = vEsd.toBool();
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    if (m_defaultMasterCheckBox)
        m_defaultMasterCheckBox->setChecked(m_useDefaultMaster);

    updateMasterImageCombo();

    if (m_esdCheckBox)
        m_esdCheckBox->setChecked(m_bESD);
}

void S1TopsBackGeocodingNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // 选择工程
    auto* projectLayout = new QHBoxLayout();
    QLabel* projectLabel = new QLabel("选择工程");
    projectLabel->setFixedWidth(80);
    projectLayout->addWidget(projectLabel);
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    m_projectCombo->addItem(projectName().isEmpty() ? "未打开项目" : projectName());
    projectLayout->addWidget(m_projectCombo);
    layout->addLayout(projectLayout);

    // 数据节点
    auto* dataNodeLayout = new QHBoxLayout();
    QLabel* dataNodeLabel = new QLabel("数据节点");
    dataNodeLabel->setFixedWidth(80);
    dataNodeLayout->addWidget(dataNodeLabel);
    m_dataNodeCombo = new QComboBox();
    m_dataNodeCombo->setEditable(false);
    if (m_inputData)
    {
        QString nodeName = m_inputData->nodeName();
        m_dataNodeCombo->addItem(nodeName);
    }
    else
    {
        m_dataNodeCombo->addItem("等待输入");
    }
    dataNodeLayout->addWidget(m_dataNodeCombo);
    layout->addLayout(dataNodeLayout);

    // 默认第一张为主图像
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

    // 主图像选择 (Master Image)
    auto* masterImageLayout = new QHBoxLayout();
    QLabel* masterImageLabel = new QLabel("主图像选择");
    masterImageLabel->setFixedWidth(80);
    masterImageLayout->addWidget(masterImageLabel);
    m_masterImageCombo = new QComboBox();
    m_masterImageCombo->setEditable(false);
    m_masterImageCombo->setEnabled(!m_useDefaultMaster);
    connect(m_masterImageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        if (m_useDefaultMaster) return;
        int val = index; // With placeholder at 0, selected index maps exactly to 1-based masterIndex
        if (m_masterIndex != val) {
            if (!confirmParameterChange()) {
                m_masterImageCombo->blockSignals(true);
                m_masterImageCombo->setCurrentIndex(m_masterIndex);
                m_masterImageCombo->blockSignals(false);
                return;
            }
            m_masterIndex = val;
            invalidateNodeData();
        }
    });
    masterImageLayout->addWidget(m_masterImageCombo);
    layout->addLayout(masterImageLayout);

    // ESD 校正
    auto* esdLayout = new QHBoxLayout();
    QLabel* esdLabel = new QLabel("启用 ESD 校正");
    esdLayout->addWidget(esdLabel);
    m_esdCheckBox = new QCheckBox();
    m_esdCheckBox->setChecked(m_bESD);
    connect(m_esdCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool val = (state == Qt::Checked);
        if (m_bESD != val) {
            if (!confirmParameterChange()) {
                m_esdCheckBox->blockSignals(true);
                m_esdCheckBox->setChecked(m_bESD);
                m_esdCheckBox->blockSignals(false);
                return;
            }
            m_bESD = val;
            invalidateNodeData();
        }
    });
    esdLayout->addWidget(m_esdCheckBox);
    layout->addLayout(esdLayout);

    // 目标节点名
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

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // Populate master image combobox initially if input data is already connected
    updateLabels();
}

void S1TopsBackGeocodingNode::updateLabels()
{
    // Update project combo
    if (m_projectCombo)
    {
        QString projName = projectName();
        if (!projName.isEmpty())
        {
            if (m_projectCombo->count() == 0 || m_projectCombo->itemText(0) != projName)
            {
                m_projectCombo->clear();
                m_projectCombo->addItem(projName);
            }
        }
    }

    // Update data node combo
    if (m_dataNodeCombo)
    {
        m_dataNodeCombo->clear();
        if (m_inputData)
        {
            QString nodeName = m_inputData->nodeName();
            m_dataNodeCombo->addItem(nodeName);
        }
        else
        {
            m_dataNodeCombo->addItem("等待输入");
        }
    }

    // Update master image combo
    updateMasterImageCombo();
}

void S1TopsBackGeocodingNode::updateMasterImageCombo()
{
    if (!m_masterImageCombo) return;
    
    m_masterImageCombo->blockSignals(true);
    m_masterImageCombo->clear();
    
    m_masterImageCombo->setEnabled(!m_useDefaultMaster);
    
    if (!m_inputData) {
        if (m_useDefaultMaster) {
            m_masterImageCombo->addItem("自动选择首张图像...");
            m_masterIndex = 1;
        } else {
            m_masterImageCombo->addItem("请选择主图像...");
            m_masterIndex = 0;
        }
        m_masterImageCombo->setCurrentIndex(0);
        m_masterImageCombo->blockSignals(false);
        return;
    }
    
    qDebug() << "[BackGeocoding debug] updateMasterImageCombo start.";
    qDebug() << "[BackGeocoding debug] input nodeName:" << m_inputData->nodeName();
    qDebug() << "[BackGeocoding debug] projectName:" << projectName();
    qDebug() << "[BackGeocoding debug] projectPath:" << projectPath();
    
    QStandardItemModel* model = projectModel();
    if (!model) {
        qDebug() << "[BackGeocoding debug] projectModel is null!";
        m_masterImageCombo->blockSignals(false);
        return;
    }
    
    QList<QStandardItem*> foundProjects = model->findItems(projectName());
    if (foundProjects.isEmpty()) {
        qDebug() << "[BackGeocoding debug] Cannot find project item for project name:" << projectName();
        for (int k = 0; k < model->rowCount(); ++k) {
            if (model->item(k)) {
                qDebug() << "[BackGeocoding debug] Available project in model:" << model->item(k)->text();
            }
        }
        m_masterImageCombo->blockSignals(false);
        return;
    }
    
    QStandardItem* projectItem = foundProjects.first();
    QString srcNode = m_inputData->nodeName();
    qDebug() << "[BackGeocoding debug] Found projectItem. Children count:" << projectItem->rowCount();
    
    QStringList imageNames;
    bool foundSrcNode = false;
    for (int i = 0; i < projectItem->rowCount(); ++i)
    {
        QStandardItem* nodeItem = projectItem->child(i, 0);
        if (nodeItem) {
            qDebug() << "[BackGeocoding debug] child node" << i << ":" << nodeItem->text();
            if (nodeItem->text() == srcNode)
            {
                foundSrcNode = true;
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
    }
    
    if (!foundSrcNode) {
        qDebug() << "[BackGeocoding debug] FAILED to find srcNode:" << srcNode << "in project children.";
    } else {
        qDebug() << "[BackGeocoding debug] Successfully found srcNode. Images count:" << imageNames.size();
        for (const QString& name : imageNames) {
            qDebug() << "[BackGeocoding debug] -> Image:" << name;
        }
    }
    
    if (m_useDefaultMaster)
    {
        for (const QString& name : imageNames) {
            m_masterImageCombo->addItem(name);
        }
        if (m_masterImageCombo->count() > 0) {
            m_masterIndex = 1;
            m_masterImageCombo->setCurrentIndex(0);
        } else {
            m_masterImageCombo->addItem("自动选择首张图像...");
            m_masterIndex = 0;
            m_masterImageCombo->setCurrentIndex(0);
        }
    }
    else
    {
        m_masterImageCombo->addItem("请选择主图像...");
        for (const QString& name : imageNames) {
            m_masterImageCombo->addItem(name);
        }
        if (m_masterIndex > 0 && m_masterIndex < m_masterImageCombo->count())
        {
            m_masterImageCombo->setCurrentIndex(m_masterIndex);
        }
        else
        {
            m_masterIndex = 0;
            m_masterImageCombo->setCurrentIndex(0);
        }
    }
    
    m_masterImageCombo->blockSignals(false);
}

QString S1TopsBackGeocodingNode::generateDefaultOutputName() const
{
    if (m_inputData)
    {
        QString nodeName = m_inputData->nodeName();
        return nodeName + "_regis";
    }
    return "后向地理编码结果";
}

bool S1TopsBackGeocodingNode::validateInputs() const
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

    // Validate project context
    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
    {
        return false;
    }

    // Validate master image selection
    if (m_masterIndex <= 0)
    {
        return false;
    }

    return true;
}

void S1TopsBackGeocodingNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void S1TopsBackGeocodingNode::onProcessingFinished()
{
    // Create output data
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    QString outputPath = projectPath() + "/" + dstNode + "/";
    m_outputData = std::make_shared<ImportedFileData>(outputPath, dstNode);

    // 生成预览图
    QStringList jpgPaths;
    QStandardItemModel* model = projectModel();
    if (model)
    {
        QList<QStandardItem*> foundProjects = model->findItems(projectName());
        if (!foundProjects.isEmpty())
        {
            QStandardItem* projectItem = foundProjects.first();
            QString srcNode = m_inputData->nodeName();
            for (int i = 0; i < projectItem->rowCount(); ++i)
            {
                QStandardItem* nodeItem = projectItem->child(i, 0);
                if (nodeItem && nodeItem->text() == srcNode)
                {
                    for (int j = 0; j < nodeItem->rowCount(); ++j)
                    {
                        QStandardItem* childItem = nodeItem->child(j, 0);
                        if (childItem) {
                            QString origin_name = childItem->text();
                            QString h5Path = outputPath + origin_name + "_regis.h5";
                            QString jpgPath = outputPath + origin_name + "_regis.jpg";
                            // 静默生成预览图
                            NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "complex");
                            jpgPaths.append(jpgPath);
                        }
                    }
                    break;
                }
            }
        }
    }

    if (!jpgPaths.isEmpty())
    {
        m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    }
    else
    {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        Q_EMIT dataUpdated(1);
    }

    // Clean up thread
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    // Update UI
    m_outputNodeNameEdit->setEnabled(true);
    if (m_defaultMasterCheckBox) m_defaultMasterCheckBox->setEnabled(true);
    if (m_masterImageCombo) m_masterImageCombo->setEnabled(!m_useDefaultMaster);
    if (m_esdCheckBox) m_esdCheckBox->setEnabled(true);

    // Notify base class that we're finished
    setState(ExecutionState::Running);
    setProgress(100);
    finishExecution();
    Q_EMIT dataUpdated(0);
}

void S1TopsBackGeocodingNode::onError(const QString& error)
{
    Q_UNUSED(error);
    // Clean up thread
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    m_outputNodeNameEdit->setEnabled(true);
    if (m_defaultMasterCheckBox) m_defaultMasterCheckBox->setEnabled(true);
    if (m_masterImageCombo) m_masterImageCombo->setEnabled(!m_useDefaultMaster);
    if (m_esdCheckBox) m_esdCheckBox->setEnabled(true);
    setState(ExecutionState::Error);
}

void S1TopsBackGeocodingNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* S1TopsBackGeocodingNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString S1TopsBackGeocodingNode::projectPath() const
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

QString S1TopsBackGeocodingNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* S1TopsBackGeocodingNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void S1TopsBackGeocodingNode::execute()
{
    executeProcessing();
}

void S1TopsBackGeocodingNode::stopExecution()
{
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void S1TopsBackGeocodingNode::processAutomatically()
{
    if (validateInputs())
    {
        executeProcessing();
    }
}

void S1TopsBackGeocodingNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void S1TopsBackGeocodingNode::executeProcessing()
{
    InSARLogManager::LogInfo("S1TopsBackGeocodingNode", "executeProcessing started.");
    if (!validateInputs())
        return;

    // Prepare processing
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    QString savePath = projectPath();
    QString dstProject = projectName();
    QString srcNode = m_inputData->nodeName();
    int masterIndex = m_masterIndex;
    bool b_ESD = m_esdCheckBox ? m_esdCheckBox->isChecked() : true;

    // 覆盖提示判断
    QStringList pathsToCheck;
    QStandardItemModel* model = projectModel();
    if (model)
    {
        QList<QStandardItem*> foundProjects = model->findItems(dstProject);
        if (!foundProjects.isEmpty())
        {
            QStandardItem* projectItem = foundProjects.first();
            for (int i = 0; i < projectItem->rowCount(); ++i)
            {
                QStandardItem* nodeItem = projectItem->child(i, 0);
                if (nodeItem && nodeItem->text() == srcNode)
                {
                    int temp_images_number = nodeItem->rowCount();
                    for (int j = 0; j < temp_images_number; ++j)
                    {
                        QStandardItem* childItem = nodeItem->child(j, 0);
                        if (childItem) {
                            QString origin_name = childItem->text();
                            pathsToCheck.append(savePath + "/" + dstNode + "/" + origin_name + "_regis.h5");
                        }
                    }
                    break;
                }
            }
        }
    }

    auto overwriteRes = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), dstNode, pathsToCheck, nullptr);
    if (overwriteRes == NodeUtils::OverwriteResult::Cancel) {
        setState(ExecutionState::Idle);
        return;
    } else if (overwriteRes == NodeUtils::OverwriteResult::LoadExisting) {
        // 直接复用磁盘上的现有数据，不重新计算
        // 不调用 onProcessingFinished()，因为它会在 UI 主线程上执行重度 HDF5 读取，
        // 若文件损坏会直接崩溃。改为安全地调用 validateAndRestoreOutput()。
        qDebug() << "[BackGeocoding] LoadExisting: restoring output from disk.";
        m_outputNodeName = dstNode;
        m_outputNodeNameEdit->setEnabled(true);
        if (m_defaultMasterCheckBox) m_defaultMasterCheckBox->setEnabled(true);
        if (m_masterImageCombo) m_masterImageCombo->setEnabled(!m_useDefaultMaster);
        if (m_esdCheckBox) m_esdCheckBox->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) {
            finishExecution();
        } else {
            qDebug() << "[BackGeocoding] LoadExisting: validateAndRestoreOutput failed, setting error state.";
            setState(ExecutionState::Error);
        }
        return;
    }

    setProgress(0);
    setState(ExecutionState::Running);

    // Count images under the srcNode
    int images_number = 0;
    model = projectModel();
    if (model)
    {
        QList<QStandardItem*> foundProjects = model->findItems(dstProject);
        if (!foundProjects.isEmpty())
        {
            QStandardItem* projectItem = foundProjects.first();
            for (int i = 0; i < projectItem->rowCount(); ++i)
            {
                QStandardItem* nodeItem = projectItem->child(i, 0);
                if (nodeItem) {
                    qDebug() << "[BackGeocoding debug] executeProcessing: child" << i << "is" << nodeItem->text();
                    if (nodeItem->text() == srcNode)
                    {
                        images_number = nodeItem->rowCount();
                        qDebug() << "[BackGeocoding debug] executeProcessing: found target node" << srcNode << "with rowCount:" << images_number;
                        break;
                    }
                }
            }
        }
        else
        {
            qDebug() << "[BackGeocoding debug] executeProcessing: FAILED to find project" << dstProject << "in model.";
        }
    }
    else
    {
        qDebug() << "[BackGeocoding debug] executeProcessing: projectModel is null!";
    }

    if (images_number < 2)
    {
        InSARLogManager::LogError("S1TopsBackGeocodingNode", "Images number is less than 2, cannot perform Back-Geocoding.");
        setState(ExecutionState::Error);
        return;
    }

    // Create thread
    m_thread = new QThread();
    m_workerThread = new S1TopsBackGeocodingWorker();
    m_workerThread->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1TopsBackGeocodingNode::startBackGeocoding, m_workerThread, &S1TopsBackGeocodingWorker::S1_TOPS_BackGeocoding);
    connect(m_thread, &QThread::started, [this, images_number, masterIndex, savePath, dstProject, srcNode, dstNode, b_ESD]() {
        Q_EMIT startBackGeocoding(images_number, masterIndex, savePath, dstProject, srcNode, dstNode, projectModel(), b_ESD);
    });
    connect(m_workerThread, &S1TopsBackGeocodingWorker::updateProcess, this, &S1TopsBackGeocodingNode::onProgressUpdate);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::endProcess, this, &S1TopsBackGeocodingNode::onProcessingFinished);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::errorProcess, this, &S1TopsBackGeocodingNode::onError);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::sendModel, this, &S1TopsBackGeocodingNode::onModelUpdated);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
    m_thread->start();
    m_outputNodeNameEdit->setEnabled(false);
    if (m_defaultMasterCheckBox) m_defaultMasterCheckBox->setEnabled(false);
    if (m_masterImageCombo) m_masterImageCombo->setEnabled(false);
    if (m_esdCheckBox) m_esdCheckBox->setEnabled(false);
    InSARLogManager::LogInfo("S1TopsBackGeocodingNode", "executeProcessing completed.");

    // 在下一个事件循环中强行将状态重置为 Running，防止基类 setInData 在 Automatic 模式下将其强行设为 Idle
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

bool S1TopsBackGeocodingNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + dstNode + "/";

    // 检查目录是否存在
    QDir dir(outputPath);
    if (dir.exists()) {
        // 查找所有匹配 "*_regis.h5" 的文件
        QStringList filters;
        filters << "*_regis.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);

        if (!h5Files.isEmpty()) {
            m_outputData = std::make_shared<ImportedFileData>(outputPath, dstNode);
            setOutputData(0, m_outputData);
            Q_EMIT dataUpdated(0);

            // 双路输出：Port 1 预览输出
            // 收集已有 JPG 并查找缺失的 JPG
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

            // 先把已有的预览显示出来（如果有的话），或者清空旧的预览
            if (!existingJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
                setOutputData(1, m_imageInfoData);
                Q_EMIT dataUpdated(1);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
                Q_EMIT dataUpdated(1);
            }

            // 如果有缺失的 JPG 且 H5 存在，启动后台异步补救生成
            if (!missingH5s.isEmpty()) {
                m_remedyWatcher.cancel();
                m_remedyWatcher.waitForFinished();
                m_remedyWatcher.disconnect();

                connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, allJpgPaths]() {
                    qDebug() << "[BackGeocoding] Remedy preview generation finished. Updating Port 1.";
                    m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
                    setOutputData(1, m_imageInfoData);
                    Q_EMIT dataUpdated(1);
                });

                QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
                    for (int i = 0; i < missingH5s.size(); ++i) {
                        qDebug() << "[BackGeocoding] Remedy generating missing preview:" << missingJpgs[i];
                        NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
                    }
                });
                m_remedyWatcher.setFuture(future);
            }

            // == 树视图 (Model) 与 XML 自愈补录逻辑 ==
            QStandardItemModel* projModelPtr = projectModel();
            if (projModelPtr) {
                QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
                if (!foundProjects.isEmpty()) {
                    QStandardItem* projectItem = foundProjects.first();

                    // 1. 查找或建立配准根节点
                    QStandardItem* regis = nullptr;
                    for (int i = 0; i < projectItem->rowCount(); i++) {
                        if (projectItem->child(i, 0)->text() == dstNode) {
                            regis = projectItem->child(i, 0);
                            break;
                        }
                    }

                    if (!regis) {
                        regis = new QStandardItem(dstNode);
                        regis->setToolTip(projectName());
                        int insert = 0;
                        for (; insert < projectItem->rowCount(); insert++) {
                            if (projectItem->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-2.0") == 0)
                                continue;
                            else
                                break;
                        }
                        regis->setIcon(QIcon(FOLDER_ICON));
                        projectItem->insertRow(insert, regis);
                        QStandardItem* regis_Rank = new QStandardItem("complex-2.0");
                        projectItem->setChild(insert, 1, regis_Rank);
                    }

                    // 2. 将扫描到的各图像文件添加进模型
                    for (const QString& h5File : h5Files) {
                        QString h5Path = dir.absoluteFilePath(h5File);
                        QFileInfo fileinfo(h5Path);
                        QString regis_name = fileinfo.baseName();

                        QStandardItem* item_img = nullptr;
                        for (int j = 0; j < regis->rowCount(); j++) {
                            if (regis->child(j, 0)->text() == regis_name) {
                                item_img = regis->child(j, 0);
                                break;
                            }
                        }

                        if (!item_img) {
                            QStandardItem* regis_images_name = new QStandardItem(regis_name);
                            regis_images_name->setToolTip("complex");
                            QStandardItem* regis_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                            regis_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                            regis->appendRow(regis_images_name);
                            regis->setChild(regis->rowCount() - 1, 1, regis_images_path);
                        } else {
                            regis->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                        }
                    }
                }
            }

            // 3. XML 永久保存自愈
            XMLFile* xml = projectXml();
            if (xml) {
                bool xmlModified = false;
                TiXmlElement* root = nullptr;
                xml->get_root(root);
                if (root) {
                    // 检查 DataNode 中是否已包含此成果节点
                    bool dataNodeExists = false;
                    for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                        const char* nameAttr = p->Attribute("name");
                        if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
                            dataNodeExists = true;
                            break;
                        }
                    }

                    // 如果 XML 中不存在该数据节点，则补录
                    if (!dataNodeExists) {
                        // 我们直接在 EXE 端用 TinyXML 实现自愈写入，彻底规避旧版 FormatConversion_d.dll 的崩溃 Bug
                        for (const QString& h5File : h5Files) {
                            QString h5Path = dir.absoluteFilePath(h5File);
                            QFileInfo fileinfo(h5Path);
                            QString relativePath = QString("/%1/%2").arg(dstNode).arg(fileinfo.fileName());
                            
                            // 查找或创建该 DataNode
                            TiXmlElement* dataNodeElem = nullptr;
                            for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                                const char* nameAttr = p->Attribute("name");
                                if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
                                    dataNodeElem = p;
                                    break;
                                }
                            }
                            
                            if (!dataNodeElem) {
                                // 创建新的 DataNode
                                dataNodeElem = new TiXmlElement("DataNode");
                                dataNodeElem->SetAttribute("name", dstNode.toStdString().c_str());
                                dataNodeElem->SetAttribute("data_count", "1");
                                dataNodeElem->SetAttribute("data_processing", "coregistration");
                                dataNodeElem->SetAttribute("rank", "complex-2.0");
                                
                                int index = 1;
                                TiXmlElement* root_child = root->FirstChildElement();
                                if (root_child) {
                                    root_child = root_child->NextSiblingElement(); // 略过 project_info
                                }
                                
                                TiXmlElement* insertBeforeNode = nullptr;
                                for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++) {
                                    const char* rankAttr = p->Attribute("rank");
                                    if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 ||
                                                     strcmp(rankAttr, "complex-1.0") == 0 ||
                                                     strcmp(rankAttr, "complex-2.0") == 0)) {
                                        continue;
                                    } else {
                                        insertBeforeNode = p;
                                        break;
                                    }
                                }
                                dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());
                                
                                TiXmlElement* dataElem = new TiXmlElement("Data");
                                
                                TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                                dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
                                dataElem->LinkEndChild(dataNameNode);
                                
                                TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                                dataRankNode->LinkEndChild(new TiXmlText("complex-2.0"));
                                dataElem->LinkEndChild(dataRankNode);
                                
                                TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                                dataIndexNode->LinkEndChild(new TiXmlText("1"));
                                dataElem->LinkEndChild(dataIndexNode);
                                
                                TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                                dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                                dataElem->LinkEndChild(dataPathNode);
                                
                                TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
                                rowOffsetNode->LinkEndChild(new TiXmlText("0"));
                                dataElem->LinkEndChild(rowOffsetNode);
                                
                                TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
                                colOffsetNode->LinkEndChild(new TiXmlText("0"));
                                dataElem->LinkEndChild(colOffsetNode);
                                
                                dataNodeElem->LinkEndChild(dataElem);
                                
                                TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");
                                TiXmlElement* masterImageElem = new TiXmlElement("master_image");
                                masterImageElem->LinkEndChild(new TiXmlText(QString::number(m_masterIndex > 0 ? m_masterIndex : 1).toStdString().c_str()));
                                paramsElem->LinkEndChild(masterImageElem);
                                dataNodeElem->LinkEndChild(paramsElem);
                                
                                if (insertBeforeNode) {
                                    root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                                    delete dataNodeElem; // InsertBeforeChild 做的是拷贝，需要销毁原堆对象
                                    
                                    // 刷新后续节点的 index
                                    for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement()) {
                                        index++;
                                        p->SetAttribute("index", QString::number(index).toStdString().c_str());
                                    }
                                } else {
                                    root->LinkEndChild(dataNodeElem);
                                }
                            } else {
                                // 成果节点已存在，追加新的 Data 元素
                                const char* countAttr = dataNodeElem->Attribute("data_count");
                                int count = countAttr ? QString(countAttr).toInt() : 0;
                                count++;
                                dataNodeElem->SetAttribute("data_count", QString::number(count).toStdString().c_str());
                                
                                TiXmlElement* lastChildNode = dataNodeElem->LastChild() ? dataNodeElem->LastChild()->ToElement() : nullptr;
                                
                                TiXmlElement* dataElem = new TiXmlElement("Data");
                                
                                TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                                dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
                                dataElem->LinkEndChild(dataNameNode);
                                
                                TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                                dataRankNode->LinkEndChild(new TiXmlText("complex-2.0"));
                                dataElem->LinkEndChild(dataRankNode);
                                
                                TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                                dataIndexNode->LinkEndChild(new TiXmlText(QString::number(count).toStdString().c_str()));
                                dataElem->LinkEndChild(dataIndexNode);
                                
                                TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                                dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                                dataElem->LinkEndChild(dataPathNode);
                                
                                TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
                                rowOffsetNode->LinkEndChild(new TiXmlText("0"));
                                dataElem->LinkEndChild(rowOffsetNode);
                                
                                TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
                                colOffsetNode->LinkEndChild(new TiXmlText("0"));
                                dataElem->LinkEndChild(colOffsetNode);
                                
                                if (lastChildNode) {
                                    dataNodeElem->InsertBeforeChild(lastChildNode, *dataElem);
                                    delete dataElem; // 销毁堆对象
                                } else {
                                    dataNodeElem->LinkEndChild(dataElem);
                                }
                            }
                            xmlModified = true;
                        }
                    }
                }
                if (xmlModified) {
                    QString xmlPath = projectPath() + "/" + projectName();
                    xml->XMLFile_save(xmlPath.toStdString().c_str());
                }
            }

            // 4. 刷新左侧树视图
            auto iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                iface->refreshProjectTree();
            }

            return true;
        }
    }

    return false;
}

QStringList S1TopsBackGeocodingNode::previewImagePaths() const
{
    QStringList existingPaths;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return existingPaths;

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*_regis.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);
        for (const QString& h5File : h5Files) {
            QString h5Path = dir.absoluteFilePath(h5File);
            QFileInfo fi(h5Path);
            QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                existingPaths << jpgPath;
            }
        }
    }
    return existingPaths;
}

} // namespace QtNodes
