#pragma once
#include "BaseImportWorker.h"

class CSKImportWorker : public BaseImportWorker
{
    Q_OBJECT
public:
    explicit CSKImportWorker(QObject* parent = nullptr);
    virtual ~CSKImportWorker();

protected:
    bool convertToH5(const QStringList& arguments, const QString& outputPath,
                     int progressMin, int progressMax, QString& outErrorMsg) override;
    QString satelliteFormatTag() const override { return "CSG-2"; }
};
