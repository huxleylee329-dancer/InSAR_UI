#pragma once

#include "BaseWorker.h"
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <atomic>

struct SBASRebuildParameters {
    double temporalThreshLow = 0.0;
    double temporalThresh = 200.0;
    double spatialThresh = 500.0;
    int multilookRg = 4;
    int multilookAz = 4;
    int unwrapMethod = 1;
    double alpha = 0.8;
    double coherenceThresh = 0.5;
    double temporalCoherenceThresh = 0.6;
    double refinementCohThresh = 0.8;
    double refinementDefThresh = 0.01;
};

struct SBASReferenceReselectionResult {
    QString outputH5Path;
};
Q_DECLARE_METATYPE(SBASReferenceReselectionResult)

class SBASReferenceReselectionWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit SBASReferenceReselectionWorker(QObject* parent = nullptr);
    ~SBASReferenceReselectionWorker();
    void StopProcess() override;
    bool cancellationRequested() const noexcept;

public slots:
    void SBAS_reference_reselection(QString projectRoot, QString stagingNode,
                                    QStringList sourceInputs, SBASRebuildParameters parameters,
                                    int ref_row, int ref_col, QList<QPoint> GCPs);

signals:
    void cancelled();
    void reselectionGenerated(const SBASReferenceReselectionResult& result);

private:
    std::atomic_bool m_cancelRequested{false};
};
