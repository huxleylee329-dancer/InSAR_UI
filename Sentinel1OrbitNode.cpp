#include "Sentinel1OrbitNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include "tinyxml.h"
#include "EarthdataLoginDialog.h"
#include "NodeDetailWindow.hpp"
#include <QSettings>
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
#include <QFileDialog>
#include <QDirIterator>
#include <QListWidget>
#include <QSplitter>
#include <QFrame>
#include <QTableWidget>
#include <QHeaderView>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace QtNodes {

Sentinel1OrbitNode::Sentinel1OrbitNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_orbitSourceCombo(nullptr)
    , m_cacheDirEdit(nullptr)
    , m_browseCacheBtn(nullptr)
    , m_clearCacheBtn(nullptr)
    , m_cacheSizeLabel(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_loginStatusLabel(nullptr)
    , m_loginBtn(nullptr)
    , m_logoutBtn(nullptr)
    , m_orbitSource(0)
    , m_cacheDir("")
    , m_outputNodeName("")
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);

    // 读取全局默认轨道路径
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    m_cacheDir = settings.value("Orbit/LastMatchDir", "").toString();
    if (m_cacheDir.isEmpty())
    {
        m_cacheDir = QDir::currentPath() + "/orbits";
    }
    m_cacheDir = QDir::toNativeSeparators(m_cacheDir);
}

Sentinel1OrbitNode::~Sentinel1OrbitNode()
{
    stopExecution();
}

unsigned int Sentinel1OrbitNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    return 2;  // 输出: port0=成果, port1=预览(可选)
}

NodeDataType Sentinel1OrbitNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "Imported File"};
    if (portIndex == 0)
        return NodeDataType{"imported_file", "Imported File"};
    return NodeDataType{"image_info", "Image Info"};
}

bool Sentinel1OrbitNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString Sentinel1OrbitNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入影像");
    }
    if (portIndex == 0) {
        return tr("成果 *");
    }
    return tr("预览 ?");
}

bool Sentinel1OrbitNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return false;
    return (portIndex == 1);  // port0 必选, port1 预览可选
}

std::shared_ptr<NodeData> Sentinel1OrbitNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    return m_previewData;
}

void Sentinel1OrbitNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    qDebug() << "[OrbitNode] setInData() port:" << port
             << "data:" << (data ? "valid" : "null")
             << "currentState:" << (int)executionState();
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData)
    {
        bool alreadyCompleted = (executionState() == ExecutionState::Completed);
        if (alreadyCompleted && m_outputData && !m_outputData->filePaths().isEmpty())
        {
            // 保持已写入轨道的输出数据路径，不被输入覆盖
        }
        else
        {
            m_outputData = m_inputData;
            QStringList inputJpgs;
            for (const QString& h5 : m_inputData->filePaths()) {
                inputJpgs.append(h5.left(h5.lastIndexOf('.')) + ".jpg");
            }
            m_previewData = std::make_shared<ImageInfoData>(inputJpgs);
        }

        // 自动生成默认目标节点名（仅当用户未手动设置时）
        if (m_outputNodeName.isEmpty() && !m_inputData->nodeName().isEmpty()) {
            m_outputNodeName = m_inputData->nodeName() + "_Orbit";
            if (m_outputNodeNameEdit) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
        }

        // 如果节点状态已为 Completed，则向下游传播数据
        if (executionState() == ExecutionState::Completed)
        {
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
        }
    }
    else
    {
        m_outputData.reset();
        m_previewData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
    }

    // 调用基类 setInData，触发自动执行链
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* Sentinel1OrbitNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

QJsonObject Sentinel1OrbitNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["orbitSource"] = m_orbitSourceCombo ? m_orbitSourceCombo->currentIndex() : m_orbitSource;
    json["cacheDir"] = m_cacheDirEdit ? m_cacheDirEdit->text().trimmed() : m_cacheDir;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;

    // 保存输出路径，使 validateAndRestoreOutput() 在工程恢复时不依赖 m_inputData
    if (m_outputData && !m_outputData->filePaths().isEmpty()) {
        QJsonArray outArr;
        for (const QString& p : m_outputData->filePaths())
            outArr.append(p);
        json["outputPaths"] = outArr;
    }

    return json;
}

