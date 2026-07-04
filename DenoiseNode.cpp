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
#include <QDateTime>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

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
    QString dstProject = projectName();
    QString srcNode = m_inputData->nodeName();

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

    // Clean up old data nodes to prevent tree duplicates
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode);

    m_thread = new QThread();
    m_workerThread = new DenoiseWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &DenoiseNode::startDenoise, m_workerThread, &DenoiseWorker::Denoise);
    connect(m_thread, &QThread::started, [this, para, alpha, savePath, dstProject, srcNode, dstNode]() {
        Q_EMIT startDenoise(para, alpha, savePath, dstProject, srcNode, dstNode, projectModel());
    });
    connect(m_workerThread, &DenoiseWorker::updateProcess, this, &DenoiseNode::onProgressUpdate);
    connect(m_workerThread, &DenoiseWorker::endProcess, this, &DenoiseNode::onProcessingFinished);
    connect(m_workerThread, &DenoiseWorker::errorProcess, this, &DenoiseNode::onError);
    connect(m_workerThread, &DenoiseWorker::sendModel, this, &DenoiseNode::onModelUpdated);
    connect(m_workerThread, &DenoiseWorker::destroyed, m_thread, &QThread::quit);
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

    m_thread->start();
}

void DenoiseNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void DenoiseNode::onProcessingFinished()
{
    QString dstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text().trimmed();
    QString outputPath = projectPath() + "/" + dstNode + "/";

    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;

    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);
        for (const QString& h5File : h5Files) {
            QString h5Path = dir.absoluteFilePath(h5File);
            h5Paths.append(h5Path);
            QString baseName = QFileInfo(h5File).baseName();
            jpgPaths.append(outputPath + baseName + ".jpg");
            types.append("phase");
        }
    }

    // Clean up worker thread
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

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    if (!h5Paths.isEmpty())
    {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, jpgPaths]() {
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);

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
        Q_EMIT dataUpdated(0);
    }
}

void DenoiseNode::onError(const QString& error)
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

    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_prefilterWinEdit) m_prefilterWinEdit->setEnabled(true);
    if (m_slopeWinEdit) m_slopeWinEdit->setEnabled(true);
    if (m_goldsteinWinEdit) m_goldsteinWinEdit->setEnabled(true);
    if (m_nPadEdit) m_nPadEdit->setEnabled(true);
    if (m_alphaEdit) m_alphaEdit->setEnabled(true);
    onMethodChanged(m_method - 1);

    setState(ExecutionState::Error);
}

void DenoiseNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

bool DenoiseNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (!dir.exists())
        return false;

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
        QString baseName = QFileInfo(h5File).baseName();
        h5Paths.append(h5Path);
        expectedJpgPaths.append(outputPath + baseName + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    // Remedy missing JPG previews in background
    QStringList existingJpgPaths;
    QStringList missingH5s;
    QStringList missingJpgs;
    QStringList missingTypes;

    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (QFile::exists(expectedJpgPaths[i])) {
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
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, expectedJpgPaths]() {
            m_imageInfoData = std::make_shared<ImageInfoData>(expectedJpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
            InSARLogManager::LogInfo("DenoiseNode", "validateAndRestoreOutput background rendering completed.");
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

QStringList DenoiseNode::previewImagePaths() const
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
            QString jpgPath = outputPath + baseName + ".jpg";
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
    if (prepareToStart())
    {
        executeProcessing();
    }
}

void DenoiseNode::stopExecution()
{
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
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

} // namespace QtNodes
