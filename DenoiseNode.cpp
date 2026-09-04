#include "DenoiseNode.h"
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
#include <QCoreApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QTimer>
#include <QVector>
#include <QtConcurrent/QtConcurrent>
#include <Hdf5IO.h>
#include <algorithm>
#include <cmath>
#include <omp.h>
#include "QtNodes/internal/NodeDetailWindow.hpp"

namespace QtNodes {

namespace {

bool validateSnapCompatibleRestorePreflight(const QString& h5Path, QString* errorMessage)
{
    int method = 0;
    int schemaVersion = 0;
    int contractVersion = 0;
    int supportCount = 0;
    int window = 0;
    int nPad = -1;
    int gammaWindowRange = 0;
    int gammaWindowAzimuth = 0;
    std::string profile;
    std::string alphaSemantics;
    std::string spectralSmoothing;
    std::string overlapWindow;
    std::string supportSemantics;
    std::string fallbackSemantics;
    std::string gammaSemantics;
    std::string gammaAlgorithm;
    if (!NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_method"), method, errorMessage) || method != 4 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("phase_processing_schema_version"), schemaVersion, errorMessage) || schemaVersion != 2 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_mask_contract_version"), contractVersion, errorMessage) || contractVersion != 2 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_filter_support_count"), supportCount, errorMessage) || supportCount < 0 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_goldstein_win"), window, errorMessage) || window != 64 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_goldstein_npad"), nPad, errorMessage) || nPad != 0 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("complex_gamma_window_range"), gammaWindowRange, errorMessage) ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("complex_gamma_window_azimuth"), gammaWindowAzimuth, errorMessage) ||
        gammaWindowRange < 3 || gammaWindowAzimuth < 3 || gammaWindowRange % 2 == 0 || gammaWindowAzimuth % 2 == 0 ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_goldstein_profile"), profile, errorMessage) || profile != "GoldsteinSnapCompatibleV1" ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_goldstein_alpha_semantics"), alphaSemantics, errorMessage) ||
        alphaSemantics != "clamp_1_minus_mean_complex_gamma_0.2_1.0_v1" ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_goldstein_spectral_smoothing"), spectralSmoothing, errorMessage) ||
        spectralSmoothing != "mean_3x3_skip_zero_power_v1" ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_goldstein_overlap_window"), overlapWindow, errorMessage) ||
        overlapWindow != "separable_triangular_v1" ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_filter_support_semantics"), supportSemantics, errorMessage) ||
        supportSemantics != "original_valid_pixel_with_at_least_one_processed_fft_window_v2" ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_filter_fallback_semantics"), fallbackSemantics, errorMessage) ||
        fallbackSemantics != "input_phase_passthrough_when_unsupported_v2" ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("complex_gamma_semantics"), gammaSemantics, errorMessage) ||
        gammaSemantics != NodeUtils::CoherenceSemantics::kComplexGamma ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("complex_gamma_algorithm"), gammaAlgorithm, errorMessage) ||
        gammaAlgorithm != "corrected_multilooked_source_row_aware_v1") {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("GoldsteinSnapCompatibleV1 恢复预检的元数据或语义不匹配：%1").arg(h5Path);
        }
        return false;
    }

    int phaseRows = 0;
    int phaseCols = 0;
    if (!NodeUtils::probeH5DatasetMetadata(h5Path, QStringLiteral("phase"), &phaseRows, &phaseCols, errorMessage) ||
        phaseRows <= 0 || phaseCols <= 0) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("GoldsteinSnapCompatibleV1 恢复预检缺少非空 phase 栅格：%1").arg(h5Path);
        }
        return false;
    }

    const QStringList requiredRasters = {
        QStringLiteral("phase_valid_mask"),
        QStringLiteral("phase_valid_sample_count"),
        QStringLiteral("denoise_filter_support_mask"),
        QStringLiteral("interferogram_i"),
        QStringLiteral("interferogram_q"),
        QStringLiteral("complex_gamma"),
        QStringLiteral("complex_gamma_valid_mask"),
        QStringLiteral("complex_gamma_valid_sample_count")
    };
    for (const QString& dataset : requiredRasters) {
        int rows = 0;
        int cols = 0;
        if (!NodeUtils::probeH5DatasetMetadata(h5Path, dataset, &rows, &cols, errorMessage) ||
            rows != phaseRows || cols != phaseCols) {
            if (errorMessage && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("GoldsteinSnapCompatibleV1 恢复预检的栅格缺失、为空或尺寸不匹配：%1 (%2)")
                    .arg(h5Path, dataset);
            }
            return false;
        }
    }

    if (static_cast<qint64>(supportCount) > static_cast<qint64>(phaseRows) * phaseCols) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("GoldsteinSnapCompatibleV1 恢复预检的支持像元计数越界：%1").arg(h5Path);
        }
        return false;
    }
    return true;
}

} // namespace

DenoiseNode::DenoiseNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_methodCombo(nullptr)
    , m_prefilterWinLabel(nullptr)
    , m_prefilterWinEdit(nullptr)
    , m_slopeWinLabel(nullptr)
    , m_slopeWinEdit(nullptr)
    , m_goldsteinWinLabel(nullptr)
    , m_goldsteinWinEdit(nullptr)
    , m_nPadLabel(nullptr)
    , m_nPadEdit(nullptr)
    , m_alphaLabel(nullptr)
    , m_alphaEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_method(1) // default: Slope
    , m_prefilterWin(5)
    , m_slopeWin(5)
    , m_goldsteinWin(64)
    , m_nPad(16)
    , m_alpha(0.5)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    qRegisterMetaType<DenoiseFileResult>("DenoiseFileResult");
    setExecutionMode(ExecutionMode::Automatic);
}

DenoiseNode::~DenoiseNode()
{
    stopExecution();
}

unsigned int DenoiseNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType DenoiseNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "Imported File"};
    else
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool DenoiseNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString DenoiseNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入图像");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool DenoiseNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void DenoiseNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    const bool inputChanged = (!data || !m_inputData || m_inputData != data);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    if (inputChanged) {
        // Invalidate detail-view validation cache when the input connection changes
        m_validationCache.clear();
    }

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
    }

    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);
}

std::shared_ptr<NodeData> DenoiseNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_imageInfoData;
}

::QWidget* DenoiseNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject DenoiseNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["method"] = m_method;
    modelJson["prefilterWin"] = m_prefilterWinEdit ? m_prefilterWinEdit->text().toInt() : m_prefilterWin;
    modelJson["slopeWin"] = m_slopeWinEdit ? m_slopeWinEdit->text().toInt() : m_slopeWin;
    modelJson["goldsteinWin"] = m_goldsteinWinEdit ? m_goldsteinWinEdit->text().toInt() : m_goldsteinWin;
    modelJson["nPad"] = m_nPadEdit ? m_nPadEdit->text().toInt() : m_nPad;
    modelJson["alpha"] = m_alphaEdit ? m_alphaEdit->text().toDouble() : m_alpha;

    return modelJson;
}

void DenoiseNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vMethod = json["method"];
    if (!vMethod.isUndefined()) m_method = vMethod.toInt();

    QJsonValue vPrefilter = json["prefilterWin"];
    if (!vPrefilter.isUndefined()) m_prefilterWin = vPrefilter.toInt();

    QJsonValue vSlope = json["slopeWin"];
    if (!vSlope.isUndefined()) m_slopeWin = vSlope.toInt();

    QJsonValue vGoldstein = json["goldsteinWin"];
    if (!vGoldstein.isUndefined()) m_goldsteinWin = vGoldstein.toInt();

    QJsonValue vNPad = json["nPad"];
    if (!vNPad.isUndefined()) m_nPad = vNPad.toInt();

    QJsonValue vAlpha = json["alpha"];
    if (!vAlpha.isUndefined()) m_alpha = vAlpha.toDouble();

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_methodCombo) {
        m_methodCombo->setCurrentIndex(m_method - 1);
    }
    if (m_prefilterWinEdit) m_prefilterWinEdit->setText(QString::number(m_prefilterWin));
    if (m_slopeWinEdit) m_slopeWinEdit->setText(QString::number(m_slopeWin));
    if (m_goldsteinWinEdit) m_goldsteinWinEdit->setText(QString::number(m_goldsteinWin));
    if (m_nPadEdit) m_nPadEdit->setText(QString::number(m_nPad));
    if (m_alphaEdit) m_alphaEdit->setText(QString::number(m_alpha));

    onMethodChanged(m_method - 1);
}

std::vector<QString> DenoiseNode::processingInfo() const
{
    std::vector<QString> info;

    const QString outputNodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    info.push_back(QStringLiteral("输出节点：%1").arg(outputNodeName.isEmpty() ? QStringLiteral("未指定") : outputNodeName));

    const QString methodName = m_method == 1 ? QStringLiteral("斜坡自适应滤波")
        : m_method == 2 ? QStringLiteral("Goldstein 相位滤波")
        : m_method == 3 ? QStringLiteral("深度学习滤波")
        : QStringLiteral("Goldstein SNAP 兼容滤波");
    info.push_back(QStringLiteral("滤波方法：%1").arg(methodName));

    if (m_method == 1) {
        const int pre = m_prefilterWinEdit ? m_prefilterWinEdit->text().toInt() : m_prefilterWin;
        const int slop = m_slopeWinEdit ? m_slopeWinEdit->text().toInt() : m_slopeWin;
        info.push_back(QStringLiteral("预滤波窗口：%1，斜坡窗口：%2").arg(pre).arg(slop));
    } else if (m_method == 2) {
        const int gold = m_goldsteinWinEdit ? m_goldsteinWinEdit->text().toInt() : m_goldsteinWin;
        const int pad = m_nPadEdit ? m_nPadEdit->text().toInt() : m_nPad;
        const double alpha = m_alphaEdit ? m_alphaEdit->text().toDouble() : m_alpha;
        info.push_back(QStringLiteral("滤波窗口：%1，补零窗口：%2，滤波参数 alpha：%3")
            .arg(gold).arg(pad).arg(alpha, 0, 'f', 4));
	} else if (m_method == 4) {
		info.push_back(QStringLiteral("固定 64x64、步长 16、无补零；I/Q 与 complex_gamma 为必需输入"));
    }

    if (m_inputData) {
        info.push_back(QStringLiteral("输入影像数：%1 景").arg(m_inputData->filePaths().size()));
    } else {
        info.push_back(QStringLiteral("输入影像数：0 景"));
    }

    if (m_outputData) {
        info.push_back(QStringLiteral("输出影像数：%1 景").arg(m_outputData->filePaths().size()));
    } else {
        info.push_back(QStringLiteral("输出影像数：0 景"));
    }
    return info;
}

