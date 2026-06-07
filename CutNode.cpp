#include "CutNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "Preview_Window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QImage>
#include <QSet>
#include <QtGlobal>
#include "InSARLogManager.h"
#include <Utils.h>
#include <FormatConversion.h>

namespace QtNodes {

CutNode::CutNode()
    : ExecutableNodeDelegateModel()
    , m_mode(0) // Default to Auto Center
    , m_boxSelected(false)
    , m_coordsSet(false)
{
    setExecutionMode(ExecutionMode::Automatic);
}

CutNode::~CutNode()
{
    if (m_thread) {
        if (m_thread->isRunning()) {
            m_thread->requestInterruption();
            m_thread->quit();
            m_thread->wait(5000);
        }
        delete m_thread;  // 安全：线程已停止，且无parent
        m_thread = nullptr;
    }
    // m_worker由finished→deleteLater自动删除，此处无需处理
    m_worker = nullptr;
    m_isExecuting = false;
}

unsigned int CutNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType CutNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        return NodeDataType{"imported_file", "Imported File"};
    }
    else
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool CutNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString CutNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入数据");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else
            return QStringLiteral("预览 ?");
    }
}

bool CutNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void CutNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
        Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData && !m_inputData->filePaths().isEmpty()) {
                if (!isRestoring()) {
                        m_boxSelected = false;
            // 从第一个H5文件的gcps自动提取中心经纬度作为默认值
            QStringList h5Paths = resolvedInputH5Paths();
            if (!h5Paths.isEmpty()) {
                FormatConversion FC;
                cv::Mat gcps;
                if (FC.read_array_from_h5(h5Paths.first().toLocal8Bit().constData(), "gcps", gcps) == 0
                    && gcps.rows > 0 && gcps.cols >= 2) {
                    cv::Mat lon = gcps.col(0);
                    cv::Mat lat = gcps.col(1);
                    m_lon = cv::mean(lon)[0];
                    m_lat = cv::mean(lat)[0];
                    m_coordsSet = true;
                                        if (m_lonEdit) m_lonEdit->setText(QString::number(m_lon, 'f', 6));
                    if (m_latEdit) m_latEdit->setText(QString::number(m_lat, 'f', 6));
                } else {
                                    }
            }
        } else {
                    }
    }

    updateLabels();
    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
                m_outputData.reset();
        m_previewData.reset();
    }
}

std::shared_ptr<NodeData> CutNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

