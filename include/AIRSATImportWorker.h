#pragma once
#include "BaseImportWorker.h"

class AIRSATImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit AIRSATImportWorker(QObject* parent = nullptr);
    virtual ~AIRSATImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax) override;
    QString satelliteFormatTag() const override { return "AIRSAT"; }
};
