#include "SLCDerampWorker.h"
#include <FormatConversion.h>
#include <Deflat.h>
#include <Utils.h>
#include <Package.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QElapsedTimer>
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

thread_local SLCDerampWorker* t_currentDerampWorker = nullptr;
thread_local int t_derampLastLoggedProgress = -10;

static bool __stdcall derampProgressCallback(int progress, const char* message)
{
    thread_local QElapsedTimer callbackTimer;
    thread_local bool timerStarted = false;
    if (!timerStarted) {
        callbackTimer.start();
        timerStarted = true;
    }
    if (progress != 0 && progress != 100 && callbackTimer.elapsed() < 100) {
        return true;
    }
    callbackTimer.restart();

    if (!t_currentDerampWorker) {
        return true;
    }
    if (t_currentDerampWorker->thread()->isInterruptionRequested() ||
        t_currentDerampWorker->isStopRequested()) {
        return false;
    }

    const int mappedProgress = 10 + progress * 40 / 100;
    const QString messageText = message ? QString::fromLocal8Bit(message) : QString();
    emit t_currentDerampWorker->updateProcess(mappedProgress,
        QStringLiteral("DEM mapping: %1% (%2)").arg(progress).arg(messageText));

    if (progress == 0 || progress == 100 || progress - t_derampLastLoggedProgress >= 10) {
        InSARLogManager::LogInfo("SLCDerampWorker",
            QString("demMapping progress: %1% (total: %2%) - %3")
                .arg(progress).arg(mappedProgress).arg(messageText));
        t_derampLastLoggedProgress = progress;
    }
    return true;
}

struct DerampThreadLocalGuard {
    explicit DerampThreadLocalGuard(SLCDerampWorker* worker)
    {
        t_currentDerampWorker = worker;
        t_derampLastLoggedProgress = -10;
    }

    ~DerampThreadLocalGuard()
    {
        t_currentDerampWorker = nullptr;
        t_derampLastLoggedProgress = -10;
    }
};

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
    SLC_deramp_with_dem(masterIndex, projectName, savePath, dstNode, inputPaths, QString());
}

void SLCDerampWorker::SLC_deramp_with_dem(
    int masterIndex,
    QString projectName,
    QString savePath,
    QString dstNode,
    QStringList inputPaths,
    QString demPath)
{
    DerampThreadLocalGuard guard(this);
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

    if (demPath.isEmpty()) {
        demPath = QDir::toNativeSeparators(savePath + "/.dem_cache");
    }

    Utils util;
    FormatConversion conversion;
    Deflat flat;
    {
        NodeUtils::Hdf5Locker locker;
        for (const string& outputPath : derampImages) {
            conversion.creat_new_h5(outputPath.c_str());
        }
    }

    emit updateProcess(10, QStringLiteral("Preparing DEM mapping..."));
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

    wavelength = VEL_C / wavelength;
    nearRangeTime = 2.0 * nearRangeTime / VEL_C;
    conversion.utc2gps(startTime.c_str(), &start);
    conversion.utc2gps(endTime.c_str(), &end);
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        emit cancelled();
        return;
    }

    util.computeImageGeoBoundry(latCoef, lonCoef, sceneHeight, sceneWidth, offsetRow, offsetCol,
        &lonMax, &latMax, &lonMin, &latMin);
    util.getSRTMDEM(demPath.toStdString().c_str(), dem, &lonUpperLeft, &latUpperLeft, lonMin, lonMax, latMin, latMax);
    ret = flat.demMapping(dem, mappedDem, mappedLat, mappedLon, lonUpperLeft, latUpperLeft, offsetRow, offsetCol,
        sceneHeight, sceneWidth, prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec,
        20, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, derampProgressCallback);
    if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            emit cancelled();
        } else {
            emit errorProcess(QStringLiteral("DEM mapping failed."));
        }
        return;
    }

    {
        NodeUtils::Hdf5Locker locker;
        const QString masterOutputPath = QString::fromStdString(derampImages.at(masterIndex - 1));
        NodeUtils::writeMatToH5(masterOutputPath, "mapped_lat", mappedLat);
        NodeUtils::writeMatToH5(masterOutputPath, "mapped_lon", mappedLon);
    }

    QStringList resultH5Paths;
    for (int i = 0; i < inputPaths.size(); ++i) {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            emit cancelled();
            return;
        }

        {
            NodeUtils::Hdf5Locker locker;
            ret = flat.SLC_deramp(slc, mappedDem, mappedLat, mappedLon, sourceImages.at(i).c_str());
            if (ret < 0) {
                emit errorProcess(QStringLiteral("SLC deramp failed."));
                return;
            }
            conversion.write_slc_to_h5(derampImages.at(i).c_str(), slc);
            conversion.Copy_para_from_h5_2_h5(sourceImages.at(i).c_str(), derampImages.at(i).c_str());

            const QString sourcePath = inputPaths.at(i);
            const QString outputPath = QString::fromStdString(derampImages.at(i));
            NodeUtils::readScalarFromH5(sourcePath, "offset_row", offsetRow);
            NodeUtils::writeScalarToH5(outputPath, "offset_row", offsetRow);
            NodeUtils::readScalarFromH5(sourcePath, "offset_col", offsetCol);
            NodeUtils::writeScalarToH5(outputPath, "offset_col", offsetCol);
            NodeUtils::writeScalarToH5(outputPath, "range_len", sceneWidth);
            NodeUtils::writeScalarToH5(outputPath, "azimuth_len", sceneHeight);
        }

        const QFileInfo outputInfo(QString::fromStdString(derampImages.at(i)));
        const QString outputPath = outputInfo.absoluteFilePath();
        const QString previewPath = outputInfo.absolutePath() + "/" + outputInfo.baseName() + ".jpg";
        {
            NodeUtils::Hdf5Locker locker;
            NodeUtils::generateJpgPreviewFromH5(outputPath, previewPath, "complex");
        }
        resultH5Paths.append(outputPath);
        emit updateProcess(50 + 40 * (i + 1) / inputPaths.size(), QStringLiteral("Processing SLC images..."));
    }

    emit sendResults(dstNode, resultH5Paths, originNames, savePath, projectName);
    InSARLogManager::LogInfo("SLCDerampWorker",
        QString("SLC_deramp completed. Total images: %1").arg(inputPaths.size()));
    emit endProcess();
}
