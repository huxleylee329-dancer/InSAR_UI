#include "CDSELoginDialog.h"
#include "NodeUtils.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrlQuery>

namespace {
const char* kCdseTokenUrl = "https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/openid-connect/token";
QString s_sessionAccessToken;
QDateTime s_sessionTokenExpiry;
}

CDSELoginDialog::CDSELoginDialog(QWidget* parent)
    : QDialog(parent)
    , m_userEdit(new QLineEdit(this))
    , m_passEdit(new QLineEdit(this))
    , m_totpEdit(new QLineEdit(this))
    , m_loginBtn(new QPushButton(QStringLiteral("登录"), this))
    , m_cancelBtn(new QPushButton(QStringLiteral("取消"), this))
    , m_statusLabel(new QLabel(this))
    , m_progressBar(new QProgressBar(this))
{
    setWindowTitle(QStringLiteral("Copernicus Data Space 登录"));
    setMinimumWidth(400);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    auto* helpLabel = new QLabel(this);
    helpLabel->setText(QStringLiteral(
        "下载 ESA Sentinel-1 辅助轨道需要 Copernicus Data Space 账户。"
        "如果尚未注册，请访问 <a href=\"https://dataspace.copernicus.eu/\">Copernicus Data Space</a>。"
        "<br>登录信息按现有项目约定保存在本机 Config.ini 中，请勿在共享计算机上保存个人账户。"));
    helpLabel->setOpenExternalLinks(true);
    helpLabel->setWordWrap(true);
    helpLabel->setTextFormat(Qt::RichText);
    mainLayout->addWidget(helpLabel);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(8);

    m_userEdit->setPlaceholderText(QStringLiteral("请输入 CDSE 用户名"));
    formLayout->addRow(QStringLiteral("用户名:"), m_userEdit);

    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    formLayout->addRow(QStringLiteral("密  码:"), m_passEdit);

    m_totpEdit->setPlaceholderText(QStringLiteral("启用双重验证时填写，不保存"));
    m_totpEdit->setMaxLength(12);
    formLayout->addRow(QStringLiteral("验证码:"), m_totpEdit);
    mainLayout->addLayout(formLayout);

    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: gray;");
    mainLayout->addWidget(m_statusLabel);

    m_progressBar->setRange(0, 0);
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_loginBtn->setDefault(true);
    buttonLayout->addWidget(m_loginBtn);
    buttonLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(buttonLayout);

    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString savedUser = settings.value("Orbit/CDSEUser", "").toString();
    QString savedPass = settings.value("Orbit/CDSEPassword", "").toString();
    if (!savedUser.isEmpty()) {
        m_userEdit->setText(QString::fromUtf8(QByteArray::fromBase64(savedUser.toUtf8())));
    }
    if (!savedPass.isEmpty()) {
        m_passEdit->setText(QString::fromUtf8(QByteArray::fromBase64(savedPass.toUtf8())));
    }
    (m_userEdit->text().isEmpty() ? m_userEdit : m_totpEdit)->setFocus();

    connect(m_loginBtn, &QPushButton::clicked, this, &CDSELoginDialog::onLoginPressed);
    connect(m_cancelBtn, &QPushButton::clicked, this, &CDSELoginDialog::onCancelPressed);
}

CDSELoginDialog::~CDSELoginDialog()
{
    if (m_activeReply) {
        m_activeReply->abort();
    }
}

QString CDSELoginDialog::sessionAccessToken()
{
    return s_sessionAccessToken;
}

QDateTime CDSELoginDialog::sessionTokenExpiry()
{
    return s_sessionTokenExpiry;
}

void CDSELoginDialog::reject()
{
    if (m_activeReply) {
        m_activeReply->abort();
    }
    QDialog::reject();
}

void CDSELoginDialog::setInputEnabled(bool enabled)
{
    m_userEdit->setEnabled(enabled);
    m_passEdit->setEnabled(enabled);
    m_totpEdit->setEnabled(enabled);
    m_loginBtn->setEnabled(enabled);
    m_cancelBtn->setEnabled(enabled);
    m_progressBar->setVisible(!enabled);
}

