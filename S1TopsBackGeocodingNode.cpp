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
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>

namespace QtNodes {

S1TopsBackGeocodingNode::S1TopsBackGeocodingNode()
    : ExecutableNodeDelegateModel()
    , m_masterImageCombo(nullptr)
    , m_defaultMasterCheckBox(nullptr)
    , m_esdCheckBox(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_inputData(nullptr)
    , m_demInputData(nullptr)
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
        return 2;  // 0: S1 SLC Data, 1: Optional DEM File
    else
        return 2;  // Two output ports (0: Results, 1: Preview)
}

NodeDataType S1TopsBackGeocodingNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "S1 SLC Data"};
        else
            return NodeDataType{"imported_file", "DEM File"};
    }
    else
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "S1 Back-Geocoded Data"};
        else if (portIndex == 1)
            return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool S1TopsBackGeocodingNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    Q_UNUSED(portType);
    return true;
}

QString S1TopsBackGeocodingNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        if (portIndex == 0)
            return tr("输入图像");
        else
            return tr("DEM");
    }
    else
    {
        if (portIndex == 0)
            return tr("成果 *");
        else if (portIndex == 1)
            return tr("预览 ?");
    }
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
    return ExecutableNodeDelegateModel::outData(port);
}

void S1TopsBackGeocodingNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
        updateLabels();

        // Generate default output name if not set
        if (m_inputData && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(generateDefaultOutputName());
        }

        if (!m_inputData)
        {
            m_outputData.reset();
            m_imageInfoData.reset();
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

        updateParameterWidgetsEnableState(); // 更新 DEM 连接状态对应的控件可用性
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
    modelJson["demPath"] = m_demPath;

    QStringList outputPaths = m_savedOutputPaths;
    if (m_outputData && !m_outputData->filePaths().isEmpty()) {
        outputPaths = m_outputData->filePaths();
    }
    if (!outputPaths.isEmpty()) {
        QJsonArray outputArray;
        for (const QString& path : outputPaths) {
            outputArray.append(path);
        }
        modelJson["outputPaths"] = outputArray;

        QString masterOutputPath = m_savedMasterOutputPath;
        if (masterOutputPath.isEmpty() || !outputPaths.contains(masterOutputPath)) {
            masterOutputPath = outputPaths.first();
        }
        modelJson["masterOutputPath"] = masterOutputPath;
    }

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

    QJsonValue vDemPath = json["demPath"];
    if (!vDemPath.isUndefined())
    {
        m_demPath = vDemPath.toString();
        if (m_demPathEdit) m_demPathEdit->setText(m_demPath);
    }

    m_savedOutputPaths.clear();
    QJsonArray outputArray = json["outputPaths"].toArray();
    for (const QJsonValue& value : outputArray) {
        QString path = value.toString();
        if (!path.isEmpty()) {
            m_savedOutputPaths.append(path);
        }
    }
    m_savedMasterOutputPath = json["masterOutputPath"].toString();

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

    // DEM 路径选择
    auto* demLayout = new QHBoxLayout();
    m_demPathLabel = new QLabel("DEM高程数据");
    m_demPathLabel->setFixedWidth(80);
    m_demPathLabel->setStyleSheet("QLabel:disabled { color: #888888; }");

    if (m_demPath.isEmpty()) {
        auto* iface = NodeUtils::getProjectContext(_widget);
        if (iface) {
            m_demPath = NodeUtils::getGlobalDemPath(iface);
        }
    }

    m_demPathEdit = new QLineEdit();
    m_demPathEdit->setObjectName("demPathEdit");
    m_demPathEdit->setText(m_demPath);
    m_demPathEdit->setPlaceholderText(QStringLiteral("选择DEM数据 (*.h5, *.tiff, *.zip)..."));
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
        QString file = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择DEM数据"), "", "DEM Files (*.h5 *.tiff *.tif *.zip)");
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

    // Populate master image combobox initially if input data is already connected
    updateLabels();
    updateParameterWidgetsEnableState();
}

void S1TopsBackGeocodingNode::updateLabels()
{
    // Update master image combo
    updateMasterImageCombo();
    updateParameterWidgetsEnableState();
}

