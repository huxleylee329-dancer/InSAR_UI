#pragma once
#include <QtWidgets/QMainWindow>
#include <qstandarditemmodel.h>
#include "ui_Unwrap.h"
#include "NodeUtils.h"
#include <QThread>
#include <QStringList>
#include <QList>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>

#include "UnwrapWorker.h"

class Unwrap_ui : public QWidget
{
    Q_OBJECT
public:
    explicit Unwrap_ui(QWidget* parent = Q_NULLPTR);
    ~Unwrap_ui();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
    void onWorkerError(const QString& error);
    void onWorkerCancelled();
    void onUnwrapFileGenerated(const UnwrapFileResult& result);
    void onSnaphuRunEvent(const SnaphuRunEventInfo& event);
private:
    Ui::Unwrap* ui;
    QStandardItemModel* copy;
    UnwrapWorker* Unwrap_worker;
    QThread* m_thread;
    QString save_path;
    int method;
    int image_number;
    NodeUtils::OutputTransaction m_outputTransaction;
    QStringList m_preparedOutputPaths;
    QList<UnwrapFileResult> m_pendingUnwrapResults;
    QGroupBox* m_snaphuOptionsGroup = nullptr;
    QLabel* m_snaphuTaskLabel = nullptr;
    QLabel* m_snaphuStatusLabel = nullptr;
    QLabel* m_snaphuProcessLabel = nullptr;
    QSpinBox* m_snaphuTileRowsSpin = nullptr;
    QSpinBox* m_snaphuTileColsSpin = nullptr;
    QSpinBox* m_snaphuRowOverlapSpin = nullptr;
    QSpinBox* m_snaphuColOverlapSpin = nullptr;
    QSpinBox* m_snaphuTimeoutSpin = nullptr;
    QCheckBox* m_snaphuKeepArtifactsCheck = nullptr;
    void ChangeVision(bool Editable);
    SnaphuUiOptions snaphuOptions() const;
    void updateSnaphuOptionWidgets();
    void abandonOutputTransaction(const QString& reason);
    void releaseStoppedThread();
signals:
    void operate(int, double, QString, QString, QStringList, SnaphuUiOptions);
    void sendCopy(QStandardItemModel*);
private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_2_currentIndexChanged();
    // void on_masterpushButton_pressed();
     //void on_slavepushButton_pressed()
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void Change_Setting();
};
