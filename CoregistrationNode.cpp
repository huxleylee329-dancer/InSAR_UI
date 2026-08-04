#include "CoregistrationNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "tinyxml.h"
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
#include <QFileDialog>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>


#include "Registration.h"
#include "ImageView.h"
#include "QtNodes/internal/NodeDetailWindow.hpp"
#include <QTimer>
#include <QUuid>
#include <memory>
#include <cmath>

namespace QtNodes {

CoregistrationNode::CoregistrationNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_method("Coarse")
    , m_defaultFirstMaster(true)
    , m_masterIndex(1)
    , m_interpTimes(4)
    , m_blockSize(64)
    , m_demPath("")
    , m_outputNodeName("Coregistration")
    , m_outputFileName("{InputName}_regis")
    , m_worker(nullptr)
    , m_thread(nullptr)
    , m_isExecuting(false)
{
    setExecutionMode(ExecutionMode::Automatic);
}

CoregistrationNode::~CoregistrationNode()
{
    stopExecution();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.waitForFinished();
    }
    for (const QString& temporaryJpgPath : m_previewTemporaryJpgPaths) {
        QFile::remove(temporaryJpgPath);
    }
}

unsigned int CoregistrationNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType CoregistrationNode::dataType(PortType portType, PortIndex portIndex) const
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

bool CoregistrationNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString CoregistrationNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0)
            return QStringLiteral("输入数据");
        else
            return QStringLiteral("DEM ?");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else
            return QStringLiteral("预览 ?");
    }
}

bool CoregistrationNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1)
        return true;
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void CoregistrationNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
        if (!m_inputData || m_inputData->filePaths().isEmpty()) {
            if (!isRestoring()) {
                m_outputData.reset();
                m_previewData.reset();
                m_outputImagePaths.clear();
                m_outputJpgPaths.clear();
                m_savedOutputFiles.clear();
                setOutputData(0, nullptr);
                setOutputData(1, nullptr);
            }
        }
        updateMasterImageCombo();
        if (m_inputData && m_outputNodeName.isEmpty()) {
            m_outputNodeName = "Coregistration";
            if (m_outputNodeNameEdit) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
        }
    } else if (port == 1) {
        m_demInputData = std::dynamic_pointer_cast<ImportedFileData>(data);
        if (m_demInputData) {
            m_demPath = m_demInputData->filePath();
            if (m_demPathEdit) {
                m_demPathEdit->setText(m_demPath);
            }
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

std::shared_ptr<NodeData> CoregistrationNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

QWidget* CoregistrationNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void CoregistrationNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);

    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_previewData) m_previewData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
    };

    // 1. formLayout1 (Top parameters)
    QFormLayout* formLayout1 = new QFormLayout();
    formLayout1->setContentsMargins(0, 0, 0, 0);
    formLayout1->setSpacing(6);
    formLayout1->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout1->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_methodCombo = new QComboBox();
    m_methodCombo->addItem(QStringLiteral("强度图配准 (Coarse)"), "Coarse");
    m_methodCombo->addItem(QStringLiteral("DEM辅助配准 (Fine)"), "Fine");
    m_methodCombo->setCurrentIndex(m_method == "Coarse" ? 0 : 1);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        QString newMethod = m_methodCombo->itemData(index).toString();
        if (m_method != newMethod) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_methodCombo);
                m_methodCombo->setCurrentIndex(m_method == "Coarse" ? 0 : 1);
                return;
            }
            m_method = newMethod;
            updateWidgetSize();
            updateParameterWidgetsEnableState();
            invalidateNodeData();
        }
    });
    QLabel* methodLabel = new QLabel(QStringLiteral("配准模式："));
    methodLabel->setFixedWidth(100);
    formLayout1->addRow(methodLabel, m_methodCombo);

    m_defaultFirstMasterCheckBox = new QCheckBox(QStringLiteral("默认首张图像为主图像"));
    m_defaultFirstMasterCheckBox->setChecked(m_defaultFirstMaster);
    connect(m_defaultFirstMasterCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool newState = (state == Qt::Checked);
        if (m_defaultFirstMaster != newState) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_defaultFirstMasterCheckBox);
                m_defaultFirstMasterCheckBox->setChecked(m_defaultFirstMaster);
                return;
            }
            m_defaultFirstMaster = newState;
            updateMasterImageCombo();
            updateParameterWidgetsEnableState();
            invalidateNodeData();
        }
    });

    m_masterImageCombo = new QComboBox();
    connect(m_masterImageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        if (!m_defaultFirstMaster && m_inputData && index >= 0) {
            int newIndex = index + 1;
            if (m_masterIndex != newIndex) {
                if (!confirmParameterChange()) {
                    QSignalBlocker blocker(m_masterImageCombo);
                    m_masterImageCombo->setCurrentIndex(m_masterIndex - 1);
                    return;
                }
                m_masterIndex = newIndex;
                invalidateNodeData();
            }
        }
    });
    QLabel* masterLabel = new QLabel(QStringLiteral("主图像："));
    masterLabel->setFixedWidth(100);
    formLayout1->addRow(masterLabel, m_masterImageCombo);

    layout->addLayout(formLayout1);
    layout->addWidget(m_defaultFirstMasterCheckBox);

    // 2. Coarse parameters container
    m_coarseParamsWidget = new QWidget();
    QVBoxLayout* coarseLayout = new QVBoxLayout(m_coarseParamsWidget);
    coarseLayout->setContentsMargins(0, 0, 0, 0);
    coarseLayout->setSpacing(6);

    QHBoxLayout* interpRow = new QHBoxLayout();
    m_interpLabel = new QLabel(QStringLiteral("插值倍数："));
    m_interpLabel->setFixedWidth(100);
    m_interpCombo = new QComboBox();
    m_interpCombo->addItems(QStringList() << "2" << "4" << "8" << "16");
    m_interpCombo->setCurrentText(QString::number(m_interpTimes));
    connect(m_interpCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        int val = text.toInt();
        if (m_interpTimes != val) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_interpCombo);
                m_interpCombo->setCurrentText(QString::number(m_interpTimes));
                return;
            }
            m_interpTimes = val;
            invalidateNodeData();
        }
    });
    interpRow->addWidget(m_interpLabel);
    interpRow->addWidget(m_interpCombo);
    coarseLayout->addLayout(interpRow);

    QHBoxLayout* blockSizeRow = new QHBoxLayout();
    m_blockSizeLabel = new QLabel(QStringLiteral("块大小："));
    m_blockSizeLabel->setFixedWidth(100);
    m_blockSizeCombo = new QComboBox();
    m_blockSizeCombo->addItems(QStringList() << "32" << "64" << "128" << "256");
    m_blockSizeCombo->setCurrentText(QString::number(m_blockSize));
    connect(m_blockSizeCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        int val = text.toInt();
        if (m_blockSize != val) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_blockSizeCombo);
                m_blockSizeCombo->setCurrentText(QString::number(m_blockSize));
                return;
            }
            m_blockSize = val;
            invalidateNodeData();
        }
    });
    blockSizeRow->addWidget(m_blockSizeLabel);
    blockSizeRow->addWidget(m_blockSizeCombo);
    coarseLayout->addLayout(blockSizeRow);

    layout->addWidget(m_coarseParamsWidget);

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
    connect(m_demBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择DEM数据"), "", "DEM Files (*.h5 *.tiff *.tif)");
        if (!file.isEmpty()) {
            m_demPath = file;
            if (m_demPathEdit) m_demPathEdit->setText(m_demPath);
            if (m_outputData) m_outputData.reset();
            if (m_previewData) m_previewData.reset();
            setOutputData(0, nullptr);
            setOutputData(1, nullptr);
            invalidateExecution();
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_demPath, true);
            }
        }
    });

    // 3. formLayout2 (Output parameters)
    QFormLayout* formLayout2 = new QFormLayout();
    formLayout2->setContentsMargins(0, 0, 0, 0);
    formLayout2->setSpacing(6);
    formLayout2->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout2->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    QLabel* outNodeLabel = new QLabel(QStringLiteral("输出节点名："));
    outNodeLabel->setFixedWidth(100);
    formLayout2->addRow(outNodeLabel, m_outputNodeNameEdit);

    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setText(m_outputFileName);
    connect(m_outputFileNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputFileNameEdit->text().trimmed();
        if (m_outputFileName != text) {
            m_outputFileName = text;
            invalidateNodeData();
        }
    });
    QLabel* outFileLabel = new QLabel(QStringLiteral("文件名命名规则："));
    outFileLabel->setFixedWidth(100);
    formLayout2->addRow(outFileLabel, m_outputFileNameEdit);

    layout->addLayout(formLayout2);

    // 4. DEM Row container
    m_demRowWidget = new QWidget();
    QHBoxLayout* demRow = new QHBoxLayout(m_demRowWidget);
    demRow->setContentsMargins(0, 0, 0, 0);
    demRow->setSpacing(6);

    m_demPathLabel = new QLabel(QStringLiteral("DEM路径："));
    m_demPathLabel->setFixedWidth(100);
    m_demPathLabel->setStyleSheet("QLabel:disabled { color: #888888; }");

    demRow->addWidget(m_demPathLabel);
    demRow->addWidget(m_demPathEdit);
    demRow->addWidget(m_demBrowseBtn);

    layout->addWidget(m_demRowWidget);

    updateWidgetSize();
    updateParameterWidgetsEnableState();
}

