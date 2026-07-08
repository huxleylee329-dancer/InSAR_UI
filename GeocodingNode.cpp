#include "GeocodingNode.h"
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
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QTimer>
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

GeocodingNode::GeocodingNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_typeCombo(nullptr)
    , m_multiRgLabel(nullptr)
    , m_multiRgSpin(nullptr)
    , m_multiAzLabel(nullptr)
    , m_multiAzSpin(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_type(1)       // default: 干涉产品
    , m_multiRg(1)    // default: 1
    , m_multiAz(1)    // default: 1
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

GeocodingNode::~GeocodingNode()
{
    stopExecution();
}

unsigned int GeocodingNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType GeocodingNode::dataType(PortType portType, PortIndex portIndex) const
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

bool GeocodingNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString GeocodingNode::portCaption(PortType portType, PortIndex portIndex) const
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

bool GeocodingNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1)
        return true;
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void GeocodingNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

        if (!m_inputData || m_inputData->filePaths().isEmpty()) {
            m_outputData.reset();
            m_imageInfoData.reset();
            setOutputData(0, nullptr);
            setOutputData(1, nullptr);
        } else {
            // Automatically determine type based on the level of the input node
            QString srcNode = m_inputData->nodeName();
            QStandardItemModel* model = projectModel();
            if (model) {
                QStandardItem* project = model->findItems(projectName())[0];
                if (project) {
                    for (int i = 0; i < project->rowCount(); i++) {
                        if (project->child(i, 0)->text() == srcNode) {
                            QString level = project->child(i, 1)->text();
                            if (level.contains("complex") || level.contains("amplitude")) {
                                m_type = 2; // SAR图像
                            } else {
                                m_type = 1; // 干涉产品
                            }
                            if (m_typeCombo) {
                                m_typeCombo->setCurrentIndex(m_type - 1);
                                onTypeChanged(m_type - 1);
                            }
                            break;
                        }
                    }
                }
            }
        }

        if (m_inputData && m_outputNodeName.isEmpty()) {
            m_outputNodeName = generateDefaultOutputName();
            if (m_outputNodeNameEdit) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
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

std::shared_ptr<NodeData> GeocodingNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* GeocodingNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject GeocodingNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["type"] = m_type;
    modelJson["multiRg"] = m_multiRgSpin ? m_multiRgSpin->value() : m_multiRg;
    modelJson["multiAz"] = m_multiAzSpin ? m_multiAzSpin->value() : m_multiAz;
    modelJson[QStringLiteral("demPath")] = m_demPath;

    return modelJson;
}

void GeocodingNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vType = json["type"];
    if (!vType.isUndefined()) m_type = vType.toInt();

    QJsonValue vMultiRg = json["multiRg"];
    if (!vMultiRg.isUndefined()) m_multiRg = vMultiRg.toInt();

    QJsonValue vMultiAz = json["multiAz"];
    if (!vMultiAz.isUndefined()) m_multiAz = vMultiAz.toInt();

    QJsonValue vDemPath = json[QStringLiteral("demPath")];
    if (!vDemPath.isUndefined()) m_demPath = vDemPath.toString();

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_typeCombo) {
        m_typeCombo->setCurrentIndex(m_type - 1);
        onTypeChanged(m_type - 1);
    }
    if (m_multiRgSpin) m_multiRgSpin->setValue(m_multiRg);
    if (m_multiAzSpin) m_multiAzSpin->setValue(m_multiAz);

    validateAndRestoreOutput();
}

void GeocodingNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void GeocodingNode::createWidget()
{
    _widget = new QWidget();
    _widget->setFixedWidth(300); // SOP Rule 2: prevent size inflation

    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Type Row
    auto* typeRow = new QHBoxLayout();
    auto* typeLabel = new QLabel(QStringLiteral("处理类型:"));
    typeLabel->setFixedWidth(80); // SOP Rule 10: Fixed Label Width
    m_typeCombo = new QComboBox();
    m_typeCombo->addItem(QStringLiteral("干涉产品"));
    m_typeCombo->addItem(QStringLiteral("SAR图像"));
    m_typeCombo->setCurrentIndex(m_type - 1);
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(m_typeCombo);
    layout->addLayout(typeRow);

    // Multi Range Row
    auto* rgRow = new QHBoxLayout();
    m_multiRgLabel = new QLabel(QStringLiteral("距离多视数:"));
    m_multiRgLabel->setFixedWidth(80);
    m_multiRgSpin = new QSpinBox();
    m_multiRgSpin->setRange(1, 100);
    m_multiRgSpin->setValue(m_multiRg);
    rgRow->addWidget(m_multiRgLabel);
    rgRow->addWidget(m_multiRgSpin);
    layout->addLayout(rgRow);

    // Multi Azimuth Row
    auto* azRow = new QHBoxLayout();
    m_multiAzLabel = new QLabel(QStringLiteral("方位多视数:"));
    m_multiAzLabel->setFixedWidth(80);
    m_multiAzSpin = new QSpinBox();
    m_multiAzSpin->setRange(1, 100);
    m_multiAzSpin->setValue(m_multiAz);
    azRow->addWidget(m_multiAzLabel);
    azRow->addWidget(m_multiAzSpin);
    layout->addLayout(azRow);

    // Output Name Row
    auto* outRow = new QHBoxLayout();
    auto* outLabel = new QLabel(QStringLiteral("目标节点名:"));
    outLabel->setFixedWidth(80);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入")); // SOP Rule 11
    m_outputNodeNameEdit->setText(m_outputNodeName);
    outRow->addWidget(outLabel);
    outRow->addWidget(m_outputNodeNameEdit);
    layout->addLayout(outRow);

    // DEM Path Row
    auto* demRow = new QHBoxLayout();
    m_demPathLabel = new QLabel(QStringLiteral("DEM路径:"));
    m_demPathLabel->setFixedWidth(80);
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
    connect(m_demPathEdit, &QLineEdit::editingFinished, this, [this]() {
        QString text = m_demPathEdit->text().trimmed();
        if (m_demPath != text) {
            m_demPath = text;
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
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_demPath, true);
            }
        }
    });

    demRow->addWidget(m_demPathLabel);
    demRow->addWidget(m_demPathEdit);
    demRow->addWidget(m_demBrowseBtn);
    layout->addLayout(demRow);

    // Connections
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GeocodingNode::onTypeChanged);
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text;
        Q_EMIT m_outputNodeNameEdit->textChanged(text);
    });

    onTypeChanged(m_type - 1);
}

void GeocodingNode::onTypeChanged(int index)
{
    m_type = index + 1;
    updateParameterWidgetsEnableState();
}

QString GeocodingNode::generateDefaultOutputName() const
{
    if (m_inputData && !m_inputData->nodeName().isEmpty()) {
        return m_inputData->nodeName() + "_geocoded";
    }
    return "Geocoded";
}

bool GeocodingNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;

    QString outputName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName.trimmed();
    if (outputName.isEmpty())
        return false;

    // Check if the output node name contains invalid characters
    bool bFlag = outputName.contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
        return false;

    return true;
}

bool GeocodingNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    QString dstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text().trimmed();

    QString savePath = projectPath();

    m_preparedType = m_typeCombo ? m_typeCombo->currentIndex() + 1 : m_type;
    m_preparedMultiRg = m_multiRgSpin ? m_multiRgSpin->value() : m_multiRg;
    m_preparedMultiAz = m_multiAzSpin ? m_multiAzSpin->value() : m_multiAz;
    m_preparedDemPath = m_demPath;

    m_preparedDstNode = dstNode;

    QStringList srcPaths = m_inputData->filePaths();

    m_preparedOutputPaths.clear();
    for (const QString& srcPath : srcPaths) {
        QFileInfo fi(srcPath);
        QString changeName = fi.baseName() + "_geocoded";
        m_preparedOutputPaths.append(savePath + "/" + dstNode + "/" + changeName + ".h5");
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), dstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void GeocodingNode::executeProcessing()
{
    InSARLogManager::LogInfo("GeocodingNode", "executeProcessing started.");

    setProgress(0);

    QString dstNode = m_preparedDstNode;
    QString savePath = projectPath();
    QString dstProject = projectName();
    QString srcNode = m_inputData->nodeName();
    QString preparedDemPath = m_preparedDemPath;

    int type = m_preparedType;
    int multiRg = m_preparedMultiRg;
    int multiAz = m_preparedMultiAz;

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = dstNode;
        
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

    // Clean up old data nodes to prevent tree duplicates (SOP Rule 14)
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode);

    m_thread = new QThread();
    m_workerThread = new GeocodingWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &GeocodingNode::startGeocoding, m_workerThread, &GeocodingWorker::GeocodingWithDem);
    connect(m_thread, &QThread::started, [this, type, multiRg, multiAz, dstProject, srcNode, dstNode, preparedDemPath]() {
        Q_EMIT startGeocoding(type, multiRg, multiAz, dstProject, srcNode, dstNode, projectModel(), preparedDemPath);
    });
    connect(m_workerThread, &GeocodingWorker::updateProcess, this, &GeocodingNode::onProgressUpdate);
    connect(m_workerThread, &GeocodingWorker::endProcess, this, &GeocodingNode::onProcessingFinished);
    connect(m_workerThread, &GeocodingWorker::errorProcess, this, &GeocodingNode::onError);
    connect(m_workerThread, &GeocodingWorker::sendModel, this, &GeocodingNode::onModelUpdated);
    connect(m_workerThread, &GeocodingWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Dynamic recovery of Running state for Automatic execution mode (SOP Rule 5)
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
            setState(ExecutionState::Running);
    });

    // Disable inputs during execution
    updateParameterWidgetsEnableState();

    m_thread->start();
}

void GeocodingNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void GeocodingNode::onProcessingFinished()
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
        
        FormatConversion FC;
        cv::Mat dummy;
        for (const QString& h5File : h5Files) {
            QString h5Path = dir.absoluteFilePath(h5File);
            h5Paths.append(h5Path);
            QString baseName = QFileInfo(h5File).baseName();
            jpgPaths.append(outputPath + baseName + ".jpg");
            
            // Determine type dynamically from datasets
            QString type = "amplitude";
            {
                NodeUtils::Hdf5Locker locker;
                if (NodeUtils::readMatFromH5(h5Path, "phase", dummy)) {
                    type = "phase";
                } else if (NodeUtils::readMatFromH5(h5Path, "coherence", dummy)) {
                    type = "coherence";
                } else if (NodeUtils::readMatFromH5(h5Path, "dem", dummy)) {
                    type = "dem";
                } else if (NodeUtils::readMatFromH5(h5Path, "defomation_velocity", dummy)) {
                    type = "SBAS";
                }
            }
            types.append(type);
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

    updateParameterWidgetsEnableState();

    m_outputNodeName = dstNode;
    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    // Generate JPG previews asynchronously (SOP Rule 7)
    if (!h5Paths.isEmpty()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, jpgPaths]() {
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("GeocodingNode", "Processing preview generation completed.");
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
        setState(ExecutionState::Running);
        setProgress(100);
        finishExecution();
    }
}

void GeocodingNode::onError(const QString& error)
{
    InSARLogManager::LogError("GeocodingNode", "executeProcessing failed: " + error);
    Q_EMIT executionError(error);

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

void GeocodingNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

bool GeocodingNode::validateAndRestoreOutput()
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

    FormatConversion FC;
    cv::Mat dummy;
    for (const QString& h5File : h5Files) {
        QString h5Path = dir.absoluteFilePath(h5File);
        QString baseName = QFileInfo(h5File).baseName();
        h5Paths.append(h5Path);
        expectedJpgPaths.append(outputPath + baseName + ".jpg");

        // Determine type dynamically from datasets
        QString type = "amplitude";
        {
            NodeUtils::Hdf5Locker locker;
            if (NodeUtils::readMatFromH5(h5Path, "phase", dummy)) {
                type = "phase";
            } else if (NodeUtils::readMatFromH5(h5Path, "coherence", dummy)) {
                type = "coherence";
            } else if (NodeUtils::readMatFromH5(h5Path, "dem", dummy)) {
                type = "dem";
            } else if (NodeUtils::readMatFromH5(h5Path, "defomation_velocity", dummy)) {
                type = "SBAS";
            }
        }
        types.append(type);
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    // Remedy missing JPG previews in background (SOP Rule 15)
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
            InSARLogManager::LogInfo("GeocodingNode", "validateAndRestoreOutput background rendering completed.");
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

QStringList GeocodingNode::previewImagePaths() const
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

QStandardItemModel* GeocodingNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString GeocodingNode::projectPath() const
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

QString GeocodingNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* GeocodingNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void GeocodingNode::execute()
{
    executeProcessing();
}

void GeocodingNode::stopExecution()
{
    if (m_workerThread) {
        m_workerThread->StopProcess();
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void GeocodingNode::processAutomatically()
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

void GeocodingNode::updateParameterWidgetsEnableState()
{
    bool isExec = m_thread && m_thread->isRunning();
    bool enableWidgets = !isExec;

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets);
    if (m_typeCombo) m_typeCombo->setEnabled(enableWidgets);

    bool isInterfero = (m_type == 1);
    if (m_multiRgSpin) m_multiRgSpin->setEnabled(enableWidgets && !isInterfero);
    if (m_multiAzSpin) m_multiAzSpin->setEnabled(enableWidgets && !isInterfero);
    if (m_multiRgLabel) m_multiRgLabel->setEnabled(enableWidgets && !isInterfero);
    if (m_multiAzLabel) m_multiAzLabel->setEnabled(enableWidgets && !isInterfero);

    bool hasDemConn = (m_demInputData != nullptr);
    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !hasDemConn);
    if (m_demPathEdit) m_demPathEdit->setEnabled(enableWidgets && !hasDemConn);
    if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(enableWidgets && !hasDemConn);
}

} // namespace QtNodes
