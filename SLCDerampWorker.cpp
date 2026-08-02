#include "SLCDerampWorker.h"
#include <FormatConversion.h>
#include <Deflat.h>
#include <Utils.h>
#include <Package.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTemporaryDir>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "Deflat_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Filter_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "Deflat.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Filter.lib")
#endif

using namespace cv;
using namespace std;

namespace {

constexpr int kDerampProgressSlotCount = 8;

struct DerampProgressContext
{
    explicit DerampProgressContext(SLCDerampWorker* sourceWorker)
        : worker(sourceWorker)
    {
    }

    bool isCancellationRequested() const
    {
        return !worker || worker->thread()->isInterruptionRequested() || worker->isStopRequested();
    }

    void setStage(int start, int end, const QString& name)
    {
        std::lock_guard<std::mutex> guard(progressMutex);
        stageStart = qBound(0, start, 100);
        stageEnd = qBound(stageStart, end, 100);
        stageName = name;
    }

    bool report(int localProgress, const char* message, bool force = false)
    {
        // Cancellation must never be delayed by UI progress throttling.
        if (isCancellationRequested()) {
            return false;
        }

        const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        QString status;
        int mappedProgress = 0;
        {
            std::lock_guard<std::mutex> guard(progressMutex);
            const int clamped = qBound(0, localProgress, 100);
            mappedProgress = stageStart + (stageEnd - stageStart) * clamped / 100;
            if (mappedProgress <= lastObservedProgress) {
                return true;
            }
            lastObservedProgress = mappedProgress;
            if (!force && mappedProgress < stageEnd && now - lastUiUpdateMs < 100) {
                return true;
            }
            lastUiUpdateMs = now;
            const QString detail = message ? QString::fromLocal8Bit(message) : QString();
            status = detail.isEmpty() ? stageName : QStringLiteral("%1：%2").arg(stageName, detail);
        }

        emit worker->updateProcess(mappedProgress, status);
        return true;
    }

    bool completeStage(const QString& completionMessage)
    {
        return report(100, completionMessage.toLocal8Bit().constData(), true);
    }

    SLCDerampWorker* worker = nullptr;
    std::mutex progressMutex;
    int stageStart = 0;
    int stageEnd = 0;
    int lastObservedProgress = 0;
    qint64 lastUiUpdateMs = 0;
    QString stageName;
};

std::array<std::shared_ptr<DerampProgressContext>, kDerampProgressSlotCount> g_derampProgressSlots;
std::mutex g_derampProgressSlotsMutex;
std::condition_variable g_derampProgressSlotAvailable;

static bool dispatchDerampProgress(int slotIndex, int progress, const char* message)
{
    std::shared_ptr<DerampProgressContext> context;
    {
        std::lock_guard<std::mutex> guard(g_derampProgressSlotsMutex);
        context = g_derampProgressSlots[slotIndex];
    }
    return !context || context->report(progress, message);
}

template <int SlotIndex>
static bool __stdcall derampProgressCallbackSlot(int progress, const char* message)
{
    return dispatchDerampProgress(SlotIndex, progress, message);
}

const std::array<DeflatProgressCallback, kDerampProgressSlotCount> g_derampProgressCallbacks = {
    &derampProgressCallbackSlot<0>, &derampProgressCallbackSlot<1>,
    &derampProgressCallbackSlot<2>, &derampProgressCallbackSlot<3>,
    &derampProgressCallbackSlot<4>, &derampProgressCallbackSlot<5>,
    &derampProgressCallbackSlot<6>, &derampProgressCallbackSlot<7>
};

class ScopedDerampProgressCallback
{
public:
    explicit ScopedDerampProgressCallback(SLCDerampWorker* worker)
        : m_context(std::make_shared<DerampProgressContext>(worker))
    {
    }

    ~ScopedDerampProgressCallback()
    {
        release();
    }

    bool acquire()
    {
        std::unique_lock<std::mutex> lock(g_derampProgressSlotsMutex);
        while (m_slotIndex < 0) {
            if (m_context->isCancellationRequested()) {
                return false;
            }
            for (int i = 0; i < kDerampProgressSlotCount; ++i) {
                if (!g_derampProgressSlots[i]) {
                    g_derampProgressSlots[i] = m_context;
                    m_slotIndex = i;
                    return true;
                }
            }
            g_derampProgressSlotAvailable.wait_for(lock, std::chrono::milliseconds(100));
        }
        return true;
    }

    DeflatProgressCallback callback() const
    {
        return m_slotIndex >= 0 ? g_derampProgressCallbacks[m_slotIndex] : nullptr;
    }

    void setStage(int start, int end, const QString& name)
    {
        m_context->setStage(start, end, name);
    }

