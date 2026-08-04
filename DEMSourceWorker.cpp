#include "DEMSourceWorker.h"
#include <demsourcemanager.h>
#include <FormatConversion.h>
#include <Utils.h>
#include <InSARLogManager.h>
#include <icon_source.h>
#include <QThread>
#include "NodeUtils.h"
#include <gdal_priv.h>


#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QEventLoop>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QtMath>
#include <QTextStream>
#include <QMap>
#include <QRegularExpression>
#include <QTimer>


#ifdef _DEBUG
#pragma comment(lib, "Dem_d.lib")
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Dem.lib")
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif



using namespace std;
using namespace cv;

DEMSourceWorker::DEMSourceWorker(QObject* parent)
    : BaseWorker(parent)
{
}

DEMSourceWorker::~DEMSourceWorker()
{
}

int DEMSourceWorker::downloadTile(const QString& url, const QString& savePath, bool requiresEarthdataAuth,
                                  QString* failureDetail)
{
    QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(url)));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    if (requiresEarthdataAuth)
    {
        // Earthdata-protected sources need Basic authentication. Public Copernicus S3
        // rejects this header with HTTP 400, so it must never be sent there.
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
        QString encryptedPass = settings.value("DEM/EarthdataPassword", "").toString();
        QString username = QString::fromUtf8(QByteArray::fromBase64(encryptedUser.toUtf8()));
        QString password = QString::fromUtf8(QByteArray::fromBase64(encryptedPass.toUtf8()));

        QByteArray authHeader = "Basic " + QByteArray(QString("%1:%2").arg(username).arg(password).toUtf8()).toBase64();
        request.setRawHeader("Authorization", authHeader);

        connect(&manager, &QNetworkAccessManager::authenticationRequired,
                this, [username, password](QNetworkReply*, QAuthenticator* authenticator) {
                    authenticator->setUser(username);
                    authenticator->setPassword(password);
                });
    }

    QString tempPath = savePath + ".part";
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly))
    {
        InSARLogManager::LogError("DEMSourceWorker", QString("Failed to open temp file for write: ") + tempPath);
        if (failureDetail) {
            *failureDetail = QStringLiteral("无法写入临时文件：%1").arg(tempPath);
        }
        return -1;
    }

    QNetworkReply* reply = manager.get(request);

    constexpr int kDownloadInactivityTimeoutMs = 30000;
    bool downloadTimedOut = false;
    qint64 downloadedBytes = 0;

    QTimer inactivityTimer;
    inactivityTimer.setSingleShot(true);
    connect(&inactivityTimer, &QTimer::timeout, reply, [&]() {
        downloadTimedOut = true;
        InSARLogManager::LogWarning("DEMSourceWorker", QString("Download tile stalled for %1 ms: %2")
            .arg(kDownloadInactivityTimeoutMs).arg(url));
        reply->abort();
    });

    // 绑定读取信号
    connect(reply, &QNetworkReply::readyRead, this, [&]() {
        const QByteArray data = reply->readAll();
        if (!data.isEmpty()) {
            tempFile.write(data);
            inactivityTimer.start(kDownloadInactivityTimeoutMs);
        }
    });

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    connect(reply, &QNetworkReply::downloadProgress, &inactivityTimer,
            [&](qint64 bytesReceived, qint64) {
        if (bytesReceived > downloadedBytes) {
            downloadedBytes = bytesReceived;
            inactivityTimer.start(kDownloadInactivityTimeoutMs);
        }
    });

    QTimer cancelCheckTimer;
    connect(&cancelCheckTimer, &QTimer::timeout, this, [&]() {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogInfo("DEMSourceWorker", "User requested stop during tile download.");
            reply->abort();
        }
    });

    inactivityTimer.start(kDownloadInactivityTimeoutMs);
    cancelCheckTimer.start(200); // 200ms

    loop.exec(); // 阻塞当前线程直到下载完毕、超时或被取消

    inactivityTimer.stop();
    cancelCheckTimer.stop();

    tempFile.close();

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError err = reply->error();
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QString finalUrl = reply->url().toString();
    QString errorString = reply->errorString();

    if (err == QNetworkReply::OperationCanceledError)
    {
        tempFile.remove();
        reply->deleteLater();
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogInfo("DEMSourceWorker", "Download canceled by user.");
        }
        else if (downloadTimedOut)
        {
            InSARLogManager::LogError("DEMSourceWorker", QString("Download timeout. URL: %1").arg(url));
            if (failureDetail) {
                *failureDetail = QStringLiteral("下载 %1 秒内无数据进度").arg(kDownloadInactivityTimeoutMs / 1000);
            }
        }
        else
        {
            InSARLogManager::LogError("DEMSourceWorker", QString("Download aborted. URL: %1").arg(url));
            if (failureDetail) {
                *failureDetail = QStringLiteral("下载被中止");
            }
        }
        return -1;
    }


    if (err == QNetworkReply::NoError && (statusCode == 200 || statusCode == 206) && !contentType.contains("html", Qt::CaseInsensitive))
    {
        // 增加下载内容一致性校验：确保下载文件大小与 Content-Length 一致
        qint64 expectedSize = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        qint64 actualSize = tempFile.size();
        if (expectedSize > 0 && actualSize != expectedSize)
        {
            InSARLogManager::LogError("DEMSourceWorker", QString("Downloaded file size mismatch. Expected: %1, Actual: %2").arg(expectedSize).arg(actualSize));
            if (failureDetail) {
                *failureDetail = QStringLiteral("下载文件大小不完整，期望 %1 字节，实际 %2 字节").arg(expectedSize).arg(actualSize);
            }
            tempFile.remove();
            reply->deleteLater();
            return -1;
        }

        if (QFile::exists(savePath))
        {
            QFile::remove(savePath);
        }
        tempFile.rename(savePath);
        reply->deleteLater();
        return 1;
    }
    else
    {
        tempFile.remove();
        reply->deleteLater();

        if (err == QNetworkReply::ContentNotFoundError || statusCode == 404)
        {
            return 0; // 404 Not Found (例如海洋瓦片不存在)
        }
        else if (err == QNetworkReply::AuthenticationRequiredError || statusCode == 401)
        {
            if (failureDetail) {
                *failureDetail = QStringLiteral("HTTP %1：%2").arg(statusCode).arg(errorString);
            }
            InSARLogManager::LogError("DEMSourceWorker", QString("DEM authentication failed. URL: %1, Status Code: %2, Error: %3")
                .arg(url).arg(statusCode).arg(errorString));
            return -1;
        }
        else
        {
            QString errorMessage = QString("Download failed: URL: %1, Final URL: %2, Status Code: %3, Content-Type: %4, Error: %5")
                .arg(url).arg(finalUrl).arg(statusCode).arg(contentType).arg(errorString);
            InSARLogManager::LogWarning("DEMSourceWorker", errorMessage);

            if (failureDetail) {
                *failureDetail = QStringLiteral("HTTP %1：%2").arg(statusCode).arg(errorString);
            }
            return -1;
        }
    }
}