QString DenoiseNode::validationCacheKey() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        return QString();
    }
    if (!m_outputData || m_outputData->filePaths().isEmpty()) {
        return QString();
    }

    const QStringList inPaths = m_inputData->filePaths();
    const QStringList outPaths = m_outputData->filePaths();
    const int pre = m_prefilterWinEdit ? m_prefilterWinEdit->text().toInt() : m_prefilterWin;
    const int slop = m_slopeWinEdit ? m_slopeWinEdit->text().toInt() : m_slopeWin;
    const int gold = m_goldsteinWinEdit ? m_goldsteinWinEdit->text().toInt() : m_goldsteinWin;
    const int pad = m_nPadEdit ? m_nPadEdit->text().toInt() : m_nPad;
    const double alpha = m_alphaEdit ? m_alphaEdit->text().toDouble() : m_alpha;

    // File fingerprints (size + last modified time) so re-generated files with identical
    // paths/parameters invalidate stale cached validation results.
    const auto fileFingerprint = [](const QString& path) {
        const QFileInfo info(path);
        if (!info.exists()) {
            return QStringLiteral("missing");
        }
        return QString::number(info.size()) + QLatin1Char('_')
            + QString::number(info.lastModified().toMSecsSinceEpoch());
    };
    QString key;
    for (const QString& inPath : inPaths) {
        key += inPath + QLatin1Char('@') + fileFingerprint(inPath) + QLatin1Char('|');
    }
    key += QLatin1String("##");
    for (const QString& outPath : outPaths) {
        key += outPath + QLatin1Char('@') + fileFingerprint(outPath) + QLatin1Char('|');
    }
    key += QLatin1String("##") + QString::number(m_method)
        + QLatin1String("##") + QString::number(pre)
        + QLatin1String("##") + QString::number(slop)
        + QLatin1String("##") + QString::number(gold)
        + QLatin1String("##") + QString::number(pad)
        + QLatin1String("##") + QString::number(alpha, 'f', 6);
    return key;
}

bool DenoiseNode::loadValidationCache(const QString& key, ValidationResults& results) const
{
    const auto it = m_validationCache.constFind(key);
    if (it == m_validationCache.constEnd()) {
        return false;
    }
    results = it.value();
    return true;
}

void DenoiseNode::storeValidationCache(const QString& key, const ValidationResults& results)
{
    m_validationCache.insert(key, results);
}

void DenoiseNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void DenoiseNode::createWidget()
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

    const int labelWidth = 100;



    // 3. 滤波方法
    auto* methodLayout = new QHBoxLayout();
    QLabel* methodLabel = new QLabel("滤波方法");
    methodLabel->setFixedWidth(labelWidth);
    methodLayout->addWidget(methodLabel);
    m_methodCombo = new QComboBox();
    m_methodCombo->setEditable(false);
    m_methodCombo->addItem("斜坡自适应滤波");
    m_methodCombo->addItem("Goldstein 滤波");
    m_methodCombo->addItem("深度学习滤波");
	m_methodCombo->addItem("Goldstein SNAP 兼容滤波");
    m_methodCombo->setCurrentIndex(m_method - 1);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        int val = index + 1;
        if (m_method != val) {
            if (!confirmParameterChange()) {
                m_methodCombo->blockSignals(true);
                m_methodCombo->setCurrentIndex(m_method - 1);
                m_methodCombo->blockSignals(false);
                return;
            }
            m_method = val;
            onMethodChanged(index);
            invalidateNodeData();
        }
    });
    methodLayout->addWidget(m_methodCombo);
    layout->addLayout(methodLayout);

    // 4. Prefilter W (Slope)
    auto* prefilterLayout = new QHBoxLayout();
    m_prefilterWinLabel = new QLabel("预滤波窗口");
    m_prefilterWinLabel->setFixedWidth(labelWidth);
    prefilterLayout->addWidget(m_prefilterWinLabel);
    m_prefilterWinEdit = new QLineEdit();
    m_prefilterWinEdit->setText(QString::number(m_prefilterWin));
    connect(m_prefilterWinEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_prefilterWinEdit->text().toInt();
        if (m_prefilterWin != val) {
            if (!confirmParameterChange()) {
                m_prefilterWinEdit->setText(QString::number(m_prefilterWin));
                return;
            }
            m_prefilterWin = val;
            invalidateNodeData();
        }
    });
    prefilterLayout->addWidget(m_prefilterWinEdit);
    layout->addLayout(prefilterLayout);

    // 5. Slope W (Slope)
    auto* slopeLayout = new QHBoxLayout();
    m_slopeWinLabel = new QLabel("斜坡窗口");
    m_slopeWinLabel->setFixedWidth(labelWidth);
    slopeLayout->addWidget(m_slopeWinLabel);
    m_slopeWinEdit = new QLineEdit();
    m_slopeWinEdit->setText(QString::number(m_slopeWin));
    connect(m_slopeWinEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_slopeWinEdit->text().toInt();
        if (m_slopeWin != val) {
            if (!confirmParameterChange()) {
                m_slopeWinEdit->setText(QString::number(m_slopeWin));
                return;
            }
            m_slopeWin = val;
            invalidateNodeData();
        }
    });
    slopeLayout->addWidget(m_slopeWinEdit);
    layout->addLayout(slopeLayout);

    // 6. Goldstein Win (Goldstein)
    auto* goldsteinLayout = new QHBoxLayout();
    m_goldsteinWinLabel = new QLabel("滤波窗口尺寸");
    m_goldsteinWinLabel->setFixedWidth(labelWidth);
    goldsteinLayout->addWidget(m_goldsteinWinLabel);
    m_goldsteinWinEdit = new QLineEdit();
    m_goldsteinWinEdit->setText(QString::number(m_goldsteinWin));
    connect(m_goldsteinWinEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_goldsteinWinEdit->text().toInt();
        if (m_goldsteinWin != val) {
            if (!confirmParameterChange()) {
                m_goldsteinWinEdit->setText(QString::number(m_goldsteinWin));
                return;
            }
            m_goldsteinWin = val;
            invalidateNodeData();
        }
    });
    goldsteinLayout->addWidget(m_goldsteinWinEdit);
    layout->addLayout(goldsteinLayout);

    // 7. Padding Win (Goldstein)
    auto* nPadLayout = new QHBoxLayout();
    m_nPadLabel = new QLabel("补零窗口尺寸");
    m_nPadLabel->setFixedWidth(labelWidth);
    nPadLayout->addWidget(m_nPadLabel);
    m_nPadEdit = new QLineEdit();
    m_nPadEdit->setText(QString::number(m_nPad));
    connect(m_nPadEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_nPadEdit->text().toInt();
        if (m_nPad != val) {
            if (!confirmParameterChange()) {
                m_nPadEdit->setText(QString::number(m_nPad));
                return;
            }
            m_nPad = val;
            invalidateNodeData();
        }
    });
    nPadLayout->addWidget(m_nPadEdit);
    layout->addLayout(nPadLayout);

    // 8. Alpha (Goldstein)
    auto* alphaLayout = new QHBoxLayout();
    m_alphaLabel = new QLabel("滤波参数 alpha");
    m_alphaLabel->setFixedWidth(labelWidth);
    alphaLayout->addWidget(m_alphaLabel);
    m_alphaEdit = new QLineEdit();
    m_alphaEdit->setText(QString::number(m_alpha));
    connect(m_alphaEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        double val = m_alphaEdit->text().toDouble();
        if (qAbs(m_alpha - val) > 1e-6) {
            if (!confirmParameterChange()) {
                m_alphaEdit->setText(QString::number(m_alpha));
                return;
            }
            m_alpha = val;
            invalidateNodeData();
        }
    });
    alphaLayout->addWidget(m_alphaEdit);
    layout->addLayout(alphaLayout);

    // 9. 目标节点
    auto* outputLayout = new QHBoxLayout();
    QLabel* outputLabel = new QLabel("目标节点");
    outputLabel->setFixedWidth(labelWidth);
    outputLayout->addWidget(outputLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
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
    outputLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(outputLayout);

    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    onMethodChanged(m_method - 1);
}

void DenoiseNode::onMethodChanged(int index)
{
    bool isSlope = (index == 0);
    bool isGoldstein = (index == 1);

    if (m_prefilterWinLabel) m_prefilterWinLabel->setVisible(isSlope);
    if (m_prefilterWinEdit) m_prefilterWinEdit->setVisible(isSlope);
    if (m_slopeWinLabel) m_slopeWinLabel->setVisible(isSlope);
    if (m_slopeWinEdit) m_slopeWinEdit->setVisible(isSlope);

    if (m_goldsteinWinLabel) m_goldsteinWinLabel->setVisible(isGoldstein);
    if (m_goldsteinWinEdit) m_goldsteinWinEdit->setVisible(isGoldstein);
    if (m_nPadLabel) m_nPadLabel->setVisible(isGoldstein);
    if (m_nPadEdit) m_nPadEdit->setVisible(isGoldstein);
    if (m_alphaLabel) m_alphaLabel->setVisible(isGoldstein);
    if (m_alphaEdit) m_alphaEdit->setVisible(isGoldstein);

    updateWidgetSize();
}

void DenoiseNode::updateWidgetSize()
{
    if (_widget)
    {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString DenoiseNode::generateDefaultOutputName() const
{
    if (m_inputData) {
        return m_inputData->nodeName() + "_Denoised";
    }
    return "Denoised_Phase";
}

bool DenoiseNode::validateInputs() const
{
    if (projectName().isEmpty()) {
        return false;
    }
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        return false;
    }

    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstNode.isEmpty()) {
        return false;
    }

    // Check parameters
    if (m_method == 1) {
        int pre = m_prefilterWinEdit ? m_prefilterWinEdit->text().toInt() : m_prefilterWin;
        int slop = m_slopeWinEdit ? m_slopeWinEdit->text().toInt() : m_slopeWin;
        if (pre <= 0 || slop <= 0 || pre % 2 == 0 || slop % 2 == 0) {
            return false;
        }
    } else if (m_method == 2) {
        int gold = m_goldsteinWinEdit ? m_goldsteinWinEdit->text().toInt() : m_goldsteinWin;
        int pad = m_nPadEdit ? m_nPadEdit->text().toInt() : m_nPad;
        double alpha = m_alphaEdit ? m_alphaEdit->text().toDouble() : m_alpha;
        if ((gold & (gold - 1)) != 0 || (pad & (pad - 1)) != 0 || gold <= 1 || pad <= 1 || alpha < 0 || alpha > 1) {
            return false;
        }
	} else if (m_method == 4) {
		// Parameters are intentionally fixed to SNAP's compatibility profile.
		return true;
    }

    return true;
}

ProductInputContract DenoiseNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("denoise.input.interferogram");
    contract.allowedProductTypes = QStringList() << QStringLiteral("interferogram");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract DenoiseNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0 ? QStringLiteral("denoise.output.filtered_interferogram")
                                         : QStringLiteral("denoise.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList{QStringLiteral("filtered_interferogram")} : QStringList{QStringLiteral("preview")};
    return contract;
}

