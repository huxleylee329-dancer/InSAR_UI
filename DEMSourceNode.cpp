#include "DEMSourceNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include "EarthdataLoginDialog.h"
#include <QSettings>
#include <gdal_priv.h>
#include "Utils.h"
#include "QtNodes/internal/NodeDetailWindow.hpp"


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
    
    // 初始化默认缓存目录，优先使用项目的全局默认高程数据路径
    auto* iface = NodeUtils::getProjectContext(nullptr);
    if (iface) {
        m_cacheDir = NodeUtils::getGlobalDemPath(iface);
    }
}

DEMSourceNode::~DEMSourceNode()
{
    if (m_workerThread) {
        m_workerThread->StopProcess();
    }
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
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
            return NodeDataType{"dem_file", "DEM File"};
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
    if (executionState() != ExecutionState::Completed)
        return nullptr;

    std::shared_ptr<NodeData> data;
    if (port == 0) {
        data = m_outputData;
    } else {
        data = m_imageInfoData;
    }
    return data;
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
    // 动态确定默认缓存目录，避免使用运行目录下的 dem，统一使用工程路径下的 .dem_cache
    if (m_cacheDir.isEmpty())
    {
        QString projPath = projectPath();
        if (!projPath.isEmpty())
        {
            m_cacheDir = QDir::toNativeSeparators(projPath + "/.dem_cache");
        }
    }

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

    // 账户状态与登录注销按钮
    auto* loginLayout = new QHBoxLayout();
    QLabel* loginLabel = new QLabel(QStringLiteral("账户状态"));
    loginLabel->setFixedWidth(labelWidth);
    loginLayout->addWidget(loginLabel);

    m_loginStatusLabel = new QLabel();
    m_loginBtn = new QPushButton(QStringLiteral("登录"));
    m_loginBtn->setFixedWidth(50);
    m_logoutBtn = new QPushButton(QStringLiteral("注销"));
    m_logoutBtn->setFixedWidth(50);

    loginLayout->addWidget(m_loginStatusLabel);
    loginLayout->addWidget(m_loginBtn);
    loginLayout->addWidget(m_logoutBtn);
    layout->addLayout(loginLayout);

    connect(m_demSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DEMSourceNode::updateLoginStatus);
    connect(m_loginBtn, &QPushButton::clicked, this, [this]() {
        EarthdataLoginDialog dlg(nullptr);
        if (dlg.exec() == QDialog::Accepted) {
            updateLoginStatus();
        }
    });
    connect(m_logoutBtn, &QPushButton::clicked, this, [this]() {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        settings.remove("DEM/EarthdataUser");
        settings.remove("DEM/EarthdataPassword");
        updateLoginStatus();
    });

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
    m_cacheDirEdit->setObjectName("demPathEdit");
    m_cacheDirEdit->setText(m_cacheDir);
    m_cacheDirEdit->setToolTip(QStringLiteral("默认指向软件全局共享 dem 目录。可修改为工程局部目录以便打包工程。"));
    connect(m_cacheDirEdit, &QLineEdit::editingFinished, this, [this]() {
        QString dir = m_cacheDirEdit->text().trimmed();
        if (m_cacheDir != dir) {
            m_cacheDir = dir;
            updateCacheSizeLabel();
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_cacheDir, true);
            }
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
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_cacheDir, true);
            }
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
    updateLoginStatus();
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

    // 检查 NASA Earthdata 登录状态（如果选择的源非 Copernicus 且未登录）
    if (m_preparedSource != 2)
    {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
        QString encryptedPass = settings.value("DEM/EarthdataPassword", "").toString();
        if (encryptedUser.isEmpty() || encryptedPass.isEmpty())
        {
            if (_isAutoTriggered)
            {
                InSARLogManager::LogError("DEMSourceNode", "NASA Earthdata login required but credentials are missing. Skip execution.");
                return false;
            }
            else
            {
                QMessageBox::warning(nullptr, "Warning", QStringLiteral("所选 DEM 数据源需要 NASA Earthdata 账户登录，请先登录！"));
                EarthdataLoginDialog dlg(nullptr);
                if (dlg.exec() != QDialog::Accepted)
                {
                    return false;
                }
                updateLoginStatus();
            }
        }
    }

    int resIdx = m_resolutionCombo ? m_resolutionCombo->currentIndex() : m_resMode;
    if (resIdx == 0) m_preparedResolution = 0.0;
    else if (resIdx == 1) m_preparedResolution = 30.0;
    else if (resIdx == 2) m_preparedResolution = 90.0;
    else m_preparedResolution = m_customResEdit ? m_customResEdit->text().toDouble() : m_customResolution;

    // Overwrite check
    QString targetH5Dir = m_preparedSavePath + "/" + m_preparedDstNode;
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
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
}

