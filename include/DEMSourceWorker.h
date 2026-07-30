#pragma once
#ifndef DEM_SOURCE_WORKER_H
#define DEM_SOURCE_WORKER_H

#include "BaseWorker.h"
#include <QStringList>

class DEMSourceWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit DEMSourceWorker(QObject* parent = nullptr);
    ~DEMSourceWorker();

public slots:
    void fetch_dem(
        QString projectPath,
        QString projectName,
        QString dstNode,
        QStringList filePaths,
        int demSource,
        double targetResolution,
        QString cacheDir
    );

signals:
    void cancelled();
    void demFetchFinished(
        const QString& outputH5Path,
        const QString& dstNode,
        const QString& projectName,
        int demSource,
        double targetResolution,
        const QStringList& availableTiles,
        const QStringList& serverNotFoundTiles,
        int requestedTileCount,
        bool outputValidated
    );

private:
    int downloadTile(const QString& url, const QString& savePath, bool requiresEarthdataAuth,
                     QString* failureDetail = nullptr, bool allowRetry = true);
};

#endif // DEM_SOURCE_WORKER_H
