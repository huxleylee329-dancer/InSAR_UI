#include "PhaseElevationRegressionNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QStandardItemModel>
#include <QDebug>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

PhaseElevationRegressionNode::PhaseElevationRegressionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_polyOrderCombo(nullptr)
    , m_windowSizeEdit(nullptr)
    , m_coherenceThreshSpin(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_polyOrder(1)
    , m_windowSize(0)
    , m_coherenceThresh(0.3)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

PhaseElevationRegressionNode::~PhaseElevationRegressionNode()
{
    stopExecution();
}

unsigned int PhaseElevationRegressionNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2; // Port 0: 干涉图, Port 1: DEM（可选）
    else
        return 2; // Port 0: 成果, Port 1: 预览（可选）
}

NodeDataType PhaseElevationRegressionNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In) {
        return NodeDataType{"imported_file", "Imported File"};
    } else {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool PhaseElevationRegressionNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString PhaseElevationRegressionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0) return QStringLiteral("干涉图");
        else return QStringLiteral("DEM ?");
    } else {
        if (portIndex == 0) return QStringLiteral("成果 *");
        else return QStringLiteral("预览 ?");
    }
    return QString();
}

bool PhaseElevationRegressionNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1) return true;
    if (portType == PortType::Out && portIndex == 1) return true;
    return false;
}

void PhaseElevationRegressionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_imageInfoData.reset();
    }
}

std::shared_ptr<NodeData> PhaseElevationRegressionNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* PhaseElevationRegressionNode::embeddedWidget()
{
    if (!_widget) createWidget();
    return _widget;
}

QJsonObject PhaseElevationRegressionNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["polyOrder"] = m_polyOrder;
    modelJson["windowSize"] = m_windowSize;
    modelJson["coherenceThresh"] = m_coherenceThresh;
    return modelJson;
}

void PhaseElevationRegressionNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();
    QJsonValue vPoly = json["polyOrder"];
    if (!vPoly.isUndefined()) m_polyOrder = vPoly.toInt();
    QJsonValue vWin = json["windowSize"];
    if (!vWin.isUndefined()) m_windowSize = vWin.toInt();
    QJsonValue vCoh = json["coherenceThresh"];
    if (!vCoh.isUndefined()) m_coherenceThresh = vCoh.toDouble();

    // SOP Rule 15: 先解析参数，再调用基类 load（会触发 validateAndRestoreOutput）
    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_polyOrderCombo) m_polyOrderCombo->setCurrentIndex(m_polyOrder - 1);
    if (m_windowSizeEdit) m_windowSizeEdit->setText(QString::number(m_windowSize));
    if (m_coherenceThreshSpin) m_coherenceThreshSpin->setValue(m_coherenceThresh);
}

void PhaseElevationRegressionNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void PhaseElevationRegressionNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* formLayout = new QFormLayout(_widget);
    formLayout->setContentsMargins(6, 6, 6, 6);
    formLayout->setSpacing(6);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) Q_EMIT _scene->modified(_scene);
    };

    // 多项式阶数
    m_polyOrderCombo = new QComboBox();
    m_polyOrderCombo->setEditable(false);
    m_polyOrderCombo->addItem("1 - 线性");
    m_polyOrderCombo->addItem("2 - 二次");
    m_polyOrderCombo->setCurrentIndex(m_polyOrder - 1);
    connect(m_polyOrderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, invalidateNodeData](int index) {
            int val = index + 1;
            if (m_polyOrder != val) {
                if (!confirmParameterChange()) {
                    m_polyOrderCombo->blockSignals(true);
                    m_polyOrderCombo->setCurrentIndex(m_polyOrder - 1);
                    m_polyOrderCombo->blockSignals(false);
                    return;
                }
                m_polyOrder = val;
                invalidateNodeData();
            }
        });

    // 滑动窗口大小
    m_windowSizeEdit = new QLineEdit();
    m_windowSizeEdit->setText(QString::number(m_windowSize));
    m_windowSizeEdit->setPlaceholderText("0 = 全局回归");
    connect(m_windowSizeEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_windowSizeEdit->text().toInt();
        if (m_windowSize != val) {
            if (!confirmParameterChange()) {
                m_windowSizeEdit->setText(QString::number(m_windowSize));
                return;
            }
            m_windowSize = val;
            invalidateNodeData();
        }
    });

    // 相干性阈值
    m_coherenceThreshSpin = new QDoubleSpinBox();
    m_coherenceThreshSpin->setRange(0.0, 1.0);
    m_coherenceThreshSpin->setSingleStep(0.05);
    m_coherenceThreshSpin->setValue(m_coherenceThresh);
    connect(m_coherenceThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, invalidateNodeData](double val) {
            if (qAbs(m_coherenceThresh - val) > 1e-6) {
                if (!confirmParameterChange()) {
                    m_coherenceThreshSpin->setValue(m_coherenceThresh);
                    return;
                }
                m_coherenceThresh = val;
                invalidateNodeData();
            }
        });

    // 目标节点名称
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

    formLayout->addRow("回归阶数", m_polyOrderCombo);
    formLayout->addRow("滑动窗口", m_windowSizeEdit);
    formLayout->addRow("相干阈值", m_coherenceThreshSpin);
    formLayout->addRow("目标节点", m_outputNodeNameEdit);
}

void PhaseElevationRegressionNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString PhaseElevationRegressionNode::generateDefaultOutputName() const
{
    if (m_inputData) {
        return m_inputData->nodeName() + "_AtmosReg";
    }
    return "Atmospheric_Regression";
}

