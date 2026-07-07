#pragma once
#include "BaseWorker.h"

class GeocodingWorker : public BaseWorker
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

    void GeocodingWithDem(
        int type,
        int multi_rg,
        int multi_az,
        QString project_name,
        QString srcNode,
        QString dstNode,
        QStandardItemModel* model,
        QString dem_path
    );
};