void DEMSourceNode::processAutomatically()
{
    if (m_workerThread || m_thread) {
        deferAutomaticCompletion();
        return;
    }
    if (prepareToStart()) {
        executeProcessing();
        deferAutomaticCompletion();
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

    QString savePath = m_preparedSavePath;
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
    connect(m_workerThread, &DEMSourceWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &DEMSourceWorker::cancelled, this, &DEMSourceNode::onCancelled);
    connect(m_workerThread, &DEMSourceWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &DEMSourceWorker::demFetchFinished, this, &DEMSourceNode::onProcessingFinished);
    connect(m_workerThread, &DEMSourceWorker::demFetchFinished, m_thread, &QThread::quit);
    connect(m_workerThread, &DEMSourceWorker::sendModel, this, &DEMSourceNode::onModelUpdated);

    connect(this, &DEMSourceNode::startDemFetch, m_workerThread, &DEMSourceWorker::fetch_dem);

    m_thread->start();
    setState(ExecutionState::Running);
    deferAutomaticCompletion();

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
    m_workerThread = nullptr;
    m_thread = nullptr;

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    setLastErrorMessage(error);
    setState(ExecutionState::Error);
    InSARLogManager::LogError("DEMSourceNode", "Execution failed: " + error);
    Q_EMIT executionError(error);
}

void DEMSourceNode::onProcessingFinished(
    const QString& outputH5Path,
    const QString& dstNode,
    const QString& projectName,
    int demSource,
    double targetResolution
)
{
    m_workerThread = nullptr;
    m_thread = nullptr;

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    QString h5Path = outputH5Path;
    QString tifPath = h5Path.left(h5Path.lastIndexOf('.')) + ".tif";
    QString jpgPath = h5Path.left(h5Path.lastIndexOf('.')) + ".jpg";

    m_outputData = std::make_shared<DEMFileData>(tifPath, dstNode);
    // 先将预览重置，待 finished 回调确认生成成功后再加载，杜绝不存在的 JPG 路径发布给下游
    m_imageInfoData.reset();
    setOutputData(0, m_outputData);
    setOutputData(1, nullptr);

    // 主线程更新全局 XML 并保存
    XMLFile* xml = projectXml();
    if (xml)
    {
        std::string srcName = "SRTM1";
        if (demSource == 1) srcName = "SRTM3";
        else if (demSource == 2) srcName = "Copernicus";
        else if (demSource == 3) srcName = "ASTER";

        QString outputH5Name = QFileInfo(h5Path).fileName();
        xml->XMLFile_add_dem(
            dstNode.toStdString().c_str(), 
            (dstNode + "_dem").toStdString().c_str(),
            ("/" + dstNode + "/" + outputH5Name).toStdString().c_str(),
            0, 0, srcName.c_str(), targetResolution
        );
        xml->XMLFile_save(NodeUtils::getProjectFilePath(_widget).toStdString().c_str());
    }

    // 主线程挂载项目树 UI
    QStandardItemModel* model = projectModel();
    if (model)
    {
        QStandardItem* project = nullptr;
        QList<QStandardItem*> foundProjects = model->findItems(projectName);
        if (!foundProjects.isEmpty())
        {
            project = foundProjects.first();
        }

        if (project)
        {
            QStandardItem* demNode = nullptr;
            for (int i = 0; i < project->rowCount(); ++i)
            {
                if (project->child(i, 0)->text() == dstNode)
                {
                    demNode = project->child(i, 0);
                    break;
                }
            }

            if (!demNode)
            {
                demNode = new QStandardItem(dstNode);
                demNode->setToolTip(projectName);
                demNode->setIcon(QIcon(FOLDER_ICON));
                int insertIndex = 0;
                for (; insertIndex < project->rowCount(); ++insertIndex)
                {
                    QString t = project->child(insertIndex, 1)->text();
                    if (t == "complex-0.0" || t == "complex-1.0" || t == "complex-2.0" || 
                        t == "phase-1.0" || t == "phase-2.0" || t == "phase-3.0" || t == "dem-1.0")
                    {
                        continue;
                    }
                    break;
                }
                project->insertRow(insertIndex, demNode);
                project->setChild(insertIndex, 1, new QStandardItem("dem-1.0"));
            }

            QStandardItem* itemImg = nullptr;
            QString imgName = dstNode + "_dem";
            for (int j = 0; j < demNode->rowCount(); ++j)
            {
                if (demNode->child(j, 0)->text() == imgName)
                {
                    itemImg = demNode->child(j, 0);
                    break;
                }
            }

            if (!itemImg)
            {
                QStandardItem* image = new QStandardItem(imgName);
                image->setToolTip("dem");
                image->setIcon(QIcon(IMAGEDATA_ICON));
                demNode->appendRow(image);
                demNode->setChild(demNode->rowCount() - 1, 1, new QStandardItem(outputH5Path));
            }
            else
            {
                demNode->setChild(itemImg->row(), 1, new QStandardItem(outputH5Path));
            }
        }
    }

    m_remedyWatcher.disconnect();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
    }

    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, h5Path, jpgPath]() {
        bool jpgExists = QFile::exists(jpgPath) && QFileInfo(jpgPath).size() > 0;
        if (jpgExists) {
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPath);
            setOutputData(1, m_imageInfoData);
        } else {
            m_imageInfoData.reset();
            setOutputData(1, nullptr);
            InSARLogManager::LogWarning("DEMSourceNode", "DEM preview JPG failed to generate: " + jpgPath);
        }

        // TIF 成功但 JPG 失败时状态显示为 Warning，而不是 Completed
        if (!jpgExists) {
            setLastWarningMessage(QStringLiteral("DEM data was generated, but its preview image could not be generated."));
            setState(ExecutionState::Warning);
        } else {
            setState(ExecutionState::Completed);
        }
        setProgress(100);
        Q_EMIT computingFinished();
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
        updateCacheSizeLabel();
        
        auto iface = NodeUtils::getProjectContext(_widget);
        if (iface) {
            iface->refreshProjectTree();
        }
    });

    // 异步生成预览图，已存在则跳过重生成
    m_remedyWatcher.setFuture(QtConcurrent::run([=]() {
        if (QFileInfo::exists(jpgPath) && QFileInfo(jpgPath).size() > 0) {
            return;
        }
        NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "dem");
    }));
}

