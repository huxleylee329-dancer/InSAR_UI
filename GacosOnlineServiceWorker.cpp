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
        return;
    }

    // 创建输出目录
    QDir dir(save_path);
    QString absolute_path = save_path + "/" + file_name;
    if (dir.exists(file_name)) dir.remove(file_name);
    dir.mkdir(file_name);

    // 获取输入干涉图信息
    QStringList phase_names;
    QStringList phase_paths;
    QStringList output_names;
    QStringList absolute_output_paths;

    emit updateProcess(5, QStringLiteral("读取输入数据……"));

    for (const QString& inputPath : inputPaths) {
        if (inputPath.isEmpty() || !QFileInfo::exists(inputPath)) {
            emit errorProcess(QStringLiteral("Input phase file does not exist."));
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
        return;
    }

    FormatConversion FC;
    int ret = 0;

    std::vector<bool> process_ok(image_count, false);

    // 创建网络管理器
    QNetworkAccessManager nam;

    // 处理每幅干涉图
    for (int idx = 0; idx < image_count; idx++)
    {
        if (QThread::currentThread()->isInterruptionRequested()) {
            emit cancelled();
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
            if (ret < 0) continue;
            FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lat", lat_mat);
            FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lon", lon_mat);
            FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_1", source_1_str);
            FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_2", source_2_str);
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

        // 同步等待提交响应
        QEventLoop submitLoop;
        connect(submitReply, &QNetworkReply::finished, &submitLoop, &QEventLoop::quit);
        QTimer::singleShot(30000, &submitLoop, &QEventLoop::quit); // 30秒超时
        submitLoop.exec();

        if (submitReply->error() != QNetworkReply::NoError) {
            InSARLogManager::LogError("GacosOnlineServiceWorker",
                QString("GACOS提交失败: %1").arg(submitReply->errorString()));
            submitReply->deleteLater();
            continue;
        }

        QJsonDocument submitDoc = QJsonDocument::fromJson(submitReply->readAll());
        submitReply->deleteLater();
        QString jobId = submitDoc.object()["job_id"].toString();
        if (jobId.isEmpty()) {
            InSARLogManager::LogError("GacosOnlineServiceWorker", "GACOS返回空 job_id");
            continue;
        }

        // === 阶段2: 轮询状态 ===
        emit updateProcess(progress + 5, QStringLiteral("等待GACOS服务器处理（第%1幅）……").arg(idx + 1));

        bool completed = false;
        QString downloadUrl;
        for (int poll = 0; poll < 60; poll++) { // 最多轮询60次，每次30秒
            if (QThread::currentThread()->isInterruptionRequested()) {
                emit cancelled();
                return;
            }

            QThread::sleep(30);

            QNetworkRequest statusReq;
            statusReq.setUrl(QUrl(GACOS_API_BASE + "/status/" + jobId));
            QNetworkReply* statusReply = nam.get(statusReq);

            QEventLoop statusLoop;
            connect(statusReply, &QNetworkReply::finished, &statusLoop, &QEventLoop::quit);
            QTimer::singleShot(15000, &statusLoop, &QEventLoop::quit);
            statusLoop.exec();

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
            InSARLogManager::LogError("GacosOnlineServiceWorker",
                QString("GACOS作业超时: %1").arg(jobId));
            continue;
        }

        // === 阶段3: 下载并解析 ===
        emit updateProcess(progress + 10, QStringLiteral("下载GACOS结果（第%1幅）……").arg(idx + 1));

        QString localDownloadPath = absolute_path + "/gacos_download_" + QString::number(idx);
        if (dataFormat == 0) localDownloadPath += ".tif";
        else localDownloadPath += ".ztd";

        QNetworkRequest downloadReq;
        downloadReq.setUrl(QUrl(downloadUrl));
        QNetworkReply* downloadReply = nam.get(downloadReq);

        QEventLoop downloadLoop;
        connect(downloadReply, &QNetworkReply::finished, &downloadLoop, &QEventLoop::quit);
        QTimer::singleShot(120000, &downloadLoop, &QEventLoop::quit); // 2分钟下载超时
        downloadLoop.exec();

        if (downloadReply->error() != QNetworkReply::NoError) {
            InSARLogManager::LogError("GacosOnlineServiceWorker",
                QString("GACOS下载失败: %1").arg(downloadReply->errorString()));
            downloadReply->deleteLater();
            continue;
        }

        QFile dlFile(localDownloadPath);
        if (dlFile.open(QIODevice::WriteOnly)) {
            dlFile.write(downloadReply->readAll());
            dlFile.close();
        }
        downloadReply->deleteLater();

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
            InSARLogManager::LogError("GacosOnlineServiceWorker", 
                QString("GACOS算法处理失败 (图%1): %2").arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            QFile::remove(localDownloadPath);
            continue;
        }

        // Write the output and its metadata while holding the HDF5 lock.
        {
            NodeUtils::Hdf5Locker locker;
            ret = FC.creat_new_h5(absolute_output_paths[idx].toStdString().c_str());
            if (ret < 0) {
                QFile::remove(localDownloadPath);
                continue;
            }

            string tmp_str;
            Mat tmp;
            FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_1", tmp_str);
            FC.write_str_to_h5(absolute_output_paths[idx].toStdString().c_str(), "source_1", tmp_str.c_str());
            FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_2", tmp_str);
            FC.write_str_to_h5(absolute_output_paths[idx].toStdString().c_str(), "source_2", tmp_str.c_str());
            QString sourcePathMetadataError;
            if (!NodeUtils::copySourcePathMetadata(phase_paths[idx], absolute_output_paths[idx], &sourcePathMetadataError)) {
                emit errorProcess(QStringLiteral("Failed to preserve source-path metadata: %1").arg(sourcePathMetadataError));
                currentWorker = nullptr;
                return;
            }
            FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "phase", aps_phase);

            if (0 == FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lat", tmp))
                FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "mapped_lat", tmp);
            if (0 == FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lon", tmp))
                FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "mapped_lon", tmp);
        }

        process_ok[idx] = true;

        // 清理下载文件
        QFile::remove(localDownloadPath);
    }

    QStringList generatedNames;
    QStringList generatedPaths;
    for (int i = 0; i < image_count; ++i) {
        if (process_ok[i]) {
            generatedNames.append(output_names[i]);
            generatedPaths.append(absolute_output_paths[i]);
        }
    }
    if (generatedPaths.isEmpty()) {
        emit errorProcess(QStringLiteral("GACOS did not generate any output files."));
        currentWorker = nullptr;
        return;
    }

    emit outputsGenerated(file_name, generatedNames, generatedPaths, save_path, project_name);
    currentWorker = nullptr;
    InSARLogManager::LogInfo("GacosOnlineServiceWorker", "GACOS处理完成");
    emit endProcess();
}
