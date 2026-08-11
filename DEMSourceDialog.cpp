#include "DEMSourceDialog.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include "EarthdataLoginDialog.h"
#include "icon_source.h"
#include "FormatConversion.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QSettings>

namespace
{
QString normalizedFilePath(const QString& path)
{
    QFileInfo fileInfo(path);
    QString normalizedPath = fileInfo.canonicalFilePath();
    if (normalizedPath.isEmpty())
    {
        normalizedPath = fileInfo.absoluteFilePath();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(normalizedPath));
}
}

DEMSourceDialog::DEMSourceDialog(QWidget* parent)
    : QDialog(parent)
    , m_model(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setWindowTitle(QStringLiteral("下载与导入外部 DEM 数据"));
    setMinimumWidth(400);

    auto* mainLayout = new QVBoxLayout(this);

    auto* formLayout = new QFormLayout();
    
    m_projectCombo = new QComboBox(this);
    formLayout->addRow(QStringLiteral("目标工程:"), m_projectCombo);

    m_slcCombo = new QComboBox(this);
    formLayout->addRow(QStringLiteral("参考 SLC 影像:"), m_slcCombo);

    m_demSourceCombo = new QComboBox(this);
    m_demSourceCombo->addItem("Copernicus DEM (30m)", 2);
    m_demSourceCombo->addItem("SRTM 1\" (~30m)", 0);
    m_demSourceCombo->addItem("SRTM 3\" (~90m)", 1);
    m_demSourceCombo->addItem("ASTER GDEM v3 (30m)", 3);
    formLayout->addRow(QStringLiteral("DEM 数据源:"), m_demSourceCombo);

    // 账户状态与登录注销按钮
    auto* loginLayout = new QHBoxLayout();
    m_loginStatusLabel = new QLabel(this);
    m_loginBtn = new QPushButton(QStringLiteral("登录"), this);
    m_loginBtn->setFixedWidth(60);
    m_logoutBtn = new QPushButton(QStringLiteral("注销"), this);
    m_logoutBtn->setFixedWidth(60);
    loginLayout->addWidget(m_loginStatusLabel);
    loginLayout->addWidget(m_loginBtn);
    loginLayout->addWidget(m_logoutBtn);
    formLayout->addRow(QStringLiteral("账户状态:"), loginLayout);

    m_dstNodeEdit = new QLineEdit(this);
    m_dstNodeEdit->setText("External_DEM");
    formLayout->addRow(QStringLiteral("保存节点名称:"), m_dstNodeEdit);

    m_resolutionCombo = new QComboBox(this);
    m_resolutionCombo->addItem(QStringLiteral("原始分辨率"));
    m_resolutionCombo->addItem(QStringLiteral("30 米"));
    m_resolutionCombo->addItem(QStringLiteral("90 米"));
    m_resolutionCombo->addItem(QStringLiteral("自定义"));
    m_resolutionCombo->setCurrentIndex(1); // 默认 30m
    formLayout->addRow(QStringLiteral("目标分辨率:"), m_resolutionCombo);

    m_customResEdit = new QLineEdit(this);
    m_customResEdit->setText("30.0");
    m_customResEdit->setEnabled(false);
    formLayout->addRow(QStringLiteral("分辨率(米):"), m_customResEdit);

    auto* cacheLayout = new QHBoxLayout();
    m_cacheDirEdit = new QLineEdit(this);
    m_cacheDirEdit->setObjectName("demPathEdit");
    m_browseCacheBtn = new QPushButton(QStringLiteral("浏览..."), this);
    m_browseCacheBtn->setFixedWidth(60);
    cacheLayout->addWidget(m_cacheDirEdit);
    cacheLayout->addWidget(m_browseCacheBtn);
    formLayout->addRow(QStringLiteral("缓存目录:"), cacheLayout);

    connect(m_cacheDirEdit, &QLineEdit::editingFinished, this, [this]() {
        QString dir = m_cacheDirEdit->text().trimmed();
        auto* iface = NodeUtils::getProjectContext(this);
        if (iface && !dir.isEmpty()) {
            NodeUtils::setGlobalDemPath(iface, dir, true);
        }
    });

    auto* cacheManageLayout = new QHBoxLayout();
    m_cacheSizeLabel = new QLabel(QStringLiteral("当前缓存: 0.00 MB"), this);
    m_clearCacheBtn = new QPushButton(QStringLiteral("清除缓存"), this);
    m_clearCacheBtn->setFixedWidth(80);
    cacheManageLayout->addWidget(m_cacheSizeLabel);
    cacheManageLayout->addWidget(m_clearCacheBtn);
    formLayout->addRow(QStringLiteral("缓存管理:"), cacheManageLayout);

    mainLayout->addLayout(formLayout);

    // 进度条与状态
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    // 按钮布局
    auto* btnLayout = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("开始下载"), this);
    m_cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_projectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DEMSourceDialog::onProjectChanged);
    connect(m_startBtn, &QPushButton::clicked, this, &DEMSourceDialog::onStartPressed);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_worker) {
            m_cancelRequested = true;
            m_worker->StopProcess();
            m_cancelBtn->setEnabled(false);
            m_statusLabel->setText(QStringLiteral("正在取消 DEM 任务..."));
        } else {
            reject();
        }
    });
    connect(m_resolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DEMSourceDialog::onResolutionModeChanged);
    connect(m_browseCacheBtn, &QPushButton::clicked, this, &DEMSourceDialog::onBrowseCachePressed);
    connect(m_clearCacheBtn, &QPushButton::clicked, this, &DEMSourceDialog::onClearCachePressed);

    // 绑定登录注销信号
    connect(m_demSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DEMSourceDialog::updateLoginStatus);
    connect(m_loginBtn, &QPushButton::clicked, this, [this]() {
        EarthdataLoginDialog dlg(this);
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

    updateLoginStatus();
}

DEMSourceDialog::~DEMSourceDialog()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    IApplicationInterface* iface = NodeUtils::getProjectContext(this);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("dialog destroyed"),
                                        iface ? iface->projectXml() : nullptr);
}