bool DenoiseNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    QString dstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text().trimmed();

    QString savePath = projectPath();

    int pre = m_prefilterWinEdit ? m_prefilterWinEdit->text().toInt() : m_prefilterWin;
    int slop = m_slopeWinEdit ? m_slopeWinEdit->text().toInt() : m_slopeWin;
    int gold = m_goldsteinWinEdit ? m_goldsteinWinEdit->text().toInt() : m_goldsteinWin;
    int pad = m_nPadEdit ? m_nPadEdit->text().toInt() : m_nPad;
    m_preparedAlpha = m_alphaEdit ? m_alphaEdit->text().toDouble() : m_alpha;

    m_preparedPara.clear();
    m_preparedPara.append(pre);
    m_preparedPara.append(slop);
    m_preparedPara.append(gold);
    m_preparedPara.append(pad);
    m_preparedPara.append(m_method);

    m_preparedDstNode = dstNode;

    m_preparedOutputPaths.clear();
    QStringList srcPaths = m_inputData->filePaths();
    QString identityError;
    if (!NodeUtils::validateH5Identities(srcPaths, m_inputData->physicalProductDescriptor(), &identityError)) {
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }
    for (const QString& srcPath : srcPaths) {
        QFileInfo fi(srcPath);
        QString changeName = fi.baseName() + "_denoised";
        m_preparedOutputPaths.append(savePath + "/" + dstNode + "/" + changeName + ".h5");
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), dstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void DenoiseNode::executeProcessing()
{
    QString dstNode = m_preparedDstNode;
    QString savePath = projectPath();
    QStringList phasePaths = m_inputData->filePaths();
    QStringList phaseNames;
    for (const QString& phasePath : phasePaths) {
        phaseNames.append(QFileInfo(phasePath).baseName());
    }

    InSARLogManager::LogDebug("DenoiseNode",
        QStringLiteral("executeProcessing started: method=%1, alpha=%2, inputCount=%3")
            .arg(m_method).arg(m_preparedAlpha, 0, 'f', 2).arg(phasePaths.size()),
        "lifecycle.execute_processing");

    setProgress(0);

    QList<int> para = m_preparedPara;
    double alpha = m_preparedAlpha;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = dstNode;
        
        m_outputNodeNameEdit->setEnabled(true);
        m_methodCombo->setEnabled(true);
        onMethodChanged(m_method - 1);

        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) {
            finishExecution();
        } else {
            setState(ExecutionState::Error);
        }
        return;
    }

    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(savePath, dstNode, m_preparedOutputPaths,
                                           phasePaths, m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> descriptorProvenance;
    descriptorProvenance.insert(QStringLiteral("producer"), name());
    descriptorProvenance.insert(QStringLiteral("output_port"),
                                QStringLiteral("denoise.output.filtered_interferogram"));
    if (!NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction, ProductDescriptor::create(
                QStringLiteral("filtered_interferogram"), QStringLiteral("sat-explorer-product"), 1,
                ProductState::Committed, name(), descriptorProvenance), &transactionError)) {
        onError(transactionError);
        return;
    }
    m_pendingDenoiseResults.clear();
    m_xmlDirty = false;
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    m_thread = new QThread();
    m_workerThread = new DenoiseWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &DenoiseNode::startDenoise, m_workerThread, &DenoiseWorker::Denoise);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, [this, para, alpha, savePath, stagingNode, phaseNames, phasePaths]() {
        Q_EMIT startDenoise(para, alpha, savePath, stagingNode, phaseNames, phasePaths);
    });
    connect(m_workerThread, &DenoiseWorker::denoiseGenerated, this, &DenoiseNode::onDenoiseGenerated);
    connect(m_workerThread, &DenoiseWorker::updateProcess, this, &DenoiseNode::onProgressUpdate);
    connect(m_workerThread, &DenoiseWorker::endProcess, this, &DenoiseNode::onProcessingFinished);
    connect(m_workerThread, &DenoiseWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &DenoiseWorker::cancelled, this, &DenoiseNode::onCancelled);
    connect(m_workerThread, &DenoiseWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &DenoiseWorker::errorProcess, this, &DenoiseNode::onError);
    connect(m_workerThread, &DenoiseWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &DenoiseWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &DenoiseWorker::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Dynamic recovery of Running state for Automatic execution mode (SOP rule 139)
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
            setState(ExecutionState::Running);
    });

    // Disable inputs during execution
    m_outputNodeNameEdit->setEnabled(false);
    m_methodCombo->setEnabled(false);
    if (m_prefilterWinEdit) m_prefilterWinEdit->setEnabled(false);
    if (m_slopeWinEdit) m_slopeWinEdit->setEnabled(false);
    if (m_goldsteinWinEdit) m_goldsteinWinEdit->setEnabled(false);
    if (m_nPadEdit) m_nPadEdit->setEnabled(false);
    if (m_alphaEdit) m_alphaEdit->setEnabled(false);

    deferAutomaticCompletion();
    m_thread->start();
}

void DenoiseNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void DenoiseNode::onProcessingFinished()
{
    const QString dstNode = m_preparedDstNode;
    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;
    QList<DenoiseFileResult> committedResults;

    // Clean up worker thread
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

    QString transactionError;
    if (!projectXml()) {
        onError(QStringLiteral("Project XML context is unavailable for denoise output commit."));
        return;
    }
    if (!NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("phase"), &transactionError)) {
        onError(transactionError);
        return;
    }
    QStringList workerOutputPaths;
    for (const DenoiseFileResult& result : m_pendingDenoiseResults) {
        workerOutputPaths.append(result.filterPath);
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
    for (DenoiseFileResult result : m_pendingDenoiseResults) {
        const QString fileName = QFileInfo(result.filterPath).fileName();
        result.fileName = dstNode;
        result.filterPath = QDir(projectPath() + "/" + dstNode).absoluteFilePath(fileName);
        result.relativePath = QStringLiteral("/%1/%2").arg(dstNode, fileName);
        commitDenoiseResult(result);
        committedResults.append(result);
    }
    if (!m_xmlDirty) {
        onError(QStringLiteral("Denoise output metadata was not produced."));
        return;
    }
    if (!NodeUtils::saveProjectXmlAtomically(
            projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }
    if (!NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    for (const DenoiseFileResult& result : committedResults) {
        publishDenoiseResultToProjectTree(result);
    }
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }
    for (const QString& h5Path : h5Paths) {
        const QString baseName = QFileInfo(h5Path).baseName();
        jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    // Output successfully committed; invalidate stale detail-view validation results.
    m_validationCache.clear();

    if (!h5Paths.isEmpty())
    {
        startPreviewGeneration(h5Paths, jpgPaths, types, h5Paths, jpgPaths, true);
    }
    else
    {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);

        m_outputNodeNameEdit->setEnabled(true);
        m_methodCombo->setEnabled(true);
        if (m_prefilterWinEdit) m_prefilterWinEdit->setEnabled(true);
        if (m_slopeWinEdit) m_slopeWinEdit->setEnabled(true);
        if (m_goldsteinWinEdit) m_goldsteinWinEdit->setEnabled(true);
        if (m_nPadEdit) m_nPadEdit->setEnabled(true);
        if (m_alphaEdit) m_alphaEdit->setEnabled(true);
        onMethodChanged(m_method - 1);

        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("DenoiseNode", "executeProcessing completed (empty output list).");
        finishExecution();
    }
}

void DenoiseNode::onError(const QString& error)
{
    InSARLogManager::LogError("DenoiseNode", QString("Execution failed: %1").arg(error));
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    cleanUpThreadAndWorker();

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_prefilterWinEdit) m_prefilterWinEdit->setEnabled(true);
    if (m_slopeWinEdit) m_slopeWinEdit->setEnabled(true);
    if (m_goldsteinWinEdit) m_goldsteinWinEdit->setEnabled(true);
    if (m_nPadEdit) m_nPadEdit->setEnabled(true);
    if (m_alphaEdit) m_alphaEdit->setEnabled(true);
    onMethodChanged(m_method - 1);

    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    setLastErrorMessage(error);
    setState(ExecutionState::Error);
}



bool DenoiseNode::validateAndRestoreOutput()
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
	if (m_method == 4) {
		for (const QString& h5Path : h5Paths) {
			if (!validateSnapCompatibleRestorePreflight(h5Path, &identityError)) {
				return false;
			}
		}
	}

    QStringList expectedJpgPaths;
    QStringList types;
    for (const QString& h5Path : h5Paths) {
        QString baseName = QFileInfo(h5Path).baseName();
        expectedJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);

    // Remedy missing JPG previews in background
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

void DenoiseNode::startPreviewGeneration(const QStringList& h5Paths,
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
            if (NodeUtils::isJpgPreviewCurrent(resultH5Paths[i], resultJpgPaths[i])) {
                currentJpgPaths.append(resultJpgPaths[i]);
            }
        }
        if (completeExecution) {
            if (discardObsoleteAutomaticExecution()) return;
            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
            setOutputData(1, m_imageInfoData);
            m_outputNodeNameEdit->setEnabled(true);
            m_methodCombo->setEnabled(true);
            if (m_prefilterWinEdit) m_prefilterWinEdit->setEnabled(true);
            if (m_slopeWinEdit) m_slopeWinEdit->setEnabled(true);
            if (m_goldsteinWinEdit) m_goldsteinWinEdit->setEnabled(true);
            if (m_nPadEdit) m_nPadEdit->setEnabled(true);
            if (m_alphaEdit) m_alphaEdit->setEnabled(true);
            onMethodChanged(m_method - 1);
            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("DenoiseNode", "executeProcessing completed.");
            finishExecution();
        } else {
            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
            InSARLogManager::LogInfo("DenoiseNode", "validateAndRestoreOutput background rendering completed.");
        }
    });
    m_remedyWatcher.setFuture(QtConcurrent::run([h5Paths, generatedJpgPaths, types]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], generatedJpgPaths[i], types[i]);
        }
    }));
}

QStringList DenoiseNode::previewImagePaths() const
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

QStandardItemModel* DenoiseNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString DenoiseNode::projectPath() const
{
    IApplicationInterface* iface = nullptr;
    if (_widget) iface = NodeUtils::getProjectContext(_widget);
    if (!iface) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* mainWin = qobject_cast<MainWindow*>(w)) {
                if (mainWin->workspaceUI()) { iface = mainWin->workspaceUI(); break; }
                if (mainWin->interfaceManager()) { iface = mainWin->interfaceManager()->currentInterface(); if (iface) break; }
            }
        }
    }
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString DenoiseNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* DenoiseNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void DenoiseNode::execute()
{
    executeProcessing();
}

void DenoiseNode::stopExecution()
{
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
    cleanUpThreadAndWorker();
}

void DenoiseNode::cleanUpThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning())
        thread->quit();
}

void DenoiseNode::onCancelled()
{
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
    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
}

void DenoiseNode::processAutomatically()
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

void DenoiseNode::onDenoiseGenerated(const DenoiseFileResult& result)
{
    m_pendingDenoiseResults.append(result);
}

