#pragma once

#include "BaseWorker.h"
#include <QList>
#include <QStringList>

class BaselineWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit BaselineWorker(QObject* parent = nullptr);
    ~BaselineWorker();

public slots:
    void Baseline_Estimate(int index, const QStringList& filePaths);

signals:
    // 特有信号：回传基线数据
    void sendBL(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
    void cancelled();
};