void DEMSourceDialog::ShowProjectList(QStandardItemModel* model)
{
    m_model = model;
    m_projectCombo->clear();
    
    for (int i = 0; i < model->rowCount(); ++i) {
        m_projectCombo->addItem(model->item(i, 0)->text());
    }

    if (model->rowCount() > 0) {
        onProjectChanged(0);
    }
}

void DEMSourceDialog::onProjectChanged(int index)
{
    Q_UNUSED(index);
    updateSlcCombo();

    if (m_model && m_projectCombo->currentIndex() >= 0) {
        int projIdx = m_projectCombo->currentIndex();
        QStandardItem* pathItem = m_model->item(projIdx, 1);
        if (pathItem) {
            QString projectPath = pathItem->text();
            auto* iface = NodeUtils::getProjectContext(this);
            QString cacheDir;
            if (iface) {
                cacheDir = NodeUtils::getGlobalDemPath(iface);
            }
            if (cacheDir.isEmpty()) {
                cacheDir = QFileInfo(projectPath).absolutePath() + "/.dem_cache";
            }
            m_cacheDirEdit->setText(QDir::toNativeSeparators(cacheDir));
            updateCacheSizeLabel();
        }
    }
}

void DEMSourceDialog::updateSlcCombo()
{
    m_slcCombo->clear();
    if (!m_model || m_projectCombo->currentIndex() < 0) {
        return;
    }

    int projIdx = m_projectCombo->currentIndex();
    QStandardItem* projectItem = m_model->item(projIdx, 0);
    if (!projectItem) {
        return;
    }

    for (int i = 0; i < projectItem->rowCount(); ++i) {
        QStandardItem* typeItem = projectItem->child(i, 1);
        if (typeItem) {
            QString typeText = typeItem->text();
            if (typeText == "complex-0.0" || typeText == "complex-1.0" || typeText == "complex-2.0") {
                m_slcCombo->addItem(projectItem->child(i, 0)->text());
            }
        }
    }
}