void S1TopsBackGeocodingNode::updateParameterWidgetsEnableState()
{
    bool isExec = m_thread && m_thread->isRunning();
    bool enableWidgets = !isExec;
    bool hasDemConn = (m_demInputData != nullptr);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets);
    if (m_defaultMasterCheckBox) m_defaultMasterCheckBox->setEnabled(enableWidgets);
    if (m_masterImageCombo) m_masterImageCombo->setEnabled(enableWidgets && !m_useDefaultMaster);
    if (m_esdCheckBox) m_esdCheckBox->setEnabled(enableWidgets);

    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !hasDemConn);
    if (m_demPathEdit) m_demPathEdit->setEnabled(enableWidgets && !hasDemConn);
    if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(enableWidgets && !hasDemConn);
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
        if (nodeItem) {
            if (nodeItem->text() == srcNode)
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

    // Validate DEM path
    if (m_demPath.isEmpty())
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

QStringList S1TopsBackGeocodingNode::moveMasterToFront(const QStringList& paths, int masterIndex) const
{
    QStringList orderedPaths = paths;
    int masterPosition = masterIndex - 1;
    if (masterPosition > 0 && masterPosition < orderedPaths.size()) {
        orderedPaths.prepend(orderedPaths.takeAt(masterPosition));
    }
    return orderedPaths;
}

QStringList S1TopsBackGeocodingNode::jpgPathsFromH5Paths(const QStringList& h5Paths) const
{
    QStringList jpgPaths;
    for (const QString& h5Path : h5Paths) {
        QFileInfo fi(h5Path);
        jpgPaths.append(fi.absolutePath() + "/" + fi.baseName() + ".jpg");
    }
    return jpgPaths;
}

QString S1TopsBackGeocodingNode::resolveSavedOutputPath(const QString& path, const QString& dstNode) const
{
    if (path.isEmpty())
        return QString();

    QFileInfo savedInfo(path);
    if (savedInfo.isAbsolute() && savedInfo.exists())
        return savedInfo.absoluteFilePath();

    QString relativePath = QDir::fromNativeSeparators(path);
    while (relativePath.startsWith('/')) {
        relativePath.remove(0, 1);
    }

    QString candidate;
    if (relativePath.contains('/')) {
        candidate = QDir(projectPath()).absoluteFilePath(relativePath);
    } else {
        candidate = QDir(projectPath() + "/" + dstNode).absoluteFilePath(relativePath);
    }
    if (QFileInfo::exists(candidate))
        return QFileInfo(candidate).absoluteFilePath();

    QString relocatedPath = QDir(projectPath() + "/" + dstNode).absoluteFilePath(savedInfo.fileName());
    return QFileInfo::exists(relocatedPath) ? QFileInfo(relocatedPath).absoluteFilePath() : QString();
}

QStringList S1TopsBackGeocodingNode::restoreOrderedH5Paths(const QString& dstNode) const
{
    QStringList orderedPaths;
    int restoreSource = 0;
    int restoredMasterIndex = m_masterIndex;

    // 新工程优先使用节点保存的权威输出顺序
    if (!m_savedOutputPaths.isEmpty()) {
        bool allExist = true;
        for (const QString& savedPath : m_savedOutputPaths) {
            QString resolvedPath = resolveSavedOutputPath(savedPath, dstNode);
            if (resolvedPath.isEmpty()) {
                allExist = false;
                break;
            }
            orderedPaths.append(resolvedPath);
        }
        if (!allExist) {
            orderedPaths.clear();
        } else {
            restoreSource = 1;
        }
    }

    // 旧工程优先保留 XML 中 Data 元素的物理顺序
    if (orderedPaths.isEmpty()) {
        XMLFile* xml = projectXml();
        TiXmlElement* root = nullptr;
        if (xml && xml->get_root(root) >= 0 && root) {
            for (TiXmlElement* node = root->FirstChildElement("DataNode"); node;
                 node = node->NextSiblingElement("DataNode")) {
                const char* nameAttr = node->Attribute("name");
                if (!nameAttr || QString::fromUtf8(nameAttr) != dstNode)
                    continue;

                TiXmlElement* params = node->FirstChildElement("Data_Processing_Parameters");
                TiXmlElement* masterElem = params ? params->FirstChildElement("master_image") : nullptr;
                if (masterElem && masterElem->GetText()) {
                    int xmlMasterIndex = QString::fromUtf8(masterElem->GetText()).toInt();
                    if (xmlMasterIndex > 0) {
                        restoredMasterIndex = xmlMasterIndex;
                    }
                }

                for (TiXmlElement* data = node->FirstChildElement("Data"); data;
                     data = data->NextSiblingElement("Data")) {
                    TiXmlElement* pathElem = data->FirstChildElement("Data_Path");
                    if (!pathElem || !pathElem->GetText())
                        continue;
                    QString resolvedPath = resolveSavedOutputPath(QString::fromUtf8(pathElem->GetText()), dstNode);
                    if (!resolvedPath.isEmpty() && !orderedPaths.contains(resolvedPath)) {
                        orderedPaths.append(resolvedPath);
                    }
                }
                break;
            }
        }
        if (!orderedPaths.isEmpty()) {
            restoreSource = 2;
        }
    }

    // XML 缺失时保留当前项目树的子项顺序
    if (orderedPaths.isEmpty()) {
        QStandardItemModel* model = projectModel();
        if (model) {
            QList<QStandardItem*> projects = model->findItems(projectName());
            if (!projects.isEmpty()) {
                QStandardItem* project = projects.first();
                for (int i = 0; i < project->rowCount(); ++i) {
                    QStandardItem* node = project->child(i, 0);
                    if (!node || node->text() != dstNode)
                        continue;
                    for (int j = 0; j < node->rowCount(); ++j) {
                        QStandardItem* pathItem = node->child(j, 1);
                        QString resolvedPath = pathItem
                            ? resolveSavedOutputPath(pathItem->text(), dstNode) : QString();
                        if (!resolvedPath.isEmpty() && !orderedPaths.contains(resolvedPath)) {
                            orderedPaths.append(resolvedPath);
                        }
                    }
                    break;
                }
            }
        }
        if (!orderedPaths.isEmpty()) {
            restoreSource = 3;
        }
    }

    // 树和 XML 都不可用时，按上游列表顺序推导旧工程输出路径
    if (orderedPaths.isEmpty() && m_inputData) {
        QDir outputDir(projectPath() + "/" + dstNode);
        for (const QString& inputPath : m_inputData->filePaths()) {
            QString candidate = outputDir.absoluteFilePath(QFileInfo(inputPath).baseName() + "_regis.h5");
            if (QFileInfo::exists(candidate)) {
                orderedPaths.append(QFileInfo(candidate).absoluteFilePath());
            }
        }
        if (!orderedPaths.isEmpty()) {
            restoreSource = 4;
        }
    }

    // 最后才使用目录枚举，旧工程无法从磁盘恢复历史添加顺序
    if (orderedPaths.isEmpty()) {
        QDir outputDir(projectPath() + "/" + dstNode);
        const QStringList h5Files = outputDir.entryList(QStringList{"*_regis.h5"}, QDir::Files);
        for (const QString& h5File : h5Files) {
            orderedPaths.append(outputDir.absoluteFilePath(h5File));
        }
        if (!orderedPaths.isEmpty()) {
            restoreSource = 5;
            InSARLogManager::LogWarning("S1TopsBackGeocodingNode",
                "旧工程缺少输出顺序信息，已使用目录顺序恢复。");
        }
    }

    QString resolvedMasterPath;
    if (!m_savedMasterOutputPath.isEmpty()) {
        resolvedMasterPath = resolveSavedOutputPath(m_savedMasterOutputPath, dstNode);
    }

    if (!resolvedMasterPath.isEmpty()) {
        int masterPosition = orderedPaths.indexOf(resolvedMasterPath);
        if (masterPosition > 0) {
            orderedPaths.prepend(orderedPaths.takeAt(masterPosition));
        }
    } else if (restoreSource >= 2 && restoreSource <= 4) {
        orderedPaths = moveMasterToFront(orderedPaths, restoredMasterIndex);
    } else if (restoreSource == 5 && m_masterIndex > 1) {
        InSARLogManager::LogWarning("S1TopsBackGeocodingNode",
            "目录恢复无法确认历史主影像身份，已保留目录顺序。");
    }

    return orderedPaths;
}

void S1TopsBackGeocodingNode::syncProjectTreeOrder(const QStringList& h5Paths, const QString& dstNode)
{
    QStandardItemModel* model = projectModel();
    if (!model)
        return;

    QList<QStandardItem*> projects = model->findItems(projectName());
    if (projects.isEmpty())
        return;

    QStandardItem* project = projects.first();
    QStandardItem* resultNode = NodeUtils::findOrCreateProjectNode(
        project, dstNode, "complex-2.0", FOLDER_ICON);
    if (!resultNode)
        return;

    resultNode->setToolTip(projectName());
    resultNode->removeRows(0, resultNode->rowCount());
    for (const QString& h5Path : h5Paths) {
        QFileInfo fi(h5Path);
        NodeUtils::findOrCreateChildItem(
            resultNode, fi.baseName(), "complex", fi.absoluteFilePath(), IMAGEDATA_ICON);
    }
}

void S1TopsBackGeocodingNode::syncProjectXmlOrder(
    const QStringList& h5Paths, const QString& dstNode)
{
    XMLFile* xml = projectXml();
    TiXmlElement* root = nullptr;
    if (!xml || xml->get_root(root) < 0 || !root)
        return;

    TiXmlElement* dataNode = nullptr;
    for (TiXmlElement* node = root->FirstChildElement("DataNode"); node;
         node = node->NextSiblingElement("DataNode")) {
        const char* nameAttr = node->Attribute("name");
        if (nameAttr && QString::fromUtf8(nameAttr) == dstNode) {
            dataNode = node;
            break;
        }
    }

    if (!dataNode) {
        dataNode = new TiXmlElement("DataNode");
        dataNode->SetAttribute("name", dstNode.toStdString().c_str());
        dataNode->SetAttribute("data_processing", "coregistration");
        dataNode->SetAttribute("rank", "complex-2.0");
        int dataNodeIndex = 1;
        for (TiXmlElement* node = root->FirstChildElement("DataNode"); node;
             node = node->NextSiblingElement("DataNode")) {
            ++dataNodeIndex;
        }
        dataNode->SetAttribute("index", QString::number(dataNodeIndex).toStdString().c_str());
        root->LinkEndChild(dataNode);
    }
    dataNode->SetAttribute("data_count", QString::number(h5Paths.size()).toStdString().c_str());

    TiXmlElement* data = dataNode->FirstChildElement("Data");
    while (data) {
        TiXmlElement* next = data->NextSiblingElement("Data");
        dataNode->RemoveChild(data);
        data = next;
    }

    TiXmlElement* params = dataNode->FirstChildElement("Data_Processing_Parameters");
    if (!params) {
        params = new TiXmlElement("Data_Processing_Parameters");
        dataNode->LinkEndChild(params);
    }
    TiXmlElement* masterElem = params->FirstChildElement("master_image");
    if (!masterElem) {
        masterElem = new TiXmlElement("master_image");
        params->LinkEndChild(masterElem);
    }
    masterElem->Clear();
    // Data 已规范为主影像首位，XML 中的主影像索引也统一为 1
    masterElem->LinkEndChild(new TiXmlText("1"));

    for (int i = 0; i < h5Paths.size(); ++i) {
        QFileInfo fi(h5Paths.at(i));
        TiXmlElement dataElem("Data");

        TiXmlElement* nameElem = new TiXmlElement("Data_Name");
        nameElem->LinkEndChild(new TiXmlText(fi.baseName().toStdString().c_str()));
        dataElem.LinkEndChild(nameElem);
        TiXmlElement* rankElem = new TiXmlElement("Data_Rank");
        rankElem->LinkEndChild(new TiXmlText("complex-2.0"));
        dataElem.LinkEndChild(rankElem);
        TiXmlElement* indexElem = new TiXmlElement("Data_Index");
        indexElem->LinkEndChild(new TiXmlText(QString::number(i + 1).toStdString().c_str()));
        dataElem.LinkEndChild(indexElem);
        TiXmlElement* pathElem = new TiXmlElement("Data_Path");
        pathElem->LinkEndChild(new TiXmlText(
            QString("/%1/%2").arg(dstNode, fi.fileName()).toStdString().c_str()));
        dataElem.LinkEndChild(pathElem);
        TiXmlElement* rowElem = new TiXmlElement("Row_Offset");
        rowElem->LinkEndChild(new TiXmlText("0"));
        dataElem.LinkEndChild(rowElem);
        TiXmlElement* colElem = new TiXmlElement("Col_Offset");
        colElem->LinkEndChild(new TiXmlText("0"));
        dataElem.LinkEndChild(colElem);

        dataNode->InsertBeforeChild(params, dataElem);
    }

    QString xmlPath = projectPath();
    if (!xmlPath.endsWith(".insar", Qt::CaseInsensitive)) {
        xmlPath = QDir(xmlPath).absoluteFilePath(projectName());
    }
    xml->XMLFile_save(xmlPath.toStdString().c_str());
}

void S1TopsBackGeocodingNode::onProcessingFinished(
    const QStringList& regisH5Paths,
    const QString& dstNode,
    const QString& dstProject,
    const QString& savePath,
    int masterIndex
)
{
    Q_UNUSED(dstProject);
    Q_UNUSED(savePath);

    // Worker 保持原输入顺序计算，Node 仅在输出阶段把主影像稳定移到首位
    QStringList orderedH5Paths = moveMasterToFront(regisH5Paths, masterIndex);
    m_savedOutputPaths = orderedH5Paths;
    m_savedMasterOutputPath = orderedH5Paths.isEmpty() ? QString() : orderedH5Paths.first();

    syncProjectXmlOrder(orderedH5Paths, dstNode);
    syncProjectTreeOrder(orderedH5Paths, dstNode);

    QStringList jpgPaths = jpgPathsFromH5Paths(orderedH5Paths);
    m_outputData = std::make_shared<ImportedFileData>(orderedH5Paths, dstNode);
    setOutputData(0, m_outputData);

    if (!orderedH5Paths.isEmpty())
    {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, jpgPaths]() {
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);

            updateParameterWidgetsEnableState();
            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("S1TopsBackGeocodingNode", "executeProcessing completed.");
            finishExecution();
            Q_EMIT dataUpdated(0);
        });

        QFuture<void> future = QtConcurrent::run([orderedH5Paths, jpgPaths]() {
            for (int i = 0; i < orderedH5Paths.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(orderedH5Paths[i], jpgPaths[i], "complex");
            }
        });
        m_remedyWatcher.setFuture(future);
    }
    else
    {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        Q_EMIT dataUpdated(1);

        updateParameterWidgetsEnableState();
        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("S1TopsBackGeocodingNode", "executeProcessing completed.");
        finishExecution();
        Q_EMIT dataUpdated(0);
    }
}

