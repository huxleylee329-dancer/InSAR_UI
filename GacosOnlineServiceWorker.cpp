#include "GacosOnlineServiceWorker.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QElapsedTimer>
#include <memory>
#include <QCoreApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QCryptographicHash>
#include <QTimer>
#include <QRegularExpression>
#include <QTextStream>
#include <cmath>
#include <fstream>
#include <AtmosphericCorrection.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif

// GACOS API 基础 URL
static const QString GACOS_API_BASE = "https://api.gacos.xyz";

using namespace cv;
using namespace std;

static thread_local GacosOnlineServiceWorker* currentWorker = nullptr;

static bool __stdcall gacosProgressCallback(int progress, const char* message)
{
    thread_local QElapsedTimer s_cbTimer;
    thread_local bool s_timerStarted = false;
    if (!s_timerStarted) {
        s_cbTimer.start();
        s_timerStarted = true;
    }
    if (progress != 0 && progress != 100 && s_cbTimer.elapsed() < 100) {
        return true;
    }
    s_cbTimer.restart();

    if (currentWorker) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            return false;
        }
        emit currentWorker->updateProcess(progress, QString::fromLocal8Bit(message));
    }
    return true;
}

namespace {

// GACOS 下载结果的缓存路径。
//
// 必须放在【事务之外】：本节点的暂存目录（save_path/file_name）在失败回滚时会被
// abandonOutputTransaction 递归删除，放在那里等于没缓存。
// 键由 (master日期, slave日期, 经纬度框, 格式) 决定 —— 这正是 GACOS 作业的输入，
// 所以另一批影像只要日期与范围相同就能复用（语义上本就该复用同一份大气场）。
QString gacosDownloadCachePath(const QString& savePath, const QString& masterDate, const QString& slaveDate,
                               double latMin, double latMax, double lonMin, double lonMax, int dataFormat)
{
    QString cacheRoot = savePath;
    if (cacheRoot.endsWith(QStringLiteral(".insar"), Qt::CaseInsensitive)) {
        cacheRoot = QFileInfo(cacheRoot).absolutePath();
    }
    QStringList parts;
    parts << QStringLiteral("v1") << masterDate << slaveDate
          << QString::number(latMin, 'g', 10) << QString::number(latMax, 'g', 10)
          << QString::number(lonMin, 'g', 10) << QString::number(lonMax, 'g', 10)
          << QString::number(dataFormat);
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(
        parts.join(QLatin1Char(';')).toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    const QDir cacheDir(QDir(cacheRoot).absoluteFilePath(QStringLiteral(".gacos_cache")));
    return cacheDir.absoluteFilePath(QStringLiteral("%1_%2_%3.%4")
        .arg(masterDate, slaveDate, digest, dataFormat == 0 ? QStringLiteral("tif") : QStringLiteral("ztd")));
}

}   // namespace

GacosOnlineServiceWorker::GacosOnlineServiceWorker(QObject* parent)
    : BaseWorker(parent)
{
}

GacosOnlineServiceWorker::~GacosOnlineServiceWorker()
{
}

void GacosOnlineServiceWorker::doGacosRequest(
    QString apiKey, QString email, int dataFormat,
    QString save_path, QString project_name,
    QString file_name, QStringList inputPaths)
{
    currentWorker = this;
    InSARLogManager::LogInfo("GacosOnlineServiceWorker",
        QString("GACOS请求开始. 输出: %1, 格式: %2").arg(file_name).arg(dataFormat == 0 ? "GeoTIFF" : "Binary"));

    if (save_path.isEmpty() || project_name.isEmpty() ||
        file_name.isEmpty() || apiKey.isEmpty() || inputPaths.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        currentWorker = nullptr;
        return;
    }

    // 创建输出目录
    QDir dir(save_path);
    if (!dir.mkpath(file_name)) {
        emit errorProcess(QStringLiteral("Unable to create GACOS staging output directory."));
        currentWorker = nullptr;
        return;
    }

    // 获取输入干涉图信息
    QStringList phase_names;
    QStringList phase_paths;
    QStringList output_names;
    QStringList absolute_output_paths;

    emit updateProcess(5, QStringLiteral("读取输入数据……"));

    for (const QString& inputPath : inputPaths) {
        if (inputPath.isEmpty() || !QFileInfo::exists(inputPath)) {
            emit errorProcess(QStringLiteral("Input phase file does not exist."));
            currentWorker = nullptr;
            return;
        }
        const QString phaseName = QFileInfo(inputPath).baseName();
        const QString outputName = phaseName + "_gacos_aps";
        phase_names.append(phaseName);
        phase_paths.append(inputPath);
        output_names.append(outputName);
        absolute_output_paths.append(save_path + "/" + file_name + "/" + outputName + ".h5");
    }

    int image_count = phase_paths.size();
    if (image_count == 0) {
        emit errorProcess(QStringLiteral("没有可处理的干涉图"));
        currentWorker = nullptr;
        return;
    }

    FormatConversion FC;
    int ret = 0;

    // 预检：整批输入契约。原先是逐景读元数据，第 k 景缺数据集要等前 k-1 景各自跑完
    //「提交 → 等服务器（最多 30 分钟）→ 下载 → 重采样」才暴露 —— 而作业一旦提交就无法撤回，
    // 前 k-1 景的等待全部白费。这里在提交任何作业之前把整批验完：
    // 三个数组数据集用 dataspace 探测（不读数据），两个字符串用现成的读取接口。
    for (int idx = 0; idx < image_count; ++idx) {
        const char* const requiredArrays[] = { "phase", "mapped_lat", "mapped_lon" };
        for (const char* dataset : requiredArrays) {
            int probeRows = 0, probeCols = 0;
            QString probeError;
            if (!NodeUtils::probeH5DatasetMetadata(phase_paths.at(idx), QString::fromLatin1(dataset),
                                                   &probeRows, &probeCols, &probeError)) {
                emit errorProcess(QStringLiteral("第 %1 幅缺少必需数据集 %2：%3")
                                      .arg(idx + 1).arg(QString::fromLatin1(dataset)).arg(probeError));
                currentWorker = nullptr;
                return;
            }
        }
        const char* const requiredStrings[] = { "source_1", "source_2" };
        std::string probeString;
        for (const char* dataset : requiredStrings) {
            if (!NodeUtils::readStringFromH5(phase_paths.at(idx), QString::fromLatin1(dataset), probeString)) {
                emit errorProcess(QStringLiteral("第 %1 幅缺少必需元数据 %2")
                                      .arg(idx + 1).arg(QString::fromLatin1(dataset)));
                currentWorker = nullptr;
                return;
            }
        }
    }

    // 创建网络管理器
    QNetworkAccessManager nam;
    const auto waitForReply = [this](QNetworkReply* reply, int timeoutMs) {
        QEventLoop replyLoop;
        QTimer timeoutTimer;
        QTimer interruptionTimer;
        timeoutTimer.setSingleShot(true);
        interruptionTimer.setInterval(100);
        QObject::connect(reply, &QNetworkReply::finished, &replyLoop, &QEventLoop::quit);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &replyLoop, &QEventLoop::quit);
        QObject::connect(&interruptionTimer, &QTimer::timeout, &replyLoop, [&replyLoop, reply]() {
            if (QThread::currentThread()->isInterruptionRequested()) {
                reply->abort();
                replyLoop.quit();
            }
        });
        timeoutTimer.start(timeoutMs);
        interruptionTimer.start();
        replyLoop.exec();
        interruptionTimer.stop();
        timeoutTimer.stop();
        return reply->isFinished();
    };

