#pragma once

#include "BaseWorker.h"
#include <QMetaType>
#include <QStringList>

struct DemFileResult {
    QString demName;
    QString relativeDemPath;
    QString absoluteDemPath;
    int offsetRow = 0;
    int offsetCol = 0;
};
Q_DECLARE_METATYPE(DemFileResult)

class DemWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit DemWorker(QObject* parent = nullptr);
    ~DemWorker();

public slots:
    void Dem(int method, int times, QString savePath, QString outputNode,
             QStringList phaseNames, QStringList phasePaths);

signals:
    void cancelled();
    void demFileGenerated(const DemFileResult& result);
};
