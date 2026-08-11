#pragma once
#ifndef DEM_SOURCE_WORKER_H
#define DEM_SOURCE_WORKER_H

#include "BaseWorker.h"
#include <QStringList>

// Kept deliberately small and value-only because it crosses the worker-thread
// boundary.  The node persists the same facts into the managed DEM audit.
struct DemCoverageAudit
{
    QString status;
    QString evidence;
    QStringList serverNotFoundTiles;
    QStringList intersectingServerNotFoundTiles;
    qint64 validPixelCount = 0;
    qint64 invalidPixelCount = 0;
    bool hasUnverifiedCoverage = false;
};

Q_DECLARE_METATYPE(DemCoverageAudit)

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
        QString stagingNode,
        QString outputNodeName,
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
        bool outputValidated,
        const DemCoverageAudit& coverageAudit
    );

private:
    int downloadTile(const QString& url, const QString& savePath, bool requiresEarthdataAuth,
                     QString* failureDetail = nullptr);
};

#endif // DEM_SOURCE_WORKER_H
