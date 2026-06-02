#include "InSARLogManager.h"
#include "S1SwathMergeNode.h"
#include "S1SwathMergeWorker.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InterfaceManager.h"
#include "NodeUtils.h"
#include "tinyxml.h"
#include "icon_source.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace QtNodes {

S1SwathMergeNode::S1SwathMergeNode()
    : ExecutableNodeDelegateModel()
    , m_projectCombo(nullptr)
    , m_dataNodeCombo{nullptr, nullptr, nullptr}
    , m_indexSpins{nullptr, nullptr, nullptr}
    , m_outputNodeNameEdit(nullptr)
    , m_inputs{nullptr, nullptr, nullptr}
    , m_outputData(nullptr)
    , m_imageInfoData(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
}

S1SwathMergeNode::~S1SwathMergeNode()
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

unsigned int S1SwathMergeNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 3;  // Three input ports (IW1, IW2, IW3)
    else
        return 2;  // Two output ports: 0 result, 1 preview
}

NodeDataType S1SwathMergeNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "S1 Swath Data"};

    if (portIndex == 0)
        return NodeDataType{"imported_file", "S1 Merged Swath"};
    else
        return NodeDataType{"image_info", "Image Info"};
}

std::shared_ptr<NodeData> S1SwathMergeNode::outData(PortIndex port)
{
    if (port == 0)
        return m_outputData;
    else
        return m_imageInfoData;
}

bool S1SwathMergeNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return portType == PortType::Out;
}

QString S1SwathMergeNode::portCaption(PortType portType, PortIndex portIndex) const
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

bool S1SwathMergeNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void S1SwathMergeNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port >= 0 && port < 3)
    {
        m_inputs[port] = std::dynamic_pointer_cast<ImportedFileData>(data);
        updateLabels();

        // Generate default output name if all inputs connected and name not set
        if (m_inputs[0] && m_inputs[1] && m_inputs[2] && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeName = generateDefaultOutputName();
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    // Delegate to base class to handle execution mode
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* S1SwathMergeNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject S1SwathMergeNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    QString nodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["outputNodeName"] = nodeName;

    modelJson["index1"] = m_index1;
    modelJson["index2"] = m_index2;
    modelJson["index3"] = m_index3;

    return modelJson;
}

void S1SwathMergeNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load() (会调用 validateAndRestoreOutput)
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined())
    {
        m_outputNodeName = vName.toString();
    }

    QJsonValue v1 = json["index1"];
    if (!v1.isUndefined())
    {
        m_index1 = v1.toInt();
    }

    QJsonValue v2 = json["index2"];
    if (!v2.isUndefined())
    {
        m_index2 = v2.toInt();
    }

    QJsonValue v3 = json["index3"];
    if (!v3.isUndefined())
    {
        m_index3 = v3.toInt();
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    if (m_indexSpins[0])
        m_indexSpins[0]->setValue(m_index1);
    if (m_indexSpins[1])
        m_indexSpins[1]->setValue(m_index2);
    if (m_indexSpins[2])
        m_indexSpins[2]->setValue(m_index3);
}

