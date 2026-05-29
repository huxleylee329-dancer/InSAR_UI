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
    , m_projectCombo(nullptr)
    , m_manifestPaths()
    , m_importedFilePaths()
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

    // Project Row [3:7]
    auto* projectRow = new QHBoxLayout();
    projectRow->setStretch(0, 3);
    projectRow->setStretch(1, 7);
    projectRow->addWidget(new QLabel("项目名称："));
    m_projectCombo = new QComboBox();
    m_projectCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_projectCombo->setEditable(false);

    // Populate project list from model (Align with Workspace behavior)
    QStandardItemModel* model = projectModel();
    if (model && model->rowCount() > 0) {
        for (int i = 0; i < model->rowCount(); ++i) {
            auto item = model->item(i, 0);
            if (item) {
                m_projectCombo->addItem(item->text());
            }
        }
        // Set current project as default selection
        int index = m_projectCombo->findText(projectName());
        if (index >= 0) m_projectCombo->setCurrentIndex(index);
    } else {
        m_projectCombo->addItem("未打开项目");
    }

    projectRow->addWidget(m_projectCombo);
    configLayout->addLayout(projectRow);

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

void Sentinel1BatchImportNode::executeImport()
{
    // Safety check: Ensure project is open
    auto* model = projectModel();
    QString path = projectPath();
    QString name = projectName();

    if (!model || path.isEmpty() || name.isEmpty())
    {
        onError("未检测到打开的项目，请先打开或新建一个项目。");
        return;
    }

    if (m_manifestPaths.isEmpty())
    {
        onError("请至少添加一个清单文件。");
        return;
    }

    for (const QString& manifestPath : m_manifestPaths)
    {
        if (!QFileInfo::exists(manifestPath))
        {
            onError("清单文件不存在：" + manifestPath);
            return;
        }
    }

    std::vector<QString> originalNameList;
    std::vector<QString> importNameList;

    for (const QString& manifestPath : m_manifestPaths)
    {
        QString importName = generateImportName(manifestPath);
        if (importName.isEmpty())
        {
            onError("无法从清单文件提取日期：" + manifestPath);
            return;
        }
        originalNameList.push_back(manifestPath);
        importNameList.push_back(importName);
    }

    QString outputNodeName = getOutputNodeName();

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &Sentinel1BatchImportNode::startBatchImport,
            m_workerThread, &MyThread::import_sentinel_patch);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &Sentinel1BatchImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &Sentinel1BatchImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &Sentinel1BatchImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &Sentinel1BatchImportNode::onModelUpdated);

    m_thread->start();

    QString subswath = m_subswathCombo->currentText();
    QString pol = m_polarizationCombo->currentText();

    Q_EMIT startBatchImport(
        originalNameList,
        importNameList,
        subswath,
        pol,
        projectPath(),
        outputNodeName,
        projectName(),
        projectModel()
    );
}

QString Sentinel1BatchImportNode::getImportedFilePath() const
{
    if (!m_importedFilePaths.isEmpty())
    {
        return m_importedFilePaths.first();
    }

    QString outputNodeName = getOutputNodeName();
    return QString("%1/%2/").arg(projectPath()).arg(outputNodeName);
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
            
            // 自愈：如果 JPG 丢失了，但 H5 还存在，则静默重建
            if (!QFileInfo::exists(jpgPath)) {
                NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "complex");
            }
            
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
    QString fileName = fileInfo.fileName();

    QRegularExpression dateRegex(R"(\d{8})");
    QRegularExpressionMatch match = dateRegex.match(fileName);
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
            m_fileListWidget->addItem(QFileInfo(file).fileName());
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

            QString outputPath = projectPath() + "/" + getOutputNodeName() + "/";
            auto outputData = std::make_shared<ImportedFileData>(outputPath, getOutputNodeName());
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
    json["subswath"] = m_subswath;
    json["polarization"] = m_polarization;
    json["outputNodeName"] = m_outputNodeName;
    return json;
}

void Sentinel1BatchImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_manifestPaths.clear();
    QJsonArray pathsArray = json["manifestPaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_manifestPaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("S1_Batch_Import");

    ExecutableNodeDelegateModel::load(json);

    updateAvailableParameters();

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_manifestPaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);

    m_subswath = json["subswath"].toString("iw1");
    if (m_subswathCombo) {
        int idx = m_subswathCombo->findText(m_subswath);
        if (idx >= 0) m_subswathCombo->setCurrentIndex(idx);
    }

    m_polarization = json["polarization"].toString("vv");
    if (m_polarizationCombo) {
        int idx = m_polarizationCombo->findText(m_polarization);
        if (idx >= 0) m_polarizationCombo->setCurrentIndex(idx);
    }
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
            QFileInfo fi(manifestPath);
            QString importedPath = outputPath + fi.completeBaseName() + ".h5";
            if (QFile::exists(importedPath)) {
                m_importedFilePaths.append(importedPath);
            }
        }
        if (!m_importedFilePaths.isEmpty()) {
            auto outputData = std::make_shared<ImportedFileData>(outputPath, nodeName);
            setOutputData(0, outputData);
            Q_EMIT dataUpdated(0);

            // 双路输出：Port 1 预览输出
            QStringList jpgPaths;
            for (const QString& h5Path : m_importedFilePaths) {
                QFileInfo fi(h5Path);
                QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
                jpgPaths.append(jpgPath);
            }
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);

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
            return NodeDataType{"imported_file", "Imported File"};
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
    if (port == 0)
    {
        return ImportNodeBase::outData(0);
    }
    else if (port == 1)
    {
        return m_imageInfoData;
    }
    return nullptr;
}

} // namespace QtNodes
