#include "BiomassImportNode.h"
#include "BiomassImportWorker.h"
#include "ImportTask.h"
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
    , m_outputNodeName()
{
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

    std::vector<ImportTask> tasks;
    for (int i = 0; i < m_ampPaths.size(); ++i)
    {
        if (!QFileInfo::exists(m_ampPaths[i])) { onError("幅度文件不存在：" + m_ampPaths[i]); return; }
        if (!QFileInfo::exists(m_phasePaths[i])) { onError("相位文件不存在：" + m_phasePaths[i]); return; }
        if (!QFileInfo::exists(m_xmlPaths[i])) { onError("参数XML文件不存在：" + m_xmlPaths[i]); return; }
        if (!QFileInfo::exists(m_orbitPaths[i])) { onError("轨道文件不存在：" + m_orbitPaths[i]); return; }

        ImportTask task;
        task.filename = m_importNames[i];
        task.arguments = QStringList{ m_ampPaths[i], m_phasePaths[i], m_xmlPaths[i], m_orbitPaths[i], m_polarizations[i] };
        tasks.push_back(task);
    }

    startWorker(new BiomassImportWorker(), tasks);
}

QStringList BiomassImportNode::getExpectedOutputFilePaths() const
{
    QStringList expectedPaths;
    QString outputNodeName = getOutputNodeName();
    for (int i = 0; i < m_importNames.size(); ++i)
    {
        expectedPaths.append(projectPath() + "/" + outputNodeName + "/" + m_importNames[i] + ".h5");
    }
    return expectedPaths;
}

QString BiomassImportNode::getOutputNodeName() const
{
    if (m_outputNodeNameEdit) {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (!name.isEmpty())
            return name;
    }
    return m_outputNodeName.isEmpty() ? "Biomass_Batch_Import" : m_outputNodeName;
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

} // namespace QtNodes