QWidget* CutNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void CutNode::createWidget()
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

    // Mode Selector
    auto* modeLayout = new QHBoxLayout();
    QLabel* modeLabel = new QLabel(QStringLiteral("裁剪模式："));
    modeLabel->setFixedWidth(80);
    m_modeCombo = new QComboBox();
    m_modeCombo->addItem(QStringLiteral("自动中心裁剪"), 0);
    m_modeCombo->addItem(QStringLiteral("经纬度裁剪"), 1);
    m_modeCombo->addItem(QStringLiteral("框选裁剪"), 2);
    m_modeCombo->setCurrentIndex(m_mode);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CutNode::onModeChanged);
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeCombo);
    layout->addLayout(modeLayout);

    // =========================================================================
    // Mode 0: Auto Center Widgets
    // =========================================================================
    m_autoCenterWidget = new QWidget();
    auto* autoCenterForm = new QFormLayout(m_autoCenterWidget);
    autoCenterForm->setContentsMargins(0, 0, 0, 0);
    autoCenterForm->setSpacing(4);

    m_leftSpin = new QDoubleSpinBox();
    m_leftSpin->setRange(0.0, 1.0);
    m_leftSpin->setSingleStep(0.05);
    m_leftSpin->setValue(m_left);
    connect(m_leftSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, invalidateNodeData](double val) {
        if (m_left != val) {
            m_left = val;
            invalidateNodeData();
        }
    });

    m_rightSpin = new QDoubleSpinBox();
    m_rightSpin->setRange(0.0, 1.0);
    m_rightSpin->setSingleStep(0.05);
    m_rightSpin->setValue(m_right);
    connect(m_rightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, invalidateNodeData](double val) {
        if (m_right != val) {
            m_right = val;
            invalidateNodeData();
        }
    });

    m_topSpin = new QDoubleSpinBox();
    m_topSpin->setRange(0.0, 1.0);
    m_topSpin->setSingleStep(0.05);
    m_topSpin->setValue(m_top);
    connect(m_topSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, invalidateNodeData](double val) {
        if (m_top != val) {
            m_top = val;
            invalidateNodeData();
        }
    });

    m_bottomSpin = new QDoubleSpinBox();
    m_bottomSpin->setRange(0.0, 1.0);
    m_bottomSpin->setSingleStep(0.05);
    m_bottomSpin->setValue(m_bottom);
    connect(m_bottomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, invalidateNodeData](double val) {
        if (m_bottom != val) {
            m_bottom = val;
            invalidateNodeData();
        }
    });

    autoCenterForm->addRow(QStringLiteral("左边界(0~1)："), m_leftSpin);
    autoCenterForm->addRow(QStringLiteral("右边界(0~1)："), m_rightSpin);
    autoCenterForm->addRow(QStringLiteral("上边界(0~1)："), m_topSpin);
    autoCenterForm->addRow(QStringLiteral("下边界(0~1)："), m_bottomSpin);
    layout->addWidget(m_autoCenterWidget);

    // =========================================================================
    // Mode 1: Coordinate Widgets
    // =========================================================================
    m_coordinateWidget = new QWidget();
    auto* coordForm = new QFormLayout(m_coordinateWidget);
    coordForm->setContentsMargins(0, 0, 0, 0);
    coordForm->setSpacing(4);

    m_lonEdit = new QLineEdit();
    m_lonEdit->setText(QString::number(m_lon, 'f', 6));
    connect(m_lonEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        double val = m_lonEdit->text().toDouble();
        if (m_lon != val) {
            m_lon = val;
            m_coordsSet = (m_lon != 0.0 || m_lat != 0.0);
            invalidateNodeData();
        }
    });

    m_latEdit = new QLineEdit();
    m_latEdit->setText(QString::number(m_lat, 'f', 6));
    connect(m_latEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        double val = m_latEdit->text().toDouble();
        if (m_lat != val) {
            m_lat = val;
            m_coordsSet = (m_lon != 0.0 || m_lat != 0.0);
            invalidateNodeData();
        }
    });

    m_widthEdit = new QLineEdit();
    m_widthEdit->setText(QString::number(m_width, 'f', 1));
    connect(m_widthEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        double val = m_widthEdit->text().toDouble();
        if (m_width != val) {
            m_width = val;
            invalidateNodeData();
        }
    });

    m_heightEdit = new QLineEdit();
    m_heightEdit->setText(QString::number(m_height, 'f', 1));
    connect(m_heightEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        double val = m_heightEdit->text().toDouble();
        if (m_height != val) {
            m_height = val;
            invalidateNodeData();
        }
    });

    coordForm->addRow(QStringLiteral("中心经度："), m_lonEdit);
    coordForm->addRow(QStringLiteral("中心纬度："), m_latEdit);
    coordForm->addRow(QStringLiteral("裁剪宽(m)："), m_widthEdit);
    coordForm->addRow(QStringLiteral("裁剪高(m)："), m_heightEdit);
    layout->addWidget(m_coordinateWidget);

    // =========================================================================
    // Mode 2: Box Selection Widgets (Canvas Face is a label tip only)
    // =========================================================================
    m_boxSelectionWidget = new QWidget();
    auto* boxLayout = new QVBoxLayout(m_boxSelectionWidget);
    boxLayout->setContentsMargins(0, 4, 0, 4);
    boxLayout->setSpacing(4);

    QLabel* tipLabel = new QLabel(QStringLiteral("请在“详细视图”中选择裁剪范围"));
    tipLabel->setStyleSheet("color: #3B82F6; font-size: 11px; font-weight: bold; line-height: 14px;");
    tipLabel->setAlignment(Qt::AlignCenter);
    tipLabel->setWordWrap(true);
    boxLayout->addWidget(tipLabel);

    m_boundsLabel = new QLabel();
    m_boundsLabel->setAlignment(Qt::AlignCenter);
    m_boundsLabel->setStyleSheet("color: #888888; font-size: 10px;");
    boxLayout->addWidget(m_boundsLabel);
    layout->addWidget(m_boxSelectionWidget);

    // =========================================================================
    // Shared Output Settings
    // =========================================================================
    m_saveToProjectCheckBox = new QCheckBox(QStringLiteral("保存到项目树"));
    m_saveToProjectCheckBox->setChecked(m_saveToProject);
    connect(m_saveToProjectCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        m_saveToProject = (state == Qt::Checked);
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setEnabled(m_saveToProject);
        }
        invalidateNodeData();
    });
    layout->addWidget(m_saveToProjectCheckBox);

    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel(QStringLiteral("目标节点："));
    nodeNameLabel->setFixedWidth(80);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    nodeNameLayout->addWidget(nodeNameLabel);
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    onModeChanged(m_mode);
    updateLabels();
}

void CutNode::onModeChanged(int index)
{
    m_mode = index;
    if (_widget) {
        m_autoCenterWidget->setVisible(m_mode == 0);
        m_coordinateWidget->setVisible(m_mode == 1);
        m_boxSelectionWidget->setVisible(m_mode == 2);
        updateWidgetSize();
    }

    if (m_outputData) m_outputData.reset();
    if (m_previewData) m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    invalidateExecution();
}

void CutNode::onPreviewPressed()
{
    // Keeping function structure for backward slot connectivity compatibility, but UI button is removed.
}

void CutNode::onBoxSelected(double left, double right, double top, double bottom)
{
    m_left = left;
    m_right = right;
    m_top = top;
    m_bottom = bottom;
    m_boxSelected = true;

    updateLabels();
    
    if (m_outputData) m_outputData.reset();
    if (m_previewData) m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    invalidateExecution();
}


bool CutNode::isReady() const
{
    if (!m_inputData || resolvedInputH5Paths().isEmpty()) {
        return false;
    }

    if (m_saveToProject) {
        if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty()) {
            return false;
        }
        if (m_outputNodeName.trimmed().isEmpty()) {
            return false;
        }
    }

    if (m_mode == 1) { // Coordinate mode: block execution until coordinates are validly set
        if (m_lon == 0.0 && m_lat == 0.0) {
            return false;
        }
        if (m_width <= 0 || m_height <= 0) {
            return false;
        }
    } else if (m_mode == 2) { // Box selection mode: block execution until the user crops in Detail view
        if (!m_boxSelected) {
            return false;
        }
        // 如果已有有效输出（Completed状态），不自动重新执行
        if (executionState() == ExecutionState::Completed && !m_outputPaths.isEmpty()) {
            bool allExist = true;
            for (const QString& p : m_outputPaths) {
                if (!QFile::exists(p)) { allExist = false; break; }
            }
            if (allExist) return false;
        }
        if (m_left < 0 || m_right < 0 || m_top < 0 || m_bottom < 0 ||
            m_left >= m_right || m_top >= m_bottom ||
            m_left > 1 || m_right > 1 || m_top > 1 || m_bottom > 1) {
            return false;
        }
    }

    return true;
}

