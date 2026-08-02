#pragma once
#ifndef DEM_SOURCE_DIALOG_H
#define DEM_SOURCE_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QStandardItemModel>
#include <QThread>
#include "DEMSourceWorker.h"
#include "NodeUtils.h"

class DEMSourceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DEMSourceDialog(QWidget* parent = nullptr);
    ~DEMSourceDialog();

public slots:
    void ShowProjectList(QStandardItemModel* model);

signals:
    void sendCopy(QStandardItemModel* model);

private slots:
    void onProjectChanged(int index);
    void onStartPressed();
    void onProgressUpdate(int progress, const QString& message);
    void onError(const QString& error);
    void onCancelled();
    void onFinished();
    void onDemFetchFinished(
        const QString& outputH5Path,
        const QString& stagingNode,
        const QString& projectName,
        int demSource,
        double targetResolution
    );
    void onResolutionModeChanged(int index);
    void onBrowseCachePressed();
    void onClearCachePressed();
    void updateCacheSizeLabel();
    void updateLoginStatus();

private:
    QComboBox* m_projectCombo;
    QComboBox* m_slcCombo;
    QComboBox* m_demSourceCombo;
    QLineEdit* m_dstNodeEdit;
    QComboBox* m_resolutionCombo;
    QLineEdit* m_customResEdit;
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
    DEMSourceWorker* m_worker;
    QThread* m_thread;
    QString m_preparedProjectRoot;
    QString m_preparedProjectName;
    QString m_preparedDstNode;
    QStringList m_preparedInputPaths;
    QStringList m_preparedOutputPaths;
    QString m_preparedXmlPath;
    int m_preparedDemSource = 0;
    double m_preparedResolution = 0.0;
    bool m_cancelRequested = false;
    NodeUtils::OutputTransaction m_outputTransaction;

    void updateSlcCombo();
    bool commitOutputTransaction(const QString& stagedH5Path, const QString& stagingNode,
                                 const QString& projectName, int demSource,
                                 double targetResolution, QString* errorMessage);
    void restoreUiAfterFailure();
};

#endif // DEM_SOURCE_DIALOG_H
