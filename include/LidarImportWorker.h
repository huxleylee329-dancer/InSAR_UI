#pragma once
#include "BaseImportWorker.h"

class LidarImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit LidarImportWorker(QObject* parent = nullptr);
    virtual ~LidarImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax) override;
    QString satelliteFormatTag() const override { return "LiDAR"; }
};
