#pragma once
#include "BaseImportWorker.h"

class SpacetyImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit SpacetyImportWorker(QObject* parent = nullptr);
    virtual ~SpacetyImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax) override;
    QString satelliteFormatTag() const override { return "Spacety"; }
};