void DEMSourceNode::onCancelled()
{
    InSARLogManager::LogInfo("DEMSourceNode", "DEM fetch cancellation cleanup completed.");
    m_workerThread = nullptr;
    m_thread = nullptr;

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
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
    QString savePath = projectPath();
    QString name = m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName;
    QString targetH5 = savePath + "/" + name + "/" + name + "_dem.h5";
    QString targetTif = savePath + "/" + name + "/" + name + "_dem.tif";
    QString targetJpg = savePath + "/" + name + "/" + name + "_dem.jpg";



    if (QFile::exists(targetH5)) {
        bool needsTif = !QFile::exists(targetTif);
        bool needsJpg = !QFile::exists(targetJpg);

        if (needsTif || needsJpg) {
            m_remedyWatcher.disconnect();
            if (m_remedyWatcher.isRunning()) {
                m_remedyWatcher.cancel();
                m_remedyWatcher.waitForFinished();
            }

            auto writeTifSuccess = std::make_shared<bool>(true);
            connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, targetTif, targetJpg, name, writeTifSuccess]() {
                bool tifExists = QFile::exists(targetTif) && *writeTifSuccess;
                bool jpgExists = QFile::exists(targetJpg) && QFileInfo(targetJpg).size() > 0;

                if (tifExists) {
                    m_outputData = std::make_shared<DEMFileData>(targetTif, name);
                    setOutputData(0, m_outputData);
                    Q_EMIT dataUpdated(0);
                } else {
                    m_outputData.reset();
                    setOutputData(0, nullptr);
                    Q_EMIT dataUpdated(0);
                }

                if (jpgExists) {
                    m_imageInfoData = std::make_shared<ImageInfoData>(targetJpg);
                    setOutputData(1, m_imageInfoData);
                    Q_EMIT dataUpdated(1);
                } else {
                    m_imageInfoData.reset();
                    setOutputData(1, nullptr);
                    Q_EMIT dataUpdated(1);
                }

                if (executionState() == ExecutionState::Running || executionState() == ExecutionState::Completed) {
                    if (tifExists) {
                        if (!jpgExists) {
                            setLastWarningMessage(QStringLiteral("DEM data was restored, but its preview image could not be generated."));
                            setState(ExecutionState::Warning);
                            InSARLogManager::LogWarning("DEMSourceNode", "DEM recovery finished with warning: JPG preview generation failed.");
                        } else {
                            setState(ExecutionState::Completed);
                        }
                        setProgress(100);
                        Q_EMIT computingFinished();
                    } else {
                        setState(ExecutionState::Error);
                        InSARLogManager::LogError("DEMSourceNode", "TIFF generation failed during background recovery. Target path: " + targetTif);
                    }
                }
            });

            QFuture<void> future = QtConcurrent::run([targetH5, targetTif, targetJpg, needsTif, needsJpg, writeTifSuccess]() {
                if (needsTif) {
                    cv::Mat dem;
                    double min_lon = 0, max_lon = 0, min_lat = 0, max_lat = 0;
                    bool read_success = false;
                    {
                        NodeUtils::Hdf5Locker locker;
                        read_success = (NodeUtils::readMatFromH5(targetH5, "dem", dem) &&
                                        NodeUtils::readScalarFromH5(targetH5, "dem_min_lon", min_lon) &&
                                        NodeUtils::readScalarFromH5(targetH5, "dem_max_lon", max_lon) &&
                                        NodeUtils::readScalarFromH5(targetH5, "dem_min_lat", min_lat) &&
                                        NodeUtils::readScalarFromH5(targetH5, "dem_max_lat", max_lat));
                    }
                    if (read_success) {
                        double res_lon = (max_lon - min_lon) / dem.cols;
                        double res_lat = (max_lat - min_lat) / dem.rows;
                        double new_gt[6] = { min_lon, res_lon, 0.0, max_lat, 0.0, -res_lat };
                        const char* wkt_projection = "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]";
                        if (!NodeUtils::writeDemToTif(targetTif, dem, new_gt, wkt_projection)) {
                            *writeTifSuccess = false;
                        }
                    } else {
                        *writeTifSuccess = false;
                    }
                }
                if (needsJpg) {
                    // 已存在有效 JPG 则跳过重生成
                    if (QFileInfo::exists(targetJpg) && QFileInfo(targetJpg).size() > 0) {
                        return;
                    }
                    NodeUtils::generateJpgPreviewFromH5(targetH5, targetJpg, "dem");
                }
            });
            m_remedyWatcher.setFuture(future);

            if (executionState() != ExecutionState::Running) {
                setState(ExecutionState::Completed);
            }
        } else {
            m_outputData = std::make_shared<DEMFileData>(targetTif, name);
            m_imageInfoData = std::make_shared<ImageInfoData>(targetJpg);
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
            setState(ExecutionState::Completed);
        }

        updateCacheSizeLabel();
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
    return NodeUtils::getProjectDirectory(_widget);
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

