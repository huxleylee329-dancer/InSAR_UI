#include "OrbitSourceDialog.h"
#include "NodeUtils.h"
#include "EarthdataLoginDialog.h"
#include "CDSELoginDialog.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QSettings>
#include <QCoreApplication>

OrbitSourceDialog::OrbitSourceDialog(QWidget* parent)
    : QDialog(parent)
    , m_model(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setWindowTitle(QStringLiteral("Sentinel-1 精密轨道数据管理器"));
    setMinimumWidth(450);

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    m_projectCombo = new QComboBox(this);
    formLayout->addRow(QStringLiteral("目标工程:"), m_projectCombo);

    m_slcCombo = new QComboBox(this);
    formLayout->addRow(QStringLiteral("参考 SLC 影像:"), m_slcCombo);

    m_orbitSourceCombo = new QComboBox(this);
    m_orbitSourceCombo->addItem("NASA ASF (S1 Aux Orbits)");
    m_orbitSourceCombo->addItem("ESA CDSE (Copernicus)");
    formLayout->addRow(QStringLiteral("轨道数据源:"), m_orbitSourceCombo);

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
    formLayout->addRow(QStringLiteral("数据源账户:"), loginLayout);

    // 缓存文件夹配置
    auto* cacheLayout = new QHBoxLayout();
    m_cacheDirEdit = new QLineEdit(this);
    m_browseCacheBtn = new QPushButton(QStringLiteral("浏览..."), this);
    m_browseCacheBtn->setFixedWidth(60);
    cacheLayout->addWidget(m_cacheDirEdit);
    cacheLayout->addWidget(m_browseCacheBtn);
    formLayout->addRow(QStringLiteral("轨道存放目录:"), cacheLayout);

    // 读取全局默认轨道路径
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString lastOrbitDir = settings.value("Orbit/LastMatchDir", "").toString();
    if (lastOrbitDir.isEmpty())
    {
        lastOrbitDir = QCoreApplication::applicationDirPath() + "/orbits";
    }
    m_cacheDirEdit->setText(QDir::toNativeSeparators(lastOrbitDir));

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
    m_cancelBtn = new QPushButton(QStringLiteral("关闭"), this);
    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_projectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &OrbitSourceDialog::onProjectChanged);
    connect(m_startBtn, &QPushButton::clicked, this, &OrbitSourceDialog::onStartPressed);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(m_browseCacheBtn, &QPushButton::clicked, this, &OrbitSourceDialog::onBrowseCachePressed);
    connect(m_clearCacheBtn, &QPushButton::clicked, this, &OrbitSourceDialog::onClearCachePressed);

    connect(m_orbitSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int) { updateLoginStatus(); });

    // 绑定登录注销信号
    connect(m_loginBtn, &QPushButton::clicked, this, [this]() {
        const bool useCdse = m_orbitSourceCombo->currentIndex() == 1;
        const int result = useCdse
            ? CDSELoginDialog(this).exec()
            : EarthdataLoginDialog(this).exec();
        if (result == QDialog::Accepted) {
            updateLoginStatus();
        }
    });
    connect(m_logoutBtn, &QPushButton::clicked, this, [this]() {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        if (m_orbitSourceCombo->currentIndex() == 1) {
            settings.remove("Orbit/CDSEUser");
            settings.remove("Orbit/CDSEPassword");
        } else {
            settings.remove("DEM/EarthdataUser");
            settings.remove("DEM/EarthdataPassword");
        }
        updateLoginStatus();
    });

    updateLoginStatus();
    updateCacheSizeLabel();
}

OrbitSourceDialog::~OrbitSourceDialog()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

void OrbitSourceDialog::ShowProjectList(QStandardItemModel* model)
{
    m_model = model;
    m_projectCombo->clear();
    if (!m_model) return;

    for (int i = 0; i < m_model->rowCount(); ++i)
    {
        QStandardItem* item = m_model->item(i);
        if (item)
        {
            m_projectCombo->addItem(item->text());
        }
    }
}

void OrbitSourceDialog::onProjectChanged(int index)
{
    Q_UNUSED(index);
    updateSlcCombo();

    QString projName = m_projectCombo->currentText();
    if (!projName.isEmpty())
    {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        QString projOrbitDir = settings.value(QString("Orbit/ProjectDir_%1").arg(projName), "").toString();
        if (!projOrbitDir.isEmpty())
        {
            m_cacheDirEdit->setText(QDir::toNativeSeparators(projOrbitDir));
        }
        else
        {
            QString lastOrbitDir = settings.value("Orbit/LastMatchDir", "").toString();
            if (lastOrbitDir.isEmpty())
            {
                lastOrbitDir = QDir::currentPath() + "/orbits";
            }
            m_cacheDirEdit->setText(QDir::toNativeSeparators(lastOrbitDir));
        }
        updateCacheSizeLabel();
    }
}

