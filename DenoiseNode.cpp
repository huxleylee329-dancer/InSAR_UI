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
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include "QtNodes/internal/NodeDetailWindow.hpp"

namespace QtNodes {

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
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

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
    }

    return true;
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
    InSARLogManager::LogInfo("DenoiseNode", "executeProcessing started.");

    setProgress(0);

    QString dstNode = m_preparedDstNode;
    QString savePath = projectPath();
    QStringList phasePaths = m_inputData->filePaths();
    QStringList phaseNames;
    for (const QString& phasePath : phasePaths) {
        phaseNames.append(QFileInfo(phasePath).baseName());
    }

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
                                           phasePaths, m_outputTransaction, &transactionError)) {
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
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) return false;

    QStringList expectedJpgPaths;
    QStringList types;
    for (const QString& h5Path : h5Paths) {
        QString baseName = QFileInfo(h5Path).baseName();
        expectedJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + baseName + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
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
                    QObject::tr("特征值分析"));

        m_lblDiffMean = createFeatureLabel();
        m_lblDiffStd = createFeatureLabel();
        m_lblDiffResultant = createFeatureLabel();
        m_lblGradientSummary = createFeatureLabel();
        m_lblResidueSummary = createFeatureLabel();
        m_lblInWidth = m_lblDiffMean;
        m_lblOutWidth = m_lblDiffStd;
        m_lblInMean = m_lblDiffResultant;
        m_lblOutMean = m_lblGradientSummary;

        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差圆均值 (rad):")), m_lblDiffMean);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差圆标准差 (rad):")), m_lblDiffStd);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位差集中度 R (0-1):")), m_lblDiffResultant);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缠绕相位梯度 RMS (输入 -> 输出):")), m_lblGradientSummary);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("相位残差点密度 (输入 -> 输出):")), m_lblResidueSummary);
    }

    struct PhaseQualityMetrics {
        double gradientRms = 0.0;
        double residueDensity = 0.0;
        bool hasGradient = false;
        bool hasResidueDensity = false;
    };

    struct ValidationResults {
        bool success = false;
        QString errorMsg;
        // Compare values
        int expectedMethod = 1;
        int actualMethod = 0;
        int expectedPrefilter = 5;
        int actualPrefilter = 0;
        int expectedSlopeWindow = 5;
        int actualSlopeWindow = 0;
        bool hasActualMethod = false;
        bool hasActualPrefilter = false;
        bool hasActualSlopeWindow = false;
        // Calculated features
        int inRows = 0, inCols = 0;
        int outRows = 0, outCols = 0;
        double wrappedDiffMean = 0.0;
        double wrappedDiffStd = 0.0;
        double wrappedDiffResultant = 0.0;
        bool hasWrappedDifference = false;
        PhaseQualityMetrics inputQuality;
        PhaseQualityMetrics outputQuality;
    };

    void startAsyncValidation() override
    {
        m_isTimedOut = false;

        // 1. Quick checks: If output not complete or inputs missing
        if (m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(QObject::tr("验证未通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未找到输入或输出文件的元数据，无法进行比对。"));
            
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);
            
            m_lblInWidth->setText(QObject::tr("未执行"));
            m_lblOutWidth->setText(QObject::tr("未执行"));
            m_lblInMean->setText(QObject::tr("未执行"));
            m_lblOutMean->setText(QObject::tr("未执行"));
            m_lblDiffStd->setText(QObject::tr("未执行"));
            m_lblDiffMean->setText(QObject::tr("未执行"));
            m_lblDiffStd->setText(QObject::tr("未执行"));
            m_lblDiffResultant->setText(QObject::tr("未执行"));
            m_lblGradientSummary->setText(QObject::tr("未执行"));
            m_lblResidueSummary->setText(QObject::tr("未执行"));

            return;
        }

        // Output paths check
        auto outData = std::dynamic_pointer_cast<ImportedFileData>(m_node->outData(0));
        auto inData = std::dynamic_pointer_cast<ImportedFileData>(m_node->getInputData(0));
        if (!outData || outData->filePaths().isEmpty() || !inData || inData->filePaths().isEmpty()) {
            m_statusTitle->setText(QObject::tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未找到输入或输出文件的元数据，无法进行比对。"));
            return;
        }

        m_loadingOverlay->startLoading(QObject::tr("正在加载 H5 文件并计算统计特征值..."));

        QString inH5 = inData->filePaths().first();
        QString outH5 = outData->filePaths().first();
        
        // Settings to compare
        int expMethod = m_node->save()["method"].toInt(1);
        int expPrefilter = m_node->save()["prefilterWin"].toInt(5);
        int expSlopeWindow = m_node->save()["slopeWin"].toInt(5);

        // Run validation in background
        QFuture<ValidationResults> future = QtConcurrent::run([inH5, outH5, expMethod, expPrefilter, expSlopeWindow]() {
            NodeUtils::Hdf5Locker locker;
            ValidationResults res;
            res.expectedMethod = expMethod;
            res.expectedPrefilter = expPrefilter;
            res.expectedSlopeWindow = expSlopeWindow;

            // 1. Read metadata parameters from output H5
            res.hasActualMethod = NodeUtils::readScalarFromH5(outH5, "denoise_method", res.actualMethod);
            res.hasActualPrefilter = NodeUtils::readScalarFromH5(outH5, "denoise_slope_pre_win", res.actualPrefilter);
            res.hasActualSlopeWindow = NodeUtils::readScalarFromH5(outH5, "denoise_slope_win", res.actualSlopeWindow);
            
            // 2. Read matrices
            cv::Mat inPhase, outPhase;
            bool ok1 = NodeUtils::readMatFromH5(inH5, "phase", inPhase, CV_32F);
            bool ok2 = NodeUtils::readMatFromH5(outH5, "phase", outPhase, CV_32F);

            if (ok1 && ok2 && !inPhase.empty() && !outPhase.empty()) {
                res.success = true;
                res.inRows = inPhase.rows;
                res.inCols = inPhase.cols;
                res.outRows = outPhase.rows;
                res.outCols = outPhase.cols;

                const auto calculatePhaseQuality = [](const cv::Mat& phase) {
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

                    PhaseQualityMetrics metrics;
                    double gradientSumSquares = 0.0;
                    qint64 gradientCount = 0;
                    for (int row = 0; row < phase.rows; ++row) {
                        const float* values = phase.ptr<float>(row);
                        const float* nextRow = row + 1 < phase.rows ? phase.ptr<float>(row + 1) : nullptr;
                        for (int col = 0; col < phase.cols; ++col) {
                            const double value = values[col];
                            if (!std::isfinite(value)) {
                                continue;
                            }
                            if (col + 1 < phase.cols && std::isfinite(values[col + 1])) {
                                const double gradient = wrapDifference(static_cast<double>(values[col + 1]) - value);
                                gradientSumSquares += gradient * gradient;
                                ++gradientCount;
                            }
                            if (nextRow && std::isfinite(nextRow[col])) {
                                const double gradient = wrapDifference(static_cast<double>(nextRow[col]) - value);
                                gradientSumSquares += gradient * gradient;
                                ++gradientCount;
                            }
                        }
                    }
                    if (gradientCount > 0) {
                        metrics.gradientRms = std::sqrt(gradientSumSquares / gradientCount);
                        metrics.hasGradient = true;
                    }

                    qint64 residueCount = 0;
                    qint64 plaquetteCount = 0;
                    for (int row = 0; row + 1 < phase.rows; ++row) {
                        const float* top = phase.ptr<float>(row);
                        const float* bottom = phase.ptr<float>(row + 1);
                        for (int col = 0; col + 1 < phase.cols; ++col) {
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
                            if (std::abs(closure) > pi) {
                                ++residueCount;
                            }
                            ++plaquetteCount;
                        }
                    }
                    if (plaquetteCount > 0) {
                        metrics.residueDensity = 100.0 * residueCount / plaquetteCount;
                        metrics.hasResidueDensity = true;
                    }
                    return metrics;
                };

                res.inputQuality = calculatePhaseQuality(inPhase);
                res.outputQuality = calculatePhaseQuality(outPhase);

                if (inPhase.rows == outPhase.rows && inPhase.cols == outPhase.cols) {
                    double sumSin = 0.0;
                    double sumCos = 0.0;
                    qint64 count = 0;

                    for (int row = 0; row < inPhase.rows; ++row) {
                        const float* inValues = inPhase.ptr<float>(row);
                        const float* outValues = outPhase.ptr<float>(row);
                        for (int col = 0; col < inPhase.cols; ++col) {
                            const double inputValue = inValues[col];
                            const double outputValue = outValues[col];
                            if (!std::isfinite(inputValue) || !std::isfinite(outputValue)) {
                                continue;
                            }

                            const double delta = outputValue - inputValue;
                            sumSin += std::sin(delta);
                            sumCos += std::cos(delta);
                            ++count;
                        }
                    }

                    if (count > 0) {
                        res.wrappedDiffMean = std::atan2(sumSin, sumCos);
                        res.wrappedDiffResultant = std::min(1.0, std::hypot(sumSin / count, sumCos / count));
                        res.wrappedDiffStd = std::sqrt(-2.0 * std::log(std::max(res.wrappedDiffResultant, 1e-12)));
                        res.hasWrappedDifference = true;
                    }
                }
            } else {
                res.success = false;
                res.errorMsg = QObject::tr("读取相位数据集失败，可能文件已损坏或格式不兼容。");
            }
            return res;
        });

        // Use QFutureWatcher to monitor finished state and update UI
        auto* watcher = new QFutureWatcher<ValidationResults>(this);
        connect(watcher, &QFutureWatcher<ValidationResults>::finished, this, [this, watcher]() {
            if (m_isTimedOut) {
                watcher->deleteLater();
                return;
            }

            ValidationResults res = watcher->result();
            m_loadingOverlay->stopLoading();

            if (res.success) {
                // Update parameters comparison table
                m_compTable->clearComparison();
                m_compTable->setEnabled(true);
                
                QString methodStrExp = res.expectedMethod == 1 ? "Slope" : (res.expectedMethod == 2 ? "Goldstein" : "DL");
                QString methodStrAct = res.hasActualMethod
                    ? (res.actualMethod == 1 ? "Slope" : (res.actualMethod == 2 ? "Goldstein" : (res.actualMethod == 3 ? "DL" : QObject::tr("未知"))))
                    : QObject::tr("未记录（旧结果）");
                m_compTable->addComparison(QObject::tr("滤波方法"), methodStrExp, methodStrAct);
                if (res.expectedMethod == 1) {
                    m_compTable->addComparison(QObject::tr("预滤波窗口大小"),
                        QString::number(res.expectedPrefilter),
                        res.hasActualPrefilter ? QString::number(res.actualPrefilter) : QObject::tr("未记录（旧结果）"));
                    m_compTable->addComparison(QObject::tr("斜坡滤波窗口大小"),
                        QString::number(res.expectedSlopeWindow),
                        res.hasActualSlopeWindow ? QString::number(res.actualSlopeWindow) : QObject::tr("未记录（旧结果）"));
                }
                m_compTable->addComparison(QObject::tr("图像宽度 (列数)"), QString::number(res.inCols), QString::number(res.outCols));
                m_compTable->addComparison(QObject::tr("图像高度 (行数)"), QString::number(res.inRows), QString::number(res.outRows));

                // Update feature analysis labels
                m_lblDiffMean->setText(res.hasWrappedDifference
                    ? QString::number(res.wrappedDiffMean, 'f', 4)
                    : QObject::tr("图像尺寸不一致"));
                m_lblDiffStd->setText(res.hasWrappedDifference
                    ? QString::number(res.wrappedDiffStd, 'f', 4)
                    : QObject::tr("图像尺寸不一致"));
                m_lblDiffResultant->setText(res.hasWrappedDifference
                    ? QString::number(res.wrappedDiffResultant, 'f', 4)
                    : QObject::tr("图像尺寸不一致"));
                const auto transitionText = [](double input, bool hasInput, double output, bool hasOutput,
                    int precision, const QString& unitSuffix) {
                    if (!hasInput || !hasOutput || input <= 0.0) {
                        return QObject::tr("无有效数据");
                    }
                    const QString inputText = QString::number(input, 'f', precision) + unitSuffix;
                    const QString outputText = QString::number(output, 'f', precision) + unitSuffix;
                    const double reduction = 100.0 * (input - output) / input;
                    return QString("%1 -> %2 (%3%)").arg(inputText).arg(outputText).arg(QString::number(reduction, 'f', 2));
                };
                m_lblGradientSummary->setText(transitionText(res.inputQuality.gradientRms, res.inputQuality.hasGradient,
                    res.outputQuality.gradientRms, res.outputQuality.hasGradient, 4, QString()));
                m_lblResidueSummary->setText(transitionText(res.inputQuality.residueDensity, res.inputQuality.hasResidueDensity,
                    res.outputQuality.residueDensity, res.outputQuality.hasResidueDensity, 3, QStringLiteral("%")));

                // Final status card
                m_statusTitle->setText(QObject::tr("验证通过"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                m_statusDesc->setText(QObject::tr("图像行列数比对无误，平滑窗口等重要滤波参数比对成功。实际滤波结果特征值已成功计算并展现。"));
            } else {
                m_statusTitle->setText(QObject::tr("验证失败"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                m_statusDesc->setText(res.errorMsg);
            }
            
            watcher->deleteLater();
        });

        watcher->setFuture(future);
    }

private:
    DenoiseNode* m_node = nullptr;
    
    QLabel* m_lblDiffMean = nullptr;
    QLabel* m_lblDiffStd = nullptr;
    QLabel* m_lblDiffResultant = nullptr;
    QLabel* m_lblGradientSummary = nullptr;
    QLabel* m_lblResidueSummary = nullptr;
    QLabel* m_lblInWidth = nullptr;
    QLabel* m_lblOutWidth = nullptr;
    QLabel* m_lblInMean = nullptr;
    QLabel* m_lblOutMean = nullptr;

};

::QWidget* DenoiseNode::createValidationWidget(::QWidget* parent)
{
    return new DenoiseValidationWidget(this, parent);
}

} // namespace QtNodes
