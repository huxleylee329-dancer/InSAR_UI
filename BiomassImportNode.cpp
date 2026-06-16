#include "BiomassImportNode.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>

namespace QtNodes {

BiomassImportNode::BiomassImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectLabel(nullptr)
    , m_widget(nullptr)
    , m_ampPaths()
    , m_phasePaths()
    , m_xmlPaths()
    , m_orbitPaths()
    , m_polarizations()
    , m_importNames()
    , m_importedFilePaths()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

BiomassImportNode::~BiomassImportNode()
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
}

QWidget* BiomassImportNode::createWidget()
{
    m_widget = new QWidget();
    m_widget->setFixedWidth(300); // Node UI Sizing rule requirement

    auto* mainLayout = new QVBoxLayout(m_widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // Top section: file list layout
    auto* topSection = new QHBoxLayout();
    topSection->setStretch(0, 8);
    topSection->setStretch(1, 2);

    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
    topSection->addWidget(m_fileListWidget);

    auto* buttonLayout = new QVBoxLayout();
    QPushButton* addFiles = new QPushButton("添加");
    QPushButton* removeFiles = new QPushButton("移除");
    buttonLayout->addWidget(addFiles);
    buttonLayout->addWidget(removeFiles);
    topSection->addLayout(buttonLayout);

    mainLayout->addLayout(topSection, 4);

    // Project Name Badge
    m_projectLabel = createProjectBadge(projectName());
    mainLayout->addWidget(m_projectLabel);

    // Target node layout
    auto* configLayout = new QVBoxLayout();
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName.isEmpty() ? "Biomass_Batch_Import" : m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
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
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    mainLayout->addLayout(configLayout, 4);

    connect(addFiles, &QPushButton::clicked, this, &BiomassImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &BiomassImportNode::onRemoveFilesClicked);

    return m_widget;
}

void BiomassImportNode::executeImport()
{
    if (m_ampPaths.isEmpty())
    {
        onError("请至少添加一组 Biomass L1A 数据。");
        return;
    }

    std::vector<QString> ampFileList;
    std::vector<QString> phaseFileList;
    std::vector<QString> xmlFileList;
    std::vector<QString> orbitFileList;
    std::vector<QString> polList;
    std::vector<QString> importNameList;

    for (int i = 0; i < m_ampPaths.size(); ++i)
    {
        if (!QFileInfo::exists(m_ampPaths[i])) { onError("幅度文件不存在：" + m_ampPaths[i]); return; }
        if (!QFileInfo::exists(m_phasePaths[i])) { onError("相位文件不存在：" + m_phasePaths[i]); return; }
        if (!QFileInfo::exists(m_xmlPaths[i])) { onError("参数XML文件不存在：" + m_xmlPaths[i]); return; }
        if (!QFileInfo::exists(m_orbitPaths[i])) { onError("轨道文件不存在：" + m_orbitPaths[i]); return; }

        ampFileList.push_back(m_ampPaths[i]);
        phaseFileList.push_back(m_phasePaths[i]);
        xmlFileList.push_back(m_xmlPaths[i]);
        orbitFileList.push_back(m_orbitPaths[i]);
        polList.push_back(m_polarizations[i]);
        importNameList.push_back(m_importNames[i]);
    }

    QString outputNodeName = getOutputNodeName();
    QStringList pathsToCheck;
    for (const QString& importName : importNameList) {
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + importName + ".jpg");
    }

    auto overwriteRes = NodeUtils::checkAndPromptOverwrite(getProjectContext(), outputNodeName, pathsToCheck, nullptr);
    if (overwriteRes == NodeUtils::OverwriteResult::Cancel) {
        setState(ExecutionState::Idle);
        return;
    } else if (overwriteRes == NodeUtils::OverwriteResult::LoadExisting) {
        setProgress(100);
        onImportFinished();
        return;
    } else if (overwriteRes == NodeUtils::OverwriteResult::Overwrite) {
        NodeUtils::removeDataNodeFromProject(getProjectContext(), outputNodeName);
    }

    m_thread = new QThread(this);
    m_workerThread = new BiomassImportWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &BiomassImportNode::startBiomassImport, m_workerThread, &BiomassImportWorker::import_Biomass_patch);
    connect(m_workerThread, &BiomassImportWorker::updateProcess, this, &BiomassImportNode::onImportProgress);
    connect(m_workerThread, &BiomassImportWorker::endProcess, this, &BiomassImportNode::onImportFinished);
    connect(m_workerThread, &BiomassImportWorker::errorProcess, this, &BiomassImportNode::onThreadError);
    connect(m_workerThread, &BiomassImportWorker::sendModel, this, &BiomassImportNode::onModelUpdated);

    m_thread->start();

    Q_EMIT startBiomassImport(
        projectPath(),
        ampFileList,
        phaseFileList,
        xmlFileList,
        orbitFileList,
        polList,
        importNameList,
        outputNodeName,
        projectName(),
        projectModel()
    );
}

