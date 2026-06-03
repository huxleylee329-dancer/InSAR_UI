#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

class BaselineWorker : public QObject
{
    Q_OBJECT
public:
    explicit BaselineWorker(QObject* parent = nullptr);
    ~BaselineWorker();

public slots:
    void Baseline_Estimate(int index, const QStringList& filePaths);

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error);
    void sendBL(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
};