    bool completeStage(const QString& completionMessage)
    {
        return m_context->completeStage(completionMessage);
    }

private:
    void release()
    {
        if (m_slotIndex < 0) {
            return;
        }
        std::lock_guard<std::mutex> guard(g_derampProgressSlotsMutex);
        g_derampProgressSlots[m_slotIndex].reset();
        m_slotIndex = -1;
        g_derampProgressSlotAvailable.notify_one();
    }

    std::shared_ptr<DerampProgressContext> m_context;
    int m_slotIndex = -1;
};

} // namespace

SLCDerampWorker::SLCDerampWorker(QObject* parent)
    : BaseWorker(parent)
{
}

SLCDerampWorker::~SLCDerampWorker()
{
}

void SLCDerampWorker::SLC_deramp(
    int masterIndex,
    QString projectName,
    QString savePath,
    QString dstNode,
    QStringList inputPaths)
{
    SLC_deramp_with_dem(masterIndex, projectName, savePath, dstNode, inputPaths, QString(), true, true);
}

void SLCDerampWorker::SLC_deramp_with_dem(
    int masterIndex,
    QString projectName,
    QString savePath,
    QString dstNode,
    QStringList inputPaths,
    QString demPath,
    bool isDeflat,
    bool isTopoRemoval)
{
    InSARLogManager::LogInfo("SLCDerampWorker",
        QString("SLC_deramp task started. Project: %1, destination: %2").arg(projectName).arg(dstNode));

    if (masterIndex < 1 || savePath.isEmpty() || dstNode.isEmpty() || inputPaths.isEmpty()) {
        emit errorProcess(QStringLiteral("Invalid parameters or empty input paths."));
        return;
    }
    for (const QString& path : inputPaths) {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            emit errorProcess(QStringLiteral("Input image file does not exist."));
            return;
        }
    }
    if (masterIndex > inputPaths.size()) {
        emit errorProcess(QStringLiteral("Master image index is out of range."));
        return;
    }

    const auto cancelIfRequested = [this]() {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            emit cancelled();
            return true;
        }
        return false;
    };

    QDir projectDir(savePath);
    if (!projectDir.exists(dstNode) && !projectDir.mkdir(dstNode)) {
        emit errorProcess(QStringLiteral("Unable to create output directory."));
        return;
    }

    vector<string> sourceImages;
    vector<string> derampImages;
    QStringList originNames;
    sourceImages.reserve(inputPaths.size());
    derampImages.reserve(inputPaths.size());
    for (const QString& inputPath : inputPaths) {
        const QFileInfo fileInfo(inputPath);
        const QString originName = fileInfo.baseName();
        sourceImages.push_back(inputPath.toStdString());
        derampImages.push_back(QString("%1/%2/%3_deramp.h5").arg(savePath, dstNode, originName).toStdString());
        originNames.append(originName);
    }

    if (!isDeflat) {
        QStringList resultH5Paths;
        for (int i = 0; i < inputPaths.size(); ++i) {
            if (cancelIfRequested()) {
                return;
            }

            const int copyStart = 10 + 80 * i / inputPaths.size();
            const int copyEnd = 10 + 80 * (i + 1) / inputPaths.size();
            emit updateProcess(copyStart,
                               QStringLiteral("正在复制 SLC：%1/%2").arg(i + 1).arg(inputPaths.size()));

            const QString outputPath = QString::fromStdString(derampImages.at(i));
            if (QFile::exists(outputPath) && !QFile::remove(outputPath)) {
                emit errorProcess(QStringLiteral("Unable to replace SLC output."));
                return;
            }
            if (cancelIfRequested()) {
                return;
            }
            if (!QFile::copy(inputPaths.at(i), outputPath)) {
                emit errorProcess(QStringLiteral("Failed to copy input SLC."));
                return;
            }
            if (cancelIfRequested()) {
                return;
            }

            resultH5Paths.append(outputPath);
            emit updateProcess(copyEnd,
                               QStringLiteral("已复制 SLC：%1/%2").arg(i + 1).arg(inputPaths.size()));
        }

        emit sendResults(dstNode, resultH5Paths, originNames, savePath, projectName);
        InSARLogManager::LogInfo("SLCDerampWorker",
            QString("SLC_deramp completed without phase removal. Total images: %1").arg(inputPaths.size()));
        emit endProcess();
        return;
    }

    QTemporaryDir temporaryDemDirectory;
    if (demPath.isEmpty()) {
        if (!temporaryDemDirectory.isValid()) {
            emit errorProcess(QStringLiteral("Unable to create temporary DEM cache directory."));
            return;
        }
        demPath = QDir::toNativeSeparators(temporaryDemDirectory.path());
    }

    Utils util;
    FormatConversion conversion;
    Deflat flat;
    emit updateProcess(5, QStringLiteral("正在创建 SLC 输出文件..."));
    for (const string& outputPath : derampImages) {
        if (cancelIfRequested()) {
            return;
        }
        {
            NodeUtils::Hdf5Locker locker;
            conversion.creat_new_h5(outputPath.c_str());
        }
        if (cancelIfRequested()) {
            return;
        }
    }

    emit updateProcess(10, QStringLiteral("正在读取主影像元数据..."));
    double lonMax = 0.0;
    double lonMin = 0.0;
    double latMax = 0.0;
    double latMin = 0.0;
    double lonUpperLeft = 0.0;
    double latUpperLeft = 0.0;
    double rangeSpacing = 0.0;
    double nearRangeTime = 0.0;
    double wavelength = 0.0;
    double prf = 0.0;
    double start = 0.0;
    double end = 0.0;
    int sceneHeight = 0;
    int sceneWidth = 0;
    int offsetRow = 0;
    int offsetCol = 0;
    Mat lonCoef;
    Mat latCoef;
    Mat dem;
    Mat mappedDem;
    Mat mappedLat;
    Mat mappedLon;
    Mat statevec;
    ComplexMat slc;
    string startTime;
    string endTime;
    int ret = 0;

    const QString masterH5Path = inputPaths.at(masterIndex - 1);
    if (cancelIfRequested()) {
        return;
    }
    {
        NodeUtils::Hdf5Locker locker;
        if (!NodeUtils::readScalarFromH5(masterH5Path, "range_len", sceneWidth) ||
            !NodeUtils::readScalarFromH5(masterH5Path, "azimuth_len", sceneHeight) ||
            !NodeUtils::readScalarFromH5(masterH5Path, "offset_row", offsetRow) ||
            !NodeUtils::readScalarFromH5(masterH5Path, "offset_col", offsetCol) ||
            !NodeUtils::readMatFromH5(masterH5Path, "lon_coefficient", lonCoef) ||
            !NodeUtils::readMatFromH5(masterH5Path, "lat_coefficient", latCoef) ||
            !NodeUtils::readScalarFromH5(masterH5Path, "prf", prf) ||
            !NodeUtils::readScalarFromH5(masterH5Path, "carrier_frequency", wavelength) ||
            !NodeUtils::readScalarFromH5(masterH5Path, "range_spacing", rangeSpacing) ||
            !NodeUtils::readScalarFromH5(masterH5Path, "slant_range_first_pixel", nearRangeTime) ||
            !NodeUtils::readStringFromH5(masterH5Path, "acquisition_start_time", startTime) ||
            !NodeUtils::readStringFromH5(masterH5Path, "acquisition_stop_time", endTime) ||
            !NodeUtils::readMatFromH5(masterH5Path, "state_vec", statevec)) {
            emit errorProcess(QStringLiteral("Unable to read master image metadata."));
            return;
        }
    }
    if (cancelIfRequested()) {
        return;
    }

    wavelength = VEL_C / wavelength;
    nearRangeTime = 2.0 * nearRangeTime / VEL_C;
    conversion.utc2gps(startTime.c_str(), &start);
    conversion.utc2gps(endTime.c_str(), &end);
    if (cancelIfRequested()) {
        return;
    }

    emit updateProcess(10, QStringLiteral("正在加载 DEM..."));
    util.computeImageGeoBoundry(latCoef, lonCoef, sceneHeight, sceneWidth, offsetRow, offsetCol,
        &lonMax, &latMax, &lonMin, &latMin);
    util.getSRTMDEM(demPath.toStdString().c_str(), dem, &lonUpperLeft, &latUpperLeft, lonMin, lonMax, latMin, latMax);
    if (!isTopoRemoval) {
        dem = Mat::zeros(dem.size(), dem.type());
    }

    if (cancelIfRequested()) {
        return;
    }

    emit updateProcess(10, QStringLiteral("正在等待 DEM 映射进度槽位..."));
    ScopedDerampProgressCallback progressCallback(this);
    if (!progressCallback.acquire()) {
        emit cancelled();
        return;
    }
    progressCallback.setStage(10, 46, QStringLiteral("正在映射 DEM"));
    ret = flat.demMapping(dem, mappedDem, mappedLat, mappedLon, lonUpperLeft, latUpperLeft, offsetRow, offsetCol,
        sceneHeight, sceneWidth, prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec,
        20, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, progressCallback.callback());
    if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            emit cancelled();
        } else {
            emit errorProcess(QStringLiteral("DEM mapping failed."));
        }
        return;
    }
    if (!progressCallback.completeStage(QStringLiteral("DEM 映射完成"))) {
        emit cancelled();
        return;
    }

    progressCallback.setStage(46, 48, QStringLiteral("正在写入主景纬度坐标"));
    emit updateProcess(46, QStringLiteral("正在写入主景纬度坐标..."));
    if (cancelIfRequested()) {
        return;
    }
    {
        NodeUtils::Hdf5Locker locker;
        const QString masterOutputPath = QString::fromStdString(derampImages.at(masterIndex - 1));
        NodeUtils::writeMatToH5(masterOutputPath, "mapped_lat", mappedLat);
    }
    if (cancelIfRequested()) {
        return;
    }
    if (!progressCallback.completeStage(QStringLiteral("主景纬度坐标已写入"))) {
        emit cancelled();
        return;
    }

    progressCallback.setStage(48, 50, QStringLiteral("正在写入主景经度坐标"));
    emit updateProcess(48, QStringLiteral("正在写入主景经度坐标..."));
    if (cancelIfRequested()) {
        return;
    }
    {
        NodeUtils::Hdf5Locker locker;
        const QString masterOutputPath = QString::fromStdString(derampImages.at(masterIndex - 1));
        NodeUtils::writeMatToH5(masterOutputPath, "mapped_lon", mappedLon);
    }
    if (cancelIfRequested()) {
        return;
    }
    if (!progressCallback.completeStage(QStringLiteral("主景经度坐标已写入"))) {
        emit cancelled();
        return;
    }

    QStringList resultH5Paths;
    for (int i = 0; i < inputPaths.size(); ++i) {
        if (cancelIfRequested()) {
            return;
        }

        const int imageStart = 50 + 40 * i / inputPaths.size();
        const int imageEnd = 50 + 40 * (i + 1) / inputPaths.size();
        const int derampEnd = imageStart + 3 * (imageEnd - imageStart) / 4;
        progressCallback.setStage(imageStart, derampEnd,
                                  QStringLiteral("正在去除第 %1/%2 景 SLC 相位")
                                      .arg(i + 1).arg(inputPaths.size()));
        emit updateProcess(imageStart,
                           QStringLiteral("正在读取并去除第 %1/%2 景 SLC 相位...")
                               .arg(i + 1).arg(inputPaths.size()));

        {
            NodeUtils::Hdf5Locker locker;
            ret = flat.SLC_deramp(slc, mappedDem, mappedLat, mappedLon, sourceImages.at(i).c_str(),
                                  TR_MODE_SINGLE_TX_SINGLE_RX, progressCallback.callback());
            if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                emit cancelled();
                return;
            }
            if (ret < 0) {
                emit errorProcess(QStringLiteral("SLC deramp failed."));
                return;
            }
            if (!progressCallback.completeStage(QStringLiteral("正在写入第 %1/%2 景 SLC...")
                                                .arg(i + 1).arg(inputPaths.size()))) {
                emit cancelled();
                return;
            }
            progressCallback.setStage(derampEnd, imageEnd,
                                      QStringLiteral("正在写入第 %1/%2 景 SLC")
                                          .arg(i + 1).arg(inputPaths.size()));
            if (cancelIfRequested()) {
                return;
            }
            if (conversion.write_slc_to_h5(derampImages.at(i).c_str(), slc) < 0) {
                emit errorProcess(QStringLiteral("Failed to write deramped SLC output."));
                return;
            }
            if (cancelIfRequested()) {
                return;
            }
            conversion.Copy_para_from_h5_2_h5(sourceImages.at(i).c_str(), derampImages.at(i).c_str());
            if (cancelIfRequested()) {
                return;
            }

            const QString sourcePath = inputPaths.at(i);
            const QString outputPath = QString::fromStdString(derampImages.at(i));
            NodeUtils::readScalarFromH5(sourcePath, "offset_row", offsetRow);
            if (cancelIfRequested()) {
                return;
            }
            NodeUtils::writeScalarToH5(outputPath, "offset_row", offsetRow);
            if (cancelIfRequested()) {
                return;
            }
            NodeUtils::readScalarFromH5(sourcePath, "offset_col", offsetCol);
            if (cancelIfRequested()) {
                return;
            }
            NodeUtils::writeScalarToH5(outputPath, "offset_col", offsetCol);
            if (cancelIfRequested()) {
                return;
            }
            NodeUtils::writeScalarToH5(outputPath, "range_len", sceneWidth);
            if (cancelIfRequested()) {
                return;
            }
            NodeUtils::writeScalarToH5(outputPath, "azimuth_len", sceneHeight);
        }
        if (cancelIfRequested()) {
            return;
        }

        resultH5Paths.append(QString::fromStdString(derampImages.at(i)));
        if (!progressCallback.completeStage(QStringLiteral("第 %1/%2 景 SLC 已完成")
                                            .arg(i + 1).arg(inputPaths.size()))) {
            emit cancelled();
            return;
        }
    }

    emit sendResults(dstNode, resultH5Paths, originNames, savePath, projectName);
    InSARLogManager::LogInfo("SLCDerampWorker",
        QString("SLC_deramp completed. Total images: %1").arg(inputPaths.size()));
    emit endProcess();
}
