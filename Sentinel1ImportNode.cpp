#include "InSARLogManager.h"
#include "Sentinel1ImportNode.h"
#include "IApplicationInterface.h"
#include "ImportDataTypes.h"
#include "NodeUtils.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>


namespace QtNodes {

Sentinel1ImportNode::Sentinel1ImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_projectCombo(nullptr)
    , m_manifestEdit(nullptr)
    , m_podEdit(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_manifestPath()
    , m_podPath()
    , m_importedFilePath()
    , m_outputNodeName("{InputName}")
    , m_outputFileName("{InputName}")
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

Sentinel1ImportNode::~Sentinel1ImportNode()
{
    // Clean up worker thread
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

QWidget* Sentinel1ImportNode::createWidget()
{
    auto* widget = new QWidget();
    widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // 哨兵图像文件（.safe） + 浏览按钮 [3:7:0]
    auto* manifestLayout = new QHBoxLayout();
    QLabel* manifestLabel = new QLabel("哨兵图像文件（.safe）");
    manifestLayout->addWidget(manifestLabel, 3);
    m_manifestEdit = new QLineEdit();
    m_manifestEdit->setPlaceholderText("选择 .safe 目录中的 manifest 文件");
    connect(m_manifestEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() { 
        QString text = m_manifestEdit->text();
        if (m_manifestPath != text) {
            if (!confirmParameterChange()) {
                m_manifestEdit->setText(m_manifestPath);
                return;
            }
            m_manifestPath = text; 
            
            updateAvailableParameters(m_manifestPath);

            QString autoName = generateOutputFileName();
            if (!autoName.isEmpty() && m_outputNodeNameEdit->text().isEmpty()) {
                m_outputNodeNameEdit->setText(autoName);
            }
            if (!autoName.isEmpty() && m_outputFileNameEdit->text().isEmpty()) {
                m_outputFileNameEdit->setText(autoName);
            }
            invalidateNodeData();
        }
    });
    manifestLayout->addWidget(m_manifestEdit, 7);
    QPushButton* manifestBrowse = new QPushButton("浏览...");
    manifestLayout->addWidget(manifestBrowse, 0);
    layout->addLayout(manifestLayout);

    // 精轨文件（可空缺） + 浏览按钮 [3:7:0]
    auto* podLayout = new QHBoxLayout();
    QLabel* podLabel = new QLabel("精轨文件（可空缺）");
    podLayout->addWidget(podLabel, 3);
    m_podEdit = new QLineEdit();
    m_podEdit->setPlaceholderText("可选，留空则不使用精轨文件");
    connect(m_podEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() { 
        QString text = m_podEdit->text();
        if (m_podPath != text) {
            if (!confirmParameterChange()) {
                m_podEdit->setText(m_podPath);
                return;
            }
            m_podPath = text; 
            invalidateNodeData();
        }
    });
    podLayout->addWidget(m_podEdit, 7);
    QPushButton* podBrowse = new QPushButton("浏览...");
    podLayout->addWidget(podBrowse, 0);
    layout->addLayout(podLayout);

    // 子带选择（subswath） [3:7]
    auto* subswathLayout = new QHBoxLayout();
    QLabel* subswathLabel = new QLabel("子带选择（subswath）");
    subswathLayout->addWidget(subswathLabel, 3);
    m_subswathCombo = new QComboBox();
    m_subswathCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_subswathCombo->addItem("iw1");
    m_subswathCombo->addItem("iw2");
    m_subswathCombo->addItem("iw3");
    m_subswathCombo->setCurrentText(m_subswath);
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
    subswathLayout->addWidget(m_subswathCombo, 7);
    layout->addLayout(subswathLayout);

    // 极化方式选择 [3:7]
    auto* polLayout = new QHBoxLayout();
    QLabel* polLabel = new QLabel("极化方式选择");
    polLayout->addWidget(polLabel, 3);
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_polarizationCombo->addItem("vv");
    m_polarizationCombo->addItem("vh");
    m_polarizationCombo->setCurrentText(m_polarization);
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
    polLayout->addWidget(m_polarizationCombo, 7);
    layout->addLayout(polLayout);

    // 项目名称 [3:7]
    auto* projectLayout = new QHBoxLayout();
    QLabel* projectLabel = new QLabel("项目名称：");
    projectLayout->addWidget(projectLabel, 3);
    m_projectCombo = new QComboBox();
    m_projectCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_projectCombo->setEditable(false);
    projectLayout->addWidget(m_projectCombo, 7);
    layout->addLayout(projectLayout);

    // 目标节点 [3:7]
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点");
    nodeNameLayout->addWidget(nodeNameLabel, 3);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("支持 {InputName} 变量");
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            NodeUtils::removeDataNodeFromProject(getProjectContext(), resolveInputName(m_outputNodeName));
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    nodeNameLayout->addWidget(m_outputNodeNameEdit, 7);
    layout->addLayout(nodeNameLayout);

    // 目标文件名 [3:7]
    auto* fileNameLayout = new QHBoxLayout();
    QLabel* fileNameLabel = new QLabel("目标文件名");
    fileNameLayout->addWidget(fileNameLabel, 3);
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setText(m_outputFileName);
    m_outputFileNameEdit->setPlaceholderText("支持 {InputName} 变量");
    connect(m_outputFileNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() { 
        QString text = m_outputFileNameEdit->text();
        if (m_outputFileName != text) {
            if (!confirmParameterChange()) {
                m_outputFileNameEdit->setText(m_outputFileName);
                return;
            }
            m_outputFileName = text; 
            invalidateNodeData();
        }
    });
    fileNameLayout->addWidget(m_outputFileNameEdit, 7);
    layout->addLayout(fileNameLayout);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // Connect signals
    connect(manifestBrowse, &QPushButton::clicked, this, &Sentinel1ImportNode::onManifestBrowseClicked);
    connect(podBrowse, &QPushButton::clicked, this, &Sentinel1ImportNode::onPodBrowseClicked);

    // 从项目模型动态填充项目列表 (对齐 Workspace 与 Generic SAR 行为)
    QStandardItemModel* model = projectModel();
    if (model && model->rowCount() > 0) {
        for (int i = 0; i < model->rowCount(); ++i) {
            auto item = model->item(i, 0);
            if (item) {
                m_projectCombo->addItem(item->text());
            }
        }
        // 默认选中当前活动项目
        int index = m_projectCombo->findText(projectName());
        if (index >= 0) {
            m_projectCombo->setCurrentIndex(index);
        }
    } else {
        m_projectCombo->addItem("未打开项目");
    }

    return widget;
}

void Sentinel1ImportNode::executeImport()
{
    m_manifestPath = m_manifestEdit->text().trimmed();
    if (m_manifestPath.isEmpty())
    {
        onError("请选择清单文件");
        return;
    }

    if (!QFileInfo::exists(m_manifestPath))
    {
        onError("清单文件不存在：" + m_manifestPath);
        return;
    }

    m_podPath = m_podEdit->text().trimmed();
    if (!m_podPath.isEmpty() && !QFileInfo::exists(m_podPath))
    {
        onError("精轨文件不存在：" + m_podPath);
        return;
    }

    m_outputFileName = m_outputFileNameEdit ? m_outputFileNameEdit->text().trimmed() : m_outputFileName.trimmed();
    if (m_outputFileName.isEmpty())
    {
        m_outputFileName = "{InputName}";
    }

    QString resolvedFileName = getOutputFileName();
    if (resolvedFileName.isEmpty())
    {
        onError("无法从清单文件生成输出文件名");
        return;
    }

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(this, &Sentinel1ImportNode::startImport,
            m_workerThread, &MyThread::import_sentinel);
    connect(m_workerThread, &MyThread::updateProcess,
            this, &Sentinel1ImportNode::onImportProgress);
    connect(m_workerThread, &MyThread::endProcess,
            this, &Sentinel1ImportNode::onImportFinished);
    connect(m_workerThread, &MyThread::errorProcess,
            this, &Sentinel1ImportNode::onThreadError);
    connect(m_workerThread, &MyThread::sendModel,
            this, &Sentinel1ImportNode::onModelUpdated);

    m_thread->start();

    QString subswath = m_subswathCombo->currentText();
    QString polarization = m_polarizationCombo->currentText();
    QString outputNodeName = getOutputNodeName();

    Q_EMIT startImport(
        m_podPath,
        m_manifestPath,
        subswath,
        polarization,
        projectPath(),
        outputNodeName,
        resolvedFileName,
        projectName(),
        projectModel()
    );
}

QString Sentinel1ImportNode::resolveInputName(const QString& name) const
{
    QString resolved = name;
    resolved.replace(QString::fromUtf8("\uFF5BInputName\uFF5D"), "{InputName}");
    resolved.replace(QString::fromUtf8("\uFF5BInputName}"), "{InputName}");
    resolved.replace(QString::fromUtf8("{InputName\uFF5D"), "{InputName}");

    if (resolved.contains("{InputName}", Qt::CaseInsensitive)) {
        QString baseInputName;
        if (!m_manifestPath.isEmpty()) {
            QFileInfo fi(m_manifestPath);
            QString parentDirName = QFileInfo(fi.absolutePath()).fileName();
            if (parentDirName.endsWith(".SAFE", Qt::CaseInsensitive)) {
                baseInputName = parentDirName.left(parentDirName.length() - 5);
            } else {
                baseInputName = parentDirName;
            }
        }
        if (baseInputName.isEmpty()) {
            baseInputName = "S1_Image";
        }
        resolved.replace("{InputName}", baseInputName, Qt::CaseInsensitive);
    }
    return resolved;
}

QString Sentinel1ImportNode::getImportedFilePath() const
{
    if (m_importedFilePath.isEmpty())
    {
        QString outputNodeName = getOutputNodeName();
        QString fileName = getOutputFileName();
        if (fileName.isEmpty()) {
            QString subswath = m_subswathCombo ? m_subswathCombo->currentText() : m_subswath;
            QString pol = m_polarizationCombo ? m_polarizationCombo->currentText() : m_polarization;
            fileName = subswath + "_" + pol;
        }
        return QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(fileName);
    }
    return m_importedFilePath;
}

QString Sentinel1ImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName.trimmed();
    if (name.isEmpty())
    {
        return "S1_Import";
    }
    return resolveInputName(name);
}

QStringList Sentinel1ImportNode::previewImagePaths() const
{
    if (!m_importedFilePath.isEmpty()) {
        QFileInfo fi(m_importedFilePath);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        
        // 自愈：如果 JPG 丢失了，但 H5 还存在，则静默重建
        if (!QFileInfo::exists(jpgPath) && QFileInfo::exists(m_importedFilePath)) {
            NodeUtils::generateJpgPreviewFromH5(m_importedFilePath, jpgPath, "complex");
        }
        
        if (QFileInfo::exists(jpgPath)) {
            return QStringList() << jpgPath;
        }
    }
    return QStringList();
}

QString Sentinel1ImportNode::getOutputFileName() const
{
    QString fileName = m_outputFileNameEdit ? m_outputFileNameEdit->text().trimmed() : m_outputFileName.trimmed();
    if (fileName.isEmpty())
    {
        return generateOutputFileName();
    }
    return resolveInputName(fileName);
}

QString Sentinel1ImportNode::generateOutputFileName() const
{
    QFileInfo fileInfo(m_manifestPath);
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

void Sentinel1ImportNode::updateAvailableParameters(const QString& manifestPath)
{
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

void Sentinel1ImportNode::onManifestBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择哨兵一号清单文件"),
        QFileInfo(m_manifestPath).absolutePath(),
        tr("清单文件 (manifest.safe);;所有文件 (*)")
    );

    if (!filePath.isEmpty())
    {
        m_manifestEdit->setText(filePath);
        m_manifestPath = filePath;

        updateAvailableParameters(filePath);

        QString autoName = generateOutputFileName();
        if (!autoName.isEmpty() && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(autoName);
        }
        if (!autoName.isEmpty() && m_outputFileNameEdit->text().isEmpty())
        {
            m_outputFileNameEdit->setText(autoName);
        }
    }
}

void Sentinel1ImportNode::onPodBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择精轨文件"),
        QFileInfo(m_podPath).absolutePath(),
        tr("精轨文件 (*.EOF *.eofs);;所有文件 (*)")
    );