void CoregistrationNode::updateMasterImageCombo()
{
    if (!m_masterImageCombo) return;

    m_masterImageCombo->clear();

    if (m_defaultFirstMaster) {
        m_masterImageCombo->addItem(QStringLiteral("自动选择首张图像..."));
    } else {
        if (m_inputData) {
            QStringList paths = m_inputData->filePaths();
            for (const QString& path : paths) {
                m_masterImageCombo->addItem(QFileInfo(path).fileName());
            }
            if (m_masterIndex > 0 && m_masterIndex <= m_masterImageCombo->count()) {
                m_masterImageCombo->setCurrentIndex(m_masterIndex - 1);
            } else {
                m_masterImageCombo->setCurrentIndex(0);
                m_masterIndex = 1;
            }
        } else {
            m_masterImageCombo->addItem(QStringLiteral("无数据输入"));
        }
    }
    updateParameterWidgetsEnableState();
}

void CoregistrationNode::updateWidgetSize()
{
    if (!_widget) return;

    bool isCoarse = (m_method == "Coarse");

    if (m_coarseParamsWidget) m_coarseParamsWidget->setVisible(isCoarse);
    if (m_demRowWidget) m_demRowWidget->setVisible(!isCoarse);

    _widget->adjustSize();
}

void CoregistrationNode::updateParameterWidgetsEnableState()
{
    bool hasInput = (m_inputData && !m_inputData->filePaths().isEmpty());
    bool isExec = m_isExecuting;
    bool enableWidgets = hasInput && !isExec;

    if (m_methodCombo) m_methodCombo->setEnabled(enableWidgets);
    if (m_defaultFirstMasterCheckBox) m_defaultFirstMasterCheckBox->setEnabled(enableWidgets);
    if (m_masterImageCombo) m_masterImageCombo->setEnabled(enableWidgets && !m_defaultFirstMaster);

    bool isCoarse = (m_method == "Coarse");
    bool hasDemConn = (m_demInputData != nullptr);
    if (m_interpCombo) { m_interpCombo->setEnabled(false); m_interpCombo->setToolTip(QStringLiteral("底层算法已升级为抛物线拟合，无需网格插值")); }
    if (m_blockSizeCombo) m_blockSizeCombo->setEnabled(enableWidgets && isCoarse);
    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !isCoarse && !hasDemConn);
    if (m_demPathEdit) m_demPathEdit->setEnabled(enableWidgets && !isCoarse && !hasDemConn);
    if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(enableWidgets && !isCoarse && !hasDemConn);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setEnabled(enableWidgets);
}

QStringList CoregistrationNode::previewImagePaths() const
{
    QStringList validPaths;
    for (const QString& path : m_outputJpgPaths) {
        if (QFileInfo::exists(path)) {
            validPaths << path;
        }
    }
    return validPaths;
}

QString CoregistrationNode::getRealSavePath() const
{
    QString path = projectPath();
    if (path.endsWith(".insar", Qt::CaseInsensitive)) {
        return QFileInfo(path).absolutePath();
    }
    return path;
}

QString CoregistrationNode::resolveOutputFileName(const QString& originalName) const
{
    QString pattern = m_outputFileName.trimmed();
    if (pattern.isEmpty()) {
        pattern = "{InputName}_regis";
    }
    // Normalize brackets
    QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
    pattern.replace(re, "{InputName}");
    if (pattern.contains("{InputName}")) {
        pattern.replace("{InputName}", originalName);
    } else {
        pattern = originalName + "_" + pattern;
    }
    return pattern;
}

bool CoregistrationNode::isReady() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;
    if (projectPath().isEmpty() || projectName().isEmpty()) return false;
    if (m_outputNodeName.trimmed().isEmpty()) return false;

    if (m_method == "Fine") {
        if (m_demPath.trimmed().isEmpty()) return false;
        if (!QFileInfo(m_demPath).exists()) return false;
    }

    return true;
}

void CoregistrationNode::processAutomatically()
{
    if (m_worker || m_thread) {
        deferAutomaticCompletion();
        return;
    }

    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

void CoregistrationNode::execute()
{
    executeProcessing();
}

bool CoregistrationNode::prepareToStart()
{
    if (m_worker || m_thread) {
        return false;
    }
    if (!isReady()) {
        InSARLogManager::LogWarning("CoregistrationNode", "prepareToStart skipped: node not ready.");
        return false;
    }

    m_preparedInputPaths = m_inputData->filePaths();
    m_preparedSavePath = getRealSavePath();
    m_preparedDstNode = m_outputNodeName.trimmed();
    m_preparedProjectName = projectName();
    if (m_preparedInputPaths.isEmpty() || m_preparedSavePath.isEmpty() ||
        m_preparedDstNode.isEmpty() || m_preparedProjectName.isEmpty()) {
        return false;
    }

    m_preparedOutputNames.clear();
    m_preparedH5Paths.clear();
    m_preparedJpgPaths.clear();

    for (const QString& path : m_preparedInputPaths) {
        QString origName = QFileInfo(path).completeBaseName();
        QString outName = resolveOutputFileName(origName);
        if (!outName.endsWith(".h5", Qt::CaseInsensitive)) {
            outName += ".h5";
        }
        m_preparedOutputNames.append(QFileInfo(outName).completeBaseName());
        m_preparedH5Paths.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + outName);
        m_preparedJpgPaths.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" +
                                  QFileInfo(outName).completeBaseName() + ".jpg");
    }

    auto* iface = NodeUtils::getProjectContext(_widget);

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            iface, m_preparedDstNode, m_preparedH5Paths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void CoregistrationNode::executeProcessing()
{
    InSARLogManager::LogInfo("CoregistrationNode", "executeProcessing started.");

    if (m_worker || m_thread) {
        stopExecution();
    }

    const QString dstNode = m_preparedDstNode;
    const QString savePath = m_preparedSavePath;
    const QString project = m_preparedProjectName;
    if (dstNode.isEmpty() || savePath.isEmpty() || project.isEmpty() ||
        m_preparedInputPaths.isEmpty() || m_preparedH5Paths.isEmpty()) {
        onError(QStringLiteral("Coregistration output was not prepared before execution."));
        return;
    }

    m_generatedOutputNames.clear();
    m_generatedOutputPaths.clear();
    m_generatedOffsetRows.clear();
    m_generatedOffsetCols.clear();
    m_generatedTemporalBaseline.clear();
    m_generatedEffectiveBaseline.clear();
    m_generatedParallelBaseline.clear();

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (validateAndRestoreOutput()) {
            setState(ExecutionState::Running);
            setProgress(100);
            if (m_previewGenerationPending) {
                m_isExecuting = true;
                updateParameterWidgetsEnableState();
                deferAutomaticCompletion();
            } else {
                finishExecution();
            }
            return;
        } else {
            QMessageBox::warning(nullptr, QStringLiteral("警告"), QStringLiteral("加载已有文件失败，将开始重新计算！"));
        }
    }

    setProgress(0);
    setState(ExecutionState::Running);
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(savePath, dstNode, m_preparedH5Paths,
                                           m_preparedInputPaths, m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_xmlDirty = false;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;

    m_worker = new CoregistrationWorker();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    m_worker->setDemPath(m_demPath);
    m_worker->setFilePattern(m_outputFileName);

    connect(m_worker, &CoregistrationWorker::updateProcess, this, &CoregistrationNode::onProgressUpdate, Qt::QueuedConnection);
    connect(m_worker, &CoregistrationWorker::outputsGenerated, this,
        [this](const QStringList& names, const QStringList& paths,
               const QList<int>& rows, const QList<int>& cols,
               const QString& temporalBaseline, const QString& effectiveBaseline,
               const QString& parallelBaseline) {
            m_generatedOutputNames = names;
            m_generatedOutputPaths = paths;
            m_generatedOffsetRows = rows;
            m_generatedOffsetCols = cols;
            m_generatedTemporalBaseline = temporalBaseline;
            m_generatedEffectiveBaseline = effectiveBaseline;
            m_generatedParallelBaseline = parallelBaseline;
        }, Qt::QueuedConnection);
    connect(m_worker, &CoregistrationWorker::endProcess, this, &CoregistrationNode::onProcessingFinished, Qt::QueuedConnection);
    connect(m_worker, &CoregistrationWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &CoregistrationWorker::cancelled, this, &CoregistrationNode::onCancelled, Qt::QueuedConnection);
    connect(m_worker, &CoregistrationWorker::cancelled, m_thread, &QThread::quit);
    connect(m_worker, &CoregistrationWorker::errorProcess, this, &CoregistrationNode::onError, Qt::QueuedConnection);
    connect(m_worker, &CoregistrationWorker::errorProcess, m_thread, &QThread::quit);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_isExecuting = true;
    m_thread->start(QThread::LowestPriority);

    int masterIdx = m_defaultFirstMaster ? 1 : m_masterIndex;

    if (m_method == "Coarse") {
        QList<int> para;
        para.push_back(masterIdx);
        para.push_back(m_interpTimes);
        para.push_back(m_blockSize);
        para.push_back(m_preparedInputPaths.size());

        // Invoke Regis slot inside worker thread
        QMetaObject::invokeMethod(m_worker, "Regis", Qt::QueuedConnection,
            Q_ARG(QList<int>, para),
            Q_ARG(QString, savePath),
            Q_ARG(QString, project),
            Q_ARG(QString, m_outputTransaction.stagingName),
            Q_ARG(QStringList, m_preparedInputPaths));
    } else {
        // Invoke DEMAssistCoregistration slot inside worker thread
        QMetaObject::invokeMethod(m_worker, "DEMAssistCoregistration", Qt::QueuedConnection,
            Q_ARG(int, masterIdx),
            Q_ARG(QString, savePath),
            Q_ARG(QString, project),
            Q_ARG(QString, m_outputTransaction.stagingName),
            Q_ARG(QStringList, m_preparedInputPaths));
    }

    updateParameterWidgetsEnableState();

    setState(ExecutionState::Running);
    deferAutomaticCompletion();

    InSARLogManager::LogInfo("CoregistrationNode", "executeProcessing thread started successfully.");
}

void CoregistrationNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
        if (m_thread && m_thread->isRunning()) {
            m_thread->requestInterruption();
        }
        return;
    }

    if (m_previewGenerationPending || m_remedyWatcher.isRunning()) {
        m_previewGenerationPending = false;
        ++m_previewGenerationId;
        const quint64 cleanupGenerationId = m_previewGenerationId;
        const QStringList temporaryJpgPaths = m_previewTemporaryJpgPaths;
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.disconnect(this);
            connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                    [this, temporaryJpgPaths, cleanupGenerationId]() {
                for (const QString& temporaryJpgPath : temporaryJpgPaths) {
                    QFile::remove(temporaryJpgPath);
                }
                if (m_previewGenerationId == cleanupGenerationId) {
                    m_previewTemporaryJpgPaths.clear();
                }
            });
            m_remedyWatcher.cancel();
        } else {
            for (const QString& temporaryJpgPath : temporaryJpgPaths) {
                QFile::remove(temporaryJpgPath);
            }
            m_previewTemporaryJpgPaths.clear();
        }
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("preview generation cancelled"), projectXml());
        m_isExecuting = false;
        m_outputData.reset();
        m_previewData.reset();
        m_outputImagePaths.clear();
        m_outputJpgPaths.clear();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        if (discardObsoleteAutomaticExecution()) {
            return;
        }
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
    }
    updateParameterWidgetsEnableState();
}

void CoregistrationNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void CoregistrationNode::onProcessingFinished()
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (isAutomaticExecutionObsolete()) {
        m_previewGenerationPending = false;
        ++m_previewGenerationId;
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
        }
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete automatic execution"), projectXml());
        m_isExecuting = false;
        m_outputData.reset();
        m_previewData.reset();
        m_outputImagePaths.clear();
        m_outputJpgPaths.clear();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        discardObsoleteAutomaticExecution();
        return;
    }

    if (m_generatedOutputPaths.isEmpty() ||
        m_generatedOutputNames.size() != m_generatedOutputPaths.size() ||
        m_preparedH5Paths.size() != m_generatedOutputPaths.size() ||
        m_generatedOffsetRows.size() != m_generatedOutputPaths.size() ||
        m_generatedOffsetCols.size() != m_generatedOutputPaths.size()) {
        onError(QStringLiteral("Coregistration did not return a complete output result."));
        return;
    }

    for (int i = 0; i < m_generatedOutputPaths.size(); ++i) {
        const QString expectedName = QFileInfo(m_preparedH5Paths[i]).completeBaseName();
        if (QFileInfo(m_generatedOutputPaths[i]).fileName() != QFileInfo(m_preparedH5Paths[i]).fileName() ||
            m_generatedOutputNames[i] != expectedName) {
            onError(QStringLiteral("Coregistration worker output names do not match the prepared manifest."));
            return;
        }
    }

    QString transactionError;
    QStringList h5Paths;
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
                                             QStringList() << QStringLiteral("s_re") << QStringLiteral("s_im"),
                                             &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedH5Paths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError.isEmpty()
                    ? QStringLiteral("Coregistration output transaction validation failed.")
                    : transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_preparedDstNode, false, false);
    m_xmlDirty = commitResultsToProjectXml(m_generatedOutputNames, h5Paths,
                                           m_generatedOffsetRows, m_generatedOffsetCols,
                                           m_generatedTemporalBaseline, m_generatedEffectiveBaseline,
                                           m_generatedParallelBaseline);
    if (!m_xmlDirty ||
        !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty()
                    ? QStringLiteral("Coregistration output metadata was not produced.")
                    : transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), m_preparedDstNode);
    publishResultsToProjectTree(m_generatedOutputNames, h5Paths);

    m_savedOutputFiles = m_generatedOutputNames;
    m_outputImagePaths = h5Paths;
    m_outputJpgPaths.clear();
    for (const QString& h5Path : m_outputImagePaths) {
        m_outputJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg");
    }

    m_outputData = std::make_shared<ImportedFileData>(m_outputImagePaths, m_preparedDstNode);
    setOutputData(0, m_outputData);

    InSARLogManager::LogInfo("CoregistrationNode", "Coregistration process finished. Generating previews...");
    startPreviewGeneration(m_outputImagePaths, m_outputJpgPaths, true);
}

void CoregistrationNode::onError(const QString& error)
{
    m_worker = nullptr;
    m_thread = nullptr;
    m_isExecuting = false;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());

    m_outputData.reset();
    m_previewData.reset();
    m_outputImagePaths.clear();
    m_outputJpgPaths.clear();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    setLastErrorMessage(error);
    InSARLogManager::LogError("CoregistrationNode", error);
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    updateParameterWidgetsEnableState();
}

void CoregistrationNode::onCancelled()
{
    InSARLogManager::LogInfo("CoregistrationNode", "Coregistration cancellation cleanup completed.");
    m_worker = nullptr;
    m_thread = nullptr;
    m_isExecuting = false;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());

    m_outputData.reset();
    m_previewData.reset();
    m_outputImagePaths.clear();
    m_outputJpgPaths.clear();
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

bool CoregistrationNode::commitResultsToProjectXml(const QStringList& outputNames,
                                                    const QStringList& outputPaths,
                                                    const QList<int>& offsetRows,
                                                    const QList<int>& offsetCols,
                                                    const QString& temporalBaseline,
                                                    const QString& effectiveBaseline,
                                                    const QString& parallelBaseline)
{
    if (outputNames.size() != outputPaths.size() ||
        outputPaths.size() != offsetRows.size() ||
        outputPaths.size() != offsetCols.size()) {
        InSARLogManager::LogError("CoregistrationNode", "Generated output metadata is inconsistent.");
        return false;
    }

    XMLFile* xml = projectXml();
    if (!xml || m_preparedDstNode.isEmpty()) {
        return false;
    }
    const int masterIndex = m_defaultFirstMaster ? 1 : m_masterIndex;
    const int interpTimes = m_method == "Coarse" ? m_interpTimes : -1;
    const int blockSize = m_method == "Coarse" ? m_blockSize : -1;
    for (int i = 0; i < outputPaths.size(); ++i) {
        const QString relativePath = QString("/%1/%2").arg(m_preparedDstNode, QFileInfo(outputPaths[i]).fileName());
        if (xml->XMLFile_add_regis(m_preparedDstNode.toStdString().c_str(), outputNames[i].toStdString().c_str(),
                                   relativePath.toStdString().c_str(), offsetRows[i], offsetCols[i], masterIndex,
                                   interpTimes, blockSize, temporalBaseline.toStdString().c_str(),
                                   effectiveBaseline.toStdString().c_str(), parallelBaseline.toStdString().c_str()) < 0) {
            return false;
        }
    }
    return true;
}

void CoregistrationNode::publishResultsToProjectTree(const QStringList& outputNames,
                                                      const QStringList& outputPaths)
{
    if (outputNames.size() != outputPaths.size()) {
        return;
    }

    QStandardItemModel* model = projectModel();
    const QList<QStandardItem*> projects = model ? model->findItems(m_preparedProjectName) : QList<QStandardItem*>();
    if (projects.isEmpty()) {
        InSARLogManager::LogError("CoregistrationNode", "Project tree root was not found.");
        return;
    }

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), m_preparedDstNode, "complex-2.0", FOLDER_ICON);
    if (!outputNode) {
        InSARLogManager::LogError("CoregistrationNode", "Unable to create coregistration output node.");
        return;
    }
    outputNode->setToolTip(m_preparedProjectName);
    for (int i = 0; i < outputPaths.size(); ++i) {
        QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
            outputNode, outputNames[i], "complex", outputPaths[i], IMAGEDATA_ICON);
        if (imageItem) {
            outputNode->setChild(imageItem->row(), 1, new QStandardItem(outputPaths[i]));
        }
    }
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }
}

