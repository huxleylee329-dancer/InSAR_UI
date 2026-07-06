#include "DEMSourceNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QFileDialog>
#include <QDirIterator>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

DEMSourceNode::DEMSourceNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_demSourceCombo(nullptr)
    , m_resolutionCombo(nullptr)
    , m_customResEdit(nullptr)
    , m_cacheDirEdit(nullptr)
    , m_browseCacheBtn(nullptr)
    , m_clearCacheBtn(nullptr)
    , m_cacheSizeLabel(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_demSource(0)
    , m_resMode(0)
    , m_customResolution(30.0)
    , m_cacheDir("")
    , m_outputNodeName("")
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
    
    // 初始化默认缓存目录为全局共享目录 D:\SRC\insar_ui\bin\dem
    QString appDir = QCoreApplication::applicationDirPath();
    m_cacheDir = QDir::toNativeSeparators(appDir + "/dem");
}

DEMSourceNode::~DEMSourceNode()
{
    stopExecution();
}

unsigned int DEMSourceNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType DEMSourceNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "Imported File"};
    else
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool DEMSourceNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString DEMSourceNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入影像");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool DEMSourceNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void DEMSourceNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_imageInfoData.reset();
    }
}

std::shared_ptr<NodeData> DEMSourceNode::outData(PortIndex port)
{
    if (port == 0)
        return m_outputData;
    else
        return m_imageInfoData;
}

::QWidget* DEMSourceNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject DEMSourceNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["demSource"] = m_demSource;
    modelJson["resMode"] = m_resMode;
    modelJson["customResolution"] = m_customResolution;
    modelJson["cacheDir"] = m_cacheDirEdit ? m_cacheDirEdit->text() : m_cacheDir;

    return modelJson;
}

void DEMSourceNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vSource = json["demSource"];
    if (!vSource.isUndefined()) m_demSource = vSource.toInt();

    QJsonValue vResMode = json["resMode"];
    if (!vResMode.isUndefined()) m_resMode = vResMode.toInt();

    QJsonValue vCustomRes = json["customResolution"];
    if (!vCustomRes.isUndefined()) m_customResolution = vCustomRes.toDouble();

    QJsonValue vCache = json["cacheDir"];
    if (!vCache.isUndefined()) m_cacheDir = vCache.toString();

    // 先加载基础节点，然后再刷新 UI
    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_demSourceCombo) m_demSourceCombo->setCurrentIndex(m_demSource);
    if (m_resolutionCombo) m_resolutionCombo->setCurrentIndex(m_resMode);
    if (m_customResEdit) m_customResEdit->setText(QString::number(m_customResolution));
    if (m_cacheDirEdit) m_cacheDirEdit->setText(m_cacheDir);

    onResolutionModeChanged(m_resMode);
    updateCacheSizeLabel();
}

void DEMSourceNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void DEMSourceNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene);
        }
    };

    const int labelWidth = 90;

    // 1. DEM 数据源
    auto* sourceLayout = new QHBoxLayout();
    QLabel* sourceLabel = new QLabel(QStringLiteral("DEM 数据源"));
    sourceLabel->setFixedWidth(labelWidth);
    sourceLayout->addWidget(sourceLabel);
    m_demSourceCombo = new QComboBox();
    m_demSourceCombo->addItem("SRTM 1\" (~30m)");
    m_demSourceCombo->addItem("SRTM 3\" (~90m)");
    m_demSourceCombo->addItem("Copernicus DEM (30m)");
    m_demSourceCombo->addItem("ASTER GDEM v3 (30m)");
    m_demSourceCombo->setCurrentIndex(m_demSource);
    connect(m_demSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        if (m_demSource != index) {
            if (!confirmParameterChange()) {
                m_demSourceCombo->blockSignals(true);
                m_demSourceCombo->setCurrentIndex(m_demSource);
                m_demSourceCombo->blockSignals(false);
                return;
            }
            m_demSource = index;
            invalidateNodeData();
        }
    });
    sourceLayout->addWidget(m_demSourceCombo);
    layout->addLayout(sourceLayout);

    // 2. 目标分辨率模式
    auto* resLayout = new QHBoxLayout();
    QLabel* resLabel = new QLabel(QStringLiteral("目标分辨率"));
    resLabel->setFixedWidth(labelWidth);
    resLayout->addWidget(resLabel);
    m_resolutionCombo = new QComboBox();
    m_resolutionCombo->addItem(QStringLiteral("原始分辨率"));
    m_resolutionCombo->addItem("30 米");
    m_resolutionCombo->addItem("90 米");
    m_resolutionCombo->addItem(QStringLiteral("自定义"));
    m_resolutionCombo->setCurrentIndex(m_resMode);
    connect(m_resolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        if (m_resMode != index) {
            if (!confirmParameterChange()) {
                m_resolutionCombo->blockSignals(true);
                m_resolutionCombo->setCurrentIndex(m_resMode);
                m_resolutionCombo->blockSignals(false);
                return;
            }
            m_resMode = index;
            onResolutionModeChanged(index);
            invalidateNodeData();
        }
    });
    resLayout->addWidget(m_resolutionCombo);
    layout->addLayout(resLayout);

    // 3. 自定义分辨率输入
    auto* customLayout = new QHBoxLayout();
    QLabel* customLabel = new QLabel(QStringLiteral("分辨率(米)"));
    customLabel->setFixedWidth(labelWidth);
    customLayout->addWidget(customLabel);
    m_customResEdit = new QLineEdit();
    m_customResEdit->setText(QString::number(m_customResolution));
    connect(m_customResEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        bool ok = false;
        double val = m_customResEdit->text().toDouble(&ok);
        if (ok && val > 0.0 && m_customResolution != val) {
            if (!confirmParameterChange()) {
                m_customResEdit->setText(QString::number(m_customResolution));
                return;
            }
            m_customResolution = val;
            invalidateNodeData();
        } else if (!ok || val <= 0.0) {
            QMessageBox::warning(nullptr, "Warning", QStringLiteral("分辨率必须为正数！"));
            m_customResEdit->setText(QString::number(m_customResolution));
        }
    });
    customLayout->addWidget(m_customResEdit);
    layout->addLayout(customLayout);

    // 4. 缓存目录及浏览
    auto* cacheLayout = new QHBoxLayout();
    QLabel* cacheLabel = new QLabel(QStringLiteral("缓存目录"));
    cacheLabel->setFixedWidth(labelWidth);
    cacheLayout->addWidget(cacheLabel);
    
    m_cacheDirEdit = new QLineEdit();
    m_cacheDirEdit->setText(m_cacheDir);
    m_cacheDirEdit->setToolTip(QStringLiteral("默认指向软件全局共享 dem 目录。可修改为工程局部目录以便打包工程。"));
    connect(m_cacheDirEdit, &QLineEdit::editingFinished, this, [this]() {
        QString dir = m_cacheDirEdit->text();
        if (m_cacheDir != dir) {
            m_cacheDir = dir;
            updateCacheSizeLabel();
        }
    });
    cacheLayout->addWidget(m_cacheDirEdit);

    m_browseCacheBtn = new QPushButton("...");
    m_browseCacheBtn->setFixedWidth(30);
    connect(m_browseCacheBtn, &QPushButton::clicked, this, [this]() {
        QString selectedDir = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("选择缓存目录"), m_cacheDirEdit->text());
        if (!selectedDir.isEmpty()) {
            m_cacheDir = QDir::toNativeSeparators(selectedDir);
            m_cacheDirEdit->setText(m_cacheDir);
            updateCacheSizeLabel();
        }
    });
    cacheLayout->addWidget(m_browseCacheBtn);
    layout->addLayout(cacheLayout);

    // 5. 缓存大小与清理
    auto* clearLayout = new QHBoxLayout();
    m_cacheSizeLabel = new QLabel("0.00 MB");
    clearLayout->addWidget(m_cacheSizeLabel);
    
    m_clearCacheBtn = new QPushButton(QStringLiteral("清理缓存"));
    connect(m_clearCacheBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(nullptr, QStringLiteral("确认"), QStringLiteral("确定要清空该目录下的所有 DEM 缓存文件吗？")) == QMessageBox::Yes) {
            QDir dir(m_cacheDir);
            if (dir.exists()) {
                dir.removeRecursively();
                dir.mkpath(m_cacheDir);
                updateCacheSizeLabel();
            }
        }
    });
    clearLayout->addWidget(m_clearCacheBtn);
    layout->addLayout(clearLayout);

    // 6. 目标节点名
    auto* outputLayout = new QHBoxLayout();
    QLabel* outputLabel = new QLabel(QStringLiteral("目标节点"));
    outputLabel->setFixedWidth(labelWidth);
    outputLayout->addWidget(outputLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (!name.isEmpty() && m_outputNodeName != name) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            m_outputNodeName = name;
            invalidateNodeData();
        }
    });
    outputLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(outputLayout);

    // 初始化控件状态
    onResolutionModeChanged(m_resMode);
    updateCacheSizeLabel();
}