void CutNode::processAutomatically()
{
    if (m_isExecuting) {
        deferAutomaticCompletion();
        return;
    }

    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

void CutNode::execute()
{
    executeProcessing();
}

bool CutNode::prepareToStart()
{
    if (m_isExecuting) {
        return false;
    }
    if (!isReady()) {
        if (m_mode == 1 && m_lon == 0.0 && m_lat == 0.0) {
            InSARLogManager::LogWarning("CutNode", "prepareToStart skipped: lon/lat not set (still 0.0). Please enter coordinates and press Enter to confirm.");
        } else {
            InSARLogManager::LogWarning("CutNode", "prepareToStart skipped: node not ready.");
        }
        return false;
    }

    m_preparedInputPaths = resolvedInputH5Paths();
    m_preparedDstNodeName = m_outputNodeName.trimmed();
    m_preparedProjDir = projectPath();
    if (m_preparedProjDir.endsWith(".insar", Qt::CaseInsensitive)) {
        m_preparedProjDir = QFileInfo(m_preparedProjDir).absolutePath();
    }
    m_preparedProjName = projectName();
    m_preparedModel = projectModel();

    // Expected Output Paths
    m_preparedOutputPaths.clear();
    for (const QString& path : m_preparedInputPaths) {
        QString base = QFileInfo(path).baseName();
        // Mode 1 (Coord) uses _cut, Mode 0 & 2 (ratios) use _cut2
        QString outBase = base + (m_mode == 1 ? "_cut" : "_cut2");
        m_preparedOutputPaths.append(m_preparedProjDir + "/" + m_preparedDstNodeName + "/" + outBase + ".h5");
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedDstNodeName, m_preparedOutputPaths);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void CutNode::executeProcessing()
{
    InSARLogManager::LogInfo("CutNode", "executeProcessing started.");

    if (m_isExecuting) {
        InSARLogManager::LogInfo("CutNode", "executeProcessing skipped: already executing.");
        return;
    }

    setProgress(0);
    m_outputPaths = m_preparedOutputPaths;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (validateAndRestoreOutput()) {
            setState(ExecutionState::Completed);
            finishExecution();
            return;
        } else {
            QMessageBox::warning(nullptr, QStringLiteral("警告"), QStringLiteral("加载已有文件失败，将重新计算！"));
        }
    }

    // Clean up old files in destination directory
    QString dstDir = m_preparedProjDir + "/" + m_preparedDstNodeName;
    if (QDir(dstDir).exists()) {
        QDir dir(dstDir);
        for (const QFileInfo& fi : dir.entryInfoList({"*.h5", "*.jpg"}, QDir::Files)) {
            QFile::remove(fi.absoluteFilePath());
        }
    }

    // Clean up old project tree node if it exists
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_preparedDstNodeName);

    // Instantiate thread and worker
    m_worker = new CutWorker();
    m_thread = new QThread; // 不设parent，由析构函数显式管理
    m_worker->moveToThread(m_thread);

    connect(m_worker, &CutWorker::updateProcess, this, &CutNode::onProgressUpdate, Qt::QueuedConnection);
    connect(m_worker, &CutWorker::endProcess, this, &CutNode::onProcessingFinished, Qt::QueuedConnection);
    connect(m_worker, &CutWorker::errorProcess, this, &CutNode::onError, Qt::QueuedConnection);
    connect(m_worker, &CutWorker::sendModel, this, &CutNode::onModelUpdated, Qt::QueuedConnection);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
    m_isExecuting = true;

    QString srcNodeName = m_inputData->nodeName();
    QString dstNodeName = m_preparedDstNodeName;
    QString projDir = m_preparedProjDir;
    QString projName = m_preparedProjName;
    QStandardItemModel* model = m_preparedModel;

    if (m_mode == 1) { // Coordinate crop
        QList<double> para;
        para.append(m_lon);
        para.append(m_lat);
        para.append(m_width);
        para.append(m_height);

        QMetaObject::invokeMethod(m_worker, "Cut", Qt::QueuedConnection,
            Q_ARG(QList<double>, para),
            Q_ARG(QString, projDir),
            Q_ARG(QString, projName.endsWith(".insar", Qt::CaseInsensitive) ? projName : projName + ".insar"),
            Q_ARG(QString, srcNodeName),
            Q_ARG(QString, dstNodeName),
            Q_ARG(QStandardItemModel*, model));
    } else { // Auto center (0) and Box selection (2) crop via Cut2
        QMetaObject::invokeMethod(m_worker, "Cut2", Qt::QueuedConnection,
            Q_ARG(double, m_left),
            Q_ARG(double, m_right),
            Q_ARG(double, m_top),
            Q_ARG(double, m_bottom),
            Q_ARG(QString, projDir),
            Q_ARG(QString, projName.endsWith(".insar", Qt::CaseInsensitive) ? projName : projName + ".insar"),
            Q_ARG(QString, srcNodeName),
            Q_ARG(QString, dstNodeName),
            Q_ARG(QStandardItemModel*, model));
    }

    if (m_modeCombo) m_modeCombo->setEnabled(false);
    if (m_lonEdit) m_lonEdit->setEnabled(false);
    if (m_latEdit) m_latEdit->setEnabled(false);
    if (m_widthEdit) m_widthEdit->setEnabled(false);
    if (m_heightEdit) m_heightEdit->setEnabled(false);
    if (m_leftSpin) m_leftSpin->setEnabled(false);
    if (m_rightSpin) m_rightSpin->setEnabled(false);
    if (m_topSpin) m_topSpin->setEnabled(false);
    if (m_bottomSpin) m_bottomSpin->setEnabled(false);
    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setEnabled(false);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(false);

    setState(ExecutionState::Running);
    deferAutomaticCompletion();

    InSARLogManager::LogInfo("CutNode", "executeProcessing completed.");
}

