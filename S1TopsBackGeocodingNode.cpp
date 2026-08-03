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
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonArray>
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QDateTime>
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
#include <QImage>
#include <QImageReader>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <vector>
#include <cmath>
#include <opencv2/opencv.hpp>

namespace QtNodes {

namespace {

bool generateRegistrationOverviewPreview(const QString& masterJpgPath,
                                         const QString& slaveJpgPath,
                                         const QString& overviewJpgPath)
{
    QImage master(masterJpgPath);
    QImage slave(slaveJpgPath);
    if (master.isNull() || slave.isNull()) {
        return false;
    }

    const int maxDimension = 1600;
    QSize targetSize = master.size();
    if (targetSize.width() > maxDimension || targetSize.height() > maxDimension) {
        targetSize.scale(maxDimension, maxDimension, Qt::KeepAspectRatio);
    }

    master = master.convertToFormat(QImage::Format_ARGB32).scaled(
        targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    slave = slave.convertToFormat(QImage::Format_ARGB32).scaled(
        targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QImage overview(targetSize, QImage::Format_ARGB32);
    for (int y = 0; y < targetSize.height(); ++y) {
        const QRgb* masterLine = reinterpret_cast<const QRgb*>(master.constScanLine(y));
        const QRgb* slaveLine = reinterpret_cast<const QRgb*>(slave.constScanLine(y));
        QRgb* overviewLine = reinterpret_cast<QRgb*>(overview.scanLine(y));
        for (int x = 0; x < targetSize.width(); ++x) {
            const int masterGray = qGray(masterLine[x]);
            const int slaveGray = qGray(slaveLine[x]);
            overviewLine[x] = qRgba(masterGray, slaveGray, slaveGray, 255);
        }
    }

    return overview.save(overviewJpgPath, "JPG", 92);
}

QString normalizedAbsolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

bool removeCompletedLegacyRefinementManifest(const QString& manifestPath,
                                             const QStringList& expectedOutputPaths,
                                             QString& errorMessage)
{
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("Cannot read legacy refinement transaction manifest: %1")
            .arg(manifestPath);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = QStringLiteral("Legacy refinement transaction manifest is invalid: %1")
            .arg(manifestPath);
        return false;
    }

    const QJsonObject manifest = document.object();
    if (manifest.value(QStringLiteral("state")).toString() != QStringLiteral("complete")) {
        errorMessage = QStringLiteral("Refusing to rerun while an unfinished refinement transaction remains: %1")
            .arg(manifestPath);
        return false;
    }

    const QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (outputs.isEmpty() || outputs.size() != expectedOutputPaths.size()) {
        errorMessage = QStringLiteral("Completed refinement transaction has an unexpected output list: %1")
            .arg(manifestPath);
        return false;
    }

    const QDir outputDir = QFileInfo(manifestPath).dir();
    const QString normalizedOutputDir = normalizedAbsolutePath(outputDir.absolutePath());
    QSet<QString> expectedOutputs;
    for (const QString& outputPath : expectedOutputPaths) {
        expectedOutputs.insert(normalizedAbsolutePath(outputPath));
    }

    QSet<QString> manifestOutputs;
    const QStringList temporaryKeys = {
        QStringLiteral("temporary"),
        QStringLiteral("fullBurst"),
        QStringLiteral("fullBurstTemporary"),
        QStringLiteral("backup")
    };
    for (const QJsonValue& value : outputs) {
        if (!value.isObject()) {
            errorMessage = QStringLiteral("Completed refinement transaction contains an invalid output entry: %1")
                .arg(manifestPath);
            return false;
        }

        const QJsonObject output = value.toObject();
        const QString finalPath = output.value(QStringLiteral("output")).toString();
        const QFileInfo finalInfo(finalPath);
        if (finalPath.isEmpty() || normalizedAbsolutePath(finalInfo.absolutePath()) != normalizedOutputDir ||
            !finalInfo.isFile()) {
            errorMessage = QStringLiteral("Completed refinement transaction has a missing or unsafe final output: %1")
                .arg(manifestPath);
            return false;
        }
        manifestOutputs.insert(normalizedAbsolutePath(finalPath));

        for (const QString& key : temporaryKeys) {
            const QString temporaryPath = output.value(key).toString();
            const QFileInfo temporaryInfo(temporaryPath);
            if (temporaryPath.isEmpty() ||
                normalizedAbsolutePath(temporaryInfo.absolutePath()) != normalizedOutputDir ||
                temporaryInfo.exists()) {
                errorMessage = QStringLiteral("Completed refinement transaction still has unsafe or residual %1 data: %2")
                    .arg(key, manifestPath);
                return false;
            }
        }
    }

    if (manifestOutputs != expectedOutputs) {
        errorMessage = QStringLiteral("Completed refinement transaction does not match this run's outputs: %1")
            .arg(manifestPath);
        return false;
    }

    manifestFile.close();
    if (!QFile::remove(manifestPath)) {
        errorMessage = QStringLiteral("Cannot remove completed legacy refinement transaction manifest: %1")
            .arg(manifestPath);
        return false;
    }

    InSARLogManager::LogInfo("S1TopsBackGeocodingNode",
        QStringLiteral("Removed completed legacy refinement transaction manifest: %1").arg(manifestPath));
    return true;
}

} // namespace

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
    m_destroying = true;
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
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("node destroyed"), projectXml());
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

    modelJson["processingWarning"] = m_processingWarning;
    QJsonArray qualityWarnings;
    for (const QString& warning : m_processingQualityWarnings) {
        qualityWarnings.append(warning);
    }
    modelJson["processingQualityWarnings"] = qualityWarnings;

    QJsonArray registrationOffsets;
    for (const RegistrationOffsetSummary& offset : m_registrationOffsets) {
        QJsonObject offsetJson;
        offsetJson["slaveName"] = offset.slaveName;
        offsetJson["azimuthOffset"] = offset.azimuthOffset;
        offsetJson["rangeOffset"] = offset.rangeOffset;
        offsetJson["hasAzimuthOffset"] = offset.hasAzimuthOffset;
        offsetJson["hasRangeOffset"] = offset.hasRangeOffset;
        registrationOffsets.append(offsetJson);
    }
    modelJson["registrationOffsets"] = registrationOffsets;

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

    m_processingWarning = json["processingWarning"].toBool(false);
    m_processingQualityWarnings.clear();
    for (const QJsonValue& warning : json["processingQualityWarnings"].toArray()) {
        if (warning.isString()) {
            m_processingQualityWarnings.append(warning.toString());
        }
    }