void DEMSourceDialog::onStartPressed()
{
    if (m_projectCombo->currentIndex() < 0 || m_slcCombo->currentIndex() < 0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请确保选中了有效的工程和参考 SLC 影像！"));
        return;
    }

    QString dstNode = m_dstNodeEdit->text().trimmed();
    if (dstNode.isEmpty()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("请输入保存的节点名称！"));
        return;
    }

    int projIdx = m_projectCombo->currentIndex();
    QStandardItem* projectItem = m_model->item(projIdx, 0);
    QStandardItem* pathItem = m_model->item(projIdx, 1);
    if (!projectItem || !pathItem) {
        return;
    }

    QString projectPath = pathItem->text();
    QString projectName = projectItem->text();

    // 查找选中的 SLC 影像节点并获取其子影像 H5 路径
    QString slcNodeName = m_slcCombo->currentText();
    QStandardItem* slcNode = nullptr;
    for (int i = 0; i < projectItem->rowCount(); ++i) {
        if (projectItem->child(i, 0)->text() == slcNodeName) {
            slcNode = projectItem->child(i, 0);
            break;
        }
    }

    if (!slcNode || slcNode->rowCount() <= 0) {
        QMessageBox::warning(this, "Warning", QStringLiteral("选中的参考 SLC 影像节点中没有有效的影像文件！"));
        return;
    }

    QStringList filePaths;
    for (int i = 0; i < slcNode->rowCount(); ++i) {
        QStandardItem* fileTypeItem = slcNode->child(i, 1);
        if (fileTypeItem) {
            filePaths.append(fileTypeItem->text());
        }
    }

    int demSource = m_demSourceCombo->currentData().toInt();
    if (demSource != 2) // Copernicus DEM 不需要登录
    {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
        QString encryptedPass = settings.value("DEM/EarthdataPassword", "").toString();
        if (encryptedUser.isEmpty() || encryptedPass.isEmpty())
        {
            QMessageBox::warning(this, "Warning", QStringLiteral("所选 DEM 数据源需要 NASA Earthdata 账户登录，请先登录！"));
            EarthdataLoginDialog dlg(this);
            if (dlg.exec() != QDialog::Accepted)
            {
                return;
            }
            updateLoginStatus();
        }
    }
    
    // 解析分辨率参数
    int resIdx = m_resolutionCombo->currentIndex();
    double targetResolution = 0.0;
    if (resIdx == 1) targetResolution = 30.0;
    else if (resIdx == 2) targetResolution = 90.0;
    else if (resIdx == 3) {
        bool ok = false;
        double val = m_customResEdit->text().toDouble(&ok);
        if (!ok || val <= 0.0) {
            QMessageBox::warning(this, "Warning", QStringLiteral("自定义分辨率必须是有效正数！"));
            return;
        }
        targetResolution = val;
    }

    // 获取缓存目录
    QString cacheDir = m_cacheDirEdit->text().trimmed();
    if (cacheDir.isEmpty()) {
        QMessageBox::warning(this, "Warning", QStringLiteral("缓存目录不能为空！"));
        return;
    }
    QDir().mkpath(cacheDir);

    IApplicationInterface* iface = NodeUtils::getProjectContext(this);
    const QString xmlPath = NodeUtils::getProjectFilePath(this);
    const QString projectRoot = projectPath.endsWith(QStringLiteral(".insar"), Qt::CaseInsensitive)
        ? QFileInfo(projectPath).absolutePath() : QDir(projectPath).absolutePath();
    const QString projectFileName = projectName.endsWith(QStringLiteral(".insar"), Qt::CaseInsensitive)
        ? projectName : projectName + QStringLiteral(".insar");
    const QString selectedXmlPath = projectPath.endsWith(QStringLiteral(".insar"), Qt::CaseInsensitive)
        ? projectPath : QDir(projectRoot).absoluteFilePath(projectFileName);
    const QString normalizedActiveXmlPath = normalizedFilePath(xmlPath);
    const QString normalizedSelectedXmlPath = normalizedFilePath(selectedXmlPath);
    if (!iface || !iface->projectXml() || normalizedActiveXmlPath.isEmpty() ||
        normalizedSelectedXmlPath.isEmpty() ||
        normalizedActiveXmlPath.compare(normalizedSelectedXmlPath, Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, QStringLiteral("无法启动"),
                             QStringLiteral("所选工程不是当前可提交的工程 XML 上下文，无法安全写入 DEM 成果。"));
        return;
    }

    m_cancelRequested = false;
    m_preparedProjectRoot = projectRoot;
    m_preparedProjectName = projectName;
    m_preparedDstNode = dstNode;
    m_preparedInputPaths = filePaths;
    m_preparedDemSource = demSource;
    m_preparedResolution = targetResolution;
    m_preparedXmlPath = normalizedActiveXmlPath;
    const QDir outputDirectory(projectRoot + "/" + dstNode);
    m_preparedOutputPaths = QStringList()
        << outputDirectory.absoluteFilePath(dstNode + "_dem.h5")
        << outputDirectory.absoluteFilePath(dstNode + "_dem.tif");
    const NodeUtils::OverwriteResult overwriteResult = NodeUtils::checkAndPromptOverwrite(
        iface, m_preparedDstNode, m_preparedOutputPaths, this);
    if (overwriteResult == NodeUtils::OverwriteResult::Cancel) {
        return;
    }
    if (overwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        QStringList committedPaths;
        if (!NodeUtils::loadCommittedOutputManifest(m_preparedProjectRoot, m_preparedDstNode,
                                                    committedPaths)) {
            QMessageBox::warning(this, QStringLiteral("无法加载现有成果"),
                                 QStringLiteral("现有 DEM 目录不是已提交的事务成果，请重新运行以生成可验证输出。"));
            return;
        }
        QMessageBox::information(this, QStringLiteral("已保留现有成果"),
                                 QStringLiteral("已保留现有已提交的 DEM 成果，未执行下载。"));
        Q_EMIT sendCopy(m_model);
        accept();
        return;
    }
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedProjectRoot, m_preparedDstNode,
                                           m_preparedOutputPaths, m_preparedInputPaths,
                                           m_outputTransaction, &transactionError, nullptr, selectedXmlPath)) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, transactionError, iface->projectXml());
        QMessageBox::critical(this, QStringLiteral("无法启动"), transactionError);
        return;
    }

    // 锁定 UI 开始下载
    m_projectCombo->setEnabled(false);
    m_slcCombo->setEnabled(false);
    m_demSourceCombo->setEnabled(false);
    m_dstNodeEdit->setEnabled(false);
    m_resolutionCombo->setEnabled(false);
    m_customResEdit->setEnabled(false);
    m_cacheDirEdit->setEnabled(false);
    m_browseCacheBtn->setEnabled(false);
    m_clearCacheBtn->setEnabled(false);
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    m_progressBar->show();
    m_progressBar->setValue(0);
    m_statusLabel->setText(QStringLiteral("正在初始化 DEM 下载与裁剪任务..."));

    // 启动线程
    m_worker = new DEMSourceWorker();
    m_thread = new QThread();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    connect(m_worker, &DEMSourceWorker::updateProcess, this, &DEMSourceDialog::onProgressUpdate);
    connect(m_worker, &DEMSourceWorker::errorProcess, this, &DEMSourceDialog::onError);
    connect(m_worker, &DEMSourceWorker::cancelled, this, &DEMSourceDialog::onCancelled);
    connect(m_worker, &DEMSourceWorker::cancelled, m_thread, &QThread::quit);
    connect(m_worker, &DEMSourceWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &DEMSourceWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &DEMSourceWorker::demFetchFinished, this, &DEMSourceDialog::onDemFetchFinished);

    DEMSourceWorker* const worker = m_worker;
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, m_worker,
            [worker, projectRoot, projectName, stagingNode, dstNode, filePaths, demSource,
             targetResolution, cacheDir]() {
        worker->fetch_dem(projectRoot, projectName, stagingNode, dstNode, filePaths,
                          demSource, targetResolution, cacheDir);
    });

    m_thread->start();
}