void BiomassImportNode::stopExecution()
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

QStringList BiomassImportNode::getImportedFilePaths() const
{
    return m_importedFilePaths;
}

NodeDataType BiomassImportNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
        return NodeDataType{"imported_file", "Imported Files"};
    return NodeDataType();
}

QString BiomassImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "Biomass_Batch_Import";
    }
    return name;
}

void BiomassImportNode::onAddFilesClicked()
{
    QString xmlFile = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择 Biomass XML 参数文件"),
        QDir::currentPath(),
        tr("XML 文件 (*.xml)")
    );
    if (xmlFile.isEmpty()) return;

    QFileInfo xmlInfo(xmlFile);
    QDir dir = xmlInfo.absoluteDir();
    QString xmlBase = xmlInfo.baseName();

    QString ampFile, phaseFile, orbitFile;
    QStringList tiffFiles = dir.entryList(QStringList() << "*.tiff" << "*.tif", QDir::Files);
    for (const QString& f : tiffFiles)
    {
        QString absF = dir.filePath(f);
        if (f.contains("amp", Qt::CaseInsensitive) || f.contains("amplitude", Qt::CaseInsensitive))
            ampFile = absF;
        else if (f.contains("phase", Qt::CaseInsensitive) || f.contains("phs", Qt::CaseInsensitive))
            phaseFile = absF;
        else if (f.contains("orbit", Qt::CaseInsensitive) || f.contains("orb", Qt::CaseInsensitive))
            orbitFile = absF;
    }

    if (ampFile.isEmpty()) {
        ampFile = QFileDialog::getOpenFileName(nullptr, tr("选择对应幅度文件 (.tiff)"), xmlInfo.absolutePath(), tr("TIFF 文件 (*.tiff *.tif)"));
    }
    if (phaseFile.isEmpty()) {
        phaseFile = QFileDialog::getOpenFileName(nullptr, tr("选择对应相位文件 (.tiff)"), xmlInfo.absolutePath(), tr("TIFF 文件 (*.tiff *.tif)"));
    }
    if (orbitFile.isEmpty()) {
        orbitFile = QFileDialog::getOpenFileName(nullptr, tr("选择对应轨道文件 (.tiff)"), xmlInfo.absolutePath(), tr("TIFF 文件 (*.tiff *.tif)"));
    }

    if (ampFile.isEmpty() || phaseFile.isEmpty() || orbitFile.isEmpty())
    {
        QMessageBox::warning(nullptr, tr("警告"), tr("缺少对应的幅度、相位或轨道文件，无法添加！"));
        return;
    }

    bool ok = false;
    QStringList items;
    items << "HH" << "HV" << "VH" << "VV";
    QString pol = QInputDialog::getItem(nullptr, tr("选择极化方式"), tr("极化方式:"), items, 0, false, &ok);
    if (!ok || pol.isEmpty()) return;

    QString importName = xmlBase + "_" + pol;

    m_ampPaths.append(ampFile);
    m_phasePaths.append(phaseFile);
    m_xmlPaths.append(xmlFile);
    m_orbitPaths.append(orbitFile);
    m_polarizations.append(pol);
    m_importNames.append(importName);

    m_fileListWidget->addItem(importName + " (" + pol + ")");

    int outCount = nPorts(PortType::Out);
    for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
    invalidateExecution();
}

void BiomassImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_ampPaths.removeAt(row);
            m_phasePaths.removeAt(row);
            m_xmlPaths.removeAt(row);
            m_orbitPaths.removeAt(row);
            m_polarizations.removeAt(row);
            m_importNames.removeAt(row);
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

void BiomassImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void BiomassImportNode::onImportFinished()
{
    QString outputNodeName = getOutputNodeName();
    m_importedFilePaths.clear();
    for (int i = 0; i < m_ampPaths.size(); ++i)
    {
        QString filePath = QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(m_importNames[i]);
        m_importedFilePaths.append(filePath);
    }

    ImportNodeBase::onImportFinished();

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
}

void BiomassImportNode::onThreadError(const QString& error)
{
    onError(error);

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
}

void BiomassImportNode::setExecutionMode(ExecutionMode mode)
{
    ImportNodeBase::setExecutionMode(mode);
}

void BiomassImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
    }
}

QJsonObject BiomassImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    
    QJsonArray ampArray, phaseArray, xmlArray, orbitArray, polArray, nameArray;
    for (int i = 0; i < m_ampPaths.size(); ++i)
    {
        ampArray.append(m_ampPaths[i]);
        phaseArray.append(m_phasePaths[i]);
        xmlArray.append(m_xmlPaths[i]);
        orbitArray.append(m_orbitPaths[i]);
        polArray.append(m_polarizations[i]);
        nameArray.append(m_importNames[i]);
    }
    
    json["ampPaths"] = ampArray;
    json["phasePaths"] = phaseArray;
    json["xmlPaths"] = xmlArray;
    json["orbitPaths"] = orbitArray;
    json["polarizations"] = polArray;
    json["importNames"] = nameArray;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    
    return json;
}

void BiomassImportNode::load(QJsonObject const &json)
{
    m_ampPaths.clear();
    m_phasePaths.clear();
    m_xmlPaths.clear();
    m_orbitPaths.clear();
    m_polarizations.clear();
    m_importNames.clear();

    QJsonArray ampArray = json["ampPaths"].toArray();
    QJsonArray phaseArray = json["phasePaths"].toArray();
    QJsonArray xmlArray = json["xmlPaths"].toArray();
    QJsonArray orbitArray = json["orbitPaths"].toArray();
    QJsonArray polArray = json["polarizations"].toArray();
    QJsonArray nameArray = json["importNames"].toArray();

    for (int i = 0; i < ampArray.size(); ++i)
    {
        m_ampPaths.append(ampArray[i].toString());
        m_phasePaths.append(phaseArray[i].toString());
        m_xmlPaths.append(xmlArray[i].toString());
        m_orbitPaths.append(orbitArray[i].toString());
        m_polarizations.append(polArray[i].toString());
        m_importNames.append(nameArray[i].toString());
    }

    m_outputNodeName = json["outputNodeName"].toString("Biomass_Batch_Import");

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (int i = 0; i < m_importNames.size(); ++i)
            m_fileListWidget->addItem(m_importNames[i] + " (" + m_polarizations[i] + ")");
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
}

bool BiomassImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/";
    QDir dir(outputPath);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0) {
        QStringList importedFiles;
        for (int i = 0; i < m_importNames.size(); ++i) {
            QString importedPath = outputPath + m_importNames[i] + ".h5";
            if (QFile::exists(importedPath)) {
                importedFiles.append(importedPath);
            }
        }
        if (!importedFiles.isEmpty()) {
            m_importedFilePaths = importedFiles;
            auto outputData = std::make_shared<ImportedFileData>(importedFiles, nodeName);
            setOutputData(0, outputData);
            return true;
        }
    }

    return false;
}

} // namespace QtNodes