    m_registrationOffsets.clear();
    for (const QJsonValue& value : json["registrationOffsets"].toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject offsetJson = value.toObject();
        RegistrationOffsetSummary offset;
        offset.slaveName = offsetJson["slaveName"].toString();
        offset.azimuthOffset = offsetJson["azimuthOffset"].toDouble();
        offset.rangeOffset = offsetJson["rangeOffset"].toDouble();
        offset.hasAzimuthOffset = offsetJson["hasAzimuthOffset"].toBool(false);
        offset.hasRangeOffset = offsetJson["hasRangeOffset"].toBool(false);
        m_registrationOffsets.append(offset);
    }

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
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    Q_UNUSED(message);
    // Reserve the last two percent for the node-owned preview stage. The
    // native worker has already published valid H5 output when it reports 100.
    setProgress(qBound(0, progress, 98));
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

QStringList S1TopsBackGeocodingNode::registrationOverviewPathsFromH5Paths(const QStringList& h5Paths) const
{
    QStringList overviewPaths;
    if (h5Paths.size() < 2) {
        return overviewPaths;
    }

    const QFileInfo masterInfo(h5Paths.first());
    for (int i = 1; i < h5Paths.size(); ++i) {
        const QFileInfo slaveInfo(h5Paths.at(i));
        overviewPaths.append(masterInfo.absolutePath() + "/" + masterInfo.baseName() +
            "__" + slaveInfo.baseName() + "_registration_overview.jpg");
    }
    return overviewPaths;
}

void S1TopsBackGeocodingNode::updateRegistrationOffsets(const QStringList& h5Paths)
{
    m_registrationOffsets.clear();
    if (h5Paths.size() < 2) {
        return;
    }

    FormatConversion conversion;
    for (int i = 1; i < h5Paths.size(); ++i) {
        RegistrationOffsetSummary summary;
        summary.slaveName = QFileInfo(h5Paths.at(i)).baseName();
        {
            NodeUtils::Hdf5Locker locker(h5Paths.at(i));
            summary.hasAzimuthOffset = conversion.read_double_from_h5(
                h5Paths.at(i).toStdString().c_str(), "offset_a", &summary.azimuthOffset) == 0;
            summary.hasRangeOffset = conversion.read_double_from_h5(
                h5Paths.at(i).toStdString().c_str(), "offset_r", &summary.rangeOffset) == 0;
        }
        m_registrationOffsets.append(summary);
    }
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

bool S1TopsBackGeocodingNode::syncProjectTreeOrder(const QStringList& h5Paths, const QString& dstNode)
{
    QStandardItemModel* model = projectModel();
    if (!model)
        return false;

    QList<QStandardItem*> projects = model->findItems(projectName());
    if (projects.isEmpty())
        return false;

    QStandardItem* project = projects.first();
    QStandardItem* resultNode = NodeUtils::findOrCreateProjectNode(
        project, dstNode, "complex-2.0", FOLDER_ICON);
    if (!resultNode)
        return false;

    resultNode->setToolTip(projectName());
    resultNode->removeRows(0, resultNode->rowCount());
    for (const QString& h5Path : h5Paths) {
        QFileInfo fi(h5Path);
        NodeUtils::findOrCreateChildItem(
            resultNode, fi.baseName(), "complex", fi.absoluteFilePath(), IMAGEDATA_ICON);
    }
    return true;
}

bool S1TopsBackGeocodingNode::syncProjectXmlOrder(
    const QStringList& h5Paths, const QString& dstNode)
{
    XMLFile* xml = projectXml();
    TiXmlElement* root = nullptr;
    if (!xml || xml->get_root(root) < 0 || !root)
        return false;

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

    return true;
}

void S1TopsBackGeocodingNode::onProcessingFinished(
    const QStringList& regisH5Paths,
    const QString& dstNode,
    const QString& dstProject,
    const QString& savePath,
    int masterIndex,
    bool hasQualityWarning,
    const QStringList& qualityWarnings
)
{
    Q_UNUSED(dstProject);
    Q_UNUSED(savePath);
    Q_UNUSED(dstNode);
    Q_UNUSED(masterIndex);
    if (!isCurrentGeneration(m_activeGeneration)) return;
    m_pendingWorkerH5Paths = regisH5Paths;
    m_processingWarning = hasQualityWarning;
    m_processingQualityWarnings = qualityWarnings;
    m_workerFinishedSuccessfully = true;
}

void S1TopsBackGeocodingNode::onError(const QString& error)
{
    m_pendingWorkerError = error;
    if (!m_thread || !m_thread->isRunning()) failStagedTransaction(error);
}

void S1TopsBackGeocodingNode::onCancelled(const QStringList& cleanupFailures)
{
    m_pendingWorkerCancelled = true;
    if (!cleanupFailures.isEmpty()) m_pendingWorkerError = cleanupFailures.join(QStringLiteral(", "));
    if (!m_thread || !m_thread->isRunning()) failStagedTransaction(m_pendingWorkerError, true);
}

void S1TopsBackGeocodingNode::onModelUpdated(QStandardItemModel* model)
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
    if (!m_executionSuperseded && !m_destroying && !isAutomaticExecutionObsolete()) {
        m_userCancellationRequested = true;
    }
    invalidateExecutionGeneration();
    // Keep the completion callback connected so it can abandon staging only
    // after the preview task has reached its finished boundary.
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }

    if (m_workerThread) {
        // 直接调用线程安全接口；不能排队到忙碌的 Worker 线程。
        m_workerThread->requestCancel();
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
    setStartFailureMessage(QString());
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
    m_preparedInputPaths = m_inputData->filePaths();
    if (m_preparedInputPaths.size() < 2) {
        InSARLogManager::LogError("S1TopsBackGeocodingNode", "At least two input files are required for Back-Geocoding.");
        return false;
    }

    // 覆盖提示判断
    QStringList pathsToCheck;
    for (const QString& inputPath : m_preparedInputPaths) {
        const QString originName = QFileInfo(inputPath).baseName();
        if (originName.isEmpty()) {
            InSARLogManager::LogError("S1TopsBackGeocodingNode", "Input path does not have a valid file name: " + inputPath);
            return false;
        }
        pathsToCheck.append(QDir(m_preparedSavePath).filePath(m_preparedDstNode + "/" + originName + "_regis.h5"));
    }

    const QString refinementManifestPath = QDir(m_preparedSavePath).filePath(
        m_preparedDstNode + QStringLiteral("/refinement_transaction.json"));
    if (QFileInfo::exists(refinementManifestPath)) {
        QString manifestError;
        if (!removeCompletedLegacyRefinementManifest(refinementManifestPath, pathsToCheck, manifestError)) {
            setStartFailureMessage(manifestError);
            InSARLogManager::LogError("S1TopsBackGeocodingNode", manifestError);
            return false;
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
    m_preparedImagesNumber = m_preparedInputPaths.size();

    if (m_preparedImagesNumber < 2)
    {
        InSARLogManager::LogError("S1TopsBackGeocodingNode", "Images number is less than 2, cannot perform Back-Geocoding.");
        return false;
    }

    return true;
}

void S1TopsBackGeocodingNode::executeProcessing()
{
    if (m_remedyWatcher.isRunning()) {
        m_executionSuperseded = true;
        stopExecution();
        connect(&m_remedyWatcher, &QFutureWatcher<PreviewGenerationResult>::finished, this, [this]() {
            QTimer::singleShot(0, this, [this]() { executeProcessing(); });
        });
        return;
    }
    if (m_workerThread || m_thread) {
        QPointer<QThread> stoppingThread = m_thread;
        m_executionSuperseded = true;
        stopExecution();
        if (stoppingThread) {
            if (stoppingThread->isRunning()) {
                connect(stoppingThread, &QThread::finished, this, [this]() {
                    QTimer::singleShot(0, this, [this]() { executeProcessing(); });
                });
            } else {
                QTimer::singleShot(0, this, [this]() { executeProcessing(); });
            }
            return;
        }
    }
    m_executionSuperseded = false;
    m_userCancellationRequested = false;
    invalidateExecutionGeneration();
    const quint64 generation = m_activeGeneration;
    m_executionTimer.start();
    InSARLogManager::LogDiagnostic(InSARLogManager::LevelInfo, "S1 TOPS Back-Geocoding",
        QStringLiteral("后向地理编码任务已提交。"), LogTargets(LogTarget::UserProjectLog),
        QStringLiteral("lifecycle"), QStringLiteral("queued"), QStringLiteral("queued"));

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

    setProgress(0);
    m_processingWarning = false;
    m_processingQualityWarnings.clear();
    m_pendingWorkerH5Paths.clear();
    m_pendingWorkerError.clear();
    m_pendingWorkerCancelled = false;
    m_workerFinishedSuccessfully = false;

    QString transactionError;
    const QStringList expectedH5Paths = expectedH5PathsForTransaction();
    const QStringList expectedJpgPaths = expectedPreviewPathsForH5Paths(expectedH5Paths);
    const QStringList orderedExpectedH5Paths = moveMasterToFront(expectedH5Paths, m_preparedMasterIndex);
    const QStringList expectedOverviewPaths = registrationOverviewPathsFromH5Paths(orderedExpectedH5Paths);
    QStringList expectedFinalPaths = expectedH5Paths;
    expectedFinalPaths.append(expectedJpgPaths);
    expectedFinalPaths.append(expectedOverviewPaths);
    QSet<QString> expectedNames;
    for (const QString& expectedPath : expectedFinalPaths) {
        expectedNames.insert(QFileInfo(expectedPath).fileName().toCaseFolded());
    }
    if (expectedNames.size() != expectedFinalPaths.size()) {
        failStagedTransaction(QStringLiteral("Back-geocoding output manifest contains duplicate file names."));
        return;
    }
    NodeUtils::OutputTransactionRecoveryInfo recoveryInfo;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode, expectedFinalPaths,
                                           m_preparedInputPaths, m_outputTransaction, &transactionError,
                                           &recoveryInfo)) {
        failStagedTransaction(transactionError);
        return;
    }
    if (recoveryInfo.projectXmlRestored && !reloadProjectXmlAfterRecovery(&transactionError)) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, transactionError, projectXml());
        failStagedTransaction(transactionError);
        return;
    }

    // Create thread
    m_thread = new QThread();
    m_workerThread = new S1TopsBackGeocodingWorker();
    TaskLogContext logContext = InSARLogManager::currentTaskContext();
    logContext.nodeId = name();
    logContext.displayName = caption();
    logContext.scope = QStringLiteral("task");
    m_workerThread->setTaskLogContext(logContext);
    m_workerThread->setDemPath(m_preparedDemPath);
    m_workerThread->setRangeRefine(m_preparedBRangeRefine);
    m_workerThread->prepareForStart();
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_thread, &QThread::finished, this, [this, generation]() {
        m_workerThread = nullptr;
        m_thread = nullptr;
        const bool automaticObsolete = discardObsoleteAutomaticExecution();
        if (!isCurrentGeneration(generation) || automaticObsolete) {
            NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete execution"), projectXml());
            if (m_userCancellationRequested && !m_executionSuperseded && !m_destroying) {
                failStagedTransaction(QStringLiteral("cancelled"), true);
            }
        } else if (m_pendingWorkerCancelled) {
            failStagedTransaction(m_pendingWorkerError, true);
        } else if (!m_pendingWorkerError.isEmpty() || !m_workerFinishedSuccessfully) {
            failStagedTransaction(m_pendingWorkerError.isEmpty()
                ? QStringLiteral("Back-geocoding worker ended without a successful result.") : m_pendingWorkerError);
        } else {
            beginStagedPreview(generation);
        }
    });

    // Connect signals
    connect(this, &S1TopsBackGeocodingNode::startBackGeocoding, m_workerThread, &S1TopsBackGeocodingWorker::S1_TOPS_BackGeocoding);
    
    int masterIndex = m_preparedMasterIndex;
    QString savePath = m_preparedSavePath;
    QString dstProject = m_preparedDstProject;
    QString dstNode = m_outputTransaction.stagingName;
    QStringList inputPaths = m_preparedInputPaths;
    bool b_ESD = m_preparedBESD;
    
    connect(m_thread, &QThread::started, [this, generation, masterIndex, savePath, dstProject, dstNode, inputPaths, b_ESD]() {
        if (!isCurrentGeneration(generation)) return;
        InSARLogManager::LogDiagnostic(InSARLogManager::LevelInfo, "S1 TOPS Back-Geocoding",
            QStringLiteral("后向地理编码 Worker 已启动。"), LogTargets(LogTarget::UserProjectLog),
            QStringLiteral("lifecycle"), QStringLiteral("worker_started"), QStringLiteral("running"));
        Q_EMIT startBackGeocoding(masterIndex, savePath, dstProject, dstNode, inputPaths, b_ESD);
    });
    connect(m_workerThread, &S1TopsBackGeocodingWorker::updateProcess, this,
            [this, generation](int progress, const QString& message) {
        if (isCurrentGeneration(generation)) onProgressUpdate(progress, message);
    });
    connect(m_workerThread, &S1TopsBackGeocodingWorker::registrationFinished, this,
            [this, generation](const QStringList& paths, const QString& node, const QString& project,
                               const QString& root, int master, bool warning, const QStringList& warnings) {
        if (isCurrentGeneration(generation)) onProcessingFinished(paths, node, project, root, master, warning, warnings);
    });
    connect(m_workerThread, &S1TopsBackGeocodingWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::cancelled, this, [this, generation](const QStringList& failures) {
        if (isCurrentGeneration(generation)) onCancelled(failures);
    });
    connect(m_workerThread, &S1TopsBackGeocodingWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &S1TopsBackGeocodingWorker::errorProcess, this, [this, generation](const QString& error) {
        if (isCurrentGeneration(generation)) onError(error);
    });
    connect(m_workerThread, &S1TopsBackGeocodingWorker::errorProcess, m_thread, &QThread::quit);

    // Start thread
    deferAutomaticCompletion();
    m_thread->start();
    updateParameterWidgetsEnableState();

    // 在下一个事件循环中强行将状态重置为 Running，防止基类 setInData 在 Automatic 模式下将其强行设为 Idle
    QTimer::singleShot(0, this, [this, generation]() {
        if (isCurrentGeneration(generation) && m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

bool S1TopsBackGeocodingNode::isCurrentGeneration(quint64 generation) const
{
    return generation != 0 && generation == m_activeGeneration;
}

void S1TopsBackGeocodingNode::invalidateExecutionGeneration()
{
    m_activeGeneration = ++m_executionGeneration;
}

QStringList S1TopsBackGeocodingNode::expectedH5PathsForTransaction() const
{
    QStringList paths;
    const QDir outputDir(QDir(m_preparedSavePath).filePath(m_preparedDstNode));
    for (const QString& inputPath : m_preparedInputPaths) {
        const QString baseName = QFileInfo(inputPath).baseName();
        if (!baseName.isEmpty()) paths.append(outputDir.absoluteFilePath(baseName + QStringLiteral("_regis.h5")));
    }
    return paths;
}

QStringList S1TopsBackGeocodingNode::expectedPreviewPathsForH5Paths(const QStringList& h5Paths) const
{
    return jpgPathsFromH5Paths(h5Paths);
}

bool S1TopsBackGeocodingNode::reloadProjectXmlAfterRecovery(QString* errorMessage)
{
    XMLFile* xml = projectXml();
    const QString xmlPath = NodeUtils::getProjectFilePath(_widget);
    const QByteArray nativePath = QDir::toNativeSeparators(xmlPath).toLocal8Bit();
    if (!xml || xmlPath.isEmpty() || xml->XMLFile_load(nativePath.constData()) < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot reload project XML restored by output transaction recovery.");
        return false;
    }
    return true;
}

void S1TopsBackGeocodingNode::failStagedTransaction(const QString& error, bool cancelled)
{
    if (m_outputTransaction.stage != NodeUtils::OutputTransaction::Stage::Inactive &&
        m_outputTransaction.stage != NodeUtils::OutputTransaction::Stage::Failed &&
        m_outputTransaction.stage != NodeUtils::OutputTransaction::Stage::MetadataCommitted &&
        m_outputTransaction.stage != NodeUtils::OutputTransaction::Stage::Completed) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            cancelled ? QStringLiteral("cancelled") : error,
                                            projectXml());
    }
    m_pendingWorkerH5Paths.clear();
    m_workerFinishedSuccessfully = false;
    if (cancelled) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
    } else {
        const QString message = error.isEmpty() ? QStringLiteral("Back-geocoding staging transaction failed.") : error;
        setLastErrorMessage(message);
        setState(ExecutionState::Error);
        Q_EMIT executionError(message);
    }
    updateParameterWidgetsEnableState();
}