void S1TopsBackGeocodingNode::onError(const QString& error)
{
    InSARLogManager::LogError("S1TopsBackGeocodingNode", "Execution failed: " + error);
    qDebug() << "[RegistrationNode] Error:" << error;

    updateParameterWidgetsEnableState();
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
    }
}

void S1TopsBackGeocodingNode::processAutomatically()
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

void S1TopsBackGeocodingNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

bool S1TopsBackGeocodingNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    m_preparedDstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    m_preparedSavePath = projectPath();
    m_preparedDstProject = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedMasterIndex = m_masterIndex;
    m_preparedBESD = m_esdCheckBox ? m_esdCheckBox->isChecked() : true;
    m_preparedDemPath = m_demPath;

    // 覆盖提示判断
    QStringList pathsToCheck;
    QStandardItemModel* model = projectModel();
    if (model)
    {
        QList<QStandardItem*> foundProjects = model->findItems(m_preparedDstProject);
        if (!foundProjects.isEmpty())
        {
            QStandardItem* projectItem = foundProjects.first();
            for (int i = 0; i < projectItem->rowCount(); ++i)
            {
                QStandardItem* nodeItem = projectItem->child(i, 0);
                if (nodeItem && nodeItem->text() == m_preparedSrcNode)
                {
                    int temp_images_number = nodeItem->rowCount();
                    for (int j = 0; j < temp_images_number; ++j)
                    {
                        QStandardItem* childItem = nodeItem->child(j, 0);
                        if (childItem) {
                            QString origin_name = childItem->text();
                            pathsToCheck.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + origin_name + "_regis.h5");
                        }
                    }
                    break;
                }
            }
        }
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), m_preparedDstNode, pathsToCheck, nullptr);
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        return false;
    }

    m_preparedImagesNumber = 0;
    model = projectModel();
    if (model)
    {
        QList<QStandardItem*> foundProjects = model->findItems(m_preparedDstProject);
        if (!foundProjects.isEmpty())
        {
            QStandardItem* projectItem = foundProjects.first();
            for (int i = 0; i < projectItem->rowCount(); ++i)
            {
                QStandardItem* nodeItem = projectItem->child(i, 0);
                if (nodeItem) {
                    if (nodeItem->text() == m_preparedSrcNode)
                    {
                        m_preparedImagesNumber = nodeItem->rowCount();
                        break;
                    }
                }
            }
        }
    }

    if (m_preparedImagesNumber < 2)
    {
        InSARLogManager::LogError("S1TopsBackGeocodingNode", "Images number is less than 2, cannot perform Back-Geocoding.");
        return false;
    }

    return true;
}