void CutNode::stopExecution()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_isExecuting = false;
    setState(ExecutionState::Idle);
}

void CutNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void CutNode::onProcessingFinished()
{
    InSARLogManager::LogInfo("CutNode", "onProcessingFinished.");

    QString dstNodeName = m_outputNodeName.trimmed();
    QString projDir = projectPath();
    if (projDir.endsWith(".insar", Qt::CaseInsensitive)) {
        projDir = QFileInfo(projDir).absolutePath();
    }

        for (const QString& p : m_outputPaths)
        
    // Set outputs
    m_outputData = std::make_shared<ImportedFileData>(m_outputPaths, dstNodeName);
    setOutputData(0, m_outputData);

    // Rebuild JPG paths
    QStringList jpgPaths;
    for (const QString& path : m_outputPaths) {
        jpgPaths.append(QFileInfo(path).absolutePath() + "/" + QFileInfo(path).baseName() + ".jpg");
    }

    // Remedy JPG previews if missing using background thread pool
    QStringList missingH5s;
    QStringList missingJpgs;
    for (int i = 0; i < m_outputPaths.size(); ++i) {
        if (!QFile::exists(jpgPaths[i])) {
            missingH5s.append(m_outputPaths[i]);
            missingJpgs.append(jpgPaths[i]);
        }
    }

    
    if (!missingH5s.isEmpty()) {
        QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
                bool ok = NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
                            }
        });
        auto* watcher = new QFutureWatcher<void>(this);
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, jpgPaths]() {
            watcher->deleteLater();
            m_previewData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_previewData);
            Q_EMIT dataUpdated(1);
        });
        watcher->setFuture(future);
    } else {
        m_previewData = std::make_shared<ImageInfoData>(jpgPaths);
        setOutputData(1, m_previewData);
    }

    if (m_modeCombo) m_modeCombo->setEnabled(true);
    if (m_lonEdit) m_lonEdit->setEnabled(m_mode == 1);
    if (m_latEdit) m_latEdit->setEnabled(m_mode == 1);
    if (m_widthEdit) m_widthEdit->setEnabled(m_mode == 1);
    if (m_heightEdit) m_heightEdit->setEnabled(m_mode == 1);
    if (m_leftSpin) m_leftSpin->setEnabled(m_mode == 0);
    if (m_rightSpin) m_rightSpin->setEnabled(m_mode == 0);
    if (m_topSpin) m_topSpin->setEnabled(m_mode == 0);
    if (m_bottomSpin) m_bottomSpin->setEnabled(m_mode == 0);
    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setEnabled(true);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(m_saveToProject);

    m_isExecuting = false;
    m_thread = nullptr;
    m_worker = nullptr;

    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);

    setState(ExecutionState::Completed);
    finishExecution();
}

void CutNode::onError(const QString& error)
{
    InSARLogManager::LogError("CutNode", error);
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);

    if (m_modeCombo) m_modeCombo->setEnabled(true);
    if (m_lonEdit) m_lonEdit->setEnabled(m_mode == 1);
    if (m_latEdit) m_latEdit->setEnabled(m_mode == 1);
    if (m_widthEdit) m_widthEdit->setEnabled(m_mode == 1);
    if (m_heightEdit) m_heightEdit->setEnabled(m_mode == 1);
    if (m_leftSpin) m_leftSpin->setEnabled(m_mode == 0);
    if (m_rightSpin) m_rightSpin->setEnabled(m_mode == 0);
    if (m_topSpin) m_topSpin->setEnabled(m_mode == 0);
    if (m_bottomSpin) m_bottomSpin->setEnabled(m_mode == 0);
    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setEnabled(true);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(m_saveToProject);

    m_isExecuting = false;
    m_thread = nullptr;
    m_worker = nullptr;

    m_outputData.reset();
    m_previewData.reset();
}

void CutNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }
}

