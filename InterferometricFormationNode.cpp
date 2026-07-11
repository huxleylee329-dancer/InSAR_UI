#include "InterferometricFormationNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
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
    setExecutionMode(ExecutionMode::Automatic);
}

InterferometricFormationNode::~InterferometricFormationNode()
{
    stopExecution();
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
            return NodeDataType{"imported_file", "DEM File"};
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
            return QStringLiteral("DEM ?");
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
    modelJson[QStringLiteral("demPath")] = m_demPath;

    return modelJson;
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

    QJsonValue vDemPath = json[QStringLiteral("demPath")];
    if (!vDemPath.isUndefined()) m_demPath = vDemPath.toString();

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
    m_demPathLabel = new QLabel("DEM路径");
    m_demPathLabel->setFixedWidth(100);
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
    Q_UNUSED(message);
    setProgress(progress);
}

void InterferometricFormationNode::onProcessingFinished()
{
    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    QString outputPath = projectPath() + "/" + dstNode + "/";

    // Scan output directory for H5 results and generate JPG previews
    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;

    QStandardItemModel* model = projectModel();
    if (model)
    {
        QList<QStandardItem*> foundProjects = model->findItems(projectName());
        if (!foundProjects.isEmpty())
        {
            QStandardItem* projectItem = foundProjects.first();
            QString srcNode = m_inputData->nodeName();
            int masterIndex = m_useDefaultMaster ? 0 : m_masterIndex;
            
            QStandardItem* srcNodeItem = nullptr;
            for (int i = 0; i < projectItem->rowCount(); ++i)
            {
                QStandardItem* nodeItem = projectItem->child(i, 0);
                if (nodeItem && nodeItem->text() == srcNode)
                {
                    srcNodeItem = nodeItem;
                    break;
                }
            }

            if (srcNodeItem && masterIndex >= 0 && masterIndex < srcNodeItem->rowCount())
            {
                QString master_path = srcNodeItem->child(masterIndex, 1)->text();
                QFileInfo master_fi(master_path);
                QString master_name = master_fi.baseName();

                for (int i = 0; i < srcNodeItem->rowCount(); ++i)
                {
                    if (i == masterIndex)
                        continue;

                    QStandardItem* slaveItem = srcNodeItem->child(i, 0);
                    if (slaveItem) {
                        QString slave_path = srcNodeItem->child(i, 1)->text();
                        QFileInfo slave_fi(slave_path);
                        QString slave_name = slave_fi.baseName();

                        QString h5_name = master_name + "_" + slave_name;
                        QString h5_path = outputPath + h5_name + ".h5";

                        QString phase_name = h5_name + "_phase";
                        QString phase_jpg = outputPath + phase_name + ".jpg";
                        h5Paths.append(h5_path);
                        jpgPaths.append(phase_jpg);
                        types.append("phase");

                        if (m_isCoherence)
                        {
                            QString coh_name = h5_name + "_coh";
                            QString coh_jpg = outputPath + coh_name + ".jpg";
                            h5Paths.append(h5_path);
                            jpgPaths.append(coh_jpg);
                            types.append("coherence");
                        }
                    }
                }
            }
        }
    }

    QStringList uniqueH5Paths = h5Paths;
    uniqueH5Paths.removeDuplicates();
    uniqueH5Paths.sort();

    if (uniqueH5Paths.isEmpty())
    {
        QDir dir(outputPath);
        if (dir.exists()) {
            QStringList filters;
            filters << "*.h5";
            QStringList h5Files = dir.entryList(filters, QDir::Files | QDir::NoSymLinks);
            h5Files.sort();
            for (const QString& h5File : h5Files) {
                uniqueH5Paths.append(dir.absoluteFilePath(h5File));
            }
        }
    }

    m_outputData = std::make_shared<ImportedFileData>(uniqueH5Paths, dstNode);
    setOutputData(0, m_outputData);

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

    if (!h5Paths.isEmpty())
    {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, jpgPaths]() {
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);

            // Update UI state
            updateParameterWidgetsEnableState();

            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("InterferometricFormationNode", "executeProcessing completed.");
            finishExecution();
            Q_EMIT dataUpdated(0);
        });

        QFuture<void> future = QtConcurrent::run([h5Paths, jpgPaths, types]() {
            for (int i = 0; i < h5Paths.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(h5Paths[i], jpgPaths[i], types[i]);
            }
        });
        m_remedyWatcher.setFuture(future);
    }
    else
    {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        Q_EMIT dataUpdated(1);

        // Update UI state
        updateParameterWidgetsEnableState();

        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("InterferometricFormationNode", "executeProcessing completed (empty output list).");
        finishExecution();
        Q_EMIT dataUpdated(0);
    }
}

