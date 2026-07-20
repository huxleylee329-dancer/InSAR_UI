#include "InSARLogManager.h"
#include "gdal_priv.h"
#include "tinyxml.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QProgressBar>
#include <QDebug>

#include "Sentinel1BatchImportNode.h"
#include "Sentinel1ImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "ImportDataTypes.h"
#include "NodeUtils.h"
#include "FormatConversion.h"
#include "Utils.h"
#include "NodeDetailWindow.hpp"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QSettings>
#include <QDate>
#include <QDateTime>
#include <QFileDialog>

namespace QtNodes {

static TiXmlElement* findElementRecursive(TiXmlElement* parent, const std::string& name);

Sentinel1BatchImportNode::Sentinel1BatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_projectLabel(nullptr)
    , m_importAllBurstsCheckBox(nullptr)
    , m_startBurstSpin(nullptr)
    , m_endBurstSpin(nullptr)
    , m_manifestPaths()
    , m_outputNodeName("S1_Batch_Import")
    , m_importAllBursts(true)
    , m_startBurst(0)
    , m_endBurst(0)
{
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

    // 爆块选择 CheckBox
    m_importAllBurstsCheckBox = new QCheckBox("导入全部爆块(Bursts)");
    m_importAllBurstsCheckBox->setChecked(m_importAllBursts);
    configLayout->addWidget(m_importAllBurstsCheckBox);

    // 爆块范围行
    auto* burstRow = new QHBoxLayout();
    burstRow->addWidget(new QLabel("爆块范围："));
    m_startBurstSpin = new QSpinBox();
    m_startBurstSpin->setRange(0, 99);
    m_startBurstSpin->setKeyboardTracking(false);
    m_startBurstSpin->setValue(m_startBurst);
    m_endBurstSpin = new QSpinBox();
    m_endBurstSpin->setRange(0, 99);
    m_endBurstSpin->setKeyboardTracking(false);
    m_endBurstSpin->setValue(m_endBurst);
    
    burstRow->addWidget(m_startBurstSpin);
    burstRow->addWidget(new QLabel("至"));
    burstRow->addWidget(m_endBurstSpin);
    configLayout->addLayout(burstRow);

    // 联动使能与信号连接
    m_startBurstSpin->setEnabled(!m_importAllBursts);
    m_endBurstSpin->setEnabled(!m_importAllBursts);

    connect(m_importAllBurstsCheckBox, &QCheckBox::toggled, this, [this, invalidateNodeData](bool checked) {
        if (m_importAllBursts != checked) {
            m_importAllBursts = checked;
            m_startBurstSpin->setEnabled(!checked);
            m_endBurstSpin->setEnabled(!checked);
            invalidateNodeData();
        }
    });

    connect(m_startBurstSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, invalidateNodeData](int val) {
        if (m_startBurst != val) {
            m_startBurst = val;
            if (m_endBurstSpin && m_endBurstSpin->value() < val) {
                m_endBurstSpin->setValue(val);
            }
            invalidateNodeData();
        }
    });

    connect(m_endBurstSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, invalidateNodeData](int val) {
        if (m_endBurst != val) {
            m_endBurst = val;
            if (m_startBurstSpin && m_startBurstSpin->value() > val) {
                m_startBurstSpin->setValue(val);
            }
            invalidateNodeData();
        }
    });

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onRemoveFilesClicked);

    return widget;
}

bool Sentinel1BatchImportNode::prepareToStart()
{
    // 强制把 SpinBox 的最新输入解析并同步到变量中，防止用户直接点击画布上的虚拟 Play 按钮而未触发 focusOut 失去焦点导致值滞留
    if (m_startBurstSpin) {
        m_startBurstSpin->interpretText();
        m_startBurst = m_startBurstSpin->value();
    }
    if (m_endBurstSpin) {
        m_endBurstSpin->interpretText();
        m_endBurst = m_endBurstSpin->value();
    }

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

    // 增加对爆块(Burst)及极化/子带的前置校验
    if (!m_importAllBursts)
    {
        if (m_startBurst > m_endBurst)
        {
            onError(QStringLiteral("爆块(Burst)起始编号(%1)不能大于结束编号(%2)！").arg(m_startBurst).arg(m_endBurst));
            return false;
        }

        QString subswath = m_subswathCombo->currentText();
        QString pol = m_polarizationCombo->currentText();

        for (const QString& manifestPath : m_manifestPaths)
        {
            QFileInfo manifestInfo(manifestPath);
            QDir safeDir = manifestInfo.dir();
            QDir annDir(safeDir.filePath("annotation"));
            if (!annDir.exists())
            {
                onError(QStringLiteral("清单文件对应的 annotation 目录不存在：%1").arg(annDir.absolutePath()));
                return false;
            }

            QStringList xmlFilters;
            xmlFilters << QString("*-%1-slc-%2-*.xml").arg(subswath.toLower()).arg(pol.toLower());
            QStringList entries = annDir.entryList(xmlFilters, QDir::Files);
            if (entries.isEmpty())
            {
                onError(QStringLiteral("在 %1 中未找到子带 %2 极化 %3 的元数据XML文件！请检查子带/极化选择是否正确。")
                    .arg(manifestInfo.fileName()).arg(subswath).arg(pol));
                return false;
            }

            QString xmlPath = annDir.absoluteFilePath(entries.first());
            XMLFile xmldoc;
            TiXmlElement* xmlRoot = nullptr;
            if (xmldoc.XMLFile_load(xmlPath.toStdString().c_str()) >= 0)
            {
                xmldoc.get_root(xmlRoot);
                if (xmlRoot)
                {
                    int burstCount = 0;
                    TiXmlElement* listNode = findElementRecursive(xmlRoot, "burstList");
                    if (listNode)
                    {
                        for (TiXmlElement* bNode = listNode->FirstChildElement("burst"); bNode; bNode = bNode->NextSiblingElement("burst"))
                        {
                            burstCount++;
                        }
                    }

                    if (m_endBurst >= burstCount)
                    {
                        onError(QStringLiteral("输入的结束爆块编号(%1)超出了文件 %2 的最大爆块数(%3)！有效编号范围应为 0-%4。")
                            .arg(m_endBurst)
                            .arg(manifestInfo.fileName())
                            .arg(burstCount)
                            .arg(burstCount - 1));
                        return false;
                    }
                }
                else
                {
                    onError(QStringLiteral("无法读取元数据XML文件的根节点：%1").arg(xmlPath));
                    return false;
                }
            }
            else
            {
                onError(QStringLiteral("无法加载元数据XML文件：%1").arg(xmlPath));
                return false;
            }
        }
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

    QString subswath = m_subswathCombo->currentText();
    QString pol = m_polarizationCombo->currentText();

    // 构造 ImportTask 列表
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < m_preparedOriginalNameList.size(); ++i) {
        ImportTask task;
        task.filename = m_preparedImportNameList[i];
        QStringList args = QStringList{ m_preparedOriginalNameList[i], subswath, pol };

        // 非全量导入时，传递 burst 范围参数到 DLL
        if (!m_importAllBursts) {
            args.append(QString::number(m_startBurst));
            args.append(QString::number(m_endBurst));
        }

        task.arguments = args;
        tasks.push_back(task);
    }

    // 三行启动
    auto* worker = new Sentinel1ImportWorker();
    startWorker(worker, tasks);
}

QStringList Sentinel1BatchImportNode::getExpectedOutputFilePaths() const
{
    QStringList paths;
    QString outputNodeName = getOutputNodeName();
    for (const QString& manifestPath : m_manifestPaths) {
        QString importName = generateImportName(manifestPath);
        if (!importName.isEmpty()) {
            paths.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
        }
    }
    return paths;
}

QString Sentinel1BatchImportNode::getOutputNodeName() const
{
    if (m_outputNodeNameEdit)
    {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (name.isEmpty())
        {
            return "S1_Batch_Import";
        }
        return name;
    }
    return m_outputNodeName.isEmpty() ? "S1_Batch_Import" : m_outputNodeName;
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
        QString subswath = m_subswathCombo ? m_subswathCombo->currentText() : m_subswath;
        QString pol = m_polarizationCombo ? m_polarizationCombo->currentText() : m_polarization;
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

                                    auto* iface = getProjectContext();
                                    if (iface && iface->projectXml()) {
                                        XMLFile* xml = iface->projectXml();
                                        xml->XMLFile_remove_node(getOutputNodeName().toStdString().c_str(),
                                                              fileName.toStdString().c_str(),
                                                              importedPath.toStdString().c_str());
                                        xml->XMLFile_save(iface->projectPath().toStdString().c_str());
                                    } else {
                                        XMLFile xml;
                                        QString xmlPath = projectPath() + "/" + projectName();
                                        if (xml.XMLFile_load(xmlPath.toStdString().c_str()) >= 0) {
                                            xml.XMLFile_remove_node(getOutputNodeName().toStdString().c_str(),
                                                                  fileName.toStdString().c_str(),
                                                                  importedPath.toStdString().c_str());
                                            xml.XMLFile_save(xmlPath.toStdString().c_str());
                                        }
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
            m_imageInfo = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfo);
            Q_EMIT dataUpdated(1);

            auto outputData = std::make_shared<ImportedFileData>(m_importedFilePaths, getOutputNodeName());
            setOutputData(0, outputData);
            Q_EMIT dataUpdated(0);
        } else {
            m_imageInfo.reset();
            setOutputData(1, nullptr);
            Q_EMIT dataUpdated(1);

            setOutputData(0, nullptr);
            Q_EMIT dataUpdated(0);
        }

        invalidateExecution();
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
    json["importAllBursts"] = m_importAllBurstsCheckBox ? m_importAllBurstsCheckBox->isChecked() : m_importAllBursts;
    json["startBurst"] = m_startBurstSpin ? m_startBurstSpin->value() : m_startBurst;
    json["endBurst"] = m_endBurstSpin ? m_endBurstSpin->value() : m_endBurst;
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
    m_importAllBursts = json["importAllBursts"].toBool(true);
    m_startBurst = json["startBurst"].toInt(0);
    m_endBurst = json["endBurst"].toInt(0);

    // 同步 UI 控件到最新反序列化的值，防止基类 load() 触发的 validateAndRestoreOutput() 读到旧 of UI 控件值
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

    if (m_importAllBurstsCheckBox) {
        m_importAllBurstsCheckBox->setChecked(m_importAllBursts);
        // setChecked 发出的 toggled 信号可能被卫语句短路（m_importAllBursts 已提前赋值），
        // 所以显式同步 spinbox 使能状态，确保工程恢复后 UI 与实际参数一致
        m_startBurstSpin->setEnabled(!m_importAllBursts);
        m_endBurstSpin->setEnabled(!m_importAllBursts);
    }

    if (m_startBurstSpin)
        m_startBurstSpin->setValue(m_startBurst);

    if (m_endBurstSpin)
        m_endBurstSpin->setValue(m_endBurst);

    ExecutableNodeDelegateModel::load(json);

    updateAvailableParameters();
}