void DenoiseNode::commitDenoiseResult(const DenoiseFileResult& result)
{
    XMLFile* xml = projectXml();
    if (xml && m_preparedPara.size() >= 5) {
        const int method = m_preparedPara.at(4);
        if (method == 1) {
            xml->XMLFile_add_denoise(result.fileName.toStdString().c_str(), result.filterName.toStdString().c_str(),
                result.relativePath.toStdString().c_str(), result.offsetRow, result.offsetCol, "Slope",
                m_preparedPara.at(1), m_preparedPara.at(0), 0, 0, 0, "", "", "");
        } else if (method == 2) {
            xml->XMLFile_add_denoise(result.fileName.toStdString().c_str(), result.filterName.toStdString().c_str(),
                result.relativePath.toStdString().c_str(), result.offsetRow, result.offsetCol, "Goldstein",
                0, 0, m_preparedPara.at(2), m_preparedPara.at(3), m_preparedAlpha, "", "", "");
		} else if (method == 4) {
			xml->XMLFile_add_denoise(result.fileName.toStdString().c_str(), result.filterName.toStdString().c_str(),
				result.relativePath.toStdString().c_str(), result.offsetRow, result.offsetCol, "GoldsteinSnapCompatibleV1",
				0, 0, 64, 0, 0, "", "", "");
        } else if (method == 3) {
            const QString applicationPath = QCoreApplication::applicationDirPath();
            const QString modelPath = applicationPath + "\\other\\net.pt";
            const QString outputPath = QDir::toNativeSeparators(projectPath() + "/" + result.fileName);
            xml->XMLFile_add_denoise(result.fileName.toStdString().c_str(), result.filterName.toStdString().c_str(),
                result.relativePath.toStdString().c_str(), result.offsetRow, result.offsetCol, "DL",
                0, 0, 0, 0, 0, applicationPath.toStdString().c_str(), modelPath.toStdString().c_str(),
                outputPath.toStdString().c_str());
        }
        m_xmlDirty = true;
    }
}

void DenoiseNode::publishDenoiseResultToProjectTree(const DenoiseFileResult& result)
{
    QStandardItemModel* model = projectModel();
    if (!model) return;

    QList<QStandardItem*> foundProjects = model->findItems(projectName());
    if (foundProjects.isEmpty()) return;
    QStandardItem* project = foundProjects[0];

    QStandardItem* denoiseNode = NodeUtils::findOrCreateProjectNode(project, result.fileName, "phase-1.0");
    if (denoiseNode) {
        denoiseNode->setToolTip(projectName());
        QStandardItem* itemImg = nullptr;
        for (int j = 0; j < denoiseNode->rowCount(); j++) {
            if (denoiseNode->child(j, 0)->text() == result.filterName) {
                itemImg = denoiseNode->child(j, 0);
                break;
            }
        }
        if (!itemImg) {
            QStandardItem* filterNameItem = new QStandardItem(result.filterName);
            filterNameItem->setToolTip("phase");
            QStandardItem* filterPathItem = new QStandardItem(result.filterPath);
            filterNameItem->setIcon(QIcon(IMAGEDATA_ICON));
            denoiseNode->appendRow(filterNameItem);
            denoiseNode->setChild(denoiseNode->rowCount() - 1, 1, filterPathItem);
        } else {
            denoiseNode->setChild(itemImg->row(), 1, new QStandardItem(result.filterPath));
        }
    }

}

// ==========================================
// DenoiseValidationWidget Implementation
// ==========================================

class DenoiseValidationWidget : public BaseValidationWidget
{
public:
    DenoiseValidationWidget(DenoiseNode* node, QWidget* parent)
        : BaseValidationWidget(node, parent)
        , m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }

    ~DenoiseValidationWidget() override = default;