void DEMSourceNode::updateLoginStatus()
{
    if (!m_demSourceCombo || !m_loginStatusLabel || !m_loginBtn || !m_logoutBtn)
        return;

    int demSource = m_demSourceCombo->currentIndex();
    if (demSource == 2) // Copernicus DEM 不需要登录
    {
        m_loginStatusLabel->setText(QStringLiteral("无需登录"));
        m_loginStatusLabel->setStyleSheet("color: gray;");
        m_loginBtn->setEnabled(false);
        m_logoutBtn->setEnabled(false);
        m_loginBtn->hide();
        m_logoutBtn->hide();
    }
    else
    {
        m_loginBtn->show();
        m_loginBtn->setEnabled(true);
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
        if (encryptedUser.isEmpty())
        {
            m_loginStatusLabel->setText(QStringLiteral("未登录"));
            m_loginStatusLabel->setStyleSheet("color: red;");
            m_logoutBtn->setEnabled(false);
            m_logoutBtn->hide();
        }
        else
        {
            QString username = QString::fromUtf8(QByteArray::fromBase64(encryptedUser.toUtf8()));
            m_loginStatusLabel->setText(QStringLiteral("已保存(%1)").arg(username));
            m_loginStatusLabel->setStyleSheet("color: green;");
            m_logoutBtn->setEnabled(true);
            m_logoutBtn->show();
            m_loginBtn->hide();
        }
    }
}

QStringList DEMSourceNode::getExpectedOutputFilePaths() const
{
    QString savePath = projectPath();
    QString name = m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName;
    QString targetH5 = savePath + "/" + name + "/" + name + "_dem.h5";
    QString targetTif = savePath + "/" + name + "/" + name + "_dem.tif";
    return QStringList() << targetH5 << targetTif;
}