void DEMSourceNode::onResolutionModeChanged(int index)
{
    if (m_customResEdit) {
        m_customResEdit->setEnabled(index == 3);
    }
}

void DEMSourceNode::updateCacheSizeLabel()
{
    if (!m_cacheSizeLabel) return;
    
    double sizeMB = 0;
    QDir dir(m_cacheDir);
    if (dir.exists()) {
        QDirIterator it(m_cacheDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            sizeMB += it.fileInfo().size();
        }
    }
    sizeMB /= (1024.0 * 1024.0);
    m_cacheSizeLabel->setText(QString("%1 MB").arg(sizeMB, 0, 'f', 2));
}

QString DEMSourceNode::generateDefaultOutputName() const
{
    if (m_inputData && !m_inputData->nodeName().isEmpty()) {
        return m_inputData->nodeName() + "_ExternalDEM";
    }
    return "ExternalDEM";
}

bool DEMSourceNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;
    
    QString cache = m_cacheDirEdit ? m_cacheDirEdit->text().trimmed() : m_cacheDir.trimmed();
    if (cache.isEmpty())
        return false;

    QString dst = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName.trimmed();
    if (dst.isEmpty())
        return false;

    return true;
}

bool DEMSourceNode::prepareToStart()
{
    if (!validateInputs()) {
        return false;
    }

    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    
    m_preparedDstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName.trimmed();
    m_preparedSource = m_demSourceCombo ? m_demSourceCombo->currentIndex() : m_demSource;
    m_preparedCacheDir = m_cacheDirEdit ? m_cacheDirEdit->text().trimmed() : m_cacheDir;

    int resIdx = m_resolutionCombo ? m_resolutionCombo->currentIndex() : m_resMode;
    if (resIdx == 0) m_preparedResolution = 0.0;
    else if (resIdx == 1) m_preparedResolution = 30.0;
    else if (resIdx == 2) m_preparedResolution = 90.0;
    else m_preparedResolution = m_customResEdit ? m_customResEdit->text().toDouble() : m_customResolution;

    // Overwrite check
    QString targetH5Dir = QFileInfo(m_preparedSavePath).absolutePath() + "/" + m_preparedDstNode;
    QString targetH5 = targetH5Dir + "/" + m_preparedDstNode + "_dem.h5";
    
    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    if (QFile::exists(targetH5))
    {
        if (_isAutoTriggered)
        {
            m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        }
        else
        {
            auto iface = NodeUtils::getProjectContext(_widget);
            m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(iface, m_preparedDstNode, QStringList() << targetH5, nullptr);
            if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel)
            {
                return false;
            }
        }
    }
    
    return true;
}

void DEMSourceNode::execute()
{
    executeProcessing();
}

