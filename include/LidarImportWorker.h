#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>
#include <QMutex>
#include <vector>

class LidarImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit LidarImportWorker(QObject* parent = nullptr);
    ~LidarImportWorker();

public slots:
    void import_Lidar_patch(
        QString savepath,
        std::vector<QString> original_file_list,
        std::vector<QString> import_namelist,
        QString product_type,
        int rh_percentile,
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
