#include "ALOS2ImportNode.h"
#include "ALOS2ImportWorker.h"
#include "ImportTask.h"
#include "NodeUtils.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

namespace QtNodes {

ALOS2ImportNode::ALOS2ImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_fileListWidget(nullptr)
    , m_projectLabel(nullptr)
    , m_imgPaths()
    , m_outputNodeName()
{
}

QWidget* ALOS2ImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // Top section: file list (8:2 stretch) - stretch 4
    auto* topSection = new QHBoxLayout();
    topSection->setStretch(0, 8);
    topSection->setStretch(1, 2);

    // Left side: file list widget
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(100);
    topSection->addWidget(m_fileListWidget);

    // Right side: add/remove buttons
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

    // Bottom section: configuration options - stretch 4
    auto* bottomSection = new QHBoxLayout();
    auto* configLayout = new QVBoxLayout();

    // Target node [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName.isEmpty() ? "ALOS2_Batch_Import" : m_outputNodeName);
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
    nodeRow->addWidget(m_outputNodeNameEdit);
    configLayout->addLayout(nodeRow);

    bottomSection->addLayout(configLayout);
    mainLayout->addLayout(bottomSection, 4);

    // Connect signals
    connect(addFiles, &QPushButton::clicked, this, &ALOS2ImportNode::onAddFilesClicked);
    connect(removeFiles, &QPushButton::clicked, this, &ALOS2ImportNode::onRemoveFilesClicked);

    return widget;
}

void ALOS2ImportNode::executeImport()
{
    if (m_imgPaths.isEmpty())
    {
        onError("请至少添加一个 IMG 文件。");
        return;
    }

    for (const QString& path : m_imgPaths)
    {
        if (!QFileInfo::exists(path))
        {
            onError("IMG 文件不存在：" + path);
            return;
        }
    }

    std::vector<ImportTask> tasks;
    for (const QString& imgPath : m_imgPaths)
    {
        QString importName = generateOutputFileName(imgPath);
        if (importName.isEmpty())
        {
            onError("无法从 IMG 文件生成输出文件名：" + imgPath);
            return;
        }

        QString ledPath = generateLEDPath(imgPath);
        if (!QFileInfo::exists(ledPath))
        {
            onError("IMG 文件对应的 LED 文件未找到：" + imgPath);
            return;
        }

        ImportTask task;
        task.filename = importName;
        task.arguments = QStringList{ imgPath, ledPath };
        tasks.push_back(task);
    }

    QString outputNodeName = getOutputNodeName();

    QStringList pathsToCheck;
    for (const auto& task : tasks) {
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + task.filename + ".h5");
        pathsToCheck.append(projectPath() + "/" + outputNodeName + "/" + task.filename + ".jpg");
    }

    // 因为已经在 prepareToStart() 中完成了存在性检查，这里直接读取 m_preparedOverwriteResult 并分支处理
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        setState(ExecutionState::Idle);
        return;
    } else if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        setProgress(100);
        onImportFinished();
        return;
    }


    // 三行启动
    auto* worker = new ALOS2ImportWorker();
    startWorker(worker, tasks);
}

QStringList ALOS2ImportNode::getExpectedOutputFilePaths() const
{
    QStringList paths;
    QString outputNodeName = getOutputNodeName();
    for (const QString& imgPath : m_imgPaths) {
        QString importName = generateOutputFileName(imgPath);
        if (!importName.isEmpty()) {
            paths.append(projectPath() + "/" + outputNodeName + "/" + importName + ".h5");
        }
    }
    return paths;
}

QString ALOS2ImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return "ALOS2_Batch_Import";
    }
    return name;
}

QString ALOS2ImportNode::generateOutputFileName(const QString& imgPath) const
{
    QFileInfo fileInfo(imgPath);
    QString baseName = fileInfo.baseName();

    // ALOS-2 IMG file name is 39 characters
    // Position 4-5: Polarization (2 chars)
    // Position 22-27: Date (6 chars, without century)
    // Format: 20 + date + "_" + polarization
    if (baseName.length() == 39)
    {
        QString polarization = baseName.mid(4, 2);
        QString date = "20" + baseName.mid(22, 6);
        return date + "_" + polarization;
    }

    return QString();
}

QString ALOS2ImportNode::generateLEDPath(const QString& imgPath) const
{
    QFileInfo imgFileInfo(imgPath);
    QString imgBaseName = imgFileInfo.baseName();

    // LED file has same numbering as IMG file
    // IMG: ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx (39 chars)
    // LED: LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx (same base structure as IMG)
    // LED files are in the same directory with "LED" prefix instead of "IMG"
    // The numbering part is at position 34-36 (001)
    // Actually, looking at ALOS-2 naming, LED files are usually:
    // LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx (same base structure as IMG)
    // LED files are in the same directory with "LED" prefix instead of "IMG"
    // The number is at position 34-36 (001)
    // Get the directory
    QDir dir = imgFileInfo.absoluteDir();
    // Construct LED file name: LED-ALOS2xxxxx-HH-xxxxx-001-xxx-xxxxx
    // Remove "IMG-" and add "LED-"
    QString ledBaseName = "LED-" + imgBaseName.mid(3);  // Remove "IMG-" and add "LED-"
    QString ledPath = dir.filePath(ledBaseName + ".LED");
    return ledPath;
}

void ALOS2ImportNode::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        nullptr,
        tr("导入 ALOS-2 数据"),
        QDir::currentPath(),
        tr("IMG 文件 (*.IMG *.img)")
    );

    for (const QString& file : files)
    {
        if (!m_imgPaths.contains(file))
        {
            m_imgPaths.append(file);
            m_fileListWidget->addItem(QFileInfo(file).fileName());

            int outCount = nPorts(PortType::Out);
            for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
            invalidateExecution();
        }
    }
}

void ALOS2ImportNode::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        for (QListWidgetItem* item : selectedItems)
        {
            int row = m_fileListWidget->row(item);
            m_imgPaths.removeAt(row);
            delete item;
        }
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    }
}

QJsonObject ALOS2ImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    QJsonArray pathsArray;
    for (const QString &path : m_imgPaths)
        pathsArray.append(path);
    json["imgPaths"] = pathsArray;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    return json;
}

void ALOS2ImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_imgPaths.clear();
    QJsonArray pathsArray = json["imgPaths"].toArray();
    for (const QJsonValue &val : pathsArray)
        m_imgPaths.append(val.toString());

    m_outputNodeName = json["outputNodeName"].toString("ALOS2_Batch_Import");

    ExecutableNodeDelegateModel::load(json);

    if (m_fileListWidget) {
        m_fileListWidget->clear();
        for (const QString &path : m_imgPaths)
            m_fileListWidget->addItem(QFileInfo(path).fileName());
    }

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
}

} // namespace QtNodes
