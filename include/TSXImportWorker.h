#pragma once
#include "BaseImportWorker.h"

class TSXImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit TSXImportWorker(QObject* parent = nullptr);
    virtual ~TSXImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax, QString& outErrorMsg) override;
    QString satelliteFormatTag() const override { return "TSX"; }
};
