#pragma once

#include "BaseWorker.h"
#include <QStringList>

class SLCDerampWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit SLCDerampWorker(QObject* parent = nullptr);
    ~SLCDerampWorker();

public slots:
    void SLC_deramp(
        int masterIndex,
        QString projectName,
        QString savePath,
        QString dstNode,
        QStringList inputPaths
    );

    void SLC_deramp_with_dem(
        int masterIndex,
        QString projectName,
        QString savePath,
        QString dstNode,
        QStringList inputPaths,
        QString demPath
    );

signals:
    void cancelled();
    // 特有信号：回传 SLC 去斜坡结果
    void sendResults(const QString& dstNode, const QStringList& h5Paths, const QStringList& originNames,
                     const QString& savePath, const QString& projectName);
};