void DEMSourceDialog::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    m_progressBar->setValue(progress);
    m_statusLabel->setText(QStringLiteral("处理进度: %1%").arg(progress));
}

void DEMSourceDialog::onError(const QString& error)
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
        m_worker = nullptr;
    }
    IApplicationInterface* iface = NodeUtils::getProjectContext(this);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, iface ? iface->projectXml() : nullptr);

    QMessageBox::critical(this, "Error", error);

    restoreUiAfterFailure();
}

void DEMSourceDialog::onCancelled()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
        m_worker = nullptr;
    }
    IApplicationInterface* iface = NodeUtils::getProjectContext(this);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"),
                                        iface ? iface->projectXml() : nullptr);
    restoreUiAfterFailure();
    reject();
}

void DEMSourceDialog::restoreUiAfterFailure()
{
    m_projectCombo->setEnabled(true);
    m_slcCombo->setEnabled(true);
    m_demSourceCombo->setEnabled(true);
    m_dstNodeEdit->setEnabled(true);
    m_resolutionCombo->setEnabled(true);
    m_customResEdit->setEnabled(m_resolutionCombo->currentIndex() == 3);
    m_cacheDirEdit->setEnabled(true);
    m_browseCacheBtn->setEnabled(true);
    m_clearCacheBtn->setEnabled(true);
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(true);
    m_progressBar->hide();
    m_statusLabel->clear();
}

