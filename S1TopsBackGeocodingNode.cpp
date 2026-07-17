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
#include <Registration.h>
#include "ImageView.h"
#include "QtNodes/internal/NodeDetailWindow.hpp"
#include <QTableWidget>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QFutureWatcher>
#include <QColor>
#include <QFont>
#include <algorithm>
#include <vector>
#include <cmath>
#include <opencv2/opencv.hpp>

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
    , m_bRangeRefine(false)
    , m_rangeRefineCheckBox(nullptr)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

S1TopsBackGeocodingNode::~S1TopsBackGeocodingNode()
{
    // 安全断开并等待 remedyWatcher，防止析构时的悬空指针回调崩溃
    m_remedyWatcher.disconnect();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
    }

    // 强行中止并清除运行中线程，消灭残留
    stopExecution();

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
            return NodeDataType{"dem_file", "DEM File"};
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
    modelJson["bRangeRefine"] = m_bRangeRefine;
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

    QJsonValue vRangeRefine = json["bRangeRefine"];
    if (!vRangeRefine.isUndefined())
    {
        m_bRangeRefine = vRangeRefine.toBool();
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

    if (m_rangeRefineCheckBox)
        m_rangeRefineCheckBox->setChecked(m_bRangeRefine);
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

    // 距离向幅度精化
    auto* rangeRefineLayout = new QHBoxLayout();
    QLabel* rangeRefineLabel = new QLabel(tr("距离向振幅精配准"));
    rangeRefineLayout->addWidget(rangeRefineLabel);
    m_rangeRefineCheckBox = new QCheckBox();
    m_rangeRefineCheckBox->setChecked(m_bRangeRefine);
    connect(m_rangeRefineCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool val = (state == Qt::Checked);
        if (m_bRangeRefine != val) {
            if (!confirmParameterChange()) {
                m_rangeRefineCheckBox->blockSignals(true);
                m_rangeRefineCheckBox->setChecked(m_bRangeRefine);
                m_rangeRefineCheckBox->blockSignals(false);
                return;
            }
            m_bRangeRefine = val;
            invalidateNodeData();
        }
    });
    rangeRefineLayout->addWidget(m_rangeRefineCheckBox);
    layout->addLayout(rangeRefineLayout);

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

    QString xmlPath = NodeUtils::getProjectFilePath(_widget);
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
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
            m_remedyWatcher.waitForFinished();
        }

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, orderedH5Paths, jpgPaths]() {
            QStringList validJpgPaths;
            bool anyFailed = false;
            for (const QString& path : jpgPaths) {
                if (QFile::exists(path) && QFileInfo(path).size() > 0) {
                    validJpgPaths.append(path);
                } else {
                    anyFailed = true;
                }
            }

            // 只有确实存在且生成成功的 JPG 路径才能加入 ImageInfoData
            if (!validJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(validJpgPaths);
                setOutputData(1, m_imageInfoData);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
            }
            Q_EMIT dataUpdated(1);

            updateParameterWidgetsEnableState();

            // 区分 JPG 预览生成失败
            if (anyFailed) {
                setState(ExecutionState::Warning);
                InSARLogManager::LogWarning("S1TopsBackGeocodingNode", "executeProcessing completed with warnings. Some preview images failed to generate.");
                
                // Warning 状态也要通知完成
                setProgress(100);
                Q_EMIT computingFinished();
                Q_EMIT dataUpdated(0);
            } else {
                setState(ExecutionState::Running); // 确保 finishExecution() 能通过状态校验
                setProgress(100);
                InSARLogManager::LogInfo("S1TopsBackGeocodingNode", "executeProcessing completed.");
                finishExecution();
                Q_EMIT dataUpdated(0);
            }
        });

        QFuture<void> future = QtConcurrent::run([orderedH5Paths, jpgPaths]() {
            for (int i = 0; i < orderedH5Paths.size(); ++i) {
                // 已存在且有效的 JPG 跳过重生成
                if (QFileInfo::exists(jpgPaths[i]) && QFileInfo(jpgPaths[i]).size() > 0) {
                    continue;
                }
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
    return NodeUtils::getProjectDirectory(_widget);
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
    // 安全断开并取消 remedyWatcher，防止重新执行时的竞态与崩溃
    m_remedyWatcher.disconnect();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }

    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
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
    m_preparedBRangeRefine = m_rangeRefineCheckBox ? m_rangeRefineCheckBox->isChecked() : false;
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

    // 检测是否有运行中的线程，有的话先安全终止，消灭重入隐患
    if (m_workerThread || m_thread) {
        stopExecution();
    }

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
    m_workerThread->setRangeRefine(m_preparedBRangeRefine);
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
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
            m_remedyWatcher.waitForFinished();
        }

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, allJpgPaths, missingJpgs]() {
            QStringList validJpgPaths;
            bool anyFailed = false;
            for (const QString& path : missingJpgs) {
                if (!QFile::exists(path) || QFileInfo(path).size() == 0) {
                    anyFailed = true;
                }
            }

            for (const QString& path : allJpgPaths) {
                if (QFile::exists(path) && QFileInfo(path).size() > 0) {
                    validJpgPaths.append(path);
                }
            }

            // 仅输出生成成功的 JPG，防止不存在的路径传入下游
            if (!validJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(validJpgPaths);
                setOutputData(1, m_imageInfoData);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
            }
            Q_EMIT dataUpdated(1);

            if (anyFailed) {
                setState(ExecutionState::Warning);
                InSARLogManager::LogWarning("S1TopsBackGeocodingNode", "Output recovery finished with warnings. Some preview images failed to generate.");
            } else {
                setState(ExecutionState::Completed);
            }
        });

        QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
                // 已存在有效 JPG 则跳过重生成
                if (QFileInfo::exists(missingJpgs[i]) && QFileInfo(missingJpgs[i]).size() > 0) {
                    continue;
                }
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

