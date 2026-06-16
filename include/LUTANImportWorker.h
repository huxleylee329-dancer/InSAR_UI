#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>
#include <QMutex>
#include <vector>

class LUTANImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit LUTANImportWorker(QObject* parent = nullptr);
    ~LUTANImportWorker();

public slots:
    void import_LUTAN_patch(
        QString savepath,
        std::vector<QString> data_files,
        std::vector<QString> xml_files,
        std::vector<int> modes,
        std::vector<QString> import_names,
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
