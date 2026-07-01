#pragma once
#include "BaseWorker.h"
#include <QStringList>

class PSTimeSeriesWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit PSTimeSeriesWorker(QObject* parent = nullptr);
    ~PSTimeSeriesWorker();

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
};