void CDSELoginDialog::onLoginPressed()
{
    const QString username = m_userEdit->text().trimmed();
    const QString password = m_passEdit->text();
    const QString totp = m_totpEdit->text().trimmed();
    if (username.isEmpty() || password.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("用户名和密码不能为空。"));
        m_statusLabel->setStyleSheet("color: red;");
        return;
    }

    setInputEnabled(false);
    m_statusLabel->setText(QStringLiteral("正在向 Copernicus Data Space 验证账户……"));
    m_statusLabel->setStyleSheet("color: blue;");

    auto* manager = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(kCdseTokenUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Accept", "application/json");

    QUrlQuery form;
    form.addQueryItem("client_id", "cdse-public");
    form.addQueryItem("username", username);
    form.addQueryItem("password", password);
    form.addQueryItem("grant_type", "password");
    if (!totp.isEmpty()) {
        form.addQueryItem("totp", totp);
    }

    auto* timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    m_activeReply = manager->post(request, form.query(QUrl::FullyEncoded).toUtf8());

    connect(timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_activeReply) {
            m_activeReply->abort();
        }
    });

    connect(m_activeReply, &QNetworkReply::finished, this,
        [this, manager, timeoutTimer, username, password]() {
            if (!m_activeReply) {
                return;
            }

            QNetworkReply* reply = m_activeReply;
            m_activeReply = nullptr;
            timeoutTimer->stop();
            timeoutTimer->deleteLater();

            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QNetworkReply::NetworkError networkError = reply->error();
            const QByteArray responseData = reply->readAll();
            reply->deleteLater();
            manager->deleteLater();
            setInputEnabled(true);

            if (networkError == QNetworkReply::OperationCanceledError) {
                m_statusLabel->setText(QStringLiteral("CDSE 登录验证已取消或超时。"));
                m_statusLabel->setStyleSheet("color: red;");
                return;
            }

            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(responseData, &parseError);
            const QJsonObject object = document.object();
            const QString accessToken = object.value("access_token").toString();
            if (networkError == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300 && !accessToken.isEmpty()) {
                s_sessionAccessToken = accessToken;
                const int expiresIn = object.value("expires_in").toInt(600);
                s_sessionTokenExpiry = QDateTime::currentDateTimeUtc().addSecs(qMax(60, expiresIn));
                saveCredentials(username, password);
                accept();
                return;
            }

            QString detail = object.value("error_description").toString();
            if (detail.isEmpty()) {
                detail = object.value("error").toString();
            }
            if (statusCode == 400 || statusCode == 401) {
                m_passEdit->clear();
                m_totpEdit->clear();
                m_statusLabel->setText(detail.isEmpty()
                    ? QStringLiteral("用户名、密码或双重验证码无效。")
                    : QStringLiteral("认证失败：%1").arg(detail));
            } else if (statusCode == 429) {
                m_statusLabel->setText(QStringLiteral("CDSE 请求过于频繁，请稍后重试。"));
            } else if (parseError.error != QJsonParseError::NoError) {
                m_statusLabel->setText(QStringLiteral("CDSE 返回了无法解析的认证响应。"));
            } else {
                m_statusLabel->setText(QStringLiteral("CDSE 登录验证失败：HTTP %1，%2")
                    .arg(statusCode).arg(detail.isEmpty() ? QStringLiteral("网络或服务异常") : detail));
            }
            m_statusLabel->setStyleSheet("color: red;");
        });

    timeoutTimer->start(20000);
}

void CDSELoginDialog::onCancelPressed()
{
    reject();
}

void CDSELoginDialog::saveCredentials(const QString& username, const QString& password)
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    settings.setValue("Orbit/CDSEUser", QString::fromUtf8(username.toUtf8().toBase64()));
    settings.setValue("Orbit/CDSEPassword", QString::fromUtf8(password.toUtf8().toBase64()));
}
