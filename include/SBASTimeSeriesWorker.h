#pragma once

#include "BaseWorker.h"
#include <QStringList>
#include <atomic>

struct SBASTimeSeriesResult {
    QString dstNode;
    QString timesSeriesH5Path;
    QString relativePath;
};
Q_DECLARE_METATYPE(SBASTimeSeriesResult)

class SBASTimeSeriesWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit SBASTimeSeriesWorker(QObject* parent = nullptr);
    ~SBASTimeSeriesWorker();
    void StopProcess() override;
    bool cancellationRequested() const noexcept;

public slots:
    void SBAS_time_series(double temporal_thresh_low, double temporal_thresh, double spatial_thresh,
                          int multilook_rg, int multilook_az, int unwrap_method, double alpha,
                          double coherence_thresh, double temporal_coherence_thresh,
                          double refinement_coh_thresh, double refinemen_def_thresh,
                          QString projectPath, QString projectName, QString dstNode, QString csvPath,
                          QStringList filePaths, bool outputDirectoryIsStaging,
                          QString interferogramDirectory = QString(),
                          std::atomic_bool* externalCancellationFlag = nullptr);

signals:
    void cancelled();
    void sbasGenerated(const SBASTimeSeriesResult& result);

private:
    std::atomic_bool m_cancelRequested{false};
};
