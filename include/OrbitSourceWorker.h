#pragma once
#ifndef ORBIT_SOURCE_WORKER_H
#define ORBIT_SOURCE_WORKER_H

#include "BaseWorker.h"
#include <QStringList>
#include <QDateTime>
#include <QHash>
#include <QUrl>
#include <QNetworkReply>

class QNetworkRequest;

class OrbitSourceWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit OrbitSourceWorker(QObject* parent = nullptr);
    ~OrbitSourceWorker();

    // Resolves an already cached EOF for a Sentinel-1 SAFE manifest. Matching
    // prefers POEORB and falls back to RESORB, using the same coverage and XML
    // validation rules as the Apply Orbit File node.
    static bool findCachedOrbitForManifest(const QString& manifestPath,
                                           const QString& cacheDir,
                                           QString& orbitPath,
                                           bool& isPrecise,
                                           QString* errorMessage = nullptr);

public slots:
    void fetch_orbits(
        QString projectPath,
        QStringList filePaths,
        int orbitSource,
        QString cacheDir
    );

    void fetch_and_apply_orbits(
        QString projectPath,
        QStringList filePaths,
        int orbitSource,
        QString cacheDir,
        QString targetDirName
    );

signals:
    void cancelled();
    void applyOrbitsFinished(
        const QStringList& newH5Paths,
        int podApplyOk,
        int podApplyFail,
        int podSkipped,
        const QString& targetDirName
    );

private:
    enum class OrbitSource
    {
        Asf = 0,
        Cdse = 1
    };

    struct SlcInfo
    {
        QString platform;
        QDateTime acquisitionStart;
        QDateTime acquisitionStop;
    };

    struct OrbitProduct
    {
        QString id;
        QString fileName;
        QUrl downloadUrl;
        QDateTime validStart;
        QDateTime validEnd;
        QString checksum;
        QString checksumAlgorithm;
        bool isPrecise = false;
    };

    struct NetworkResult
    {
        int statusCode = 0;
        QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
        QByteArray data;
        QString contentType;
        QString errorString;
        QUrl redirectUrl;
        bool cancelled = false;
        bool timedOut = false;
    };

    QString m_cdseAccessToken;
    QDateTime m_cdseTokenExpiry;

    bool convertOrbitSource(int value, OrbitSource& source, QString& errorMessage) const;
    QString getOriginalGranuleName(const QString& h5FilePath, const QString& projectDir);
    bool readSlcInfo(const QString& h5FilePath, const QString& projectDir, SlcInfo& info, QString& errorMessage);
    bool parseOrbitValidity(const QString& fileName, QDateTime& validStart, QDateTime& validEnd) const;
    bool productCovers(const OrbitProduct& product, const SlcInfo& info) const;
    bool findCachedOrbit(const QString& cacheDir, const SlcInfo& info, bool precise, OrbitProduct& product) const;
    bool findAsfOrbit(const SlcInfo& info, bool precise, OrbitProduct& product, QString& errorMessage);
    bool findCdseOrbit(const SlcInfo& info, bool precise, OrbitProduct& product, QString& errorMessage);
    bool resolveAndDownloadOrbit(OrbitSource source, const QString& h5FilePath, const QString& projectDir,
        const QString& cacheDir, QString& localEofPath, bool& isPrecise, QString& errorMessage);

    NetworkResult performRequest(const QNetworkRequest& request, const QByteArray& method,
        const QByteArray& body = QByteArray(), int timeoutMs = 30000);
    bool ensureCdseToken(QString& errorMessage, bool forceRefresh = false);
    bool downloadOrbitFile(OrbitSource source, const OrbitProduct& product, const QString& savePath,
        QString& errorMessage, bool allowAuthRetry = true);
    bool verifyDownloadedFile(const OrbitProduct& product, const QString& filePath, QString& errorMessage) const;
    bool validateOrbitXml(const QString& filePath, const QDateTime& expectedStart,
        const QDateTime& expectedEnd, QString& errorMessage) const;
    void clearCdseCredentials();
};

#endif // ORBIT_SOURCE_WORKER_H
