#pragma once
#include "BaseWorker.h"

class S1TopsBackGeocodingWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit S1TopsBackGeocodingWorker(QObject* parent = nullptr);
    ~S1TopsBackGeocodingWorker();

    void setDemPath(const QString& path) { m_demPath = path; }

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

private:
    QString m_demPath;
};
