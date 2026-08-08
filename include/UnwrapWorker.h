#pragma once

#include "BaseWorker.h"

struct UnwrapFileResult
{
    QString unwrapName;
    QString absolutePath;
    QString relativePath;
    int offsetRow = 0;
    int offsetCol = 0;
    QString method;
    QString amplitudeStatus;
    QString amplitudeReason;
    int amplitudeMasterRows = 0;
    int amplitudeMasterCols = 0;
    int amplitudeSlaveRows = 0;
    int amplitudeSlaveCols = 0;
    int amplitudeExpectedRows = 0;
    int amplitudeExpectedCols = 0;
    bool amplitudeDegraded = false;
};
Q_DECLARE_METATYPE(UnwrapFileResult)

class UnwrapWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit UnwrapWorker(QObject* parent = nullptr);
    ~UnwrapWorker();

public slots:
    void Unwrap(int method, double coherenceThreshold, QString savePath, QString fileName, QStringList phasePaths);

signals:
    void cancelled();
    void unwrapFileGenerated(const UnwrapFileResult& result);
};
