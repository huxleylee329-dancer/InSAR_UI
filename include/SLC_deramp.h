#pragma once
#include <QtWidgets/QMainWindow>
#include <qstandarditemmodel.h>
#include <QStringList>
#include <QPointer>
#include "ui_SlcDeramp.h"

class SLCDerampWorker;
class QThread;

class SLC_deramp : public QWidget
{
    Q_OBJECT
public:
    explicit SLC_deramp(QWidget* parent = Q_NULLPTR);
    ~SLC_deramp();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
signals:
    void operate(int masterIndex, QString projectName, QString savePath,
                 QString dstNode, QStringList inputPaths, QString demPath);
    void sendCopy(QStandardItemModel*);

private:
    Ui::SlcDeramp* ui;
    QStandardItemModel* copy;
    QPointer<QThread> m_thread;
    QPointer<SLCDerampWorker> m_worker;

    QString save_path;
    int m_masterIndex = 1;
    int method;
    int image_number;
    void ChangeVision(bool Editable);

    QLabel* m_demPathLabel = nullptr;
    QLineEdit* m_demPathEdit = nullptr;
    QPushButton* m_demBrowseBtn = nullptr;

private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_dst_node_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void handleResults(const QString& dstNode, const QStringList& h5Paths, const QStringList& originNames,
                       const QString& savePath, const QString& projectName);
};
