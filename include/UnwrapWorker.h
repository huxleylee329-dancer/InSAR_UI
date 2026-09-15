#pragma once

#include "BaseWorker.h"

struct SnaphuUiOptions
{
    quint32 tileRows = 1;
    quint32 tileCols = 1;
    quint32 rowOverlap = 0;
    quint32 colOverlap = 0;
    quint64 wallTimeoutMilliseconds = 0;
    bool keepArtifactsOnSuccess = false;
    // SNAPHU 统计代价模式，取值与 Unwrap.h 的 SnaphuStatisticalCostMode 对应：
    // 0=TOPO（SNAPHU 默认，本工程既有行为），1=DEFO，2=SMOOTH
    quint32 statisticalCostMode = 0;
};
Q_DECLARE_METATYPE(SnaphuUiOptions)

struct SnaphuRunEventInfo
{
    quint32 type = 0;
    quint32 effectiveProcessCount = 1;
    quint32 metricAvailability = 0;
    quint64 elapsedMilliseconds = 0;
    quint64 totalCpuMilliseconds = 0;
    quint64 peakJobMemoryBytes = 0;
    quint64 readBytes = 0;
    quint64 writeBytes = 0;
    QString taskDirectory;
    QString configPath;
    QString message;
};
Q_DECLARE_METATYPE(SnaphuRunEventInfo)

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
    void Unwrap(int method, double coherenceThreshold, QString savePath, QString fileName,
                QStringList phasePaths, SnaphuUiOptions snaphuOptions);

signals:
    void cancelled();
    void unwrapFileGenerated(const UnwrapFileResult& result);
    void snaphuRunEvent(const SnaphuRunEventInfo& event);
};