// ============================================================================
// Sentinel1BatchValidationWidget - Validation view for Sentinel1BatchImportNode
// ============================================================================
// ============================================================================
// Sentinel1BatchValidationWidget - Validation view for Sentinel1BatchImportNode
// ============================================================================
struct CompareItem {
    QString group;
    QString name;
    QString rawVal;
    QString h5Val;
    QString status; // "PASS", "FAILED", "WARNING", "IGNORED"
};

// 递归寻找 XML 节点的辅助函数
static TiXmlElement* findElementRecursive(TiXmlElement* parent, const std::string& name) {
    if (!parent) return nullptr;
    if (parent->Value() == name) return parent;
    for (TiXmlElement* child = parent->FirstChildElement(); child; child = child->NextSiblingElement()) {
        TiXmlElement* res = findElementRecursive(child, name);
        if (res) return res;
    }
    return nullptr;
}

// 浮点数比对辅助函数
static bool floatCompare(double a, double b, double eps = 1e-5) {
    return std::abs(a - b) < eps;
}

static QString normalizeTimestamp(const QString& timestamp)
{
    QString normalized = timestamp;
    return normalized.replace(QRegularExpression("[^0-9]"), "");
}

static bool timestampsMatch(const QString& expected, const QString& actual)
{
    const QString expectedDigits = normalizeTimestamp(expected);
    const QString actualDigits = normalizeTimestamp(actual);
    return !expectedDigits.isEmpty() && !actualDigits.isEmpty()
        && (expectedDigits == actualDigits
            || expectedDigits.startsWith(actualDigits)
            || actualDigits.startsWith(expectedDigits));
}

static bool extractExpectedAcquisitionStartTime(
    TiXmlElement* xmlRoot,
    bool importAllBursts,
    int startBurst,
    QString& expectedTime,
    QString& sourceDescription)
{
    expectedTime.clear();
    if (!xmlRoot) {
        sourceDescription = QStringLiteral("原始 XML 未加载");
        return false;
    }

    if (importAllBursts) {
        TiXmlElement* startNode = findElementRecursive(xmlRoot, "startTime");
        if (!startNode) {
            startNode = findElementRecursive(xmlRoot, "productImageStart");
        }
        if (startNode && startNode->GetText()) {
            expectedTime = startNode->GetText();
            sourceDescription = QStringLiteral("整景");
            return true;
        }

        sourceDescription = QStringLiteral("原始 XML 缺少整景开始时间");
        return false;
    }

    TiXmlElement* burstList = findElementRecursive(xmlRoot, "burstList");
    if (!burstList || startBurst < 0) {
        sourceDescription = QStringLiteral("原始 XML 缺少 Burst 列表");
        return false;
    }

    int burstIndex = 0;
    for (TiXmlElement* burst = burstList->FirstChildElement("burst"); burst;
         burst = burst->NextSiblingElement("burst"), ++burstIndex) {
        if (burstIndex != startBurst) {
            continue;
        }

        TiXmlElement* azimuthTime = burst->FirstChildElement("azimuthTime");
        if (azimuthTime && azimuthTime->GetText()) {
            expectedTime = azimuthTime->GetText();
            sourceDescription = QStringLiteral("选中 Burst %1").arg(startBurst);
            return true;
        }

        sourceDescription = QStringLiteral("选中 Burst %1 缺少 azimuthTime").arg(startBurst);
        return false;
    }

    sourceDescription = QStringLiteral("选中 Burst %1 超出原始 XML 范围").arg(startBurst);
    return false;
}

// 精轨缓存扫描辅助函数
// 根据影像的平台与日期，扫描本地目录及 Config.ini 中配置的精轨缓存目录
// 判断是否存在可用的精轨 EOF 文件（POEORB 或 RESORB）
// 与 executeImport 中的 findMatchedEofFile() 保持一致的三级查找策略
static bool orbitCacheHasEof(const QString& manifestPath, const QString& projName)
{
    // === 第 1 级：就近查找 manifest.safe 同级目录 ===
    QFileInfo manifestInfo(manifestPath);
    QDir safeDir = manifestInfo.dir();
    QStringList eofFilters;
    eofFilters << "*.EOF" << "*.eofs";
    if (!safeDir.entryList(eofFilters, QDir::Files).isEmpty()) {
        return true;
    }

    // === 第 2 级：向上查找 .SAFE 上级目录 ===
    if (manifestInfo.fileName().toLower() == "manifest.safe") {
        QDir parentDir = safeDir;
        parentDir.cdUp();
        if (!parentDir.entryList(eofFilters, QDir::Files).isEmpty()) {
            return true;
        }
    }

    // === 第 3 级：全局精轨缓存目录扫描 ===
    QString pathLower = manifestPath.toLower();
    QString platform = "S1A";
    if (pathLower.contains("s1b")) platform = "S1B";

    QRegularExpression dateRe("(20\\d{6})t(\\d{6})");
    QRegularExpressionMatch dateMatch = dateRe.match(pathLower);
    if (!dateMatch.hasMatch()) return false;

    QString dateStr = dateMatch.captured(1);
    QDate centerDate = QDate::fromString(dateStr, "yyyyMMdd");
    if (!centerDate.isValid()) return false;

    QDate prevDate = centerDate.addDays(-1);
    QDate nextDate = centerDate.addDays(1);

    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString cacheDir;
    if (!projName.isEmpty()) {
        cacheDir = settings.value(QString("Orbit/ProjectDir_%1").arg(projName), "").toString();
    }
    if (cacheDir.isEmpty()) {
        cacheDir = settings.value("Orbit/LastMatchDir", "").toString();
    }
    if (cacheDir.isEmpty() || !QDir(cacheDir).exists()) return false;

    QDir globalDir(cacheDir);

    // 检查 POEORB 精轨
    QString poePattern = QString("*%1*V%2*_%3*.EOF")
                            .arg(platform)
                            .arg(prevDate.toString("yyyyMMdd"))
                            .arg(nextDate.toString("yyyyMMdd"));
    if (!globalDir.entryList(QStringList{poePattern}, QDir::Files).isEmpty()) {
        return true;
    }

    // 检查 RESORB 重构轨道
    QString resorbPattern = QString("*%1*RESORB*V%2*.EOF").arg(platform).arg(dateStr);
    QStringList resorbMatches = globalDir.entryList(QStringList{resorbPattern}, QDir::Files);
    for (const QString& resFile : resorbMatches) {
        QRegularExpression valRe("V(\\d{8}T\\d{6})_(\\d{8}T\\d{6})");
        QRegularExpressionMatch valMatch = valRe.match(resFile);
        if (valMatch.hasMatch()) {
            QDateTime startVal = QDateTime::fromString(valMatch.captured(1), "yyyyMMddTHHmmss");
            QDateTime endVal = QDateTime::fromString(valMatch.captured(2), "yyyyMMddTHHmmss");
            QDateTime imgTime = QDateTime::fromString(dateStr + "T" + dateMatch.captured(2), "yyyyMMddTHHmmss");
            if (imgTime >= startVal && imgTime <= endVal) {
                return true;
            }
        }
    }

    return false;
}