void Sentinel1OrbitNode::load(QJsonObject const &json)
{
    // 先读取输出路径和输出节点名，供 validateAndRestoreOutput() 使用
    // （此时 m_inputData 尚未恢复，必须在基类 load() 之前完成）
    m_savedOutputPaths.clear();
    QJsonArray outArr = json["outputPaths"].toArray();
    for (const QJsonValue& v : outArr)
        m_savedOutputPaths.append(v.toString());
    m_outputNodeName = json["outputNodeName"].toString("");

    ExecutableNodeDelegateModel::load(json);
    m_orbitSource = json["orbitSource"].toInt(0);
    m_cacheDir = json["cacheDir"].toString();

    if (m_cacheDir.isEmpty())
    {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        m_cacheDir = settings.value("Orbit/LastMatchDir", "").toString();
        if (m_cacheDir.isEmpty())
        {
            m_cacheDir = QDir::currentPath() + "/orbits";
        }
    }
    m_cacheDir = QDir::toNativeSeparators(m_cacheDir);

    if (_widget)
    {
        if (m_orbitSourceCombo) m_orbitSourceCombo->setCurrentIndex(m_orbitSource);
        if (m_cacheDirEdit) m_cacheDirEdit->setText(m_cacheDir);
        if (m_outputNodeNameEdit) {
            if (!m_outputNodeName.isEmpty()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
        }
        updateLoginStatus();
        updateCacheSizeLabel();
    }
}

void Sentinel1OrbitNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

bool Sentinel1OrbitNode::validateAndRestoreOutput()
{
    QString targetDirName = m_outputNodeName.isEmpty() ? "S1_Orbit" : m_outputNodeName;

    // 重建待校验路径：优先使用保存的输出路径（工程恢复时 m_inputData 尚未设置）
    QStringList expectedPaths;
    if (!m_savedOutputPaths.isEmpty()) {
        expectedPaths = m_savedOutputPaths;
    } else if (m_inputData && !m_inputData->filePaths().isEmpty()) {
        // 向后兼容：旧工程无 outputPaths，从 m_inputData 推导
        QString projectDir = QFileInfo(projectPath()).path();
        QString targetDirPath = projectDir + "/" + targetDirName;
        for (const QString& h5Path : m_inputData->filePaths())
        {
            expectedPaths.append(targetDirPath + "/" + QFileInfo(h5Path).fileName());
        }
    } else {
        setState(ExecutionState::Idle);
        return false;
    }

    FormatConversion FC;
    bool allHaveOrbit = true;
    for (const QString& h5Path : expectedPaths)
    {
        if (!QFile::exists(h5Path)) {
            allHaveOrbit = false;
            break;
        }
        int r = 0, c = 0;
        if (FC.get_dataset_dims(h5Path.toLocal8Bit().constData(), "fine_state_vec", &r, &c) != 0 || r < 5)
        {
            allHaveOrbit = false;
            break;
        }
    }
    if (allHaveOrbit)
    {
        m_outputData = std::make_shared<ImportedFileData>(expectedPaths, targetDirName);
        QStringList expectedJpgPaths;
        for (const QString& h5 : expectedPaths) {
            expectedJpgPaths.append(h5.left(h5.lastIndexOf('.')) + ".jpg");
        }
        m_previewData = std::make_shared<ImageInfoData>(expectedJpgPaths);
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
        setState(ExecutionState::Completed);
        updateCacheSizeLabel();
        return true;
    }

    setState(ExecutionState::Idle);
    return false;
}

bool Sentinel1OrbitNode::prepareToStart()
{
    qDebug() << "[OrbitNode] prepareToStart()";
    if (!validateInputs()) {
        qDebug() << "[OrbitNode] validateInputs() failed";
        return false;
    }
    qDebug() << "[OrbitNode] input valid, filePaths:" << m_inputData->filePaths();

    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedFilePaths = m_inputData->filePaths();
    m_preparedSource = m_orbitSourceCombo ? m_orbitSourceCombo->currentIndex() : m_orbitSource;
    m_preparedCacheDir = m_cacheDirEdit ? m_cacheDirEdit->text().trimmed() : m_cacheDir;

    qDebug() << "[OrbitNode] projectPath:" << m_preparedSavePath;
    qDebug() << "[OrbitNode] projectName:" << m_preparedProjectName;
    qDebug() << "[OrbitNode] cacheDir:" << m_preparedCacheDir;
    qDebug() << "[OrbitNode] orbitSource:" << m_preparedSource;

    // 检查 NASA Earthdata 登录状态
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
    QString encryptedPass = settings.value("DEM/EarthdataPassword", "").toString();
    qDebug() << "[OrbitNode] Earthdata user present:" << !encryptedUser.isEmpty() << "pass present:" << !encryptedPass.isEmpty();
    if (encryptedUser.isEmpty() || encryptedPass.isEmpty())
    {
        if (_isAutoTriggered)
        {
            qDebug() << "[OrbitNode] auto-triggered, no credentials, skipping";
            InSARLogManager::LogError("Sentinel1OrbitNode", "NASA Earthdata login required but credentials are missing. Skip execution.");
            return false;
        }
        else
        {
            QMessageBox::warning(nullptr, QStringLiteral("提示"), QStringLiteral("下载精密轨道需要登录 NASA Earthdata 账户。请先登录！"));
            EarthdataLoginDialog dlg(nullptr);
            if (dlg.exec() != QDialog::Accepted)
            {
                qDebug() << "[OrbitNode] user cancelled login dialog";
                return false;
            }
            updateLoginStatus();
        }
    }

    // 检查输出冲突并提示覆盖/复用 (SOP 移植规范 #3)
    QString targetDirName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (targetDirName.isEmpty() && m_inputData) {
        targetDirName = m_inputData->nodeName() + "_Orbit";
    }

    QString projectDir = QFileInfo(m_preparedSavePath).path();
    QString targetDirPath = projectDir + "/" + targetDirName;

    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QDir outDir(targetDirPath);
    if (outDir.exists() && outDir.entryList(QStringList{"*.h5"}, QDir::Files).count() > 0)
    {
        auto ctx = NodeUtils::getProjectContext(_widget);
        if (_isAutoTriggered) {
            m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        } else {
            QStringList expectedH5Paths;
            for (const QString& h5Path : m_preparedFilePaths) {
                expectedH5Paths.append(targetDirPath + "/" + QFileInfo(h5Path).fileName());
            }
            m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(ctx, targetDirName, expectedH5Paths, nullptr);
        }
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        qDebug() << "[OrbitNode] user cancelled overwrite";
        return false;
    }

    qDebug() << "[OrbitNode] prepareToStart() OK";
    return true;
}

void Sentinel1OrbitNode::execute()
{
    executeProcessing();
}

void Sentinel1OrbitNode::stopExecution()
{
    if (m_workerThread && m_thread)
    {
        m_workerThread->StopProcess();
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void Sentinel1OrbitNode::processAutomatically()
{
    qDebug() << "[OrbitNode] processAutomatically() called";
    if (m_workerThread || m_thread) {
        deferAutomaticCompletion();
        return;
    }
    if (prepareToStart()) {
        qDebug() << "[OrbitNode] prepareToStart() returned true, starting execution";
        executeProcessing();
    } else {
        qDebug() << "[OrbitNode] prepareToStart() returned false, setting Idle";
        setState(ExecutionState::Idle);
    }
}

void Sentinel1OrbitNode::executeProcessing()
{
    qDebug() << "[OrbitNode] executeProcessing()";
    if (m_workerThread || m_thread) {
        qDebug() << "[OrbitNode] already running, skip";
        return;
    }

    QString targetDirName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (targetDirName.isEmpty() && m_inputData) {
        targetDirName = m_inputData->nodeName() + "_Orbit";
    }

    // 覆盖/复用判断 (SOP 移植规范 #3 & #14)
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting)
    {
        m_outputNodeName = targetDirName;
        if (validateAndRestoreOutput())
        {
            qDebug() << "[OrbitNode] Loaded existing data successfully.";
            return;
        }
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite)
    {
        auto* iface = NodeUtils::getProjectContext(_widget);
        if (iface) {
            NodeUtils::removeDataNodeFromProject(iface, targetDirName);
        }
    }

    qDebug() << "[OrbitNode] creating worker thread, cacheDir:" << m_preparedCacheDir
             << "files:" << m_preparedFilePaths.size();
    for (const QString& f : m_preparedFilePaths) {
        qDebug() << "[OrbitNode]   input H5:" << f << "exists:" << QFileInfo::exists(f);
    }

    m_workerThread = new OrbitSourceWorker();
    m_thread = new QThread();
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    connect(m_workerThread, &OrbitSourceWorker::updateProcess, this, &Sentinel1OrbitNode::onProgressUpdate);
    connect(m_workerThread, &OrbitSourceWorker::errorProcess, this, &Sentinel1OrbitNode::onError);
    connect(m_workerThread, &OrbitSourceWorker::endProcess, this, &Sentinel1OrbitNode::onProcessingFinished);

    connect(this, &Sentinel1OrbitNode::startOrbitFetch, m_workerThread, &OrbitSourceWorker::fetch_orbits);

    m_thread->start();
    setState(ExecutionState::Running);
    deferAutomaticCompletion();

    qDebug() << "[OrbitNode] emitting startOrbitFetch";
    emit startOrbitFetch(
        m_preparedSavePath,
        m_preparedProjectName,
        m_preparedFilePaths,
        m_preparedSource,
        m_preparedCacheDir,
        projectModel()
    );
}

void Sentinel1OrbitNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        setOutputData(0, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene);
        }
    };

    const int labelWidth = 90;

    // 1. 轨道数据源
    auto* sourceLayout = new QHBoxLayout();
    QLabel* sourceLabel = new QLabel(QStringLiteral("轨道数据源"));
    sourceLabel->setFixedWidth(labelWidth);
    sourceLayout->addWidget(sourceLabel);
    m_orbitSourceCombo = new QComboBox();
    m_orbitSourceCombo->addItem("NASA ASF (S1 Aux Orbits)");
    m_orbitSourceCombo->addItem("ESA CDSE (Copernicus)");
    m_orbitSourceCombo->setCurrentIndex(m_orbitSource);
    connect(m_orbitSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        if (m_orbitSource != index) {
            if (!confirmParameterChange()) {
                m_orbitSourceCombo->blockSignals(true);
                m_orbitSourceCombo->setCurrentIndex(m_orbitSource);
                m_orbitSourceCombo->blockSignals(false);
                return;
            }
            m_orbitSource = index;
            invalidateNodeData();
        }
    });
    sourceLayout->addWidget(m_orbitSourceCombo);
    layout->addLayout(sourceLayout);

    // 2. 账户状态与登录注销按钮
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

    // 3. 轨道存放目录
    auto* cacheLayout = new QHBoxLayout();
    QLabel* cacheLabel = new QLabel(QStringLiteral("存放目录"));
    cacheLabel->setFixedWidth(labelWidth);
    cacheLayout->addWidget(cacheLabel);

    m_cacheDirEdit = new QLineEdit();
    m_cacheDirEdit->setText(m_cacheDir);
    m_cacheDirEdit->setToolTip(QStringLiteral("轨道文件存放路径。默认指向软件全局 orbits 目录。"));
    connect(m_cacheDirEdit, &QLineEdit::editingFinished, this, [this]() {
        QString dir = m_cacheDirEdit->text().trimmed();
        if (m_cacheDir != dir) {
            m_cacheDir = dir;
            updateCacheSizeLabel();
        }
    });
    cacheLayout->addWidget(m_cacheDirEdit);

    m_browseCacheBtn = new QPushButton("...");
    m_browseCacheBtn->setFixedWidth(30);
    connect(m_browseCacheBtn, &QPushButton::clicked, this, [this]() {
        QString selectedDir = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("选择轨道存放目录"), m_cacheDirEdit->text());
        if (!selectedDir.isEmpty()) {
            m_cacheDir = QDir::toNativeSeparators(selectedDir);
            m_cacheDirEdit->setText(m_cacheDir);
            updateCacheSizeLabel();
        }
    });
    cacheLayout->addWidget(m_browseCacheBtn);
    layout->addLayout(cacheLayout);

    // 4. 缓存大小与清理
    auto* clearLayout = new QHBoxLayout();
    m_cacheSizeLabel = new QLabel("0.00 MB");
    clearLayout->addWidget(m_cacheSizeLabel);

    m_clearCacheBtn = new QPushButton(QStringLiteral("清理缓存"));
    connect(m_clearCacheBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(nullptr, QStringLiteral("确认"), QStringLiteral("确定要清空该目录下的所有轨道文件吗？")) == QMessageBox::Yes) {
            QDir dir(m_cacheDir);
            if (dir.exists()) {
                QStringList filters;
                filters << "*.EOF" << "*.part";
                for (const QString& file : dir.entryList(filters, QDir::Files)) {
                    dir.remove(file);
                }
                updateCacheSizeLabel();
            }
        }
    });
    clearLayout->addWidget(m_clearCacheBtn);
    layout->addLayout(clearLayout);

    // 5. 目标节点名称（输出在项目树中的位置）
    auto* outputLayout = new QHBoxLayout();
    QLabel* outputLabel = new QLabel(QStringLiteral("目标节点"));
    outputLabel->setFixedWidth(labelWidth);
    outputLayout->addWidget(outputLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    if (!m_outputNodeName.isEmpty()) {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != name) {
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

    updateLoginStatus();
    updateCacheSizeLabel();
}

void Sentinel1OrbitNode::updateCacheSizeLabel()
{
    if (!m_cacheSizeLabel) return;

    double sizeMB = 0;
    QDir dir(m_cacheDir);
    if (dir.exists()) {
        QDirIterator it(m_cacheDir, QDir::Files);
        while (it.hasNext()) {
            it.next();
            if (it.fileName().endsWith(".EOF") || it.fileName().endsWith(".part")) {
                sizeMB += it.fileInfo().size();
            }
        }
    }
    sizeMB /= (1024.0 * 1024.0);
    m_cacheSizeLabel->setText(QString("%1 MB").arg(sizeMB, 0, 'f', 2));
}

void Sentinel1OrbitNode::updateLoginStatus()
{
    if (!m_loginStatusLabel || !m_loginBtn || !m_logoutBtn) return;

    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString user = settings.value("DEM/EarthdataUser", "").toString();
    if (!user.isEmpty())
    {
        QString plainUser = QString::fromUtf8(QByteArray::fromBase64(user.toUtf8()));
        m_loginStatusLabel->setText(QStringLiteral("已授权 (%1)").arg(plainUser));
        m_loginStatusLabel->setStyleSheet("color: green; font-weight: bold;");
        m_loginBtn->hide();
        m_logoutBtn->show();
    }
    else
    {
        m_loginStatusLabel->setText(QStringLiteral("未登录"));
        m_loginStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        m_loginBtn->show();
        m_logoutBtn->hide();
    }
}

void Sentinel1OrbitNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void Sentinel1OrbitNode::onProcessingFinished()
{
    qDebug() << "[OrbitNode] onProcessingFinished()";
    stopExecution();
    m_workerThread = nullptr;
    m_thread = nullptr;

    // 保存本次使用的路径到全局 Config 和项目专属 Config
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    settings.setValue("Orbit/LastMatchDir", m_preparedCacheDir);
    settings.setValue(QString("Orbit/ProjectDir_%1").arg(m_preparedProjectName), m_preparedCacheDir);

    // 将下载的精密轨道写入拷贝后的 H5 文件
    qDebug() << "[OrbitNode] m_inputData:" << (m_inputData ? "valid" : "null");
    int podApplyOk = 0, podApplyFail = 0, podSkipped = 0;
    QStringList newH5Paths;
    QString targetDirName = m_outputNodeName.isEmpty() ? "S1_Orbit" : m_outputNodeName;

    if (m_inputData) {
        QString projectDir = QFileInfo(m_preparedSavePath).path();
        QString targetDirPath = projectDir + "/" + targetDirName;
        QDir().mkpath(targetDirPath);

        // 如果覆盖，先清理项目中的旧数据
        auto* iface = NodeUtils::getProjectContext(_widget);
        if (iface) {
            NodeUtils::removeDataNodeFromProject(iface, targetDirName);
        }

        qDebug() << "[OrbitNode] cacheDir for EOF matching:" << m_preparedCacheDir;
        FormatConversion FC;
        for (const QString& h5Path : m_inputData->filePaths()) {
            qDebug() << "[OrbitNode] processing H5:" << h5Path;

            // 从原始 H5 读取传感器平台与成像时间（用于 EOF 文件匹配），彻底防 read_POD 独占冲突
            std::string startTimeStr, stopTimeStr, sensorStr, source1Str;
            {
                NodeUtils::Hdf5Locker locker;
                NodeUtils::readStringFromH5(h5Path, "acquisition_start_time", startTimeStr);
                NodeUtils::readStringFromH5(h5Path, "acquisition_stop_time", stopTimeStr);
                NodeUtils::readStringFromH5(h5Path, "sensor", sensorStr);
                NodeUtils::readStringFromH5(h5Path, "source_1", source1Str);
            }
            qDebug() << "[OrbitNode]   startTimeStr:" << QString::fromStdString(startTimeStr);
            qDebug() << "[OrbitNode]   sensorStr:" << QString::fromStdString(sensorStr);
            qDebug() << "[OrbitNode]   source1Str:" << QString::fromStdString(source1Str);

            // 在缓存目录中匹配 EOF 文件（日期优先，platform 仅作辅助过滤）
            QDir cacheDir(m_preparedCacheDir);
            QString sensorQ = QString::fromStdString(sensorStr).toUpper();
            QString source1Q = QString::fromStdString(source1Str).toUpper();
            QString platform = "";

            // 优先通过 source_1 确定平台，彻底解决 burst 混淆问题
            if (source1Q.contains("S1A")) {
                platform = "S1A";
            } else if (source1Q.contains("S1B")) {
                platform = "S1B";
            }

            // 兜底通过 sensor 属性确定
            if (platform.isEmpty()) {
                if (sensorQ == "SENTINEL" || sensorQ.isEmpty()) {
                    // burst H5: sensor 为 "sentinel" 无法区分 A/B，尝试双平台
                } else if (sensorQ.contains("SENTINEL-1A") || sensorQ == "S1A") {
                    platform = "S1A";
                } else if (sensorQ.contains("SENTINEL-1B") || sensorQ == "S1B") {
                    platform = "S1B";
                }
            }
            QString datePart = QString::fromStdString(startTimeStr).left(10).remove('-');
            // POEORB 文件名有效期窗口为 D-1 ~ D+1，不含 D 日期本身
            QDate d = QDate::fromString(datePart, "yyyyMMdd");
            QString datePrev = d.addDays(-1).toString("yyyyMMdd");
            QString dateNext = d.addDays(1).toString("yyyyMMdd");
            QString platPrefix = platform.isEmpty() ? "*" : platform;

            // 优先 POEORB 通配符匹配（文件系统过滤）
            // 兼容新旧 ESA 命名规范（2021 前后），用 * 代替时分秒
            QString poePattern = QString("*%1*V%2*_%3*.EOF")
                .arg(platPrefix, datePrev, dateNext);
            QStringList candidates = cacheDir.entryList({poePattern}, QDir::Files);

            // 回落 RESORB 通配符匹配 + 时效窗口精确校验
            if (candidates.isEmpty()) {
                QString resPattern = QString("*%1*%2*.EOF").arg(platPrefix, datePart);
                QStringList resCandidates = cacheDir.entryList({resPattern}, QDir::Files);
                // 解析采集时刻，用于 RESORB 时间窗口校验
                QDateTime acqTime = QDateTime::fromString(
                    QString::fromStdString(startTimeStr), Qt::ISODate);
                QRegularExpression resValidityRe("V(\\d{8}T\\d{6})_(\\d{8}T\\d{6})");
                for (const QString& f : resCandidates) {
                    // 跳过非 RESORB 文件（POEORB 虽已在上层被 filtered 但仍需防御）
                    if (!f.contains("RESORB", Qt::CaseInsensitive)) continue;
                    QRegularExpressionMatch vm = resValidityRe.match(f);
                    if (!vm.hasMatch()) { candidates.append(f); continue; }  // 无法解析则直接接受
                    QDateTime s = QDateTime::fromString(vm.captured(1), "yyyyMMddTHHmmss");
                    QDateTime e = QDateTime::fromString(vm.captured(2), "yyyyMMddTHHmmss");
                    if (acqTime.isValid() && acqTime >= s && acqTime <= e) {
                        candidates.append(f);
                    }
                }
            }

            QString matchedEof = candidates.isEmpty() ? QString() : cacheDir.absoluteFilePath(candidates.first());

            // 拷贝 H5 文件到新目录下
            QString newH5Path = targetDirPath + "/" + QFileInfo(h5Path).fileName();
            if (QFile::exists(newH5Path)) {
                QFile::remove(newH5Path);
            }
            if (!QFile::copy(h5Path, newH5Path)) {
                qDebug() << "[OrbitNode] failed to copy H5 file from" << h5Path << "to" << newH5Path;
                podApplyFail++;
                continue;
            }

            // 拷贝对应的预览 JPG 文件（如果有的话）
            QString jpgPath = h5Path.left(h5Path.lastIndexOf('.')) + ".jpg";
            QString newJpgPath = newH5Path.left(newH5Path.lastIndexOf('.')) + ".jpg";
            if (QFile::exists(jpgPath)) {
                if (QFile::exists(newJpgPath)) {
                    QFile::remove(newJpgPath);
                }
                QFile::copy(jpgPath, newJpgPath);
            }

            newH5Paths.append(newH5Path);

            if (!matchedEof.isEmpty()) {
                int ret;
                {
                    NodeUtils::Hdf5Locker locker;  // 全局 HDF5 锁，防止与验证 tab 等并发读冲突
                    double start_t = 0.0, stop_t = 1e12;
                    int rStart = FC.utc2gps(startTimeStr.c_str(), &start_t);
                    int rStop = FC.utc2gps(stopTimeStr.c_str(), &stop_t);

                    QString nativeEof = QDir::toNativeSeparators(matchedEof);
                    QString nativeH5 = QDir::toNativeSeparators(newH5Path);

                    InSARLogManager::LogInfo("Sentinel1OrbitNode", 
                        QString("开始写入精密轨道. H5影像: %1, 匹配轨道文件: %2, 覆盖成像时间: %3 ~ %4")
                            .arg(QFileInfo(newH5Path).fileName())
                            .arg(QFileInfo(matchedEof).fileName())
                            .arg(QString::fromStdString(startTimeStr))
                            .arg(QString::fromStdString(stopTimeStr)));

                    ret = FC.read_POD(
                        nativeEof.toLocal8Bit().constData(),
                        start_t, stop_t,
                        nativeH5.toLocal8Bit().constData()
                    );
                    if (ret >= 0) {
                        QString orbitType = "Precise (POE)";
                        if (QFileInfo(matchedEof).fileName().contains("RESORB", Qt::CaseInsensitive)) {
                            orbitType = "Reconstructed (RES)";
                        }
                        FC.write_str_to_h5(
                            nativeH5.toLocal8Bit().constData(),
                            "orbit_type",
                            orbitType.toStdString().c_str()
                        );
                    }
                }
                if (ret >= 0) {
                    podApplyOk++;
                    InSARLogManager::LogInfo("Sentinel1OrbitNode",
                        "精密轨道已写入: " + newH5Path + " <- " + QFileInfo(matchedEof).fileName());
                } else {
                    podApplyFail++;
                    InSARLogManager::LogError("Sentinel1OrbitNode",
                        QString("精密轨道写入失败: %1, read_POD 返回值: %2").arg(newH5Path).arg(ret));
                }
            } else {
                podSkipped++;
                InSARLogManager::LogWarning("Sentinel1OrbitNode",
                    QString("未匹配到轨道文件: %1").arg(QFileInfo(h5Path).fileName()));
            }
        }
    }
    qDebug() << "[OrbitNode] POD apply results: ok=" << podApplyOk << "fail=" << podApplyFail << "skipped=" << podSkipped;

    // 传播输出数据
    if (!newH5Paths.isEmpty()) {
        m_outputData = std::make_shared<ImportedFileData>(newH5Paths, targetDirName);
        QStringList newJpgPaths;
        for (const QString& h5 : newH5Paths) {
            newJpgPaths.append(h5.left(h5.lastIndexOf('.')) + ".jpg");
        }
        m_previewData = std::make_shared<ImageInfoData>(newJpgPaths);
    } else {
        m_outputData.reset();
        m_previewData.reset();
    }
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);

    // 写入项目 XML 并刷新项目树
    if (podApplyOk > 0) {
        XMLFile* xml = projectXml();
        if (xml) {
            TiXmlElement* root = nullptr;
            xml->get_root(root);
            if (root) {
                TiXmlElement* dataNodeElem = new TiXmlElement("DataNode");
                dataNodeElem->SetAttribute("name", targetDirName.toStdString().c_str());
                dataNodeElem->SetAttribute("data_count", std::to_string(newH5Paths.size()).c_str());
                dataNodeElem->SetAttribute("data_processing", "orbit");
                dataNodeElem->SetAttribute("rank", "complex-1.0");

                // 排序插入
                int index = 1;
                TiXmlElement* root_child = root->FirstChildElement();
                if (root_child) root_child = root_child->NextSiblingElement(); // skip project_info

                TiXmlElement* insertBeforeNode = nullptr;
                for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++)
                {
                    const char* rankAttr = p->Attribute("rank");
                    if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 || strcmp(rankAttr, "complex-1.0") == 0))
                        continue;
                    else { insertBeforeNode = p; break; }
                }
                dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

                for (int i = 0; i < newH5Paths.size(); i++) {
                    QFileInfo fileinfo(newH5Paths.at(i));
                    QString relativePath = QString("/%1/%2").arg(targetDirName).arg(fileinfo.fileName());

                    TiXmlElement* dataElem = new TiXmlElement("Data");
                    TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                    dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
                    dataElem->LinkEndChild(dataNameNode);
                    TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                    dataRankNode->LinkEndChild(new TiXmlText("complex-1.0"));
                    dataElem->LinkEndChild(dataRankNode);
                    TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                    dataIndexNode->LinkEndChild(new TiXmlText(QString::number(i + 1).toStdString().c_str()));
                    dataElem->LinkEndChild(dataIndexNode);
                    TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                    dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                    dataElem->LinkEndChild(dataPathNode);
                    dataNodeElem->LinkEndChild(dataElem);
                }

                TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");
                TiXmlElement* nillElem  = new TiXmlElement("nill");
                nillElem->LinkEndChild(new TiXmlText("0"));
                paramsElem->LinkEndChild(nillElem);
                dataNodeElem->LinkEndChild(paramsElem);

                if (insertBeforeNode)
                {
                    root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                    delete dataNodeElem;
                    for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement())
                    {
                        index++;
                        p->SetAttribute("index", QString::number(index).toStdString().c_str());
                    }
                }
                else
                {
                    root->LinkEndChild(dataNodeElem);
                }

                xml->XMLFile_save(projectPath().toStdString().c_str());
            }
        }

        // 恢复左侧树标准项目模型 (Standard Item Model Tree View)
        QStandardItemModel* projModelPtr = projectModel();
        if (projModelPtr) {
            QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
            if (!foundProjects.isEmpty()) {
                QStandardItem* projectItem = foundProjects.first();

                // 1. 查找或建立 Orbit 根节点
                QStandardItem* orbitItem = nullptr;
                for (int i = 0; i < projectItem->rowCount(); i++) {
                    if (projectItem->child(i, 0)->text() == targetDirName) {
                        orbitItem = projectItem->child(i, 0);
                        break;
                    }
                }

                if (!orbitItem) {
                    orbitItem = new QStandardItem(targetDirName);
                    orbitItem->setToolTip(projectName());
                    int insert = 0;
                    for (; insert < projectItem->rowCount(); insert++) {
                        if (projectItem->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                            projectItem->child(insert, 1)->text().compare("complex-1.0") == 0)
                            continue;
                        else
                            break;
                    }
                    orbitItem->setIcon(QIcon(FOLDER_ICON));
                    projectItem->insertRow(insert, orbitItem);
                    QStandardItem* orbitRank = new QStandardItem("complex-1.0");
                    projectItem->setChild(insert, 1, orbitRank);
                }

                // 2. 补全下属图像节点
                for (const QString& h5Path : newH5Paths) {
                    QFileInfo fileinfo(h5Path);
                    QString orbit_img_name = fileinfo.baseName();

                    QStandardItem* item_img = nullptr;
                    for (int j = 0; j < orbitItem->rowCount(); j++) {
                        if (orbitItem->child(j, 0)->text() == orbit_img_name) {
                            item_img = orbitItem->child(j, 0);
                            break;
                        }
                    }

                    if (!item_img) {
                        QStandardItem* orbit_images_name = new QStandardItem(orbit_img_name);
                        orbit_images_name->setToolTip("complex");
                        QStandardItem* orbit_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                        orbit_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                        orbitItem->appendRow(orbit_images_name);
                        orbitItem->setChild(orbitItem->rowCount() - 1, 1, orbit_images_path);
                    } else {
                        orbitItem->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                    }
                }
            }
        }

        auto* iface = NodeUtils::getProjectContext(_widget);
        if (iface) {
            iface->refreshProjectTree();
        }
    }

    // 根据写入结果判定最终状态
    int total = qMax(1, podApplyOk + podApplyFail + podSkipped);
    if (podApplyOk == total) {
        finishExecution();
        qDebug() << "[OrbitNode] all OK, state=Completed";
    } else if (podApplyOk > 0) {
        // 部分成功 → Warning
        setProgress(100);
        Q_EMIT progressUpdated(100);
        setState(ExecutionState::Warning);
        qDebug() << "[OrbitNode] partial success, state=Warning";
    } else {
        // 全部失败 → Error
        setProgress(0);
        Q_EMIT progressUpdated(0);
        setState(ExecutionState::Error);
        if (!_isAutoTriggered) {
            QMessageBox::warning(nullptr, QStringLiteral("轨道应用失败"),
                QStringLiteral("精密轨道下载或写入失败（%1/%2 跳过）。\n请检查网络连接与 Earthdata 认证。")
                    .arg(podSkipped).arg(total));
        }
        qDebug() << "[OrbitNode] all failed, state=Error";
    }
    updateCacheSizeLabel();
}

