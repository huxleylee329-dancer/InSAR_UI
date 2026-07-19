#pragma once

#include "BaseWorker.h"
#include <QList>
#include <QPoint>

class SBASReferenceReselectionWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit SBASReferenceReselectionWorker(QObject* parent = nullptr);
    ~SBASReferenceReselectionWorker();

public slots:
    void SBAS_reference_reselection(QString save_path, QString srcNode, QString times_series_h5,
                                    int ref_row, int ref_col, QList<QPoint> GCPs);

signals:
    void cancelled();
};
