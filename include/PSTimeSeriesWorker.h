#pragma once
#include "BaseWorker.h"
#include <QStringList>
#include <atomic>

class PSTimeSeriesWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit PSTimeSeriesWorker(QObject* parent = nullptr);
    ~PSTimeSeriesWorker();
    void StopProcess() override;
    bool cancellationRequested() const noexcept;

public slots:
    void ps_time_series(
        double coherence_threshold,
        double max_deformation_rate,
        int atmospheric_window,
        QString projectPath,
        QString projectName,
        QString dstNode,
        QStringList filePaths
    );

signals:
    void cancelled();

private:
    std::atomic_bool m_cancelRequested{false};
};