QJsonObject CoregistrationNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["method"] = m_method;
    modelJson["defaultFirstMaster"] = m_defaultFirstMaster;
    modelJson["masterIndex"] = m_masterIndex;
    modelJson["interpTimes"] = m_interpTimes;
    modelJson["blockSize"] = m_blockSize;
    modelJson["demPath"] = m_demPath;
    modelJson["outputNodeName"] = m_outputNodeName;
    modelJson["outputFileName"] = m_outputFileName;

    QJsonArray h5FilesArr;
    for (const QString& path : m_outputImagePaths) {
        h5FilesArr.append(QFileInfo(path).fileName());
    }
    modelJson["outputFiles"] = h5FilesArr;

    return modelJson;
}

void CoregistrationNode::load(QJsonObject const &json)
{
    m_method = json["method"].toString("Coarse");
    m_defaultFirstMaster = json["defaultFirstMaster"].toBool(true);
    m_masterIndex = json["masterIndex"].toInt(1);
    m_interpTimes = json["interpTimes"].toInt(4);
    m_blockSize = json["blockSize"].toInt(64);
    m_demPath = json["demPath"].toString("");
    m_outputNodeName = json["outputNodeName"].toString("Coregistration");
    m_outputFileName = json["outputFileName"].toString("{InputName}_regis");

    m_savedOutputFiles.clear();
    if (json.contains("outputFiles")) {
        QJsonArray arr = json["outputFiles"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            m_savedOutputFiles.append(arr[i].toString());
        }
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_methodCombo) {
        m_methodCombo->setCurrentIndex(m_method == "Coarse" ? 0 : 1);
    }
    if (m_defaultFirstMasterCheckBox) {
        m_defaultFirstMasterCheckBox->setChecked(m_defaultFirstMaster);
    }
    if (m_interpCombo) {
        m_interpCombo->setCurrentText(QString::number(m_interpTimes));
    }
    if (m_blockSizeCombo) {
        m_blockSizeCombo->setCurrentText(QString::number(m_blockSize));
    }
    if (m_demPathEdit) {
        m_demPathEdit->setText(m_demPath);
    }
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }
    if (m_outputFileNameEdit) {
        m_outputFileNameEdit->setText(m_outputFileName);
    }

    updateMasterImageCombo();
    updateWidgetSize();
}