void Sentinel1OrbitNode::onError(const QString& error)
{
    qDebug() << "[OrbitNode] onError:" << error;
    stopExecution();
    m_workerThread = nullptr;
    m_thread = nullptr;

    setProgress(0);
    Q_EMIT progressUpdated(0);
    setState(ExecutionState::Error);
    if (!_isAutoTriggered)
    {
        QMessageBox::critical(nullptr, "Error", error);
    }
}

bool Sentinel1OrbitNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;

    QString cache = m_cacheDirEdit ? m_cacheDirEdit->text().trimmed() : m_cacheDir.trimmed();
    if (cache.isEmpty())
        return false;

    return true;
}

QStringList Sentinel1OrbitNode::previewImagePaths() const
{
    QStringList jpgPaths;
    if (m_outputData) {
        for (const QString& h5Path : m_outputData->filePaths()) {
            QString jpg = h5Path.left(h5Path.lastIndexOf('.')) + ".jpg";
            if (QFile::exists(jpg)) {
                jpgPaths.append(jpg);
            }
        }
    }
    return jpgPaths;
}

void Sentinel1OrbitNode::updateWidgetSize()
{
    if (_widget) {
        _widget->adjustSize();
    }
}

QStandardItemModel* Sentinel1OrbitNode::projectModel() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString Sentinel1OrbitNode::projectPath() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectPath() : QString();
}

