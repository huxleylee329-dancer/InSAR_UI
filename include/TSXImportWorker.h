#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>
#include <QMutex>
#include <vector>

class TSXImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit TSXImportWorker(QObject* parent = nullptr);
    ~TSXImportWorker();

public slots:
    void import_TSX(
        QString polarization,
        QString xml_filename,
        QString project_path,
        QString folder,
        QString filename,
        QString project_name,
        QStandardItemModel* model
    );

    void import_TSX_patch(
        QString polarization,
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
