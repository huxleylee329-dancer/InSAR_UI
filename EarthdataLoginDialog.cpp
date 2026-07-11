#include "EarthdataLoginDialog.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include "NodeUtils.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QTimer>
#include <QUrl>

EarthdataLoginDialog::EarthdataLoginDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("NASA Earthdata 登录"));
    setMinimumWidth(380);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    // 提示及注册超链接
    auto* helpLabel = new QLabel(this);
    helpLabel->setText(QStringLiteral("提示：下载 SRTM 或 ASTER GDEM 数据需要 NASA Earthdata 账户，并在账户中授权LP DAAC Data Pool权限。<br>"
                                      "如果您还没有账户，请 <a href=\"https://urs.earthdata.nasa.gov/users/new\">点击此处注册</a>。"));
    helpLabel->setOpenExternalLinks(true);
    helpLabel->setWordWrap(true);
    helpLabel->setTextFormat(Qt::RichText);
    mainLayout->addWidget(helpLabel);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(8);

    m_userEdit = new QLineEdit(this);
    m_userEdit->setPlaceholderText(QStringLiteral("请输入 Earthdata 用户名"));
    formLayout->addRow(QStringLiteral("用户名:"), m_userEdit);

    m_passEdit = new QLineEdit(this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    formLayout->addRow(QStringLiteral("密  码:"), m_passEdit);

    mainLayout->addLayout(formLayout);

    // 状态提示及进度条
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: gray;");
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0); // 无限循环进度条
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    // 按钮布局
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_loginBtn = new QPushButton(QStringLiteral("登录"), this);
    m_loginBtn->setDefault(true);
    m_cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    btnLayout->addWidget(m_loginBtn);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    // 从 Config.ini 加载已存用户名，方便用户重新输入密码
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
    if (!encryptedUser.isEmpty())
    {
        QString username = QString::fromUtf8(QByteArray::fromBase64(encryptedUser.toUtf8()));
        m_userEdit->setText(username);
        m_passEdit->setFocus();
    }
    else
    {
        m_userEdit->setFocus();
    }

    connect(m_loginBtn, &QPushButton::clicked, this, &EarthdataLoginDialog::onLoginPressed);
    connect(m_cancelBtn, &QPushButton::clicked, this, &EarthdataLoginDialog::onCancelPressed);
}

EarthdataLoginDialog::~EarthdataLoginDialog()
{
    if (m_activeReply)
    {
        m_activeReply->abort();
    }
}

void EarthdataLoginDialog::reject()
{
    if (m_activeReply)
    {
        m_activeReply->abort();
    }
    QDialog::reject();
}

