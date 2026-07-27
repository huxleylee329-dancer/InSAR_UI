#include "InSARLogManager.h"
#include "S1FrameMergeNode.h"
#include "S1FrameMergeWorker.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InterfaceManager.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "tinyxml.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace QtNodes {

S1FrameMergeNode::S1FrameMergeNode()
    : ExecutableNodeDelegateModel()
    , m_indexSpins{nullptr, nullptr}
    , m_outputNodeNameEdit(nullptr)
    , m_inputs{nullptr, nullptr}
    , m_outputData(nullptr)
    , m_imageInfoData(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
}

S1FrameMergeNode::~S1FrameMergeNode()
{
    m_remedyWatcher.cancel();
    m_remedyWatcher.waitForFinished();

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
    else if (m_worker)
    {
        m_worker->deleteLater();
    }
    m_worker = nullptr;
}

unsigned int S1FrameMergeNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType S1FrameMergeNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "S1 Frame Data"};

    if (portIndex == 0)
        return NodeDataType{"imported_file", "S1 Merged Frame"};
    else
        return NodeDataType{"image_info", "Image Info"};
}

std::shared_ptr<NodeData> S1FrameMergeNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_imageInfoData;
}

bool S1FrameMergeNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return portType == PortType::Out;
}

QString S1FrameMergeNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return tr("成果 *");
        else if (portIndex == 1)
            return tr("预览 ?");
    }
    Q_UNUSED(portIndex);
    return QString();
}

bool S1FrameMergeNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void S1FrameMergeNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port >= 0 && port < 2)
    {
        m_inputs[port] = std::dynamic_pointer_cast<ImportedFileData>(data);

        // Generate default output name if both inputs connected and name not set
        if (m_inputs[0] && m_inputs[1] && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeName = generateDefaultOutputName();
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    // Delegate to base class to handle execution mode
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* S1FrameMergeNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject S1FrameMergeNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    QString nodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["outputNodeName"] = nodeName;

    modelJson["index1"] = m_index1;
    modelJson["index2"] = m_index2;

    return modelJson;
}

void S1FrameMergeNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined())
    {
        m_outputNodeName = vName.toString();
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    QJsonValue v1 = json["index1"];
    if (!v1.isUndefined())
    {
        m_index1 = v1.toInt();
        if (m_indexSpins[0])
            m_indexSpins[0]->setValue(m_index1);
    }

    QJsonValue v2 = json["index2"];
    if (!v2.isUndefined())
    {
        m_index2 = v2.toInt();
        if (m_indexSpins[1])
            m_indexSpins[1]->setValue(m_index2);
    }
}

void S1FrameMergeNode::createWidget()
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



    // Image Index 1
    auto* index1Layout = new QHBoxLayout();
    QLabel* index1Label = new QLabel("Image Index 1:");
    index1Label->setFixedWidth(85);
    index1Layout->addWidget(index1Label);
    m_indexSpins[0] = new QSpinBox();
    m_indexSpins[0]->setMinimum(1);
    m_indexSpins[0]->setMaximum(100);
    m_indexSpins[0]->setValue(m_index1);
    connect(m_indexSpins[0], &QSpinBox::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_indexSpins[0]->value();
        if (m_index1 != val) {
            if (!confirmParameterChange()) {
                m_indexSpins[0]->setValue(m_index1);
                return;
            }
            m_index1 = val;
            invalidateNodeData();
        }
    });
    index1Layout->addWidget(m_indexSpins[0]);
    layout->addLayout(index1Layout);



    // Image Index 2
    auto* index2Layout = new QHBoxLayout();
    QLabel* index2Label = new QLabel("Image Index 2:");
    index2Label->setFixedWidth(85);
    index2Layout->addWidget(index2Label);
    m_indexSpins[1] = new QSpinBox();
    m_indexSpins[1]->setMinimum(1);
    m_indexSpins[1]->setMaximum(100);
    m_indexSpins[1]->setValue(m_index2);
    connect(m_indexSpins[1], &QSpinBox::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_indexSpins[1]->value();
        if (m_index2 != val) {
            if (!confirmParameterChange()) {
                m_indexSpins[1]->setValue(m_index2);
                return;
            }
            m_index2 = val;
            invalidateNodeData();
        }
    });
    index2Layout->addWidget(m_indexSpins[1]);
    layout->addLayout(index2Layout);

    // 目标节点名
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLabel->setFixedWidth(85);
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("鑷姩鐢熸垚鎴栨墜鍔ㄨ緭鍏?"));
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

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // 自愈与刷新
}