QString Sentinel1OrbitNode::projectName() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* Sentinel1OrbitNode::projectXml() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

// ============================================================================
// Sentinel1OrbitValidationWidget - 精密轨道验证选项卡
// 检查下载的精密轨道是否成功写入每个 H5 的 fine_state_vec 数据集
// ============================================================================
struct OrbitValidationItem
{
    QString filePath;
    bool hasFineStateVec = false;
    int broadcastRows = 0;   // state_vec 行数（广播轨道）
    int preciseRows = 0;     // fine_state_vec 行数（精密轨道）
    QString orbitType;       // 轨道类型属性（如 Precise (POE) / Reconstructed (RES)）
    bool passed = false;
    QString errorMsg;
};

class Sentinel1OrbitValidationWidget : public QWidget
{
public:
    explicit Sentinel1OrbitValidationWidget(Sentinel1OrbitNode* node, QWidget* parent = nullptr)
        : QWidget(parent), m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }
    ~Sentinel1OrbitValidationWidget() override = default;

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
        m_statusTitle = new QLabel(tr("轨道验证中..."), m_statusCard);
        m_statusTitle->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(isDark ? "#60A5FA" : "#2563EB"));
        m_statusDesc = new QLabel(tr("正在检查精密轨道应用状态。"), m_statusCard);
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
        m_compareTable->setHorizontalHeaderLabels({tr("分析项目"), tr("输入 H5"), tr("输出 H5"), tr("状态")});
        m_compareTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        m_compareTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_compareTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        m_compareTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        m_compareTable->setColumnWidth(0, 150);
        m_compareTable->setColumnWidth(3, 80);
        m_compareTable->verticalHeader()->setVisible(false);
        m_compareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_compareTable->setSelectionBehavior(QAbstractItemView::SelectRows);

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
        m_compareTable->setStyleSheet(tableStyle);

        splitter->addWidget(m_fileList);
        splitter->addWidget(m_compareTable);

        connect(m_fileList, &QListWidget::currentItemChanged, this, &Sentinel1OrbitValidationWidget::onFileSelected);
    }

    void startAsyncValidation()
    {
        if (!m_node || m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(tr("验证未通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(tr("节点未完成执行，请先运行 Apply Orbit File。"));
            m_compareTable->setEnabled(false);
            return;
        }

        // 获取输入/输出 H5 文件列表（当前为透传，输入输出路径相同）
        std::shared_ptr<NodeData> outData = m_node->outData(0);
        auto* fileData = dynamic_cast<ImportedFileData*>(outData.get());
        if (!fileData || fileData->filePaths().isEmpty()) {
            m_statusTitle->setText(tr("无数据"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
            m_statusDesc->setText(tr("输出端口没有可用的 H5 文件。"));
            m_compareTable->setEnabled(false);
            return;
        }

        QStringList h5Paths = fileData->filePaths();
        QFuture<void> future = QtConcurrent::run([this, h5Paths]() {
            QStringList existing;
            for (const QString& path : h5Paths) {
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
                    m_statusTitle->setText(tr("就绪"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                    m_statusDesc->setText(tr("已检测到 %1 个 H5 文件。请点击文件查看精密轨道验证结果。").arg(existing.size()));
                    m_compareTable->setEnabled(true);
                    m_fileList->setCurrentRow(0);
                } else {
                    m_statusTitle->setText(tr("文件缺失"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                    m_statusDesc->setText(tr("H5 文件不存在，请检查节点执行状态。"));
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
        m_statusTitle->setText(tr("正在分析..."));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #3B82F6;");

        QFuture<OrbitValidationItem> future = QtConcurrent::run(
            performOrbitValidation, h5Path);

        auto* watcher = new QFutureWatcher<OrbitValidationItem>(this);
        connect(watcher, &QFutureWatcher<OrbitValidationItem>::finished, this,
            [this, watcher]() {
                OrbitValidationItem item = watcher->result();
                watcher->deleteLater();
                displayResults(item);
                QString color = item.passed ? "#10B981" : "#EF4444";
                m_statusTitle->setText(item.passed ? tr("验证通过") : tr("验证失败"));
                m_statusTitle->setStyleSheet(
                    QString("font-size: 14px; font-weight: bold; color: %1;").arg(color));
                m_statusDesc->setText(item.errorMsg.isEmpty() ?
                    tr("精密轨道应用正常。") : item.errorMsg);
            });
        watcher->setFuture(future);
    }

    void displayResults(const OrbitValidationItem& item)
    {
        m_compareTable->setRowCount(0);
        bool isDark = NodeDetailWindow::isDarkTheme(this);

        auto addRow = [&](const QString& name, const QString& inputVal,
                          const QString& outputVal, const QString& status, const QString& color) {
            int r = m_compareTable->rowCount();
            m_compareTable->insertRow(r);
            m_compareTable->setItem(r, 0, new QTableWidgetItem(name));
            m_compareTable->setItem(r, 1, new QTableWidgetItem(inputVal));
            m_compareTable->setItem(r, 2, new QTableWidgetItem(outputVal));
            auto* statusItem = new QTableWidgetItem(status);
            statusItem->setForeground(QBrush(QColor(color)));
            statusItem->setTextAlignment(Qt::AlignCenter);
            QFont boldFont = statusItem->font();
            boldFont.setBold(true);
            statusItem->setFont(boldFont);
            m_compareTable->setItem(r, 3, statusItem);
        };

        // 1. fine_state_vec 存在性
        QString grpOrbit = QStringLiteral("  精密轨道");
        if (item.hasFineStateVec) {
            QString outVal = QStringLiteral("fine_state_vec: %1 行").arg(item.preciseRows);
            if (!item.orbitType.isEmpty()) {
                outVal += QString(" (%1)").arg(item.orbitType);
            }
            addRow(grpOrbit,
                tr("fine_state_vec: 不存在"),
                outVal,
                tr("PASS"),
                "#10B981");
        } else {
            addRow(grpOrbit,
                tr("fine_state_vec: 不存在"),
                tr("fine_state_vec: 不存在"),
                tr("FAILED"),
                "#EF4444");
        }

        // 2. 轨道向量点数
        QString grpPoints = QStringLiteral("  轨道点数");
        addRow(grpPoints,
            QStringLiteral("广播轨道: %1 点").arg(item.broadcastRows),
            item.hasFineStateVec
                ? QStringLiteral("精密轨道: %1 点").arg(item.preciseRows)
                : tr("精密轨道: 0 点"),
            item.hasFineStateVec && item.preciseRows >= 5 ? tr("PASS") : tr("WARNING"),
            item.hasFineStateVec && item.preciseRows >= 5 ? "#10B981" : "#F59E0B");

        // 3. 判定摘要
        QString grpSummary = QStringLiteral("  综合判定");
        QString detailText;
        if (item.hasFineStateVec) {
            int ratio = item.broadcastRows > 0 ? (item.preciseRows * 100 / item.broadcastRows) : 0;
            detailText = tr("精密轨道点数 %1 / 广播轨道点数 %2 ≈ %3%")
                .arg(item.preciseRows).arg(item.broadcastRows).arg(ratio);
        } else {
            detailText = tr("精密轨道未应用。可能原因：EOF 下载失败或文件匹配错误。");
        }
        addRow(grpSummary, detailText, "",
            item.passed ? tr("PASS") : tr("FAILED"),
            item.passed ? "#10B981" : "#EF4444");
    }

    // ========================================================================
    // 静态分析函数（后台线程运行）
    // ========================================================================
    static OrbitValidationItem performOrbitValidation(const QString& h5Path)
    {
        OrbitValidationItem item;
        item.filePath = h5Path;

        FormatConversion FC;
        {
            NodeUtils::Hdf5Locker locker;

            // 检查 fine_state_vec 是否存在
            int r = 0, c = 0;
            item.hasFineStateVec = (FC.get_dataset_dims(
                h5Path.toLocal8Bit().constData(), "fine_state_vec", &r, &c) == 0);
            item.preciseRows = r;

            // 读取广播轨道 state_vec 行数
            r = 0; c = 0;
            FC.get_dataset_dims(
                h5Path.toLocal8Bit().constData(), "state_vec", &r, &c);
            item.broadcastRows = r;

            // 读取写入的轨道类型描述（兼容无此数据集的旧数据）
            if (item.hasFineStateVec) {
                std::string typeStr;
                if (FC.read_str_from_h5(
                    h5Path.toLocal8Bit().constData(), "orbit_type", typeStr) == 0) {
                    item.orbitType = QString::fromStdString(typeStr);
                } else {
                    item.orbitType = tr("Unknown");
                }
            }
        }

        // 判定：fine_state_vec 存在且 ≥5 点 → 精密轨道已应用
        item.passed = item.hasFineStateVec && item.preciseRows >= 5;

        if (!item.hasFineStateVec) {
            item.errorMsg = QStringLiteral("精密轨道未能写入 H5 文件。请检查 EOF 文件是否成功下载。");
        } else if (item.preciseRows < 5) {
            item.errorMsg = QStringLiteral("精密轨道点数不足（%1 < 5），可能为非精密轨道路径。").arg(item.preciseRows);
        }

        return item;
    }

    Sentinel1OrbitNode* m_node = nullptr;
    QFrame* m_statusCard = nullptr;
    QLabel* m_statusTitle = nullptr;
    QLabel* m_statusDesc = nullptr;
    QListWidget* m_fileList = nullptr;
    QTableWidget* m_compareTable = nullptr;
    QFutureWatcher<void> m_watcher;
};

// ============================================================================
// 验证选项卡接口实现
// ============================================================================
::QWidget* Sentinel1OrbitNode::createValidationWidget(::QWidget* parent)
{
    return new Sentinel1OrbitValidationWidget(this, parent);
}

} // namespace QtNodes