bool CutNode::validateAndRestoreOutput()
{
        QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty()) return false;

    QString projDir = projectPath();
    if (projDir.endsWith(".insar", Qt::CaseInsensitive)) {
        projDir = QFileInfo(projDir).absolutePath();
    }

    QStringList expectedH5Paths;
    QStringList expectedJpgPaths;

    if (!m_outputPaths.isEmpty()) {
        // m_outputPaths已有完整路径（执行后保存的），直接使用
                for (const QString& path : m_outputPaths) {
            QString outPath = path;
            if (!outPath.endsWith(".h5", Qt::CaseInsensitive)) outPath += ".h5";
            expectedH5Paths.append(outPath);
            expectedJpgPaths.append(QFileInfo(outPath).absolutePath() + "/" + QFileInfo(outPath).baseName() + ".jpg");
        }
    } else if (!m_savedOutputFileNames.isEmpty() && !projDir.isEmpty()) {
        // load时projDir不可用，现在可用，从保存的文件名重建路径
        for (const auto& fn : m_savedOutputFileNames) {
            expectedH5Paths.append(projDir + "/" + nodeName + "/" + fn);
            expectedJpgPaths.append(projDir + "/" + nodeName + "/" + QFileInfo(fn).baseName() + ".jpg");
        }
    } else if (m_inputData && !projDir.isEmpty()) {
        for (const QString& path : resolvedInputH5Paths()) {
            QString origName = QFileInfo(path).baseName();
            QString outBase = origName + (m_mode == 1 ? "_cut" : "_cut2");
            expectedH5Paths.append(projDir + "/" + nodeName + "/" + outBase + ".h5");
            expectedJpgPaths.append(projDir + "/" + nodeName + "/" + outBase + ".jpg");
        }
    } else {
                return false;
    }

    // Verify files exist
    for (const QString& path : expectedH5Paths) {
                if (!QFile::exists(path)) return false;
    }

    m_outputPaths = expectedH5Paths;

    // Remedy JPG previews if missing
    QStringList missingH5s;
    QStringList missingJpgs;
    for (int i = 0; i < expectedH5Paths.size(); ++i) {
        if (!QFile::exists(expectedJpgPaths[i])) {
            missingH5s.append(expectedH5Paths[i]);
            missingJpgs.append(expectedJpgPaths[i]);
        }
    }

    if (!missingH5s.isEmpty()) {
                QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
                bool ok = NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
                            }
        });
        auto* watcher = new QFutureWatcher<void>(this);
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, expectedJpgPaths]() {
                        watcher->deleteLater();
            m_previewData = std::make_shared<ImageInfoData>(expectedJpgPaths);
            setOutputData(1, m_previewData);
                        Q_EMIT dataUpdated(1);
        });
        watcher->setFuture(future);
    } else {
                m_previewData = std::make_shared<ImageInfoData>(expectedJpgPaths);
        setOutputData(1, m_previewData);
        Q_EMIT dataUpdated(1);
    }

        m_outputData = std::make_shared<ImportedFileData>(expectedH5Paths, nodeName);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    
    // Rebuild project tree node
    QStandardItemModel* projModelPtr = projectModel();
    if (projModelPtr) {
        QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
        if (!foundProjects.isEmpty()) {
            QStandardItem* projectItem = foundProjects.first();

            QStandardItem* cutNodeItem = nullptr;
            for (int i = 0; i < projectItem->rowCount(); i++) {
                if (projectItem->child(i, 0)->text() == nodeName) {
                    cutNodeItem = projectItem->child(i, 0);
                    break;
                }
            }

            if (!cutNodeItem) {
                cutNodeItem = new QStandardItem(nodeName);
                cutNodeItem->setToolTip(projectName());
                int insert = 0;
                for (; insert < projectItem->rowCount(); insert++) {
                    if (projectItem->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                        projectItem->child(insert, 1)->text().compare("complex-1.0") == 0)
                        continue;
                    else
                        break;
                }
                cutNodeItem->setIcon(QIcon(FOLDER_ICON));
                projectItem->insertRow(insert, cutNodeItem);

                QString rank = "complex-1.0";
                if (m_inputData && m_inputData->filePaths().size() > 0) {
                    for (int i = 0; i < projectItem->rowCount(); i++) {
                        if (projectItem->child(i, 0)->text() == m_inputData->nodeName()) {
                            rank = projectItem->child(i, 1)->text();
                            break;
                        }
                    }
                }
                QStandardItem* cutRank = new QStandardItem(rank);
                projectItem->setChild(insert, 1, cutRank);
            }

            for (const QString& h5Path : expectedH5Paths) {
                QFileInfo fileinfo(h5Path);
                QString cut_img_name = fileinfo.baseName();

                QStandardItem* item_img = nullptr;
                for (int j = 0; j < cutNodeItem->rowCount(); j++) {
                    if (cutNodeItem->child(j, 0)->text() == cut_img_name) {
                        item_img = cutNodeItem->child(j, 0);
                        break;
                    }
                }

                if (!item_img) {
                    QStandardItem* cut_images_name = new QStandardItem(cut_img_name);
                    cut_images_name->setToolTip("complex");
                    QStandardItem* cut_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                    cut_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                    cutNodeItem->appendRow(cut_images_name);
                    cutNodeItem->setChild(cutNodeItem->rowCount() - 1, 1, cut_images_path);
                } else {
                    cutNodeItem->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                }
            }
        }
    }

    // Native XML self-healing
    XMLFile* xml = projectXml();
    if (xml) {
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
                QStringList inputPaths = resolvedInputH5Paths();
                TiXmlElement* dataNodeElem = new TiXmlElement("DataNode");
                dataNodeElem->SetAttribute("name", nodeName.toStdString().c_str());
                dataNodeElem->SetAttribute("data_count", QString::number(inputPaths.size()).toStdString().c_str());
                dataNodeElem->SetAttribute("data_processing", "cut");

                QString rank = "complex-1.0";
                TiXmlElement* srcNodeElem = nullptr;
                if (xml->find_node_with_attribute("DataNode", "name", m_inputData->nodeName().toStdString().c_str(), srcNodeElem) == 0 && srcNodeElem) {
                    const char* srcRank = srcNodeElem->Attribute("rank");
                    if (srcRank) rank = srcRank;
                }
                dataNodeElem->SetAttribute("rank", rank.toStdString().c_str());

                int index = 1;
                TiXmlElement* root_child = root->FirstChildElement();
                if (root_child) {
                    root_child = root_child->NextSiblingElement(); // skip project_info
                }

                TiXmlElement* insertBeforeNode = nullptr;
                for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++) {
                    const char* rankAttr = p->Attribute("rank");
                    if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 ||
                                     strcmp(rankAttr, "complex-1.0") == 0)) {
                        continue;
                    } else {
                        insertBeforeNode = p;
                        break;
                    }
                }
                dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

                int master_index = -1;
                if (srcNodeElem) {
                    TiXmlElement* pnode = nullptr;
                    if (xml->_find_node(srcNodeElem, "master_image", pnode) == 0 && pnode) {
                        sscanf(pnode->GetText(), "%d", &master_index);
                    }
                }

                for (int i = 0; i < expectedH5Paths.size(); ++i) {
                    QFileInfo fi(expectedH5Paths[i]);
                    QString outBase = fi.baseName();
                    QString relativePath = "/" + nodeName + "/" + outBase + ".h5";

                    TiXmlElement* imageElem = new TiXmlElement("image");
                    imageElem->SetAttribute("name", outBase.toStdString().c_str());
                    imageElem->SetAttribute("path", relativePath.toStdString().c_str());
                    imageElem->SetAttribute("index", QString::number(i + 1).toStdString().c_str());

                    TiXmlElement* offsetRowElem = new TiXmlElement("offset_row");
                    offsetRowElem->LinkEndChild(new TiXmlText("0"));
                    imageElem->LinkEndChild(offsetRowElem);

                    TiXmlElement* offsetColElem = new TiXmlElement("offset_col");
                    offsetColElem->LinkEndChild(new TiXmlText("0"));
                    imageElem->LinkEndChild(offsetColElem);

                    if (m_mode == 1) { // Coordinates
                        TiXmlElement* lonElem = new TiXmlElement("lon");
                        lonElem->LinkEndChild(new TiXmlText(QString::number(m_lon, 'f', 6).toStdString().c_str()));
                        imageElem->LinkEndChild(lonElem);

                        TiXmlElement* latElem = new TiXmlElement("lat");
                        latElem->LinkEndChild(new TiXmlText(QString::number(m_lat, 'f', 6).toStdString().c_str()));
                        imageElem->LinkEndChild(latElem);

                        TiXmlElement* widthElem = new TiXmlElement("width");
                        widthElem->LinkEndChild(new TiXmlText(QString::number(m_width, 'f', 1).toStdString().c_str()));
                        imageElem->LinkEndChild(widthElem);

                        TiXmlElement* heightElem = new TiXmlElement("height");
                        heightElem->LinkEndChild(new TiXmlText(QString::number(m_height, 'f', 1).toStdString().c_str()));
                        imageElem->LinkEndChild(heightElem);
                    }

                    dataNodeElem->LinkEndChild(imageElem);
                }

                if (master_index != -1) {
                    TiXmlElement* masterElem = new TiXmlElement("master_image");
                    masterElem->LinkEndChild(new TiXmlText(QString::number(master_index).toStdString().c_str()));
                    dataNodeElem->LinkEndChild(masterElem);
                }

                if (insertBeforeNode) {
                    root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                    delete dataNodeElem;
                } else {
                    root->LinkEndChild(dataNodeElem);
                }
            }
        }
    }

    // 阻塞信号，避免修改 widget 参数时触发 invalidateNodeData
    QSignalBlocker b1(m_modeCombo);
    QSignalBlocker b2(m_saveToProjectCheckBox);
    QSignalBlocker b3(m_leftSpin);
    QSignalBlocker b4(m_rightSpin);
    QSignalBlocker b5(m_topSpin);
    QSignalBlocker b6(m_bottomSpin);
    QSignalBlocker b7(m_lonEdit);
    QSignalBlocker b8(m_latEdit);
    QSignalBlocker b9(m_widthEdit);
    QSignalBlocker b10(m_heightEdit);
    QSignalBlocker b11(m_outputNodeNameEdit);

    if (m_modeCombo) m_modeCombo->setCurrentIndex(m_mode);
    if (m_lonEdit) m_lonEdit->setText(QString::number(m_lon, 'f', 6));
    if (m_latEdit) m_latEdit->setText(QString::number(m_lat, 'f', 6));
    if (m_widthEdit) m_widthEdit->setText(QString::number(m_width, 'f', 1));
    if (m_heightEdit) m_heightEdit->setText(QString::number(m_height, 'f', 1));
    if (m_leftSpin) m_leftSpin->setValue(m_left);
    if (m_rightSpin) m_rightSpin->setValue(m_right);
    if (m_topSpin) m_topSpin->setValue(m_top);
    if (m_bottomSpin) m_bottomSpin->setValue(m_bottom);
    if (m_saveToProjectCheckBox) m_saveToProjectCheckBox->setChecked(m_saveToProject);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);

    updateLabels();
    updateWidgetSize();
        return true;
}

