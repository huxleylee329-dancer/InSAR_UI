#pragma once

#include "BaseWorker.h"
#include <QList>
#include <QPoint>
#include <atomic>

class SBASReferenceReselectionWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit SBASReferenceReselectionWorker(QObject* parent = nullptr);
    ~SBASReferenceReselectionWorker();
    void StopProcess() override;
    bool cancellationRequested() const noexcept;

public slots:
    void SBAS_reference_reselection(QString save_path, QString srcNode, QString times_series_h5,
                                    int ref_row, int ref_col, QList<QPoint> GCPs);

signals:
    void cancelled();

private:
    std::atomic_bool m_cancelRequested{false};
};
