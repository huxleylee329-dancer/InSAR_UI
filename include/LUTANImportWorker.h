#pragma once
#include "BaseImportWorker.h"

class LUTANImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit LUTANImportWorker(QObject* parent = nullptr);
    virtual ~LUTANImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax) override;
    QString satelliteFormatTag() const override { return "LUTAN"; }
};