void S1TopsBackGeocodingNode::beginStagedPreview(quint64 generation)
{
    if (!isCurrentGeneration(generation) || m_outputTransaction.stage != NodeUtils::OutputTransaction::Stage::StagingPrepared) {
        return;
    }
    const QStringList expectedH5Paths = expectedH5PathsForTransaction();
    const QStringList stagingH5Paths = [&]() {
        QStringList paths;
        const QDir staging(QDir(m_outputTransaction.projectRoot).absoluteFilePath(m_outputTransaction.stagingName));
        for (const QString& expected : expectedH5Paths) paths.append(staging.absoluteFilePath(QFileInfo(expected).fileName()));
        return paths;
    }();
    m_pendingPreviewH5Paths = moveMasterToFront(stagingH5Paths, m_preparedMasterIndex);
    m_pendingPreviewJpgPaths = jpgPathsFromH5Paths(m_pendingPreviewH5Paths);
    m_pendingPreviewOverviewPaths = registrationOverviewPathsFromH5Paths(m_pendingPreviewH5Paths);
    setProgress(99);

    m_remedyWatcher.disconnect(this);
    connect(&m_remedyWatcher, &QFutureWatcher<PreviewGenerationResult>::finished, this, [this, generation]() {
        const bool automaticObsolete = discardObsoleteAutomaticExecution();
        if (!isCurrentGeneration(generation) || automaticObsolete) {
            NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete preview execution"), projectXml());
            if (m_userCancellationRequested && !m_executionSuperseded && !m_destroying) {
                failStagedTransaction(QStringLiteral("cancelled"), true);
            }
            return;
        }
        const PreviewGenerationResult previewResult = m_remedyWatcher.result();
        if (!previewResult.generationFailures.isEmpty()) {
            failStagedTransaction(QStringLiteral("Staged preview generation failed: %1")
                                      .arg(previewResult.generationFailures.join(QStringLiteral("; "))));
            return;
        }
        for (const QString& path : m_pendingPreviewJpgPaths + m_pendingPreviewOverviewPaths) {
            QImageReader reader(path);
            const QImage image = reader.read();
            if (image.isNull()) {
                failStagedTransaction(QStringLiteral("Staged preview is missing or cannot be decoded: %1").arg(path));
                return;
            }
        }
        finalizeStagedTransaction(generation);
    });
    const QStringList h5Paths = m_pendingPreviewH5Paths;
    const QStringList jpgPaths = m_pendingPreviewJpgPaths;
    const QStringList overviewPaths = m_pendingPreviewOverviewPaths;
    m_remedyWatcher.setFuture(QtConcurrent::run([h5Paths, jpgPaths, overviewPaths]() {
        PreviewGenerationResult result;
        for (int i = 0; i < h5Paths.size(); ++i) {
            if (!NodeUtils::generateJpgPreviewFromH5(h5Paths.at(i), jpgPaths.at(i), "complex")) {
                result.generationFailures.append(jpgPaths.at(i));
            }
        }
        for (int i = 1; i < jpgPaths.size() && i - 1 < overviewPaths.size(); ++i) {
            if (!generateRegistrationOverviewPreview(jpgPaths.first(), jpgPaths.at(i), overviewPaths.at(i - 1))) {
                result.generationFailures.append(overviewPaths.at(i - 1));
            }
        }
        return result;
    }));
}

void S1TopsBackGeocodingNode::finalizeStagedTransaction(quint64 generation)
{
    if (!isCurrentGeneration(generation)) return;
    if (!projectXml()) {
        failStagedTransaction(QStringLiteral("Project XML context is unavailable for output commit."));
        return;
    }
    QString error;
    const QStringList expectedH5Paths = expectedH5PathsForTransaction();
    const QDir staging(QDir(m_outputTransaction.projectRoot).absoluteFilePath(m_outputTransaction.stagingName));
    QStringList stagedManifestH5Paths;
    for (const QString& expected : expectedH5Paths) stagedManifestH5Paths.append(staging.absoluteFilePath(QFileInfo(expected).fileName()));
    for (const QString& workerPath : m_pendingWorkerH5Paths) {
        if (QDir::cleanPath(QFileInfo(workerPath).absolutePath()).compare(
                QDir::cleanPath(staging.absolutePath()), Qt::CaseInsensitive) != 0) {
            failStagedTransaction(QStringLiteral("Worker reported an output outside the staging directory: %1").arg(workerPath));
            return;
        }
    }
    if (m_pendingWorkerH5Paths.size() != stagedManifestH5Paths.size()) {
        failStagedTransaction(QStringLiteral("Worker H5 output count does not match the input-order manifest."));
        return;
    }
    for (int i = 0; i < stagedManifestH5Paths.size(); ++i) {
        if (QFileInfo(m_pendingWorkerH5Paths.at(i)).fileName() != QFileInfo(stagedManifestH5Paths.at(i)).fileName()) {
            failStagedTransaction(QStringLiteral("Worker H5 output order does not match the input-order manifest."));
            return;
        }
    }
    if (!NodeUtils::workerOutputsMatchManifest(stagedManifestH5Paths, m_pendingWorkerH5Paths, &error) ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &error) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
                                              QStringList() << QStringLiteral("s_re") << QStringLiteral("s_im"), &error)) {
        failStagedTransaction(error);
        return;
    }
    for (const QString& path : stagedManifestH5Paths) {
        if (!isCompleteBackGeocodingOutput(path)) {
            failStagedTransaction(QStringLiteral("Staged back-geocoding output is incomplete: %1").arg(path));
            return;
        }
    }

    QStringList finalPaths;
    if (!NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &error)) {
        failStagedTransaction(error);
        return;
    }
    QHash<QString, QString> finalByName;
    for (const QString& path : finalPaths) finalByName.insert(QFileInfo(path).fileName(), path);
    QStringList finalH5Paths;
    for (const QString& expected : expectedH5Paths) {
        const QString finalPath = finalByName.value(QFileInfo(expected).fileName());
        if (finalPath.isEmpty()) {
            failStagedTransaction(QStringLiteral("Promoted H5 is missing from the transaction manifest."));
            return;
        }
        finalH5Paths.append(finalPath);
    }
    const QStringList orderedH5Paths = moveMasterToFront(finalH5Paths, m_preparedMasterIndex);
    if (!NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, projectXml(),
                                                           NodeUtils::getProjectFilePath(_widget), &error) ||
        !syncProjectXmlOrder(orderedH5Paths, m_preparedDstNode) ||
        !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &error) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &error)) {
        if (error.isEmpty()) error = QStringLiteral("Unable to commit back-geocoding project metadata.");
        failStagedTransaction(error);
        return;
    }

    m_savedOutputPaths = orderedH5Paths;
    m_savedMasterOutputPath = orderedH5Paths.isEmpty() ? QString() : orderedH5Paths.first();
    m_outputNodeName = m_preparedDstNode;
    m_registrationOverviewPaths = registrationOverviewPathsFromH5Paths(orderedH5Paths);
    updateRegistrationOffsets(orderedH5Paths);
    const bool treePublished = syncProjectTreeOrder(orderedH5Paths, m_preparedDstNode);
    m_outputData = std::make_shared<ImportedFileData>(orderedH5Paths, m_preparedDstNode);
    m_imageInfoData = std::make_shared<ImageInfoData>(jpgPathsFromH5Paths(orderedH5Paths));
    setOutputData(0, m_outputData);
    setOutputData(1, m_imageInfoData);
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }
    if (!treePublished) {
        InSARLogManager::LogWarning("S1TopsBackGeocodingNode",
            "Back-geocoding output committed, but the project tree could not be published; requested a tree refresh.");
    }
    setProgress(100);
    if (m_processingWarning) {
        setLastWarningMessage(m_processingQualityWarnings.join('\n'));
        finishExecutionWithWarning();
    } else {
        finishExecution();
    }
    updateParameterWidgetsEnableState();
}
bool S1TopsBackGeocodingNode::isCompleteBackGeocodingOutput(const QString& path) const
{
    if (!QFileInfo::exists(path)) {
        return false;
    }

    FormatConversion conversion;
    int complete = 0;
    int azimuthLen = 0;
    int rangeLen = 0;
    int realRows = 0;
    int realCols = 0;
    int imagRows = 0;
    int imagCols = 0;
    {
        NodeUtils::Hdf5Locker locker(path.toStdString());
        if (conversion.read_int_from_h5(path.toStdString().c_str(), "s1_tops_back_geocoding_complete", &complete) != 0 || complete != 1 ||
            conversion.get_dataset_dims(path.toStdString().c_str(), "s_re", &realRows, &realCols) != 0 ||
            conversion.get_dataset_dims(path.toStdString().c_str(), "s_im", &imagRows, &imagCols) != 0 ||
            conversion.read_int_from_h5(path.toStdString().c_str(), "azimuth_len", &azimuthLen) != 0 ||
            conversion.read_int_from_h5(path.toStdString().c_str(), "range_len", &rangeLen) != 0) {
            return false;
        }
    }

    return realRows > 0 && realCols > 0 && imagRows > 0 && imagCols > 0 &&
        realRows == imagRows && realCols == imagCols &&
        realRows == azimuthLen && realCols == rangeLen;
}