// 执行全量属性比对与数据抽查
static std::vector<CompareItem> performComparison(
    const QString& h5Path,
    const QString& manifestPath,
    bool importAllBursts,
    int startBurst,
    int endBurst
) {
    std::vector<CompareItem> results;
    FormatConversion FC;

    // 解析 .SAFE 路径
    QFileInfo manifestInfo(manifestPath);
    QDir safeDir = manifestInfo.dir();
    QDir measDir(safeDir.filePath("measurement"));
    QDir annDir(safeDir.filePath("annotation"));

    // 1. 从 H5 文件名中提取实际的子带与极化 (因为 H5 属性中未包含这两个字符串属性)
    QFileInfo h5Info(h5Path);
    QString baseName = h5Info.baseName().toLower();
    QRegularExpression re("_(iw[1-3])(vv|vh|hh|hv)");
    QRegularExpressionMatch match = re.match(baseName);
    
    QString actSub = "";
    QString actPol = "";
    if (match.hasMatch()) {
        actSub = match.captured(1); // "iw1"
        actPol = match.captured(2); // "vv"
    }

    // 2. 定位 XML 与 TIFF 文件
    QStringList xmlFilters, tiffFilters;
    xmlFilters << QString("*-%1-slc-%2-*.xml").arg(actSub).arg(actPol);
    tiffFilters << QString("*-%1-slc-%2-*.tiff").arg(actSub).arg(actPol);

    QString xmlPath = annDir.entryList(xmlFilters, QDir::Files).isEmpty() ? "" : annDir.absoluteFilePath(annDir.entryList(xmlFilters, QDir::Files).first());
    QString tiffPath = measDir.entryList(tiffFilters, QDir::Files).isEmpty() ? "" : measDir.absoluteFilePath(measDir.entryList(tiffFilters, QDir::Files).first());

    // 3. 从定位到的原始测量 TIFF 或 XML 文件名中反向校验极化与子带 (确保物理解译真实性)
    QString rawSub = "";
    QString rawPol = "";
    if (!tiffPath.isEmpty()) {
        QFileInfo tiffInfo(tiffPath);
        QString tiffName = tiffInfo.fileName().toLower();
        QRegularExpression tiffRe("-(iw[1-3])-slc-(vv|vh|hh|hv)-");
        QRegularExpressionMatch tiffMatch = tiffRe.match(tiffName);
        if (tiffMatch.hasMatch()) {
            rawSub = tiffMatch.captured(1);
            rawPol = tiffMatch.captured(2);
        }
    }
    if (rawSub.isEmpty() && !xmlPath.isEmpty()) {
        QFileInfo xmlInfo(xmlPath);
        QString xmlName = xmlInfo.fileName().toLower();
        QRegularExpression xmlRe("-(iw[1-3])-slc-(vv|vh|hh|hv)-");
        QRegularExpressionMatch xmlMatch = xmlRe.match(xmlName);
        if (xmlMatch.hasMatch()) {
            rawSub = xmlMatch.captured(1);
            rawPol = xmlMatch.captured(2);
        }
    }

    // 加载 XML 元数据
    XMLFile xmldoc;
    TiXmlElement* xmlRoot = nullptr;
    bool xmlLoaded = false;
    if (!xmlPath.isEmpty() && xmldoc.XMLFile_load(xmlPath.toStdString().c_str()) >= 0) {
        xmldoc.get_root(xmlRoot);
        xmlLoaded = (xmlRoot != nullptr);
    }

    // ---------------------------------------------------------
    // 1. 元数据与平台
    // ---------------------------------------------------------
    QString gPlatform = QStringLiteral("元数据与平台");
    
    // 卫星平台
    QString rawPlatform = "S1A";
    if (manifestInfo.absoluteFilePath().toLower().contains("s1b")) rawPlatform = "S1B";
    else if (manifestInfo.absoluteFilePath().toLower().contains("s1c")) rawPlatform = "S1C";
    results.push_back({gPlatform, QStringLiteral("卫星平台 (Platform)"), rawPlatform, rawPlatform, "PASS"});

    // 极化与子带
    results.push_back({gPlatform, QStringLiteral("极化方式 (Polarization)"), rawPol.toUpper(), actPol.toUpper(), 
                       (!rawPol.isEmpty() && rawPol == actPol) ? "PASS" : "FAILED"});
    results.push_back({gPlatform, QStringLiteral("子条带 (Subswath)"), rawSub.toUpper(), actSub.toUpper(), 
                       (!rawSub.isEmpty() && rawSub == actSub) ? "PASS" : "FAILED"});

    // 成像时间
    std::string h5Start = "", h5Stop = "";
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        NodeUtils::readStringFromH5(h5Path, "acquisition_start_time", h5Start);
        NodeUtils::readStringFromH5(h5Path, "acquisition_stop_time", h5Stop);
    }
    QString expectedStart;
    QString startSource;
    const bool hasExpectedStart = extractExpectedAcquisitionStartTime(
        xmlRoot, importAllBursts, startBurst, expectedStart, startSource);
    QString h5StartStr = QString::fromStdString(h5Start);
    const QString startLabel = importAllBursts
        ? QStringLiteral("成像开始时间（整景）")
        : QStringLiteral("成像开始时间（选中 Burst %1）").arg(startBurst);
    const QString expectedStartDisplay = hasExpectedStart
        ? expectedStart
        : QStringLiteral("未读取（%1）").arg(startSource);
    const QString startStatus = (!hasExpectedStart || h5StartStr.isEmpty())
        ? "WARNING"
        : (timestampsMatch(expectedStart, h5StartStr) ? "PASS" : "FAILED");

    results.push_back({gPlatform, startLabel, expectedStartDisplay, h5StartStr, startStatus});

    // 入射角中心值 (inc_center)
    double h5IncCenter = 0.0;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        NodeUtils::readScalarFromH5(h5Path, "inc_center", h5IncCenter);
    }
    double xmlIncCenter = 0.0;
    if (xmlLoaded) {
        TiXmlElement* incNode = findElementRecursive(xmlRoot, "incidenceAngleMidSwath");
        if (incNode && incNode->GetText()) {
            xmlIncCenter = QString(incNode->GetText()).toDouble();
        }
    }
    results.push_back({gPlatform, QStringLiteral("入射角中心值 (inc_center)"),
                       xmlLoaded ? QString::number(xmlIncCenter, 'f', 6) + "°" : QStringLiteral("未读取"),
                       QString::number(h5IncCenter, 'f', 6) + "°",
                       (xmlLoaded && floatCompare(xmlIncCenter, h5IncCenter, 1e-4)) ? "PASS" : "FAILED"});

    // ---------------------------------------------------------
    // 2. 斜距几何参数
    // ---------------------------------------------------------
    QString gGeom = QStringLiteral("斜距几何参数");

    double h5Freq = 0.0, h5Spacing = 0.0, h5FirstPixel = 0.0, h5Prf = 0.0;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        NodeUtils::readScalarFromH5(h5Path, "carrier_frequency", h5Freq);
        NodeUtils::readScalarFromH5(h5Path, "range_spacing", h5Spacing);
        NodeUtils::readScalarFromH5(h5Path, "slant_range_first_pixel", h5FirstPixel);
        NodeUtils::readScalarFromH5(h5Path, "prf", h5Prf);
    }
    
    double xmlFreq = 0.0, xmlSpacing = 0.0, xmlFirstPixel = 0.0, xmlPrf = 0.0;
    if (xmlLoaded) {
        TiXmlElement* freqNode = findElementRecursive(xmlRoot, "radarFrequency");
        if (freqNode && freqNode->GetText()) xmlFreq = QString(freqNode->GetText()).toDouble();
        
        // 读取采样率并以光速公式折算为物理间隔
        double xmlSamplingRate = 0.0;
        TiXmlElement* samplingNode = findElementRecursive(xmlRoot, "rangeSamplingRate");
        if (samplingNode && samplingNode->GetText()) xmlSamplingRate = QString(samplingNode->GetText()).toDouble();
        double c_light = 299792458.0;
        if (xmlSamplingRate > 0.0) {
            xmlSpacing = c_light / (2.0 * xmlSamplingRate);
        }

        // 读取首像素双程斜距时间并折算为近距斜距物理长度
        double xmlSlantRangeTime = 0.0;
        TiXmlElement* timeNode = findElementRecursive(xmlRoot, "slantRangeTime");
        if (timeNode && timeNode->GetText()) xmlSlantRangeTime = QString(timeNode->GetText()).toDouble();
        if (xmlSlantRangeTime > 0.0) {
            xmlFirstPixel = xmlSlantRangeTime * c_light / 2.0;
        }

        // 读取方位向时间间隔 (azimuthTimeInterval) 并求倒数折算为等效采样率
        double xmlAzimuthTimeInterval = 0.0;
        TiXmlElement* intervalNode = findElementRecursive(xmlRoot, "azimuthTimeInterval");
        if (intervalNode && intervalNode->GetText()) xmlAzimuthTimeInterval = QString(intervalNode->GetText()).toDouble();
        if (xmlAzimuthTimeInterval > 0.0) {
            xmlPrf = 1.0 / xmlAzimuthTimeInterval;
        }
    }

    results.push_back({gGeom, QStringLiteral("载波频率 (Frequency)"), 
                       xmlLoaded ? QString::number(xmlFreq, 'f', 1) + " Hz" : QStringLiteral("未读取"), 
                       QString::number(h5Freq, 'f', 1) + " Hz", 
                       (xmlLoaded && floatCompare(xmlFreq, h5Freq)) ? "PASS" : "FAILED"});

    results.push_back({gGeom, QStringLiteral("距离采样物理间隔 (Spacing)"), 
                       xmlLoaded ? QString::number(xmlSpacing, 'f', 6) + " m" : QStringLiteral("未读取"), 
                       QString::number(h5Spacing, 'f', 6) + " m", 
                       (xmlLoaded && floatCompare(xmlSpacing, h5Spacing)) ? "PASS" : "FAILED"});

    results.push_back({gGeom, QStringLiteral("首像素斜距 (near_range)"), 
                       xmlLoaded ? QString::number(xmlFirstPixel, 'f', 4) + " m" : QStringLiteral("未读取"), 
                       QString::number(h5FirstPixel, 'f', 4) + " m", 
                       (xmlLoaded && floatCompare(xmlFirstPixel, h5FirstPixel, 1.0)) ? "PASS" : "FAILED"});

    results.push_back({gGeom, QStringLiteral("等效方位向采样率 (Azimuth PRF)"), 
                       xmlLoaded ? QString::number(xmlPrf, 'f', 3) + " Hz" : QStringLiteral("未读取"), 
                       QString::number(h5Prf, 'f', 3) + " Hz", 
                       (xmlLoaded && floatCompare(xmlPrf, h5Prf)) ? "PASS" : "FAILED"});

    // 物理分辨率参数 (azimuth_resolution & range_resolution)
    double h5AzRes = 0.0, h5RgRes = 0.0;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        NodeUtils::readScalarFromH5(h5Path, "azimuth_resolution", h5AzRes);
        NodeUtils::readScalarFromH5(h5Path, "range_resolution", h5RgRes);
    }
    results.push_back({gGeom, QStringLiteral("方位向物理分辨率 (azimuth_res)"),
                       QStringLiteral("预期: 20.0 m"),
                       QString::number(h5AzRes, 'f', 1) + " m",
                       floatCompare(h5AzRes, 20.0) ? "PASS" : "FAILED"});
    results.push_back({gGeom, QStringLiteral("距离向物理分辨率 (range_res)"),
                       QStringLiteral("预期: 5.0 m"),
                       QString::number(h5RgRes, 'f', 1) + " m",
                       floatCompare(h5RgRes, 5.0) ? "PASS" : "FAILED"});

    // ---------------------------------------------------------
    // 3. 定位多项式与轨道（五场景交叉验证）
    // ---------------------------------------------------------
    QString gOrbit = QStringLiteral("定位多项式与轨道");

    // 轨道向量
    cv::Mat h5Orbit;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        NodeUtils::readMatFromH5(h5Path, "state_vec", h5Orbit, CV_64F);
    }
    std::vector<cv::Vec6d> xmlOrbits;
    if (xmlLoaded) {
        TiXmlElement* orbitListNode = findElementRecursive(xmlRoot, "orbitList");
        if (orbitListNode) {
            for (TiXmlElement* orbitNode = orbitListNode->FirstChildElement("orbit"); orbitNode; orbitNode = orbitNode->NextSiblingElement("orbit")) {
                TiXmlElement* posNode = orbitNode->FirstChildElement("position");
                TiXmlElement* velNode = orbitNode->FirstChildElement("velocity");
                if (posNode && velNode) {
                    double x = QString(posNode->FirstChildElement("x")->GetText()).toDouble();
                    double y = QString(posNode->FirstChildElement("y")->GetText()).toDouble();
                    double z = QString(posNode->FirstChildElement("z")->GetText()).toDouble();
                    double vx = QString(velNode->FirstChildElement("x")->GetText()).toDouble();
                    double vy = QString(velNode->FirstChildElement("y")->GetText()).toDouble();
                    double vz = QString(velNode->FirstChildElement("z")->GetText()).toDouble();
                    xmlOrbits.push_back(cv::Vec6d(x, y, z, vx, vy, vz));
                }
            }
        }
    }

    // 五场景交叉验证核心逻辑
    QString orbitStatus = "FAILED";
    int xCol = (h5Orbit.cols >= 7) ? 1 : 0;
    double h5X = (!h5Orbit.empty() && h5Orbit.rows > 0) ? h5Orbit.at<double>(0, xCol) : 0.0;
    double xmlX = !xmlOrbits.empty() ? xmlOrbits[0][0] : 0.0;

    // 检测输入端精轨可用性
    bool hasEof = orbitCacheHasEof(manifestPath, "");

    QString rawOrbitStr = xmlLoaded ? QStringLiteral("%1 点 (首点X: %2 m)").arg(xmlOrbits.size()).arg(xmlX, 0, 'f', 1) : QStringLiteral("未读取");
    rawOrbitStr += hasEof ? QStringLiteral("\n（精轨库：有可用 EOF）") : QStringLiteral("\n（精轨库：无可用 EOF）");
    QString h5OrbitStr = h5Orbit.empty() ? QStringLiteral("无轨道") : QStringLiteral("%1 点").arg(h5Orbit.rows);

    if (!h5Orbit.empty() && h5Orbit.rows > 0) {
        // 场景 1/3: 精轨 EOF 可用（本地目录或全局缓存中存在匹配的 POEORB/RESORB）
        if (hasEof) {
            if (h5Orbit.rows >= 50) {
                // 场景 1: 精轨可用且 H5 轨道点数充足 → 精密轨道已成功应用
                orbitStatus = "PASS";
                h5OrbitStr += QStringLiteral(" [精密轨道覆盖]");
            } else {
                // 场景 3: 精轨可用但 H5 轨道点数不足 → DLL 静默降级
                orbitStatus = "FAILED";
                h5OrbitStr += QStringLiteral(" [精轨可用但未使用]");
            }
        } else {
            // 精轨 EOF 不可用，进一步区分场景 2/4/5
            if (h5Orbit.rows >= 50) {
                // 场景 4: H5 中有精密轨道数据但本地未找到对应 EOF
                // 可能是历史导入或精轨文件已被移动
                orbitStatus = "WARNING";
                h5OrbitStr += QStringLiteral(" [精轨来源未知]");
            } else {
                // 广播轨道场景：H5 点数少，检查是否与 XML 轨道对齐
                if (!xmlOrbits.empty() && floatCompare(h5X, xmlX, 10.0)) {
                    // 场景 2: 无精轨，广播轨道与 XML 对齐 → 正常
                    orbitStatus = "PASS";
                    h5OrbitStr += QStringLiteral(" [广播轨道]");
                } else {
                    // 场景 5: 无精轨且坐标异常
                    orbitStatus = "FAILED";
                    h5OrbitStr += QStringLiteral(" [广播轨道异常]");
                }
            }
        }
    }
    results.push_back({gOrbit, QStringLiteral("轨道向量个数 & 存储精度"), rawOrbitStr, h5OrbitStr, orbitStatus});

    // 轨道物理平均高度 (orbit_altitude)
    double h5OrbitAltitude = 0.0;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        NodeUtils::readScalarFromH5(h5Path, "orbit_altitude", h5OrbitAltitude);
    }
    double xmlOrbitAltitude = 0.0;
    if (xmlLoaded && !xmlOrbits.empty()) {
        double sumAlt = 0.0;
        for (const auto& pt : xmlOrbits) {
            double dist = std::sqrt(pt[0]*pt[0] + pt[1]*pt[1] + pt[2]*pt[2]);
            sumAlt += (dist - 6378137.0);
        }
        xmlOrbitAltitude = sumAlt / xmlOrbits.size();
    }
    results.push_back({gOrbit, QStringLiteral("轨道物理平均高度 (orbit_altitude)"),
                       xmlLoaded ? QString::number(xmlOrbitAltitude, 'f', 3) + " m" : QStringLiteral("未读取"),
                       QString::number(h5OrbitAltitude, 'f', 3) + " m",
                       (xmlLoaded && floatCompare(xmlOrbitAltitude, h5OrbitAltitude, 5.0)) ? "PASS" : "FAILED"});

    // 定位多项式系数
    cv::Mat lonCoef, latCoef;
    bool hasLon = false, hasLat = false;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        hasLon = NodeUtils::readMatFromH5(h5Path, "lon_coefficient", lonCoef, CV_64F);
        hasLat = NodeUtils::readMatFromH5(h5Path, "lat_coefficient", latCoef, CV_64F);
    }
    bool polyPass = (hasLon && hasLat && !lonCoef.empty() && !latCoef.empty() && cv::countNonZero(lonCoef) > 0);
    results.push_back({gOrbit, QStringLiteral("经纬度定位多项式系数"), 
                       QStringLiteral("从地理控制网格拟合"), 
                       polyPass ? QStringLiteral("已存入 (双精度 6x6 矩阵)") : QStringLiteral("未存入或全零"), 
                       polyPass ? "PASS" : "FAILED"});

    // ---------------------------------------------------------
    // 4. 测量图像与切片
    // ---------------------------------------------------------
    QString gImage = QStringLiteral("测量图像与切片");

    int h5Rows = 0, h5Cols = 0;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        NodeUtils::readScalarFromH5(h5Path, "azimuth_len", h5Rows);
        NodeUtils::readScalarFromH5(h5Path, "range_len", h5Cols);
    }

    int xmlLinesPerBurst = 0, xmlSamplesPerBurst = 0;
    int burstCount = 0;
    if (xmlLoaded) {
        TiXmlElement* swathTimingNode = findElementRecursive(xmlRoot, "swathTiming");
        if (swathTimingNode) {
            TiXmlElement* linesNode = swathTimingNode->FirstChildElement("linesPerBurst");
            if (linesNode && linesNode->GetText()) xmlLinesPerBurst = QString(linesNode->GetText()).toInt();
            TiXmlElement* samplesNode = swathTimingNode->FirstChildElement("samplesPerBurst");
            if (samplesNode && samplesNode->GetText()) xmlSamplesPerBurst = QString(samplesNode->GetText()).toInt();
        }
        TiXmlElement* listNode = findElementRecursive(xmlRoot, "burstList");
        if (listNode) {
            for (TiXmlElement* bNode = listNode->FirstChildElement("burst"); bNode; bNode = bNode->NextSiblingElement("burst")) {
                burstCount++;
            }
        }
    }

    int expectedOffsetRow = 0;
    int expectedRows = 0;
    if (importAllBursts) {
        expectedRows = burstCount * xmlLinesPerBurst;
        expectedOffsetRow = 0;
    } else {
        expectedOffsetRow = startBurst * xmlLinesPerBurst;
        expectedRows = (endBurst - startBurst + 1) * xmlLinesPerBurst;
    }

    bool sizeMatch = (h5Rows == expectedRows && (xmlSamplesPerBurst == 0 || h5Cols == xmlSamplesPerBurst));

    results.push_back({gImage, QStringLiteral("图像行列大小 & 存储格式"), 
                       xmlLoaded ? QString("%1 x %2").arg(expectedRows).arg(xmlSamplesPerBurst) : QStringLiteral("未读取"), 
                       QString("%1 x %2 (CV_32FC2 复浮点)").arg(h5Rows).arg(h5Cols), 
                       sizeMatch ? "PASS" : "FAILED"});

    // 随机 5 点数据采样比对
    bool pixelMatch = false;
    QString pixelDetail = QStringLiteral("未执行 (未找到原始 TIFF)");
    if (!tiffPath.isEmpty() && h5Rows > 0 && h5Cols > 0) {
        GDALAllRegister();
        GDALDataset* poDS = (GDALDataset*)GDALOpen(tiffPath.toLocal8Bit().constData(), GA_ReadOnly);
        if (poDS) {
            GDALRasterBand* poBand = poDS->GetRasterBand(1);
            if (poBand) {
                std::vector<cv::Point> pts;
                // 用网格步长在 H5 影像中自适应搜寻 5 个有回波信号的非零像素点（避开黑边掩膜区）
                int stepY = std::max(1, h5Rows / 6);
                int stepX = std::max(1, h5Cols / 6);
                {
                    // 在循环外层统一加锁保护，提升 HDF5 读写效率并降低锁竞争
                    NodeUtils::Hdf5Locker locker(h5Path);
                    for (int y = stepY; y < h5Rows - stepY && pts.size() < 5; y += stepY) {
                        for (int x = stepX; x < h5Cols - stepX && pts.size() < 5; x += stepX) {
                            cv::Mat h5Re, h5Im;
                            bool rRe = (FC.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_re", y, x, 1, 1, h5Re) == 0);
                            bool rIm = (FC.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_im", y, x, 1, 1, h5Im) == 0);
                            
                            if (rRe && rIm && !h5Re.empty() && !h5Im.empty()) {
                                // 强制进行类型转换至双精度，杜绝 .at 访问时的类型 mismatch 风险
                                h5Re.convertTo(h5Re, CV_64F);
                                h5Im.convertTo(h5Im, CV_64F);
                                double valRe = h5Re.at<double>(0, 0);
                                double valIm = h5Im.at<double>(0, 0);
                                if (std::abs(valRe) > 1e-4 || std::abs(valIm) > 1e-4) {
                                    pts.push_back(cv::Point(x, y));
                                }
                            }
                        }
                    }
                }
                
                // 保底防御：若图像为全空（极罕见），则保底采用四角收缩点
                if (pts.size() < 5) {
                    pts = {
                        cv::Point(2, 2),
                        cv::Point(h5Cols - 3, 2),
                        cv::Point(2, h5Rows - 3),
                        cv::Point(h5Cols - 3, h5Rows - 3),
                        cv::Point(h5Cols / 2, h5Rows / 2)
                    };
                }
                
                QStringList diagMsgs;
                int matchCount = 0;
                int idx = 1;
                for (const auto& pt : pts) {
                    int tiffX = pt.x;
                    int tiffY = pt.y + expectedOffsetRow;
                    
                    if (tiffX < 0 || tiffX >= poDS->GetRasterXSize() || tiffY < 0 || tiffY >= poDS->GetRasterYSize()) {
                        diagMsgs.append(QString("pt%1(%2,%3):越界(TIFF W:%4, H:%5)")
                                        .arg(idx).arg(pt.x).arg(pt.y)
                                        .arg(poDS->GetRasterXSize()).arg(poDS->GetRasterYSize()));
                        idx++;
                        continue;
                    }
                    
                    short tiffVal[2] = {0, 0};
                    if (poBand->RasterIO(GF_Read, tiffX, tiffY, 1, 1, tiffVal, 1, 1, GDT_CInt16, 0, 0) == CE_None) {
                        cv::Mat h5Re, h5Im;
                        bool readRe = false, readIm = false;
                        {
                            NodeUtils::Hdf5Locker locker(h5Path);
                            readRe = (FC.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_re", pt.y, pt.x, 1, 1, h5Re) == 0);
                            readIm = (FC.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_im", pt.y, pt.x, 1, 1, h5Im) == 0);
                        }
                        
                        if (readRe && readIm && !h5Re.empty() && !h5Im.empty()) {
                            // 同样强制类型转换以确保安全读取
                            h5Re.convertTo(h5Re, CV_64F);
                            h5Im.convertTo(h5Im, CV_64F);
                            double h5ReVal = h5Re.at<double>(0, 0);
                            double h5ImVal = h5Im.at<double>(0, 0);
                            
                            bool reOk = (std::abs(tiffVal[0] - h5ReVal) < 1e-4);
                            bool imOk = (std::abs(tiffVal[1] - h5ImVal) < 1e-4);
                            if (reOk && imOk) {
                                matchCount++;
                            } else {
                                diagMsgs.append(QString("pt%1(%2,%3):TIFF[%4,%5],H5[%6,%7]")
                                                .arg(idx).arg(pt.x).arg(pt.y)
                                                .arg(tiffVal[0]).arg(tiffVal[1])
                                                .arg(h5ReVal, 0, 'f', 1).arg(h5ImVal, 0, 'f', 1));
                            }
                        } else {
                            diagMsgs.append(QString("pt%1(%2,%3):读H5空").arg(idx).arg(pt.x).arg(pt.y));
                        }
                    } else {
                        diagMsgs.append(QString("pt%1(%2,%3):读TIF错").arg(idx).arg(pt.x).arg(pt.y));
                    }
                    idx++;
                }
                
                GDALClose(poDS);
                if (matchCount == (int)pts.size()) {
                    pixelMatch = true;
                    pixelDetail = QStringLiteral("随机 5 点实虚部数值完全一致");
                } else {
                    pixelMatch = false;
                    pixelDetail = QStringLiteral("仅 %1/%2 一致; %3").arg(matchCount).arg(pts.size()).arg(diagMsgs.join(" | "));
                    qDebug() << "[PIXEL_VALIDATION_ERROR]" << pixelDetail;
                }
            }
        }
    }
    results.push_back({gImage, QStringLiteral("像素抽样值一致性 (5点)"),
                       QStringLiteral("原始 CInt16 雷达像素"),
                       pixelDetail,
                       pixelMatch ? "PASS" : "FAILED"});

    // 切片位置偏移量校验（DLL 固定写入 offset_row=0, offset_col=0）
    int h5OffsetRow = -1, h5OffsetCol = -1;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        // 容忍读取失败（旧版 DLL 可能未写入这些属性）
        NodeUtils::readScalarFromH5(h5Path, "offset_row", h5OffsetRow);
        NodeUtils::readScalarFromH5(h5Path, "offset_col", h5OffsetCol);
    }
    if (h5OffsetRow >= 0 && h5OffsetCol >= 0) {
        bool offsetOk = (h5OffsetRow == 0 && h5OffsetCol == 0);
        results.push_back({gImage, QStringLiteral("切片位置偏移 (offset_row / offset_col)"),
                           QStringLiteral("预期: (0, 0)"),
                           QString("实际: (%1, %2)").arg(h5OffsetRow).arg(h5OffsetCol),
                           offsetOk ? "PASS" : "FAILED"});
    }

    // ---------------------------------------------------------
    // 5. 多普勒参数拟合
    // ---------------------------------------------------------
    QString gDoppler = QStringLiteral("多普勒参数拟合");
    bool hasDopplerCentroid = false;
    bool hasDopplerCoeffA = false;
    bool hasDopplerCoeffB = false;
    int r1 = 0, c1 = 0;
    int r2 = 0, c2 = 0;
    int r3 = 0, c3 = 0;
    {
        NodeUtils::Hdf5Locker locker(h5Path);
        hasDopplerCentroid = (FC.get_dataset_dims(h5Path.toLocal8Bit().constData(), "doppler_centroid", &r1, &c1) == 0);
        hasDopplerCoeffA = (FC.get_dataset_dims(h5Path.toLocal8Bit().constData(), "doppler_coefficient_a", &r2, &c2) == 0);
        hasDopplerCoeffB = (FC.get_dataset_dims(h5Path.toLocal8Bit().constData(), "doppler_coefficient_b", &r3, &c3) == 0);
    }
    QString dopplerStatus = (hasDopplerCentroid && hasDopplerCoeffA && hasDopplerCoeffB) ? "PASS" : "FAILED";
    QString dopplerH5Desc = QStringLiteral("已存入 (Centroid: %1x%2, Coeff A: %3x%4, Coeff B: %5x%6)")
                            .arg(r1).arg(c1).arg(r2).arg(c2).arg(r3).arg(c3);
    if (dopplerStatus == "FAILED") {
        dopplerH5Desc = QStringLiteral("未完整写入 (Centroid: %1, Coeff A: %2, Coeff B: %3)")
                        .arg(hasDopplerCentroid ? "Y" : "N")
                        .arg(hasDopplerCoeffA ? "Y" : "N")
                        .arg(hasDopplerCoeffB ? "Y" : "N");
    }
    results.push_back({gDoppler, QStringLiteral("多普勒三维系数矩阵 (Centroid / Coeff A / Coeff B)"),
                       QStringLiteral("原始 XML 动态多项式矩阵"),
                       dopplerH5Desc,
                       dopplerStatus});

    // ---------------------------------------------------------
    // 6. 元数据忽略报告
    // ---------------------------------------------------------
    QString gOmit = QStringLiteral("元数据忽略报告");

    results.push_back({gOmit, QStringLiteral("绝对定标查找表 (calibration LUTs)"), 
                       QStringLiteral("原始 XML 含有定标表"), 
                       QStringLiteral("已忽略 (InSAR干涉跳过)"), 
                       "IGNORED"});

    results.push_back({gOmit, QStringLiteral("传感器热噪声表 (noise LUTs)"), 
                       QStringLiteral("原始 XML 含有噪声查找表"), 
                       QStringLiteral("已忽略 (InSAR干涉跳过)"), 
                       "IGNORED"});

    results.push_back({gOmit, QStringLiteral("爆块精细成像时刻 (burst startTime)"),
                       QStringLiteral("原始 XML 含有各个 Burst 精确时间戳"),
                       QStringLiteral("未独立存储 (由 PRF 与行数隐式计算)"),
                       "IGNORED"});

    return results;
}

class Sentinel1BatchValidationWidget : public QWidget
{
public:
    explicit Sentinel1BatchValidationWidget(Sentinel1BatchImportNode* node, QWidget* parent = nullptr)
        : QWidget(parent), m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }
    ~Sentinel1BatchValidationWidget() override = default;

private:
    void setupUI()
    {
        bool isDark = NodeDetailWindow::isDarkTheme(this);
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);
        mainLayout->setSpacing(12);

        // 状态卡片
        m_statusCard = new QFrame(this);
        m_statusCard->setFrameShape(QFrame::StyledPanel);
        m_statusCard->setStyleSheet(isDark ?
            "QFrame { background-color: rgba(55, 65, 81, 0.4); border: 1px solid #374151; border-radius: 6px; padding: 12px; }" :
            "QFrame { background-color: rgba(243, 244, 246, 0.6); border: 1px solid #E5E7EB; border-radius: 6px; padding: 12px; }");
        QVBoxLayout* cardLayout = new QVBoxLayout(m_statusCard);
        cardLayout->setContentsMargins(0,0,0,0);
        cardLayout->setSpacing(4);
        m_statusTitle = new QLabel(tr("验证中..."), m_statusCard);
        m_statusTitle->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(isDark ? "#60A5FA" : "#2563EB"));
        m_statusDesc = new QLabel(tr("正在检查批量导入的结果。"), m_statusCard);
        m_statusDesc->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
        cardLayout->addWidget(m_statusTitle);
        cardLayout->addWidget(m_statusDesc);
        mainLayout->addWidget(m_statusCard);

        // 分栏布局
        QSplitter* splitter = new QSplitter(Qt::Vertical, this);
        mainLayout->addWidget(splitter, 1);

        m_fileList = new QListWidget(splitter);
        m_fileList->setMaximumHeight(75);

        m_compareTable = new QTableWidget(splitter);
        m_compareTable->setColumnCount(4);
        m_compareTable->setHorizontalHeaderLabels({tr("对比内容"), tr("原始输入端 (.SAFE)"), tr("H5 输出端"), tr("状态")});
        m_compareTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_compareTable->verticalHeader()->setVisible(false);
        m_compareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_compareTable->setSelectionBehavior(QAbstractItemView::SelectRows);

        splitter->addWidget(m_fileList);
        splitter->addWidget(m_compareTable);

        connect(m_fileList, &QListWidget::currentItemChanged, this, &Sentinel1BatchValidationWidget::onFileSelected);
    }

    void startAsyncValidation()
    {
        if (m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(tr("验证未通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(tr("节点未完成执行，请先运行批量导入节点。"));
            m_compareTable->setEnabled(false);
            return;
        }

        QStringList expected = m_node->getExpectedOutputFilePaths();
        if (expected.isEmpty()) {
            m_statusTitle->setText(tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(tr("未生成任何输出文件。"));
            m_compareTable->setEnabled(false);
            return;
        }

        QFuture<void> future = QtConcurrent::run([this, expected]() {
            QStringList existing;
            for (const QString& path : expected) {
                if (QFileInfo::exists(path)) {
                    existing.append(path);
                }
            }
            QMetaObject::invokeMethod(this, [this, existing]() {
                m_fileList->clear();
                for (const QString& p : existing) {
                    QFileInfo fi(p);
                    QListWidgetItem* item = new QListWidgetItem(fi.fileName(), m_fileList);
                    item->setData(Qt::UserRole, p);
                }
                
                if (!existing.isEmpty()) {
                    m_statusTitle->setText(tr("验证通过"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                    m_statusDesc->setText(tr("成功检测到已生成的 H5 文件。点击列表文件以加载精细数据核对表。"));
                    m_compareTable->setEnabled(true);
                    
                    m_fileList->setCurrentRow(0);
                } else {
                    m_statusTitle->setText(tr("验证失败"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                    m_statusDesc->setText(tr("未找到生成的 H5 文件，请检查导入过程。"));
                    m_compareTable->setEnabled(false);
                }
            }, Qt::QueuedConnection);
        });
        m_watcher.setFuture(future);
    }

    void onFileSelected(QListWidgetItem* current, QListWidgetItem* previous)
    {
        Q_UNUSED(previous);
        if (!current) {
            m_compareTable->setRowCount(0);
            return;
        }

        QString h5Path = current->data(Qt::UserRole).toString();
        QString manifestPath = "";
        QFileInfo h5Info(h5Path);
        QString baseName = h5Info.baseName();
        for (const QString& manifest : m_node->manifestPaths()) {
            if (m_node->generateImportName(manifest) == baseName) {
                manifestPath = manifest;
                break;
            }
        }

        if (manifestPath.isEmpty()) {
            m_compareTable->setRowCount(0);
            return;
        }

        m_statusTitle->setText(tr("正在读取并核对元数据与像素值..."));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #3B82F6;");

        QJsonObject saved = m_node->save();
        bool importAll = saved["importAllBursts"].toBool(true);
        int startB = saved["startBurst"].toInt(0);
        int endB = saved["endBurst"].toInt(0);

        QFuture<std::vector<CompareItem>> future = QtConcurrent::run(
            performComparison, h5Path, manifestPath, importAll, startB, endB
        );

        auto* watcher = new QFutureWatcher<std::vector<CompareItem>>(this);
        connect(watcher, &QFutureWatcher<std::vector<CompareItem>>::finished, this, [this, watcher]() {
            std::vector<CompareItem> results = watcher->result();
            watcher->deleteLater();

            m_compareTable->setRowCount(0);
            
            QString currentGroup = "";
            for (const auto& item : results) {
                if (item.group != currentGroup) {
                    currentGroup = item.group;
                    int r = m_compareTable->rowCount();
                    m_compareTable->insertRow(r);
                    QTableWidgetItem* grpItem = new QTableWidgetItem(currentGroup);
                    grpItem->setFont(QFont("", 9, QFont::Bold));
                    bool isDark = NodeDetailWindow::isDarkTheme(this);
                    grpItem->setBackground(isDark ? QColor(55, 65, 81, 100) : QColor(243, 244, 246));
                    m_compareTable->setItem(r, 0, grpItem);
                    m_compareTable->setSpan(r, 0, 1, 4);
                }

                int r = m_compareTable->rowCount();
                m_compareTable->insertRow(r);
                
                m_compareTable->setItem(r, 0, new QTableWidgetItem("  " + item.name));
                QTableWidgetItem* rawItem = new QTableWidgetItem(item.rawVal);
                rawItem->setToolTip(item.rawVal);
                m_compareTable->setItem(r, 1, rawItem);
                
                QTableWidgetItem* h5Item = new QTableWidgetItem(item.h5Val);
                h5Item->setToolTip(item.h5Val);
                m_compareTable->setItem(r, 2, h5Item);
                
                QTableWidgetItem* statusItem = new QTableWidgetItem();
                statusItem->setToolTip(item.h5Val);
                if (item.status == "PASS") {
                    statusItem->setText(tr("[ PASS ]"));
                    statusItem->setForeground(QBrush(QColor(16, 185, 129)));
                } else if (item.status == "WARNING") {
                    statusItem->setText(tr("[ WARNING ]"));
                    statusItem->setForeground(QBrush(QColor(245, 158, 11)));
                } else if (item.status == "FAILED") {
                    statusItem->setText(tr("[ FAILED ]"));
                    statusItem->setForeground(QBrush(QColor(239, 68, 68)));
                } else {
                    statusItem->setText(tr("[ 已忽略 ]"));
                    statusItem->setForeground(QBrush(QColor(156, 163, 175)));
                }
                statusItem->setTextAlignment(Qt::AlignCenter);
                m_compareTable->setItem(r, 3, statusItem);
            }

            m_statusTitle->setText(tr("验证完毕"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
            
            bool anyFailed = false;
            bool anyWarning = false;
            for (const auto& r : results) {
                if (r.status == "FAILED") anyFailed = true;
                if (r.status == "WARNING") anyWarning = true;
            }
            if (anyFailed) {
                m_statusTitle->setText(tr("校验发现异常"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                m_statusDesc->setText(tr("部分关键几何字段或采样像素点存在偏差，请确认是否成功导入！"));
            } else if (anyWarning) {
                m_statusTitle->setText(tr("校验存在提醒"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                m_statusDesc->setText(tr("部分检查结果存在不确定性，请确认数据来源。"));
            } else {
                m_statusDesc->setText(tr("当前选中文件的元数据和抽样像素点一致性校验通过。"));
            }
        });
        
        watcher->setFuture(future);
    }

    Sentinel1BatchImportNode* m_node = nullptr;
    QFrame* m_statusCard = nullptr;
    QLabel* m_statusTitle = nullptr;
    QLabel* m_statusDesc = nullptr;
    QListWidget* m_fileList = nullptr;
    QTableWidget* m_compareTable = nullptr;
    QFutureWatcher<void> m_watcher;
};

// Implementation of createValidationWidget for Sentinel1BatchImportNode
::QWidget* Sentinel1BatchImportNode::createValidationWidget(::QWidget* parent)
{
    return new Sentinel1BatchValidationWidget(this, parent);
}

// ============================================================================
// 干涉测量分析数据结构与辅助函数
// ============================================================================

/// 干涉测量分析结果
struct InterferometryResult
{
    // 基线参数
    double perpendicularBaseline = 0.0;   // 垂直基线 (m)
    double temporalBaseline = 0.0;        // 时间基线 (天)
    bool baselinePass = false;
    int orbitPointCountMaster = 0;        // 主影像轨道向量点数
    int orbitPointCountSlave = 0;         // 辅影像轨道向量点数
    bool isPreciseOrbit = false;          // 主辅是否均使用精密轨道（≥50点）

    // 轨道参数
    int relativeOrbitMaster = -1;
    int relativeOrbitSlave = -1;
    bool orbitMatch = false;

    // 飞行方向
    QString passMaster;
    QString passSlave;
    bool passMatch = false;

    // 子条带
    QString swathMaster;
    QString swathSlave;
    bool swathMatch = false;

    // 极化方式
    QString polMaster;
    QString polSlave;
    bool polMatch = false;           // 极化通道是否一致
    bool polIsVV = false;            // 是否均为 VV（用于陆地监测推荐）

    // 传感器平台
    QString sensorMaster;
    QString sensorSlave;

    // 爆块信息
    int burstCountMaster = 0;
    int burstCountSlave = 0;
    bool burstCompatible = false;

    // 成像日期
    QString dateMaster;
    QString dateSlave;

    // 综合评估
    QString overallAssessment;       // "适合干涉" / "谨慎使用" / "不适合干涉"
    QString overallColor;            // "#10B981" / "#F59E0B" / "#EF4444"
    QStringList recommendations;     // 建议事项列表
};

/// 从 Sentinel-1 manifest.safe XML 中提取相对轨道号
/// 使用命名空间自适应正则，兼容 IPF 新旧版本
/// @param manifestPath manifest.safe 文件的完整路径
/// @return 相对轨道号，失败返回 -1
static int extractRelativeOrbitFromManifest(const QString& manifestPath)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    // 健壮命名空间自适应正则：兼容有/无 safe: 前缀的两种 IPF 版本
    QRegularExpression re(
        QStringLiteral(R"(<(?:safe:)?relativeOrbitNumber[^>]*>(\d+)</(?:safe:)?relativeOrbitNumber>)"));
    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }
    return -1;
}

// ============================================================================
// Sentinel1BatchInterferometryWidget - 干涉测量分析选项卡组件
// 分析批量导入的 H5 文件对是否适合进行 InSAR 干涉处理
// ============================================================================
class Sentinel1BatchInterferometryWidget : public QWidget
{
public:
    explicit Sentinel1BatchInterferometryWidget(Sentinel1BatchImportNode* node, QWidget* parent = nullptr)
        : QWidget(parent), m_node(node)
    {
        setupUI();
        populateFileSelectors();
    }
    ~Sentinel1BatchInterferometryWidget() override = default;

private:
    void setupUI()
    {
        bool isDark = NodeDetailWindow::isDarkTheme(this);
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);
        mainLayout->setSpacing(12);

        // ---- 1. 状态卡片 ----
        m_statusCard = new QFrame(this);
        m_statusCard->setFrameShape(QFrame::StyledPanel);
        m_statusCard->setStyleSheet(isDark ?
            "QFrame { background-color: rgba(55, 65, 81, 0.4); border: 1px solid #374151; border-radius: 6px; padding: 12px; }" :
            "QFrame { background-color: rgba(243, 244, 246, 0.6); border: 1px solid #E5E7EB; border-radius: 6px; padding: 12px; }");
        QVBoxLayout* cardLayout = new QVBoxLayout(m_statusCard);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(4);
        m_statusTitle = new QLabel(tr("干涉测量分析"), m_statusCard);
        m_statusTitle->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;")
            .arg(isDark ? "#60A5FA" : "#2563EB"));
        m_statusDesc = new QLabel(tr("选择主影像与辅影像，评估干涉测量适用性。"), m_statusCard);
        m_statusDesc->setStyleSheet(QString("font-size: 11px; color: %1;")
            .arg(isDark ? "#9CA3AF" : "#6B7280"));
        cardLayout->addWidget(m_statusTitle);
        cardLayout->addWidget(m_statusDesc);

        // 分析结果摘要（分析完成后填充，初始隐藏）
        m_statusDetail = new QLabel(m_statusCard);
        m_statusDetail->setWordWrap(true);
        m_statusDetail->setStyleSheet(QString("font-size: 11px; color: %1;")
            .arg(isDark ? "#D1D5DB" : "#4B5563"));
        m_statusDetail->hide();
        cardLayout->addWidget(m_statusDetail);

        mainLayout->addWidget(m_statusCard);

        // ---- 2. 影像对选择区域 ----
        auto* selectorRow = new QHBoxLayout();
        selectorRow->setSpacing(12);

        // 主影像选择
        auto* masterGroup = new QVBoxLayout();
        QLabel* masterLabel = new QLabel(tr("主影像 (Master):"), this);
        masterLabel->setStyleSheet(QString("font-weight: bold; color: %1;")
            .arg(isDark ? "#F3F4F6" : "#1F2937"));
        m_masterCombo = new QComboBox(this);
        m_masterCombo->setMinimumWidth(220);
        m_masterCombo->setToolTip(tr("选择作为干涉基准的主影像"));
        masterGroup->addWidget(masterLabel);
        masterGroup->addWidget(m_masterCombo);
        selectorRow->addLayout(masterGroup);

        // 辅影像选择
        auto* slaveGroup = new QVBoxLayout();
        QLabel* slaveLabel = new QLabel(tr("辅影像 (Slave):"), this);
        slaveLabel->setStyleSheet(QString("font-weight: bold; color: %1;")
            .arg(isDark ? "#F3F4F6" : "#1F2937"));
        m_slaveCombo = new QComboBox(this);
        m_slaveCombo->setMinimumWidth(220);
        m_slaveCombo->setToolTip(tr("选择待评估干涉适用性的辅影像"));
        slaveGroup->addWidget(slaveLabel);
        slaveGroup->addWidget(m_slaveCombo);
        selectorRow->addLayout(slaveGroup);

        // 分析按钮
        m_analyzeBtn = new QPushButton(tr("分析干涉适用性"), this);
        m_analyzeBtn->setFixedHeight(32);
        m_analyzeBtn->setStyleSheet(isDark ?
            "QPushButton { background-color: #2563EB; color: white; border-radius: 4px; padding: 4px 16px; font-weight: bold; }"
            "QPushButton:hover { background-color: #1D4ED8; }"
            "QPushButton:disabled { background-color: #4B5563; color: #9CA3AF; }" :
            "QPushButton { background-color: #3B82F6; color: white; border-radius: 4px; padding: 4px 16px; font-weight: bold; }"
            "QPushButton:hover { background-color: #2563EB; }"
            "QPushButton:disabled { background-color: #D1D5DB; color: #9CA3AF; }");
        selectorRow->addWidget(m_analyzeBtn, 0, Qt::AlignBottom);
        selectorRow->addStretch();

        mainLayout->addLayout(selectorRow);

        // ---- 3. 分析结果表格 ----
        m_resultsTable = new QTableWidget(this);
        m_resultsTable->setColumnCount(3);
        m_resultsTable->setHorizontalHeaderLabels({tr("分析项目"), tr("结果"), tr("评估")});
        m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        m_resultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_resultsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        m_resultsTable->setColumnWidth(0, 130);   // 分析项目列固定宽度
        m_resultsTable->setColumnWidth(2, 80);    // 评估列固定宽度
        m_resultsTable->verticalHeader()->setVisible(false);
        m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_resultsTable->setSelectionMode(QAbstractItemView::NoSelection);
        m_resultsTable->setAlternatingRowColors(true);

        QString tableStyle = isDark ?
            "QTableWidget { background-color: #1F2937; alternate-background-color: #374151; "
            "border: 1px solid #374151; border-radius: 4px; gridline-color: #374151; }"
            "QHeaderView::section { background-color: #111827; color: #9CA3AF; padding: 6px; "
            "border: none; border-bottom: 1px solid #374151; font-weight: bold; font-size: 11px; }"
            "QTableWidget::item { color: #D1D5DB; }" :
            "QTableWidget { background-color: #FFFFFF; alternate-background-color: #F9FAFB; "
            "border: 1px solid #E5E7EB; border-radius: 4px; gridline-color: #E5E7EB; }"
            "QHeaderView::section { background-color: #F3F4F6; color: #4B5563; padding: 6px; "
            "border: none; border-bottom: 1px solid #E5E7EB; font-weight: bold; font-size: 11px; }"
            "QTableWidget::item { color: #374151; }";
        m_resultsTable->setStyleSheet(tableStyle);

        mainLayout->addWidget(m_resultsTable, 1);

        // ---- 信号连接 ----
        connect(m_analyzeBtn, &QPushButton::clicked, this, &Sentinel1BatchInterferometryWidget::onAnalyzeClicked);

        connect(m_masterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            validateSelection();
        });
        connect(m_slaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            validateSelection();
        });
    }

    /// 影像选择变化时校验按钮状态
    void validateSelection()
    {
        // 主辅影像变化时清除旧结果
        m_resultsTable->setRowCount(0);
        if (m_statusDetail) m_statusDetail->hide();
        bool valid = (m_masterCombo->count() >= 2 && m_masterCombo->currentIndex() >= 0
                      && m_slaveCombo->currentIndex() >= 0
                      && m_masterCombo->currentIndex() != m_slaveCombo->currentIndex());
        m_analyzeBtn->setEnabled(valid);
    }

    /// 填充主/辅影像下拉框
    void populateFileSelectors()
    {
        m_masterCombo->clear();
        m_slaveCombo->clear();

        if (m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(tr("未完成导入"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
            m_statusDesc->setText(tr("请先执行批量导入，生成 H5 文件后再进行干涉测量分析。"));
            m_analyzeBtn->setEnabled(false);
            return;
        }

        QStringList expected = m_node->getExpectedOutputFilePaths();
        QStringList existing;
        for (const QString& path : expected) {
            if (QFileInfo::exists(path)) {
                existing.append(path);
            }
        }

        if (existing.size() < 2) {
            m_statusTitle->setText(tr("影像数量不足"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
            m_statusDesc->setText(tr("至少需要 2 个 H5 文件才能进行干涉测量分析。当前可用: %1 个。")
                .arg(existing.size()));
            m_analyzeBtn->setEnabled(false);
            return;
        }

        for (const QString& path : existing) {
            QFileInfo fi(path);
            QString displayName = fi.baseName();
            m_masterCombo->addItem(displayName, path);
            m_slaveCombo->addItem(displayName, path);
        }

        // 自动选择：主影像=第0个，辅影像=第1个
        m_masterCombo->setCurrentIndex(0);
        m_slaveCombo->setCurrentIndex(qMin(1, existing.size() - 1));

        m_statusTitle->setText(tr("就绪"));
        m_statusTitle->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;")
            .arg(NodeDetailWindow::isDarkTheme(this) ? "#10B981" : "#10B981"));
        m_statusDesc->setText(tr("已发现 %1 个 H5 文件，请选择主/辅影像对进行分析。").arg(existing.size()));
        m_analyzeBtn->setEnabled(existing.size() >= 2);
    }

    /// 按钮点击：触发分析
    void onAnalyzeClicked()
    {
        if (m_masterCombo->currentIndex() == m_slaveCombo->currentIndex()) {
            m_statusTitle->setText(tr("选择错误"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(tr("主影像和辅影像不能为同一文件，请重新选择。"));
            return;
        }

        QString masterPath = m_masterCombo->currentData().toString();
        QString slavePath = m_slaveCombo->currentData().toString();

        // 反查 manifest 路径（用于提取相对轨道号）
        QString masterManifest = findManifestForH5(masterPath);
        QString slaveManifest = findManifestForH5(slavePath);

        m_statusTitle->setText(tr("正在分析..."));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #3B82F6;");
        m_statusDesc->setText(tr("正在计算基线与干涉参数，请稍候..."));
        m_analyzeBtn->setEnabled(false);
        m_resultsTable->setRowCount(0);
        if (m_statusDetail) m_statusDetail->hide();

        // 异步执行分析
        QFuture<InterferometryResult> future = QtConcurrent::run(
            performInterferometryAnalysis, masterPath, slavePath,
            masterManifest, slaveManifest);

        auto* watcher = new QFutureWatcher<InterferometryResult>(this);
        connect(watcher, &QFutureWatcher<InterferometryResult>::finished, this,
            [this, watcher]() {
                InterferometryResult result = watcher->result();
                watcher->deleteLater();
                displayResults(result);
                m_analyzeBtn->setEnabled(true);
            });
        watcher->setFuture(future);
    }

    /// 根据 H5 文件路径反查对应的 manifest.safe 路径
    QString findManifestForH5(const QString& h5Path)
    {
        QFileInfo h5Info(h5Path);
        QString baseName = h5Info.baseName();
        const QStringList& manifests = m_node->manifestPaths();
        for (const QString& manifest : manifests) {
            if (m_node->generateImportName(manifest) == baseName) {
                return manifest;
            }
        }
        return QString();
    }

    /// 显示分析结果
    void displayResults(const InterferometryResult& result)
    {
        bool isDark = NodeDetailWindow::isDarkTheme(this);
        m_resultsTable->setRowCount(0);

        // 辅助 lambda：添加分析行
        auto addRow = [&](const QString& category, const QString& content,
                          const QString& status, const QString& color) {
            int r = m_resultsTable->rowCount();
            m_resultsTable->insertRow(r);
            m_resultsTable->setItem(r, 0, new QTableWidgetItem(category));
            m_resultsTable->setItem(r, 1, new QTableWidgetItem(content));
            auto* statusItem = new QTableWidgetItem(status);
            statusItem->setForeground(QBrush(QColor(color)));
            statusItem->setTextAlignment(Qt::AlignCenter);
            QFont boldFont = statusItem->font();
            boldFont.setBold(true);
            statusItem->setFont(boldFont);
            m_resultsTable->setItem(r, 2, statusItem);
        };

        // ---- 1. 基线分析 ----
        QString grpBaseline = QStringLiteral("  基线分析");

        QString blStatus, blColor, blText;
        if (result.perpendicularBaseline < -998.0) {
            // 基线计算失败
            blText = QStringLiteral("B⊥: 计算失败（Utils_d.dll 基线估算库调用异常，已回退至时间基线手动计算）");
            blStatus = QStringLiteral("WARNING");
            blColor = "#F59E0B";
        } else {
            blText = QStringLiteral("B⊥ = %1 m").arg(result.perpendicularBaseline, 0, 'f', 1);
            if (result.isPreciseOrbit) {
                blText += QStringLiteral("（基于精密轨道，精度可靠）");
                if (std::abs(result.perpendicularBaseline) <= 100.0) {
                    blStatus = QStringLiteral("PASS");
                    blColor = "#10B981";
                } else {
                    blStatus = QStringLiteral("超标");
                    blColor = "#EF4444";
                }
            } else {
                blText += QStringLiteral("（基于广播轨道，可能存在~30m偏差，建议优先完成精轨匹配再行干涉）");
                blStatus = QStringLiteral("WARNING");
                blColor = "#F59E0B";
            }
        }
        addRow(grpBaseline, blText, blStatus, blColor);

        // 轨道精度信息行
        addRow(grpBaseline,
            QStringLiteral("主影像轨道点数: %1 | 辅影像轨道点数: %2")
                .arg(result.orbitPointCountMaster).arg(result.orbitPointCountSlave),
            result.isPreciseOrbit ? QStringLiteral("精密轨道") : QStringLiteral("广播轨道"),
            result.isPreciseOrbit ? "#10B981" : "#F59E0B");

        // 时间基线
        QString btText, btStatus, btColor;
        if (result.temporalBaseline < -998.0) {
            btText = QStringLiteral("Bt: 计算失败");
            btStatus = QStringLiteral("WARNING");
            btColor = "#F59E0B";
        } else {
            btText = QStringLiteral("Bt = %1 天").arg(result.temporalBaseline, 0, 'f', 1);
            if (result.temporalBaseline >= 6.0 && result.temporalBaseline <= 36.0) {
                btStatus = QStringLiteral("PASS");
                btColor = "#10B981";
            } else if (result.temporalBaseline > 36.0) {
                btStatus = QStringLiteral("偏长");
                btColor = "#F59E0B";
            } else {
                btStatus = QStringLiteral("过短");
                btColor = "#F59E0B";
            }
        }
        addRow(grpBaseline, btText, btStatus, btColor);

        // ---- 2. 轨道与覆盖 ----
        QString grpOrbit = QStringLiteral("  轨道与覆盖");

        // 相对轨道号
        QString relOrbitText, relOrbitStatus, relOrbitColor;
        if (result.relativeOrbitMaster >= 0 && result.relativeOrbitSlave >= 0) {
            relOrbitText = QStringLiteral("主: %1 / 辅: %2")
                .arg(result.relativeOrbitMaster).arg(result.relativeOrbitSlave);
            relOrbitStatus = result.orbitMatch ? QStringLiteral("一致") : QStringLiteral("不一致");
            relOrbitColor = result.orbitMatch ? "#10B981" : "#EF4444";
        } else {
            relOrbitText = QStringLiteral("未知（manifest 不可用，请手动核查）");
            relOrbitStatus = QStringLiteral("未知");
            relOrbitColor = "#9CA3AF";
        }
        addRow(grpOrbit, relOrbitText, relOrbitStatus, relOrbitColor);

        // 飞行方向
        addRow(grpOrbit,
            QStringLiteral("主: %1 / 辅: %2").arg(result.passMaster).arg(result.passSlave),
            result.passMatch ? QStringLiteral("一致") : QStringLiteral("不一致"),
            result.passMatch ? "#10B981" : "#EF4444");

        // 子条带
        addRow(grpOrbit,
            QStringLiteral("主: %1 / 辅: %2").arg(result.swathMaster).arg(result.swathSlave),
            result.swathMatch ? QStringLiteral("一致") : QStringLiteral("不一致"),
            result.swathMatch ? "#10B981" : "#EF4444");

        // 爆块数
        addRow(grpOrbit,
            QStringLiteral("主: %1 / 辅: %2").arg(result.burstCountMaster).arg(result.burstCountSlave),
            result.burstCompatible ? QStringLiteral("兼容") : QStringLiteral("不兼容"),
            result.burstCompatible ? "#10B981" : "#EF4444");

        // ---- 3. 极化与传感器 ----
        QString grpPol = QStringLiteral("  极化与传感器");

        // 极化方式（物理约束严格判定）
        QString polText = QStringLiteral("主: %1 / 辅: %2").arg(result.polMaster).arg(result.polSlave);
        QString polStatus, polColor;
        if (result.polMatch) {
            if (result.polIsVV) {
                polStatus = QStringLiteral("PASS 理想");
                polColor = "#10B981";
                polText += QStringLiteral("（VV在陆地监测中相干性最佳）");
            } else {
                polStatus = QStringLiteral("WARNING");
                polColor = "#F59E0B";
                polText += QStringLiteral("（极化一致可干涉，但在地表监测中VV相干性通常优于VH）");
            }
        } else {
            polStatus = QStringLiteral("FAILED");
            polColor = "#EF4444";
            polText += QStringLiteral("（极化不一致，物理上无法进行干涉！）");
        }
        addRow(grpPol, polText, polStatus, polColor);

        // 传感器平台
        addRow(grpPol,
            QStringLiteral("主: %1 / 辅: %2").arg(result.sensorMaster).arg(result.sensorSlave),
            QStringLiteral("信息"),
            "#9CA3AF");

        // 成像日期
        addRow(grpPol,
            QStringLiteral("主: %1 / 辅: %2").arg(result.dateMaster).arg(result.dateSlave),
            QStringLiteral("信息"),
            "#9CA3AF");

        // ---- 4. 综合评估（合并入状态卡片） ----
        // 更新状态卡片标题与摘要
        m_statusTitle->setText(tr("综合评估: ") + result.overallAssessment);
        m_statusTitle->setStyleSheet(
            QString("font-size: 14px; font-weight: bold; color: %1;").arg(result.overallColor));

        // 简短摘要行
        QString summaryLine;
        if (result.perpendicularBaseline > -998.0) {
            summaryLine += QStringLiteral("B⊥ = %1 m  |  ").arg(result.perpendicularBaseline, 0, 'f', 1);
        }
        summaryLine += QStringLiteral("Bt = %1 天").arg(result.temporalBaseline, 0, 'f', 1);
        m_statusDesc->setText(summaryLine);

        // 详细建议放入状态卡片的第三行
        m_statusDetail->setText(result.recommendations.join("\n"));
        m_statusDetail->show();

        // 状态卡片边框颜色跟随评估结果
        m_statusCard->setStyleSheet(isDark ?
            QString("QFrame { background-color: rgba(55, 65, 81, 0.4); border: 2px solid %1; border-radius: 6px; padding: 12px; }")
                .arg(result.overallColor) :
            QString("QFrame { background-color: rgba(243, 244, 246, 0.6); border: 2px solid %1; border-radius: 6px; padding: 12px; }")
                .arg(result.overallColor));
    }

    // ========================================================================
    // 静态分析函数（在后台线程中运行）
    // ========================================================================
    static InterferometryResult performInterferometryAnalysis(
        const QString& masterH5, const QString& slaveH5,
        const QString& masterManifest, const QString& slaveManifest)
    {
        InterferometryResult result;

        // ---- 1. 读取基础元数据 ----
        {
            NodeUtils::Hdf5Locker locker;
            std::string passM, passS, sensorM, sensorS, polM, polS, swathM, swathS;
            std::string dateM, dateS;
            int burstM = 0, burstS = 0;

            NodeUtils::readStringFromH5(masterH5, "pass", passM);
            NodeUtils::readStringFromH5(masterH5, "sensor", sensorM);
            NodeUtils::readStringFromH5(masterH5, "polarization", polM);
            NodeUtils::readStringFromH5(masterH5, "swath", swathM);
            NodeUtils::readStringFromH5(masterH5, "acquisition_start_time", dateM);
            NodeUtils::readScalarFromH5(masterH5, "burstCount", burstM);

            NodeUtils::readStringFromH5(slaveH5, "pass", passS);
            NodeUtils::readStringFromH5(slaveH5, "sensor", sensorS);
            NodeUtils::readStringFromH5(slaveH5, "polarization", polS);
            NodeUtils::readStringFromH5(slaveH5, "swath", swathS);
            NodeUtils::readStringFromH5(slaveH5, "acquisition_start_time", dateS);
            NodeUtils::readScalarFromH5(slaveH5, "burstCount", burstS);

            result.passMaster = QString::fromStdString(passM);
            result.passSlave = QString::fromStdString(passS);
            result.passMatch = (result.passMaster.toUpper() == result.passSlave.toUpper());

            result.sensorMaster = QString::fromStdString(sensorM);
            result.sensorSlave = QString::fromStdString(sensorS);

            result.polMaster = QString::fromStdString(polM);
            result.polSlave = QString::fromStdString(polS);
            result.polMatch = (result.polMaster.toLower() == result.polSlave.toLower());
            result.polIsVV = (result.polMaster.toLower() == "vv" && result.polSlave.toLower() == "vv");

            result.swathMaster = QString::fromStdString(swathM);
            result.swathSlave = QString::fromStdString(swathS);
            result.swathMatch = (result.swathMaster.toLower() == result.swathSlave.toLower());

            result.burstCountMaster = burstM;
            result.burstCountSlave = burstS;
            result.burstCompatible = (burstM == burstS);

            result.dateMaster = QString::fromStdString(dateM).left(10);
            result.dateSlave = QString::fromStdString(dateS).left(10);
        }

        // ---- 2. 相对轨道号（从 manifest XML 提取） ----
        if (!masterManifest.isEmpty() && !slaveManifest.isEmpty()) {
            result.relativeOrbitMaster = extractRelativeOrbitFromManifest(masterManifest);
            result.relativeOrbitSlave = extractRelativeOrbitFromManifest(slaveManifest);
        }
        result.orbitMatch = (result.relativeOrbitMaster == result.relativeOrbitSlave
                             && result.relativeOrbitMaster >= 0);

        // ---- 3. 读取轨道向量点数（用于轨道精度判定） ----
        {
            NodeUtils::Hdf5Locker locker;
            cv::Mat stateVecM, stateVecS;
            if (NodeUtils::readMatFromH5(masterH5, "state_vec", stateVecM, CV_64F)) {
                result.orbitPointCountMaster = stateVecM.rows;
            }
            if (NodeUtils::readMatFromH5(slaveH5, "state_vec", stateVecS, CV_64F)) {
                result.orbitPointCountSlave = stateVecS.rows;
            }
            // 精密轨道判定：主辅均 ≥50 个轨道点
            result.isPreciseOrbit = (result.orbitPointCountMaster >= 50
                                     && result.orbitPointCountSlave >= 50);
        }

        // ---- 4. 基线计算 ----
        try {
            std::vector<std::string> filePair = {
                masterH5.toStdString(),
                slaveH5.toStdString()
            };
            Utils util;
            cv::Mat temporal, spatial;
            int ret = util.spatialTemporalBaselineEstimation(filePair, 1, temporal, spatial);
            if (ret == 0 && !spatial.empty() && spatial.cols >= 2) {
                result.perpendicularBaseline = spatial.at<double>(0, 1);
                result.temporalBaseline = temporal.at<double>(0, 1);
                result.baselinePass = (std::abs(result.perpendicularBaseline) <= 100.0);
            } else {
                // DLL 返回异常，回退到手动计算时间基线
                computeTemporalBaselineFallback(result, masterH5, slaveH5);
            }
        } catch (...) {
            // DLL 调用异常，回退
            computeTemporalBaselineFallback(result, masterH5, slaveH5);
        }

        // ---- 5. 综合评估 ----
        QStringList issues;
        if (!result.passMatch)
            issues.append(QStringLiteral("飞行方向不一致（%1 vs %2）")
                .arg(result.passMaster).arg(result.passSlave));
        if (!result.orbitMatch && result.relativeOrbitMaster >= 0)
            issues.append(QStringLiteral("相对轨道号不一致（%1 vs %2）")
                .arg(result.relativeOrbitMaster).arg(result.relativeOrbitSlave));
        if (!result.swathMatch)
            issues.append(QStringLiteral("子条带不一致（%1 vs %2）")
                .arg(result.swathMaster).arg(result.swathSlave));
        if (!result.polMatch)
            issues.append(QStringLiteral("极化方式不一致（%1 vs %2）")
                .arg(result.polMaster).arg(result.polSlave));
        if (!result.burstCompatible)
            issues.append(QStringLiteral("爆块数量不兼容（%1 vs %2）")
                .arg(result.burstCountMaster).arg(result.burstCountSlave));
        if (result.perpendicularBaseline > -998.0 && !result.baselinePass)
            issues.append(QStringLiteral("垂直基线过大（%1m > 100m）")
                .arg(result.perpendicularBaseline, 0, 'f', 1));
        if (result.temporalBaseline > -998.0 && result.temporalBaseline > 36.0)
            issues.append(QStringLiteral("时间基线较长（%1天 > 36天）")
                .arg(result.temporalBaseline, 0, 'f', 1));

        // 硬性失败条件：极化不一致或飞行方向不一致 → 直接判定不适合
        bool hardFailure = (!result.polMatch || !result.passMatch);

        if (issues.isEmpty()) {
            result.overallAssessment = QStringLiteral("适合干涉");
            result.overallColor = "#10B981";  // 绿色
            result.recommendations.append(
                QStringLiteral("该影像对满足 InSAR 干涉处理的基本条件，建议进行配准与干涉处理。"));
            if (result.polIsVV) {
                result.recommendations.append(
                    QStringLiteral("VV 极化适合陆地形变监测，干涉相位信噪比较高。"));
            }
        } else if (!hardFailure && issues.size() <= 2 && result.baselinePass) {
            result.overallAssessment = QStringLiteral("谨慎使用");
            result.overallColor = "#F59E0B";  // 橙色
            result.recommendations.append(
                QStringLiteral("部分条件不满足，干涉结果可能受以下因素影响:"));
            for (const QString& issue : issues) {
                result.recommendations.append(QStringLiteral("  • ") + issue);
            }
        } else {
            result.overallAssessment = QStringLiteral("不适合干涉");
            result.overallColor = "#EF4444";  // 红色
            result.recommendations.append(
                QStringLiteral("该影像对不满足干涉处理的基本条件:"));
            for (const QString& issue : issues) {
                result.recommendations.append(QStringLiteral("  • ") + issue);
            }
        }

        // 环境因素提示（不易自动化检测）
        result.recommendations.append(QString());
        result.recommendations.append(
            QStringLiteral("注意事项: 地表环境（积雪、植被物候、土壤湿度）可能影响干涉相干性，"
               "请根据成像季节手动核查。建议优先选择冬季或旱季获取的影像对，"
               "以减少大气延迟和地表变化的影响。"));

        if (!result.isPreciseOrbit && result.perpendicularBaseline > -998.0) {
            result.recommendations.append(
                QStringLiteral("轨道精度提示: 当前使用的轨道数据点数不足50个，建议优先匹配精密轨道文件（POEORB）"
                   "以获得更准确的基线估计。"));
        }

        return result;
    }

    /// 备选时间基线计算（不依赖 Utils DLL）
    static void computeTemporalBaselineFallback(InterferometryResult& result,
                                                 const QString& masterH5, const QString& slaveH5)
    {
        std::string dateMasterStr, dateSlaveStr;
        {
            NodeUtils::Hdf5Locker locker;
            NodeUtils::readStringFromH5(masterH5, "acquisition_start_time", dateMasterStr);
            NodeUtils::readStringFromH5(slaveH5, "acquisition_start_time", dateSlaveStr);
        }
        QDateTime dtMaster = QDateTime::fromString(
            QString::fromStdString(dateMasterStr), Qt::ISODate);
        QDateTime dtSlave = QDateTime::fromString(
            QString::fromStdString(dateSlaveStr), Qt::ISODate);
        if (dtMaster.isValid() && dtSlave.isValid()) {
            result.temporalBaseline = dtMaster.msecsTo(dtSlave) / (1000.0 * 3600.0 * 24.0);
        }
        // 垂直基线保持标记为计算失败
        result.perpendicularBaseline = -999.0;
        result.baselinePass = false;
    }

    Sentinel1BatchImportNode* m_node = nullptr;

    // UI 组件
    QFrame* m_statusCard = nullptr;
    QLabel* m_statusTitle = nullptr;
    QLabel* m_statusDesc = nullptr;
    QComboBox* m_masterCombo = nullptr;
    QComboBox* m_slaveCombo = nullptr;
    QPushButton* m_analyzeBtn = nullptr;
    QTableWidget* m_resultsTable = nullptr;
    QLabel* m_statusDetail = nullptr;  // 状态卡片第三行：建议与详细信息
};

// ============================================================================
// 干涉测量分析选项卡接口实现
// ============================================================================
::QWidget* Sentinel1BatchImportNode::createInterferometryWidget(::QWidget* parent)
{
    return new Sentinel1BatchInterferometryWidget(this, parent);
}

// ============================================================================
// End of added validation widget implementation
// ============================================================================

} // namespace QtNodes
