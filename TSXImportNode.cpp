#include "InSARLogManager.h"

#include "TSXImportNode.h"
#include "TSXImportWorker.h"
#include "ImportDataTypes.h"
#include "NodeUtils.h"
#include <QFile>
#include <QFileInfo>

namespace QtNodes {

TSXImportNode::TSXImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_xmlEdit(nullptr)
    , m_polarizationCombo(nullptr)
    , m_projectLabel(nullptr)
    , m_xmlPath()
    , m_importedFilePath()
    , m_outputFileName()
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
}

TSXImportNode::~TSXImportNode()
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

QWidget* TSXImportNode::createWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // Project Name Badge
    m_projectLabel = createProjectBadge(projectName());
    layout->addWidget(m_projectLabel);

    // XML file row: Label:LineEdit:Button
    auto* xmlRow = new QHBoxLayout();
    xmlRow->addWidget(new QLabel("TSX/TDX图像（.xml）："));
    m_xmlEdit = new QLineEdit();
    connect(m_xmlEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() { 
        QString text = m_xmlEdit->text();
        if (m_xmlPath != text) {
            if (!confirmParameterChange()) {
                m_xmlEdit->setText(m_xmlPath);
                return;
            }
            m_xmlPath = text; 
            invalidateNodeData();
        }
    });
    QPushButton* xmlBrowse = new QPushButton("浏览...");
    xmlRow->addWidget(m_xmlEdit);
    xmlRow->addWidget(xmlBrowse);
    layout->addLayout(xmlRow);



    // Target node row [3:7]
    auto* nodeRow = new QHBoxLayout();
    nodeRow->setStretch(0, 3);
    nodeRow->setStretch(1, 7);
    nodeRow->addWidget(new QLabel("目标节点："));
    m_outputNodeNameEdit = new QLineEdit();
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
    layout->addLayout(nodeRow);

    // Output filename row [3:7]
    auto* filenameRow = new QHBoxLayout();
    filenameRow->setStretch(0, 3);
    filenameRow->setStretch(1, 7);
    filenameRow->addWidget(new QLabel("目标文件名："));
    m_outputFileNameEdit = new QLineEdit();
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
    filenameRow->addWidget(m_outputFileNameEdit);
    layout->addLayout(filenameRow);

    // Polarization row [3:7]
    auto* polRow = new QHBoxLayout();
    polRow->setStretch(0, 3);
    polRow->setStretch(1, 7);
    polRow->addWidget(new QLabel("极化方式："));
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItem("HH");
    m_polarizationCombo->addItem("VV");
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
    layout->addLayout(polRow);

    // Connect signals
    connect(xmlBrowse, &QPushButton::clicked, this, &TSXImportNode::onXmlBrowseClicked);

    return widget;
}

void TSXImportNode::executeImport()
{
    m_xmlPath = m_xmlEdit->text().trimmed();
    if (m_xmlPath.isEmpty())
    {
        onError("请选择一个 XML 文件。");
        return;
    }

    if (!QFileInfo::exists(m_xmlPath))
    {
        onError("XML 文件不存在：" + m_xmlPath);
        return;
    }

    // Get output filename from edit box, or generate if empty
    m_outputFileName = m_outputFileNameEdit->text().trimmed();
    if (m_outputFileName.isEmpty())
    {
        m_outputFileName = generateOutputFileName();
        if (m_outputFileName.isEmpty())
        {
            onError("无法从 XML 文件生成输出文件名。");
            return;
        }
        m_outputFileNameEdit->setText(m_outputFileName);
    }

    if (m_outputFileName.isEmpty())
    {
        onError("无法从 XML 文件生成输出文件名。");
        return;
    }

    QString outputNodeName = getOutputNodeName();
    QString outputPath = projectPath() + "/" + outputNodeName + "/" + m_outputFileName + ".h5";
    QString previewPath = projectPath() + "/" + outputNodeName + "/" + m_outputFileName + ".jpg";

    auto overwriteRes = NodeUtils::checkAndPromptOverwrite(getProjectContext(), outputNodeName, {outputPath, previewPath}, nullptr);
    if (overwriteRes == NodeUtils::OverwriteResult::Cancel) {
        setState(ExecutionState::Idle);
        return;
    } else if (overwriteRes == NodeUtils::OverwriteResult::LoadExisting) {
        setProgress(100);
        onImportFinished();
        return;
    }

    m_thread = new QThread(this);
    m_workerThread = new TSXImportWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &TSXImportNode::startTSXImport,
            m_workerThread, &TSXImportWorker::import_TSX);
    connect(m_workerThread, &TSXImportWorker::updateProcess,
            this, &TSXImportNode::onImportProgress);
    connect(m_workerThread, &TSXImportWorker::endProcess,
            this, &TSXImportNode::onImportFinished);
    connect(m_workerThread, &TSXImportWorker::errorProcess,
            this, &TSXImportNode::onThreadError);
    connect(m_workerThread, &TSXImportWorker::sendModel,
            this, &TSXImportNode::onModelUpdated);

    m_thread->start();

    QString polarization = m_polarizationCombo->currentText();
    outputNodeName = getOutputNodeName();

    Q_EMIT startTSXImport(
        polarization,
        m_xmlPath,
        projectPath(),
        outputNodeName,
        m_outputFileName,
        projectName(),
        projectModel()
    );
}

