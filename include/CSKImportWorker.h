#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>
#include <QMutex>
#include <vector>

class CSKImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit CSKImportWorker(QObject* parent = nullptr);
    ~CSKImportWorker();

public slots:
    void import_CSK_patch(
        QString savepath,
        std::vector<QString> original_file_list,
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