    // 处理每幅干涉图
    for (int idx = 0; idx < image_count; idx++)
    {
        if (QThread::currentThread()->isInterruptionRequested()) {
            emit cancelled();
            currentWorker = nullptr;
            return;
        }

        int progress = 10 + idx * 80 / image_count;
        emit updateProcess(progress, QStringLiteral("处理第%1/%2幅干涉图……").arg(idx + 1).arg(image_count));

        // 读取原始相位获取尺寸
        Mat phase;
        Mat lat_mat, lon_mat;
        double lat_min = 0, lat_max = 0, lon_min = 0, lon_max = 0;
        string source_1_str, source_2_str;
        double carrier_frequency = 0;
        {
            NodeUtils::Hdf5Locker locker;
            ret = FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "phase", phase);
            if (!locker.isLocked() || ret != 0 || phase.empty() ||
                FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lat", lat_mat) != 0 ||
                FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lon", lon_mat) != 0 ||
                FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_1", source_1_str) != 0 ||
                FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_2", source_2_str) != 0) {
                emit errorProcess(QStringLiteral("Failed to read required GACOS input metadata: %1").arg(phase_paths[idx]));
                currentWorker = nullptr;
                return;
            }
            FC.read_double_from_h5(phase_paths[idx].toStdString().c_str(), "carrier_frequency", &carrier_frequency);
        }
        if (!lat_mat.empty()) {
            double minVal, maxVal;
            cv::minMaxLoc(lat_mat, &minVal, &maxVal);
            lat_min = minVal;
            lat_max = maxVal;
        }
        if (!lon_mat.empty()) {
            double minVal, maxVal;
            cv::minMaxLoc(lon_mat, &minVal, &maxVal);
            lon_min = minVal;
            lon_max = maxVal;
        }

        // === 阶段1: 提交 GACOS 作业 ===
        emit updateProcess(progress, QStringLiteral("提交GACOS作业（第%1幅）……").arg(idx + 1));

