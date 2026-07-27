#pragma once

#include "BaseWorker.h"

struct UnwrapFileResult
{
    QString unwrapName;
    QString absolutePath;
    QString relativePath;
    int offsetRow = 0;
    int offsetCol = 0;
    QString method;
};
Q_DECLARE_METATYPE(UnwrapFileResult)

class UnwrapWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit UnwrapWorker(QObject* parent = nullptr);
    ~UnwrapWorker();

public slots:
    void Unwrap(int method, double coherenceThreshold, QString savePath, QString fileName, QStringList phasePaths);

signals:
    void cancelled();
    void unwrapFileGenerated(const UnwrapFileResult& result);
};