void DEMSourceWorker::fetch_dem(
    QString projectPath,
    QString projectName,
    QString stagingNode,
    QString outputNodeName,
    QStringList filePaths,
    int demSource,
    double targetResolution,
    QString cacheDir
)
{
    InSARLogManager::LogInfo("DEMSourceWorker", QString("External DEM fetch started. Target node: %1, Source Type: %2").arg(outputNodeName).arg(demSource));

    if (projectPath.isEmpty() || projectName.isEmpty() || stagingNode.isEmpty() ||
        outputNodeName.isEmpty() || filePaths.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }

    emit updateProcess(5, QStringLiteral("准备解析范围……"));

    // 1. 获取绝对工作目录（防止二次剥离目录路径）
    QString save_path = projectPath;
    if (projectPath.endsWith(".insar", Qt::CaseInsensitive))
    {
        save_path = QFileInfo(projectPath).absolutePath();
    }

    // 2. 精准提取 AOI 范围
    double min_lon = 0, max_lon = 0, min_lat = 0, max_lat = 0;
    bool aoi_ok = false;
    QString firstInput = filePaths.first();
    if (QFileInfo(firstInput).isRelative())
    {
        firstInput = save_path + "/" + firstInput;
    }

    FormatConversion FC;

    // 检查是否存在 mapped_lon/mapped_lat (已地理编码的 H5)并从其提取 AOI 范围
    Mat mat_lon, mat_lat;
    bool mapped_check = false;
    {
        NodeUtils::Hdf5Locker locker;
        mapped_check = (NodeUtils::readMatFromH5(firstInput, "mapped_lon", mat_lon) &&
                        NodeUtils::readMatFromH5(firstInput, "mapped_lat", mat_lat));
    }
    if (mapped_check)
    {
        double min_lon_val, max_lon_val, min_lat_val, max_lat_val;
        minMaxLoc(mat_lon, &min_lon_val, &max_lon_val);
        minMaxLoc(mat_lat, &min_lat_val, &max_lat_val);
        min_lon = min_lon_val;
        max_lon = max_lon_val;
        min_lat = min_lat_val;
        max_lat = max_lat_val;
        aoi_ok = true;
        InSARLogManager::LogInfo("DEMSourceWorker", QString("Extracted AOI from mapped coordinates: Lon[%1, %2], Lat[%3, %4]").arg(min_lon).arg(max_lon).arg(min_lat).arg(max_lat));
    }

    if (!aoi_ok)
    {
        // 尝试从雷达多项式系数计算边界
        std::string source_file;
        QString src_file;
        bool read_src_ok = false;
        {
            NodeUtils::Hdf5Locker locker;
            read_src_ok = NodeUtils::readStringFromH5(firstInput, "source_1", source_file);
        }
        if (read_src_ok)
        {
            src_file = save_path + "/" + QString(source_file.c_str());
            // 如果拼接出的文件不存在（如 source_1 存储的是绝对路径且不可访问），则回退使用输入 H5 本身
            if (!QFile::exists(src_file))
            {
                src_file = firstInput;
            }
        }
        else
        {
            // 否则本身即是原始 SLC 或裁剪影像，直接以输入 H5 文件自身作为参数源文件
            src_file = firstInput;
        }

        if (QFile::exists(src_file))
        {
            int sceneHeight = 0, sceneWidth = 0, offset_row = 0, offset_col = 0;
            Mat lon_coef, lat_coef;
            bool read_para_ok = false;
            {
                NodeUtils::Hdf5Locker locker;
                if (NodeUtils::readScalarFromH5(src_file, "range_len", sceneWidth) &&
                    NodeUtils::readScalarFromH5(src_file, "azimuth_len", sceneHeight) &&
                    NodeUtils::readMatFromH5(src_file, "lon_coefficient", lon_coef) &&
                    NodeUtils::readMatFromH5(src_file, "lat_coefficient", lat_coef))
                {
                    read_para_ok = true;
                    offset_row = 0;
                    offset_col = 0;
                    NodeUtils::readScalarFromH5(src_file, "offset_row", offset_row);
                    NodeUtils::readScalarFromH5(src_file, "offset_col", offset_col);
                }
            }
            if (read_para_ok)
            {
                double lonMax = 0, lonMin = 0, latMax = 0, latMin = 0;
                if (Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
                    &lonMax, &latMax, &lonMin, &latMin) == 0)
                {
                    min_lon = lonMin;
                    max_lon = lonMax;
                    min_lat = latMin;
                    max_lat = latMax;
                    aoi_ok = true;
                    InSARLogManager::LogInfo("DEMSourceWorker", QString("Extracted AOI from radar geometry boundary: Lon[%1, %2], Lat[%3, %4]").arg(min_lon).arg(max_lon).arg(min_lat).arg(max_lat));
                }
            }
        }
    }

    if (!aoi_ok)
    {
        emit errorProcess(QStringLiteral("解析输入数据地理包围框失败，请检查上游节点数据是否正确。"));
        return;
    }

    // 外扩 0.05 度以保证边缘插值时不溢出
    min_lon -= 0.05;
    max_lon += 0.05;
    min_lat -= 0.05;
    max_lat += 0.05;

    // 3. 缓存目录及多源隔离初始化
    if (cacheDir.isEmpty())
    {
        QString projectDir = projectPath;
        if (projectPath.endsWith(".insar", Qt::CaseInsensitive))
        {
            projectDir = QFileInfo(projectPath).absolutePath();
        }
        
        if (!projectDir.isEmpty())
        {
            cacheDir = QDir::toNativeSeparators(projectDir + "/.dem_cache");
        }
        else
        {
            cacheDir = QDir::toNativeSeparators(QDir::currentPath() + "/.dem_cache");
        }
    }
    
    QString subDirName = "srtm1";
    if (demSource == 1) subDirName = "srtm3";
    else if (demSource == 2) subDirName = "copernicus";
    else if (demSource == 3) subDirName = "aster";

    QString fullCachePath = cacheDir + "/" + subDirName;
    QDir().mkpath(fullCachePath);

    // 启动前清理缓存目录下的重复文件，防止用户手动复制重命名绕过下载
    {
        QDir dir(fullCachePath);
        QStringList filters;
        QString ext;
        if (demSource == 0 || demSource == 1)
        {
            filters << "*.hgt";
            ext = ".hgt";
        }
        else
        {
            filters << "*.tif";
            ext = ".tif";
        }
        QFileInfoList list = dir.entryInfoList(filters, QDir::Files);
        
        // 1. 按照经纬度 Key 进行分组，避免 O(n²) 全量二进制比较
        QMap<QString, QList<QFileInfo>> groups;
        QRegularExpression keyRegex("([NSns]\\d{2}[EWew]\\d{3})");
        for (const QFileInfo& fi : list)
        {
            QRegularExpressionMatch match = keyRegex.match(fi.fileName());
            QString key;
            if (match.hasMatch())
            {
                key = match.captured(1).toUpper();
            }
            else
            {
                key = fi.baseName().toUpper();
            }
            groups[key].append(fi);
        }

        // 用于辅助检查两个文件内容是否完全一致的 lambda 函数
        auto isIdentical = [](const QString& path1, const QString& path2, qint64 size1, qint64 size2) -> bool {
            if (size1 != size2) return false;
            if (size1 == 0) return true; // 都是空文件视为一致
            QFile f1(path1);
            QFile f2(path2);
            if (!f1.open(QIODevice::ReadOnly) || !f2.open(QIODevice::ReadOnly))
            {
                return false;
            }
            QByteArray b1 = f1.read(4096);
            QByteArray b2 = f2.read(4096);
            if (b1 != b2)
            {
                return false;
            }
            while (!f1.atEnd() && !f2.atEnd())
            {
                if (f1.read(65536) != f2.read(65536))
                {
                    return false;
                }
            }
            return true;
        };

        QStringList toDelete;

        // 2. 遍历每个分组，挑选唯一的保留文件并标记其余副本
        for (auto it = groups.begin(); it != groups.end(); ++it)
        {
            const QString& key = it.key();
            const QList<QFileInfo>& groupList = it.value();
            if (groupList.size() <= 1)
            {
                continue; // 只有一个文件，无需对比
            }

            QString canonicalName = key + ext;
            QFileInfo keepFileInfo;
            bool foundKeep = false;

            // 优先保留满足规范文件名且大小 > 0 的文件
            for (const QFileInfo& fi : groupList)
            {
                if (fi.fileName().compare(canonicalName, Qt::CaseInsensitive) == 0 && fi.size() > 0)
                {
                    keepFileInfo = fi;
                    foundKeep = true;
                    break;
                }
            }

            // 没有规范文件名时，保留首个大小 > 0 的有效文件
            if (!foundKeep)
            {
                for (const QFileInfo& fi : groupList)
                {
                    if (fi.size() > 0)
                    {
                        keepFileInfo = fi;
                        foundKeep = true;
                        break;
                    }
                }
            }

            // 如果全部为空文件，则优先保留符合规范文件名的空文件，否则保留第一个
            if (!foundKeep)
            {
                for (const QFileInfo& fi : groupList)
                {
                    if (fi.fileName().compare(canonicalName, Qt::CaseInsensitive) == 0)
                    {
                        keepFileInfo = fi;
                        foundKeep = true;
                        break;
                    }
                }
                if (!foundKeep && !groupList.isEmpty())
                {
                    keepFileInfo = groupList.first();
                    foundKeep = true;
                }
            }

            // 将该分组内除保留文件外，且内容一致的其余所有副本加入删除列表（不允许把保留的文件加入删除集合）
            if (foundKeep)
            {
                for (const QFileInfo& fi : groupList)
                {
                    if (fi.absoluteFilePath() == keepFileInfo.absoluteFilePath())
                    {
                        continue;
                    }
                    if (isIdentical(fi.absoluteFilePath(), keepFileInfo.absoluteFilePath(), fi.size(), keepFileInfo.size()))
                    {
                        InSARLogManager::LogWarning("DEMSourceWorker", QStringLiteral("检测到缓存中的重复文件内容：%1 与 %2。将进行清理。").arg(fi.fileName()).arg(keepFileInfo.fileName()));
                        toDelete.append(fi.absoluteFilePath());
                    }
                }
            }
        }

        // 去重删除列表
        toDelete.removeDuplicates();

        // 3. 执行删除并报告明确错误
        bool deleteSuccess = true;
        QStringList failedPaths;
        for (const QString& path : toDelete)
        {
            if (!QFile::remove(path))
            {
                InSARLogManager::LogError("DEMSourceWorker", QStringLiteral("清理重复缓存文件失败：%1").arg(path));
                failedPaths.append(path);
                deleteSuccess = false;
            }
        }

        if (!deleteSuccess)
        {
            emit errorProcess(QStringLiteral("清理本地缓存重复文件失败：%1").arg(failedPaths.join(", ")));
            return;
        }
    }

    // 4. 计算瓦片跨度并准备下载/检索
    int startLon = qFloor(min_lon);
    int endLon = qFloor(max_lon);
    int startLat = qFloor(min_lat);
    int endLat = qFloor(max_lat);

    QStringList cachedFiles;
    QStringList serverNotFoundTiles;
    QStringList serverNotFoundTilesIntersectingOutput;
    const int requestedTileCount = (endLat - startLat + 1) * (endLon - startLon + 1);
    emit updateProcess(10, QStringLiteral("检索本地缓存及下载瓦片中……"));

    for (int lat = startLat; lat <= endLat; ++lat)
    {
        for (int lon = startLon; lon <= endLon; ++lon)
        {
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                emit cancelled();
                return;
            }

            char latNS = (lat >= 0) ? 'N' : 'S';
            char lonEW = (lon >= 0) ? 'E' : 'W';
            QString tileName = QString("%1%2%3%4")
                .arg(latNS)
                .arg(qAbs(lat), 2, 10, QChar('0'))
                .arg(lonEW)
                .arg(qAbs(lon), 3, 10, QChar('0'));

            QString expectedFile = fullCachePath + "/" + tileName;
            if (demSource == 0 || demSource == 1)
            {
                expectedFile += ".hgt";
            }
            else
            {
                expectedFile += ".tif";
            }

            // 检查缓存并验证其物理完整性
            bool tileFound = false;
            if (QFile::exists(expectedFile))
            {
                bool valid = true;
                qint64 size = QFileInfo(expectedFile).size();
                if (demSource == 0 && size != 25934402) {
                    valid = false;
                } else if (demSource == 1 && size != 2884802) {
                    valid = false;
                } else if (size < 10240) {
                    valid = false;
                }

                if (valid) {
                    tileFound = true;
                } else {
                    InSARLogManager::LogWarning("DEMSourceWorker", QStringLiteral("检测到损坏的高程瓦片缓存 %1（大小：%2 字节），将删除并重新下载。").arg(expectedFile).arg(size));
                    QFile::remove(expectedFile);
                }
            }
            else
            {
                // 如果是 zip，尝试解压
                QString zipFile = fullCachePath + "/" + tileName;
                if (demSource == 0) zipFile += ".SRTMGL1.hgt.zip";
                else if (demSource == 1) zipFile += ".SRTMGL3.hgt.zip";
                else zipFile += ".zip";

                if (QFile::exists(zipFile))
                {
                    // 调用已有的 DigitalElevationModel::unzip
                    if (DigitalElevationModel::unzip(zipFile.toLocal8Bit().constData(), fullCachePath.toLocal8Bit().constData()) == 0)
                    {
                        if (QFile::exists(expectedFile))
                        {
                            bool valid = true;
                            qint64 size = QFileInfo(expectedFile).size();
                            if (demSource == 0 && size != 25934402) valid = false;
                            else if (demSource == 1 && size != 2884802) valid = false;

                            if (valid) {
                                tileFound = true;
                            } else {
                                InSARLogManager::LogWarning("DEMSourceWorker", QStringLiteral("解压的缓存文件 %1 校验失败（大小：%2 字节），予以删除。").arg(expectedFile).arg(size));
                                QFile::remove(expectedFile);
                            }
                        }
                    }
                }
            }

            // 如果本地无缓存，执行下载
            if (!tileFound)
            {
                QString downloadUrl;
                QString targetZipOrTif = expectedFile;

                if (demSource == 0) // SRTM 1"
                {
                    downloadUrl = QString("https://data.lpdaac.earthdatacloud.nasa.gov/lp-prod-protected/SRTMGL1.003/%1.SRTMGL1.hgt/%1.SRTMGL1.hgt.zip").arg(tileName);
                    targetZipOrTif = fullCachePath + "/" + tileName + ".SRTMGL1.hgt.zip";
                }
                else if (demSource == 1) // SRTM 3"
                {
                    downloadUrl = QString("https://data.lpdaac.earthdatacloud.nasa.gov/lp-prod-protected/SRTMGL3.003/%1.SRTMGL3.hgt/%1.SRTMGL3.hgt.zip").arg(tileName);
                    targetZipOrTif = fullCachePath + "/" + tileName + ".SRTMGL3.hgt.zip";
                }
                else if (demSource == 2) // Copernicus 30m
                {
                    // 使用 AWS S3 公共免密源
                    char ns = (lat >= 0) ? 'N' : 'S';
                    char ew = (lon >= 0) ? 'E' : 'W';
                    QString tileAWS = QString("Copernicus_DSM_COG_10_%1%2_00_%3%4_00_DEM")
                        .arg(ns)
                        .arg(qAbs(lat), 2, 10, QChar('0'))
                        .arg(ew)
                        .arg(qAbs(lon), 3, 10, QChar('0'));
                    downloadUrl = QString("https://copernicus-dem-30m.s3.amazonaws.com/%1/%2.tif").arg(tileAWS).arg(tileAWS);
                }
                else if (demSource == 3) // ASTER GDEM (新云端已改为直接分发单张 .tif，无需解压)
                {
                    downloadUrl = QString("https://data.lpdaac.earthdatacloud.nasa.gov/lp-prod-protected/ASTGTM.003/ASTGTMV003_%1_dem.tif").arg(tileName);
                    targetZipOrTif = expectedFile; // 即 [tileName].tif
                }

                emit updateProcess(10 + (lat - startLat) * 30 / (endLat - startLat + 1), QStringLiteral("正在下载 DEM 瓦片 %1……").arg(tileName));
                
                QString downloadFailure;
                const bool requiresEarthdataAuth = demSource != 2;
                int dlResult = downloadTile(downloadUrl, targetZipOrTif, requiresEarthdataAuth, &downloadFailure);
                if (dlResult == 1)
                {
                    // 如果是 zip，解压它
                    if (targetZipOrTif.endsWith(".zip"))
                    {
                        int unzipRet = DigitalElevationModel::unzip(targetZipOrTif.toLocal8Bit().constData(), fullCachePath.toLocal8Bit().constData());
                        // 下载完后清理临时压缩包
                        QFile::remove(targetZipOrTif);

                        // 校验解压后文件是否真实存在且大小正确
                        bool valid = false;
                        if (unzipRet == 0 && QFile::exists(expectedFile))
                        {
                            qint64 size = QFileInfo(expectedFile).size();
                            if (demSource == 0 && size == 25934402) valid = true;
                            else if (demSource == 1 && size == 2884802) valid = true;
                        }

                        if (!valid)
                        {
                            if (QFile::exists(expectedFile)) {
                                QFile::remove(expectedFile);
                            }
                            emit errorProcess(QStringLiteral("高程瓦片 %1 解压校验失败，下载数据可能损坏或鉴权过期！").arg(tileName));
                            return;
                        }

                        // 对于 ASTER GDEM，解压出来的文件通常是 ASTGTMV003_%1_dem.tif，将其重命名为 expectedFile (%1.tif)
                        if (demSource == 3)
                        {
                            QString unzippedTif = fullCachePath + "/ASTGTMV003_" + tileName + "_dem.tif";
                            if (QFile::exists(unzippedTif))
                            {
                                QFile::rename(unzippedTif, expectedFile);
                            }
                        }
                    }
                    
                    if (QFile::exists(expectedFile))
                    {
                        tileFound = true;
                    }
                }
                else if (dlResult == 0)
                {
                    // HTTP 404 is frequently an ocean tile for SRTM, but it is not proof that
                    // the requested area is safe to treat as zero elevation.
                    serverNotFoundTiles.append(tileName);
                    const double intersectionMinLon = qMax(min_lon, static_cast<double>(lon));
                    const double intersectionMaxLon = qMin(max_lon, static_cast<double>(lon + 1));
                    const double intersectionMinLat = qMax(min_lat, static_cast<double>(lat));
                    const double intersectionMaxLat = qMin(max_lat, static_cast<double>(lat + 1));
                    if (intersectionMinLon < intersectionMaxLon && intersectionMinLat < intersectionMaxLat) {
                        serverNotFoundTilesIntersectingOutput.append(tileName);
                    }
                    InSARLogManager::LogDebug("DEMSourceWorker",
                        QString("Tile server-not-found 404: tile=%1, action=recorded_as_server_not_found").arg(tileName),
                        "dem.tile");
                    continue;
                }
                else
                {
                    emit errorProcess(QStringLiteral("获取 DEM 瓦片 %1 失败：%2").arg(tileName, downloadFailure));
                    return;
                }
            }

            if (tileFound)
            {
                cachedFiles.append(expectedFile);
            }
            else
            {
                emit errorProcess(QStringLiteral("获取 DEM 瓦片 %1 失败，请检查网络设置或手动下载至目录：%2").arg(tileName).arg(fullCachePath));
                return;
            }
        }
    }

    if (cachedFiles.isEmpty())
    {
        emit errorProcess(QStringLiteral("未找到任何有效的本地缓存或下载瓦片。"));
        return;
    }

    emit updateProcess(50, QStringLiteral("构建瓦片拼接与重采样……"));

    // 5. 多瓦片拼接机制 (VRT)
    QString finalInputFile;
    QString vrtPath = save_path + "/" + stagingNode + "/mosaic.vrt";
    QDir().mkpath(save_path + "/" + stagingNode);

    if (cachedFiles.size() == 1)
    {
        finalInputFile = cachedFiles.first();
    }
    else
    {
        // 拼接成 VRT
        QFile vrtFile(vrtPath);
        if (!vrtFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            emit errorProcess(QStringLiteral("创建拼接虚拟描述文件(vrt)失败。"));
            return;
        }

        QTextStream out(&vrtFile);
        out.setCodec("UTF-8");

        // 统一按数据源类型计算瓦片大小及重叠步长，防大小不一致导致 GDAL 无法打开 VRT
        int tileW = 3601;
        int tileH = 3601;
        int stepW = 3600;
        int stepH = 3600;
        bool hasOverlap = true;

        if (demSource == 1) // SRTM 3"
        {
            tileW = 1201;
            tileH = 1201;
            stepW = 1200;
            stepH = 1200;
            hasOverlap = true;
        }
        else if (demSource == 2 || demSource == 3) // Copernicus 30m 或 ASTER GDEM
        {
            tileW = 3600;
            tileH = 3600;
            stepW = 3600;
            stepH = 3600;
            hasOverlap = false;
        }

        double res = (demSource == 1) ? (3.0 / 3600.0) : (1.0 / 3600.0);
        double vrtMinLon = startLon;
        double vrtMaxLat = endLat + 1.0;
        int totalW = (endLon - startLon + 1) * stepW + (hasOverlap ? 1 : 0);
        int totalH = (endLat - startLat + 1) * stepH + (hasOverlap ? 1 : 0);

        out << "<VRTDataset rasterXSize=\"" << totalW << "\" rasterYSize=\"" << totalH << "\">\n";
        out << "  <SRS>GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]</SRS>\n";
        out << "  <GeoTransform>" << vrtMinLon << ", " << res << ", 0.0, " << vrtMaxLat << ", 0.0, " << -res << "</GeoTransform>\n";
        out << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
        out << "    <NoDataValue>-32767</NoDataValue>\n";

        for (const QString& file : cachedFiles)
        {
            QFileInfo fi(file);
            QString tName = fi.baseName(); // NxxExxx
            
            // 提取经纬度
            char ns = tName.at(0).toLatin1();
            int latVal = tName.mid(1, 2).toInt();
            if (ns == 'S' || ns == 's') latVal = -latVal;

            char ew = tName.at(3).toLatin1();
            int lonVal = tName.mid(4, 3).toInt();
            if (ew == 'W' || ew == 'w') lonVal = -lonVal;

            int xOff = qRound((lonVal - vrtMinLon) * stepW);
            int yOff = qRound((vrtMaxLat - (latVal + 1.0)) * stepH);

            out << "    <SimpleSource>\n";
            out << "      <SourceFilename relativeToVRT=\"0\">" << QDir::cleanPath(file) << "</SourceFilename>\n";
            out << "      <SourceBand>1</SourceBand>\n";
            out << "      <SourceProperties RasterXSize=\"" << tileW << "\" RasterYSize=\"" << tileH << "\" DataType=\"Float32\" />\n";
            out << "      <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"" << tileW << "\" ySize=\"" << tileH << "\" />\n";
            out << "      <DstRect xOff=\"" << xOff << "\" yOff=\"" << yOff << "\" xSize=\"" << tileW << "\" ySize=\"" << tileH << "\" />\n";
            out << "    </SimpleSource>\n";
        }

        out << "  </VRTRasterBand>\n";
        out << "</VRTDataset>\n";
        vrtFile.close();

        finalInputFile = vrtPath;
    }

    emit updateProcess(65, QStringLiteral("裁剪高程数据中……"));

    // 6. 调用 DEMSourceManager 执行裁剪转换
    DEMSourceManager manager;
    Mat cropped_dem, dem_x, dem_y, dem_z;
    double new_gt[6] = { 0 };
    char wkt_projection[1024] = { 0 };



    int ret = manager.read_crop_and_resample_dem(
        QDir::toNativeSeparators(finalInputFile).toLocal8Bit().constData(),
        min_lon, max_lon,
        min_lat, max_lat,
        targetResolution,
        cropped_dem,
        new_gt,
        wkt_projection,
        1024
    );

    if (ret != 0)
    {
        if (isStopRequested()) {
            emit cancelled();
            return;
        }
        emit errorProcess(QStringLiteral("裁剪 DEM 数据失败：") + QString::fromLocal8Bit(manager.error_msg));
        return;
    }
    if (isStopRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(80, QStringLiteral("转换高程坐标至 ECEF……"));
    ret = manager.convert_dem_to_ecef(
        cropped_dem,
        new_gt,
        wkt_projection,
        dem_x,
        dem_y,
        dem_z
    );

    if (ret != 0)
    {
        if (isStopRequested()) {
            emit cancelled();
            return;
        }
        emit errorProcess(QStringLiteral("坐标系转换至地心坐标(ECEF)失败。"));
        return;
    }
    if (isStopRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(90, QStringLiteral("写入 H5 数据文件……"));

    // 7. 写入 H5 文件
    QString outputH5Name = outputNodeName + "_dem.h5";
    QString outputH5Path = save_path + "/" + stagingNode + "/" + outputH5Name;

    // 7.5 写入 TIF 成果文件（供下游工作流节点使用）
    QString outputTifName = outputNodeName + "_dem.tif";
    QString outputTifPath = save_path + "/" + stagingNode + "/" + outputTifName;
    if (!NodeUtils::writeDemToTif(outputTifPath, cropped_dem, new_gt, wkt_projection))
    {
        if (!vrtPath.isEmpty() && QFile::exists(vrtPath))
        {
            QFile::remove(vrtPath);
        }
        emit errorProcess(QStringLiteral("写入 DEM TIF 成果文件失败，目标路径：%1").arg(outputTifPath));
        return;
    }

    // 写入经纬度辅助 2D 矩阵
    int rows = cropped_dem.rows;
    int cols = cropped_dem.cols;
    Mat out_lon(rows, cols, CV_64FC1);
    Mat out_lat(rows, cols, CV_64FC1);

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            // 使用严密的仿射半像素中心偏移公式修正
            double col_c = c + 0.5;
            double row_c = r + 0.5;
            out_lon.at<double>(r, c) = new_gt[0] + col_c * new_gt[1] + row_c * new_gt[2];
            out_lat.at<double>(r, c) = new_gt[3] + col_c * new_gt[4] + row_c * new_gt[5];
        }
    }

    bool write_success = false;
    // 确定数据源名称
    string srcName = "SRTM1";
    if (demSource == 1) srcName = "SRTM3";
    else if (demSource == 2) srcName = "Copernicus";
    else if (demSource == 3) srcName = "ASTER";

    {
        NodeUtils::Hdf5Locker locker;
        if (locker.isLocked() && FC.creat_new_h5(outputH5Path.toStdString().c_str()) == 0)
        {
            // 确保高程和三维坐标使用双精度(CV_64F)写入，防止 precompiled DLL 读取时发生类型 Mismatch
            if (cropped_dem.type() != CV_64F) cropped_dem.convertTo(cropped_dem, CV_64F);
            if (dem_x.type() != CV_64F) dem_x.convertTo(dem_x, CV_64F);
            if (dem_y.type() != CV_64F) dem_y.convertTo(dem_y, CV_64F);
            if (dem_z.type() != CV_64F) dem_z.convertTo(dem_z, CV_64F);

            const bool datasetsWritten =
                FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "dem", cropped_dem) == 0 &&
                FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "dem_x", dem_x) == 0 &&
                FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "dem_y", dem_y) == 0 &&
                FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "dem_z", dem_z) == 0 &&
                FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "lon", out_lon) == 0 &&
                FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "lat", out_lat) == 0;

            const bool metadataWritten =
                FC.write_str_to_h5(outputH5Path.toStdString().c_str(), "dem_source", srcName.c_str()) == 0 &&
                FC.write_double_to_h5(outputH5Path.toStdString().c_str(), "dem_min_lon", min_lon) == 0 &&
                FC.write_double_to_h5(outputH5Path.toStdString().c_str(), "dem_max_lon", max_lon) == 0 &&
                FC.write_double_to_h5(outputH5Path.toStdString().c_str(), "dem_min_lat", min_lat) == 0 &&
                FC.write_double_to_h5(outputH5Path.toStdString().c_str(), "dem_max_lat", max_lat) == 0 &&
                FC.write_str_to_h5(outputH5Path.toStdString().c_str(), "dem_cache_path",
                                   QDir::toNativeSeparators(cacheDir).toStdString().c_str()) == 0;
            const QString serverNotFoundTileText = serverNotFoundTiles.isEmpty()
                ? QStringLiteral("none") : serverNotFoundTiles.join(",");
            const QString intersectingServerNotFoundTileText = serverNotFoundTilesIntersectingOutput.isEmpty()
                ? QStringLiteral("none") : serverNotFoundTilesIntersectingOutput.join(",");
            const bool serverNotFoundMetadataWritten =
                FC.write_int_to_h5(outputH5Path.toStdString().c_str(), "dem_server_404_tile_count", serverNotFoundTiles.size()) == 0 &&
                FC.write_str_to_h5(outputH5Path.toStdString().c_str(), "dem_server_404_tiles",
                    serverNotFoundTileText.toStdString().c_str()) == 0 &&
                FC.write_int_to_h5(outputH5Path.toStdString().c_str(), "dem_server_404_intersecting_output_tile_count", serverNotFoundTilesIntersectingOutput.size()) == 0 &&
                FC.write_str_to_h5(outputH5Path.toStdString().c_str(), "dem_server_404_intersecting_output_tiles",
                    intersectingServerNotFoundTileText.toStdString().c_str()) == 0 &&
                FC.write_str_to_h5(outputH5Path.toStdString().c_str(), "dem_server_404_policy",
                    "HTTP 404 is recorded as server-not-found; it is not treated as confirmed ocean.") == 0;
            if (!serverNotFoundMetadataWritten) {
                InSARLogManager::LogError("DEMSourceWorker", "Failed to write DEM server-404 audit metadata.");
            }

            // 写入行列偏移量以向下兼容
            Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
            tmp_int.at<int>(0, 0) = 0;
            const bool offsetsWritten =
                FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "offset_row", tmp_int) == 0 &&
                FC.write_array_to_h5(outputH5Path.toStdString().c_str(), "offset_col", tmp_int) == 0;
            write_success = datasetsWritten && metadataWritten && serverNotFoundMetadataWritten && offsetsWritten;
        }
    }

    if (!write_success)
    {
        if (isStopRequested()) {
            emit cancelled();
            return;
        }
        emit errorProcess(QStringLiteral("创建或写入输出 H5 文件失败。"));
        return;
    }

    // 清理临时 .vrt 文件
    if (!vrtPath.isEmpty() && QFile::exists(vrtPath))
    {
        QFile::remove(vrtPath);
    }

    if (isStopRequested()) {
        emit cancelled();
        return;
    }

    if (!serverNotFoundTilesIntersectingOutput.isEmpty()) {
        InSARLogManager::LogWarning("DEMSourceWorker",
            QString("DEM server returned HTTP 404 for %1 tile(s) intersecting the final DEM crop: %2. "
                    "They were not assumed to be ocean; review DEM NoData coverage before terrain-sensitive processing.")
                .arg(serverNotFoundTilesIntersectingOutput.size())
                .arg(serverNotFoundTilesIntersectingOutput.join(", ")));
    }

    emit updateProcess(100, QStringLiteral("外部 DEM 获取完成。"));
    // 清理完成后再通知主线程挂载输出，取消时不会提前暴露部分结果。
    const bool outputValidated = QFileInfo(outputH5Path).isFile() && QFileInfo(outputH5Path).size() > 0 &&
        QFileInfo(outputTifPath).isFile() && QFileInfo(outputTifPath).size() > 0;
    emit demFetchFinished(outputH5Path, stagingNode, projectName, demSource, targetResolution,
                          cachedFiles, serverNotFoundTiles, requestedTileCount, outputValidated);
    emit endProcess();
}
