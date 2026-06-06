#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>
#include <QMutex>

class GeocodingWorker : public QObject
{
    Q_OBJECT
public:
    explicit GeocodingWorker(QObject* parent = nullptr);
    ~GeocodingWorker();

public slots:
    void Geocoding(
        int type,
        int multi_rg,
        int multi_az,
        QString project_name,
        QString srcNode,
        QString dstNode,
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
