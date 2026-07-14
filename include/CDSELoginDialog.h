#pragma once
#ifndef CDSE_LOGIN_DIALOG_H
#define CDSE_LOGIN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QDateTime>

class QNetworkReply;

class CDSELoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CDSELoginDialog(QWidget* parent = nullptr);
    ~CDSELoginDialog();
    static QString sessionAccessToken();
    static QDateTime sessionTokenExpiry();

    void reject() override;

private slots:
    void onLoginPressed();
    void onCancelPressed();

private:
    QLineEdit* m_userEdit;
    QLineEdit* m_passEdit;
    QLineEdit* m_totpEdit;
    QPushButton* m_loginBtn;
    QPushButton* m_cancelBtn;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QNetworkReply* m_activeReply = nullptr;

    void setInputEnabled(bool enabled);
    void saveCredentials(const QString& username, const QString& password);
};

#endif // CDSE_LOGIN_DIALOG_H
