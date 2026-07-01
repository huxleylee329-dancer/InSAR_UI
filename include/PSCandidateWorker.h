#pragma once
#include "BaseWorker.h"
#include <QStringList>

class PSCandidateWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit PSCandidateWorker(QObject* parent = nullptr);
    ~PSCandidateWorker();

public slots:
    void select_candidates(
        double da_threshold,
        int min_ps_count,
        int multilook_rg,
        int multilook_az,
        QString projectPath,
        QString projectName,
        QString dstNode,
        QStringList filePaths
    );
};