bool CoregistrationNode::validateAndRestoreOutput()
{
#if 0
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty()) return false;

    QString projDir = getRealSavePath();
    if (projDir.isEmpty()) return false;

    // Determine expected files
    QStringList expectedH5Paths;
    QStringList expectedJpgPaths;

    if (!m_savedOutputFiles.isEmpty()) {
        for (const QString& file : m_savedOutputFiles) {
            QString h5File = file;
            if (!h5File.endsWith(".h5", Qt::CaseInsensitive)) h5File += ".h5";
            expectedH5Paths.append(projDir + "/" + nodeName + "/" + h5File);
            expectedJpgPaths.append(projDir + "/" + nodeName + "/" + QFileInfo(h5File).completeBaseName() + ".jpg");
        }
    } else if (m_inputData) {
        // Fallback reconstruction
        for (const QString& path : m_inputData->filePaths()) {
            QString origName = QFileInfo(path).completeBaseName();
            QString outName = resolveOutputFileName(origName);
            if (!outName.endsWith(".h5", Qt::CaseInsensitive)) outName += ".h5";
            expectedH5Paths.append(projDir + "/" + nodeName + "/" + outName);
            expectedJpgPaths.append(projDir + "/" + nodeName + "/" + QFileInfo(outName).completeBaseName() + ".jpg");
        }
    } else {
        return false;
    }

    // Verify all H5 files exist
    for (const QString& path : expectedH5Paths) {
        bool exists = QFile::exists(path);
        if (!exists) return false;
    }

    m_outputImagePaths = expectedH5Paths;
    m_outputJpgPaths = expectedJpgPaths;

    // Asynchronous JPG preview generation
    QStringList missingH5s;
    QStringList missingJpgs;
    for (int i = 0; i < expectedH5Paths.size(); ++i) {
        if (!NodeUtils::isJpgPreviewCurrent(expectedH5Paths[i], expectedJpgPaths[i])) {
            missingH5s.append(expectedH5Paths[i]);
            missingJpgs.append(expectedJpgPaths[i]);
        }
    }

    if (!missingH5s.isEmpty()) {
        startPreviewGeneration(missingH5s, missingJpgs, expectedH5Paths, expectedJpgPaths, false);
    } else {
        m_previewData = std::make_shared<ImageInfoData>(expectedJpgPaths);
        setOutputData(1, m_previewData);
    }

    m_outputData = std::make_shared<ImportedFileData>(expectedH5Paths, nodeName);
    setOutputData(0, m_outputData);
    if (missingH5s.isEmpty()) {
        Q_EMIT dataUpdated(1);
    }

    // Restore Standard Item Model Tree View
    QStandardItemModel* projModelPtr = projectModel();
    if (projModelPtr) {
        QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
        if (!foundProjects.isEmpty()) {
            QStandardItem* projectItem = foundProjects.first();

            QStandardItem* regisItem = nullptr;
            for (int i = 0; i < projectItem->rowCount(); i++) {
                if (projectItem->child(i, 0)->text() == nodeName) {
                    regisItem = projectItem->child(i, 0);
                    break;
                }
            }

            if (!regisItem) {
                regisItem = new QStandardItem(nodeName);
                regisItem->setToolTip(projectName());
                int insert = 0;
                for (; insert < projectItem->rowCount(); insert++) {
                    if (projectItem->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                        projectItem->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                        projectItem->child(insert, 1)->text().compare("complex-2.0") == 0)
                        continue;
                    else
                        break;
                }
                regisItem->setIcon(QIcon(FOLDER_ICON));
                projectItem->insertRow(insert, regisItem);
                QStandardItem* regisRank = new QStandardItem("complex-2.0");
                projectItem->setChild(insert, 1, regisRank);
            }

            for (const QString& h5Path : expectedH5Paths) {
                QFileInfo fileinfo(h5Path);
                QString regis_img_name = fileinfo.baseName();

                QStandardItem* item_img = nullptr;
                for (int j = 0; j < regisItem->rowCount(); j++) {
                    if (regisItem->child(j, 0)->text() == regis_img_name) {
                        item_img = regisItem->child(j, 0);
                        break;
                    }
                }

                if (!item_img) {
                    QStandardItem* regis_images_name = new QStandardItem(regis_img_name);
                    regis_images_name->setToolTip("complex");
                    QStandardItem* regis_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                    regis_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                    regisItem->appendRow(regis_images_name);
                    regisItem->setChild(regisItem->rowCount() - 1, 1, regis_images_path);
                } else {
                    regisItem->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                }
            }
        }
    }

    // Safe XML restoration using native TinyXML API
    XMLFile* xml = projectXml();
    if (xml) {
        bool xmlModified = false;
        TiXmlElement* root = nullptr;
        xml->get_root(root);
        if (root) {
            bool dataNodeExists = false;
            for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                const char* nameAttr = p->Attribute("name");
                if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == nodeName) {
                    dataNodeExists = true;
                    break;
                }
            }

            if (!dataNodeExists && m_inputData) {
                QStringList inputPaths = m_inputData->filePaths();
                TiXmlElement* dataNodeElem = new TiXmlElement("DataNode");
                dataNodeElem->SetAttribute("name", nodeName.toStdString().c_str());
                dataNodeElem->SetAttribute("data_count", QString::number(inputPaths.size()).toStdString().c_str());
                dataNodeElem->SetAttribute("data_processing", "coregistration");
                dataNodeElem->SetAttribute("rank", "complex-2.0");

                int index = 1;
                TiXmlElement* root_child = root->FirstChildElement();
                if (root_child) {
                    root_child = root_child->NextSiblingElement(); // skip project_info
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

                NodeUtils::Hdf5Locker locker;
                FormatConversion FC;
                Utils util;

                Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
                Mat tmp_double = Mat::zeros(1, 1, CV_64FC1);
                double interp_interval = 0;
                double time_Master = 0;
                string time_master_str;

                int masterIdx = m_defaultFirstMaster ? 1 : m_masterIndex;
                if (masterIdx < 1 || masterIdx > inputPaths.size()) masterIdx = 1;

                QString temporal_baseline, B_parallel, B_effect;

                if (m_method == "Coarse") {
                    QString masterPath = inputPaths.at(masterIdx - 1);
                    NodeUtils::readMatFromH5(masterPath, "state_vec", State_Vec_Master, CV_64F);
                    NodeUtils::readMatFromH5(masterPath, "lon_coefficient", Lon_Coeff_Master, CV_64F);
                    NodeUtils::readMatFromH5(masterPath, "lat_coefficient", Lat_Coeff_Master, CV_64F);

                    double prf = 0.0;
                    NodeUtils::readScalarFromH5(masterPath, "prf", prf);
                    if (prf != 0) {
                        interp_interval = 1 / prf;
                    }
                    NodeUtils::readStringFromH5(masterPath, "acquisition_start_time", time_master_str);
                    FC.utc2gps(time_master_str.c_str(), &time_Master);

                    // Get Rows and Cols from master regis H5
                    int Rows = 0, Cols = 0;
                    QString masterRegisName = resolveOutputFileName(QFileInfo(masterPath).completeBaseName());
                    if (!masterRegisName.endsWith(".h5", Qt::CaseInsensitive)) masterRegisName += ".h5";
                    QString masterRegisPath = projDir + "/" + nodeName + "/" + masterRegisName;
                    NodeUtils::readScalarFromH5(masterRegisPath, "azimuth_len", Rows);
                    NodeUtils::readScalarFromH5(masterRegisPath, "range_len", Cols);

                    for (int i = 0; i < inputPaths.size(); i++) {
                        if (i == masterIdx - 1) {
                            temporal_baseline += "0 ";
                            B_parallel += "0 ";
                            B_effect += "0 ";
                        } else {
                            Mat State_Vec_Slave, Lon_Coeff_Slave, Lat_Coeff_Slave;
                            double interp_interval_slave = 0;
                            double V_baseline = 0, H_baseline = 0;
                            double sigma_V = 0, sigma_H = 0;
                            double time_Slave = 0;
                            string time_slave_str;
                            QString slavePath = inputPaths.at(i);
                            NodeUtils::readMatFromH5(slavePath, "state_vec", State_Vec_Slave, CV_64F);
                            NodeUtils::readMatFromH5(slavePath, "lon_coefficient", Lon_Coeff_Slave, CV_64F);
                            NodeUtils::readMatFromH5(slavePath, "lat_coefficient", Lat_Coeff_Slave, CV_64F);

                            double prf_slave = 0.0;
                            NodeUtils::readScalarFromH5(slavePath, "prf", prf_slave);
                            if (prf_slave != 0) {
                                interp_interval_slave = 1 / prf_slave;
                            }
                            NodeUtils::readStringFromH5(slavePath, "acquisition_start_time", time_slave_str);
                            FC.utc2gps(time_slave_str.c_str(), &time_Slave);
                            double delta = (time_Slave - time_Master) / 60 / 60 / 24;
                            char tmp_d2s[512];
                            sprintf_s(tmp_d2s, "%.4f", delta);
                            temporal_baseline += QString("%1 ").arg(QString(tmp_d2s));

                            double offset_row = 0.0, offset_col = 0.0;
                            QString regisName = resolveOutputFileName(QFileInfo(slavePath).completeBaseName());
                            if (!regisName.endsWith(".h5", Qt::CaseInsensitive)) regisName += ".h5";
                            QString regisPath = projDir + "/" + nodeName + "/" + regisName;
                            NodeUtils::readScalarFromH5(regisPath, "offset_row", offset_row);
                            NodeUtils::readScalarFromH5(regisPath, "offset_col", offset_col);

                            util.baseline_estimation(State_Vec_Master, State_Vec_Slave, Lon_Coeff_Master, Lat_Coeff_Master,
                                offset_row, offset_col, Rows, Cols, interp_interval, interp_interval_slave, &V_baseline, &H_baseline, &sigma_V, &sigma_H);

                            sprintf_s(tmp_d2s, "%.2f", V_baseline);
                            B_parallel += QString("%1 ").arg(QString(tmp_d2s));
                            sprintf_s(tmp_d2s, "%.2f", H_baseline);
                            B_effect += QString("%1 ").arg(QString(tmp_d2s));
                        }
                    }
                } else {
                    for (int i = 0; i < inputPaths.size(); ++i) {
                        temporal_baseline += "0 ";
                        B_parallel += "0 ";
                        B_effect += "0 ";
                    }
                }

                for (int i = 0; i < inputPaths.size(); i++) {
                    QString origName = QFileInfo(inputPaths[i]).completeBaseName();
                    QString outName = resolveOutputFileName(origName);
                    if (!outName.endsWith(".h5", Qt::CaseInsensitive)) outName += ".h5";
                    QString relativePath = QString("/%1/%2").arg(nodeName).arg(outName);
                    QString outH5Path = projDir + "/" + nodeName + "/" + outName;

                    double rowOffset = 0.0, colOffset = 0.0;
                    NodeUtils::readScalarFromH5(outH5Path, "offset_row", rowOffset);
                    NodeUtils::readScalarFromH5(outH5Path, "offset_col", colOffset);

                    TiXmlElement* dataElem = new TiXmlElement("Data");

                    TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                    dataNameNode->LinkEndChild(new TiXmlText(QFileInfo(outName).completeBaseName().toStdString().c_str()));
                    dataElem->LinkEndChild(dataNameNode);

                    TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                    dataRankNode->LinkEndChild(new TiXmlText("complex-2.0"));
                    dataElem->LinkEndChild(dataRankNode);

                    TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                    dataIndexNode->LinkEndChild(new TiXmlText(QString::number(i + 1).toStdString().c_str()));
                    dataElem->LinkEndChild(dataIndexNode);

                    TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                    dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                    dataElem->LinkEndChild(dataPathNode);

                    TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
                    rowOffsetNode->LinkEndChild(new TiXmlText(QString::number(rowOffset, 'g', 17).toStdString().c_str()));
                    dataElem->LinkEndChild(rowOffsetNode);

                    TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
                    colOffsetNode->LinkEndChild(new TiXmlText(QString::number(colOffset, 'g', 17).toStdString().c_str()));
                    dataElem->LinkEndChild(colOffsetNode);

                    dataNodeElem->LinkEndChild(dataElem);
                }

                TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");

                TiXmlElement* masterImgNode = new TiXmlElement("master_image");
                masterImgNode->LinkEndChild(new TiXmlText(QString::number(masterIdx).toStdString().c_str()));
                paramsElem->LinkEndChild(masterImgNode);

                TiXmlElement* blocksizeNode = new TiXmlElement("blocksize");
                blocksizeNode->LinkEndChild(new TiXmlText(QString::number(m_blockSize).toStdString().c_str()));
                paramsElem->LinkEndChild(blocksizeNode);

                TiXmlElement* interpTimesNode = new TiXmlElement("interp_times");
                interpTimesNode->LinkEndChild(new TiXmlText(QString::number(m_interpTimes).toStdString().c_str()));
                paramsElem->LinkEndChild(interpTimesNode);

                TiXmlElement* tempBaselineNode = new TiXmlElement("temporal_baseline_distribution");
                tempBaselineNode->SetAttribute("unit", "day");
                tempBaselineNode->LinkEndChild(new TiXmlText(temporal_baseline.toStdString().c_str()));
                paramsElem->LinkEndChild(tempBaselineNode);

                TiXmlElement* effectBaselineNode = new TiXmlElement("effect_baseline_distribution");
                effectBaselineNode->SetAttribute("unit", "m");
                effectBaselineNode->LinkEndChild(new TiXmlText(B_effect.toStdString().c_str()));
                paramsElem->LinkEndChild(effectBaselineNode);

                TiXmlElement* parallelBaselineNode = new TiXmlElement("parallel_baseline_distribution");
                parallelBaselineNode->SetAttribute("unit", "m");
                parallelBaselineNode->LinkEndChild(new TiXmlText(B_parallel.toStdString().c_str()));
                paramsElem->LinkEndChild(parallelBaselineNode);

                dataNodeElem->LinkEndChild(paramsElem);

                if (insertBeforeNode) {
                    root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                    delete dataNodeElem;
                    for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement()) {
                        index++;
                        p->SetAttribute("index", QString::number(index).toStdString().c_str());
                    }
                } else {
                    root->LinkEndChild(dataNodeElem);
                }

                xmlModified = true;
            }
        }
        if (xmlModified) {
            QString xmlPath = projectPath() + "/" + projectName();
            xml->XMLFile_save(xmlPath.toStdString().c_str());
        }
    }

    auto* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }

    return true;
#endif

    const QString dstNode = m_preparedDstNode.isEmpty()
        ? m_outputNodeName.trimmed()
        : m_preparedDstNode;
    QStringList h5Paths;
    if (dstNode.isEmpty() ||
        !NodeUtils::loadCommittedOutputManifest(getRealSavePath(), dstNode, h5Paths) ||
        h5Paths.isEmpty()) {
        return false;
    }

    m_preparedDstNode = dstNode;
    m_preparedProjectName = projectName();
    m_preparedH5Paths = h5Paths;
    m_preparedOutputNames.clear();
    m_outputJpgPaths.clear();
    for (const QString& h5Path : h5Paths) {
        const QFileInfo info(h5Path);
        m_preparedOutputNames.append(info.completeBaseName());
        m_outputJpgPaths.append(info.absolutePath() + "/" + info.baseName() + ".jpg");
    }
    m_preparedJpgPaths = m_outputJpgPaths;
    m_savedOutputFiles = m_preparedOutputNames;
    m_outputImagePaths = h5Paths;

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    publishResultsToProjectTree(m_preparedOutputNames, h5Paths);

    bool previewsCurrent = true;
    for (int i = 0; i < h5Paths.size() && i < m_outputJpgPaths.size(); ++i) {
        if (!NodeUtils::isJpgPreviewCurrent(h5Paths[i], m_outputJpgPaths[i])) {
            previewsCurrent = false;
            break;
        }
    }
    if (previewsCurrent) {
        m_previewData = std::make_shared<ImageInfoData>(m_outputJpgPaths);
        setOutputData(1, m_previewData);
        Q_EMIT dataUpdated(1);
    } else {
        m_previewData.reset();
        setOutputData(1, nullptr);
        startPreviewGeneration(h5Paths, m_outputJpgPaths, false);
    }
    return true;
}