void OrbitSourceDialog::updateSlcCombo()
{
    m_slcCombo->clear();
    if (!m_model) return;

    QString currentProject = m_projectCombo->currentText();
    if (currentProject.isEmpty()) return;

    m_slcCombo->addItem(QStringLiteral("[全部 SLC 影像]"));

    QList<QStandardItem*> found = m_model->findItems(currentProject);
    if (!found.isEmpty())
    {
        QStandardItem* projItem = found.first();
        for (int i = 0; i < projItem->rowCount(); ++i)
        {
            QStandardItem* nodeItem = projItem->child(i);
            // 筛选 Sentinel-1 相关的导入或裁剪节点
            if (nodeItem && (nodeItem->text().contains("Import") || nodeItem->text().contains("Cut") || nodeItem->text().contains("S1")))
            {
                for (int j = 0; j < nodeItem->rowCount(); ++j)
                {
                    QStandardItem* fileItem = nodeItem->child(j, 0);
                    QStandardItem* pathItem = nodeItem->child(j, 1);
                    if (fileItem && pathItem && QFileInfo(pathItem->text()).suffix().compare("h5", Qt::CaseInsensitive) == 0)
                    {
                        m_slcCombo->addItem(fileItem->text(), pathItem->text());
                    }
                }
            }
        }
    }
}

void OrbitSourceDialog::onStartPressed()
{
    if (m_worker && m_thread && m_thread->isRunning())
    {
        // 如果处于运行中，此按钮变为“中止下载”
        m_worker->StopProcess();
        m_startBtn->setEnabled(false);
        m_statusLabel->setText(QStringLiteral("正在请求中止..."));
        return;
    }

    // 检查所选数据源的账户
    const int selectedSource = m_orbitSourceCombo->currentIndex();
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    const bool useCdse = selectedSource == 1;
    const QString userKey = useCdse ? "Orbit/CDSEUser" : "DEM/EarthdataUser";
    const QString passKey = useCdse ? "Orbit/CDSEPassword" : "DEM/EarthdataPassword";
    if (settings.value(userKey, "").toString().isEmpty()
        || settings.value(passKey, "").toString().isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"),
            QStringLiteral("请先登录所选的 %1 轨道数据源账户。")
                .arg(useCdse ? QStringLiteral("ESA CDSE") : QStringLiteral("NASA Earthdata")));
        return;
    }

    int projIdx = m_projectCombo->currentIndex();
    if (projIdx < 0)
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先选择一个有效的工程。"));
        return;
    }
    QStandardItem* projectItem = m_model->item(projIdx, 0);
    QStandardItem* pathItem = m_model->item(projIdx, 1);
    if (!projectItem || !pathItem)
    {
        return;
    }
    QString projectPath = pathItem->text();
    QString projName = projectItem->text();

    // 确定待下载的 SLC H5 文件路径列表
    QStringList targetFiles;
    if (m_slcCombo->currentIndex() == 0) // 全部影像
    {
        for (int i = 1; i < m_slcCombo->count(); ++i)
        {
            QString path = m_slcCombo->itemData(i).toString();
            if (!path.isEmpty()) targetFiles.append(path);
        }
    }
    else
    {
        QString path = m_slcCombo->currentData().toString();
        if (!path.isEmpty()) targetFiles.append(path);
    }

    if (targetFiles.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未在此工程下检测到任何可配准的 H5 影像，请先完成数据导入！"));
        return;
    }

    QString cacheDir = m_cacheDirEdit->text().trimmed();
    if (cacheDir.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请选择有效的轨道文件存放目录。"));
        return;
    }

    // 保存本次使用的路径到全局 Config 和项目专属 Config
    settings.setValue("Orbit/LastMatchDir", cacheDir);
    settings.setValue(QString("Orbit/ProjectDir_%1").arg(projName), cacheDir);

    // 锁界面控件
    m_projectCombo->setEnabled(false);
    m_slcCombo->setEnabled(false);
    m_orbitSourceCombo->setEnabled(false);
    m_cacheDirEdit->setEnabled(false);
    m_browseCacheBtn->setEnabled(false);
    m_clearCacheBtn->setEnabled(false);

    m_progressBar->show();
    m_progressBar->setValue(0);
    m_startBtn->setText(QStringLiteral("中止下载"));

    // 建立 Worker 和 Thread
    m_thread = new QThread(this);
    m_worker = new OrbitSourceWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_thread, &QThread::started, [this, projectPath, targetFiles, cacheDir, selectedSource]() {
        emit startOrbitFetch(projectPath, targetFiles, selectedSource, cacheDir);
    });

    connect(this, &OrbitSourceDialog::startOrbitFetch, m_worker, &OrbitSourceWorker::fetch_orbits);
    connect(m_worker, &OrbitSourceWorker::updateProcess, this, &OrbitSourceDialog::onProgressUpdate);
    connect(m_worker, &OrbitSourceWorker::errorProcess, this, &OrbitSourceDialog::onError);
    connect(m_worker, &OrbitSourceWorker::endProcess, this, &OrbitSourceDialog::onFinished);

    m_thread->start();
}

