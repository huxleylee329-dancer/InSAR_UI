#include "InSARLogManager.h"

#include "Sentinel1BatchImportNode.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include "FormatConversion.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QSet>

namespace QtNodes {

Sentinel1BatchImportNode::Sentinel1BatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_projectLabel(nullptr)
    , m_manifestPaths()
    , m_importedFilePaths()
    , m_outputNodeName("S1_Batch_Import")
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

Sentinel1BatchImportNode::~Sentinel1BatchImportNode()
{
    if (m_workerThread)
    {
        if (m_thread && m_thread->isRunning())
        {
            m_workerThread->StopProcess();
        }
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

    // Note: m_widget is owned by QtNodes QGraphicsProxyWidget, do not delete here
}

void Sentinel1BatchImportNode::stopExecution()
{
    m_stopRequested = true;
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
    }
}

QWidget* Sentinel1BatchImportNode::createWidget()
{
    auto* widget = new QWidget();
    widget->setFixedWidth(300);
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // Top section: file list (8:2 stretch)
    auto* topSection = new QHBoxLayout();
    topSection->setStretch(0, 8);
    topSection->setStretch(1, 2);

    // Left side: file list widget
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
    topSection->addWidget(m_fileListWidget);

    // Right side: add/remove buttons
    auto* buttonColLayout = new QVBoxLayout();
    buttonColLayout->setContentsMargins(0, 0, 0, 0);
    buttonColLayout->setSpacing(5);
    QPushButton* addFiles = new QPushButton("添加");
    QPushButton* removeFiles = new QPushButton("移除");
    buttonColLayout->addWidget(addFiles);
    buttonColLayout->addWidget(removeFiles);
    topSection->addLayout(buttonColLayout);
    
    mainLayout->addLayout(topSection, 4);

    // Project Name Badge
    m_projectLabel = createProjectBadge(projectName());
    mainLayout->addWidget(m_projectLabel);

    // Bottom section: configuration options
    auto* bottomSection = new QHBoxLayout();
    auto* configLayout = new QVBoxLayout();

    // Subswath row [3:7]
    auto* subswathRow = new QHBoxLayout();
    subswathRow->setStretch(0, 3);
    subswathRow->setStretch(1, 7);
    subswathRow->addWidget(new QLabel("子带选择："));
    m_subswathCombo = new QComboBox();
    m_subswathCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_subswathCombo->addItem("iw1");
    m_subswathCombo->addItem("iw2");
    m_subswathCombo->addItem("iw3");
    connect(m_subswathCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        if (m_subswath != text) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_subswathCombo);
                m_subswathCombo->setCurrentText(m_subswath);
                return;
            }
            m_subswath = text;
            invalidateNodeData();
        }
    });
    subswathRow->addWidget(m_subswathCombo);
    configLayout->addLayout(subswathRow);

    // Polarization row [3:7]
    auto* polRow = new QHBoxLayout();
    polRow->setStretch(0, 3);
    polRow->setStretch(1, 7);
    polRow->addWidget(new QLabel("极化方式："));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_polarizationCombo->addItem("vv");
    m_polarizationCombo->addItem("vh");
    connect(m_polarizationCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        if (m_polarization != text) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_polarizationCombo);
                m_polarizationCombo->setCurrentText(m_polarization);
                return;
            }
            m_polarization = text;
            invalidateNodeData();
        }
    });
    polRow->addWidget(m_polarizationCombo);
    configLayout->addLayout(polRow);


    // Output node name row [3:7]
    auto* nameRow = new QHBoxLayout();
    nameRow->setStretch(0, 3);
    nameRow->setStretch(1, 7);
    nameRow->addWidget(new QLabel("目标节点："));
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
            NodeUtils::removeDataNodeFromProject(getProjectContext(), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    nameRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nameRow);

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onRemoveFilesClicked);

    return widget;
}