bool S1TopsBackGeocodingNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList manifestPaths;
    QString manifestError;
    const bool hasCommittedManifest = NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode,
                                                                              manifestPaths, &manifestError);
    QStringList orderedH5Paths;
    if (hasCommittedManifest) {
        for (const QString& path : manifestPaths) {
            if (QFileInfo(path).suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0) {
                orderedH5Paths.append(path);
            }
        }
        orderedH5Paths = moveMasterToFront(orderedH5Paths, m_masterIndex);
    } else {
        // Legacy projects without a journal remain readable, but are not migrated implicitly.
        orderedH5Paths = restoreOrderedH5Paths(dstNode);
    }
    if (orderedH5Paths.isEmpty())
        return false;

    for (const QString& path : orderedH5Paths) {
        if (!isCompleteBackGeocodingOutput(path)) {
            InSARLogManager::LogWarning("S1TopsBackGeocodingNode",
                QStringLiteral("拒绝恢复未完成或无效的后向地理编码输出：%1").arg(path));
            return false;
        }
    }

    m_savedOutputPaths = orderedH5Paths;
    m_savedMasterOutputPath = orderedH5Paths.first();
    m_registrationOverviewPaths.clear();
    updateRegistrationOffsets(orderedH5Paths);
    m_outputData = std::make_shared<ImportedFileData>(orderedH5Paths, dstNode);
    setOutputData(0, m_outputData);

    const QStringList allJpgPaths = jpgPathsFromH5Paths(orderedH5Paths);
    bool previewsValid = true;
    for (const QString& path : allJpgPaths) {
        QImageReader reader(path);
        if (reader.read().isNull()) {
            previewsValid = false;
            break;
        }
    }
    if (previewsValid) {
        const QStringList overviewPaths = registrationOverviewPathsFromH5Paths(orderedH5Paths);
        for (const QString& path : overviewPaths) {
            QImageReader reader(path);
            if (reader.read().isNull()) {
                previewsValid = false;
                break;
            }
            m_registrationOverviewPaths.append(path);
        }
    }
    if (previewsValid) {
        m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
        setOutputData(1, m_imageInfoData);
    } else {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        setLastWarningMessage(QStringLiteral("Committed back-geocoding previews are unavailable or invalid."));
    }
    if (!syncProjectTreeOrder(orderedH5Paths, dstNode)) {
        InSARLogManager::LogWarning("S1TopsBackGeocodingNode",
            "Restored back-geocoding output could not be published to the project tree; requested a tree refresh.");
    }

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

std::vector<QString> S1TopsBackGeocodingNode::processingInfo() const
{
    std::vector<QString> info;
    const QStringList h5Paths = getOrderedH5Paths();
    if (h5Paths.isEmpty()) {
        info.push_back(tr("配准状态：等待处理完成"));
        return info;
    }

    const QString status = m_processingWarning
        ? tr("配准质量：提醒")
        : tr("配准质量：通过");
    info.push_back(status);
    info.push_back(tr("主影像：%1；从影像：%2 幅")
        .arg(QFileInfo(h5Paths.first()).baseName())
        .arg(qMax(0, h5Paths.size() - 1)));
    info.push_back(tr("精配准：ESD %1；距离向振幅精配准 %2")
        .arg(m_bESD ? tr("已启用") : tr("未启用"))
        .arg(m_bRangeRefine ? tr("已启用") : tr("未启用")));

    if (!m_registrationOverviewPaths.isEmpty() ||
        !registrationOverviewPathsFromH5Paths(h5Paths).isEmpty()) {
        info.push_back(tr("全图配准概览：主影像（红）/ 从影像（青）"));
    }

    for (const RegistrationOffsetSummary& offset : m_registrationOffsets) {
        const QString azimuth = offset.hasAzimuthOffset
            ? QString::number(offset.azimuthOffset, 'f', 4)
            : tr("未应用");
        const QString range = offset.hasRangeOffset
            ? QString::number(offset.rangeOffset, 'f', 4)
            : tr("未应用");
        info.push_back(tr("%1：方位补偿 %2；距离补偿 %3")
            .arg(offset.slaveName, azimuth, range));
    }

    if (!m_processingQualityWarnings.isEmpty()) {
        info.push_back(tr("质量告警：%1 项").arg(m_processingQualityWarnings.size()));
        const int warningCount = qMin(2, m_processingQualityWarnings.size());
        for (int i = 0; i < warningCount; ++i) {
            info.push_back(m_processingQualityWarnings.at(i));
        }
    }

    return info;
}