QStandardItemModel* CutNode::projectModel() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString CutNode::projectPath() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectPath() : QString();
}

QString CutNode::projectName() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* CutNode::projectXml() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

QStringList CutNode::resolvedInputH5Paths() const
{
    QStringList paths;
    if (!m_inputData) return paths;

    QSet<QString> seen;
    for (const QString& rawPath : m_inputData->filePaths()) {
        QFileInfo info(rawPath);
        if (info.isFile() && info.suffix().compare("h5", Qt::CaseInsensitive) == 0) {
            QString path = info.absoluteFilePath();
            if (!seen.contains(path)) {
                paths.append(path);
                seen.insert(path);
            }
        } else if (info.isDir()) {
            QDir dir(info.absoluteFilePath());
            QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name);
            for (const QFileInfo& fileInfo : files) {
                if (fileInfo.suffix().compare("h5", Qt::CaseInsensitive) != 0) continue;
                QString path = fileInfo.absoluteFilePath();
                if (!seen.contains(path)) {
                    paths.append(path);
                    seen.insert(path);
                }
            }
        }
    }

    return paths;
}

QStringList CutNode::resolvedInputPreviewPaths() const
{
    QStringList paths;
    if (!m_inputData) return paths;

    QSet<QString> seen;
    QStringList imageSuffixes = {"jpg", "jpeg", "png", "bmp"};

    for (const QString& rawPath : m_inputData->filePaths()) {
        QFileInfo info(rawPath);
        if (info.isFile()) {
            QString suffix = info.suffix().toLower();
            QString path;
            if (imageSuffixes.contains(suffix)) {
                path = info.absoluteFilePath();
            } else if (suffix == "h5") {
                QString jpgPath = info.absolutePath() + "/" + info.baseName() + ".jpg";
                if (QFile::exists(jpgPath)) {
                    path = QFileInfo(jpgPath).absoluteFilePath();
                }
            }
            if (!path.isEmpty() && !seen.contains(path)) {
                paths.append(path);
                seen.insert(path);
            }
        } else if (info.isDir()) {
            QDir dir(info.absoluteFilePath());
            QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name);
            for (const QFileInfo& fileInfo : files) {
                if (!imageSuffixes.contains(fileInfo.suffix().toLower())) continue;
                QString path = fileInfo.absoluteFilePath();
                if (!seen.contains(path)) {
                    paths.append(path);
                    seen.insert(path);
                }
            }
        }
    }

    return paths;
}

