#include "InSARLogManager.h"

#include "Sentinel1BatchImportNode.h"
#include "Sentinel1ImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "ImportDataTypes.h"
#include "NodeUtils.h"
#include "FormatConversion.h"
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

Sentinel1BatchImportNode::Sentinel1BatchImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_projectLabel(nullptr)
    , m_enableOrbitCheckBox(nullptr)
    , m_orbitDirEdit(nullptr)
    , m_orbitBrowseBtn(nullptr)
    , m_manifestPaths()
    , m_outputNodeName("S1_Batch_Import")
    , m_enableOrbitMatch(true)
    , m_orbitDir("")
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

    // 精轨自动匹配 CheckBox
    m_enableOrbitCheckBox = new QCheckBox("自动挂载精密轨道(EOF)");
    m_enableOrbitCheckBox->setChecked(m_enableOrbitMatch);
    configLayout->addWidget(m_enableOrbitCheckBox);

    // 精轨路径选择行
    auto* orbitRow = new QHBoxLayout();
    orbitRow->addWidget(new QLabel("轨道目录："));

    m_orbitDirEdit = new QLineEdit();
    m_orbitDirEdit->setReadOnly(true);
    m_orbitDirEdit->setPlaceholderText("(优先就近, 其次匹配此目录)");

    // 读取并回填默认路径
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString lastOrbitDir = settings.value(QString("Orbit/ProjectDir_%1").arg(projectName()), "").toString();
    if (lastOrbitDir.isEmpty())
    {
        lastOrbitDir = settings.value("Orbit/LastMatchDir", "").toString();
    }
    if (lastOrbitDir.isEmpty())
    {
        lastOrbitDir = QDir::currentPath() + "/orbits";
    }
    m_orbitDir = lastOrbitDir;
    m_orbitDirEdit->setText(QDir::toNativeSeparators(m_orbitDir));
    orbitRow->addWidget(m_orbitDirEdit);

    m_orbitBrowseBtn = new QPushButton("浏览...");
    m_orbitBrowseBtn->setFixedWidth(50);
    orbitRow->addWidget(m_orbitBrowseBtn);

    configLayout->addLayout(orbitRow);

    // 联动使能与信号连接
    m_orbitDirEdit->setEnabled(m_enableOrbitMatch);
    m_orbitBrowseBtn->setEnabled(m_enableOrbitMatch);

    connect(m_enableOrbitCheckBox, &QCheckBox::toggled, this, [this, invalidateNodeData](bool checked) {
        if (m_enableOrbitMatch != checked) {
            m_enableOrbitMatch = checked;
            m_orbitDirEdit->setEnabled(checked);
            m_orbitBrowseBtn->setEnabled(checked);
            invalidateNodeData();
        }
    });

    connect(m_orbitBrowseBtn, &QPushButton::clicked, this, &Sentinel1BatchImportNode::onOrbitBrowseClicked);

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