void DEMSourceDialog::onFinished()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
        m_worker = nullptr;
    }

    QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("DEM 数据下载与拼接裁剪完成，并已成功导入项目！"));
    emit sendCopy(m_model);
    accept();
}

void DEMSourceDialog::onDemFetchFinished(
    const QString& outputH5Path,
    const QString& stagingNode,
    const QString& projectName,
    int demSource,
    double targetResolution
)
{
    if (m_cancelRequested) {
        onCancelled();
        return;
    }

    QString transactionError;
    if (!commitOutputTransaction(outputH5Path, stagingNode, projectName, demSource,
                                 targetResolution, &transactionError)) {
        if (m_cancelRequested) {
            onCancelled();
            return;
        }
        onError(transactionError);
        return;
    }
    onFinished();
}

bool DEMSourceDialog::commitOutputTransaction(const QString& stagedH5Path,
                                              const QString& stagingNode,
                                              const QString& projectName,
                                              int demSource,
                                              double targetResolution,
                                              QString* errorMessage)
{
    auto* iface = NodeUtils::getProjectContext(this);
    XMLFile* xml = iface ? iface->projectXml() : nullptr;
    const QString xmlPath = NodeUtils::getProjectFilePath(this);
    if (m_cancelRequested || !xml || xmlPath.isEmpty() ||
        normalizedFilePath(xmlPath).compare(m_preparedXmlPath, Qt::CaseInsensitive) != 0 ||
        stagingNode != m_outputTransaction.stagingName ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, errorMessage) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
                                              QStringList() << QStringLiteral("dem")
                                                            << QStringLiteral("dem_x")
                                                            << QStringLiteral("dem_y")
                                                            << QStringLiteral("dem_z")
                                                            << QStringLiteral("lon")
                                                            << QStringLiteral("lat"),
                                              errorMessage) ||
        !NodeUtils::workerOutputsMatchManifest(QStringList() << m_preparedOutputPaths.first(),
                                               QStringList() << stagedH5Path, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("DEM staging output did not pass transaction validation.");
        }
        return false;
    }

    QStringList finalPaths;
    if (m_cancelRequested) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DEM task was cancelled before transaction commit.");
        }
        return false;
    }
    if (!NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, errorMessage) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, xml, xmlPath, errorMessage)) {
        return false;
    }

    QString h5Path;
    for (const QString& finalPath : finalPaths) {
        if (finalPath.endsWith(QStringLiteral(".h5"), Qt::CaseInsensitive)) {
            h5Path = finalPath;
            break;
        }
    }
    if (h5Path.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Promoted DEM transaction has no H5 output.");
        return false;
    }

    NodeUtils::removeDataNodeFromProject(iface, m_preparedDstNode, false, false);
    std::string srcName = "SRTM1";
    if (demSource == 1) srcName = "SRTM3";
    else if (demSource == 2) srcName = "Copernicus";
    else if (demSource == 3) srcName = "ASTER";
    xml->XMLFile_add_dem(
        m_preparedDstNode.toStdString().c_str(),
        (m_preparedDstNode + "_dem").toStdString().c_str(),
        ("/" + m_preparedDstNode + "/" + QFileInfo(h5Path).fileName()).toStdString().c_str(),
        0, 0, srcName.c_str(), targetResolution
    );
    if (!NodeUtils::saveProjectXmlAtomically(xml, xmlPath, errorMessage) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, errorMessage)) {
        return false;
    }

    QStandardItemModel* model = iface->projectModel() ? iface->projectModel() : m_model;
    if (model)
    {
        NodeUtils::removeDataNodeFromProjectTree(iface, m_preparedDstNode);
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
                if (project->child(i, 0)->text() == m_preparedDstNode)
                {
                    demNode = project->child(i, 0);
                    break;
                }
            }

            if (!demNode)
            {
                demNode = new QStandardItem(m_preparedDstNode);
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
            QString imgName = m_preparedDstNode + "_dem";
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
                demNode->setChild(demNode->rowCount() - 1, 1, new QStandardItem(h5Path));
            }
            else
            {
                demNode->setChild(itemImg->row(), 1, new QStandardItem(h5Path));
            }
        }
    }
    iface->refreshProjectTree();
    return true;
}

