#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>
#include <QMutex>
#include <vector>

class BiomassImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit BiomassImportWorker(QObject* parent = nullptr);
    ~BiomassImportWorker();

public slots:
    void import_Biomass_patch(
        QString savepath,
        std::vector<QString> amp_files,
        std::vector<QString> phase_files,
        std::vector<QString> xml_files,
        std::vector<QString> orbit_files,
        std::vector<QString> polarizations,
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
