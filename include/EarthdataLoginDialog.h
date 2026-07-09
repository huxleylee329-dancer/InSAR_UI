#pragma once
#ifndef EARTHDATA_LOGIN_DIALOG_H
#define EARTHDATA_LOGIN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>

class QNetworkReply;

class EarthdataLoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EarthdataLoginDialog(QWidget* parent = nullptr);
    ~EarthdataLoginDialog();
    void reject() override;

private slots:
    void onLoginPressed();
    void onCancelPressed();

private:
    QLineEdit* m_userEdit;
    QLineEdit* m_passEdit;
    QPushButton* m_loginBtn;
    QPushButton* m_cancelBtn;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QNetworkReply* m_activeReply = nullptr;

    void saveCredentials(const QString& username, const QString& password);
};

#endif // EARTHDATA_LOGIN_DIALOG_H