QStringList S1TopsBackGeocodingNode::previewImagePaths() const
{
    QStringList h5Paths = getOrderedH5Paths();
    QStringList existingPaths;
    const QStringList overviewPaths = m_registrationOverviewPaths.isEmpty()
        ? registrationOverviewPathsFromH5Paths(h5Paths)
        : m_registrationOverviewPaths;
    for (const QString& overviewPath : overviewPaths) {
        if (QFileInfo::exists(overviewPath)) {
            existingPaths.append(overviewPath);
        }
    }
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

            // 计算局部空间对比度（强度变异系数 Da = stddev / mean），用来排除大面积均匀分布但易失相干的区域（如水体、植被等）
            cv::Scalar meanVal, stdDevVal;
            cv::meanStdDev(win_amp, meanVal, stdDevVal);
            double mean = meanVal[0];
            double stddev = stdDevVal[0];
            double spatialContrast = (mean > 0.0) ? (stddev / mean) : 0.0;

            // 只有当局部对比度大于阈值（如 0.30）时，才认为该点具备较强的结构特征（可能是人工建筑或裸石等高相干源）
            if (spatialContrast >= 0.30)
            {
                Candidate refined;
                refined.val = (float)maxVal;
                refined.x = start_x + maxLoc.x;
                refined.y = start_y + maxLoc.y;
                valid_points.push_back(refined);
            }
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
struct FullCoherenceResult {
    int retCode = -1;
    int slaveIndex = -1;
    int sourceRows = 0;
    int sourceCols = 0;
    QString imagePath;
    QImage image;
};

static QString fullCoherencePreviewPath(const QString& masterPath, const QString& slavePath)
{
    const QFileInfo masterInfo(masterPath);
    const QFileInfo slaveInfo(slavePath);
    return masterInfo.absolutePath() + "/" + masterInfo.baseName() + "__" +
        slaveInfo.baseName() + "_full_coherence.jpg";
}

static bool isFullCoherencePreviewCurrent(const QString& masterPath,
                                          const QString& slavePath,
                                          const QString& previewPath)
{
    const QFileInfo previewInfo(previewPath);
    if (!previewInfo.exists() || previewInfo.size() == 0) {
        return false;
    }
    return previewInfo.lastModified() >= QFileInfo(masterPath).lastModified() &&
        previewInfo.lastModified() >= QFileInfo(slavePath).lastModified();
}

static FullCoherenceResult generateFullCoherencePreview(const QString& masterPath,
                                                        const QString& slavePath,
                                                        int slaveIndex)
{
    FullCoherenceResult result;
    result.slaveIndex = slaveIndex;
    result.imagePath = fullCoherencePreviewPath(masterPath, slavePath);

    FormatConversion conversion;
    int masterRows = 0;
    int masterCols = 0;
    int slaveRows = 0;
    int slaveCols = 0;
    NodeUtils::Hdf5Locker locker;
    if (conversion.get_dataset_dims(masterPath.toStdString().c_str(), "s_re", &masterRows, &masterCols) != 0 ||
        conversion.get_dataset_dims(slavePath.toStdString().c_str(), "s_re", &slaveRows, &slaveCols) != 0 ||
        masterRows <= 0 || masterCols <= 0 || masterRows != slaveRows || masterCols != slaveCols) {
        return result;
    }
    result.sourceRows = masterRows;
    result.sourceCols = masterCols;

    const int maxDimension = 1024;
    const int downsample = qMax(1, static_cast<int>(std::ceil(
        qMax(masterRows, masterCols) / static_cast<double>(maxDimension))));
    const int previewRows = masterRows / downsample;
    const int previewCols = masterCols / downsample;
    if (previewRows < 2 || previewCols < 2) {
        return result;
    }

    cv::Mat masterReal(previewRows, previewCols, CV_32F);
    cv::Mat masterImag(previewRows, previewCols, CV_32F);
    cv::Mat slaveReal(previewRows, previewCols, CV_32F);
    cv::Mat slaveImag(previewRows, previewCols, CV_32F);
    const int usableRows = previewRows * downsample;
    const int usableCols = previewCols * downsample;
    const int maxBlockPixels = 2 * 1024 * 1024;
    const int requestedBlockRows = qMax(downsample, maxBlockPixels / usableCols);
    const int blockRows = qMax(downsample, (requestedBlockRows / downsample) * downsample);

    for (int sourceRow = 0; sourceRow < usableRows; sourceRow += blockRows) {
        int rowsToRead = qMin(blockRows, usableRows - sourceRow);
        rowsToRead -= rowsToRead % downsample;
        if (rowsToRead <= 0) {
            continue;
        }

        cv::Mat masterRealBlock;
        cv::Mat masterImagBlock;
        cv::Mat slaveRealBlock;
        cv::Mat slaveImagBlock;
        if (conversion.read_subarray_from_h5(masterPath.toStdString().c_str(), "s_re", sourceRow, 0,
                                              rowsToRead, usableCols, masterRealBlock) != 0 ||
            conversion.read_subarray_from_h5(masterPath.toStdString().c_str(), "s_im", sourceRow, 0,
                                              rowsToRead, usableCols, masterImagBlock) != 0 ||
            conversion.read_subarray_from_h5(slavePath.toStdString().c_str(), "s_re", sourceRow, 0,
                                              rowsToRead, usableCols, slaveRealBlock) != 0 ||
            conversion.read_subarray_from_h5(slavePath.toStdString().c_str(), "s_im", sourceRow, 0,
                                              rowsToRead, usableCols, slaveImagBlock) != 0) {
            return result;
        }

        masterRealBlock.convertTo(masterRealBlock, CV_32F);
        masterImagBlock.convertTo(masterImagBlock, CV_32F);
        slaveRealBlock.convertTo(slaveRealBlock, CV_32F);
        slaveImagBlock.convertTo(slaveImagBlock, CV_32F);

        const int outputRows = rowsToRead / downsample;
        const cv::Size outputSize(previewCols, outputRows);
        const int outputRow = sourceRow / downsample;
        cv::resize(masterRealBlock, masterReal.rowRange(outputRow, outputRow + outputRows), outputSize, 0, 0, cv::INTER_AREA);
        cv::resize(masterImagBlock, masterImag.rowRange(outputRow, outputRow + outputRows), outputSize, 0, 0, cv::INTER_AREA);
        cv::resize(slaveRealBlock, slaveReal.rowRange(outputRow, outputRow + outputRows), outputSize, 0, 0, cv::INTER_AREA);
        cv::resize(slaveImagBlock, slaveImag.rowRange(outputRow, outputRow + outputRows), outputSize, 0, 0, cv::INTER_AREA);
    }

    cv::Mat realInterferogram = masterReal.mul(slaveReal) + masterImag.mul(slaveImag);
    cv::Mat imagInterferogram = masterImag.mul(slaveReal) - masterReal.mul(slaveImag);
    cv::Mat masterPower = masterReal.mul(masterReal) + masterImag.mul(masterImag);
    cv::Mat slavePower = slaveReal.mul(slaveReal) + slaveImag.mul(slaveImag);

    const int windowWidth = qMax(3, qMin(9, previewCols | 1));
    const int windowHeight = qMax(3, qMin(9, previewRows | 1));
    const cv::Size window(windowWidth, windowHeight);
    cv::Mat sumReal;
    cv::Mat sumImag;
    cv::Mat sumMasterPower;
    cv::Mat sumSlavePower;
    cv::boxFilter(realInterferogram, sumReal, CV_32F, window, cv::Point(-1, -1), false, cv::BORDER_REFLECT_101);
    cv::boxFilter(imagInterferogram, sumImag, CV_32F, window, cv::Point(-1, -1), false, cv::BORDER_REFLECT_101);
    cv::boxFilter(masterPower, sumMasterPower, CV_32F, window, cv::Point(-1, -1), false, cv::BORDER_REFLECT_101);
    cv::boxFilter(slavePower, sumSlavePower, CV_32F, window, cv::Point(-1, -1), false, cv::BORDER_REFLECT_101);

    cv::Mat numerator;
    cv::sqrt(sumReal.mul(sumReal) + sumImag.mul(sumImag), numerator);
    cv::Mat denominator;
    cv::sqrt(sumMasterPower.mul(sumSlavePower), denominator);
    denominator += 1.0e-6f;
    cv::Mat coherence;
    cv::divide(numerator, denominator, coherence);
    cv::min(coherence, 1.0, coherence);
    cv::max(coherence, 0.0, coherence);

    cv::Mat coherence8;
    coherence.convertTo(coherence8, CV_8U, 255.0);
    cv::Mat coherenceColor;
    cv::applyColorMap(coherence8, coherenceColor, cv::COLORMAP_JET);
    cv::Mat coherenceRgb;
    cv::cvtColor(coherenceColor, coherenceRgb, cv::COLOR_BGR2RGB);
    result.image = QImage(coherenceRgb.data, coherenceRgb.cols, coherenceRgb.rows,
                          static_cast<int>(coherenceRgb.step), QImage::Format_RGB888).copy();
    if (!result.image.save(result.imagePath, "JPG", 92)) {
        result.image = QImage();
        return result;
    }

    result.retCode = 0;
    return result;
}

struct EvalThreadResult {
    int retCode;
    int sourceRows = 0;
    int sourceCols = 0;
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
        QFont resultsTableFont(QStringLiteral("Microsoft YaHei"));
        resultsTableFont.setPointSize(9);
        m_resultsTable->setFont(resultsTableFont);
        m_resultsTable->horizontalHeader()->setFont(resultsTableFont);
        m_resultsTable->setColumnCount(6);
        m_resultsTable->setHorizontalHeaderLabels({
            tr("测试区域"), tr("相干性(配准前)"), tr("相干性(配准后)"), tr("偏移处相干性(Max-Corr)"), tr("残余偏移(Y, X)"), tr("相关系数")
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
        m_visualModeCombo->addItem(tr("局部 2D 相干性热力图"), 0);
        m_visualModeCombo->addItem(tr("局部 红-青对齐叠合图"), 1);
        m_visualModeCombo->addItem(tr("全图相干性热力图"), 2);
        modeLayout->addWidget(m_visualModeCombo, 1);
        rightLayout->addLayout(modeLayout);

        m_fullCoherenceStatusLabel = new QLabel();
        m_fullCoherenceStatusLabel->setWordWrap(true);
        m_fullCoherenceStatusLabel->setStyleSheet(isDark ? "color: #9CA3AF;" : "color: #6B7280;");
        m_fullCoherenceStatusLabel->hide();
        rightLayout->addWidget(m_fullCoherenceStatusLabel);

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
        connect(&m_fullCoherenceWatcher, &QFutureWatcher<FullCoherenceResult>::finished,
                this, &S1TopsRegistrationEvalWidget::onFullCoherenceFinished);

        // 监听节点数据更新信号，动态刷新评估界面
        connect(m_node, &S1TopsBackGeocodingNode::dataUpdated, this, [this](unsigned int port) {
            if (port == 0) {
                m_h5Paths = m_node->getOrderedH5Paths();
                m_fullCoherenceImage = QImage();
                m_fullCoherenceSlaveIndex = -1;
                m_fullCoherenceSourceRows = 0;
                m_fullCoherenceSourceCols = 0;
                updateSlaveCombo();
                startEvaluation();
                if (m_visualModeCombo->currentData().toInt() == 2) {
                    startFullCoherenceForCurrentSlave();
                }
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
        m_fullCoherenceWatcher.cancel();
        m_fullCoherenceWatcher.waitForFinished();
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
        if (m_visualModeCombo->currentData().toInt() == 2) {
            startFullCoherenceForCurrentSlave();
        }
    }

    void onVisualModeChanged(int index)
    {
        Q_UNUSED(index);
        if (m_visualModeCombo->currentData().toInt() == 2) {
            startFullCoherenceForCurrentSlave();
        }
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
        if (m_visualModeCombo->currentData().toInt() != 2) {
            m_imageView->setImage(QImage());
        }
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

            EvalThreadResult threadRes{};
            threadRes.retCode = -1;
            for (int i = 0; i < 5; ++i) {
                threadRes.results[i].structSize = sizeof(AlignmentResult);
                threadRes.results[i].heatmap_rgb = nullptr;
                threadRes.results[i].overlay_rgb = nullptr;
                threadRes.results[i].imageWidth = 0;
                threadRes.results[i].imageHeight = 0;
                threadRes.inputCoherence[i] = -1.0;
            }

            FormatConversion FC;
            FC.get_dataset_dims(masterPath.toLocal8Bit().constData(), "s_re",
                                &threadRes.sourceRows, &threadRes.sourceCols);
            int detectRet = selectHighIntensityPoints(masterPath, threadRes.points);
            if (detectRet < 5) {
                if (threadRes.sourceRows > 0 && threadRes.sourceCols > 0) {
                    threadRes.points[0].x = threadRes.sourceCols / 5.0;       threadRes.points[0].y = threadRes.sourceRows / 5.0;
                    threadRes.points[1].x = threadRes.sourceCols * 4.0 / 5.0; threadRes.points[1].y = threadRes.sourceRows / 5.0;
                    threadRes.points[2].x = threadRes.sourceCols / 2.0;       threadRes.points[2].y = threadRes.sourceRows / 2.0;
                    threadRes.points[3].x = threadRes.sourceCols / 5.0;       threadRes.points[3].y = threadRes.sourceRows * 4.0 / 5.0;
                    threadRes.points[4].x = threadRes.sourceCols * 4.0 / 5.0; threadRes.points[4].y = threadRes.sourceRows * 4.0 / 5.0;
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
                AlignmentResult inputRes[5]{};
                for (int i = 0; i < 5; ++i) {
                    inputRes[i].structSize = sizeof(AlignmentResult);
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

    void startFullCoherenceForCurrentSlave()
    {
        const int slaveIndex = m_slaveCombo->currentIndex() + 1;
        if (m_h5Paths.size() <= 1 || slaveIndex < 1 || slaveIndex >= m_h5Paths.size()) {
            return;
        }

        m_requestedFullCoherenceSlaveIndex = slaveIndex;
        m_fullCoherenceStatusLabel->show();

        const QString masterPath = m_h5Paths.first();
        const QString slavePath = m_h5Paths.at(slaveIndex);
        if (m_fullCoherenceSlaveIndex == slaveIndex && !m_fullCoherenceImage.isNull()) {
            updateFullCoherenceOverlay();
            return;
        }
        const QString previewPath = fullCoherencePreviewPath(masterPath, slavePath);
        if (isFullCoherencePreviewCurrent(masterPath, slavePath, previewPath)) {
            QImage cachedImage(previewPath);
            if (!cachedImage.isNull()) {
                m_fullCoherenceImage = cachedImage;
                m_fullCoherenceSlaveIndex = slaveIndex;
                m_fullCoherenceStatusLabel->setText(tr("已加载全图相干性热力图（9 x 9 局部窗口）。"));
                m_imageView->clearOverlayRects();
                m_imageView->setImage(m_fullCoherenceImage);
                updateFullCoherenceOverlay();
                return;
            }
        }

        if (m_fullCoherenceWatcher.isRunning()) {
            m_fullCoherenceStatusLabel->setText(tr("正在计算其他影像对的全图相干性，当前请求将随后执行..."));
            return;
        }

        m_activeFullCoherenceSlaveIndex = slaveIndex;
        m_fullCoherenceStatusLabel->setText(tr("正在计算全图相干性热力图，请稍候..."));
        m_imageView->clearOverlayRects();
        m_imageView->setImage(QImage());
        QFuture<FullCoherenceResult> future = QtConcurrent::run(
            [masterPath, slavePath, slaveIndex]() {
                return generateFullCoherencePreview(masterPath, slavePath, slaveIndex);
            });
        m_fullCoherenceWatcher.setFuture(future);
    }

    void updateFullCoherenceOverlay()
    {
        if (!m_hasResults || m_fullCoherenceImage.isNull() ||
            m_fullCoherenceSourceRows <= 0 || m_fullCoherenceSourceCols <= 0) {
            m_imageView->clearOverlayRects();
            return;
        }

        const double scaleX = m_fullCoherenceImage.width() /
            static_cast<double>(m_fullCoherenceSourceCols);
        const double scaleY = m_fullCoherenceImage.height() /
            static_cast<double>(m_fullCoherenceSourceRows);
        const double localWindowSize = 200.0;
        const double halfWindow = localWindowSize / 2.0;
        const double minimumMarkerWidth = qMin(18.0, static_cast<double>(m_fullCoherenceImage.width()));
        const double minimumMarkerHeight = qMin(18.0, static_cast<double>(m_fullCoherenceImage.height()));
        QVector<QRectF> sampleRects;
        sampleRects.reserve(5);
        for (int i = 0; i < 5; ++i) {
            const double left = qBound(0.0, m_points[i].x - halfWindow,
                static_cast<double>(m_fullCoherenceSourceCols));
            const double top = qBound(0.0, m_points[i].y - halfWindow,
                static_cast<double>(m_fullCoherenceSourceRows));
            const double right = qBound(0.0, m_points[i].x + halfWindow,
                static_cast<double>(m_fullCoherenceSourceCols));
            const double bottom = qBound(0.0, m_points[i].y + halfWindow,
                static_cast<double>(m_fullCoherenceSourceRows));
            const QRectF actualRect(left * scaleX, top * scaleY,
                qMax(1.0, (right - left) * scaleX),
                qMax(1.0, (bottom - top) * scaleY));
            const double visualWidth = qMax(actualRect.width(), minimumMarkerWidth);
            const double visualHeight = qMax(actualRect.height(), minimumMarkerHeight);
            const double visualLeft = qBound(0.0, actualRect.center().x() - visualWidth / 2.0,
                qMax(0.0, static_cast<double>(m_fullCoherenceImage.width()) - visualWidth));
            const double visualTop = qBound(0.0, actualRect.center().y() - visualHeight / 2.0,
                qMax(0.0, static_cast<double>(m_fullCoherenceImage.height()) - visualHeight));
            sampleRects.append(QRectF(visualLeft, visualTop, visualWidth, visualHeight));
        }

        m_imageView->setOverlayRects(sampleRects, m_resultsTable->currentRow());
    }

    void onFullCoherenceFinished()
    {
        const FullCoherenceResult result = m_fullCoherenceWatcher.result();
        const int completedSlaveIndex = result.slaveIndex;
        m_activeFullCoherenceSlaveIndex = -1;

        if (result.retCode == 0 && !result.image.isNull()) {
            m_fullCoherenceImage = result.image;
            m_fullCoherenceSlaveIndex = result.slaveIndex;
            m_fullCoherenceSourceRows = result.sourceRows;
            m_fullCoherenceSourceCols = result.sourceCols;
            if (m_visualModeCombo->currentData().toInt() == 2 &&
                m_slaveCombo->currentIndex() + 1 == result.slaveIndex) {
                m_fullCoherenceStatusLabel->setText(tr("已加载全图相干性热力图（9 x 9 局部窗口）。"));
                m_imageView->clearOverlayRects();
                m_imageView->setImage(m_fullCoherenceImage);
                updateFullCoherenceOverlay();
            }
        } else if (m_visualModeCombo->currentData().toInt() == 2 &&
                   m_slaveCombo->currentIndex() + 1 == result.slaveIndex) {
            m_fullCoherenceStatusLabel->setText(tr("全图相干性热力图计算失败。"));
            m_imageView->setImage(QImage());
        }

        if (m_visualModeCombo->currentData().toInt() == 2 &&
            m_requestedFullCoherenceSlaveIndex != completedSlaveIndex) {
            startFullCoherenceForCurrentSlave();
        }
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
        m_fullCoherenceSourceRows = threadRes.sourceRows;
        m_fullCoherenceSourceCols = threadRes.sourceCols;
        m_hasResults = true;
        if (m_visualModeCombo->currentData().toInt() == 2) {
            updateFullCoherenceOverlay();
        }

        m_statusLabel->setText(tr("配准评估完成。请在表格中选择采样区域查看细节。"));

        // 统计所有 5 个区域的数据，判定整体配准效果
        int perfectCount = 0;   // 偏移为 0 的个数（包含低置信度点）
        int warningCount = 0;   // 偏移在 [-2, 2] 内但非 0 的个数
        int failedCount = 0;    // 偏移绝对值 > 2 的个数
        double sumCoh = 0.0;
        int validCohCount = 0;
        double sumPreCoh = 0.0;
        int preCohValidCount = 0;
        
        for (int i = 0; i < 5; ++i) {
            double maxCorr = m_results[i].maxCorrelation;
            double postCoh = m_results[i].coherenceZeroShift;
            
            if (maxCorr >= 0.15) {
                sumCoh += postCoh;
                validCohCount++;
            }
            if (m_inputCoherence[i] >= 0.0) {
                sumPreCoh += m_inputCoherence[i];
                preCohValidCount++;
            }
            
            double dy = std::abs(m_results[i].offsetY);
            double dx = std::abs(m_results[i].offsetX);
            
            if (maxCorr < 0.15 || postCoh < 0.20) {
                perfectCount++;
            } else {
                if (dy < 0.01 && dx < 0.01) {
                    perfectCount++;
                } else if (dy <= 2.0 && dx <= 2.0) {
                    warningCount++;
                } else {
                    failedCount++;
                }
            }
        }
        double meanCoh = (validCohCount > 0) ? (sumCoh / validCohCount) : 0.0;
        // 如果所有采样点都是低相关，则退回到全局平均，防止零除
        if (validCohCount == 0) {
            double tempSum = 0.0;
            for (int i = 0; i < 5; ++i) tempSum += m_results[i].coherenceZeroShift;
            meanCoh = tempSum / 5.0;
        }
        double meanPreCoh = (preCohValidCount > 0) ? (sumPreCoh / preCohValidCount) : -1.0;

        const QString assessment = (failedCount == 0 && perfectCount == 5 && meanCoh >= 0.22)
            ? QStringLiteral("pass")
            : (failedCount > 0 || meanCoh < 0.18) ? QStringLiteral("failed") : QStringLiteral("warning");
        InSARLogManager::LogDebug("S1TopsRegistrationEvalWidget",
            QString("Coherence assessment: status=%1, preMean=%2, postMean=%3, change=%4, perfect=%5, warning=%6, failed=%7")
                .arg(assessment).arg(meanPreCoh, 0, 'f', 4).arg(meanCoh, 0, 'f', 4)
                .arg(meanPreCoh >= 0.0 ? meanCoh - meanPreCoh : 0.0, 0, 'f', 4)
                .arg(perfectCount).arg(warningCount).arg(failedCount),
            "registration.assessment");
        for (int i = 0; i < 5; ++i) {
            InSARLogManager::LogDebug("S1TopsRegistrationEvalWidget",
                QString("Assessment sample=%1, point=(%2,%3), coherence={pre=%4, post=%5, optimal=%6}, residualOffset=(%7,%8), correlation=%9, lowConfidence=%10")
                    .arg(i + 1).arg(m_points[i].x).arg(m_points[i].y)
                    .arg(m_inputCoherence[i], 0, 'f', 4).arg(m_results[i].coherenceZeroShift, 0, 'f', 4)
                    .arg(m_results[i].coherenceOptimal, 0, 'f', 4).arg(m_results[i].offsetX, 0, 'f', 2).arg(m_results[i].offsetY, 0, 'f', 2)
                    .arg(m_results[i].maxCorrelation, 0, 'f', 4)
                    .arg((m_results[i].maxCorrelation < 0.15 || m_results[i].coherenceZeroShift < 0.20) ? "true" : "false"),
                "registration.assessment.sample");
        }

        // 智能诊断判定：幅相不一致（当相干性偏低，且最大相关偏置处的相干性反而小于零偏置相干性时，说明互相关可能受到相位噪声伪匹配的干扰）
        bool hasMismatch = false;
        for (int i = 0; i < 5; ++i) {
            if (m_results[i].coherenceZeroShift < 0.25 && m_results[i].coherenceOptimal < m_results[i].coherenceZeroShift) {
                hasMismatch = true;
                break;
            }
        }

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
            if (hasMismatch) {
                desc += tr("\n提示：检测到部分区域幅相不一致（可能存在相位噪声匹配干扰），建议在相干性较稳定的区域手动重新选点。");
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
            if (hasMismatch) {
                desc += tr("\n提示：检测到部分区域幅相不一致（可能存在相位噪声匹配干扰），建议在相干性较稳定的区域手动重新选点。");
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
            if (hasMismatch) {
                desc += tr("\n提示：检测到部分区域幅相不一致（可能存在相位噪声匹配干扰），建议在相干性较稳定的区域手动重新选点。");
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
            QFont boldResultsFont = m_resultsTable->font();
            boldResultsFont.setBold(true);
            itemPostCoh->setFont(boldResultsFont);
            itemPostCoh->setForeground(Qt::green);
 
            // 3. 偏移处相干性(Max-Corr)
            auto* itemOptCoh = new QTableWidgetItem(QString::number(m_results[i].coherenceOptimal, 'f', 4));

            // 对低相干性区域（可能处于失相干区，如水体或密集植被）进行温和背景标记与 Tooltip 警告
            if (m_results[i].coherenceZeroShift < 0.25 || m_results[i].coherenceOptimal < 0.25) {
                QColor lowCohBg = isDark ? QColor(80, 60, 20) : QColor(254, 243, 199);
                itemPostCoh->setBackground(QBrush(lowCohBg));
                itemOptCoh->setBackground(QBrush(lowCohBg));
                
                QString tooltipText = tr("该采样点处于低相干区域，配准和相干性评估易受随机相位噪声干扰，评估数据仅供参考。");
                itemPostCoh->setToolTip(tooltipText);
                itemOptCoh->setToolTip(tooltipText);
            }
            
            m_resultsTable->setItem(i, 2, itemPostCoh);
            m_resultsTable->setItem(i, 3, itemOptCoh);
 
            // 4. 残余偏移
            auto* itemOffset = new QTableWidgetItem();
            if (m_results[i].maxCorrelation < 0.15 || m_results[i].coherenceZeroShift < 0.20) {
                itemOffset->setText(QString("(%1, %2)*").arg(m_results[i].offsetY).arg(m_results[i].offsetX));
                itemOffset->setToolTip(tr("当前区域评估可信度过低（互相关或相干性过低），测得偏移量不具有置信度，仅供参考。"));
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

        for (int row = 0; row < m_resultsTable->rowCount(); ++row) {
            for (int col = 0; col < m_resultsTable->columnCount(); ++col) {
                QTableWidgetItem* item = m_resultsTable->item(row, col);
                if (!item) {
                    continue;
                }
                const QString previousTooltip = item->toolTip();
                item->setToolTip(previousTooltip.isEmpty()
                    ? item->text()
                    : item->text() + "\n" + previousTooltip);
            }
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
        const int mode = m_visualModeCombo->currentData().toInt();
        if (mode == 2) {
            startFullCoherenceForCurrentSlave();
            return;
        }

        m_fullCoherenceStatusLabel->hide();
        m_imageView->clearOverlayRects();
        if (!m_hasResults) {
            m_imageView->setImage(QImage());
            return;
        }

        int row = m_resultsTable->currentRow();
        if (row < 0 || row >= 5) {
            m_imageView->setImage(QImage());
            return;
        }

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
    QLabel* m_fullCoherenceStatusLabel = nullptr;
    
    QFrame* m_statusCard;
    QLabel* m_statusCardTitle;
    QLabel* m_statusCardDesc;

    Point2D m_points[5];
    AlignmentResult m_results[5];
    double m_inputCoherence[5];
    bool m_hasResults;

    QFutureWatcher<EvalThreadResult> m_watcher;
    QFutureWatcher<FullCoherenceResult> m_fullCoherenceWatcher;
    QImage m_fullCoherenceImage;
    int m_fullCoherenceSlaveIndex = -1;
    int m_fullCoherenceSourceRows = 0;
    int m_fullCoherenceSourceCols = 0;
    int m_activeFullCoherenceSlaveIndex = -1;
    int m_requestedFullCoherenceSlaveIndex = -1;
};

// 接口实现
::QWidget* S1TopsBackGeocodingNode::createInterferometryWidget(::QWidget* parent)
{
    return new S1TopsRegistrationEvalWidget(this, parent);
}

} // namespace QtNodes