void OrbitSourceDialog::onProgressUpdate(int progress, const QString& message)
{
    m_progressBar->setValue(progress);
    m_statusLabel->setText(message);
}

void OrbitSourceDialog::onError(const QString& error)
{
    QMessageBox::critical(this, QStringLiteral("错误"), error);
    onFinished();
}

void OrbitSourceDialog::onFinished()
{
    m_statusLabel->setText(QStringLiteral("下载完成！"));
    m_progressBar->hide();

    m_projectCombo->setEnabled(true);
    m_slcCombo->setEnabled(true);
    m_orbitSourceCombo->setEnabled(true);
    m_cacheDirEdit->setEnabled(true);
    m_browseCacheBtn->setEnabled(true);
    m_clearCacheBtn->setEnabled(true);

    m_startBtn->setEnabled(true);
    m_startBtn->setText(QStringLiteral("开始下载"));

    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    m_worker = nullptr;

    updateCacheSizeLabel();
}

void OrbitSourceDialog::onBrowseCachePressed()
{
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择精轨数据缓存目录"), m_cacheDirEdit->text());
    if (!dir.isEmpty())
    {
        m_cacheDirEdit->setText(QDir::toNativeSeparators(dir));
        updateCacheSizeLabel();
    }
}

void OrbitSourceDialog::onClearCachePressed()
{
    if (m_thread && m_thread->isRunning())
    {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("当前轨道下载任务正在运行中，无法执行缓存清理！"));
        return;
    }

    QString cacheDir = m_cacheDirEdit->text().trimmed();
    if (cacheDir.isEmpty() || !QDir(cacheDir).exists()) return;

    auto result = QMessageBox::question(this, QStringLiteral("确认"), QStringLiteral("确定要清空该目录下的所有轨道缓存文件（.EOF/.EOF.part）吗？"));
    if (result == QMessageBox::Yes)
    {
        QDir dir(cacheDir);
        QStringList filters;
        filters << "*.EOF" << "*.part" << "*.EOF.bad";
        for (const QString& file : dir.entryList(filters, QDir::Files))
        {
            dir.remove(file);
        }
        updateCacheSizeLabel();
    }
}

void OrbitSourceDialog::updateCacheSizeLabel()
{
    QString cacheDir = m_cacheDirEdit->text().trimmed();
    if (cacheDir.isEmpty() || !QDir(cacheDir).exists())
    {
        m_cacheSizeLabel->setText(QStringLiteral("当前缓存: 0.00 MB"));
        return;
    }

    qint64 totalSize = 0;
    QDir dir(cacheDir);
    QStringList filters;
    filters << "*.EOF" << "*.part" << "*.EOF.bad";
    for (const QString& file : dir.entryList(filters, QDir::Files))
    {
        totalSize += QFileInfo(cacheDir + "/" + file).size();
    }

    double sizeInMB = static_cast<double>(totalSize) / (1024.0 * 1024.0);
    m_cacheSizeLabel->setText(QStringLiteral("当前缓存: %1 MB").arg(sizeInMB, 0, 'f', 2));
}

void OrbitSourceDialog::updateLoginStatus()
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    const bool useCdse = m_orbitSourceCombo->currentIndex() == 1;
    const QString userKey = useCdse ? "Orbit/CDSEUser" : "DEM/EarthdataUser";
    QString user = settings.value(userKey, "").toString();
    if (!user.isEmpty())
    {
        QString plainUser = QString::fromUtf8(QByteArray::fromBase64(user.toUtf8()));
        m_loginStatusLabel->setText(QStringLiteral("%1 已授权 (%2)")
            .arg(useCdse ? QStringLiteral("CDSE") : QStringLiteral("Earthdata"), plainUser));
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