    if (!filePath.isEmpty())
    {
        m_podEdit->setText(filePath);
    }
}

void Sentinel1ImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void Sentinel1ImportNode::onImportFinished()
{
    QString outputPath = projectPath() + "/" + getOutputNodeName() + "/" + m_outputFileName + ".h5";
    m_importedFilePath = outputPath;

    ImportNodeBase::onImportFinished();

    // 双路输出：Port 1 预览输出
    if (!m_importedFilePath.isEmpty())
    {
        QFileInfo fi(m_importedFilePath);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        m_imageInfoData = std::make_shared<ImageInfoData>(jpgPath);
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

void Sentinel1ImportNode::onThreadError(const QString& error)
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

void Sentinel1ImportNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ImportNodeBase::setExecutionMode(mode);
    
    // If switching from Manual to Automatic and we have valid data, we could trigger execution
    // But for import nodes, automatic mode usually doesn't make sense since it needs user selection
}

void Sentinel1ImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    if (auto* iface = getProjectContext()) {
        iface->refreshProjectTree();
    }
}

QJsonObject Sentinel1ImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["manifestPath"] = m_manifestPath;
    json["podPath"] = m_podPath;
    json["subswath"] = m_subswathCombo ? m_subswathCombo->currentText() : m_subswath;
    json["polarization"] = m_polarizationCombo ? m_polarizationCombo->currentText() : m_polarization;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    json["outputFileName"] = m_outputFileName;
    return json;
}