bool Sentinel1BatchImportNode::prepareToStart()
{
    // Safety check: Ensure project is open
    auto* model = projectModel();
    QString path = projectPath();
    QString name = projectName();

    if (!model || path.isEmpty() || name.isEmpty())
    {
        onError("未检测到打开的项目，请先打开或新建一个项目。");
        return false;
    }

    if (m_manifestPaths.isEmpty())
    {
        onError("请至少添加一个清单文件。");
        return false;
    }

    for (const QString& manifestPath : m_manifestPaths)
    {
        if (!QFileInfo::exists(manifestPath))
        {
            onError("清单文件不存在：" + manifestPath);
            return false;
        }
    }

    m_preparedOriginalNameList.clear();
    m_preparedImportNameList.clear();

    for (const QString& manifestPath : m_manifestPaths)
    {
        QString importName = generateImportName(manifestPath);
        if (importName.isEmpty())
        {
            onError("无法从清单文件提取日期：" + manifestPath);
            return false;
        }
        m_preparedOriginalNameList.push_back(manifestPath);
        m_preparedImportNameList.push_back(importName);
    }

    m_preparedOutputNodeName = getOutputNodeName();

    QStringList pathsToCheck;
    for (const QString& importName : m_preparedImportNameList) {
        pathsToCheck.append(projectPath() + "/" + m_preparedOutputNodeName + "/" + importName + ".h5");
        pathsToCheck.append(projectPath() + "/" + m_preparedOutputNodeName + "/" + importName + ".jpg");
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(getProjectContext(), m_preparedOutputNodeName, pathsToCheck, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void Sentinel1BatchImportNode::executeImport()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        setProgress(100);
        onImportFinished();
        return;
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite) {
        // 清理旧数据，防止更换文件重新执行时导致历史记录累积
        NodeUtils::removeDataNodeFromProject(getProjectContext(), m_preparedOutputNodeName);
    }

    m_thread = new QThread(this);
    m_workerThread = new Sentinel1ImportWorker();
    m_workerThread->moveToThread(m_thread);

    connect(m_workerThread, &Sentinel1ImportWorker::updateProcess,
            this, &Sentinel1BatchImportNode::onImportProgress);
    connect(m_workerThread, &Sentinel1ImportWorker::endProcess,
            this, &Sentinel1BatchImportNode::onImportFinished);
    connect(m_workerThread, &Sentinel1ImportWorker::errorProcess,
            this, &Sentinel1BatchImportNode::onThreadError);
    connect(m_workerThread, &Sentinel1ImportWorker::sendModel,
            this, &Sentinel1BatchImportNode::onModelUpdated);

    m_thread->start();

    QString subswath = m_subswathCombo->currentText();
    QString pol = m_polarizationCombo->currentText();

    // 构造 ImportTask 列表
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < m_preparedOriginalNameList.size(); ++i) {
        ImportTask task;
        task.filename = m_preparedImportNameList[i];
        task.arguments = QStringList{ m_preparedOriginalNameList[i], subswath, pol };
        tasks.push_back(task);
    }

    QMetaObject::invokeMethod(m_workerThread, "import_patch",
        Q_ARG(QString, projectPath()),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, m_preparedOutputNodeName),
        Q_ARG(QString, projectName()),
        Q_ARG(QStandardItemModel*, projectModel()));
}

QStringList Sentinel1BatchImportNode::getImportedFilePaths() const
{
    return m_importedFilePaths;
}

QString Sentinel1BatchImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "S1_Batch_Import";
    }
    return name;
}

QStringList Sentinel1BatchImportNode::previewImagePaths() const
{
    QStringList existingPaths;
    for (const QString& h5Path : m_importedFilePaths) {
        if (QFileInfo::exists(h5Path)) {
            QFileInfo fi(h5Path);
            QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
            
            // 自愈已被移至 validateAndRestoreOutput 中异步执行
            
            if (QFileInfo::exists(jpgPath)) {
                existingPaths << jpgPath;
            }
        }
    }
    return existingPaths;
}

QString Sentinel1BatchImportNode::generateImportName(const QString& manifestPath) const
{
    QFileInfo fileInfo(manifestPath);
    QString dirName = fileInfo.dir().dirName();

    QRegularExpression dateRegex(R"(\d{8})");
    QRegularExpressionMatch match = dateRegex.match(dirName);
    if (match.hasMatch())
    {
        QString date = match.captured(0);
        QString subswath = m_subswathCombo->currentText();
        QString pol = m_polarizationCombo->currentText();
        return QString("%1_%2%3").arg(date).arg(subswath).arg(pol);
    }
    return QString();
}

void Sentinel1BatchImportNode::updateAvailableParameters()
{
    if (m_manifestPaths.isEmpty())
        return;

    QString manifestPath = m_manifestPaths.first();
    if (manifestPath.isEmpty() || !QFileInfo::exists(manifestPath))
        return;

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    QSet<QString> subswaths;
    QSet<QString> polarizations;

    QRegularExpression rx(R"(s1[ab]-(iw[1-3])-slc-(vv|vh|hh|hv)-)");
    QRegularExpressionMatchIterator i = rx.globalMatch(content);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        subswaths.insert(match.captured(1).toLower());
        polarizations.insert(match.captured(2).toLower());
    }

    if (subswaths.isEmpty() || polarizations.isEmpty())
        return;

    // Update subswath combo
    if (m_subswathCombo) {
        QSignalBlocker blocker(m_subswathCombo);
        QString currentSub = m_subswathCombo->currentText();
        m_subswathCombo->clear();
        QStringList subList = subswaths.values();
        subList.sort();
        m_subswathCombo->addItems(subList);
        int subIndex = m_subswathCombo->findText(currentSub);
        if (subIndex >= 0) m_subswathCombo->setCurrentIndex(subIndex);
        else m_subswath = m_subswathCombo->currentText();
    }

    // Update polarization combo
    if (m_polarizationCombo) {
        QSignalBlocker blocker(m_polarizationCombo);
        QString currentPol = m_polarizationCombo->currentText();
        m_polarizationCombo->clear();
        QStringList polList = polarizations.values();
        polList.sort();
        m_polarizationCombo->addItems(polList);
        int polIndex = m_polarizationCombo->findText(currentPol);
        if (polIndex >= 0) m_polarizationCombo->setCurrentIndex(polIndex);
        else m_polarization = m_polarizationCombo->currentText();
    }
}