QStringList TSXImportNode::getImportedFilePaths() const
{
    QStringList paths;
    if (!m_importedFilePath.isEmpty()) {
        paths.append(m_importedFilePath);
    } else {
        QString outputNodeName = getOutputNodeName();
        paths.append(QString("%1/%2/%3.h5").arg(projectPath()).arg(outputNodeName).arg(m_outputFileName));
    }
    return paths;
}

QString TSXImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        return generateOutputFileName();
    }
    return name;
}

QString TSXImportNode::generateOutputFileName() const
{
    QFileInfo fileInfo(m_xmlPath);
    QString baseName = fileInfo.baseName();

    // Find the last "T" in the filename and extract 8 characters before it
    int pos = baseName.lastIndexOf('T');
    if (pos >= 8)
    {
        return baseName.mid(pos - 8, 8);
    }

    return QString();
}

void TSXImportNode::onXmlBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("导入 TerraSAR-X/TanDEM-X 数据"),
        QFileInfo(m_xmlPath).absolutePath(),
        tr("XML 文件 (*.xml)")
    );

    if (!filePath.isEmpty())
    {
        m_xmlEdit->setText(filePath);

        QString autoName = generateOutputFileName();
        if (!autoName.isEmpty() && m_outputFileNameEdit->text().isEmpty())
        {
            m_outputFileNameEdit->setText(autoName);
        }
    }
}

void TSXImportNode::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void TSXImportNode::onImportFinished()
{
    QString outputPath = projectPath() + "/" + getOutputNodeName() + "/" + m_outputFileName + ".h5";
    m_importedFilePath = outputPath;

    ImportNodeBase::onImportFinished();

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

void TSXImportNode::onThreadError(const QString& error)
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

void TSXImportNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = executionMode();
    ImportNodeBase::setExecutionMode(mode);
}

void TSXImportNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

QJsonObject TSXImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["xmlPath"] = m_xmlPath;
    json["polarization"] = m_polarization;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    json["outputFileName"] = m_outputFileName;
    return json;
}

void TSXImportNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    m_xmlPath = json["xmlPath"].toString();
    m_outputNodeName = json["outputNodeName"].toString();
    m_outputFileName = json["outputFileName"].toString();

    ExecutableNodeDelegateModel::load(json);

    if (m_xmlEdit) m_xmlEdit->setText(m_xmlPath);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setText(m_outputFileName);

    m_polarization = json["polarization"].toString("HH");
    if (m_polarizationCombo) {
        int idx = m_polarizationCombo->findText(m_polarization);
        if (idx >= 0) m_polarizationCombo->setCurrentIndex(idx);
    }
}

bool TSXImportNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty())
        return false;

    QString fileName = m_outputFileName;
    if (fileName.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + nodeName + "/" + fileName + ".h5";

    if (QFile::exists(outputPath)) {
        m_importedFilePath = outputPath;
        auto outputData = std::make_shared<ImportedFileData>(outputPath, nodeName);
        setOutputData(0, outputData);
        return true;
    }

    return false;
}

void TSXImportNode::stopExecution()
{
    ImportNodeBase::stopExecution();
    if (m_workerThread) {
        m_workerThread->StopProcess();
    }
}

} // namespace QtNodes