void DEMSourceDialog::onResolutionModeChanged(int index)
{
    m_customResEdit->setEnabled(index == 3);
}

void DEMSourceDialog::onBrowseCachePressed()
{
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 DEM 缓存目录"), m_cacheDirEdit->text());
    if (!dir.isEmpty()) {
        m_cacheDirEdit->setText(QDir::toNativeSeparators(dir));
        updateCacheSizeLabel();

        auto* iface = NodeUtils::getProjectContext(this);
        if (iface) {
            NodeUtils::setGlobalDemPath(iface, QDir::toNativeSeparators(dir), true);
        }
    }
}

void DEMSourceDialog::onClearCachePressed()
{
    QString cacheDir = m_cacheDirEdit->text().trimmed();
    if (cacheDir.isEmpty() || !QDir(cacheDir).exists()) {
        QMessageBox::information(this, "Info", QStringLiteral("缓存目录不存在，无需清理。"));
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("清除缓存"), QStringLiteral("是否清除缓存目录 %1 下的所有缓存文件？").arg(cacheDir)) == QMessageBox::Yes) {
        QDir dir(cacheDir);
        dir.removeRecursively();
        dir.mkpath(".");
        updateCacheSizeLabel();
        QMessageBox::information(this, "Info", QStringLiteral("缓存清理完成！"));
    }
}

void DEMSourceDialog::updateCacheSizeLabel()
{
    QString cacheDir = m_cacheDirEdit->text().trimmed();
    if (cacheDir.isEmpty() || !QDir(cacheDir).exists()) {
        m_cacheSizeLabel->setText(QStringLiteral("当前缓存: 0.00 MB"));
        return;
    }

    qint64 totalSize = 0;
    QDir dir(cacheDir);
    QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &fileInfo : list) {
        if (fileInfo.isDir()) {
            std::function<qint64(const QString&)> getDirSize = [&](const QString& path) -> qint64 {
                qint64 size = 0;
                QDir d(path);
                QFileInfoList fl = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
                for (const QFileInfo &fi : fl) {
                    if (fi.isDir()) {
                        size += getDirSize(fi.absoluteFilePath());
                    } else {
                        size += fi.size();
                    }
                }
                return size;
            };
            totalSize += getDirSize(fileInfo.absoluteFilePath());
        } else {
            totalSize += fileInfo.size();
        }
    }

    double sizeMb = static_cast<double>(totalSize) / (1024.0 * 1024.0);
    m_cacheSizeLabel->setText(QStringLiteral("当前缓存: %1 MB").arg(QString::number(sizeMb, 'f', 2)));
}

void DEMSourceDialog::updateLoginStatus()
{
    int demSource = m_demSourceCombo->currentData().toInt();
    if (demSource == 2) // Copernicus DEM
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
