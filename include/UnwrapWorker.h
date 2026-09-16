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
    // 分片驱动：>1 时用 DOTILEMASK 起多个 snaphu 进程并行解缠互不相交的分块子集（每个
    // 进程 NOASSEMBLE），全部结束后再统一 --assemble 一次。上限受分块数与本层 32 约束。
    quint32 tileWorkerCount = 1;
    // 装配重放（恢复）：跳过解缠，只对已保留的 tile 现场跑一次装配。对应 DLL 的
    // SNAPHU_RUN_OPTION_ASSEMBLE_ONLY。需同时勾选"成功后保留现场"才有 tile 可复用。
    bool assembleOnly = false;
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
