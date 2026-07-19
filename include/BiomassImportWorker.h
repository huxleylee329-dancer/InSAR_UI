#pragma once
#include "BaseImportWorker.h"

class BiomassImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit BiomassImportWorker(QObject* parent = nullptr);
    virtual ~BiomassImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax, QString& outErrorMsg) override;
    QString satelliteFormatTag() const override { return "Biomass"; }
};