QStringList CutNode::previewImagePaths() const
{
    QStringList paths;
    if (m_mode == 2) {
        return resolvedInputPreviewPaths();
    }

    for (const QString& path : m_outputPaths) {
        QString jpg = QFileInfo(path).absolutePath() + "/" + QFileInfo(path).baseName() + ".jpg";
        if (QFile::exists(jpg)) {
            paths.append(jpg);
        }
    }
    return paths;
}

bool CutNode::supportsRoiSelection() const
{
    return m_mode == 2 && !resolvedInputH5Paths().isEmpty() && !resolvedInputPreviewPaths().isEmpty();
}

void CutNode::processRoiSelection(const QRectF& sceneRect, int imageIndex)
{
    if (m_mode != 2) return;

    QStringList previews = resolvedInputPreviewPaths();
    if (imageIndex < 0 || imageIndex >= previews.size()) return;

    QImage image(previews.at(imageIndex));
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) return;

    QRectF imageRect(0, 0, image.width(), image.height());
    QRectF rect = sceneRect.normalized().intersected(imageRect);
    if (rect.width() <= 0 || rect.height() <= 0) return;

    double left = qBound(0.0, rect.left() / image.width(), 1.0);
    double right = qBound(0.0, rect.right() / image.width(), 1.0);
    double top = qBound(0.0, rect.top() / image.height(), 1.0);
    double bottom = qBound(0.0, rect.bottom() / image.height(), 1.0);

    
    onBoxSelected(left, right, top, bottom);
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
}

void CutNode::clearRoiSelection()
{
    m_boxSelected = false;
    m_outputData.reset();
    m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    updateLabels();
    invalidateExecution();
}

QRectF CutNode::customRoi() const
{
    if (!m_boxSelected) return QRectF();

    QStringList previews = resolvedInputPreviewPaths();
    if (previews.isEmpty()) return QRectF();

    QImage image(previews.first());
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) return QRectF();

    return QRectF(m_left * image.width(),
                  m_top * image.height(),
                  (m_right - m_left) * image.width(),
                  (m_bottom - m_top) * image.height());
}

std::vector<QString> CutNode::processingInfo() const
{
    std::vector<QString> info;
    if (m_mode != 2) return info;

    QStringList h5Paths = resolvedInputH5Paths();
    QStringList previewPaths = resolvedInputPreviewPaths();

    if (previewPaths.isEmpty()) {
        info.push_back(QStringLiteral("警告：目录中未找到可用预览图。"));
    }
    if (h5Paths.isEmpty()) {
        info.push_back(QStringLiteral("警告：目录中未找到可裁剪的 H5 文件。"));
    }

    if (!previewPaths.isEmpty() && !h5Paths.isEmpty()) {
        if (m_boxSelected) {
            info.push_back(QStringLiteral("裁剪范围：L=%1, R=%2, T=%3, B=%4")
                .arg(m_left, 0, 'f', 4)
                .arg(m_right, 0, 'f', 4)
                .arg(m_top, 0, 'f', 4)
                .arg(m_bottom, 0, 'f', 4));
        } else {
            info.push_back(QStringLiteral("当前状态：尚未选择裁剪范围。"));
        }
    }

    return info;
}