void Sentinel1BatchImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        nullptr,
        tr("选择哨兵一号清单文件"),
        QDir::currentPath(),
        tr("清单文件 (manifest.safe);;所有文件 (*)")
    );

    bool added = false;
    for (const QString& file : files)
    {
        if (!m_manifestPaths.contains(file))
        {
            m_manifestPaths.append(file);
            QFileInfo fi(file);
            m_fileListWidget->addItem(fi.dir().dirName() + "/" + fi.fileName());
            added = true;
            
            int outCount = nPorts(PortType::Out);
            for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
            invalidateExecution();
        }
    }

    if (added)
    {
        updateAvailableParameters();
    }
}

void Sentinel1BatchImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    bool changed = false;

    for (QListWidgetItem* item : selectedItems)
    {
        int row = m_fileListWidget->row(item);
        if (row < 0 || row >= m_manifestPaths.size())
            continue;

        QString manifestPath = m_manifestPaths.at(row);
        QString importName = generateImportName(manifestPath);

        if (!importName.isEmpty()) {
            QString importedPath = QString("%1/%2/%3.h5")
                .arg(projectPath())
                .arg(getOutputNodeName())
                .arg(importName);

            QString previewPath = QString("%1/%2/%3.jpg")
                .arg(projectPath())
                .arg(getOutputNodeName())
                .arg(importName);

            QStandardItemModel* model = projectModel();
            if (model && !projectPath().isEmpty() && !projectName().isEmpty()) {
                QList<QStandardItem*> projItems = model->findItems(projectName());
                if (!projItems.isEmpty()) {
                    QStandardItem* projItem = projItems.first();
                    for (int i = 0; i < projItem->rowCount(); ++i) {
                        QStandardItem* nodeItem = projItem->child(i);
                        if (nodeItem && nodeItem->text() == getOutputNodeName()) {
                            for (int j = 0; j < nodeItem->rowCount(); ++j) {
                                QStandardItem* pathItem = nodeItem->child(j, 1);
                                if (pathItem && pathItem->text() == importedPath) {
                                    QStandardItem* fileItem = nodeItem->child(j, 0);
                                    QString fileName = fileItem ? fileItem->text() : "";

                                    XMLFile xml;
                                    QString xmlPath = projectPath() + "/" + projectName();
                                    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) >= 0) {
                                        xml.XMLFile_remove_node(getOutputNodeName().toStdString().c_str(),
                                                              fileName.toStdString().c_str(),
                                                              importedPath.toStdString().c_str());
                                        xml.XMLFile_save(xmlPath.toStdString().c_str());
                                    }

                                    if (QFile::exists(importedPath)) {
                                        QFile::remove(importedPath);
                                    }
                                    if (QFile::exists(previewPath)) {
                                        QFile::remove(previewPath);
                                    }

                                    nodeItem->removeRow(j);

                                    if (auto* iface = getProjectContext()) {
                                        iface->refreshProjectTree();
                                    }
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
            }

            // Also clean up from m_importedFilePaths if it is present
            m_importedFilePaths.removeAll(importedPath);
        }

        m_manifestPaths.removeAt(row);
        delete item;
        changed = true;
    }

    if (changed)
    {
        updateAvailableParameters();

        if (!m_importedFilePaths.isEmpty()) {
            QStringList jpgPaths;
            for (const QString& h5Path : m_importedFilePaths) {
                QFileInfo fi(h5Path);
                QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
                jpgPaths.append(jpgPath);
            }
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);

            auto outputData = std::make_shared<ImportedFileData>(m_importedFilePaths, getOutputNodeName());
            setOutputData(0, outputData);
            Q_EMIT dataUpdated(0);
        } else {
            m_imageInfoData.reset();
            setOutputData(1, nullptr);
            Q_EMIT dataUpdated(1);

            setOutputData(0, nullptr);
            Q_EMIT dataUpdated(0);
        }

        invalidateExecution();
    }
}

void Sentinel1BatchImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void Sentinel1BatchImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();

    m_importedFilePaths.clear();
    for (int i = 0; i < m_manifestPaths.size(); ++i) {
        QString importName = generateImportName(m_manifestPaths[i]);
        if (!importName.isEmpty()) {
            QString filePath = QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(importName);
            m_importedFilePaths.append(filePath);
        }
    }
    ImportNodeBase::onImportFinished();

    // 双路输出：Port 1 预览输出
    if (!m_importedFilePaths.isEmpty())
    {
        QStringList jpgPaths;
        for (const QString& h5Path : m_importedFilePaths) {
            QFileInfo fi(h5Path);
            QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
            jpgPaths.append(jpgPath);
        }
        m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
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
}

void Sentinel1BatchImportNode::onThreadError(const QString& error)
{
    onError(error);

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
}

void Sentinel1BatchImportNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ImportNodeBase::setExecutionMode(mode);
}

