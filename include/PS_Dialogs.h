#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QProgressBar>
#include <QLabel>
#include <QStandardItemModel>
#include <QThread>
#include "NodeUtils.h"
#include "PSCandidateWorker.h"
#include "PSNetworkWorker.h"
#include "PSTimeSeriesWorker.h"

namespace QtNodes {

// ==================== 1. PS 候选点选择对话框 ====================
class PS_Candidate_Dialog : public QDialog
{
    Q_OBJECT
public:
    explicit PS_Candidate_Dialog(QWidget* parent = nullptr);
    ~PS_Candidate_Dialog();

    void ShowProjectList(QStandardItemModel* model);

signals:
    void sendCopy(QStandardItemModel* model);

private slots:
    void onProjectChanged();
    void onAccept();
    void onReject();
    void onProgressUpdate(int progress, const QString& message);
    void onOutputsGenerated(const QStringList& outputPaths);
    void onProcessingFinished();
    void onError(const QString& error);
    void onCancelled();

private:
    void createUI();
    void stopThread();
    QString projectRoot() const;
    QString projectXmlPath() const;
    void reject() override;

    QComboBox* m_projectCombo;
    QComboBox* m_srcNodeCombo;
    QLineEdit* m_daThresholdEdit;
    QLineEdit* m_minPsCountEdit;
    QLineEdit* m_multilookRgEdit;
    QLineEdit* m_multilookAzEdit;
    QLineEdit* m_outputNodeNameEdit;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    QStandardItemModel* m_projectModel;
    PSCandidateWorker* m_worker;
    QThread* m_thread;
    QString m_projectPath;
    QString m_projectName;
    QString m_preparedOutputNode;
    QStringList m_preparedOutputPaths;
    QStringList m_preparedInputPaths;
    QStringList m_generatedOutputPaths;
    NodeUtils::OutputTransaction m_outputTransaction;
};

// ==================== 2. PS 网络构建对话框 ====================
class PS_Network_Dialog : public QDialog
{
    Q_OBJECT
public:
    explicit PS_Network_Dialog(QWidget* parent = nullptr);
    ~PS_Network_Dialog();

    void ShowProjectList(QStandardItemModel* model);

signals:
    void sendCopy(QStandardItemModel* model);

private slots:
    void onProjectChanged();
    void onAccept();
    void onReject();
    void onProgressUpdate(int progress, const QString& message);
    void onOutputsGenerated(const QStringList& outputPaths);
    void onProcessingFinished();
    void onError(const QString& error);
    void onCancelled();

private:
    void createUI();
    void stopThread();
    QString projectRoot() const;
    QString projectXmlPath() const;
    void reject() override;

    QComboBox* m_projectCombo;
    QComboBox* m_candidatesCombo;
    QComboBox* m_slcCombo;
    QLineEdit* m_maxEdgeLengthEdit;
    QLineEdit* m_refRowEdit;
    QLineEdit* m_refColEdit;
    QLineEdit* m_outputNodeNameEdit;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    QStandardItemModel* m_projectModel;
    PSNetworkWorker* m_worker;
    QThread* m_thread;
    QString m_projectPath;
    QString m_projectName;
    QString m_preparedOutputNode;
    QStringList m_preparedOutputPaths;
    QStringList m_preparedInputPaths;
    QStringList m_generatedOutputPaths;
    NodeUtils::OutputTransaction m_outputTransaction;
};

// ==================== 3. PS 时序分析对话框 ====================
class PS_TimeSeries_Dialog : public QDialog
{
    Q_OBJECT
public:
    explicit PS_TimeSeries_Dialog(QWidget* parent = nullptr);
    ~PS_TimeSeries_Dialog();

    void ShowProjectList(QStandardItemModel* model);

signals:
    void sendCopy(QStandardItemModel* model);

private slots:
    void onProjectChanged();
    void onAccept();
    void onReject();
    void onProgressUpdate(int progress, const QString& message);
    void onOutputsGenerated(const QStringList& outputPaths);
    void onProcessingFinished();
    void onError(const QString& error);
    void onCancelled();

private:
    void createUI();
    void stopThread();
    QString projectRoot() const;
    QString projectXmlPath() const;
    void reject() override;

    QComboBox* m_projectCombo;
    QComboBox* m_networkCombo;
    QLineEdit* m_coherenceThreshEdit;
    QLineEdit* m_maxDeformationRateEdit;
    QLineEdit* m_atmosphericWindowEdit;
    QLineEdit* m_outputNodeNameEdit;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    QStandardItemModel* m_projectModel;
    PSTimeSeriesWorker* m_worker;
    QThread* m_thread;
    QString m_projectPath;
    QString m_projectName;
    QString m_preparedOutputNode;
    QStringList m_preparedOutputPaths;
    QStringList m_preparedInputPaths;
    QStringList m_generatedOutputPaths;
    NodeUtils::OutputTransaction m_outputTransaction;
};

} // namespace QtNodes