void CoregistrationNode::startPreviewGeneration(const QStringList& h5Paths,
                                                const QStringList& finalJpgPaths,
                                                bool completeExecution)
{
    if (h5Paths.isEmpty() || h5Paths.size() != finalJpgPaths.size()) {
        if (completeExecution) {
            onError(QStringLiteral("Coregistration preview paths are incomplete."));
        }
        return;
    }

    if (m_remedyWatcher.isRunning()) {
        m_previewGenerationPending = false;
        ++m_previewGenerationId;
        const quint64 restartGenerationId = m_previewGenerationId;
        const QStringList previousTemporaryJpgPaths = m_previewTemporaryJpgPaths;
        m_remedyWatcher.cancel();
        m_remedyWatcher.disconnect(this);
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Paths, finalJpgPaths, completeExecution,
                 previousTemporaryJpgPaths, restartGenerationId]() {
            for (const QString& temporaryJpgPath : previousTemporaryJpgPaths) {
                QFile::remove(temporaryJpgPath);
            }
            if (m_previewGenerationId == restartGenerationId) {
                m_previewTemporaryJpgPaths.clear();
            }
            startPreviewGeneration(h5Paths, finalJpgPaths, completeExecution);
        });
        return;
    }

    m_remedyWatcher.disconnect(this);
    m_previewGenerationPending = true;
    const quint64 previewGenerationId = ++m_previewGenerationId;
    QStringList temporaryJpgPaths;
    for (const QString& finalJpgPath : finalJpgPaths) {
        const QFileInfo info(finalJpgPath);
        temporaryJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
                                 QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
        QFile::remove(finalJpgPath);
    }
    m_previewTemporaryJpgPaths = temporaryJpgPaths;

    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, h5Paths, finalJpgPaths, temporaryJpgPaths, completeExecution, previewGenerationId]() {
        if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
            for (const QString& temporaryJpgPath : temporaryJpgPaths) {
                QFile::remove(temporaryJpgPath);
            }
            return;
        }

        m_previewGenerationPending = false;
        if (discardObsoleteAutomaticExecution()) {
            ++m_previewGenerationId;
            for (const QString& temporaryJpgPath : temporaryJpgPaths) {
                QFile::remove(temporaryJpgPath);
            }
            m_previewTemporaryJpgPaths.clear();
            NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                                QStringLiteral("obsolete preview generation"), projectXml());
            m_isExecuting = false;
            m_outputData.reset();
            m_previewData.reset();
            setOutputData(0, nullptr);
            setOutputData(1, nullptr);
            return;
        }

        QStringList currentJpgPaths;
        for (int i = 0; i < temporaryJpgPaths.size() && i < finalJpgPaths.size(); ++i) {
            if (QFile::exists(temporaryJpgPaths[i]) &&
                QFile::rename(temporaryJpgPaths[i], finalJpgPaths[i])) {
                currentJpgPaths.append(finalJpgPaths[i]);
            }
        }
        m_previewTemporaryJpgPaths.clear();
        m_previewData = currentJpgPaths.isEmpty()
            ? nullptr : std::make_shared<ImageInfoData>(currentJpgPaths);
        setOutputData(1, m_previewData);

        if (!completeExecution && executionState() != ExecutionState::Running) {
            Q_EMIT dataUpdated(1);
            return;
        }

        InSARLogManager::LogInfo("CoregistrationNode", "Coregistration JPG preview generation finished.");
        m_isExecuting = false;
        updateParameterWidgetsEnableState();
        setState(ExecutionState::Running);
        setProgress(100);
        finishExecution();
    });
    m_remedyWatcher.setFuture(QtConcurrent::run([h5Paths, temporaryJpgPaths]() {
        for (int i = 0; i < h5Paths.size() && i < temporaryJpgPaths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], temporaryJpgPaths[i], "complex");
        }
    }));
}

QStandardItemModel* CoregistrationNode::projectModel() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString CoregistrationNode::projectPath() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectPath() : QString();
}

QString CoregistrationNode::projectName() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* CoregistrationNode::projectXml() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}


struct CoregisEvalThreadResult {
    int retCode;
    CropEvalResult evalResult;
};

struct CropEvaluationThresholds {
    double warningOffsetPixels;
    double severeOffsetPixels;
    double minimumSnr;
};

static CropEvaluationThresholds loadCropEvaluationThresholds()
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    auto readPositive = [&settings](const char* key, double fallback) {
        bool ok = false;
        const double value = settings.value(key, fallback).toDouble(&ok);
        return ok && value > 0.0 ? value : fallback;
    };

    CropEvaluationThresholds thresholds;
    thresholds.warningOffsetPixels = readPositive("RegistrationEvaluation/ResidualOffsetWarningPixels", 0.5);
    thresholds.severeOffsetPixels = std::max(
        thresholds.warningOffsetPixels,
        readPositive("RegistrationEvaluation/ResidualOffsetSeverePixels", 1.0));
    thresholds.minimumSnr = readPositive("RegistrationEvaluation/MinimumResidualOffsetSnr", 3.0);
    return thresholds;
}

struct CoregisEvalTempFiles {
    QString coherenceJpg;
    QString phaseJpg;

    ~CoregisEvalTempFiles()
    {
        QFile::remove(coherenceJpg);
        QFile::remove(phaseJpg);
    }
};

class CoregistrationEvalWidget : public QWidget
{
public:
    explicit CoregistrationEvalWidget(CoregistrationNode* node, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_node(node)
        , m_evalBtn(nullptr)
        , m_hasResults(false)
    {
        m_outputPaths = m_node->getOutputPaths();

        // 界面布局
        auto* mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);
        mainLayout->setSpacing(12);

        // 左侧栏：评估参数与定量指标显示
        auto* leftContainer = new QWidget();
        auto* leftLayout = new QVBoxLayout(leftContainer);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(10);

        auto* selectionLayout = new QHBoxLayout();
        auto* selLabel = new QLabel(tr("分析影像对:"));
        selLabel->setStyleSheet("font-weight: bold;");
        selectionLayout->addWidget(selLabel);

        m_slaveCombo = new QComboBox();
        int effMasterIdx1 = m_node->defaultFirstMaster() ? 1 : m_node->masterIndex();
        int masterIdx0 = (effMasterIdx1 >= 1 && effMasterIdx1 <= m_outputPaths.size()) ? (effMasterIdx1 - 1) : 0;
        if (m_outputPaths.size() > 1) {
            QString masterName = QFileInfo(m_outputPaths[masterIdx0]).completeBaseName();
            for (int i = 0; i < m_outputPaths.size(); ++i) {
                if (i == masterIdx0) continue;
                QString slaveName = QFileInfo(m_outputPaths[i]).completeBaseName();
                m_slaveCombo->addItem(QString("%1 -> %2").arg(slaveName).arg(masterName), i);
            }
        } else {
            m_slaveCombo->addItem(tr("无可配准的副影像"));
            m_slaveCombo->setEnabled(false);
        }
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

        m_statusCardDesc = new QLabel(tr("请点击评估获取相干性及对齐精度诊断结果。"));
        m_statusCardDesc->setWordWrap(true);
        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
        cardLayout->addWidget(m_statusCardDesc);

        leftLayout->addWidget(m_statusCard);

        // 定量指标统计表
        auto* metricsFrame = new QFrame();
        metricsFrame->setFrameShape(QFrame::StyledPanel);
        bool isDark = NodeDetailWindow::isDarkTheme(this);
        metricsFrame->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
            .arg(isDark ? "#374151" : "#FFFFFF")
            .arg(isDark ? "#4B5563" : "#E5E7EB"));

        auto* formLayout = new QFormLayout(metricsFrame);
        formLayout->setContentsMargins(12, 12, 12, 12);
        formLayout->setSpacing(10);
        formLayout->setLabelAlignment(Qt::AlignLeft);

        auto createValueLabel = [isDark]() {
            auto* label = new QLabel("-");
            label->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
            return label;
        };

        m_meanCohLabel = createValueLabel();
        m_medianCohLabel = createValueLabel();
        m_maxCohLabel = createValueLabel();
        m_highCohPctLabel = createValueLabel();
        m_snrLabel = createValueLabel();
        m_offsetYLabel = createValueLabel();
        m_offsetXLabel = createValueLabel();

        auto addFormRow = [formLayout, isDark](const QString& title, QWidget* valueWidget) {
            auto* label = new QLabel(title);
            label->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
            formLayout->addRow(label, valueWidget);
        };