void Sentinel1BatchImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
    }
}

QJsonObject Sentinel1BatchImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_manifestPaths)
        pathsArray.append(path);
    json["manifestPaths"] = pathsArray;
    json["subswath"] = m_subswathCombo ? m_subswathCombo->currentText() : m_subswath;
    json["polarization"] = m_polarizationCombo ? m_polarizationCombo->currentText() : m_polarization;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    return json;
}

void Sentinel1BatchImportNode::load(QJsonObject const &json)
{
    // 先赋值字段
    m_manifestPaths.clear();
    QJsonArray pathsArray = json["manifestPaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_manifestPaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("S1_Batch_Import");
    if (m_outputNodeName.isEmpty()) {
        m_outputNodeName = "S1_Batch_Import";
    }
    m_subswath = json["subswath"].toString("iw1");
    if (m_subswath.isEmpty()) {
        m_subswath = "iw1";
    }
    m_polarization = json["polarization"].toString("vv");
    if (m_polarization.isEmpty()) {
        m_polarization = "vv";
    }

    // 同步 UI 控件到最新反序列化的值，防止基类 load() 触发的 validateAndRestoreOutput() 读到旧的 UI 控件值
    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_manifestPaths) {
            QFileInfo fi(path);
            m_fileListWidget->addItem(fi.dir().dirName() + "/" + fi.fileName());
        }
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    if (m_subswathCombo) {
        int idx = m_subswathCombo->findText(m_subswath);
        if (idx >= 0) m_subswathCombo->setCurrentIndex(idx);
    }

    if (m_polarizationCombo) {
        int idx = m_polarizationCombo->findText(m_polarization);
        if (idx >= 0) m_polarizationCombo->setCurrentIndex(idx);
    }

    ExecutableNodeDelegateModel::load(json);

    updateAvailableParameters();
}

bool Sentinel1BatchImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/";
    QDir dir(outputPath);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0) {
        m_importedFilePaths.clear();
        for (const QString &manifestPath : m_manifestPaths) {
            QString importName = generateImportName(manifestPath);
            QString importedPath = outputPath + importName + ".h5";
            bool exists = QFile::exists(importedPath);
            if (exists) {
                m_importedFilePaths.append(importedPath);
            }
        }
        if (!m_importedFilePaths.isEmpty()) {
            auto outputData = std::make_shared<ImportedFileData>(m_importedFilePaths, nodeName);
            setOutputData(0, outputData);
            Q_EMIT dataUpdated(0);

            // 双路输出：Port 1 预览输出
            QStringList missingH5s;
            QStringList missingJpgs;
            QStringList allJpgPaths;

            for (const QString& h5Path : m_importedFilePaths) {
                QFileInfo fi(h5Path);
                QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
                allJpgPaths.append(jpgPath);
                
                if (!QFileInfo::exists(jpgPath)) {
                    missingH5s.append(h5Path);
                    missingJpgs.append(jpgPath);
                }
            }

            if (!missingH5s.isEmpty()) {
                m_remedyWatcher.cancel();
                m_remedyWatcher.waitForFinished();
                m_remedyWatcher.disconnect();

                connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, allJpgPaths]() {
                    m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
                    setOutputData(1, m_imageInfoData);
                    Q_EMIT dataUpdated(1);
                });

                QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
                    for (int i = 0; i < missingH5s.size(); ++i) {
                        NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
                    }
                });
                m_remedyWatcher.setFuture(future);
            } else {
                m_imageInfoData = std::make_shared<ImageInfoData>(allJpgPaths);
                setOutputData(1, m_imageInfoData);
                Q_EMIT dataUpdated(1);
            }

            return true;
        }
    }

    return false;
}

unsigned int Sentinel1BatchImportNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 0;
    else
        return 2;
}

NodeDataType Sentinel1BatchImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported Files"};
        else if (portIndex == 1)
            return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool Sentinel1BatchImportNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString Sentinel1BatchImportNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return tr("成果 *");
        else if (portIndex == 1)
            return tr("预览 ?");
    }
    return QString();
}

bool Sentinel1BatchImportNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> Sentinel1BatchImportNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

} // namespace QtNodes