void EarthdataLoginDialog::onLoginPressed()
{
    QString username = m_userEdit->text().trimmed();
    QString password = m_passEdit->text();

    if (username.isEmpty() || password.isEmpty())
    {
        m_statusLabel->setText(QStringLiteral("用户名和密码不能为空！"));
        m_statusLabel->setStyleSheet("color: red;");
        return;
    }

    // 锁定界面，显示进度
    m_userEdit->setEnabled(false);
    m_passEdit->setEnabled(false);
    m_loginBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);
    m_progressBar->show();
    m_statusLabel->setText(QStringLiteral("正在向 NASA 验证账户信息..."));
    m_statusLabel->setStyleSheet("color: blue;");

    // 启动网络请求校验凭据
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    
    // 使用 SRTMGL1 目录路径进行身份验证测试（避免单张瓦片不存在的问题）
    QUrl testUrl("https://data.lpdaac.earthdatacloud.nasa.gov/lp-prod-protected/SRTMGL1.003/N06E016.SRTMGL1.hgt/N06E016.SRTMGL1.hgt.zip");
    QNetworkRequest request(testUrl);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    QByteArray authHeader = "Basic " + QByteArray(QString("%1:%2").arg(username).arg(password).toUtf8()).toBase64();
    request.setRawHeader("Authorization", authHeader);

    // 10秒超时定时器
    QTimer* timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);

    connect(manager, &QNetworkAccessManager::authenticationRequired,
            this, [username, password](QNetworkReply*, QAuthenticator* authenticator) {
                authenticator->setUser(username);
                authenticator->setPassword(password);
            });

    m_activeReply = manager->get(request);

    // 清理回调
    auto cleanup = [this, manager, timeoutTimer]() {
        if (m_activeReply)
        {
            m_activeReply->deleteLater();
            m_activeReply = nullptr;
        }
        manager->deleteLater();
        timeoutTimer->stop();
        timeoutTimer->deleteLater();

        m_userEdit->setEnabled(true);
        m_passEdit->setEnabled(true);
        m_loginBtn->setEnabled(true);
        m_cancelBtn->setEnabled(true);
        m_progressBar->hide();
    };

    // 超时处理
    connect(timeoutTimer, &QTimer::timeout, this, [this, cleanup, username, password]() {
        if (m_activeReply)
        {
            m_activeReply->abort();
        }
        cleanup();
        
        int r = QMessageBox::question(this, QStringLiteral("连接超时"),
            QStringLiteral("验证超时，可能由于网络延迟或缺少代理设置。\n是否仍然强制保存该登录信息？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (r == QMessageBox::Yes)
        {
            saveCredentials(username, password);
            accept();
        }
        else
        {
            m_statusLabel->setText(QStringLiteral("验证超时，未保存信息。"));
            m_statusLabel->setStyleSheet("color: red;");
        }
    });

    // 结束处理
    connect(m_activeReply, &QNetworkReply::finished, this, [this, cleanup, username, password]() {
        if (!m_activeReply)
        {
            return;
        }
        // 如果已经被超时定时器 abort 掉了，直接返回
        if (m_activeReply->error() == QNetworkReply::OperationCanceledError)
        {
            return;
        }

        int statusCode = m_activeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QNetworkReply::NetworkError err = m_activeReply->error();
        QString contentType = m_activeReply->header(QNetworkRequest::ContentTypeHeader).toString();

        if (err == QNetworkReply::NoError && (statusCode == 200 || statusCode == 206 || statusCode == 302) && !contentType.contains("html", Qt::CaseInsensitive))
        {
            cleanup();
            saveCredentials(username, password);
            QMessageBox::information(this, QStringLiteral("登录成功"), QStringLiteral("NASA Earthdata 账户验证成功！已保存登录信息。"));
            accept();
        }
        else if (contentType.contains("html", Qt::CaseInsensitive) || err == QNetworkReply::AuthenticationRequiredError || statusCode == 401)
        {
            cleanup();
            m_passEdit->clear();
            m_statusLabel->setText(QStringLiteral("用户名或密码错误，请重新输入。"));
            m_statusLabel->setStyleSheet("color: red;");
            QMessageBox::critical(this, QStringLiteral("登录失败"), QStringLiteral("用户名或密码错误，验证失败！"));
        }
        else if (statusCode == 404 || err == QNetworkReply::ContentNotFoundError)
        {
            cleanup();
            m_statusLabel->setText(QStringLiteral("未找到测试资源 (404)"));
            m_statusLabel->setStyleSheet("color: red;");
            QMessageBox::critical(this, QStringLiteral("登录失败"), QStringLiteral("未找到验证测试文件资源 (404)，可能测试 URL 无效，请联系管理员。"));
        }
        else
        {
            cleanup();
            QString errorMsg = m_activeReply->errorString();
            int r = QMessageBox::question(this, QStringLiteral("验证失败"),
                QStringLiteral("向 NASA 验证时发生网络错误: %1。\n是否仍然强制保存您的登录信息？").arg(errorMsg),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (r == QMessageBox::Yes)
            {
                saveCredentials(username, password);
                accept();
            }
            else
            {
                m_statusLabel->setText(QStringLiteral("连接错误，未保存信息。"));
                m_statusLabel->setStyleSheet("color: red;");
            }
        }
    });

    timeoutTimer->start(10000); // 10秒超时
}

void EarthdataLoginDialog::onCancelPressed()
{
    reject();
}

void EarthdataLoginDialog::saveCredentials(const QString& username, const QString& password)
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString encryptedUser = QString::fromUtf8(username.toUtf8().toBase64());
    QString encryptedPass = QString::fromUtf8(password.toUtf8().toBase64());
    settings.setValue("DEM/EarthdataUser", encryptedUser);
    settings.setValue("DEM/EarthdataPassword", encryptedPass);
}