        addFormRow(tr("相干系数平均值:"), m_meanCohLabel);
        addFormRow(tr("相干系数中位数:"), m_medianCohLabel);
        addFormRow(tr("相干系数最大值:"), m_maxCohLabel);
        addFormRow(tr("高相干像素比例 (>0.5):"), m_highCohPctLabel);
        addFormRow(tr("残余偏移估计 SNR:"), m_snrLabel);
        addFormRow(tr("垂直残余偏移 (Y):"), m_offsetYLabel);
        addFormRow(tr("水平残余偏移 (X):"), m_offsetXLabel);

        leftLayout->addWidget(metricsFrame);

        m_statusLabel = new QLabel(tr("准备就绪。请选择影像对开始评估。"));
        m_statusLabel->setWordWrap(true);
        m_statusLabel->setStyleSheet(isDark ? "color: #9CA3AF; font-size: 11px;" : "color: #6B7280; font-size: 11px;");
        leftLayout->addWidget(m_statusLabel);

        leftLayout->addStretch(1);
        mainLayout->addWidget(leftContainer, 4);

        // 右侧栏：大图显示与双模选择
        auto* rightContainer = new QWidget();
        auto* rightLayout = new QVBoxLayout(rightContainer);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(8);

        auto* modeLayout = new QHBoxLayout();

        m_evalBtn = new QPushButton(tr(" 执行质量评估 "));
        m_evalBtn->setStyleSheet(
            "QPushButton { background-color: #3B82F6; color: white; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: #2563EB; }"
            "QPushButton:pressed { background-color: #1D4ED8; }"
            "QPushButton:disabled { background-color: #9CA3AF; }"
        );
        modeLayout->addWidget(m_evalBtn);

        modeLayout->addStretch(1);

        auto* modeLabel = new QLabel(tr("显示模式:"));
        modeLabel->setStyleSheet("font-weight: bold;");
        modeLayout->addWidget(modeLabel);

        m_visualModeCombo = new QComboBox();
        m_visualModeCombo->addItem(tr("全图干涉相位图"), 0);
        m_visualModeCombo->addItem(tr("全图相干性系数图"), 1);
        modeLayout->addWidget(m_visualModeCombo);
        rightLayout->addLayout(modeLayout);

        m_imageView = new ImageView();
        m_imageView->setMinimumSize(256, 256);
        m_imageView->setStyleSheet(QString("border: 1px solid %1; border-radius: 4px;")
            .arg(isDark ? "#4B5563" : "#D1D5DB"));
        rightLayout->addWidget(m_imageView, 1);

        mainLayout->addWidget(rightContainer, 5);

        // 绑定信号槽
        connect(m_slaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CoregistrationEvalWidget::onSlaveChanged);
        connect(m_visualModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CoregistrationEvalWidget::onVisualModeChanged);
        connect(m_evalBtn, &QPushButton::clicked, this, &CoregistrationEvalWidget::startEvaluation);
        connect(&m_watcher, &QFutureWatcher<CoregisEvalThreadResult>::finished, this, &CoregistrationEvalWidget::onEvaluationFinished);

        if (m_slaveCombo->count() > 0 && m_outputPaths.size() > 1) {
            onSlaveChanged(m_slaveCombo->currentIndex());
        } else {
            m_evalBtn->setEnabled(false);
        }
    }

    ~CoregistrationEvalWidget() override
    {
        m_watcher.cancel();
    }