void InterferometricFormationNode::onError(const QString& error)
{
    Q_UNUSED(error);
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

    updateParameterWidgetsEnableState();
    
    setState(ExecutionState::Error);
}

void InterferometricFormationNode::onModelUpdated(QStandardItemModel* model)
{
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
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
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
        setState(ExecutionState::Idle);
    }
}

bool InterferometricFormationNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    m_preparedDstNode = m_inputData->nodeName();
    m_preparedFileName = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();

    m_preparedMasterIndex = m_useDefaultMaster ? 0 : m_masterIndex;
    m_preparedIsDeflat = m_isDeflat;
    m_preparedIsTopoRemoval = m_isTopoRemoval;
    m_preparedIsCoherence = m_isCoherence;
    m_preparedDemPath = m_demPath;

    m_preparedWinW = m_winWEdit ? m_winWEdit->text().toInt() : m_winW;
    m_preparedWinH = m_winHEdit ? m_winHEdit->text().toInt() : m_winH;
    m_preparedMultilookRg = m_multilookRgEdit ? m_multilookRgEdit->text().toInt() : m_multilookRg;
    m_preparedMultilookAz = m_multilookAzEdit ? m_multilookAzEdit->text().toInt() : m_multilookAz;

    // Check files to see if they exist
    QStringList pathsToCheck;
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
                    if (m_preparedMasterIndex >= 0 && m_preparedMasterIndex < nodeItem->rowCount())
                    {
                        QString master_path = nodeItem->child(m_preparedMasterIndex, 1)->text();
                        QFileInfo master_fi(master_path);
                        QString master_name = master_fi.baseName();

                        for (int j = 0; j < nodeItem->rowCount(); ++j)
                        {
                            if (j == m_preparedMasterIndex)
                                continue;
                            QStandardItem* childItem = nodeItem->child(j, 0);
                            if (childItem) {
                                QString slave_path = nodeItem->child(j, 1)->text();
                                QFileInfo slave_fi(slave_path);
                                QString slave_name = slave_fi.baseName();

                                pathsToCheck.append(m_preparedSavePath + "/" + m_preparedFileName + "/" + master_name + "_" + slave_name + ".h5");
                            }
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
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), m_preparedFileName, pathsToCheck, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void InterferometricFormationNode::executeProcessing()
{
    InSARLogManager::LogInfo("InterferometricFormationNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedFileName;
        
        updateParameterWidgetsEnableState();

        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) {
            finishExecution();
        } else {
            setState(ExecutionState::Error);
        }
        return;
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite) {
        // Clean up old data nodes to prevent tree duplicates
        NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_preparedFileName);
    }

    setProgress(0);
    setState(ExecutionState::Running);

    m_thread = new QThread();
    m_workerThread = new InterferometricFormationWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &InterferometricFormationNode::startInterferometric, m_workerThread, &InterferometricFormationWorker::InterferometricWithDem);
    connect(m_thread, &QThread::started, [this]() {
        Q_EMIT startInterferometric(m_preparedIsDeflat, m_preparedIsTopoRemoval, m_preparedIsCoherence, 
                                    m_preparedMasterIndex, m_preparedWinW, m_preparedWinH,
                                    m_preparedMultilookRg, m_preparedMultilookAz, 
                                    m_preparedSavePath, m_preparedProjectName, 
                                    m_preparedDstNode, m_preparedFileName, projectModel(),
                                    m_preparedDemPath);
    });
    connect(m_workerThread, &InterferometricFormationWorker::updateProcess, this, &InterferometricFormationNode::onProgressUpdate);
    connect(m_workerThread, &InterferometricFormationWorker::endProcess, this, &InterferometricFormationNode::onProcessingFinished);
    connect(m_workerThread, &InterferometricFormationWorker::errorProcess, this, &InterferometricFormationNode::onError);
    connect(m_workerThread, &InterferometricFormationWorker::sendModel, this, &InterferometricFormationNode::onModelUpdated);
    connect(m_workerThread, &InterferometricFormationWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    m_thread->start();
    
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

bool InterferometricFormationNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (!dir.exists())
        return false;

    // Check if H5 outputs exist in the folder directly
    QStringList filters;
    filters << "*.h5";
    QStringList h5Files = dir.entryList(filters, QDir::Files);
    if (h5Files.isEmpty()) {
        return false;
    }

    QStringList h5Paths;
    QStringList expectedJpgPaths;
    QStringList types;

    for (const QString& h5File : h5Files) {
        QString h5Path = dir.absoluteFilePath(h5File);
        QFileInfo fi(h5Path);
        QString baseName = fi.baseName();

        // Phase output
        h5Paths.append(h5Path);
        expectedJpgPaths.append(outputPath + baseName + "_phase.jpg");
        types.append("phase");

        // Coherence output (if enabled)
        if (m_isCoherence)
        {
            h5Paths.append(h5Path);
            expectedJpgPaths.append(outputPath + baseName + "_coh.jpg");
            types.append("coherence");
        }
    }

    QStringList uniqueH5Paths;
    for (const QString& h5File : h5Files) {
        uniqueH5Paths.append(dir.absoluteFilePath(h5File));
    }
    uniqueH5Paths.sort();
    uniqueH5Paths.removeDuplicates();

    m_outputData = std::make_shared<ImportedFileData>(uniqueH5Paths, dstNode);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    // Background preview JPG generation check and fix
    QStringList existingJpgPaths;
    QStringList missingH5s;
    QStringList missingJpgs;
    QStringList missingTypes;

    int totalExpected = expectedJpgPaths.size();
    for (int i = 0; i < totalExpected; ++i) {
        QString jpgPath = expectedJpgPaths[i];
        if (QFile::exists(jpgPath)) {
            existingJpgPaths.append(jpgPath);
        } else {
            // Find corresponding h5 path
            // Note: Since each expected H5 is listed once for phase and once for coherence (if enabled),
            // the index in h5Paths maps to expectedJpgPaths based on construction.
            // Let's trace it: 
            // In construction: we append h5_path and type for phase, and then optionally h5_path and type for coh.
            // So expectedJpgPaths has length = h5Paths length.
            missingH5s.append(h5Paths[i]);
            missingJpgs.append(jpgPath);
            missingTypes.append(types[i]);
        }
    }

    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    } else {
        // Regenerate missing JPGs in the background
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, expectedJpgPaths]() {
            m_imageInfoData = std::make_shared<ImageInfoData>(expectedJpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
            InSARLogManager::LogInfo("InterferometricFormationNode", "validateAndRestoreOutput background rendering completed.");
        });

        QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs, missingTypes]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
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

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);
        for (const QString& h5File : h5Files) {
            QString baseName = QFileInfo(h5File).baseName();
            
            // Phase output preview
            QString phaseJpg = outputPath + baseName + "_phase.jpg";
            if (QFile::exists(phaseJpg)) {
                list.append(phaseJpg);
            }
            
            // Coherence output preview (if enabled)
            if (m_isCoherence) {
                QString cohJpg = outputPath + baseName + "_coh.jpg";
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
    if (m_demPathEdit) m_demPathEdit->setEnabled(demEnabled);
    if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(demEnabled);
}

} // namespace QtNodes
