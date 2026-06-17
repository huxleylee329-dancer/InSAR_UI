#pragma once
#include "BaseImportWorker.h"

class HTHTImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit HTHTImportWorker(QObject* parent = nullptr);
    virtual ~HTHTImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax) override;
    QString satelliteFormatTag() const override { return "Hongtu-1"; }
};
