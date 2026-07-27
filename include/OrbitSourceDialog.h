#pragma once
#ifndef ORBIT_SOURCE_DIALOG_H
#define ORBIT_SOURCE_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QStandardItemModel>
#include <QThread>
#include "OrbitSourceWorker.h"

class OrbitSourceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OrbitSourceDialog(QWidget* parent = nullptr);
    ~OrbitSourceDialog();

public slots:
    /**
     * @brief 由主窗口投递工程 TreeModel
     */
    void ShowProjectList(QStandardItemModel* model);

signals:
    /**
     * @brief 触发 Worker 的下载槽函数
     */
    void startOrbitFetch(
        QString projectPath,
        QStringList filePaths,
        int orbitSource,
        QString cacheDir
    );

private slots:
    void onProjectChanged(int index);
    void onStartPressed();
    void onProgressUpdate(int progress, const QString& message);
    void onError(const QString& error);
    void onFinished();
    void onBrowseCachePressed();
    void onClearCachePressed();
    void updateCacheSizeLabel();
    void updateLoginStatus();

private:
    QComboBox* m_projectCombo;
    QComboBox* m_slcCombo;
    QComboBox* m_orbitSourceCombo;
    QLineEdit* m_cacheDirEdit;
    QPushButton* m_browseCacheBtn;
    QPushButton* m_clearCacheBtn;
    QLabel* m_cacheSizeLabel;

    // 登录相关控件
    QLabel* m_loginStatusLabel;
    QPushButton* m_loginBtn;
    QPushButton* m_logoutBtn;

    QPushButton* m_startBtn;
    QPushButton* m_cancelBtn;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    QStandardItemModel* m_model;
    OrbitSourceWorker* m_worker;
    QThread* m_thread;

    void updateSlcCombo();
};

#endif // ORBIT_SOURCE_DIALOG_H