        QJsonObject submitBody;
        submitBody["api_key"] = apiKey;
        submitBody["email"] = email;
        submitBody["lat_min"] = lat_min;
        submitBody["lat_max"] = lat_max;
        submitBody["lon_min"] = lon_min;
        submitBody["lon_max"] = lon_max;
        submitBody["master_date"] = QString::fromStdString(source_1_str).left(10);
        submitBody["slave_date"] = QString::fromStdString(source_2_str).left(10);
        submitBody["format"] = (dataFormat == 0) ? "geotiff" : "binary";

        QNetworkRequest submitReq;
        submitReq.setUrl(QUrl(GACOS_API_BASE + "/submit"));
        submitReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply* submitReply = nam.post(submitReq, QJsonDocument(submitBody).toJson());

        const bool submitFinished = waitForReply(submitReply, 30000);
        if (QThread::currentThread()->isInterruptionRequested()) {
            submitReply->deleteLater();
            emit cancelled();
            currentWorker = nullptr;
            return;
        }
        if (!submitFinished) {
            submitReply->abort();
            submitReply->deleteLater();
            emit errorProcess(QStringLiteral("GACOS提交请求超时。"));
            currentWorker = nullptr;
            return;
        }

        if (submitReply->error() != QNetworkReply::NoError) {
            const QString error = QString("GACOS提交失败: %1").arg(submitReply->errorString());
            submitReply->deleteLater();
            emit errorProcess(error);
            currentWorker = nullptr;
            return;
        }

        QJsonDocument submitDoc = QJsonDocument::fromJson(submitReply->readAll());
        submitReply->deleteLater();
        QString jobId = submitDoc.object()["job_id"].toString();
        if (jobId.isEmpty()) {
            emit errorProcess(QStringLiteral("GACOS返回空 job_id"));
            currentWorker = nullptr;
            return;
        }

        // === 阶段2: 轮询状态 ===
        emit updateProcess(progress + 5, QStringLiteral("等待GACOS服务器处理（第%1幅）……").arg(idx + 1));

        bool completed = false;
        QString downloadUrl;
        for (int poll = 0; poll < 60; poll++) { // 最多轮询60次，每次30秒
            if (QThread::currentThread()->isInterruptionRequested()) {
                emit cancelled();
                currentWorker = nullptr;
                return;
            }

            for (int waitMs = 0; waitMs < 30000; waitMs += 100) {
                if (QThread::currentThread()->isInterruptionRequested()) {
                    emit cancelled();
                    currentWorker = nullptr;
                    return;
                }
                QThread::msleep(100);
            }

            QNetworkRequest statusReq;
            statusReq.setUrl(QUrl(GACOS_API_BASE + "/status/" + jobId));
            QNetworkReply* statusReply = nam.get(statusReq);

            const bool statusFinished = waitForReply(statusReply, 15000);
            if (QThread::currentThread()->isInterruptionRequested()) {
                statusReply->deleteLater();
                emit cancelled();
                currentWorker = nullptr;
                return;
            }
            if (!statusFinished) {
                statusReply->abort();
                statusReply->deleteLater();
                continue;
            }

            if (statusReply->error() == QNetworkReply::NoError) {
                QJsonDocument statusDoc = QJsonDocument::fromJson(statusReply->readAll());
                QString status = statusDoc.object()["status"].toString();
                if (status == "completed") {
                    downloadUrl = statusDoc.object()["download_url"].toString();
                    completed = true;
                    statusReply->deleteLater();
                    break;
                }
            }
            statusReply->deleteLater();
        }

        if (!completed) {
            emit errorProcess(QString("GACOS作业超时: %1").arg(jobId));
            currentWorker = nullptr;
            return;
        }

        // === 阶段3: 下载并解析 ===
        // 下载结果缓存在事务之外的 <工程>/.gacos_cache/（键 = master/slave 日期 + 范围 + 格式）。
        // 这样解析或写盘失败后重跑时，前面已下到的影像不必再提交一次云端作业、再等一次。
        // 成功路径末尾会删除缓存副本（产物已落地）；只有失败的影像会留下它。
        QString localDownloadPath = gacosDownloadCachePath(
            save_path, QString::fromStdString(source_1_str).left(10), QString::fromStdString(source_2_str).left(10),
            lat_min, lat_max, lon_min, lon_max, dataFormat);