void S1SwathMergeNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // 严格尺寸规范，防膨胀 Bug
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // 选择工程
    auto* projectLayout = new QHBoxLayout();
    QLabel* projectLabel = new QLabel("选择工程");
    projectLabel->setFixedWidth(90); // 标签固定宽度，组件完美对齐
    projectLayout->addWidget(projectLabel);
    m_projectCombo = new QComboBox();
    m_projectCombo->setEditable(false);
    m_projectCombo->addItem(projectName().isEmpty() ? "未打开项目" : projectName());
    projectLayout->addWidget(m_projectCombo);
    layout->addLayout(projectLayout);

    // 数据节点 IW1
    auto* dataNode1Layout = new QHBoxLayout();
    QLabel* dataNode1Label = new QLabel("IW1 数据节点");
    dataNode1Label->setFixedWidth(90);
    dataNode1Layout->addWidget(dataNode1Label);
    m_dataNodeCombo[0] = new QComboBox();
    m_dataNodeCombo[0]->setEditable(false);
    m_dataNodeCombo[0]->addItem("等待输入");
    dataNode1Layout->addWidget(m_dataNodeCombo[0]);
    layout->addLayout(dataNode1Layout);

    // Image Index 1
    auto* index1Layout = new QHBoxLayout();
    QLabel* index1Label = new QLabel("Image Index 1:");
    index1Label->setFixedWidth(90);
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

    // 数据节点 IW2
    auto* dataNode2Layout = new QHBoxLayout();
    QLabel* dataNode2Label = new QLabel("IW2 数据节点");
    dataNode2Label->setFixedWidth(90);
    dataNode2Layout->addWidget(dataNode2Label);
    m_dataNodeCombo[1] = new QComboBox();
    m_dataNodeCombo[1]->setEditable(false);
    m_dataNodeCombo[1]->addItem("等待输入");
    dataNode2Layout->addWidget(m_dataNodeCombo[1]);
    layout->addLayout(dataNode2Layout);

    // Image Index 2
    auto* index2Layout = new QHBoxLayout();
    QLabel* index2Label = new QLabel("Image Index 2:");
    index2Label->setFixedWidth(90);
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

    // 数据节点 IW3
    auto* dataNode3Layout = new QHBoxLayout();
    QLabel* dataNode3Label = new QLabel("IW3 数据节点");
    dataNode3Label->setFixedWidth(90);
    dataNode3Layout->addWidget(dataNode3Label);
    m_dataNodeCombo[2] = new QComboBox();
    m_dataNodeCombo[2]->setEditable(false);
    m_dataNodeCombo[2]->addItem("等待输入");
    dataNode3Layout->addWidget(m_dataNodeCombo[2]);
    layout->addLayout(dataNode3Layout);

    // Image Index 3
    auto* index3Layout = new QHBoxLayout();
    QLabel* index3Label = new QLabel("Image Index 3:");
    index3Label->setFixedWidth(90);
    index3Layout->addWidget(index3Label);
    m_indexSpins[2] = new QSpinBox();
    m_indexSpins[2]->setMinimum(1);
    m_indexSpins[2]->setMaximum(100);
    m_indexSpins[2]->setValue(m_index3);
    connect(m_indexSpins[2], &QSpinBox::editingFinished, this, [this, invalidateNodeData]() {
        int val = m_indexSpins[2]->value();
        if (m_index3 != val) {
            if (!confirmParameterChange()) {
                m_indexSpins[2]->setValue(m_index3);
                return;
            }
            m_index3 = val;
            invalidateNodeData();
        }
    });
    index3Layout->addWidget(m_indexSpins[2]);
    layout->addLayout(index3Layout);

    // 目标节点名
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLabel->setFixedWidth(90);
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入")); // 规范占位符文字
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

    // 尾部自愈刷新
    updateLabels();
}

void S1SwathMergeNode::updateLabels()
{
    // Update project combo
    if (m_projectCombo)
    {
        QString projName = projectName();
        if (!projName.isEmpty())
        {
            if (m_projectCombo->count() == 0 || m_projectCombo->itemText(0) != projName)
            {
                m_projectCombo->clear();
                m_projectCombo->addItem(projName);
            }
        }
    }

    // Update data node combos
    for (int i = 0; i < 3; ++i)
    {
        if (m_dataNodeCombo[i])
        {
            m_dataNodeCombo[i]->clear();
            if (m_inputs[i])
            {
                QString nodeName = m_inputs[i]->nodeName();
                m_dataNodeCombo[i]->addItem(nodeName);
            }
            else
            {
                m_dataNodeCombo[i]->addItem("等待输入");
            }
        }
    }
}

