#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>
#include <QMutex>
#include <vector>

class ALOS2ImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit ALOS2ImportWorker(QObject* parent = nullptr);
    ~ALOS2ImportWorker();

public slots:
    void import_ALOS2_patch(
        QString savepath,
        std::vector<QString> IMG_file_list,
        std::vector<QString> LED_file_list,
        std::vector<QString> import_namelist,
        QString dst_node,
        QString dst_project,
        QStandardItemModel* model
    );

    void StopProcess();
    bool isStopRequested();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);

private:
    QMutex lock;
    bool stop_flag;
};