void Sentinel1ImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_manifestPath = json["manifestPath"].toString();
    m_podPath = json["podPath"].toString();
    m_outputNodeName = json["outputNodeName"].toString();
    m_outputFileName = json["outputFileName"].toString();
    m_subswath = json["subswath"].toString("iw1");
    m_polarization = json["polarization"].toString("vv");

    ExecutableNodeDelegateModel::load(json);

    if (!m_manifestPath.isEmpty()) {
        updateAvailableParameters(m_manifestPath);
    }

    if (m_manifestEdit) m_manifestEdit->setText(m_manifestPath);
    if (m_podEdit) m_podEdit->setText(m_podPath);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setText(m_outputFileName);

    if (m_subswathCombo) {
        int idx = m_subswathCombo->findText(m_subswath);
        if (idx >= 0) m_subswathCombo->setCurrentIndex(idx);
    }

    if (m_polarizationCombo) {
        int idx = m_polarizationCombo->findText(m_polarization);
        if (idx >= 0) m_polarizationCombo->setCurrentIndex(idx);
    }
}

bool Sentinel1ImportNode::validateAndRestoreOutput()
{
    QString nodeName = getOutputNodeName();
    if (nodeName.isEmpty())
        return false;

    QString fileName = getOutputFileName();
    if (fileName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/" + fileName + ".h5";

    if (QFile::exists(outputPath)) {
        m_importedFilePath = outputPath;
        auto outputData = std::make_shared<ImportedFileData>(outputPath, nodeName);
        setOutputData(0, outputData);
        Q_EMIT dataUpdated(0);

        // 双路输出：Port 1 预览输出
        QFileInfo fi(outputPath);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        m_imageInfoData = std::make_shared<ImageInfoData>(jpgPath);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);

        return true;
    }

    return false;
}

unsigned int Sentinel1ImportNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 0;
    else
        return 2;
}

NodeDataType Sentinel1ImportNode::dataType(PortType portType, PortIndex portIndex) const
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

bool Sentinel1ImportNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString Sentinel1ImportNode::portCaption(PortType portType, PortIndex portIndex) const
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

bool Sentinel1ImportNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> Sentinel1ImportNode::outData(PortIndex port)
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