QString S1SwathMergeNode::generateDefaultOutputName() const
{
    if (m_inputs[0] && m_inputs[1] && m_inputs[2])
    {
        QString node1 = m_inputs[0]->nodeName();
        QString node2 = m_inputs[1]->nodeName();
        QString node3 = m_inputs[2]->nodeName();
        return node1 + "_" + node2 + "_" + node3 + "_Merged";
    }
    return "SwathMerge_Output";
}

bool S1SwathMergeNode::validateInputs() const
{
    if (!m_inputs[0] || !m_inputs[1] || !m_inputs[2])
    {
        return false;
    }

    QString node1 = m_inputs[0]->nodeName();
    QString node2 = m_inputs[1]->nodeName();
    QString node3 = m_inputs[2]->nodeName();
    if (node1.isEmpty() || node2.isEmpty() || node3.isEmpty())
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

void S1SwathMergeNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void S1SwathMergeNode::onProcessingFinished()
{
    // Clean up thread
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (m_worker)
    {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    // Set output node name to the final result name
    m_outputNodeName = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();

    // Call validateAndRestoreOutput to perform native XML self-healing/saving and output port updating
    validateAndRestoreOutput();

    // Update UI
    m_outputNodeNameEdit->setEnabled(true);

    // Notify base class that we're finished
    setState(ExecutionState::Completed);
    setProgress(100);
    InSARLogManager::LogInfo("S1SwathMergeNode", "executeProcessing completed.");
    finishExecution();
}


void S1SwathMergeNode::onError(const QString& error)
{
    // Clean up thread
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (m_worker)
    {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Error);
}

void S1SwathMergeNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

QStandardItemModel* S1SwathMergeNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString S1SwathMergeNode::projectPath() const
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

QString S1SwathMergeNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* S1SwathMergeNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void S1SwathMergeNode::execute()
{
    executeProcessing();
}

void S1SwathMergeNode::stopExecution()
{
    if (m_worker)
    {
        // worker should be stopped if there is a stop method, but worker relies on thread interruption
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void S1SwathMergeNode::processAutomatically()
{
    // In automatic mode, if inputs are valid, execute
    if (validateInputs())
    {
        executeProcessing();
    }
}

void S1SwathMergeNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void S1SwathMergeNode::executeProcessing()
{
    InSARLogManager::LogInfo("S1SwathMergeNode", "executeProcessing started.");
    if (!validateInputs())
        return;

    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    QString outputPath = projectPath() + "/" + dstNode + "/";

    QStringList pathsToCheck;
    pathsToCheck.append(outputPath + "merged_phase.h5");
    pathsToCheck.append(outputPath + "merged_phase.jpg");

    // 覆盖/复用安全拦截检测
    auto ctx = NodeUtils::getProjectContext(_widget);
    NodeUtils::OverwriteResult overwriteResult = NodeUtils::checkAndPromptOverwrite(ctx, dstNode, pathsToCheck);
    if (overwriteResult == NodeUtils::OverwriteResult::Cancel)
    {
        setState(ExecutionState::Idle);
        return;
    }
    else if (overwriteResult == NodeUtils::OverwriteResult::LoadExisting)
    {
        m_outputNodeName = dstNode;
        if (validateAndRestoreOutput())
        {
            setState(ExecutionState::Completed);
            setProgress(100);
            finishExecution();
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
            return;
        }
        else
        {
            setState(ExecutionState::Error);
            return;
        }
    }
    else if (overwriteResult == NodeUtils::OverwriteResult::Overwrite)
    {
        QDir outputDir(outputPath);
        if (outputDir.exists())
        {
            QFileInfoList entries = outputDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
            for (const QFileInfo& entry : entries)
            {
                if (entry.isDir())
                {
                    QDir(entry.absoluteFilePath()).removeRecursively();
                }
                else
                {
                    QFile::remove(entry.absoluteFilePath());
                }
            }
        }
    }

    setProgress(0);
    setState(ExecutionState::Running);

    // Prepare processing
    int index1 = m_index1;
    int index2 = m_index2;
    int index3 = m_index3;
    QString project = projectName();
    QString node1 = m_inputs[0]->nodeName();
    QString node2 = m_inputs[1]->nodeName();
    QString node3 = m_inputs[2]->nodeName();

    // Create thread
    m_thread = new QThread();
    m_worker = new S1SwathMergeWorker();
    m_worker->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1SwathMergeNode::startSwathMerge, m_worker, &S1SwathMergeWorker::S1_swath_merge);
    connect(m_thread, &QThread::started, [this, index1, index2, index3, project, node1, node2, node3, dstNode]() {
        Q_EMIT startSwathMerge(index1, index2, index3, project, node1, node2, node3, dstNode, projectModel());
    });
    connect(m_worker, &S1SwathMergeWorker::updateProcess, this, &S1SwathMergeNode::onProgressUpdate);
    connect(m_worker, &S1SwathMergeWorker::endProcess, this, &S1SwathMergeNode::onProcessingFinished);
    connect(m_worker, &S1SwathMergeWorker::errorProcess, this, &S1SwathMergeNode::onError);
    connect(m_worker, &S1SwathMergeWorker::sendModel, this, &S1SwathMergeNode::onModelUpdated);
    connect(m_worker, &QObject::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
    m_thread->start();
    m_outputNodeNameEdit->setEnabled(false);

    // 在下一个事件循环中强行将状态重置为 Running，防止基类 setInData 在 Automatic 模式下将其强行设为 Idle
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

bool S1SwathMergeNode::validateAndRestoreOutput()
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
            m_outputData = std::make_shared<ImportedFileData>(outputPath, dstNode);
            setOutputData(0, m_outputData);
            Q_EMIT dataUpdated(0);

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
                Q_EMIT dataUpdated(1);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
                Q_EMIT dataUpdated(1);
            }

            // 异步补齐 JPG 预览图 (SOP 避坑经验 #7)
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
                        NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "phase");
                    }
                });
                m_remedyWatcher.setFuture(future);
            }

            // 恢复左侧树标准项目模型
            QStandardItemModel* projModelPtr = projectModel();
            if (projModelPtr) {
                QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
                if (!foundProjects.isEmpty()) {
                    QStandardItem* projectItem = foundProjects.first();

                    // Find or create the swath_merge root item
                    QStandardItem* swathMergeItem = nullptr;
                    for (int i = 0; i < projectItem->rowCount(); i++) {
                        if (projectItem->child(i, 0)->text() == dstNode) {
                            swathMergeItem = projectItem->child(i, 0);
                            break;
                        }
                    }

                    if (!swathMergeItem) {
                        swathMergeItem = new QStandardItem(dstNode);
                        swathMergeItem->setToolTip(projectName());
                        int insert = 0;
                        for (; insert < projectItem->rowCount(); insert++) {
                            if (projectItem->child(insert, 1)->text().compare("phase-1.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-2.0") == 0)
                                continue;
                            else
                                break;
                        }
                        swathMergeItem->setIcon(QIcon(FOLDER_ICON));
                        projectItem->insertRow(insert, swathMergeItem);
                        QStandardItem* swathMergeRank = new QStandardItem("phase-1.0");
                        projectItem->setChild(insert, 1, swathMergeRank);
                    }

                    // Complete child image nodes
                    for (const QString& h5File : h5Files) {
                        QString h5Path = dir.absoluteFilePath(h5File);
                        QFileInfo fileinfo(h5Path);
                        QString filename = fileinfo.baseName();

                        QStandardItem* item_img = nullptr;
                        for (int j = 0; j < swathMergeItem->rowCount(); j++) {
                            if (swathMergeItem->child(j, 0)->text() == filename) {
                                item_img = swathMergeItem->child(j, 0);
                                break;
                            }
                        }

                        if (!item_img) {
                            QStandardItem* img_name = new QStandardItem(filename);
                            img_name->setToolTip("phase");
                            QStandardItem* img_path = new QStandardItem(h5Path);
                            img_name->setIcon(QIcon(IMAGEDATA_ICON));
                            swathMergeItem->appendRow(img_name);
                            swathMergeItem->setChild(swathMergeItem->rowCount() - 1, 1, img_path);
                        } else {
                            swathMergeItem->setChild(item_img->row(), 1, new QStandardItem(h5Path));
                        }
                    }
                }
            }

            // XML 工程文件自愈修复 (EXE端原生 TinyXML 实现，绕过 DLL)
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
                        dataNodeElem->SetAttribute("data_processing", "interferometric_formation");
                        dataNodeElem->SetAttribute("rank", "phase-1.0");

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
                                             strcmp(rankAttr, "complex-2.0") == 0 ||
                                             strcmp(rankAttr, "phase-1.0") == 0)) {
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
                        dataRankNode->LinkEndChild(new TiXmlText("phase-1.0"));
                        dataElem->LinkEndChild(dataRankNode);

                        TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                        dataIndexNode->LinkEndChild(new TiXmlText(QString::number(dataCount).toStdString().c_str()));
                        dataElem->LinkEndChild(dataIndexNode);

                        TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                        dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                        dataElem->LinkEndChild(dataPathNode);

                        TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
                        rowOffsetNode->LinkEndChild(new TiXmlText("0"));
                        dataElem->LinkEndChild(rowOffsetNode);

                        TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
                        colOffsetNode->LinkEndChild(new TiXmlText("0"));
                        dataElem->LinkEndChild(colOffsetNode);

                        dataNodeElem->LinkEndChild(dataElem);
                        xmlModified = true;
                    }

                    TiXmlElement* paramsElem = dataNodeElem->FirstChildElement("Data_Processing_Parameters");
                    if (!paramsElem) {
                        paramsElem = new TiXmlElement("Data_Processing_Parameters");
                        dataNodeElem->LinkEndChild(paramsElem);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("master_image")) {
                        TiXmlElement* masterElem = new TiXmlElement("master_image");
                        masterElem->LinkEndChild(new TiXmlText("unknown"));
                        paramsElem->LinkEndChild(masterElem);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("IsDeflat")) {
                        TiXmlElement* deflatElem = new TiXmlElement("IsDeflat");
                        deflatElem->LinkEndChild(new TiXmlText("0"));
                        paramsElem->LinkEndChild(deflatElem);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("IsTopo_Removal")) {
                        TiXmlElement* topoElem = new TiXmlElement("IsTopo_Removal");
                        topoElem->LinkEndChild(new TiXmlText("0"));
                        paramsElem->LinkEndChild(topoElem);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("IsCoherence")) {
                        TiXmlElement* cohElem = new TiXmlElement("IsCoherence");
                        cohElem->LinkEndChild(new TiXmlText("0"));
                        paramsElem->LinkEndChild(cohElem);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("coh_est_width")) {
                        TiXmlElement* cohW = new TiXmlElement("coh_est_width");
                        cohW->LinkEndChild(new TiXmlText("0"));
                        paramsElem->LinkEndChild(cohW);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("coh_est_height")) {
                        TiXmlElement* cohH = new TiXmlElement("coh_est_height");
                        cohH->LinkEndChild(new TiXmlText("0"));
                        paramsElem->LinkEndChild(cohH);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("multilook_rg")) {
                        TiXmlElement* mlRg = new TiXmlElement("multilook_rg");
                        mlRg->LinkEndChild(new TiXmlText("0"));
                        paramsElem->LinkEndChild(mlRg);
                        xmlModified = true;
                    }
                    if (!paramsElem->FirstChildElement("multilook_az")) {
                        TiXmlElement* mlAz = new TiXmlElement("multilook_az");
                        mlAz->LinkEndChild(new TiXmlText("0"));
                        paramsElem->LinkEndChild(mlAz);
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

QStringList S1SwathMergeNode::previewImagePaths() const
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