void DEMSourceNode::stopExecution()
{
    if (m_workerThread && m_thread)
    {
        m_workerThread->StopProcess();
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void DEMSourceNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

void DEMSourceNode::executeProcessing()
{
    if (m_workerThread || m_thread) {
        return;
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        invalidateExecution();
        return;
    }

    QString savePath = QFileInfo(m_preparedSavePath).absolutePath();
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite) {
        QDir oldDir(savePath + "/" + m_preparedDstNode);
        if (oldDir.exists()) {
            oldDir.removeRecursively();
        }
        QDir().mkpath(savePath + "/" + m_preparedDstNode);
        auto iface = NodeUtils::getProjectContext(_widget);
        if (iface) {
            NodeUtils::removeDataNodeFromProject(iface, m_preparedDstNode);
        }
    }

    m_workerThread = new DEMSourceWorker();
    m_thread = new QThread();
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    connect(m_workerThread, &DEMSourceWorker::updateProcess, this, &DEMSourceNode::onProgressUpdate);
    connect(m_workerThread, &DEMSourceWorker::errorProcess, this, &DEMSourceNode::onError);
    connect(m_workerThread, &DEMSourceWorker::endProcess, this, &DEMSourceNode::onProcessingFinished);
    connect(m_workerThread, &DEMSourceWorker::sendModel, this, &DEMSourceNode::onModelUpdated);

    connect(this, &DEMSourceNode::startDemFetch, m_workerThread, &DEMSourceWorker::fetch_dem);

    m_thread->start();
    setState(ExecutionState::Running);

    emit startDemFetch(
        m_preparedSavePath,
        m_preparedProjectName,
        m_preparedDstNode,
        m_inputData->filePaths(),
        m_preparedSource,
        m_preparedResolution,
        m_preparedCacheDir,
        projectModel()
    );
}

void DEMSourceNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void DEMSourceNode::onError(const QString& error)
{
    stopExecution();
    m_workerThread = nullptr;
    m_thread = nullptr;

    setState(ExecutionState::Error);
    QMessageBox::critical(nullptr, "Error", error);
}

void DEMSourceNode::onProcessingFinished()
{
    stopExecution();
    m_workerThread = nullptr;
    m_thread = nullptr;

    QString savePath = QFileInfo(m_preparedSavePath).absolutePath();
    QString h5Path = savePath + "/" + m_preparedDstNode + "/" + m_preparedDstNode + "_dem.h5";
    QString jpgPath = savePath + "/" + m_preparedDstNode + "/" + m_preparedDstNode + "_dem.jpg";

    m_outputData = std::make_shared<ImportedFileData>(h5Path, m_preparedDstNode);
    m_imageInfoData = std::make_shared<ImageInfoData>(jpgPath);

    // 异步生成预览图
    m_remedyWatcher.setFuture(QtConcurrent::run([=]() {
        NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "dem");
    }));

    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
        setState(ExecutionState::Completed);
        updateCacheSizeLabel();
    });
}

void DEMSourceNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

bool DEMSourceNode::validateAndRestoreOutput()
{
    QString savePath = QFileInfo(projectPath()).absolutePath();
    QString name = m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName;
    QString targetH5 = savePath + "/" + name + "/" + name + "_dem.h5";
    QString targetJpg = savePath + "/" + name + "/" + name + "_dem.jpg";

    if (QFile::exists(targetH5)) {
        m_outputData = std::make_shared<ImportedFileData>(targetH5, name);
        m_imageInfoData = std::make_shared<ImageInfoData>(targetJpg);

        if (!QFile::exists(targetJpg)) {
            // 后台异步生成缺失的预览图
            m_remedyWatcher.cancel();
            m_remedyWatcher.waitForFinished();
            m_remedyWatcher.disconnect();

            connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this]() {
                Q_EMIT dataUpdated(1);
            });

            m_remedyWatcher.setFuture(QtConcurrent::run([=]() {
                NodeUtils::generateJpgPreviewFromH5(targetH5, targetJpg, "dem");
            }));
        }

        setState(ExecutionState::Completed);
        updateCacheSizeLabel();

        Q_EMIT dataUpdated(0);
        if (QFile::exists(targetJpg)) {
            Q_EMIT dataUpdated(1);
        }
        return true;
    }

    setState(ExecutionState::Idle);
    return false;
}

QStringList DEMSourceNode::previewImagePaths() const
{
    QStringList list;
    if (m_imageInfoData && !m_imageInfoData->filePath().isEmpty()) {
        list.append(m_imageInfoData->filePath());
    }
    return list;
}

void DEMSourceNode::updateWidgetSize()
{
    if (_widget) {
        _widget->adjustSize();
    }
}

QStandardItemModel* DEMSourceNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString DEMSourceNode::projectPath() const
{
    IApplicationInterface* iface = nullptr;
    if (_widget) iface = NodeUtils::getProjectContext(_widget);
    if (!iface) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* mainWin = qobject_cast<MainWindow*>(w)) {
                if (mainWin->workspaceUI()) { iface = mainWin->workspaceUI(); break; }
                if (mainWin->interfaceManager()) { iface = mainWin->interfaceManager()->currentInterface(); if (iface) break; }
            }
        }
    }
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString DEMSourceNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* DEMSourceNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

} // namespace QtNodes