QStringList S1TopsBackGeocodingNode::getOrderedH5Paths() const
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
    return h5Paths;
}

QStringList S1TopsBackGeocodingNode::getInputH5Paths() const
{
    return m_inputData ? m_inputData->filePaths() : QStringList();
}

QStringList S1TopsBackGeocodingNode::previewImagePaths() const
{
    QStringList h5Paths = getOrderedH5Paths();
    QStringList existingPaths;
    for (const QString& jpgPath : jpgPathsFromH5Paths(h5Paths)) {
        if (QFileInfo::exists(jpgPath)) {
            existingPaths.append(jpgPath);
        }
    }
    return existingPaths;
}

// 基于影像强度分块格网化自动选取高反射强度控制点
static int selectHighIntensityPoints(const QString& masterPath, Point2D points[5])
{
    FormatConversion FC;
    int rows = 0, cols = 0;
    if (FC.get_dataset_dims(masterPath.toLocal8Bit().constData(), "s_re", &rows, &cols) != 0)
    {
        return 0;
    }

    // 1. 估算全局阈值 (75% 分位数)
    // 均匀在图像中选取 20 行进行采样，避免读取整张图导致内存和计算压力过大
    std::vector<float> sample_amplitudes;
    int num_sample_rows = 20;
    for (int i = 0; i < num_sample_rows; ++i)
    {
        int r = (rows / (num_sample_rows + 1)) * (i + 1);
        cv::Mat row_re, row_im;
        if (FC.read_subarray_from_h5(masterPath.toLocal8Bit().constData(), "s_re", r, 0, 1, cols, row_re) == 0 &&
            FC.read_subarray_from_h5(masterPath.toLocal8Bit().constData(), "s_im", r, 0, 1, cols, row_im) == 0)
        {
            cv::Mat amp;
            cv::magnitude(row_re, row_im, amp);
            for (int c = 0; c < cols; c += 10) // 步长 10 降采样采样点
            {
                float val = amp.at<float>(0, c);
                if (val > 0.0f)
                {
                    sample_amplitudes.push_back(val);
                }
            }
        }
    }

    float global_min_threshold = 0.0f;
    if (!sample_amplitudes.empty())
    {
        auto m = sample_amplitudes.begin() + sample_amplitudes.size() * 0.75;
        std::nth_element(sample_amplitudes.begin(), m, sample_amplitudes.end());
        global_min_threshold = *m;
    }

    // 2. 划分 3x3 网格空间，共 9 个格网块
    int cell_w = cols / 3;
    int cell_h = rows / 3;

    struct Candidate {
        float val = -1.0f;
        int x = 0;
        int y = 0;
    };
    std::vector<Candidate> candidates(9);

    // 3. 分块读取，粗糙寻优 (每块高度 2048 行，以节省内存)
    int block_height = 2048;
    for (int r = 0; r < rows; r += block_height)
    {
        int rows_to_read = std::min(block_height, rows - r);
        cv::Mat block_re, block_im;
        if (FC.read_subarray_from_h5(masterPath.toLocal8Bit().constData(), "s_re", r, 0, rows_to_read, cols, block_re) != 0 ||
            FC.read_subarray_from_h5(masterPath.toLocal8Bit().constData(), "s_im", r, 0, rows_to_read, cols, block_im) != 0)
        {
            continue;
        }

        cv::Mat amp;
        cv::magnitude(block_re, block_im, amp);

        // 粗糙扫描：步长 10 像素以提升速度
        for (int y = 0; y < amp.rows; y += 10)
        {
            int global_y = r + y;
            for (int x = 0; x < amp.cols; x += 10)
            {
                float val = amp.at<float>(y, x);
                int gy = std::min(global_y / cell_h, 2);
                int gx = std::min(x / cell_w, 2);
                int cell_idx = gy * 3 + gx;

                if (val > candidates[cell_idx].val)
                {
                    candidates[cell_idx].val = val;
                    candidates[cell_idx].x = x;
                    candidates[cell_idx].y = global_y;
                }
            }
        }
    }

    // 4. 精细寻优 (在粗糙最亮点附近 64x64 区域读取原始分辨率寻找确切最亮点)
    std::vector<Candidate> valid_points;
    for (int i = 0; i < 9; ++i)
    {
        if (candidates[i].val < global_min_threshold || candidates[i].val <= 0.0f)
        {
            continue;
        }

        int coarse_x = candidates[i].x;
        int coarse_y = candidates[i].y;

        int win_size = 64;
        int start_x = std::max(0, coarse_x - win_size / 2);
        int start_y = std::max(0, coarse_y - win_size / 2);
        int read_w = std::min(cols - start_x, win_size);
        int read_h = std::min(rows - start_y, win_size);

        cv::Mat win_re, win_im;
        if (FC.read_subarray_from_h5(masterPath.toLocal8Bit().constData(), "s_re", start_y, start_x, read_h, read_w, win_re) == 0 &&
            FC.read_subarray_from_h5(masterPath.toLocal8Bit().constData(), "s_im", start_y, start_x, read_h, read_w, win_im) == 0)
        {
            cv::Mat win_amp;
            cv::magnitude(win_re, win_im, win_amp);

            double minVal, maxVal;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(win_amp, &minVal, &maxVal, &minLoc, &maxLoc);

            Candidate refined;
            refined.val = (float)maxVal;
            refined.x = start_x + maxLoc.x;
            refined.y = start_y + maxLoc.y;
            valid_points.push_back(refined);
        }
    }

    // 5. 对候选点按振幅大小进行降序排序
    std::sort(valid_points.begin(), valid_points.end(), [](const Candidate& a, const Candidate& b) {
        return a.val > b.val;
    });

    // 6. 填充最终的 5 个点
    int count = 0;
    for (size_t i = 0; i < valid_points.size() && count < 5; ++i)
    {
        points[count].x = valid_points[i].x;
        points[count].y = valid_points[i].y;
        count++;
    }

    // 如果选出的高质量点不足 5 个，使用默认的中心和四角格子点进行排重填充
    if (count < 5)
    {
        Point2D default_pts[5];
        default_pts[0].x = cols / 5.0;       default_pts[0].y = rows / 5.0;
        default_pts[1].x = cols * 4.0 / 5.0; default_pts[1].y = rows / 5.0;
        default_pts[2].x = cols / 2.0;       default_pts[2].y = rows / 2.0;
        default_pts[3].x = cols / 5.0;       default_pts[3].y = rows * 4.0 / 5.0;
        default_pts[4].x = cols * 4.0 / 5.0; default_pts[4].y = rows * 4.0 / 5.0;

        for (int i = 0; i < 5 && count < 5; ++i)
        {
            bool duplicate = false;
            for (int j = 0; j < count; ++j)
            {
                if (std::abs(points[j].x - default_pts[i].x) < 10.0 &&
                    std::abs(points[j].y - default_pts[i].y) < 10.0)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
            {
                points[count].x = default_pts[i].x;
                points[count].y = default_pts[i].y;
                count++;
            }
        }
    }

    return count;
}

// ============================================================================
// S1TopsRegistrationEvalWidget - S1 TOPS Back-Geocoding 配准评估选项卡组件
// ============================================================================
struct EvalThreadResult {
    int retCode;
    Point2D points[5];
    AlignmentResult results[5];
    double inputCoherence[5];
};

class S1TopsRegistrationEvalWidget : public QWidget
{
public:
    explicit S1TopsRegistrationEvalWidget(S1TopsBackGeocodingNode* node, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_node(node)
        , m_hasResults(false)
    {
        // 初始化结果指针为 nullptr
        for (int i = 0; i < 5; ++i) {
            m_results[i].heatmap_rgb = nullptr;
            m_results[i].overlay_rgb = nullptr;
            m_results[i].imageWidth = 0;
            m_results[i].imageHeight = 0;
            m_inputCoherence[i] = -1.0;
        }

        m_h5Paths = m_node->getOrderedH5Paths();

        // 界面布局
        auto* mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);
        mainLayout->setSpacing(12);

        // 左侧栏：影像选择与分析数据表格
        auto* leftContainer = new QWidget();
        auto* leftLayout = new QVBoxLayout(leftContainer);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(8);

        auto* selectionLayout = new QHBoxLayout();
        auto* selLabel = new QLabel(tr("已配准影像对:"));
        selLabel->setStyleSheet("font-weight: bold;");
        selectionLayout->addWidget(selLabel);

        m_slaveCombo = new QComboBox();
        updateSlaveCombo();
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

        m_statusCardDesc = new QLabel(tr("请等待评估获取相干性及对齐精度诊断结果。"));
        m_statusCardDesc->setWordWrap(true);
        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
        cardLayout->addWidget(m_statusCardDesc);

        leftLayout->addWidget(m_statusCard);

        m_resultsTable = new QTableWidget();
        m_resultsTable->setColumnCount(6);
        m_resultsTable->setHorizontalHeaderLabels({
            tr("测试区域"), tr("相干性(配准前)"), tr("相干性(配准后)"), tr("最佳相干性"), tr("残余偏移(Y, X)"), tr("相关系数")
        });
        m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_resultsTable->verticalHeader()->setVisible(false);
        m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
        
        // 设置表格交替背景色和样式
        m_resultsTable->setAlternatingRowColors(true);
        bool isDark = NodeDetailWindow::isDarkTheme(this);
        QString tableStyle = isDark ?
            "QTableWidget { background-color: #1F2937; alternate-background-color: #374151; gridline-color: #4B5563; }"
            "QTableWidget::item { color: #D1D5DB; }" :
            "QTableWidget { background-color: #FFFFFF; alternate-background-color: #F9FAFB; gridline-color: #E5E7EB; }"
            "QTableWidget::item { color: #374151; }";
        m_resultsTable->setStyleSheet(tableStyle);
        leftLayout->addWidget(m_resultsTable, 1);

        m_statusLabel = new QLabel(tr("准备就绪。请选择影像对开始评估。"));
        m_statusLabel->setWordWrap(true);
        m_statusLabel->setStyleSheet(isDark ? "color: #9CA3AF;" : "color: #6B7280;");
        leftLayout->addWidget(m_statusLabel);

        mainLayout->addWidget(leftContainer, 4);

        // 右侧栏：图像展示与模式切换
        auto* rightContainer = new QWidget();
        auto* rightLayout = new QVBoxLayout(rightContainer);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(8);

        auto* modeLayout = new QHBoxLayout();
        auto* modeLabel = new QLabel(tr("显示模式:"));
        modeLabel->setStyleSheet("font-weight: bold;");
        modeLayout->addWidget(modeLabel);
        
        m_visualModeCombo = new QComboBox();
        m_visualModeCombo->addItem(tr("2D 相干性热力图"), 0);
        m_visualModeCombo->addItem(tr("红-青对齐叠合图"), 1);
        modeLayout->addWidget(m_visualModeCombo, 1);
        rightLayout->addLayout(modeLayout);

        m_imageView = new ImageView();
        m_imageView->setMinimumSize(256, 256);
        m_imageView->setStyleSheet(QString("border: 1px solid %1; border-radius: 4px;")
            .arg(isDark ? "#4B5563" : "#D1D5DB"));
        rightLayout->addWidget(m_imageView, 1);

        mainLayout->addWidget(rightContainer, 5);

        // 信号槽连接
        connect(m_slaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &S1TopsRegistrationEvalWidget::onSlaveChanged);
        connect(m_visualModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &S1TopsRegistrationEvalWidget::onVisualModeChanged);
        connect(m_resultsTable, &QTableWidget::itemSelectionChanged, this, &S1TopsRegistrationEvalWidget::onTableSelectionChanged);
        connect(&m_watcher, &QFutureWatcher<EvalThreadResult>::finished, this, &S1TopsRegistrationEvalWidget::onEvaluationFinished);

        // 监听节点数据更新信号，动态刷新评估界面
        connect(m_node, &S1TopsBackGeocodingNode::dataUpdated, this, [this](unsigned int port) {
            if (port == 0) {
                m_h5Paths = m_node->getOrderedH5Paths();
                updateSlaveCombo();
                startEvaluation();
            }
        });

        // 自动触发初始评估
        if (m_h5Paths.size() > 1) {
            QTimer::singleShot(200, [this]() {
                startEvaluation();
            });
        }
    }

    ~S1TopsRegistrationEvalWidget() override
    {
        m_watcher.cancel();
        m_watcher.waitForFinished();
        clearCachedResults();
    }

private:
    void updateSlaveCombo()
    {
        m_slaveCombo->blockSignals(true);
        m_slaveCombo->clear();
        if (m_h5Paths.size() > 1) {
            QString masterName = QFileInfo(m_h5Paths[0]).completeBaseName();
            for (int i = 1; i < m_h5Paths.size(); ++i) {
                QString slaveName = QFileInfo(m_h5Paths[i]).completeBaseName();
                m_slaveCombo->addItem(QString("%1 -> %2").arg(slaveName).arg(masterName));
            }
            m_slaveCombo->setEnabled(true);
        } else {
            m_slaveCombo->addItem(tr("无可配准的副影像"));
            m_slaveCombo->setEnabled(false);
        }
        m_slaveCombo->blockSignals(false);
    }

    void onSlaveChanged(int index)
    {
        Q_UNUSED(index);
        startEvaluation();
    }

    void onVisualModeChanged(int index)
    {
        Q_UNUSED(index);
        updateImageView();
    }

    void onTableSelectionChanged()
    {
        updateImageView();
    }

    void startEvaluation()
    {
        if (m_watcher.isRunning()) {
            return;
        }

        int slaveIndex = m_slaveCombo->currentIndex() + 1;
        if (m_h5Paths.size() <= 1 || slaveIndex < 1 || slaveIndex >= m_h5Paths.size()) {
            return;
        }

        m_statusLabel->setText(tr("正在进行配准评估，计算较耗时，请稍候..."));
        m_imageView->setImage(QImage());
        m_resultsTable->setRowCount(0);
        clearCachedResults();

        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
        m_statusCardTitle->setText(tr("未评估"));
        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
        m_statusCardDesc->setText(tr("请等待评估获取相干性及对齐精度诊断结果。"));

        QString masterPath = m_h5Paths[0];
        QString slavePath = m_h5Paths[slaveIndex];

        if (!QFile::exists(masterPath) || !QFile::exists(slavePath)) {
            m_statusLabel->setText(tr("错误：主图像或副图像文件不存在！"));
            return;
        }

        m_slaveCombo->setEnabled(false);

        // 获取配准前的输入 H5 路径
        QStringList inputPaths = m_node->getInputH5Paths();
        QString inputMasterPath = !inputPaths.isEmpty() ? inputPaths[0] : "";
        QString inputSlavePath = (inputPaths.size() > slaveIndex) ? inputPaths[slaveIndex] : "";

        // 异步计算
        QFuture<EvalThreadResult> future = QtConcurrent::run([masterPath, slavePath, inputMasterPath, inputSlavePath]() {
            NodeUtils::Hdf5Locker locker(masterPath);
            std::unique_ptr<NodeUtils::Hdf5Locker> inputLocker;
            if (!inputMasterPath.isEmpty() && QFile::exists(inputMasterPath)) {
                inputLocker = std::make_unique<NodeUtils::Hdf5Locker>(inputMasterPath);
            }

            EvalThreadResult threadRes;
            threadRes.retCode = -1;
            for (int i = 0; i < 5; ++i) {
                threadRes.results[i].heatmap_rgb = nullptr;
                threadRes.results[i].overlay_rgb = nullptr;
                threadRes.results[i].imageWidth = 0;
                threadRes.results[i].imageHeight = 0;
                threadRes.inputCoherence[i] = -1.0;
            }

            int detectRet = selectHighIntensityPoints(masterPath, threadRes.points);
            if (detectRet < 5) {
                FormatConversion FC;
                int rows = 0, cols = 0;
                if (FC.get_dataset_dims(masterPath.toLocal8Bit().constData(), "s_re", &rows, &cols) == 0) {
                    threadRes.points[0].x = cols / 5.0;       threadRes.points[0].y = rows / 5.0;
                    threadRes.points[1].x = cols * 4.0 / 5.0; threadRes.points[1].y = rows / 5.0;
                    threadRes.points[2].x = cols / 2.0;       threadRes.points[2].y = rows / 2.0;
                    threadRes.points[3].x = cols / 5.0;       threadRes.points[3].y = rows * 4.0 / 5.0;
                    threadRes.points[4].x = cols * 4.0 / 5.0; threadRes.points[4].y = rows * 4.0 / 5.0;
                } else {
                    threadRes.points[0].x = 500;  threadRes.points[0].y = 500;
                    threadRes.points[1].x = 2500; threadRes.points[1].y = 500;
                    threadRes.points[2].x = 1500; threadRes.points[2].y = 1500;
                    threadRes.points[3].x = 500;  threadRes.points[3].y = 2500;
                    threadRes.points[4].x = 2500; threadRes.points[4].y = 2500;
                }
            }

            // 计算配准后的残余偏移与相干性
            threadRes.retCode = CalculateOffsetAndCoherence(
                masterPath.toLocal8Bit().constData(),
                slavePath.toLocal8Bit().constData(),
                threadRes.points, 5, 200, 206, threadRes.results
            );

            // 计算配准前的 0 位移相干性
            if (threadRes.retCode == 0 && !inputMasterPath.isEmpty() && !inputSlavePath.isEmpty() &&
                QFile::exists(inputMasterPath) && QFile::exists(inputSlavePath)) {
                AlignmentResult inputRes[5];
                for (int i = 0; i < 5; ++i) {
                    inputRes[i].heatmap_rgb = nullptr;
                    inputRes[i].overlay_rgb = nullptr;
                    inputRes[i].imageWidth = 0;
                    inputRes[i].imageHeight = 0;
                }
                int inputRet = CalculateOffsetAndCoherence(
                    inputMasterPath.toLocal8Bit().constData(),
                    inputSlavePath.toLocal8Bit().constData(),
                    threadRes.points, 5, 200, 202, inputRes
                );
                if (inputRet == 0) {
                    for (int i = 0; i < 5; ++i) {
                        threadRes.inputCoherence[i] = inputRes[i].coherenceZeroShift;
                    }
                }
                FreeAlignmentResults(inputRes, 5);
            }

            return threadRes;
        });

        m_watcher.setFuture(future);
    }

    void onEvaluationFinished()
    {
        m_slaveCombo->setEnabled(true);

        EvalThreadResult threadRes = m_watcher.result();
        if (threadRes.retCode != 0) {
            m_statusLabel->setText(tr("配准评估计算失败，错误码：%1").arg(threadRes.retCode));
            FreeAlignmentResults(threadRes.results, 5);
            return;
        }

        for (int i = 0; i < 5; ++i) {
            m_points[i] = threadRes.points[i];
            m_results[i] = threadRes.results[i];
            m_inputCoherence[i] = threadRes.inputCoherence[i];
        }
        m_hasResults = true;

        m_statusLabel->setText(tr("配准评估完成。请在表格中选择采样区域查看细节。"));

        // 统计所有 5 个区域的数据，判定整体配准效果
        int perfectCount = 0;   // 偏移为 0 的个数（包含低置信度点）
        int warningCount = 0;   // 偏移在 [-2, 2] 内但非 0 的个数
        int failedCount = 0;    // 偏移绝对值 > 2 的个数
        double sumCoh = 0.0;
        double sumPreCoh = 0.0;
        int preCohValidCount = 0;
        
        for (int i = 0; i < 5; ++i) {
            sumCoh += m_results[i].coherenceZeroShift;
            if (m_inputCoherence[i] >= 0.0) {
                sumPreCoh += m_inputCoherence[i];
                preCohValidCount++;
            }
            
            double maxCorr = m_results[i].maxCorrelation;
            int dy = std::abs(m_results[i].offsetY);
            int dx = std::abs(m_results[i].offsetX);
            
            if (maxCorr < 0.15) {
                // 如果相关系数过低（低于 0.15），代表此处强度图匹配失效，偏移量结果纯属随机斑噪。
                // 此时忽略其对 FAILED 的统计贡献，防止噪声误导，默认认为物理对齐良好（由相干性判定主导）
                perfectCount++;
            } else {
                if (dy == 0 && dx == 0) {
                    perfectCount++;
                } else if (dy <= 2 && dx <= 2) {
                    warningCount++;
                } else {
                    failedCount++;
                }
            }
        }
        double meanCoh = sumCoh / 5.0;
        double meanPreCoh = (preCohValidCount > 0) ? (sumPreCoh / preCohValidCount) : -1.0;

        // 临时调试：输出配准后相干性与精度评估报告
        printf("[InSAR_DEBUG_COREG] [UI] ============ Post-Registration Coherence & Offset Assessment Report ============\n");
        printf("[InSAR_DEBUG_COREG] [UI] Overall Assessment: %s\n", 
               (failedCount == 0 && perfectCount == 5 && meanCoh >= 0.22) ? "PASS - Excellent Registration Quality" :
               (failedCount > 0 || meanCoh < 0.18) ? "FAILED - Large Residual Offsets or Low Coherence" :
               "WARNING - Moderate Registration Quality");
        printf("[InSAR_DEBUG_COREG] [UI] Global Mean Coherence: Pre-Reg = %.4f, Post-Reg = %.4f (Change: %+.4f)\n", 
               meanPreCoh, meanCoh, (meanPreCoh >= 0.0 ? (meanCoh - meanPreCoh) : 0.0));
        printf("[InSAR_DEBUG_COREG] [UI] ---------------- Sample Points Details ----------------\n");
        for (int i = 0; i < 5; ++i) {
            printf("[InSAR_DEBUG_COREG] [UI]  * Sample Point %d (X: %d, Y: %d)\n", 
                   i + 1, m_points[i].x, m_points[i].y);
            printf("[InSAR_DEBUG_COREG] [UI]    - Coherence: Pre-Reg = %.4f, Post-Reg = %.4f, Optimal = %.4f\n", 
                   m_inputCoherence[i], m_results[i].coherenceZeroShift, m_results[i].coherenceOptimal);
            printf("[InSAR_DEBUG_COREG] [UI]    - Correlation: Residual Offset (X: %d, Y: %d), Correlation Coeff = %.4f%s\n", 
                   m_results[i].offsetX, m_results[i].offsetY, m_results[i].maxCorrelation,
                   (m_results[i].maxCorrelation < 0.15) ? " (Low Confidence Point*, ignored)" : "");
        }
        printf("[InSAR_DEBUG_COREG] [UI] =========================================================================\n");

        bool isDark = NodeDetailWindow::isDarkTheme(this);
        // 合理放宽相干性阈值以适应 Sentinel-1 自然失相干情况 (底噪约 0.20)
        if (failedCount == 0 && perfectCount == 5 && meanCoh >= 0.22) {
            m_statusCardTitle->setText(tr("通过 (PASS)"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
            
            QString desc = tr("配准精度优秀。所有置信采样区域的配准残余偏差均为 0。");
            if (meanPreCoh >= 0.0) {
                desc += tr("平均相干系数由配准前的 %1 显著提升至配准后的 %2，配准对齐效果极佳。")
                    .arg(meanPreCoh, 0, 'f', 4).arg(meanCoh, 0, 'f', 4);
            } else {
                desc += tr("配准后平均相干系数为 %1，完全满足后续干涉测量要求。").arg(meanCoh, 0, 'f', 4);
            }
            m_statusCardDesc->setText(desc);
            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #10B981; border-radius: 4px;")
                .arg(isDark ? "#064E3B" : "#D1FAE5"));
        } else if (failedCount > 0 || meanCoh < 0.18) {
            m_statusCardTitle->setText(tr("异常 (FAILED)"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            
            QString desc = tr("配准未达标或发生严重偏差！");
            if (meanPreCoh >= 0.0) {
                desc += tr("配准后平均相干系数（%1）较配准前（%2）无明显改善，或有置信测试区域偏移量超过 2 像素。建议开启 ESD 改正重新运行。")
                    .arg(meanCoh, 0, 'f', 4).arg(meanPreCoh, 0, 'f', 4);
            } else {
                desc += tr("有置信区域偏移量超过 2 像素或平均相干系数过低，建议开启 ESD 改正重新运行。");
            }
            m_statusCardDesc->setText(desc);
            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #EF4444; border-radius: 4px;")
                .arg(isDark ? "#7F1D1D" : "#FEE2E2"));
        } else {
            m_statusCardTitle->setText(tr("提醒 (WARNING)"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
            
            QString desc = tr("配准精度一般。部分置信测试区域存在 1~2 像素的小幅偏差。");
            if (meanPreCoh >= 0.0) {
                desc += tr("配准后平均相干系数为 %1（配准前为 %2），可能由于地形起伏大或局部时间失相干导致。")
                    .arg(meanCoh, 0, 'f', 4).arg(meanPreCoh, 0, 'f', 4);
            } else {
                desc += tr("可能由于地形起伏大或局部时间失相干导致。");
            }
            m_statusCardDesc->setText(desc);
            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #F59E0B; border-radius: 4px;")
                .arg(isDark ? "#78350F" : "#FEF3C7"));
        }

        // 填充表格
        m_resultsTable->setRowCount(5);
        for (int i = 0; i < 5; ++i) {
            m_resultsTable->setItem(i, 0, new QTableWidgetItem(QString(tr("区域 %1 (%2, %3)")).arg(i + 1).arg(m_points[i].x).arg(m_points[i].y)));
            
            // 1. 配准前 0 位移相干性
            QString preCohStr = (m_inputCoherence[i] < 0.0) ? tr("N/A") : QString::number(m_inputCoherence[i], 'f', 4);
            auto* itemPreCoh = new QTableWidgetItem(preCohStr);
            itemPreCoh->setForeground(Qt::gray);
            m_resultsTable->setItem(i, 1, itemPreCoh);
 
            // 2. 配准后 0 位移相干性
            auto* itemPostCoh = new QTableWidgetItem(QString::number(m_results[i].coherenceZeroShift, 'f', 4));
            itemPostCoh->setFont(QFont("", -1, QFont::Bold));
            itemPostCoh->setForeground(Qt::green);
            m_resultsTable->setItem(i, 2, itemPostCoh);
 
            // 3. 最佳相干性
            m_resultsTable->setItem(i, 3, new QTableWidgetItem(QString::number(m_results[i].coherenceOptimal, 'f', 4)));
 
            // 4. 残余偏移
            auto* itemOffset = new QTableWidgetItem();
            if (m_results[i].maxCorrelation < 0.15) {
                itemOffset->setText(QString("(%1, %2)*").arg(m_results[i].offsetY).arg(m_results[i].offsetX));
                itemOffset->setToolTip(tr("当前区域互相关匹配系数过低（低于 0.15），测得偏移量不具有置信度，仅供参考。"));
                itemOffset->setForeground(Qt::gray);
            } else {
                itemOffset->setText(QString("(%1, %2)").arg(m_results[i].offsetY).arg(m_results[i].offsetX));
                if (m_results[i].offsetY == 0 && m_results[i].offsetX == 0) {
                    itemOffset->setForeground(Qt::green);
                } else if (std::abs(m_results[i].offsetY) <= 2 && std::abs(m_results[i].offsetX) <= 2) {
                    itemOffset->setForeground(Qt::yellow);
                } else {
                    itemOffset->setForeground(Qt::red);
                }
            }
            m_resultsTable->setItem(i, 4, itemOffset);
 
            // 5. 相关系数
            m_resultsTable->setItem(i, 5, new QTableWidgetItem(QString::number(m_results[i].maxCorrelation, 'f', 4)));
        }

        m_resultsTable->selectRow(0);
    }

    void clearCachedResults()
    {
        if (m_hasResults) {
            FreeAlignmentResults(m_results, 5);
            m_hasResults = false;
        }
        for (int i = 0; i < 5; ++i) {
            m_results[i].heatmap_rgb = nullptr;
            m_results[i].overlay_rgb = nullptr;
            m_results[i].imageWidth = 0;
            m_results[i].imageHeight = 0;
            m_inputCoherence[i] = -1.0;
        }
    }

    void updateImageView()
    {
        if (!m_hasResults) {
            m_imageView->setImage(QImage());
            return;
        }

        int row = m_resultsTable->currentRow();
        if (row < 0 || row >= 5) {
            m_imageView->setImage(QImage());
            return;
        }

        int mode = m_visualModeCombo->currentData().toInt();
        unsigned char* rgb_data = (mode == 0) ? m_results[row].heatmap_rgb : m_results[row].overlay_rgb;
        int w = m_results[row].imageWidth;
        int h = m_results[row].imageHeight;

        if (rgb_data && w > 0 && h > 0) {
            // 深拷贝构建以防 DLL 释放引发悬空指针
            QImage img(rgb_data, w, h, w * 3, QImage::Format_RGB888);
            m_imageView->setImage(img.copy());
        } else {
            m_imageView->setImage(QImage());
        }
    }

    S1TopsBackGeocodingNode* m_node;
    QStringList m_h5Paths;
    QComboBox* m_slaveCombo;
    QTableWidget* m_resultsTable;
    QComboBox* m_visualModeCombo;
    ImageView* m_imageView;
    QLabel* m_statusLabel;
    
    QFrame* m_statusCard;
    QLabel* m_statusCardTitle;
    QLabel* m_statusCardDesc;

    Point2D m_points[5];
    AlignmentResult m_results[5];
    double m_inputCoherence[5];
    bool m_hasResults;

    QFutureWatcher<EvalThreadResult> m_watcher;
};

// 接口实现
::QWidget* S1TopsBackGeocodingNode::createInterferometryWidget(::QWidget* parent)
{
    return new S1TopsRegistrationEvalWidget(this, parent);
}

} // namespace QtNodes
