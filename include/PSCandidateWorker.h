#pragma once
#include "BaseWorker.h"
#include <QStringList>
#include <atomic>

class PSCandidateWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit PSCandidateWorker(QObject* parent = nullptr);
    ~PSCandidateWorker();
    void StopProcess() override;
    bool cancellationRequested() const noexcept;

public slots:
    void select_candidates(
        double da_threshold,
        int min_ps_count,
        int multilook_rg,
        int multilook_az,
        QString projectPath,
        QString projectName,
        QString dstNode,
        QStringList filePaths,
        bool outputDirectoryIsStaging = false
    );

signals:
    void cancelled();
    void outputsGenerated(const QStringList& outputPaths);

private:
    std::atomic_bool m_cancelRequested{false};
};