private:
    void onSlaveChanged(int index)
    {
        Q_UNUSED(index);
        m_hasResults = false;
        m_evalResult = CropEvalResult{};
        m_imageView->setImage(QImage());

        // 重置指标标签
        m_meanCohLabel->setText("-");
        m_medianCohLabel->setText("-");
        m_maxCohLabel->setText("-");
        m_highCohPctLabel->setText("-");
        m_snrLabel->setText("-");
        m_offsetYLabel->setText("-");
        m_offsetXLabel->setText("-");

        const bool isDark = NodeDetailWindow::isDarkTheme(this);
        const QString defaultValueStyle = QString("font-size: 12px; font-weight: bold; color: %1;")
            .arg(isDark ? "#F3F4F6" : "#1F2937");
        m_offsetYLabel->setStyleSheet(defaultValueStyle);
        m_offsetXLabel->setStyleSheet(defaultValueStyle);

        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
        m_statusCardTitle->setText(tr("未评估"));
        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
        m_statusCardDesc->setText(tr("请点击评估获取相干性及对齐精度诊断结果。"));
        m_statusLabel->setText(tr("准备就绪。点击“执行质量评估”开始分析。"));

        if (m_slaveCombo->count() > 0 && m_outputPaths.size() > 1) {
            m_evalBtn->setEnabled(true);
        } else {
            m_evalBtn->setEnabled(false);
        }
    }

    void onVisualModeChanged(int index)
    {
        Q_UNUSED(index);
        updateImageView();
    }

    void startEvaluation()
    {
        if (m_watcher.isRunning()) {
            return;
        }

        int effMasterIdx1 = m_node->defaultFirstMaster() ? 1 : m_node->masterIndex();
        int masterIdx0 = (effMasterIdx1 >= 1 && effMasterIdx1 <= m_outputPaths.size()) ? (effMasterIdx1 - 1) : 0;
        if (m_outputPaths.size() <= 1 || m_slaveCombo->currentIndex() < 0) {
            return;
        }
        int slaveIdx0 = m_slaveCombo->currentData().toInt();
        if (slaveIdx0 < 0 || slaveIdx0 >= m_outputPaths.size() || slaveIdx0 == masterIdx0) {
            return;
        }

        m_statusLabel->setText(tr("正在计算配准影像相干性与干涉相位，请稍候..."));
        m_imageView->setImage(QImage());
        m_hasResults = false;

        // 重置指标标签
        m_meanCohLabel->setText("-");
        m_medianCohLabel->setText("-");
        m_maxCohLabel->setText("-");
        m_highCohPctLabel->setText("-");
        m_snrLabel->setText("-");
        m_offsetYLabel->setText("-");
        m_offsetXLabel->setText("-");
        const bool isDark = NodeDetailWindow::isDarkTheme(this);
        const QString defaultValueStyle = QString("font-size: 12px; font-weight: bold; color: %1;")
            .arg(isDark ? "#F3F4F6" : "#1F2937");
        m_offsetYLabel->setStyleSheet(defaultValueStyle);
        m_offsetXLabel->setStyleSheet(defaultValueStyle);
        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
        m_statusCardTitle->setText(tr("未评估"));
        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
        m_statusCardDesc->setText(tr("请等待评估获取相干性及对齐精度诊断结果。"));

        QString masterPath = m_outputPaths[masterIdx0];
        QString slavePath = m_outputPaths[slaveIdx0];

        if (!QFile::exists(masterPath) || !QFile::exists(slavePath)) {
            m_statusLabel->setText(tr("错误：主图像或副图像配准文件不存在，请确保节点已成功运行！"));
            m_statusCardTitle->setText(tr("无法评估 (FAILED)"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusCardDesc->setText(tr("主图像或副图像配准文件不存在，无法启动评估。"));
            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #EF4444; border-radius: 4px;")
                .arg(isDark ? "#7F1D1D" : "#FEE2E2"));
            return;
        }

        m_slaveCombo->setEnabled(false);
        m_evalBtn->setEnabled(false);

        const QString tempDir = QDir::tempPath();
        const QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto tempFiles = std::make_shared<CoregisEvalTempFiles>();
        tempFiles->coherenceJpg = tempDir + QString("/crop_coherence_%1.jpg").arg(runId);
        tempFiles->phaseJpg = tempDir + QString("/crop_phase_%1.jpg").arg(runId);
        m_activeTempFiles = tempFiles;

        QFuture<CoregisEvalThreadResult> future = QtConcurrent::run([masterPath, slavePath, tempFiles]() {
            NodeUtils::Hdf5Locker locker(masterPath);
            CoregisEvalThreadResult res{};
            res.evalResult.structSize = sizeof(CropEvalResult);
            res.retCode = AnalyzeCropRegistration(
                masterPath.toLocal8Bit().constData(),
                slavePath.toLocal8Bit().constData(),
                tempFiles->coherenceJpg.toLocal8Bit().constData(),
                tempFiles->phaseJpg.toLocal8Bit().constData(),
                -1.0, -1.0, -1.0, -1.0,
                &res.evalResult
            );
            return res;
        });

        m_watcher.setFuture(future);
    }

    void onEvaluationFinished()
    {
        m_slaveCombo->setEnabled(true);
        m_evalBtn->setEnabled(true);

        CoregisEvalThreadResult threadRes = m_watcher.result();
        if (threadRes.retCode != 0) {
            m_hasResults = false;
            m_imageView->setImage(QImage());
            const bool isDark = NodeDetailWindow::isDarkTheme(this);
            m_statusLabel->setText(tr("配准质量评估失败，错误码：%1").arg(threadRes.retCode));
            m_statusCardTitle->setText(tr("无法评估 (FAILED)"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusCardDesc->setText(tr("底层评估调用失败，错误码：%1。请检查输入数据和处理日志。")
                .arg(threadRes.retCode));
            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #EF4444; border-radius: 4px;")
                .arg(isDark ? "#7F1D1D" : "#FEE2E2"));
            return;
        }

        m_evalResult = threadRes.evalResult;
        m_hasResults = true;
        m_statusLabel->setText(tr("配准评估完成。"));

        bool isDark = NodeDetailWindow::isDarkTheme(this);
        const CropEvaluationThresholds thresholds = loadCropEvaluationThresholds();
        const bool validMeanCoherence = std::isfinite(m_evalResult.meanCoherence) &&
            m_evalResult.meanCoherence >= 0.0 && m_evalResult.meanCoherence <= 1.0;
        const bool validMedianCoherence = std::isfinite(m_evalResult.medianCoherence) &&
            m_evalResult.medianCoherence >= 0.0 && m_evalResult.medianCoherence <= 1.0;
        const bool validMaxCoherence = std::isfinite(m_evalResult.maxCoherence) &&
            m_evalResult.maxCoherence >= 0.0 && m_evalResult.maxCoherence <= 1.0;
        const bool validHighCoherencePct = std::isfinite(m_evalResult.highCoherencePct) &&
            m_evalResult.highCoherencePct >= 0.0 && m_evalResult.highCoherencePct <= 1.0;
        const bool validCoherenceMetrics = validMeanCoherence && validMedianCoherence &&
            validMaxCoherence && validHighCoherencePct;
        const bool validAssessmentStatus = m_evalResult.assessmentStatus >= 0 && m_evalResult.assessmentStatus <= 2;
        const bool validMetrics = validCoherenceMetrics && validAssessmentStatus;
        const bool validOffsets = std::isfinite(m_evalResult.offsetY) && std::isfinite(m_evalResult.offsetX) &&
            std::abs(m_evalResult.offsetY) < 1000.0 && std::abs(m_evalResult.offsetX) < 1000.0;
        const bool validSnr = std::isfinite(m_evalResult.snr) && m_evalResult.snr >= 0.0;
        const bool offsetWarning = validOffsets && (std::abs(m_evalResult.offsetY) > thresholds.warningOffsetPixels ||
            std::abs(m_evalResult.offsetX) > thresholds.warningOffsetPixels);
        const bool offsetSevere = validOffsets && (std::abs(m_evalResult.offsetY) > thresholds.severeOffsetPixels ||
            std::abs(m_evalResult.offsetX) > thresholds.severeOffsetPixels);
        const bool lowSnr = validSnr && m_evalResult.snr < thresholds.minimumSnr;

        m_meanCohLabel->setText(validMeanCoherence ? QString::number(m_evalResult.meanCoherence, 'f', 4) : "-");
        m_medianCohLabel->setText(validMedianCoherence ? QString::number(m_evalResult.medianCoherence, 'f', 4) : "-");
        m_maxCohLabel->setText(validMaxCoherence ? QString::number(m_evalResult.maxCoherence, 'f', 4) : "-");
        m_highCohPctLabel->setText(validHighCoherencePct
            ? QString("%1%").arg(QString::number(m_evalResult.highCoherencePct * 100.0, 'f', 2)) : "-");
        m_snrLabel->setText(validSnr ? QString::number(m_evalResult.snr, 'f', 2) : "-");
        m_offsetYLabel->setText(validOffsets ? QString::number(m_evalResult.offsetY, 'f', 2) : "-");
        m_offsetXLabel->setText(validOffsets ? QString::number(m_evalResult.offsetX, 'f', 2) : "-");

        if (offsetWarning) {
            m_offsetYLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #EF4444;");
            m_offsetXLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #EF4444;");
        } else {
            QString defaultColor = QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937");
            m_offsetYLabel->setStyleSheet(defaultColor);
            m_offsetXLabel->setStyleSheet(defaultColor);
        }

        QStringList reasons;
        if (!validCoherenceMetrics) {
            reasons << tr("相干性统计指标无效");
        }
        if (!validAssessmentStatus) {
            reasons << tr("相干性评估状态无效");
        } else if (m_evalResult.assessmentStatus == 1) {
            reasons << tr("相干性一般");
        } else if (m_evalResult.assessmentStatus == 2) {
            reasons << tr("相干性不足");
        }
        if (!validOffsets) {
            reasons << tr("残余偏移估计不可用");
        } else if (std::abs(m_evalResult.offsetY) > thresholds.warningOffsetPixels) {
            reasons << tr("方位残余偏移 %1 px").arg(m_evalResult.offsetY, 0, 'f', 2);
        }
        if (validOffsets && std::abs(m_evalResult.offsetX) > thresholds.warningOffsetPixels) {
            reasons << tr("距离残余偏移 %1 px").arg(m_evalResult.offsetX, 0, 'f', 2);
        }
        if (!validSnr) {
            reasons << tr("残余偏移估计 SNR 无效");
        } else if (lowSnr) {
            reasons << tr("残余偏移估计 SNR=%1，低于 %2").arg(m_evalResult.snr, 0, 'f', 2).arg(thresholds.minimumSnr, 0, 'f', 2);
        }

        auto setStatusCard = [this, isDark](const QString& title, const QString& titleColor,
            const QString& borderColor, const QString& darkBackground, const QString& lightBackground,
            const QString& description) {
            m_statusCardTitle->setText(title);
            m_statusCardTitle->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(titleColor));
            m_statusCardDesc->setText(description);
            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
                .arg(isDark ? darkBackground : lightBackground).arg(borderColor));
        };

        const QString reasonText = reasons.isEmpty() ? tr("相干性、几何残余和估计置信度均满足当前阈值。") : reasons.join(tr("；"));
        if (!validMetrics || !validOffsets || !validSnr) {
            setStatusCard(tr("无法评估 (FAILED)"), "#EF4444", "#EF4444", "#7F1D1D", "#FEE2E2",
                reasonText + tr("。无法给出可靠的配准结论。"));
        } else if (lowSnr) {
            setStatusCard(tr("结果不确定 (INCONCLUSIVE)"), "#F59E0B", "#F59E0B", "#78350F", "#FEF3C7",
                reasonText + tr("。请复核影像纹理或扩大有效评估区域。"));
        } else if (offsetSevere) {
            setStatusCard(tr("几何配准异常 (FAILED)"), "#EF4444", "#EF4444", "#7F1D1D", "#FEE2E2",
                reasonText + tr("。残余偏移超过严重阈值。"));
        } else if (m_evalResult.assessmentStatus == 2) {
            setStatusCard(tr("低相干 (LOW COHERENCE)"), "#EF4444", "#EF4444", "#7F1D1D", "#FEE2E2",
                reasonText + tr("。请注意后续干涉和解缠质量。"));
        } else if (m_evalResult.assessmentStatus == 1 || offsetWarning) {
            setStatusCard(tr("提醒 (WARNING)"), "#F59E0B", "#F59E0B", "#78350F", "#FEF3C7",
                reasonText + tr("。建议复核质量。"));
        } else {
            m_statusCardTitle->setText(tr("通过 (PASS)"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
            m_statusCardDesc->setText(reasonText);
            m_statusCard->setStyleSheet(QString("background-color: %1; border: 1px solid #10B981; border-radius: 4px;")
                .arg(isDark ? "#064E3B" : "#D1FAE5"));
        }

        updateImageView();
    }

    void updateImageView()
    {
        if (!m_hasResults) {
            m_imageView->setImage(QImage());
            return;
        }

        int mode = m_visualModeCombo->currentData().toInt();
        if (!m_activeTempFiles) {
            m_imageView->setImage(QImage());
            return;
        }
        QString imgPath = (mode == 0) ? m_activeTempFiles->phaseJpg : m_activeTempFiles->coherenceJpg;

        if (QFile::exists(imgPath)) {
            m_imageView->loadImage(imgPath);
        } else {
            m_imageView->setImage(QImage());
        }
    }

    CoregistrationNode* m_node;
    QStringList m_outputPaths;
    QComboBox* m_slaveCombo;
    QComboBox* m_visualModeCombo;
    QPushButton* m_evalBtn;
    ImageView* m_imageView;

    QFrame* m_statusCard;
    QLabel* m_statusCardTitle;
    QLabel* m_statusCardDesc;

    QLabel* m_meanCohLabel;
    QLabel* m_medianCohLabel;
    QLabel* m_maxCohLabel;
    QLabel* m_highCohPctLabel;
    QLabel* m_snrLabel;
    QLabel* m_offsetYLabel;
    QLabel* m_offsetXLabel;
    QLabel* m_statusLabel;

    // The worker owns a copy while it writes, so closing Detail View cannot
    // race temporary-file cleanup with AnalyzeCropRegistration.
    std::shared_ptr<CoregisEvalTempFiles> m_activeTempFiles;

    CropEvalResult m_evalResult;
    bool m_hasResults;
    QFutureWatcher<CoregisEvalThreadResult> m_watcher;
};

// 选项卡页面工厂实现


::QWidget* CoregistrationNode::createInterferometryWidget(::QWidget* parent)
{
    return new CoregistrationEvalWidget(this, parent);
}

} // namespace QtNodes