        if (QFileInfo(localDownloadPath).isFile() && QFileInfo(localDownloadPath).size() > 0) {
            InSARLogManager::LogInfo("GacosOnlineServiceWorker", QStringLiteral(
                "复用已下载的 GACOS 结果，跳过下载：%1").arg(localDownloadPath));
        } else {
            emit updateProcess(progress + 10, QStringLiteral("下载GACOS结果（第%1幅）……").arg(idx + 1));

            QDir().mkpath(QFileInfo(localDownloadPath).absolutePath());

            QNetworkRequest downloadReq;
            downloadReq.setUrl(QUrl(downloadUrl));
            QNetworkReply* downloadReply = nam.get(downloadReq);

            const bool downloadFinished = waitForReply(downloadReply, 120000);
            if (QThread::currentThread()->isInterruptionRequested()) {
                downloadReply->deleteLater();
                emit cancelled();
                currentWorker = nullptr;
                return;
            }
            if (!downloadFinished) {
                downloadReply->abort();
                downloadReply->deleteLater();
                emit errorProcess(QStringLiteral("GACOS下载请求超时。"));
                currentWorker = nullptr;
                return;
            }

            if (downloadReply->error() != QNetworkReply::NoError) {
                const QString error = QString("GACOS下载失败: %1").arg(downloadReply->errorString());
                downloadReply->deleteLater();
                emit errorProcess(error);
                currentWorker = nullptr;
                return;
            }

            QFile dlFile(localDownloadPath);
            const QByteArray downloadData = downloadReply->readAll();
            if (!dlFile.open(QIODevice::WriteOnly) || dlFile.write(downloadData) != downloadData.size()) {
                if (dlFile.isOpen()) dlFile.close();
                downloadReply->deleteLater();
                emit errorProcess(QStringLiteral("Unable to save downloaded GACOS response."));
                currentWorker = nullptr;
                return;
            }
            dlFile.close();
            downloadReply->deleteLater();
        }

        // 调用独立算法 DLL 解析下载文件并计算 GACOS 改正相位
        // 获取雷达波长 (米)
        double wavelength = 0.055465763; // 默认值
        if (carrier_frequency > 0) {
            wavelength = 299792458.0 / carrier_frequency;
        }

        // 预分配输出矩阵 (双重保险)
        Mat aps_phase = phase.clone();

        char errBuf[512] = {0};
        // 调用独立算法 DLL 应用 GACOS 大气延迟改正 (包含真实经纬度反投影重采样)
        bool ok = applyGacosCorrection(
            phase, lat_mat, lon_mat,
            localDownloadPath.toLocal8Bit().constData(),
            dataFormat,
            wavelength,
            aps_phase,
            errBuf, 512,
            gacosProgressCallback
        );

        if (!ok) {
            InSARLogManager::LogInfo("GacosOnlineServiceWorker", QStringLiteral(
                "保留已下载的 GACOS 结果以便重跑复用（如需强制重新下载，请删除该文件）：%1").arg(localDownloadPath));
            emit errorProcess(QString("GACOS算法处理失败 (图%1): %2")
                .arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            currentWorker = nullptr;
            return;
        }

        // Write the staged output under one HDF5 lock; metadata copying takes
        // its own lock after this scope to avoid recursive locking.
        {
            NodeUtils::Hdf5Locker locker;
            ret = FC.creat_new_h5(absolute_output_paths[idx].toStdString().c_str());
            if (!locker.isLocked() || ret != 0 ||
                FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "phase", aps_phase) != 0 ||
                FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "mapped_lat", lat_mat) != 0 ||
                FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "mapped_lon", lon_mat) != 0) {
                InSARLogManager::LogInfo("GacosOnlineServiceWorker", QStringLiteral(
                    "保留已下载的 GACOS 结果以便重跑复用（如需强制重新下载，请删除该文件）：%1").arg(localDownloadPath));
                emit errorProcess(QStringLiteral("Failed to write staged GACOS H5 output: %1").arg(absolute_output_paths[idx]));
                currentWorker = nullptr;
                return;
            }
        }
        QString sourcePathMetadataError;
        if (!NodeUtils::writeSourcePathMetadata(absolute_output_paths[idx], source_1_str, source_2_str,
                                                &sourcePathMetadataError) ||
            !NodeUtils::copySourcePathMetadata(phase_paths[idx], absolute_output_paths[idx],
                                               &sourcePathMetadataError)) {
            InSARLogManager::LogInfo("GacosOnlineServiceWorker", QStringLiteral(
                "保留已下载的 GACOS 结果以便重跑复用（如需强制重新下载，请删除该文件）：%1").arg(localDownloadPath));
            emit errorProcess(QStringLiteral("Failed to preserve source-path metadata: %1").arg(sourcePathMetadataError));
            currentWorker = nullptr;
            return;
        }

        // 清理下载文件
        QFile::remove(localDownloadPath);
    }

    emit outputsGenerated(file_name, output_names, absolute_output_paths, save_path, project_name);
    currentWorker = nullptr;
    InSARLogManager::LogInfo("GacosOnlineServiceWorker", "GACOS处理完成");
    emit endProcess();
}
