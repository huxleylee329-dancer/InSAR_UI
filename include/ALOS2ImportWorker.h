#pragma once
#include "BaseImportWorker.h"

class ALOS2ImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit ALOS2ImportWorker(QObject* parent = nullptr);
    virtual ~ALOS2ImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax, QString& outErrorMsg) override;
    QString satelliteFormatTag() const override { return "ALOS2"; }
};
