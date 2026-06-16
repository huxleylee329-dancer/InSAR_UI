#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>
#include <QMutex>
#include <vector>

class AIRSATImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit AIRSATImportWorker(QObject* parent = nullptr);
    ~AIRSATImportWorker();

public slots:
    void import_AIRSAT_patch(
        QString savepath,
        std::vector<QString> data_file_list,
        std::vector<QString> xml_file_list,
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