QString S1FrameMergeNode::generateDefaultOutputName() const
{
    if (m_inputs[0] && m_inputs[1])
    {
        QString node1 = m_inputs[0]->nodeName();
        QString node2 = m_inputs[1]->nodeName();
        return node1 + "_" + node2 + "_Merged";
    }
    return "FrameMerge_Output";
}

bool S1FrameMergeNode::validateInputs() const
{
    if (!m_inputs[0] || !m_inputs[1])
    {
        return false;
    }

    QString node1 = m_inputs[0]->nodeName();
    QString node2 = m_inputs[1]->nodeName();
    if (node1.isEmpty() || node2.isEmpty())
    {
        return false;
    }

    // Validate project context
    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
    {
        return false;
    }

    return true;
}

void S1FrameMergeNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void S1FrameMergeNode::onProcessingFinished()
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    if (m_generatedOutputPath.isEmpty() || !QFileInfo::exists(m_generatedOutputPath)) {
        onError(QStringLiteral("Frame merge did not return a valid output file."));
        return;
    }

    const QString dstNode = m_outputNodeName;
    const QFileInfo outputInfo(m_generatedOutputPath);
    const QString previewPath = outputInfo.absolutePath() + "/" + outputInfo.baseName() + ".jpg";
    m_outputData = std::make_shared<ImportedFileData>(QStringList() << m_generatedOutputPath, dstNode);
    setOutputData(0, m_outputData);
    if (QFile::exists(previewPath)) {
        m_imageInfoData = std::make_shared<ImageInfoData>(QStringList() << previewPath);
        setOutputData(1, m_imageInfoData);
    } else {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
    }

    m_worker = nullptr;
    m_thread = nullptr;

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setEnabled(true);

    setState(ExecutionState::Running);
    setProgress(100);
    InSARLogManager::LogInfo("S1FrameMergeNode", "executeProcessing completed.");
    finishExecution();
}

void S1FrameMergeNode::onError(const QString& error)
{
    // Clean up thread
    m_worker = nullptr;
    m_thread = nullptr;

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Error);
    InSARLogManager::LogError("S1FrameMergeNode", "Error during frame merge: " + error);
}

void S1FrameMergeNode::onCancelled()
{
    InSARLogManager::LogInfo("S1FrameMergeNode", "Frame merge cancellation cleanup completed.");
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void S1FrameMergeNode::onResultReceived(
    const QString& dstNode,
    const QString& filename,
    const QString& mergedH5Path,
    const QString& savePath,
    const QString& projectName)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    m_generatedOutputPath = mergedH5Path;
    QStandardItemModel* model = projectModel();
    const QList<QStandardItem*> projects = model ? model->findItems(projectName) : QList<QStandardItem*>();
    if (projects.isEmpty()) {
        InSARLogManager::LogError("S1FrameMergeNode", "Project tree root was not found.");
        return;
    }

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), dstNode, "complex-0.0", FOLDER_ICON);
    if (!outputNode) {
        InSARLogManager::LogError("S1FrameMergeNode", "Unable to create frame merge output node.");
        return;
    }
    outputNode->setToolTip(projectName);
    QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
        outputNode, filename, "complex", mergedH5Path, IMAGEDATA_ICON);
    if (imageItem) {
        outputNode->setChild(imageItem->row(), 1, new QStandardItem(mergedH5Path));
    }

    if (XMLFile* xml = projectXml()) {
        const QString relativePath = QString("/%1/%2").arg(dstNode, QFileInfo(mergedH5Path).fileName());
        xml->XMLFile_add_origin(dstNode.toStdString().c_str(), filename.toStdString().c_str(),
            relativePath.toStdString().c_str(), "sentinel");
        const QString xmlPath = savePath + "/" + projectName;
        xml->XMLFile_save(xmlPath.toStdString().c_str());
    }
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* S1FrameMergeNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString S1FrameMergeNode::projectPath() const
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

QString S1FrameMergeNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* S1FrameMergeNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void S1FrameMergeNode::execute()
{
    executeProcessing();
}

void S1FrameMergeNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
    }
}

void S1FrameMergeNode::processAutomatically()
{
    // In automatic mode, if inputs are valid, execute
    if (prepareToStart())
    {
        executeProcessing();
    }
    else
    {
        setState(ExecutionState::Idle);
    }
}

void S1FrameMergeNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