bool PhaseElevationRegressionNode::validateInputs() const
{
    if (projectName().isEmpty()) return false;
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;

    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstNode.isEmpty()) return false;
    if (!dstNode.contains(QRegularExpression("^\\w+$"))) return false;

    return true;
}

bool PhaseElevationRegressionNode::prepareToStart()
{
    if (!validateInputs()) return false;

    m_preparedDstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName() : m_outputNodeNameEdit->text().trimmed();
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedPolyOrder = m_polyOrder;
    m_preparedWindowSize = m_windowSizeEdit ? m_windowSizeEdit->text().toInt() : m_windowSize;
    m_preparedCoherenceThresh = m_coherenceThreshSpin ? m_coherenceThreshSpin->value() : m_coherenceThresh;

    QStringList pathsToCheck;
    QStringList srcPaths = m_inputData->filePaths();
    for (const QString& srcPath : srcPaths) {
        QFileInfo fi(srcPath);
        pathsToCheck.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + fi.baseName() + "_atmos.h5");
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedDstNode, pathsToCheck, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void PhaseElevationRegressionNode::executeProcessing()
{
    InSARLogManager::LogInfo("PhaseElevationRegressionNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        m_outputNodeNameEdit->setEnabled(true);
        m_polyOrderCombo->setEnabled(true);
        m_windowSizeEdit->setEnabled(true);
        m_coherenceThreshSpin->setEnabled(true);

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
        NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_preparedDstNode);
    }

    setProgress(0);
    setState(ExecutionState::Running);

    m_thread = new QThread();
    m_workerThread = new PhaseElevationRegressionWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &PhaseElevationRegressionNode::startRegression, m_workerThread, &PhaseElevationRegressionWorker::doRegression);
    connect(m_thread, &QThread::started, this, [this]() {
        Q_EMIT startRegression(m_preparedPolyOrder, m_preparedWindowSize, m_preparedCoherenceThresh,
            m_preparedSavePath, m_preparedProjectName, m_preparedSrcNode, m_preparedDstNode, projectModel());
    });
    connect(m_workerThread, &PhaseElevationRegressionWorker::updateProcess, this, &PhaseElevationRegressionNode::onProgressUpdate);
    connect(m_workerThread, &PhaseElevationRegressionWorker::endProcess, this, &PhaseElevationRegressionNode::onProcessingFinished);
    connect(m_workerThread, &PhaseElevationRegressionWorker::errorProcess, this, &PhaseElevationRegressionNode::onError);
    connect(m_workerThread, &PhaseElevationRegressionWorker::sendModel, this, &PhaseElevationRegressionNode::onModelUpdated);
    connect(m_workerThread, &PhaseElevationRegressionWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
            setState(ExecutionState::Running);
    });

    m_outputNodeNameEdit->setEnabled(false);
    m_polyOrderCombo->setEnabled(false);
    m_windowSizeEdit->setEnabled(false);
    m_coherenceThreshSpin->setEnabled(false);

    m_thread->start();
}

void PhaseElevationRegressionNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void PhaseElevationRegressionNode::onProcessingFinished()
{
    QString dstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName() : m_outputNodeNameEdit->text().trimmed();
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

    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    if (!h5Paths.isEmpty()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, jpgPaths]() {
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);

            m_outputNodeNameEdit->setEnabled(true);
            m_polyOrderCombo->setEnabled(true);
            m_windowSizeEdit->setEnabled(true);
            m_coherenceThreshSpin->setEnabled(true);

            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("PhaseElevationRegressionNode", "处理完成.");
            finishExecution();
            Q_EMIT dataUpdated(0);
        });

        QFuture<void> future = QtConcurrent::run([h5Paths, jpgPaths, types]() {
            for (int i = 0; i < h5Paths.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(h5Paths[i], jpgPaths[i], types[i]);
            }
        });
        m_remedyWatcher.setFuture(future);
    } else {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        Q_EMIT dataUpdated(1);

        m_outputNodeNameEdit->setEnabled(true);
        m_polyOrderCombo->setEnabled(true);
        m_windowSizeEdit->setEnabled(true);
        m_coherenceThreshSpin->setEnabled(true);

        setState(ExecutionState::Running);
        setProgress(100);
        finishExecution();
        Q_EMIT dataUpdated(0);
    }
}

void PhaseElevationRegressionNode::onError(const QString& error)
{
    Q_UNUSED(error);
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    m_outputNodeNameEdit->setEnabled(true);
    m_polyOrderCombo->setEnabled(true);
    m_windowSizeEdit->setEnabled(true);
    m_coherenceThreshSpin->setEnabled(true);

    setState(ExecutionState::Error);
}

void PhaseElevationRegressionNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) iface->refreshProjectTree();
}

bool PhaseElevationRegressionNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return false;

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (!dir.exists()) return false;

    QStringList filters;
    filters << "*.h5";
    QStringList h5Files = dir.entryList(filters, QDir::Files);
    if (h5Files.isEmpty()) return false;

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

QStringList PhaseElevationRegressionNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return list;

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);
        for (const QString& h5File : h5Files) {
            QString baseName = QFileInfo(h5File).baseName();
            QString jpgPath = outputPath + baseName + ".jpg";
            if (QFile::exists(jpgPath)) list.append(jpgPath);
        }
    }
    return list;
}

QStandardItemModel* PhaseElevationRegressionNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString PhaseElevationRegressionNode::projectPath() const
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

QString PhaseElevationRegressionNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* PhaseElevationRegressionNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void PhaseElevationRegressionNode::execute()
{
    executeProcessing();
}

void PhaseElevationRegressionNode::stopExecution()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void PhaseElevationRegressionNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

} // namespace QtNodes
