#pragma once
#ifndef DEM_SOURCE_WORKER_H
#define DEM_SOURCE_WORKER_H

#include "BaseWorker.h"
#include <QStringList>
#include <QJsonObject>

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

    // 在 Worker 启动前由节点注入输出事务的 product descriptor JSON，
    // 用于写入阶段在 H5 中写入语义描述并据此预计算 H5 哈希。
    void setProductDescriptorJson(const QJsonObject& descriptor);

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
        const QString& outputH5Sha256,
        const DemCoverageAudit& coverageAudit
    );

private:
    int downloadTile(const QString& url, const QString& savePath, bool requiresEarthdataAuth,
                     QString* failureDetail = nullptr);
    QJsonObject m_productDescriptorJson;
};

#endif // DEM_SOURCE_WORKER_H