void S1TopsBackGeocodingNode::executeProcessing()
{
    InSARLogManager::LogInfo("S1TopsBackGeocodingNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        // 直接复用磁盘上的现有数据，不重新计算
        // 不调用 onProcessingFinished()，因为它会在 UI 主线程上执行重度 HDF5 读取，
        // 若文件损坏会直接崩溃。改为安全地调用 validateAndRestoreOutput()。
        m_outputNodeName = m_preparedDstNode;
        updateParameterWidgetsEnableState();
        
        setProgress(100);
        if (validateAndRestoreOutput()) {
            finishExecution();
        } else {
            setState(ExecutionState::Error);
        }
        return;
    }

    // 清理旧数据，防止反复执行导致UI Tree数据累加
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_preparedDstNode);

    setProgress(0);

    // Create thread
    m_thread = new QThread();
    m_workerThread = new S1TopsBackGeocodingWorker();
    m_workerThread->setDemPath(m_preparedDemPath);
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]() {
        m_workerThread = nullptr;
        m_thread = nullptr;
    });

    // Connect signals
    connect(this, &S1TopsBackGeocodingNode::startBackGeocoding, m_workerThread, &S1TopsBackGeocodingWorker::S1_TOPS_BackGeocoding);
    
    int images_number = m_preparedImagesNumber;
    int masterIndex = m_preparedMasterIndex;
    QString savePath = m_preparedSavePath;
    QString dstProject = m_preparedDstProject;
    QString srcNode = m_preparedSrcNode;
    QString dstNode = m_preparedDstNode;
    bool b_ESD = m_preparedBESD;
    
    connect(m_thread, &QThread::started, [this, images_number, masterIndex, savePath, dstProject, srcNode, dstNode, b_ESD]() {
        Q_EMIT startBackGeocoding(images_number, masterIndex, savePath, dstProject, srcNode, dstNode, projectModel(), b_ESD);
    });
    connect(m_workerThread, &S1TopsBackGeocodingWorker::updateProcess, this, &S1TopsBackGeocodingNode::onProgressUpdate);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::registrationFinished, this, &S1TopsBackGeocodingNode::onProcessingFinished);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::errorProcess, this, &S1TopsBackGeocodingNode::onError);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::sendModel, this, &S1TopsBackGeocodingNode::onModelUpdated);

    // Start thread
    m_thread->start();
    updateParameterWidgetsEnableState();

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

    QStringList orderedH5Paths = restoreOrderedH5Paths(dstNode);
    if (orderedH5Paths.isEmpty())
        return false;

    m_savedOutputPaths = orderedH5Paths;
    m_savedMasterOutputPath = orderedH5Paths.first();
    m_outputData = std::make_shared<ImportedFileData>(orderedH5Paths, dstNode);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    QStringList allJpgPaths = jpgPathsFromH5Paths(orderedH5Paths);
    QStringList missingH5s;
    QStringList missingJpgs;
    for (int i = 0; i < orderedH5Paths.size(); ++i) {
        if (!QFileInfo::exists(allJpgPaths[i])) {
            missingH5s.append(orderedH5Paths[i]);
            missingJpgs.append(allJpgPaths[i]);
        }
    }

    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
        setOutputData(1, m_imageInfoData);
    } else {
        // 缺失预览补齐前不输出子集，避免 H5 与 JPG 按索引错位
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
    }
    Q_EMIT dataUpdated(1);

    if (!missingH5s.isEmpty()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, allJpgPaths]() {
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

    syncProjectTreeOrder(orderedH5Paths, dstNode);
    syncProjectXmlOrder(orderedH5Paths, dstNode);

    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
    return true;
}

QStringList S1TopsBackGeocodingNode::previewImagePaths() const
{
    QStringList h5Paths;
    if (m_outputData && !m_outputData->filePaths().isEmpty()) {
        h5Paths = m_outputData->filePaths();
    } else if (!m_savedOutputPaths.isEmpty()) {
        QString dstNode = m_outputNodeName.trimmed();
        for (const QString& savedPath : m_savedOutputPaths) {
            QString resolvedPath = resolveSavedOutputPath(savedPath, dstNode);
            if (!resolvedPath.isEmpty()) {
                h5Paths.append(resolvedPath);
            }
        }
    }

    QStringList existingPaths;
    for (const QString& jpgPath : jpgPathsFromH5Paths(h5Paths)) {
        if (QFileInfo::exists(jpgPath)) {
            existingPaths.append(jpgPath);
        }
    }
    return existingPaths;
}

} // namespace QtNodes