private:
    void setupUI()
    {
        setupBaseUI(QObject::tr("正在验证数据中..."),
                    QObject::tr("正在读取输入与输出 H5 数据以进行参数及特征值校验。"),
                    QObject::tr("特征值分析"), QString(), false, true);

        m_lblDiffMean = createFeatureLabel();
        m_lblDiffStd = createFeatureLabel();
        m_lblDiffResultant = createFeatureLabel();
        m_lblGradientSummary = createFeatureLabel();
        m_lblResidueCountSummary = createFeatureLabel();
        m_lblResidueSummary = createFeatureLabel();

        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差圆均值 (rad):")), m_lblDiffMean);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差圆标准差 (rad):")), m_lblDiffStd);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差集中度 R (0-1):")), m_lblDiffResultant);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("抽样缠绕相位梯度 RMS (输入 -> 输出):")), m_lblGradientSummary);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("抽样正/负/总残差单元数 (plaquette，输入 -> 输出):")), m_lblResidueCountSummary);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("抽样残差单元密度 (总数/有效 2 x 2 单元，输入 -> 输出):")), m_lblResidueSummary);

        auto* fullValidationLayout = new QHBoxLayout();
        fullValidationLayout->setContentsMargins(0, 0, 0, 0);
        fullValidationLayout->setSpacing(8);
        m_fullValidationButton = new QPushButton(QObject::tr("执行完整验证（可能耗时较长）"), m_statusCard);
        m_fullValidationButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_fullValidationButton->setToolTip(QObject::tr("完整验证会读取全量 I/Q，并逐像元检查滤波支持、complex_gamma 与掩膜合同。"));
        m_fullValidationProgressLabel = new QLabel(m_statusCard);
        const QString fullValidationProgressText = QObject::tr("完整验证中...");
        m_fullValidationProgressLabel->setText(fullValidationProgressText);
        m_fullValidationProgressLabel->setFixedWidth(120);
        m_fullValidationProgressLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_fullValidationProgressLabel->setStyleSheet(
            "font-size: 12px; color: #2563EB; background: transparent; border: none; padding: 0;");
        m_fullValidationProgressLabel->hide();
        fullValidationLayout->addWidget(m_fullValidationButton, 0, Qt::AlignLeft);
        fullValidationLayout->addWidget(m_fullValidationProgressLabel, 0, Qt::AlignVCenter);
        fullValidationLayout->addStretch(1);
        if (auto* statusLayout = qobject_cast<QVBoxLayout*>(m_statusCard->layout())) {
            statusLayout->addLayout(fullValidationLayout);
        }
        connect(m_fullValidationButton, &QPushButton::clicked, this, [this]() {
            m_requestedDeepValidation = true;
            startAsyncValidation();
        });
    }

    void startAsyncValidation() override
    {
        const bool deepValidation = m_requestedDeepValidation;
        m_requestedDeepValidation = false;
        const quint64 validationEpoch = ++m_validationEpoch;
        if (m_cancelToken) {
            m_cancelToken->store(true);
        }
        m_cancelToken = std::make_shared<std::atomic_bool>(false);
        auto cancelToken = m_cancelToken;

        m_isTimedOut = false;
        m_loadingOverlay->stopLoading();
        if (m_fullValidationProgressLabel) {
            m_fullValidationProgressLabel->setVisible(deepValidation);
            if (deepValidation) {
                m_statusDesc->setText(QObject::tr("正在执行完整 H5 合同验证，并计算特征值。"));
            }
        }
        if (m_fullValidationButton) {
            m_fullValidationButton->setEnabled(false);
        }

        // 1. Quick checks: If output not complete or inputs missing
        if (m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(QObject::tr("验证未通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未找到输入或输出文件的元数据，无法进行比对。"));

            m_compTable->clearComparison();
            m_compTable->setEnabled(false);

            m_lblDiffMean->setText(QObject::tr("未执行"));
            m_lblDiffStd->setText(QObject::tr("未执行"));
            m_lblDiffResultant->setText(QObject::tr("未执行"));
            m_lblGradientSummary->setText(QObject::tr("未执行"));
            m_lblResidueCountSummary->setText(QObject::tr("未执行"));
            m_lblResidueSummary->setText(QObject::tr("未执行"));
            if (m_fullValidationProgressLabel) m_fullValidationProgressLabel->hide();
            if (m_fullValidationButton) m_fullValidationButton->setEnabled(true);

            return;
        }

        // Output paths check
        auto outData = std::dynamic_pointer_cast<ImportedFileData>(m_node->outData(0));
        auto inData = std::dynamic_pointer_cast<ImportedFileData>(m_node->getInputData(0));
        if (!outData || outData->filePaths().isEmpty() || !inData || inData->filePaths().isEmpty()) {
            m_statusTitle->setText(QObject::tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未找到输入或输出文件的元数据，无法进行比对。"));
            if (m_fullValidationProgressLabel) m_fullValidationProgressLabel->hide();
            if (m_fullValidationButton) m_fullValidationButton->setEnabled(true);
            return;
        }

        const QStringList inPaths = inData->filePaths();
        const QStringList outPaths = outData->filePaths();

        // The default Detail View check is bounded. Full-grid validation is an
        // explicit user action and receives a size-scaled timeout.
        qint64 validationBytes = 0;
        for (const QString& path : inPaths) validationBytes += QFileInfo(path).size();
        for (const QString& path : outPaths) validationBytes += QFileInfo(path).size();
        constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;
        constexpr qint64 kBaseTimeoutMs = 120000;
        constexpr qint64 kTimeoutPerGiBMs = 30000;
        constexpr qint64 kMaximumTimeoutMs = 30LL * 60LL * 1000LL;
        const qint64 sizeBasedTimeoutMs = kBaseTimeoutMs
            + ((validationBytes + kGiB - 1) / kGiB) * kTimeoutPerGiBMs;
        const int timeoutMs = deepValidation
            ? static_cast<int>(std::min(kMaximumTimeoutMs,
                std::max<qint64>(sizeBasedTimeoutMs, inPaths.size() * 30000LL)))
            : 60000;
        if (!deepValidation) {
            m_loadingOverlay->startLoading(
                QObject::tr("正在执行快速 H5 合同抽样验证..."), timeoutMs);
        }

        // Settings to compare (same value source as save())
        const QJsonObject nodeSave = m_node->save();
        const int expMethod = nodeSave["method"].toInt(1);
        const int expPrefilter = nodeSave["prefilterWin"].toInt(5);
        const int expSlopeWindow = nodeSave["slopeWin"].toInt(5);
        const int expGoldsteinWin = nodeSave["goldsteinWin"].toInt(64);
        const int expNPad = nodeSave["nPad"].toInt(16);
        const double expAlpha = nodeSave["alpha"].toDouble(0.5);

        // Reuse cached aggregated results when inputs/outputs/parameters unchanged
        const QString cacheKey = m_node->validationCacheKey()
            + (deepValidation ? QStringLiteral("|deep") : QStringLiteral("|quick"));
        ValidationResults cached;
        if (!cacheKey.isEmpty() && m_node->loadValidationCache(cacheKey, cached)) {
            applyValidationResults(cached);
            return;
        }

        // Run validation in background
        QFuture<ValidationResults> future = QtConcurrent::run([inPaths, outPaths, expMethod, expPrefilter,
                                                               expSlopeWindow, expGoldsteinWin, expNPad, expAlpha,
                                                               deepValidation, cancelToken]() {
            ValidationResults res;
            res.quickValidation = !deepValidation;
            res.expectedMethod = expMethod;
            res.expectedPrefilter = expPrefilter;
            res.expectedSlopeWindow = expSlopeWindow;
            res.expectedGoldsteinWin = expGoldsteinWin;
            res.expectedNPad = expNPad;
            res.expectedAlpha = expAlpha;
            res.imagePairCount = inPaths.size();

            if (inPaths.size() != outPaths.size()) {
                res.success = false;
                res.errorMsg = QObject::tr("输入与输出影像数量不一致（输入 %1 / 输出 %2），无法逐对校验。")
                    .arg(inPaths.size()).arg(outPaths.size());
                return res;
            }
            if (expMethod == 4 && deepValidation) {
				for (const QString& outH5 : outPaths) {
					int method = 0;
					std::string profile;
					QString validationError;
					if (!NodeUtils::readScalarFromH5(outH5, "denoise_method", method) || method != 4 ||
						!NodeUtils::readStringFromH5(outH5, "denoise_goldstein_profile", profile) ||
						profile != "GoldsteinSnapCompatibleV1" ||
						!NodeUtils::validateDenoiseFilterSupportContract(outH5, &validationError)) {
						res.success = false;
						res.errorMsg = validationError.isEmpty()
							? QObject::tr("SNAP 兼容 Goldstein 输出合同缺失或不匹配：%1").arg(outH5)
							: validationError;
						return res;
					}
				}
			}

            const auto validateSnapCompatibleQuick = [cancelToken](const QString& h5Path, QString* error) {
                constexpr int kSampleSide = 32;
                int phaseRows = 0, phaseCols = 0;
                const auto probe = [&h5Path, &phaseRows, &phaseCols, error](const QString& dataset,
                    int expectedType, bool isPhase = false) {
                    int rows = 0, cols = 0;
                    if (!NodeUtils::probeH5DatasetMetadata(h5Path, dataset, &rows, &cols, error) ||
                        rows <= 0 || cols <= 0 || (isPhase ? false : (rows != phaseRows || cols != phaseCols))) {
                        if (error && error->isEmpty()) {
                            *error = QObject::tr("快速验证的数据集尺寸缺失或不匹配：%1 (%2)").arg(h5Path, dataset);
                        }
                        return false;
                    }
                    Q_UNUSED(expectedType);
                    if (isPhase) {
                        phaseRows = rows;
                        phaseCols = cols;
                    }
                    return true;
                };

                int schemaVersion = 0, contractVersion = 0, supportCount = 0, window = 0, nPad = -1;
                int gammaWindowRange = 0, gammaWindowAzimuth = 0;
                std::string profile, alphaSemantics, smoothing, overlapWindow;
                std::string supportSemantics, fallbackSemantics, gammaSemantics, gammaAlgorithm;
                if (!NodeUtils::readScalarFromH5(h5Path, QStringLiteral("phase_processing_schema_version"), schemaVersion, error) ||
                    schemaVersion != 2 ||
                    !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_mask_contract_version"), contractVersion, error) ||
                    contractVersion != 2 ||
                    !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_filter_support_count"), supportCount, error) ||
                    supportCount < 0 ||
                    !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_goldstein_win"), window, error) || window != 64 ||
                    !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("denoise_goldstein_npad"), nPad, error) || nPad != 0 ||
                    !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("complex_gamma_window_range"), gammaWindowRange, error) ||
                    !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("complex_gamma_window_azimuth"), gammaWindowAzimuth, error) ||
                    gammaWindowRange < 3 || gammaWindowAzimuth < 3 || gammaWindowRange % 2 == 0 || gammaWindowAzimuth % 2 == 0 ||
                    !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_goldstein_profile"), profile, error) || profile != "GoldsteinSnapCompatibleV1" ||
                    !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_goldstein_alpha_semantics"), alphaSemantics, error) ||
                    alphaSemantics != "clamp_1_minus_mean_complex_gamma_0.2_1.0_v1" ||
                    !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_goldstein_spectral_smoothing"), smoothing, error) ||
                    smoothing != "mean_3x3_skip_zero_power_v1" ||
                    !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_goldstein_overlap_window"), overlapWindow, error) ||
                    overlapWindow != "separable_triangular_v1" ||
                    !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_filter_support_semantics"), supportSemantics, error) ||
                    supportSemantics != "original_valid_pixel_with_at_least_one_processed_fft_window_v2" ||
                    !NodeUtils::readStringFromH5(h5Path, QStringLiteral("denoise_filter_fallback_semantics"), fallbackSemantics, error) ||
                    fallbackSemantics != "input_phase_passthrough_when_unsupported_v2" ||
                    !NodeUtils::readStringFromH5(h5Path, QStringLiteral("complex_gamma_semantics"), gammaSemantics, error) ||
                    gammaSemantics != NodeUtils::CoherenceSemantics::kComplexGamma ||
                    !NodeUtils::readStringFromH5(h5Path, QStringLiteral("complex_gamma_algorithm"), gammaAlgorithm, error) ||
                    gammaAlgorithm != "corrected_multilooked_source_row_aware_v1" ||
                    !probe(QStringLiteral("phase"), CV_64F, true) ||
                    !probe(QStringLiteral("phase_valid_mask"), CV_8U) ||
                    !probe(QStringLiteral("denoise_filter_support_mask"), CV_8U) ||
                    !probe(QStringLiteral("interferogram_i"), CV_32F) ||
                    !probe(QStringLiteral("interferogram_q"), CV_32F) ||
                    !probe(QStringLiteral("complex_gamma"), CV_64F) ||
                    !probe(QStringLiteral("complex_gamma_valid_mask"), CV_8U) ||
                    !probe(QStringLiteral("complex_gamma_valid_sample_count"), CV_32S) ||
                    static_cast<qint64>(supportCount) > static_cast<qint64>(phaseRows) * phaseCols) {
                    if (error && error->isEmpty()) *error = QObject::tr("SNAP 兼容 Goldstein 快速合同检查失败：%1").arg(h5Path);
                    return false;
                }

                const QByteArray utf8Path = h5Path.toUtf8();
                Hdf5IO::ReadSession* session = Hdf5IO::openReadSession(utf8Path.constData());
                if (!session) {
                    if (error) *error = QObject::tr("无法打开 H5 进行快速抽样：%1").arg(h5Path);
                    return false;
                }
                const std::unique_ptr<Hdf5IO::ReadSession, void(*)(Hdf5IO::ReadSession*)> sessionGuard(
                    session, Hdf5IO::closeReadSession);
                const QVector<QPair<int, int>> origins = {
                    { 0, 0 }, { 0, std::max(0, phaseCols - kSampleSide) },
                    { std::max(0, phaseRows - kSampleSide), 0 },
                    { std::max(0, phaseRows - kSampleSide), std::max(0, phaseCols - kSampleSide) },
                    { std::max(0, phaseRows / 2 - kSampleSide / 2), std::max(0, phaseCols / 2 - kSampleSide / 2) }
                };
                for (const auto& origin : origins) {
                    if (cancelToken && cancelToken->load()) return false;
                    const int rows = std::min(kSampleSide, phaseRows - origin.first);
                    const int cols = std::min(kSampleSide, phaseCols - origin.second);
                    cv::Mat phase, validMask, supportMask, filteredI, filteredQ, gamma, gammaMask, gammaCount;
                    if (Hdf5IO::readSubarray(session, "phase", origin.first, origin.second, rows, cols, phase) != 0 ||
                        Hdf5IO::readSubarray(session, "phase_valid_mask", origin.first, origin.second, rows, cols, validMask) != 0 ||
                        Hdf5IO::readSubarray(session, "denoise_filter_support_mask", origin.first, origin.second, rows, cols, supportMask) != 0 ||
                        Hdf5IO::readSubarray(session, "interferogram_i", origin.first, origin.second, rows, cols, filteredI) != 0 ||
                        Hdf5IO::readSubarray(session, "interferogram_q", origin.first, origin.second, rows, cols, filteredQ) != 0 ||
                        Hdf5IO::readSubarray(session, "complex_gamma", origin.first, origin.second, rows, cols, gamma) != 0 ||
                        Hdf5IO::readSubarray(session, "complex_gamma_valid_mask", origin.first, origin.second, rows, cols, gammaMask) != 0 ||
                        Hdf5IO::readSubarray(session, "complex_gamma_valid_sample_count", origin.first, origin.second, rows, cols, gammaCount) != 0 ||
                        phase.type() != CV_64F || validMask.type() != CV_8U || supportMask.type() != CV_8U ||
                        filteredI.type() != CV_32F || filteredQ.type() != CV_32F || gamma.type() != CV_64F ||
                        gammaMask.type() != CV_8U || gammaCount.type() != CV_32S) {
                        if (error) *error = QObject::tr("无法读取 SNAP 兼容 Goldstein 快速抽样块：%1").arg(h5Path);
                        return false;
                    }
                    for (int row = 0; row < rows; ++row) {
                        const double* phaseRow = phase.ptr<double>(row);
                        const uchar* validRow = validMask.ptr<uchar>(row);
                        const uchar* supportRow = supportMask.ptr<uchar>(row);
                        const float* iRow = filteredI.ptr<float>(row);
                        const float* qRow = filteredQ.ptr<float>(row);
                        const double* gammaRow = gamma.ptr<double>(row);
                        const uchar* gammaMaskRow = gammaMask.ptr<uchar>(row);
                        const int* gammaCountRow = gammaCount.ptr<int>(row);
                        for (int col = 0; col < cols; ++col) {
                            const bool finiteOutput = std::isfinite(phaseRow[col]) &&
                                std::isfinite(iRow[col]) && std::isfinite(qRow[col]);
                            const double amplitude = std::hypot(static_cast<double>(iRow[col]),
                                static_cast<double>(qRow[col]));
                            const bool phaseIqMismatch = validRow[col] != 0 && finiteOutput && amplitude > 1e-12 &&
                                std::abs(std::atan2(std::sin(phaseRow[col] - std::atan2(qRow[col], iRow[col])),
                                    std::cos(phaseRow[col] - std::atan2(qRow[col], iRow[col])))) > 1e-5;
                            const bool invalid = validRow[col] > 1 || supportRow[col] > 1 || gammaMaskRow[col] > 1 ||
                                (supportRow[col] != 0 && validRow[col] == 0) || gammaCountRow[col] < 0 ||
                                (gammaMaskRow[col] != 0 && gammaCountRow[col] <= 0) ||
                                (gammaMaskRow[col] == 0 && gammaCountRow[col] != 0) ||
                                (gammaMaskRow[col] != 0 && (!std::isfinite(gammaRow[col]) || gammaRow[col] < 0.0 || gammaRow[col] > 1.0)) ||
                                (validRow[col] != 0 && !finiteOutput) || phaseIqMismatch;
                            if (invalid) {
                                if (error) *error = QObject::tr("SNAP 兼容 Goldstein 快速抽样合同无效于 (%1,%2)：%3")
                                    .arg(origin.first + row).arg(origin.second + col).arg(h5Path);
                                return false;
                            }
                        }
                    }
                }
                return true;
            };

            if (!deepValidation) {
                for (int i = 0; i < inPaths.size(); ++i) {
                    if (cancelToken && cancelToken->load()) {
                        res.success = false;
                        res.errorMsg = QObject::tr("验证任务已取消。");
                        return res;
                    }
                    const QString& inH5 = inPaths.at(i);
                    const QString& outH5 = outPaths.at(i);
                    int inRows = 0, inCols = 0, outRows = 0, outCols = 0;
                    QString error;
                    if (!NodeUtils::probeH5DatasetMetadata(inH5, QStringLiteral("phase"), &inRows, &inCols, &error) ||
                        !NodeUtils::probeH5DatasetMetadata(outH5, QStringLiteral("phase"), &outRows, &outCols, &error) ||
                        inRows != outRows || inCols != outCols) {
                        res.success = false;
                        res.errorMsg = error.isEmpty()
                            ? QObject::tr("输入与输出 phase 矩阵尺寸不一致：%1").arg(outH5) : error;
                        return res;
                    }
                    if (i == 0) {
                        res.inRows = inRows; res.inCols = inCols;
                        res.outRows = outRows; res.outCols = outCols;
                    }
                    ++res.matchingSizePairCount;

                    int fileMethod = 0, fileGoldstein = 0, fileNPad = 0;
                    const bool hasMethod = NodeUtils::readScalarFromH5(outH5, QStringLiteral("denoise_method"), fileMethod);
                    const bool hasGoldstein = NodeUtils::readScalarFromH5(outH5, QStringLiteral("denoise_goldstein_win"), fileGoldstein);
                    const bool hasNPad = NodeUtils::readScalarFromH5(outH5, QStringLiteral("denoise_goldstein_npad"), fileNPad);
                    if (hasMethod) {
                        if (res.hasActualMethod && fileMethod != res.actualMethod) res.methodInconsistent = true;
                        res.actualMethod = res.hasActualMethod ? res.actualMethod : fileMethod;
                        res.hasActualMethod = true;
                    }
                    if (hasGoldstein) {
                        if (res.hasActualGoldsteinWin && fileGoldstein != res.actualGoldsteinWin) res.goldsteinWinInconsistent = true;
                        res.actualGoldsteinWin = res.hasActualGoldsteinWin ? res.actualGoldsteinWin : fileGoldstein;
                        res.hasActualGoldsteinWin = true;
                    }
                    if (hasNPad) {
                        if (res.hasActualNPad && fileNPad != res.actualNPad) res.nPadInconsistent = true;
                        res.actualNPad = res.hasActualNPad ? res.actualNPad : fileNPad;
                        res.hasActualNPad = true;
                    }
                    if (expMethod == 4) {
                        if (!hasMethod || fileMethod != 4 || !validateSnapCompatibleQuick(outH5, &error)) {
                            res.success = false;
                            if (cancelToken && cancelToken->load()) {
                                res.errorMsg = QObject::tr("验证任务已取消。");
                                return res;
                            }
                            res.errorMsg = error.isEmpty()
                                ? QObject::tr("SNAP 兼容 Goldstein 快速合同检查失败：%1").arg(outH5) : error;
                            return res;
                        }
                    }
                }
                res.success = true;
                return res;
            }

            const double pi = 3.14159265358979323846;
            const double twoPi = 2.0 * pi;
            const auto wrapDifference = [pi, twoPi](double delta) {
                if (delta > pi) {
                    return delta - twoPi;
                }
                if (delta <= -pi) {
                    return delta + twoPi;
                }
                return delta;
            };

            const auto calculatePhaseQuality = [wrapDifference, pi, cancelToken](const cv::Mat& phase) {
                PhaseQualityMetrics metrics;
                if (phase.empty() || phase.rows <= 0 || phase.cols <= 0) {
                    return metrics;
                }

                // Detail View quality indicators are diagnostic summaries. Keep
                // their cost bounded for production-scale phase grids while
                // preserving immediate-neighbour gradients and plaquettes.
                constexpr qint64 kMaxQualitySamples = 1000000;
                const qint64 qualityRowBudget = std::min<qint64>({ phase.rows, kMaxQualitySamples,
                    std::max<qint64>(1, static_cast<qint64>(std::floor(std::sqrt(
                        static_cast<double>(kMaxQualitySamples) * phase.rows / phase.cols)))) });
                const qint64 qualityColBudget = std::min<qint64>(phase.cols,
                    std::max<qint64>(1, kMaxQualitySamples / qualityRowBudget));
                const int qualityRowStride = static_cast<int>((static_cast<qint64>(phase.rows)
                    + qualityRowBudget - 1) / qualityRowBudget);
                const int qualityColStride = static_cast<int>((static_cast<qint64>(phase.cols)
                    + qualityColBudget - 1) / qualityColBudget);

                double gradientSumSquares = 0.0;
                double gradientCount = 0.0;

                #pragma omp parallel for reduction(+:gradientSumSquares, gradientCount) schedule(static)
                for (int row = 0; row < phase.rows; row += qualityRowStride) {
                    if (cancelToken && cancelToken->load()) {
                        continue;
                    }
                    const float* values = phase.ptr<float>(row);
                    const float* nextRow = row + 1 < phase.rows ? phase.ptr<float>(row + 1) : nullptr;
                    for (int col = 0; col < phase.cols; col += qualityColStride) {
                        const double value = values[col];
                        if (!std::isfinite(value)) {
                            continue;
                        }
                        if (col + 1 < phase.cols && std::isfinite(values[col + 1])) {
                            const double gradient = wrapDifference(static_cast<double>(values[col + 1]) - value);
                            gradientSumSquares += gradient * gradient;
                            gradientCount += 1.0;
                        }
                        if (nextRow && std::isfinite(nextRow[col])) {
                            const double gradient = wrapDifference(static_cast<double>(nextRow[col]) - value);
                            gradientSumSquares += gradient * gradient;
                            gradientCount += 1.0;
                        }
                    }
                }

                if (cancelToken && cancelToken->load()) {
                    return metrics;
                }

                if (gradientCount > 0.0) {
                    metrics.gradientRms = std::sqrt(gradientSumSquares / gradientCount);
                    metrics.hasGradient = true;
                }

                double positiveResidueCount = 0.0;
                double negativeResidueCount = 0.0;
                double plaquetteCount = 0.0;

                #pragma omp parallel for reduction(+:positiveResidueCount, negativeResidueCount, plaquetteCount) schedule(static)
                for (int row = 0; row < phase.rows - 1; row += qualityRowStride) {
                    if (cancelToken && cancelToken->load()) {
                        continue;
                    }
                    const float* top = phase.ptr<float>(row);
                    const float* bottom = phase.ptr<float>(row + 1);
                    for (int col = 0; col < phase.cols - 1; col += qualityColStride) {
                        const double p00 = top[col];
                        const double p01 = top[col + 1];
                        const double p11 = bottom[col + 1];
                        const double p10 = bottom[col];
                        if (!std::isfinite(p00) || !std::isfinite(p01) || !std::isfinite(p11) || !std::isfinite(p10)) {
                            continue;
                        }

                        const double closure = wrapDifference(p01 - p00)
                            + wrapDifference(p11 - p01)
                            + wrapDifference(p10 - p11)
                            + wrapDifference(p00 - p10);
                        if (closure > pi) {
                            positiveResidueCount += 1.0;
                        } else if (closure < -pi) {
                            negativeResidueCount += 1.0;
                        }
                        plaquetteCount += 1.0;
                    }
                }

                if (cancelToken && cancelToken->load()) {
                    return metrics;
                }

                if (plaquetteCount > 0.0) {
                    metrics.positiveResidueCount = positiveResidueCount;
                    metrics.negativeResidueCount = negativeResidueCount;
                    metrics.totalResidueCount = positiveResidueCount + negativeResidueCount;
                    metrics.validPlaquetteCount = plaquetteCount;
                    metrics.residueDensity = 100.0 * metrics.totalResidueCount / metrics.validPlaquetteCount;
                    metrics.hasResidueDensity = true;
                }
                return metrics;
            };

            // Aggregators across all image pairs
            int methodFiles = 0;
            int prefilterFiles = 0;
            int slopeFiles = 0;
            int goldsteinFiles = 0;
            int nPadFiles = 0;
            int alphaFiles = 0;
            int denoiseDlFiles = 0;
            bool methodInconsistent = false;
            bool prefilterInconsistent = false;
            bool slopeInconsistent = false;
            bool goldsteinInconsistent = false;
            bool nPadInconsistent = false;
            bool alphaInconsistent = false;

            double sumSin = 0.0;
            double sumCos = 0.0;
            double wrappedPixelCount = 0.0;

            double gradientInputSum = 0.0, gradientOutputSum = 0.0;
            int gradientInputFiles = 0, gradientOutputFiles = 0;
            double residueDensityInputSum = 0.0, residueDensityOutputSum = 0.0;
            double residuePosInputSum = 0.0, residuePosOutputSum = 0.0;
            double residueNegInputSum = 0.0, residueNegOutputSum = 0.0;
            double residueTotalInputSum = 0.0, residueTotalOutputSum = 0.0;
            double residueValidInputSum = 0.0, residueValidOutputSum = 0.0;
            int residueInputFiles = 0, residueOutputFiles = 0;

            bool anyPhaseReadFailed = false;
            bool dimsRecorded = false;

            for (int i = 0; i < inPaths.size(); ++i) {
                if (cancelToken && cancelToken->load()) {
                    res.success = false;
                    res.errorMsg = QObject::tr("验证任务已取消。");
                    return res;
                }

                const QString& inH5 = inPaths.at(i);
                const QString& outH5 = outPaths.at(i);

                // 1. Read metadata parameters and matrices from H5 with narrowed lock scope
                int fileMethod = 0;
                int filePrefilter = 0;
                int fileSlope = 0;
                int fileGoldstein = 0;
                int fileNPad = 0;
                double fileAlpha = 0.0;
                int fileDl = 0;
                bool hasMethod = false;
                bool hasPrefilter = false;
                bool hasSlope = false;
                bool hasGoldstein = false;
                bool hasNPad = false;
                bool hasAlpha = false;
                bool hasDl = false;
                cv::Mat inPhase, outPhase;
                bool ok1 = false;
                bool ok2 = false;

                {
                    NodeUtils::Hdf5Locker ioLocker;
                    hasMethod = NodeUtils::readScalarFromH5(outH5, "denoise_method", fileMethod);
                    hasPrefilter = NodeUtils::readScalarFromH5(outH5, "denoise_slope_pre_win", filePrefilter);
                    hasSlope = NodeUtils::readScalarFromH5(outH5, "denoise_slope_win", fileSlope);
                    hasGoldstein = NodeUtils::readScalarFromH5(outH5, "denoise_goldstein_win", fileGoldstein);
                    hasNPad = NodeUtils::readScalarFromH5(outH5, "denoise_goldstein_npad", fileNPad);
                    hasAlpha = NodeUtils::readScalarFromH5(outH5, "denoise_goldstein_alpha", fileAlpha);
                    hasDl = NodeUtils::readScalarFromH5(outH5, "denoise_dl", fileDl);
                    ok1 = NodeUtils::readMatFromH5(inH5, "phase", inPhase, CV_32F);
                    ok2 = NodeUtils::readMatFromH5(outH5, "phase", outPhase, CV_32F);
                }

                if (hasMethod) {
                    if (methodFiles == 0) {
                        res.actualMethod = fileMethod;
                    } else if (fileMethod != res.actualMethod) {
                        methodInconsistent = true;
                    }
                    ++methodFiles;
                }
                if (hasPrefilter) {
                    if (prefilterFiles == 0) {
                        res.actualPrefilter = filePrefilter;
                    } else if (filePrefilter != res.actualPrefilter) {
                        prefilterInconsistent = true;
                    }
                    ++prefilterFiles;
                }
                if (hasSlope) {
                    if (slopeFiles == 0) {
                        res.actualSlopeWindow = fileSlope;
                    } else if (fileSlope != res.actualSlopeWindow) {
                        slopeInconsistent = true;
                    }
                    ++slopeFiles;
                }
                if (hasGoldstein) {
                    if (goldsteinFiles == 0) {
                        res.actualGoldsteinWin = fileGoldstein;
                    } else if (fileGoldstein != res.actualGoldsteinWin) {
                        goldsteinInconsistent = true;
                    }
                    ++goldsteinFiles;
                }
                if (hasNPad) {
                    if (nPadFiles == 0) {
                        res.actualNPad = fileNPad;
                    } else if (fileNPad != res.actualNPad) {
                        nPadInconsistent = true;
                    }
                    ++nPadFiles;
                }
                if (hasAlpha) {
                    if (alphaFiles == 0) {
                        res.actualAlpha = fileAlpha;
                    } else if (qAbs(fileAlpha - res.actualAlpha) > 1e-6) {
                        alphaInconsistent = true;
                    }
                    ++alphaFiles;
                }
                if (hasDl) {
                    ++denoiseDlFiles;
                }

                // 2. Process matrices out of lock
                if (!ok1 || !ok2 || inPhase.empty() || outPhase.empty()) {
                    anyPhaseReadFailed = true;
                    continue;
                }

                if (!dimsRecorded) {
                    res.inRows = inPhase.rows;
                    res.inCols = inPhase.cols;
                    res.outRows = outPhase.rows;
                    res.outCols = outPhase.cols;
                    dimsRecorded = true;
                }

                const bool sizeMatches = (inPhase.rows == outPhase.rows && inPhase.cols == outPhase.cols);
                if (sizeMatches) {
                    ++res.matchingSizePairCount;
                }

                const PhaseQualityMetrics inMetrics = calculatePhaseQuality(inPhase);
                const PhaseQualityMetrics outMetrics = calculatePhaseQuality(outPhase);

                if (inMetrics.hasGradient) {
                    gradientInputSum += inMetrics.gradientRms;
                    ++gradientInputFiles;
                }
                if (outMetrics.hasGradient) {
                    gradientOutputSum += outMetrics.gradientRms;
                    ++gradientOutputFiles;
                }
                if (inMetrics.hasResidueDensity) {
                    residueDensityInputSum += inMetrics.residueDensity;
                    residuePosInputSum += inMetrics.positiveResidueCount;
                    residueNegInputSum += inMetrics.negativeResidueCount;
                    residueTotalInputSum += inMetrics.totalResidueCount;
                    residueValidInputSum += inMetrics.validPlaquetteCount;
                    ++residueInputFiles;
                }
                if (outMetrics.hasResidueDensity) {
                    residueDensityOutputSum += outMetrics.residueDensity;
                    residuePosOutputSum += outMetrics.positiveResidueCount;
                    residueNegOutputSum += outMetrics.negativeResidueCount;
                    residueTotalOutputSum += outMetrics.totalResidueCount;
                    residueValidOutputSum += outMetrics.validPlaquetteCount;
                    ++residueOutputFiles;
                }

                // 3. 缠绕相位差圆统计：对尺寸一致的影像对进行自适应步长采样，避免千万级像素密集调用三角函数
                if (sizeMatches) {
                    constexpr qint64 maxWrappedSamples = 500000;
                    const qint64 wrappedRowBudget = std::min<qint64>({ inPhase.rows, maxWrappedSamples,
                        std::max<qint64>(1, static_cast<qint64>(std::floor(std::sqrt(
                            static_cast<double>(maxWrappedSamples) * inPhase.rows / inPhase.cols)))) });
                    const qint64 wrappedColBudget = std::min<qint64>(inPhase.cols,
                        std::max<qint64>(1, maxWrappedSamples / wrappedRowBudget));
                    const int wrappedRowStride = static_cast<int>((static_cast<qint64>(inPhase.rows)
                        + wrappedRowBudget - 1) / wrappedRowBudget);
                    const int wrappedColStride = static_cast<int>((static_cast<qint64>(inPhase.cols)
                        + wrappedColBudget - 1) / wrappedColBudget);

                    #pragma omp parallel for reduction(+:sumSin, sumCos, wrappedPixelCount) schedule(static)
                    for (int row = 0; row < inPhase.rows; row += wrappedRowStride) {
                        if (cancelToken && cancelToken->load()) {
                            continue;
                        }
                        const float* inValues = inPhase.ptr<float>(row);
                        const float* outValues = outPhase.ptr<float>(row);
                        for (int col = 0; col < inPhase.cols; col += wrappedColStride) {
                            const double inputValue = inValues[col];
                            const double outputValue = outValues[col];
                            if (!std::isfinite(inputValue) || !std::isfinite(outputValue)) {
                                continue;
                            }

                            const double delta = outputValue - inputValue;
                            sumSin += std::sin(delta);
                            sumCos += std::cos(delta);
                            wrappedPixelCount += 1.0;
                        }
                    }
                }

                // 及时释放大图像矩阵内存，降低多景批处理峰值内存占用
                inPhase.release();
                outPhase.release();
            }

            if (cancelToken && cancelToken->load()) {
                res.success = false;
                res.errorMsg = QObject::tr("验证任务已取消。");
                return res;
            }

            res.hasActualMethod = methodFiles > 0;
            res.methodInconsistent = methodInconsistent;
            res.hasActualPrefilter = prefilterFiles > 0;
            res.prefilterInconsistent = prefilterInconsistent;
            res.hasActualSlopeWindow = slopeFiles > 0;
            res.slopeWindowInconsistent = slopeInconsistent;
            res.hasActualGoldsteinWin = goldsteinFiles > 0;
            res.goldsteinWinInconsistent = goldsteinInconsistent;
            res.hasActualNPad = nPadFiles > 0;
            res.nPadInconsistent = nPadInconsistent;
            res.hasActualAlpha = alphaFiles > 0;
            res.alphaInconsistent = alphaInconsistent;
            res.hasActualDenoiseDl = denoiseDlFiles > 0;

            if (wrappedPixelCount > 0.0) {
                res.wrappedDiffMean = std::atan2(sumSin, sumCos);
                res.wrappedDiffResultant = std::min(1.0, std::hypot(sumSin / wrappedPixelCount, sumCos / wrappedPixelCount));
                res.wrappedDiffStd = std::sqrt(-2.0 * std::log(std::max(res.wrappedDiffResultant, 1e-12)));
                res.hasWrappedDifference = true;
            }

            // Average per-file quality metrics across all pairs
            if (gradientInputFiles > 0) {
                res.inputQuality.gradientRms = gradientInputSum / gradientInputFiles;
                res.inputQuality.hasGradient = true;
            }
            if (gradientOutputFiles > 0) {
                res.outputQuality.gradientRms = gradientOutputSum / gradientOutputFiles;
                res.outputQuality.hasGradient = true;
            }
            if (residueInputFiles > 0) {
                res.inputQuality.residueDensity = residueDensityInputSum / residueInputFiles;
                res.inputQuality.positiveResidueCount = residuePosInputSum / residueInputFiles;
                res.inputQuality.negativeResidueCount = residueNegInputSum / residueInputFiles;
                res.inputQuality.totalResidueCount = residueTotalInputSum / residueInputFiles;
                res.inputQuality.validPlaquetteCount = residueValidInputSum / residueInputFiles;
                res.inputQuality.hasResidueDensity = true;
            }
            if (residueOutputFiles > 0) {
                res.outputQuality.residueDensity = residueDensityOutputSum / residueOutputFiles;
                res.outputQuality.positiveResidueCount = residuePosOutputSum / residueOutputFiles;
                res.outputQuality.negativeResidueCount = residueNegOutputSum / residueOutputFiles;
                res.outputQuality.totalResidueCount = residueTotalOutputSum / residueOutputFiles;
                res.outputQuality.validPlaquetteCount = residueValidOutputSum / residueOutputFiles;
                res.outputQuality.hasResidueDensity = true;
            }

            if (anyPhaseReadFailed) {
                res.success = false;
                res.errorMsg = QObject::tr("部分影像的相位数据集读取失败，可能文件已损坏或格式不兼容。");
            } else {
                res.success = true;
            }
            return res;
        });

        // 使用 QFutureWatcher 监听异步执行状态并平滑更新 UI
        auto* watcher = new QFutureWatcher<ValidationResults>(this);
        connect(watcher, &QFutureWatcher<ValidationResults>::finished, this, [this, watcher, cacheKey, validationEpoch]() {
            if (validationEpoch != m_validationEpoch) {
                watcher->deleteLater();
                return;
            }

            ValidationResults res = watcher->result();
            if (!cacheKey.isEmpty() && res.success) {
                m_node->storeValidationCache(cacheKey, res);
            }

            // 若在超时触发后后台最终成功完成，平滑解除超时状态并渲染特征值，避免陷入死锁
            m_isTimedOut = false;
            applyValidationResults(res);
            watcher->deleteLater();
        });

        watcher->setFuture(future);
    }

    void applyValidationResults(const ValidationResults& res)
    {
        m_loadingOverlay->stopLoading();
        if (m_fullValidationProgressLabel) m_fullValidationProgressLabel->hide();

        if (!res.success) {
            if (m_fullValidationButton) m_fullValidationButton->setEnabled(true);
            m_statusTitle->setText(QObject::tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(res.errorMsg);
            // Clear any previously rendered comparison rows so stale success data is not kept
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);
            return;
        }

        // Update parameters comparison table
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

		QString methodStrExp = res.expectedMethod == 1 ? "Slope" :
			(res.expectedMethod == 2 ? "GoldsteinPhaseLegacyV1" :
				(res.expectedMethod == 3 ? "DL" : "GoldsteinSnapCompatibleV1"));
        QString methodStrAct;
        bool methodVerifiable = false;
        if (res.hasActualMethod) {
            methodVerifiable = true;
			methodStrAct = res.actualMethod == 1 ? "Slope" :
				(res.actualMethod == 2 ? "GoldsteinPhaseLegacyV1" :
					(res.actualMethod == 3 ? "DL" : (res.actualMethod == 4 ? "GoldsteinSnapCompatibleV1" : QObject::tr("未知"))));
            if (res.methodInconsistent) {
                methodStrAct += QObject::tr("（多文件值不一致）");
            } else if (res.actualMethod != res.expectedMethod) {
                methodStrAct += QObject::tr("（与期望不一致）");
            }
        } else {
            methodStrAct = QObject::tr("未记录（旧结果）");
        }
        m_compTable->addComparison(QObject::tr("滤波方法"), methodStrExp, methodStrAct, methodVerifiable);

        if (res.expectedMethod == 1) {
            bool verifiable = false;
            m_compTable->addComparison(QObject::tr("预滤波窗口大小"),
                QString::number(res.expectedPrefilter),
                paramActualText(res.hasActualPrefilter, QString::number(res.actualPrefilter),
                    res.prefilterInconsistent, res.actualPrefilter != res.expectedPrefilter, verifiable),
                verifiable);
            m_compTable->addComparison(QObject::tr("斜坡滤波窗口大小"),
                QString::number(res.expectedSlopeWindow),
                paramActualText(res.hasActualSlopeWindow, QString::number(res.actualSlopeWindow),
                    res.slopeWindowInconsistent, res.actualSlopeWindow != res.expectedSlopeWindow, verifiable),
                verifiable);
        } else if (res.expectedMethod == 2) {
            bool verifiable = false;
            m_compTable->addComparison(QObject::tr("Goldstein 窗口"),
                QString::number(res.expectedGoldsteinWin),
                paramActualText(res.hasActualGoldsteinWin, QString::number(res.actualGoldsteinWin),
                    res.goldsteinWinInconsistent, res.actualGoldsteinWin != res.expectedGoldsteinWin, verifiable),
                verifiable);
		} else if (res.expectedMethod == 4) {
			m_compTable->addComparison(QObject::tr("Goldstein profile"), "GoldsteinSnapCompatibleV1",
				res.actualMethod == 4 ? "GoldsteinSnapCompatibleV1" : QObject::tr("未记录或不匹配"),
				res.actualMethod == 4 && !res.methodInconsistent);
			m_compTable->addComparison(QObject::tr("FFT / 补零"), "64 / 0", "64 / 0",
				res.hasActualGoldsteinWin && res.actualGoldsteinWin == 64 &&
				res.hasActualNPad && res.actualNPad == 0);
			const bool snapContractVerified = res.actualMethod == 4 && !res.methodInconsistent;
			m_compTable->addDiagnostic(QObject::tr("I/Q、complex_gamma 与 v2 掩膜合同"),
				snapContractVerified
                    ? (res.quickValidation ? QObject::tr("抽样通过（非完整）") : QObject::tr("完整验证通过"))
                    : QObject::tr("未验证"),
                QObject::tr("此项是输出合同状态，不是设置值与实际值的等值参数比较。"),
                snapContractVerified
                    ? (res.quickValidation ? QObject::tr("抽样通过（非完整）") : QObject::tr("完整验证通过"))
                    : QObject::tr("未验证"),
                !snapContractVerified);
        } else if (res.expectedMethod == 3) {
            m_compTable->addComparison(QObject::tr("深度学习滤波标记"), QObject::tr("已记录"),
                res.hasActualDenoiseDl ? QObject::tr("已记录") : QObject::tr("未记录（旧结果）"),
                res.hasActualDenoiseDl);
        }

        m_compTable->addComparison(QObject::tr("图像宽度 (列数)"), QString::number(res.inCols), QString::number(res.outCols));
        m_compTable->addComparison(QObject::tr("图像高度 (行数)"), QString::number(res.inRows), QString::number(res.outRows));

        m_compTable->addDiagnostic(QObject::tr("参与校验的影像对数"), QString::number(res.imagePairCount));
        m_compTable->addDiagnostic(QObject::tr("尺寸一致对数"), QString::number(res.matchingSizePairCount));

        // Update feature analysis labels
        const auto transitionText = [](double input, bool hasInput, double output, bool hasOutput,
            int precision, const QString& unitSuffix) {
            if (!hasInput || !hasOutput) {
                return QObject::tr("无有效数据");
            }
            const QString inputText = QString::number(input, 'f', precision) + unitSuffix;
            const QString outputText = QString::number(output, 'f', precision) + unitSuffix;
            if (input != 0.0) {
                const double reduction = 100.0 * (input - output) / input;
                return QString("%1 -> %2 (%3%)").arg(inputText).arg(outputText).arg(QString::number(reduction, 'f', 2));
            }
            return QStringLiteral("%1 -> %2").arg(inputText, outputText);
        };
        const auto residueCountText = [](const PhaseQualityMetrics& metrics) {
            if (!metrics.hasResidueDensity) {
                return QObject::tr("无有效单元");
            }
            // Counts are averaged across files (double), format as whole numbers
            return QObject::tr("+%1 / -%2 / %3（有效 %4）")
                .arg(QString::number(metrics.positiveResidueCount, 'f', 0))
                .arg(QString::number(metrics.negativeResidueCount, 'f', 0))
                .arg(QString::number(metrics.totalResidueCount, 'f', 0))
                .arg(QString::number(metrics.validPlaquetteCount, 'f', 0));
        };
        const auto residueDensityText = [](const PhaseQualityMetrics& input,
                                           const PhaseQualityMetrics& output) {
            if (!input.hasResidueDensity || !output.hasResidueDensity) {
                return QObject::tr("无有效单元");
            }
            const QString inputText = QString::number(input.residueDensity, 'f', 3) + QStringLiteral("%");
            const QString outputText = QString::number(output.residueDensity, 'f', 3) + QStringLiteral("%");
            if (input.totalResidueCount == 0) {
                return QStringLiteral("%1 -> %2").arg(inputText, outputText);
            }
            const double reduction = 100.0 * (input.residueDensity - output.residueDensity) / input.residueDensity;
            return QStringLiteral("%1 -> %2 (%3%)")
                .arg(inputText, outputText, QString::number(reduction, 'f', 2));
        };
        const auto setFeatureValue = [](QLabel* label, const QString& value) {
            label->setText(value);
            label->setToolTip(value);
        };
        if (res.quickValidation) {
            const QString notComputed = QObject::tr("快速验证未计算（执行完整验证可获取）");
            setFeatureValue(m_lblDiffMean, notComputed);
            setFeatureValue(m_lblDiffStd, notComputed);
            setFeatureValue(m_lblDiffResultant, notComputed);
            setFeatureValue(m_lblGradientSummary, notComputed);
            setFeatureValue(m_lblResidueCountSummary, notComputed);
            setFeatureValue(m_lblResidueSummary, notComputed);
        } else {
            setFeatureValue(m_lblDiffMean, res.hasWrappedDifference
                ? QString::number(res.wrappedDiffMean, 'f', 4)
                : QObject::tr("图像尺寸不一致"));
            setFeatureValue(m_lblDiffStd, res.hasWrappedDifference
                ? QString::number(res.wrappedDiffStd, 'f', 4)
                : QObject::tr("图像尺寸不一致"));
            setFeatureValue(m_lblDiffResultant, res.hasWrappedDifference
                ? QString::number(res.wrappedDiffResultant, 'f', 4)
                : QObject::tr("图像尺寸不一致"));
            setFeatureValue(m_lblGradientSummary, transitionText(res.inputQuality.gradientRms, res.inputQuality.hasGradient,
                res.outputQuality.gradientRms, res.outputQuality.hasGradient, 4, QString()));
            setFeatureValue(m_lblResidueCountSummary, residueCountText(res.inputQuality)
                + QStringLiteral(" -> ") + residueCountText(res.outputQuality));
            setFeatureValue(m_lblResidueSummary, residueDensityText(res.inputQuality, res.outputQuality));
        }

        // Final status card
        m_statusTitle->setText(res.quickValidation ? QObject::tr("快速验证通过（非完整）") : QObject::tr("完整验证通过"));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
        if (m_fullValidationButton) m_fullValidationButton->setEnabled(res.quickValidation);
        bool paramsUnverified = false;
        if (res.expectedMethod == 1) {
            paramsUnverified = !res.hasActualPrefilter || !res.hasActualSlopeWindow;
        } else if (res.expectedMethod == 2) {
            paramsUnverified = !res.hasActualGoldsteinWin || !res.hasActualNPad || !res.hasActualAlpha;
		} else if (res.expectedMethod == 4) {
			paramsUnverified = !res.hasActualMethod || res.actualMethod != 4 ||
				!res.hasActualGoldsteinWin || res.actualGoldsteinWin != 64 ||
				!res.hasActualNPad || res.actualNPad != 0;
        } else if (res.expectedMethod == 3) {
            paramsUnverified = !res.hasActualDenoiseDl;
        }
        const QString unverifiedNote = paramsUnverified
            ? QObject::tr("；该输出由旧版本生成，滤波参数未记录，无法逐项比对（结果特征值已计算）")
            : QString();
        if (res.quickValidation) {
            m_statusDesc->setText(QObject::tr("共快速校验 %1 对影像：检查元数据、矩阵尺寸及确定性 I/Q、complex_gamma、掩膜抽样；未执行逐像元完整扫描。")
                .arg(res.imagePairCount));
        } else if (res.matchingSizePairCount == res.imagePairCount) {
            m_statusDesc->setText(QObject::tr("共校验 %1 对影像，行列尺寸全部一致；滤波参数及结果特征值比对完成。%2")
                .arg(res.imagePairCount).arg(unverifiedNote));
        } else {
            m_statusDesc->setText(QObject::tr("共校验 %1 对影像，其中 %2 对行列尺寸一致；缠绕相位差统计仅针对尺寸一致的影像对。%3")
                .arg(res.imagePairCount).arg(res.matchingSizePairCount).arg(unverifiedNote));
        }
    }

private:
    DenoiseNode* m_node = nullptr;
    quint64 m_validationEpoch = 0;
    bool m_requestedDeepValidation = false;
    QPushButton* m_fullValidationButton = nullptr;
    QLabel* m_fullValidationProgressLabel = nullptr;
    
    QLabel* m_lblDiffMean = nullptr;
    QLabel* m_lblDiffStd = nullptr;
    QLabel* m_lblDiffResultant = nullptr;
    QLabel* m_lblGradientSummary = nullptr;
    QLabel* m_lblResidueCountSummary = nullptr;
    QLabel* m_lblResidueSummary = nullptr;

};

::QWidget* DenoiseNode::createValidationWidget(::QWidget* parent)
{
    return new DenoiseValidationWidget(this, parent);
}

} // namespace QtNodes