bool S1FrameMergeNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    QString dstNode = m_outputNodeNameEdit && !m_outputNodeNameEdit->text().isEmpty()
        ? m_outputNodeNameEdit->text()
        : (m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName);
    m_preparedDstNode = dstNode;

    QString savePath = projectPath();
    QString dstProject = projectName();
    QString outputPath = savePath + "/" + dstNode + "/";

    QStringList pathsToCheck;
    const QStringList inputPaths1 = m_inputs[0]->filePaths();
    const QStringList inputPaths2 = m_inputs[1]->filePaths();
    if (inputPaths1.size() >= m_index1 && inputPaths2.size() >= m_index2)
    {
        const QString h5Path1 = inputPaths1[m_index1 - 1];
        const QString h5Path2 = inputPaths2[m_index2 - 1];
        const QString outputBaseName = QFileInfo(h5Path1).baseName() + "_" + QFileInfo(h5Path2).baseName();
        pathsToCheck.append(outputPath + outputBaseName + ".h5");
        pathsToCheck.append(outputPath + outputBaseName + ".jpg");
    }

    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    if (!pathsToCheck.isEmpty())
    {
        auto ctx = NodeUtils::getProjectContext(_widget);
        if (_isAutoTriggered) {
            m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        } else {
            m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(ctx, dstNode, pathsToCheck, nullptr);
        }
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void S1FrameMergeNode::executeProcessing()
{
    InSARLogManager::LogInfo("S1FrameMergeNode", "executeProcessing started.");

    // Prepare processing
    QString dstNode = m_preparedDstNode;
    m_outputNodeName = dstNode;
    if (m_outputNodeNameEdit && m_outputNodeNameEdit->text() != dstNode)
        m_outputNodeNameEdit->setText(dstNode);
    QString savePath = projectPath();
    QString dstProject = projectName();
    QString outputPath = savePath + "/" + dstNode + "/";

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting)
    {
        m_outputNodeName = dstNode;
        if (validateAndRestoreOutput())
        {
            setState(ExecutionState::Running);
            setProgress(100);
            finishExecution();
            return;
        }
        else
        {
            setState(ExecutionState::Error);
            return;
        }
    }
    else if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite)
    {
        QDir outputDir(outputPath);
        if (outputDir.exists())
        {
            QFileInfoList entries = outputDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
            for (const QFileInfo& entry : entries)
            {
                if (entry.isDir())
                    QDir(entry.absoluteFilePath()).removeRecursively();
                else
                    QFile::remove(entry.absoluteFilePath());
            }
        }
    }

    setProgress(0);
    setState(ExecutionState::Running);

    QString project = projectName();
    const QStringList inputPaths1 = m_inputs[0]->filePaths();
    const QStringList inputPaths2 = m_inputs[1]->filePaths();
    if (inputPaths1.size() < m_index1 || inputPaths2.size() < m_index2) {
        onError(QStringLiteral("Selected input image index is invalid."));
        return;
    }
    const QString firstH5Path = inputPaths1[m_index1 - 1];
    const QString secondH5Path = inputPaths2[m_index2 - 1];
    if (firstH5Path.isEmpty() || secondH5Path.isEmpty()) {
        onError(QStringLiteral("Selected input image file was not found."));
        return;
    }
    m_generatedOutputPath.clear();

    // Create thread
    m_thread = new QThread();
    m_worker = new S1FrameMergeWorker();
    m_worker->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1FrameMergeNode::startFrameMerge, m_worker, &S1FrameMergeWorker::S1_frame_merge);
    connect(m_thread, &QThread::started, [this, project, savePath, dstNode, firstH5Path, secondH5Path]() {
        Q_EMIT startFrameMerge(project, savePath, dstNode, firstH5Path, secondH5Path);
    });
    connect(m_worker, &S1FrameMergeWorker::updateProcess, this, &S1FrameMergeNode::onProgressUpdate);
    connect(m_worker, &S1FrameMergeWorker::endProcess, this, &S1FrameMergeNode::onProcessingFinished);
    connect(m_worker, &S1FrameMergeWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &S1FrameMergeWorker::errorProcess, this, &S1FrameMergeNode::onError);
    connect(m_worker, &S1FrameMergeWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &S1FrameMergeWorker::cancelled, this, &S1FrameMergeNode::onCancelled);
    connect(m_worker, &S1FrameMergeWorker::cancelled, m_thread, &QThread::quit);
    connect(m_worker, &S1FrameMergeWorker::sendResult, this, &S1FrameMergeNode::onResultReceived);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
    deferAutomaticCompletion();
    m_thread->start();
    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setEnabled(false);

    // 在下一个事件循环中强行将状态重置为 Running，防止基类 setInData 在 Automatic 模式下将其强行设为 Idle
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

