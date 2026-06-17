#pragma once
#include "BaseImportWorker.h"

class Sentinel1ImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit Sentinel1ImportWorker(QObject* parent = nullptr);
    virtual ~Sentinel1ImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax) override;
    QString satelliteFormatTag() const override { return "sentinel"; }
};
