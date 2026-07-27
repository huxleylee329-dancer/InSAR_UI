#pragma once

#include "BaseWorker.h"

struct DenoiseFileResult {
    QString fileName;
    QString filterName;
    QString filterPath;
    QString relativePath;
    int offsetRow = 0;
    int offsetCol = 0;
};
Q_DECLARE_METATYPE(DenoiseFileResult)

class DenoiseWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit DenoiseWorker(QObject* parent = nullptr);
    ~DenoiseWorker();

public slots:
    void Denoise(QList<int> para, double alpha, QString savePath, QString outputNode,
                 QStringList phaseNames, QStringList phasePaths);

signals:
    void cancelled();
    void denoiseGenerated(const DenoiseFileResult& result);
};