bool S1FrameMergeNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + dstNode + "/";

    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);

        if (!h5Files.isEmpty()) {
            // 恢复 Port 0 数据
            QStringList h5Paths;
            for (const QString& h5File : h5Files) {
                h5Paths.append(dir.absoluteFilePath(h5File));
            }
            h5Paths.sort();
            m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
            setOutputData(0, m_outputData);

            QStringList existingJpgPaths;
            QStringList missingH5s;
            QStringList missingJpgs;
            QStringList allJpgPaths;

            for (const QString& h5File : h5Files) {
                QString h5Path = dir.absoluteFilePath(h5File);
                QFileInfo fi(h5Path);
                QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
                allJpgPaths.append(jpgPath);

                if (QFile::exists(jpgPath)) {
                    existingJpgPaths.append(jpgPath);
                } else {
                    missingH5s.append(h5Path);
                    missingJpgs.append(jpgPath);
                }
            }

            if (!existingJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
                setOutputData(1, m_imageInfoData);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
            }

            if (!missingH5s.isEmpty() && !m_remedyWatcher.isRunning()) {
                m_remedyWatcher.disconnect();

                connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, allJpgPaths]() {
                    QStringList restoredJpgPaths;
                    for (const QString& path : allJpgPaths) {
                        if (QFile::exists(path))
                            restoredJpgPaths.append(path);
                    }
                    if (!restoredJpgPaths.isEmpty()) {
                        m_imageInfoData = std::make_shared<ImageInfoData>(restoredJpgPaths);
                        setOutputData(1, m_imageInfoData);
                    } else {
                        m_imageInfoData.reset();
                        setOutputData(1, nullptr);
                    }
                    Q_EMIT dataUpdated(1);
                });

                QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
                    for (int i = 0; i < missingH5s.size(); ++i) {
                        NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
                    }
                });
                m_remedyWatcher.setFuture(future);
            }

            // 恢复左侧树标准项目模型 (Standard Item Model Tree View)
            QStandardItemModel* projModelPtr = projectModel();
            if (projModelPtr) {
                QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
                if (!foundProjects.isEmpty()) {
                    QStandardItem* projectItem = foundProjects.first();

                    // Find or create the frame_merge root item
                    QStandardItem* frameMergeItem = nullptr;
                    for (int i = 0; i < projectItem->rowCount(); i++) {
                        if (projectItem->child(i, 0)->text() == dstNode) {
                            frameMergeItem = projectItem->child(i, 0);
                            break;
                        }
                    }

                    if (!frameMergeItem) {
                        frameMergeItem = new QStandardItem(dstNode);
                        frameMergeItem->setToolTip(projectName());
                        int insert = 0;
                        for (; insert < projectItem->rowCount(); insert++) {
                            if (projectItem->child(insert, 1)->text().compare("complex-0.0") == 0)
                                continue;
                            else
                                break;
                        }
                        frameMergeItem->setIcon(QIcon(FOLDER_ICON));
                        projectItem->insertRow(insert, frameMergeItem);
                        QStandardItem* frameMergeRank = new QStandardItem("complex-0.0");
                        projectItem->setChild(insert, 1, frameMergeRank);
                    }

                    // Complete child image nodes
                    for (const QString& h5File : h5Files) {
                        QString h5Path = dir.absoluteFilePath(h5File);
                        QFileInfo fileinfo(h5Path);
                        QString filename = fileinfo.baseName();

                        QStandardItem* item_img = nullptr;
                        for (int j = 0; j < frameMergeItem->rowCount(); j++) {
                            if (frameMergeItem->child(j, 0)->text() == filename) {
                                item_img = frameMergeItem->child(j, 0);
                                break;
                            }
                        }

                        if (!item_img) {
                            QStandardItem* img_name = new QStandardItem(filename);
                            img_name->setToolTip("complex");
                            QStandardItem* img_path = new QStandardItem(h5Path);
                            img_name->setIcon(QIcon(IMAGEDATA_ICON));
                            frameMergeItem->appendRow(img_name);
                            frameMergeItem->setChild(frameMergeItem->rowCount() - 1, 1, img_path);
                        } else {
                            frameMergeItem->setChild(item_img->row(), 1, new QStandardItem(h5Path));
                        }
                    }
                }
            }

            // XML 工程文件自愈修复 (EXE端原生 TinyXML 实现)
            XMLFile* xml = projectXml();
            if (xml) {
                bool xmlModified = false;
                TiXmlElement* root = nullptr;
                xml->get_root(root);
                if (root) {
                    TiXmlElement* dataNodeElem = nullptr;
                    for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                        const char* nameAttr = p->Attribute("name");
                        if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
                            dataNodeElem = p;
                            break;
                        }
                    }

                    if (!dataNodeElem) {
                        dataNodeElem = new TiXmlElement("DataNode");
                        dataNodeElem->SetAttribute("name", dstNode.toStdString().c_str());
                        dataNodeElem->SetAttribute("data_count", "0");
                        dataNodeElem->SetAttribute("data_processing", "sentinel");
                        dataNodeElem->SetAttribute("rank", "complex-0.0");

                        int index = 1;
                        TiXmlElement* root_child = root->FirstChildElement();
                        if (root_child) {
                            root_child = root_child->NextSiblingElement(); // skip project_info
                        }

                        TiXmlElement* insertBeforeNode = nullptr;
                        for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++) {
                            const char* rankAttr = p->Attribute("rank");
                            if (rankAttr && strcmp(rankAttr, "complex-0.0") == 0) {
                                continue;
                            } else {
                                insertBeforeNode = p;
                                break;
                            }
                        }
                        dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

                        if (insertBeforeNode) {
                            root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                            delete dataNodeElem;
                            dataNodeElem = nullptr;
                            for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                                const char* nameAttr = p->Attribute("name");
                                if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
                                    dataNodeElem = p;
                                    break;
                                }
                            }
                        } else {
                            root->LinkEndChild(dataNodeElem);
                        }
                        xmlModified = true;
                    }

                    TiXmlElement* paramsElem = dataNodeElem->FirstChildElement("Data_Processing_Parameters");
                    if (!paramsElem) {
                        paramsElem = new TiXmlElement("Data_Processing_Parameters");
                        dataNodeElem->LinkEndChild(paramsElem);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("Sensor")) {
                        TiXmlElement* sensorElem = new TiXmlElement("Sensor");
                        sensorElem->LinkEndChild(new TiXmlText("sentinel"));
                        paramsElem->LinkEndChild(sensorElem);
                        xmlModified = true;
                    }

                    int dataCount = 0;
                    for (TiXmlElement* p = dataNodeElem->FirstChildElement("Data"); p != nullptr; p = p->NextSiblingElement("Data")) {
                        dataCount++;
                    }

                    for (const QString& h5File : h5Files) {
                        QString h5Path = dir.absoluteFilePath(h5File);
                        QFileInfo fileinfo(h5Path);
                        QString relativePath = QString("/%1/%2").arg(dstNode).arg(fileinfo.fileName());

                        bool dataExists = false;
                        for (TiXmlElement* p = dataNodeElem->FirstChildElement("Data"); p != nullptr; p = p->NextSiblingElement("Data")) {
                            TiXmlElement* nameElem = p->FirstChildElement("Data_Name");
                            if (nameElem && nameElem->GetText() && QString(nameElem->GetText()) == fileinfo.baseName()) {
                                dataExists = true;
                                break;
                            }
                        }
                        if (dataExists)
                            continue;

                        dataCount++;
                        TiXmlElement* dataElem = new TiXmlElement("Data");

                        TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                        dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
                        dataElem->LinkEndChild(dataNameNode);

                        TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                        dataRankNode->LinkEndChild(new TiXmlText("complex-0.0"));
                        dataElem->LinkEndChild(dataRankNode);

                        TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                        dataIndexNode->LinkEndChild(new TiXmlText(QString::number(dataCount).toStdString().c_str()));
                        dataElem->LinkEndChild(dataIndexNode);

                        TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                        dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                        dataElem->LinkEndChild(dataPathNode);

                        TiXmlElement* dataParamsElem = new TiXmlElement("Data_Processing_Parameters");
                        TiXmlElement* nillElem = new TiXmlElement("nill");
                        nillElem->LinkEndChild(new TiXmlText("0"));
                        dataParamsElem->LinkEndChild(nillElem);
                        dataElem->LinkEndChild(dataParamsElem);

                        dataNodeElem->LinkEndChild(dataElem);
                        xmlModified = true;
                    }

                    dataNodeElem->SetAttribute("data_count", QString::number(dataCount).toStdString().c_str());
                }
                if (xmlModified) {
                    QString xmlPath = projectPath() + "/" + projectName();
                    xml->XMLFile_save(xmlPath.toStdString().c_str());
                }
            }

            // 刷新左侧树视图
            auto iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                iface->refreshProjectTree();
            }

            return true;
        }
    }

    return false;
}

QStringList S1FrameMergeNode::previewImagePaths() const
{
    QStringList existingPaths;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return existingPaths;

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.h5";
        QStringList h5Files = dir.entryList(filters, QDir::Files);
        for (const QString& h5File : h5Files) {
            QString h5Path = dir.absoluteFilePath(h5File);
            QFileInfo fi(h5Path);
            QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                existingPaths.append(jpgPath);
            }
        }
    }
    return existingPaths;
}

} // namespace QtNodes

