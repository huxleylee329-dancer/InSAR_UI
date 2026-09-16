#pragma once

#include "BaseWorker.h"

class ExportKMLWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit ExportKMLWorker(QObject* parent = nullptr);
    ~ExportKMLWorker();

public slots:
    void exportKML(QString h5Path, QString outFolder, QString fileName);

signals:
    void cancelled();

private:
    bool paintColorbar(double mMin, double mMax, QString save_path);
};