void CutNode::updateLabels()
{
    if (m_previewCombo) {
        m_previewCombo->clear();
        QStringList previewPaths = resolvedInputPreviewPaths();
        for (const QString& path : previewPaths) {
            m_previewCombo->addItem(QFileInfo(path).fileName(), path);
        }
    }

    if (m_boundsLabel) {
        if (m_boxSelected) {
            m_boundsLabel->setText(QString("L: %1, R: %2, T: %3, B: %4")
                .arg(m_left, 0, 'f', 2)
                .arg(m_right, 0, 'f', 2)
                .arg(m_top, 0, 'f', 2)
                .arg(m_bottom, 0, 'f', 2));
        } else {
            m_boundsLabel->setText(QStringLiteral("尚未进行框选裁剪"));
        }
    }
}

void CutNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QJsonObject CutNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["mode"] = m_mode;
    modelJson["lon"] = m_lon;
    modelJson["lat"] = m_lat;
    modelJson["width"] = m_width;
    modelJson["height"] = m_height;

    modelJson["left"] = m_left;
    modelJson["right"] = m_right;
    modelJson["top"] = m_top;
    modelJson["bottom"] = m_bottom;
    modelJson["boxSelected"] = m_boxSelected;

    modelJson["saveToProject"] = m_saveToProject;
    modelJson["outputNodeName"] = m_outputNodeName;

    QJsonArray outputFiles;
    for (const QString& path : m_outputPaths) {
        outputFiles.append(QFileInfo(path).fileName());
    }
    modelJson["outputFiles"] = outputFiles;

    return modelJson;
}

void CutNode::load(QJsonObject const &json)
{
        // 先恢复CutNode自己的参数，因为基类load()会调用validateAndRestoreOutput()
    m_mode = json["mode"].toInt(0);
    m_lon = json["lon"].toDouble(0.0);
    m_lat = json["lat"].toDouble(0.0);
    m_width = json["width"].toDouble(1000.0);
    m_height = json["height"].toDouble(1000.0);
    
    m_left = json["left"].toDouble(0.25);
    m_right = json["right"].toDouble(0.75);
    m_top = json["top"].toDouble(0.25);
    m_bottom = json["bottom"].toDouble(0.75);
    m_boxSelected = json["boxSelected"].toBool(false);
    
    m_saveToProject = json["saveToProject"].toBool(true);
    m_outputNodeName = json["outputNodeName"].toString("AOI_Crop");

    QJsonArray outputFiles = json["outputFiles"].toArray();
    m_savedOutputFileNames.clear();
    for (const auto& f : outputFiles) {
        m_savedOutputFileNames.append(f.toString());
    }
    if (!m_savedOutputFileNames.isEmpty()) {
        QString projDir = projectPath();
                if (projDir.endsWith(".insar", Qt::CaseInsensitive)) {
            projDir = QFileInfo(projDir).absolutePath();
        }
        if (!projDir.isEmpty()) {
            m_outputPaths.clear();
            for (const auto& fn : m_savedOutputFileNames) {
                m_outputPaths.append(projDir + "/" + m_outputNodeName + "/" + fn);
            }
        }
    }

    // 基类load()会调用validateAndRestoreOutput()
        ExecutableNodeDelegateModel::load(json);
        if (_widget) {
        // 阻塞信号，避免setCurrentIndex、setValue等触发onModeChanged/invalidateExecution/invalidateNodeData
        QSignalBlocker b1(m_modeCombo);
        QSignalBlocker b2(m_saveToProjectCheckBox);
        QSignalBlocker b3(m_leftSpin);
        QSignalBlocker b4(m_rightSpin);
        QSignalBlocker b5(m_topSpin);
        QSignalBlocker b6(m_bottomSpin);
        QSignalBlocker b7(m_lonEdit);
        QSignalBlocker b8(m_latEdit);
        QSignalBlocker b9(m_widthEdit);
        QSignalBlocker b10(m_heightEdit);
        QSignalBlocker b11(m_outputNodeNameEdit);

        m_modeCombo->setCurrentIndex(m_mode);
        m_lonEdit->setText(QString::number(m_lon, 'f', 6));
        m_latEdit->setText(QString::number(m_lat, 'f', 6));
        m_widthEdit->setText(QString::number(m_width, 'f', 1));
        m_heightEdit->setText(QString::number(m_height, 'f', 1));
        m_leftSpin->setValue(m_left);
        m_rightSpin->setValue(m_right);
        m_topSpin->setValue(m_top);
        m_bottomSpin->setValue(m_bottom);
        m_saveToProjectCheckBox->setChecked(m_saveToProject);
        m_outputNodeNameEdit->setText(m_outputNodeName);

        // onModeChanged会调用invalidateExecution()，但load恢复时不需要
        // 只需要更新widget可见性
        m_autoCenterWidget->setVisible(m_mode == 0);
        m_coordinateWidget->setVisible(m_mode == 1);
        m_boxSelectionWidget->setVisible(m_mode == 2);
        updateLabels();
        updateWidgetSize();
    }
}

} // namespace QtNodes