static QString findMatchedEofFile(const QString& manifestOrSafePath, const QString& projName)
{
    // 1. 优先就近查找：去 manifest.safe 的同级或上级 .SAFE 目录下寻找是否存在 *.EOF
    QFileInfo manifestInfo(manifestOrSafePath);
    QDir safeDir = manifestInfo.dir(); // 一般为 .SAFE/
    QStringList eofFilters;
    eofFilters << "*.EOF" << "*.eofs";
    QStringList eofFiles = safeDir.entryList(eofFilters, QDir::Files);
    if (!eofFiles.isEmpty())
    {
        return safeDir.absoluteFilePath(eofFiles.first());
    }

    // 如果上级是 .SAFE 且里面也没有，向上多找一层
    if (manifestInfo.fileName().toLower() == "manifest.safe")
    {
        QDir parentDir = safeDir;
        parentDir.cdUp();
        QStringList parentEofFiles = parentDir.entryList(eofFilters, QDir::Files);
        if (!parentEofFiles.isEmpty())
        {
            return parentDir.absoluteFilePath(parentEofFiles.first());
        }
    }

    // 2. 如果就近没找到，提取影像的平台与拍摄日期做全局精轨库扫描匹配
    QString pathLower = manifestOrSafePath.toLower();
    QString platform = "S1A";
    if (pathLower.contains("s1b")) platform = "S1B";

    // 匹配日期，S1 命名标准中成像时间在第 5 段：如 S1A_IW_SLC__1SDV_20251204T015841_...
    QRegularExpression dateRe("(20\\d{6})t(\\d{6})");
    QRegularExpressionMatch dateMatch = dateRe.match(pathLower);
    if (!dateMatch.hasMatch()) return QString();

    QString dateStr = dateMatch.captured(1); // "20251204"
    QString timeStr = dateMatch.captured(2); // "015841"

    QDate centerDate = QDate::fromString(dateStr, "yyyyMMdd");
    if (!centerDate.isValid()) return QString();

    QDate prevDate = centerDate.addDays(-1);
    QDate nextDate = centerDate.addDays(1);

    // 3. 读取精轨缓存文件夹
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString cacheDir;
    if (!projName.isEmpty()) {
        cacheDir = settings.value(QString("Orbit/ProjectDir_%1").arg(projName), "").toString();
    }
    if (cacheDir.isEmpty()) {
        cacheDir = settings.value("Orbit/LastMatchDir", "").toString();
    }
    if (cacheDir.isEmpty() || !QDir(cacheDir).exists()) return QString();

    QDir globalDir(cacheDir);

    // A. 尝试精确定位 POEORB (精密轨道)
    QString poePattern = QString("*%1*V%2T215942_%3T000142*.EOF")
                            .arg(platform)
                            .arg(prevDate.toString("yyyyMMdd"))
                            .arg(nextDate.toString("yyyyMMdd"));
    QStringList poeMatches = globalDir.entryList(QStringList{poePattern}, QDir::Files);
    if (!poeMatches.isEmpty())
    {
        return globalDir.absoluteFilePath(poeMatches.first());
    }

    // B. 如果未找到 POEORB，尝试匹配包含成像时刻的 RESORB (重构轨道)
    QString resorbPattern = QString("*%1*RESORB*V%2*.EOF").arg(platform).arg(dateStr);
    QStringList resorbMatches = globalDir.entryList(QStringList{resorbPattern}, QDir::Files);
    for (const QString& resFile : resorbMatches)
    {
        QRegularExpression valRe("V(\\d{8}T\\d{6})_(\\d{8}T\\d{6})");
        QRegularExpressionMatch valMatch = valRe.match(resFile);
        if (valMatch.hasMatch())
        {
            QDateTime startVal = QDateTime::fromString(valMatch.captured(1), "yyyyMMddTHHmmss");
            QDateTime endVal = QDateTime::fromString(valMatch.captured(2), "yyyyMMddTHHmmss");
            QDateTime imgTime = QDateTime::fromString(dateStr + "T" + timeStr, "yyyyMMddTHHmmss");
            if (imgTime >= startVal && imgTime <= endVal)
            {
                return globalDir.absoluteFilePath(resFile);
            }
        }
    }

    return QString();
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
        
        // 自动发现和匹配精轨文件 (.EOF)（两级匹配：就近查找 + 全局缓存检索）
        if (m_enableOrbitMatch) {
            QString matchedEof = findMatchedEofFile(m_preparedOriginalNameList[i], projectName());
            if (!matchedEof.isEmpty()) {
                args.append(matchedEof);
                InSARLogManager::LogInfo("Sentinel1BatchImportNode", "已成功挂载精轨文件: " + matchedEof);
            }
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
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "S1_Batch_Import";
    }
    return name;
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

void Sentinel1BatchImportNode::onOrbitBrowseClicked()
{
    QString dir = QFileDialog::getExistingDirectory(nullptr, "选择精密轨道(EOF)存放目录", m_orbitDir);
    if (!dir.isEmpty())
    {
        m_orbitDir = QDir::toNativeSeparators(dir);
        if (m_orbitDirEdit)
        {
            m_orbitDirEdit->setText(m_orbitDir);
        }

        // 记忆到全局 Config 和项目专属 Config
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        settings.setValue("Orbit/LastMatchDir", m_orbitDir);
        if (!projectName().isEmpty())
        {
            settings.setValue(QString("Orbit/ProjectDir_%1").arg(projectName()), m_orbitDir);
        }

        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
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
    json["enableOrbitMatch"] = m_enableOrbitCheckBox ? m_enableOrbitCheckBox->isChecked() : m_enableOrbitMatch;
    json["orbitDir"] = m_orbitDirEdit ? m_orbitDirEdit->text().trimmed() : m_orbitDir;
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
    m_enableOrbitMatch = json["enableOrbitMatch"].toBool(true);
    m_orbitDir = json["orbitDir"].toString();

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

    if (m_enableOrbitCheckBox)
        m_enableOrbitCheckBox->setChecked(m_enableOrbitMatch);

    if (m_orbitDirEdit)
        m_orbitDirEdit->setText(QDir::toNativeSeparators(m_orbitDir));

    ExecutableNodeDelegateModel::load(json);

    updateAvailableParameters();
}

// ============================================================================
// Sentinel1BatchValidationWidget - Validation view for Sentinel1BatchImportNode
// ============================================================================
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

        // Status card
        QFrame* statusCard = new QFrame(this);
        statusCard->setFrameShape(QFrame::StyledPanel);
        statusCard->setStyleSheet(isDark ?
            "QFrame { background-color: rgba(55, 65, 81, 0.4); border: 1px solid #374151; border-radius: 6px; padding: 12px; }" :
            "QFrame { background-color: rgba(243, 244, 246, 0.6); border: 1px solid #E5E7EB; border-radius: 6px; padding: 12px; }");
        QVBoxLayout* cardLayout = new QVBoxLayout(statusCard);
        cardLayout->setContentsMargins(0,0,0,0);
        cardLayout->setSpacing(4);
        m_statusTitle = new QLabel(tr("验证中..."), statusCard);
        m_statusTitle->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(isDark ? "#60A5FA" : "#2563EB"));
        m_statusDesc = new QLabel(tr("正在检查批量导入的结果。"), statusCard);
        m_statusDesc->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
        cardLayout->addWidget(m_statusTitle);
        cardLayout->addWidget(m_statusDesc);
        mainLayout->addWidget(statusCard);

        // List of output files
        m_fileList = new QListWidget(this);
        mainLayout->addWidget(m_fileList);
    }

    void startAsyncValidation()
    {
        if (m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(tr("验证未通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(tr("节点未完成执行，请先运行批量导入节点。"));
            return;
        }
        QStringList expected = m_node->getExpectedOutputFilePaths();
        if (expected.isEmpty()) {
            m_statusTitle->setText(tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(tr("未生成任何输出文件。"));
            return;
        }
        // Start background check for existence and basic metadata
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
                    m_fileList->addItem(p);
                }
                if (!existing.isEmpty()) {
                    m_statusTitle->setText(tr("验证通过"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                    m_statusDesc->setText(tr("成功检测到已生成的 H5 文件。"));
                } else {
                    m_statusTitle->setText(tr("验证失败"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                    m_statusDesc->setText(tr("未找到生成的 H5 文件，请检查导入过程。"));
                }
            }, Qt::QueuedConnection);
        });
        // Keep future alive
        m_watcher.setFuture(future);
    }

    Sentinel1BatchImportNode* m_node = nullptr;
    QLabel* m_statusTitle = nullptr;
    QLabel* m_statusDesc = nullptr;
    QListWidget* m_fileList = nullptr;
    QFutureWatcher<void> m_watcher;
};

// Implementation of createValidationWidget for Sentinel1BatchImportNode
::QWidget* Sentinel1BatchImportNode::createValidationWidget(::QWidget* parent)
{
    return new Sentinel1BatchValidationWidget(this, parent);
}

// ============================================================================
// End of added validation widget implementation
// ============================================================================

} // namespace QtNodes
