#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>

class S1TopsBackGeocodingWorker : public QObject
{
    Q_OBJECT
public:
    explicit S1TopsBackGeocodingWorker(QObject* parent = nullptr);
    ~S1TopsBackGeocodingWorker();

public slots:
    void S1_TOPS_BackGeocoding(
        int images_number,
        int masterIndex,
        QString savePath,
        QString dstProject,
        QString srcNode,
        QString dstNode,
        QStandardItemModel* model,
        bool b_ESD = true
    );

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);
};