// 提取输入图像地理边界的辅助函数
static bool getInputImageBounds(const QString& firstInput, const QString& save_path,
                                double& min_lon, double& max_lon, double& min_lat, double& max_lat)
{
    FormatConversion FC;
    cv::Mat mat_lon, mat_lat;
    bool mapped_check = false;
    {
        NodeUtils::Hdf5Locker locker;
        mapped_check = (NodeUtils::readMatFromH5(firstInput, "mapped_lon", mat_lon) &&
                        NodeUtils::readMatFromH5(firstInput, "mapped_lat", mat_lat));
    }
    if (mapped_check)
    {
        double min_lon_val, max_lon_val, min_lat_val, max_lat_val;
        cv::minMaxLoc(mat_lon, &min_lon_val, &max_lon_val);
        cv::minMaxLoc(mat_lat, &min_lat_val, &max_lat_val);
        min_lon = min_lon_val;
        max_lon = max_lon_val;
        min_lat = min_lat_val;
        max_lat = max_lat_val;
        return true;
    }

    std::string source_file;
    QString src_file;
    bool read_src_ok = false;
    {
        NodeUtils::Hdf5Locker locker;
        read_src_ok = NodeUtils::readStringFromH5(firstInput, "source_1", source_file);
    }
    if (read_src_ok)
    {
        src_file = save_path + "/" + QString(source_file.c_str());
        if (!QFile::exists(src_file))
        {
            src_file = firstInput;
        }
    }
    else
    {
        src_file = firstInput;
    }

    if (QFile::exists(src_file))
    {
        int sceneHeight = 0, sceneWidth = 0, offset_row = 0, offset_col = 0;
        cv::Mat lon_coef, lat_coef;
        bool read_para_ok = false;
        {
            NodeUtils::Hdf5Locker locker;
            if (NodeUtils::readScalarFromH5(src_file, "range_len", sceneWidth) &&
                NodeUtils::readScalarFromH5(src_file, "azimuth_len", sceneHeight) &&
                NodeUtils::readMatFromH5(src_file, "lon_coefficient", lon_coef) &&
                NodeUtils::readMatFromH5(src_file, "lat_coefficient", lat_coef))
            {
                read_para_ok = true;
                offset_row = 0;
                offset_col = 0;
                NodeUtils::readScalarFromH5(src_file, "offset_row", offset_row);
                NodeUtils::readScalarFromH5(src_file, "offset_col", offset_col);
            }
        }
        if (read_para_ok)
        {
            double lonMax = 0, lonMin = 0, latMax = 0, latMin = 0;
            if (Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
                &lonMax, &latMax, &lonMin, &latMin) == 0)
            {
                min_lon = lonMin;
                max_lon = lonMax;
                min_lat = latMin;
                max_lat = latMax;
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// DEMSourceValidationWidget - 外部 DEM 数据验证选项卡组件
// ============================================================================
class DEMSourceValidationWidget : public BaseValidationWidget
{
public:
    DEMSourceValidationWidget(DEMSourceNode* node, QWidget* parent)
        : BaseValidationWidget(node, parent)
        , m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }
    ~DEMSourceValidationWidget() override = default;

private:
    void setupUI()
    {
        setupBaseUI(QObject::tr("正在验证数据中..."),
                    QObject::tr("正在读取并核对 DEM 文件的投影、高程范围及几何范围。"),
                    QObject::tr("高程特征与有效性"));

        m_lblElevationRange = createFeatureLabel();
        m_lblValidPixelRate = createFeatureLabel();
        m_lblDemResolution = createFeatureLabel();
        m_lblCrsInfo = createFeatureLabel();
        m_lblBoundsCheck = createFeatureLabel();

        m_featureLayout->addRow(createHeaderLabel(QObject::tr("高程数值范围:")), m_lblElevationRange);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("有效像元比例:")), m_lblValidPixelRate);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("DEM 像元行列数:")), m_lblDemResolution);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("空间参考系(CRS):")), m_lblCrsInfo);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("地理范围包容性:")), m_lblBoundsCheck);
    }

    struct DEMValidationResults {
        bool success = false;
        QString errorMsg;
        
        // 用于比对的期望参数
        QString expSource;
        QString actSource;
        double expRes = 0.0;
        double actRes = 0.0;
        int expResMode = 0;
        
        // 范围包围框
        double expMinLon = 0.0, expMaxLon = 0.0;
        double expMinLat = 0.0, expMaxLat = 0.0;
        double actMinLon = 0.0, actMaxLon = 0.0;
        double actMinLat = 0.0, actMaxLat = 0.0;
        bool hasInputBounds = false;
        
        // 特征值
        int rows = 0;
        int cols = 0;
        QString crsWkt;
        double minElev = 0.0;
        double maxElev = 0.0;
        double validRate = 0.0;
        bool hasData = false;
        
        bool boundsPass = false;
        bool crsPass = false;
    };

    void startAsyncValidation() override
    {
        m_isTimedOut = false;

        if (m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(QObject::tr("验证未通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未检测到获取完成的 DEM 数据。请先运行该节点，成功生成 DEM 数据后再进行验证。"));
            
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);
            
            m_lblElevationRange->setText(QObject::tr("未执行"));
            m_lblValidPixelRate->setText(QObject::tr("未执行"));
            m_lblDemResolution->setText(QObject::tr("未执行"));
            m_lblCrsInfo->setText(QObject::tr("未执行"));
            m_lblBoundsCheck->setText(QObject::tr("未执行"));
            return;
        }

        QStringList expectedOuts = m_node->getExpectedOutputFilePaths();
        if (expectedOuts.size() < 2 || !QFileInfo::exists(expectedOuts[0])) {
            m_statusTitle->setText(QObject::tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("生成的 DEM 成果 H5 文件不存在或路径无效。"));
            return;
        }

        m_loadingOverlay->startLoading(QObject::tr("正在加载 DEM 文件并计算高程特征值..."));

        QString h5Path = expectedOuts[0];
        QString tifPath = expectedOuts[1];
        
        auto inData = m_node->getInputData();
        QString firstInput = (inData && !inData->filePaths().isEmpty()) ? inData->filePaths().first() : "";
        QString save_path = m_node->projectPath();

        // 获取参数用于比对
        QJsonObject saved = m_node->save();
        int expSrcIdx = saved["demSource"].toInt(0);
        int expResM = saved["resMode"].toInt(0);
        double expCustRes = saved["customResolution"].toDouble(0.0);

        QFuture<DEMValidationResults> future = QtConcurrent::run([h5Path, tifPath, expSrcIdx, expResM, expCustRes, firstInput, save_path]() {
            DEMValidationResults res;
            res.expResMode = expResM;
            
            // 期望数据源
            if (expSrcIdx == 0) res.expSource = "SRTM1";
            else if (expSrcIdx == 1) res.expSource = "SRTM3";
            else if (expSrcIdx == 2) res.expSource = "Copernicus";
            else if (expSrcIdx == 3) res.expSource = "ASTER";
            
            // 期望分辨率
            if (expResM == 0) res.expRes = (expSrcIdx == 1) ? 90.0 : 30.0;
            else if (expResM == 1) res.expRes = 30.0;
            else if (expResM == 2) res.expRes = 90.0;
            else res.expRes = expCustRes;

            // 1. 读取输入影像地理边界 (期望覆盖范围)
            double inMinLon = 0, inMaxLon = 0, inMinLat = 0, inMaxLat = 0;
            if (!firstInput.isEmpty()) {
                res.hasInputBounds = getInputImageBounds(firstInput, save_path, inMinLon, inMaxLon, inMinLat, inMaxLat);
                if (res.hasInputBounds) {
                    res.expMinLon = inMinLon;
                    res.expMaxLon = inMaxLon;
                    res.expMinLat = inMinLat;
                    res.expMaxLat = inMaxLat;
                }
            }

            // 2. 读取 H5 成果元数据及 DEM 矩阵
            double demMinLon = 0.0, demMaxLon = 0.0, demMinLat = 0.0, demMaxLat = 0.0;
            std::string dem_source_str;
            cv::Mat dem;
            bool read_h5_ok = false;
            {
                NodeUtils::Hdf5Locker locker;
                read_h5_ok = (NodeUtils::readMatFromH5(h5Path, "dem", dem) &&
                              NodeUtils::readScalarFromH5(h5Path, "dem_min_lon", demMinLon) &&
                              NodeUtils::readScalarFromH5(h5Path, "dem_max_lon", demMaxLon) &&
                              NodeUtils::readScalarFromH5(h5Path, "dem_min_lat", demMinLat) &&
                              NodeUtils::readScalarFromH5(h5Path, "dem_max_lat", demMaxLat) &&
                              NodeUtils::readStringFromH5(h5Path, "dem_source", dem_source_str));
            }

            if (read_h5_ok) {
                res.actSource = QString::fromStdString(dem_source_str);
                res.actMinLon = demMinLon;
                res.actMaxLon = demMaxLon;
                res.actMinLat = demMinLat;
                res.actMaxLat = demMaxLat;
                res.cols = dem.cols;
                res.rows = dem.rows;
            }

            // 3. 读取 TIFF 获取 CRS 详细信息 (使用 GDAL)
            if (QFile::exists(tifPath)) {
                GDALAllRegister();
                GDALDataset* poDS = (GDALDataset*)GDALOpen(tifPath.toLocal8Bit().constData(), GA_ReadOnly);
                if (poDS) {
                    if (!read_h5_ok) {
                        res.cols = poDS->GetRasterXSize();
                        res.rows = poDS->GetRasterYSize();
                    }
                    const char* projRef = poDS->GetProjectionRef();
                    if (projRef && strlen(projRef) > 0) {
                        res.crsWkt = QString::fromLocal8Bit(projRef);
                    } else {
                        res.crsWkt = "WGS 84";
                    }
                    
                    double adfGeoTransform[6];
                    if (poDS->GetGeoTransform(adfGeoTransform) == CE_None && !read_h5_ok) {
                        res.actMinLon = adfGeoTransform[0];
                        res.actMaxLat = adfGeoTransform[3];
                        res.actMaxLon = res.actMinLon + adfGeoTransform[1] * res.cols;
                        res.actMinLat = res.actMaxLat + adfGeoTransform[5] * res.rows;
                    }
                    GDALClose(poDS);
                }
            } else {
                res.crsWkt = "WGS 84";
            }

            // 计算实际分辨率
            if (res.cols > 0 && res.rows > 0) {
                double lonResDeg = (res.actMaxLon - res.actMinLon) / res.cols;
                res.actRes = lonResDeg * 111000.0; // 粗略转换为米
            }

            // 4. 统计分析 DEM 矩阵高程值范围和有效像元比例
            if (!dem.empty()) {
                cv::Mat doubleDem;
                if (dem.type() != CV_64F) {
                    dem.convertTo(doubleDem, CV_64F);
                } else {
                    doubleDem = dem;
                }

                double minElev = 99999.0;
                double maxElev = -99999.0;
                qint64 validCount = 0;
                qint64 totalCount = 0;
                
                int dRows = doubleDem.rows;
                int dCols = doubleDem.cols;
                int step = 1;
                // 如果像元数大于 400 万，采用跨步采样提高统计速度
                if (dRows * dCols > 4000000) {
                    step = std::max(1, (dRows * dCols) / 4000000);
                }

                for (int r = 0; r < dRows; r += step) {
                    for (int c = 0; c < dCols; c += step) {
                        double val = doubleDem.at<double>(r, c);
                        totalCount++;
                        // 过滤掉 NoData 填充值 -32767.0 与 -9999.0
                        if (val != -32767.0 && val != -9999.0 && val > -1000.0 && val < 9000.0) {
                            validCount++;
                            if (val < minElev) minElev = val;
                            if (val > maxElev) maxElev = val;
                        }
                    }
                }

                if (validCount > 0) {
                    res.minElev = minElev;
                    res.maxElev = maxElev;
                    res.validRate = (double)validCount / totalCount;
                    res.hasData = true;
                    res.success = true;
                } else {
                    res.errorMsg = QObject::tr("DEM 数据全部为 NoData (无效高程值)。");
                }
            } else {
                res.errorMsg = QObject::tr("无法从 H5 成果文件中加载 DEM 数据集。");
            }

            // 校验坐标系投影
            res.crsPass = res.crsWkt.contains("WGS 84", Qt::CaseInsensitive) || 
                          res.crsWkt.contains("WGS84", Qt::CaseInsensitive);

            // 校验包容性
            if (res.hasInputBounds) {
                bool lonOk = (res.actMinLon <= res.expMinLon + 1e-4) && (res.actMaxLon >= res.expMaxLon - 1e-4);
                bool latOk = (res.actMinLat <= res.expMinLat + 1e-4) && (res.actMaxLat >= res.expMaxLat - 1e-4);
                res.boundsPass = lonOk && latOk;
            } else {
                res.boundsPass = true;
            }

            return res;
        });

        auto* watcher = new QFutureWatcher<DEMValidationResults>(this);
        connect(watcher, &QFutureWatcher<DEMValidationResults>::finished, this, [this, watcher]() {
            if (m_isTimedOut) {
                watcher->deleteLater();
                return;
            }

            DEMValidationResults res = watcher->result();
            m_loadingOverlay->stopLoading();

            if (res.success) {
                m_compTable->clearComparison();
                m_compTable->setEnabled(true);

                // 1. 数据源比对
                m_compTable->addComparison(QObject::tr("DEM 数据源"), res.expSource, res.actSource.toUpper());

                // 2. 几何范围比对
                QString expLonStr, actLonStr;
                if (res.hasInputBounds) {
                    if (res.boundsPass) {
                        // 如果校验通过，显示为一致的经度范围，使表格展示为“一致”
                        QString rangeStr = QString("[%1°, %2°]").arg(res.actMinLon, 0, 'f', 4).arg(res.actMaxLon, 0, 'f', 4);
                        expLonStr = rangeStr;
                        actLonStr = rangeStr;
                    } else {
                        // 如果不通过，展示要求的扩展包围框与实际包围框，以高亮“不一致”
                        expLonStr = QString("≥ [%1°, %2°]").arg(res.expMinLon - 0.05, 0, 'f', 4).arg(res.expMaxLon + 0.05, 0, 'f', 4);
                        actLonStr = QString("[%1°, %2°]").arg(res.actMinLon, 0, 'f', 4).arg(res.actMaxLon, 0, 'f', 4);
                    }
                } else {
                    expLonStr = QObject::tr("不限");
                    actLonStr = QString("[%1°, %2°]").arg(res.actMinLon, 0, 'f', 4).arg(res.actMaxLon, 0, 'f', 4);
                }
                m_compTable->addComparison(QObject::tr("经度覆盖范围"), expLonStr, actLonStr);

                QString expLatStr, actLatStr;
                if (res.hasInputBounds) {
                    if (res.boundsPass) {
                        QString rangeStr = QString("[%1°, %2°]").arg(res.actMinLat, 0, 'f', 4).arg(res.actMaxLat, 0, 'f', 4);
                        expLatStr = rangeStr;
                        actLatStr = rangeStr;
                    } else {
                        expLatStr = QString("≥ [%1°, %2°]").arg(res.expMinLat - 0.05, 0, 'f', 4).arg(res.expMaxLat + 0.05, 0, 'f', 4);
                        actLatStr = QString("[%1°, %2°]").arg(res.actMinLat, 0, 'f', 4).arg(res.actMaxLat, 0, 'f', 4);
                    }
                } else {
                    expLatStr = QObject::tr("不限");
                    actLatStr = QString("[%1°, %2°]").arg(res.actMinLat, 0, 'f', 4).arg(res.actMaxLat, 0, 'f', 4);
                }
                m_compTable->addComparison(QObject::tr("纬度覆盖范围"), expLatStr, actLatStr);

                // 3. 分辨率比对
                QString expResStr, actResStr;
                double targetRes = res.expRes;
                bool resMatch = (std::abs(res.actRes - targetRes) / targetRes < 0.15);
                if (resMatch) {
                    QString resStr = QString("%1m").arg(targetRes, 0, 'f', 1);
                    expResStr = resStr;
                    actResStr = resStr;
                } else {
                    expResStr = res.expResMode == 0 ? QObject::tr("原始分辨率") : QString("%1m").arg(targetRes, 0, 'f', 1);
                    actResStr = QString("%1m").arg(res.actRes, 0, 'f', 1);
                }
                m_compTable->addComparison(QObject::tr("目标网格分辨率"), expResStr, actResStr);

                // 4. CRS 比对
                QString actCrsName = res.crsPass ? "WGS 84" : QObject::tr("非标准 (或投影像元)");
                m_compTable->addComparison(QObject::tr("坐标系统 (CRS)"), "WGS 84", actCrsName);

                // 刷新界面标签
                m_lblElevationRange->setText(QString("%1m ~ %2m").arg(res.minElev, 0, 'f', 1).arg(res.maxElev, 0, 'f', 1));
                m_lblValidPixelRate->setText(QString("%1%").arg(res.validRate * 100.0, 0, 'f', 2));
                m_lblDemResolution->setText(QString("%1 × %2").arg(res.cols).arg(res.rows));
                
                QString crsDisp = QObject::tr("WGS 84 (EPSG:4326)");
                if (!res.crsPass) {
                    crsDisp = res.crsWkt.left(50) + (res.crsWkt.length() > 50 ? "..." : "");
                }
                m_lblCrsInfo->setText(crsDisp);

                if (res.hasInputBounds) {
                    m_lblBoundsCheck->setText(res.boundsPass ? QObject::tr("完全包含 (有效)") : QObject::tr("未完全包含 (警告)"));
                    m_lblBoundsCheck->setStyleSheet(res.boundsPass ? "color: #10B981; font-weight: bold;" : "color: #F59E0B; font-weight: bold;");
                } else {
                    m_lblBoundsCheck->setText(QObject::tr("未连接输入图像 (仅校验 DEM)"));
                    m_lblBoundsCheck->setStyleSheet("color: #6B7280;");
                }

                // 更新状态说明
                if (!res.boundsPass) {
                    m_statusTitle->setText(QObject::tr("校验范围不匹配"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                    m_statusDesc->setText(QObject::tr("下载 of DEM 地理范围未完全包含输入影像。请检查上游数据或手动扩展下载范围。"));
                } else if (res.validRate < 0.95) {
                    m_statusTitle->setText(QObject::tr("高程有效率偏低"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                    m_statusDesc->setText(QObject::tr("DEM 中包含较多无效像元（NoData，占比 %1%）。如果位于沿海或海洋，这属于正常现象。").arg(QString::number((1.0 - res.validRate) * 100.0, 'f', 1)));
                } else {
                    m_statusTitle->setText(QObject::tr("验证通过"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                    m_statusDesc->setText(QObject::tr("DEM 文件的坐标系 (WGS 84)、高程区间与覆盖空间范围均核对一致，像元无位错。"));
                }
            } else {
                m_statusTitle->setText(QObject::tr("验证失败"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                m_statusDesc->setText(res.errorMsg);
            }

            watcher->deleteLater();
        });

        watcher->setFuture(future);
    }

private:
    DEMSourceNode* m_node = nullptr;
    
    QLabel* m_lblElevationRange = nullptr;
    QLabel* m_lblValidPixelRate = nullptr;
    QLabel* m_lblDemResolution = nullptr;
    QLabel* m_lblCrsInfo = nullptr;
    QLabel* m_lblBoundsCheck = nullptr;
};

::QWidget* DEMSourceNode::createValidationWidget(::QWidget* parent)
{
    return new DEMSourceValidationWidget(this, parent);
}

} // namespace QtNodes
