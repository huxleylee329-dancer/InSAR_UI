#include "GeocodingWorker.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "Package.h"
#include <FormatConversion.h>
#include <Utils.h>
#include <Deflat.h>
#include <Filter.h>
#include <Registration.h>
#include <Unwrap.h>
#include <Dem.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QThread>
#include <QElapsedTimer>
#include "InSARLogManager.h"

using namespace cv;

thread_local GeocodingWorker* t_currentGeocodingWorker = nullptr;
thread_local int t_geocodingLastLoggedProgress = -10;

static bool resolveSourceH5Path(const std::string& rawSourcePath,
                                const QString& projectRoot,
                                QString& resolvedPath,
                                QString& error)
{
    if (rawSourcePath.empty()) {
        error = QStringLiteral("source_1 is empty.");
        return false;
    }

    PathResolver::Resolution resolution;
    PathResolver::Error pathError = PathResolver::Error::None;
    if (!PathResolver::resolve(rawSourcePath,
                               projectRoot.toUtf8().toStdString(),
                               resolution,
                               &pathError)) {
        error = QStringLiteral("Unable to resolve source_1: %1")
                    .arg(QString::fromLatin1(PathResolver::errorMessage(pathError)));
        return false;
    }

    resolvedPath = QString::fromUtf8(resolution.utf8.data(),
                                     static_cast<int>(resolution.utf8.size()));
    if (!QFileInfo(resolvedPath).isFile()) {
        error = QStringLiteral("Resolved source_1 H5 does not exist: %1").arg(resolvedPath);
        return false;
    }
    return true;
}

static bool __stdcall geocodingProgressCallback(int progress, const char* message)
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

    if (t_currentGeocodingWorker)
    {
        if (t_currentGeocodingWorker->thread()->isInterruptionRequested() || t_currentGeocodingWorker->isStopRequested())
        {
            return false;
        }

        int start_prog = 2;
        int end_prog = 90;
        int mapped_prog = start_prog + progress * (end_prog - start_prog) / 100;

        QString msgStr = QString::fromLocal8Bit(message);
        emit t_currentGeocodingWorker->updateProcess(mapped_prog, QStringLiteral("正在地理编码：%1% (%2)")
            .arg(progress).arg(msgStr));

        if (progress == 0 || progress == 100 || (progress - t_geocodingLastLoggedProgress) >= 10)
        {
            InSARLogManager::LogInfo("GeocodingWorker", QString("demMapping progress: %1% (Total: %2%) - %3")
                .arg(progress).arg(mapped_prog).arg(msgStr));
            t_geocodingLastLoggedProgress = progress;
        }
    }
    return true;
}

struct GeocodingThreadLocalGuard {
    GeocodingThreadLocalGuard(GeocodingWorker* worker) {
        t_currentGeocodingWorker = worker;
        t_geocodingLastLoggedProgress = -10;
    }
    ~GeocodingThreadLocalGuard() {
        t_currentGeocodingWorker = nullptr;
        t_geocodingLastLoggedProgress = -10;
    }
};

GeocodingWorker::GeocodingWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<GeocodingFileResult>("GeocodingFileResult");
}

GeocodingWorker::~GeocodingWorker()
{
}

void GeocodingWorker::Geocoding(
    int type,
    int multi_rg,
    int multi_az,
    QString savePath,
    QStringList inputPaths,
    QString productLevel,
    int masterIndex,
    QString dstNode
)
{
    GeocodingWithDem(type, multi_rg, multi_az, savePath, inputPaths, productLevel, masterIndex, dstNode, QString());
}

void GeocodingWorker::GeocodingWithDem(
    int type,
    int multi_rg,
    int multi_az,
    QString savePath,
    QStringList inputPaths,
    QString productLevel,
    int masterIndex,
    QString dstNode,
    QString demPath
)
{
    GeocodingThreadLocalGuard guard(this);
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        Q_EMIT cancelled();
        return;
    }

    const QString save_path = savePath;

    QDir dir(save_path);
    if (!dir.exists(dstNode))
        dir.mkdir(dstNode);

    if (demPath.isEmpty()) {
        emit errorProcess(QStringLiteral("Geocoding requires an auxiliary terrain DEM."));
        return;
    }

    std::vector<std::string> input_files;
    std::vector<std::string> output_files;
    const QString product_level = productLevel;
    for (const QString& inputPath : inputPaths) {
        QFileInfo fileinfo(inputPath);
        const QString originName = fileinfo.baseName();
        input_files.push_back(inputPath.toStdString());
        output_files.push_back(QString("%1/%2/%3_geocoded.h5").arg(save_path).arg(dstNode)
            .arg(originName).toStdString());
    }

    if (type == 1 && product_level.isEmpty()) {
        emit errorProcess(QStringLiteral("No product level was provided for geocoding."));
        return;
    }
    if (type == 2 && (masterIndex < 0 || masterIndex >= static_cast<int>(input_files.size()))) {
        emit errorProcess(QStringLiteral("The geocoding master image index is out of range."));
        return;
    }

    if (input_files.empty()) {
        emit errorProcess(QStringLiteral("未在工程XML中找到输入文件！"));
        return;
    }

    emit updateProcess(2, QStringLiteral("正在地理编码……"));
    FormatConversion conversion; Utils util;
    QString geocode_Rank_level;
    int ret;
    //干涉产品地理编码
    if (type == 1)
    {
        std::string source_file;
        Mat mapped_lat, mapped_lon, phase, mapped_phase;
        double lonMax = 0, lonMin = 0, latMax = 0, latMin = 0, lon_upperleft = 0, lat_upperleft = 0, rangeSpacing = 0,
            nearRangeTime = 0, wavelength = 0, prf = 0, start = 0, end = 0;
        int sceneHeight = 0, sceneWidth = 0, offset_row = 0, offset_col = 0, multilook_rg = 1, multilook_az = 1;
        Mat lon_coef, lat_coef, dem, mappedDem, statevec;
        std::string start_time, end_time;
        
        QString inputError;
        const QString inputH5 = QString::fromStdString(input_files[0]);
        const bool hasMappedCoordinates =
            NodeUtils::readMatFromH5(inputH5, "mapped_lon", mapped_lon, -1, &inputError) &&
            NodeUtils::readMatFromH5(inputH5, "mapped_lat", mapped_lat, -1, &inputError);
        if (hasMappedCoordinates)
        {
            if (mapped_lon.empty() || mapped_lat.empty() || mapped_lon.size() != mapped_lat.size()) {
                emit errorProcess(QStringLiteral("Geocoding input mapped_lon/mapped_lat grids are empty or have different dimensions: %1")
                                      .arg(inputH5));
                return;
            }
            sceneHeight = mapped_lon.rows;
            sceneWidth = mapped_lon.cols;
        }
        if (!hasMappedCoordinates)
        {
            if (!NodeUtils::readStringFromH5(inputH5, "source_1", source_file, &inputError)) {
                emit errorProcess(QStringLiteral("Unable to read source_1 from geocoding input %1: %2")
                                      .arg(inputH5, inputError));
                return;
            }

            QString masterH5;
            if (!resolveSourceH5Path(source_file, save_path, masterH5, inputError)) {
                emit errorProcess(QStringLiteral("Unable to resolve geocoding source for %1: %2")
                                      .arg(inputH5, inputError));
                return;
            }
            if (!NodeUtils::readScalarFromH5(inputH5, "multilook_az", multilook_az, &inputError) ||
                !NodeUtils::readScalarFromH5(inputH5, "multilook_rg", multilook_rg, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "range_len", sceneWidth, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "azimuth_len", sceneHeight, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "offset_row", offset_row, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "offset_col", offset_col, &inputError) ||
                !NodeUtils::readMatFromH5(masterH5, "lon_coefficient", lon_coef, -1, &inputError) ||
                !NodeUtils::readMatFromH5(masterH5, "lat_coefficient", lat_coef, -1, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "prf", prf, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "carrier_frequency", wavelength, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "range_spacing", rangeSpacing, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "slant_range_first_pixel", nearRangeTime, &inputError) ||
                !NodeUtils::readStringFromH5(masterH5, "acquisition_start_time", start_time, &inputError) ||
                !NodeUtils::readStringFromH5(masterH5, "acquisition_stop_time", end_time, &inputError) ||
                !NodeUtils::readMatFromH5(masterH5, "state_vec", statevec, -1, &inputError)) {
                emit errorProcess(QStringLiteral("Unable to read geocoding source metadata from %1: %2")
                                      .arg(masterH5, inputError));
                return;
            }

            Deflat flat;
            wavelength = VEL_C / wavelength;
            nearRangeTime = 2.0 * nearRangeTime / VEL_C;
            if (conversion.utc2gps(start_time.c_str(), &start) != 0 ||
                conversion.utc2gps(end_time.c_str(), &end) != 0) {
                emit errorProcess(QStringLiteral("Unable to convert geocoding source acquisition time: %1")
                                      .arg(masterH5));
                return;
            }
            if (Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
                                               &lonMax, &latMax, &lonMin, &latMin) != 0) {
                emit errorProcess(QStringLiteral("Unable to determine geocoding source bounds: %1").arg(masterH5));
                return;
            }
            const int demResult = Utils::getSRTMDEM(demPath.toStdString().c_str(), dem, &lon_upperleft,
                                                     &lat_upperleft, lonMin, lonMax, latMin, latMax);
            if (demResult < 0 || dem.empty()) {
                emit errorProcess(QStringLiteral("Unable to load auxiliary terrain DEM: %1").arg(demPath));
                return;
            }
            QElapsedTimer demMappingTimer;
            demMappingTimer.start();
            InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping started: %1").arg(masterH5));
            ret = flat.demMapping(dem, mappedDem, mapped_lat, mapped_lon, lon_upperleft, lat_upperleft, offset_row,
                                  offset_col, sceneHeight, sceneWidth, prf, rangeSpacing, wavelength, nearRangeTime,
                                  start, end, statevec, 20, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, geocodingProgressCallback);
            InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping finished: status=%1, elapsed_ms=%2, source=%3")
                .arg(ret).arg(demMappingTimer.elapsed()).arg(masterH5));
            if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                Q_EMIT cancelled();
                return;
            }
            if (ret < 0 || mapped_lat.empty() || mapped_lon.empty()) {
                emit errorProcess(QStringLiteral("DEM mapping failed for geocoding source: %1").arg(masterH5));
                return;
            }
            //多视操作
            if (multilook_rg > 1 || multilook_az > 1)
            {
                int rows_mapped = sceneHeight / multilook_az;
                int cols_mapped = sceneWidth / multilook_rg;
                Mat lon_new(rows_mapped, cols_mapped, CV_32F);
                for (int i = 0; i < rows_mapped; i++)
                {
                    for (int j = 0; j < cols_mapped; j++)
                    {
                        lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
                            cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
                    }
                }
                lon_new.copyTo(mapped_lon);
                for (int i = 0; i < rows_mapped; i++)
                {
                    for (int j = 0; j < cols_mapped; j++)
                    {
                        lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
                            cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
                    }
                }
                lon_new.copyTo(mapped_lat);
            }
        }
emit updateProcess(90, QStringLiteral("正在地理编码……"));
        double lat_north, lat_south, lon_west, lon_east;
        for (int i = 0; i < input_files.size(); i++)
        {
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                Q_EMIT cancelled();
                return;
            }

            const QString inputH5 = QString::fromStdString(input_files[i]);
            QString inputError;
            QString inputDataset;
            if (product_level == QString("phase-1.0"))
            {
                inputDataset = QStringLiteral("phase");
                geocode_Rank_level = "phase-1.1";
            }
            else if (product_level == QString("phase-2.0"))
            {
                inputDataset = QStringLiteral("phase");
                geocode_Rank_level = "phase-2.1";
            }
            else if (product_level == QString("phase-3.0"))
            {
                inputDataset = QStringLiteral("phase");
                geocode_Rank_level = "phase-3.1";
            }
            else if (product_level == QString("coherence-1.0"))
            {
                inputDataset = QStringLiteral("coherence");
                geocode_Rank_level = "coherence-1.1";
            }
            else if (product_level == QString("dem-1.0"))
            {
                inputDataset = QStringLiteral("dem");
                geocode_Rank_level = "dem-1.1";
            }
            else if (product_level == QString("SBAS-1.0"))
            {
                inputDataset = QStringLiteral("defomation_velocity");
                geocode_Rank_level = "SBAS-1.1";
            }
            else
            {
                emit errorProcess(QStringLiteral("Unsupported geocoding product level: %1").arg(product_level));
                return;
            }
            if (!NodeUtils::readMatFromH5(inputH5, inputDataset, phase, -1, &inputError) || phase.empty()) {
                emit errorProcess(QStringLiteral("Unable to read geocoding input dataset %1 from %2: %3")
                                      .arg(inputDataset, inputH5, inputError));
                return;
            }
            QElapsedTimer sar2UtmTimer;
            sar2UtmTimer.start();
            InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2UTM started: %1").arg(inputH5));
            ret = util.SAR2UTM(mapped_lon, mapped_lat, phase, mapped_phase, 1, &lon_east, &lon_west, &lat_north, &lat_south);
            InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2UTM finished: status=%1, elapsed_ms=%2, input=%3")
                .arg(ret).arg(sar2UtmTimer.elapsed()).arg(inputH5));
            if (ret < 0 || mapped_phase.empty()) {
                emit errorProcess(QStringLiteral("SAR to UTM conversion failed for geocoding input: %1").arg(inputH5));
                return;
            }
            QElapsedTimer outputTimer;
            outputTimer.start();
            InSARLogManager::LogInfo("GeocodingWorker", QString("Geocoding output write started: %1").arg(inputH5));
            {
                NodeUtils::Hdf5Locker locker;
                QString outputH5 = QString::fromStdString(output_files[i]);
                if (conversion.creat_new_h5(output_files[i].c_str()) < 0) {
                    emit errorProcess(QStringLiteral("Unable to create geocoding output H5: %1").arg(outputH5));
                    return;
                }
                QString outputError;
                bool writeSucceeded =
                    NodeUtils::writeScalarToH5(outputH5, "lon_east", lon_east, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "lon_west", lon_west, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "lat_north", lat_north, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "lat_south", lat_south, &outputError);
                if (product_level == QString("phase-1.0") ||
                    product_level == QString("phase-2.0") ||
                    product_level == QString("phase-3.0")
                    )
                {
                    writeSucceeded = writeSucceeded && NodeUtils::writeMatToH5(outputH5, "phase", mapped_phase, &outputError);
                }
                if (product_level == QString("coherence-1.0"))
                {
                    writeSucceeded = writeSucceeded && NodeUtils::writeMatToH5(outputH5, "coherence", mapped_phase, &outputError);
                }
                if (product_level == QString("dem-1.0"))
                {
                    writeSucceeded = writeSucceeded && NodeUtils::writeMatToH5(outputH5, "dem", mapped_phase, &outputError);
                }
                if (product_level == QString("SBAS-1.0"))
                {
                    writeSucceeded = writeSucceeded && NodeUtils::writeMatToH5(outputH5, "defomation_velocity", mapped_phase, &outputError);
                }
                if (!writeSucceeded) {
                    emit errorProcess(QStringLiteral("Unable to write geocoding output H5 %1: %2")
                                          .arg(outputH5, outputError));
                    return;
                }
            }
            InSARLogManager::LogInfo("GeocodingWorker", QString("Geocoding output write finished: elapsed_ms=%1, output=%2")
                .arg(outputTimer.elapsed()).arg(QString::fromStdString(output_files[i])));
int process = 90 + double(i + 1) / (double)input_files.size() * 9.0;

            emit updateProcess(process, QStringLiteral("正在地理编码……"));
        }
    }
    //SAR图像地理编码
    else
    {
        Mat mapped_lat, mapped_lon, amplitude, mapped_amplitude;
        ComplexMat slc;
        geocode_Rank_level = "amplitude-1.1";

        double lonMax2 = 0, lonMin2 = 0, latMax2 = 0, latMin2 = 0, lon_upperleft2 = 0, lat_upperleft2 = 0, rangeSpacing2 = 0,
            nearRangeTime2 = 0, wavelength2 = 0, prf2 = 0, start2 = 0, end2 = 0;
        int sceneHeight2 = 0, sceneWidth2 = 0, offset_row2 = 0, offset_col2 = 0;
        Mat lon_coef2, lat_coef2, dem2, mappedDem2, statevec2;
        std::string start_time2, end_time2;

        const QString masterH5 = QString::fromStdString(input_files[masterIndex]);
        QString inputError;
        const bool hasMappedCoordinates =
            NodeUtils::readMatFromH5(masterH5, "mapped_lon", mapped_lon, -1, &inputError) &&
            NodeUtils::readMatFromH5(masterH5, "mapped_lat", mapped_lat, -1, &inputError);
        if (!hasMappedCoordinates)
        {
            if (!NodeUtils::readScalarFromH5(masterH5, "range_len", sceneWidth2, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "azimuth_len", sceneHeight2, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "offset_row", offset_row2, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "offset_col", offset_col2, &inputError) ||
                !NodeUtils::readMatFromH5(masterH5, "lon_coefficient", lon_coef2, -1, &inputError) ||
                !NodeUtils::readMatFromH5(masterH5, "lat_coefficient", lat_coef2, -1, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "prf", prf2, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "carrier_frequency", wavelength2, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "range_spacing", rangeSpacing2, &inputError) ||
                !NodeUtils::readScalarFromH5(masterH5, "slant_range_first_pixel", nearRangeTime2, &inputError) ||
                !NodeUtils::readStringFromH5(masterH5, "acquisition_start_time", start_time2, &inputError) ||
                !NodeUtils::readStringFromH5(masterH5, "acquisition_stop_time", end_time2, &inputError) ||
                !NodeUtils::readMatFromH5(masterH5, "state_vec", statevec2, -1, &inputError)) {
                emit errorProcess(QStringLiteral("Unable to read SAR geocoding source metadata from %1: %2")
                                      .arg(masterH5, inputError));
                return;
            }

            Deflat flat;
            wavelength2 = VEL_C / wavelength2;
            nearRangeTime2 = 2.0 * nearRangeTime2 / VEL_C;
            if (conversion.utc2gps(start_time2.c_str(), &start2) != 0 ||
                conversion.utc2gps(end_time2.c_str(), &end2) != 0) {
                emit errorProcess(QStringLiteral("Unable to convert SAR geocoding acquisition time: %1").arg(masterH5));
                return;
            }
            if (Utils::computeImageGeoBoundry(lat_coef2, lon_coef2, sceneHeight2, sceneWidth2, offset_row2,
                                               offset_col2, &lonMax2, &latMax2, &lonMin2, &latMin2) != 0) {
                emit errorProcess(QStringLiteral("Unable to determine SAR geocoding source bounds: %1").arg(masterH5));
                return;
            }
            const int demResult = Utils::getSRTMDEM(demPath.toStdString().c_str(), dem2, &lon_upperleft2,
                                                     &lat_upperleft2, lonMin2, lonMax2, latMin2, latMax2);
            if (demResult < 0 || dem2.empty()) {
                emit errorProcess(QStringLiteral("Unable to load auxiliary terrain DEM: %1").arg(demPath));
                return;
            }
            QElapsedTimer demMappingTimer;
            demMappingTimer.start();
            InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping started: %1").arg(masterH5));
            ret = flat.demMapping(dem2, mappedDem2, mapped_lat, mapped_lon, lon_upperleft2, lat_upperleft2,
                                  offset_row2, offset_col2, sceneHeight2, sceneWidth2, prf2, rangeSpacing2,
                                  wavelength2, nearRangeTime2, start2, end2, statevec2, 20, 5.0 / 6000.0,
                                  5.0 / 6000.0, 0, 0, geocodingProgressCallback);
            InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping finished: status=%1, elapsed_ms=%2, source=%3")
                .arg(ret).arg(demMappingTimer.elapsed()).arg(masterH5));
            if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                Q_EMIT cancelled();
                return;
            }
            if (ret < 0 || mapped_lat.empty() || mapped_lon.empty()) {
                emit errorProcess(QStringLiteral("DEM mapping failed for SAR geocoding source: %1").arg(masterH5));
                return;
            }
        }

        //多视操作
        if (multi_rg > 1 || multi_az > 1)
        {
            int rows_mapped = mapped_lon.rows / multi_az;
            int cols_mapped = mapped_lon.cols / multi_rg;
            Mat lon_new(rows_mapped, cols_mapped, CV_32F);
            for (int i = 0; i < rows_mapped; i++)
            {
                for (int j = 0; j < cols_mapped; j++)
                {
                    lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * multi_az, i * multi_az + multi_az),
                        cv::Range(j * multi_rg, j * multi_rg + multi_rg)))[0];
                }
            }
            lon_new.copyTo(mapped_lon);
            for (int i = 0; i < rows_mapped; i++)
            {
                for (int j = 0; j < cols_mapped; j++)
                {
                    lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * multi_az, i * multi_az + multi_az),
                        cv::Range(j * multi_rg, j * multi_rg + multi_rg)))[0];
                }
            }
            lon_new.copyTo(mapped_lat);
        }

emit updateProcess(90, QStringLiteral("正在地理编码……"));
        double lat_north, lat_south, lon_west, lon_east;
        for (int i = 0; i < input_files.size(); i++)
        {
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                Q_EMIT cancelled();
                return;
            }

            const QString inputH5 = QString::fromStdString(input_files[i]);
            {
                NodeUtils::Hdf5Locker locker;
                ret = conversion.read_slc_from_h5(input_files[i].c_str(), slc);
            }
            if (ret < 0) {
                emit errorProcess(QStringLiteral("Unable to read SAR geocoding input: %1").arg(inputH5));
                return;
            }
            slc.convertTo(slc, CV_64F);
            amplitude = slc.GetMod();
            ret = util.multilook_SAR(amplitude, amplitude, multi_rg, multi_az);
            if (ret < 0 || amplitude.empty()) {
                emit errorProcess(QStringLiteral("SAR amplitude multilooking failed for geocoding input: %1").arg(inputH5));
                return;
            }
            QElapsedTimer sar2UtmTimer;
            sar2UtmTimer.start();
            InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2UTM started: %1").arg(inputH5));
            ret = util.SAR2UTM(mapped_lon, mapped_lat, amplitude, mapped_amplitude, 1, &lon_east, &lon_west, &lat_north, &lat_south);
            InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2UTM finished: status=%1, elapsed_ms=%2, input=%3")
                .arg(ret).arg(sar2UtmTimer.elapsed()).arg(inputH5));
            if (ret < 0 || mapped_amplitude.empty()) {
                emit errorProcess(QStringLiteral("SAR to UTM conversion failed for geocoding input: %1").arg(inputH5));
                return;
            }
            QElapsedTimer outputTimer;
            outputTimer.start();
            InSARLogManager::LogInfo("GeocodingWorker", QString("Geocoding output write started: %1").arg(inputH5));
            {
                NodeUtils::Hdf5Locker locker;
                QString outputH5 = QString::fromStdString(output_files[i]);
                if (conversion.creat_new_h5(output_files[i].c_str()) < 0) {
                    emit errorProcess(QStringLiteral("Unable to create geocoding output H5: %1").arg(outputH5));
                    return;
                }
                QString outputError;
                if (!NodeUtils::writeScalarToH5(outputH5, "lon_east", lon_east, &outputError) ||
                    !NodeUtils::writeScalarToH5(outputH5, "lon_west", lon_west, &outputError) ||
                    !NodeUtils::writeScalarToH5(outputH5, "lat_north", lat_north, &outputError) ||
                    !NodeUtils::writeScalarToH5(outputH5, "lat_south", lat_south, &outputError) ||
                    !NodeUtils::writeMatToH5(outputH5, "amplitude", mapped_amplitude, &outputError)) {
                    emit errorProcess(QStringLiteral("Unable to write geocoding output H5 %1: %2")
                                          .arg(outputH5, outputError));
                    return;
                }
            }
            InSARLogManager::LogInfo("GeocodingWorker", QString("Geocoding output write finished: elapsed_ms=%1, output=%2")
                .arg(outputTimer.elapsed()).arg(QString::fromStdString(output_files[i])));
int process = 90 + double(i + 1) / (double)input_files.size() * 9.0;
            emit updateProcess(process, QStringLiteral("正在地理编码……"));
        }
    }
    for (int i = 0; i < input_files.size(); i++)
    {
        QFileInfo fileinfo = QFileInfo(QString(output_files.at(i).c_str()));
        QString geocode_name = fileinfo.baseName();

        GeocodingFileResult gRes;
        gRes.dstNode = dstNode;
        gRes.geocodeName = geocode_name;
        gRes.geocodePath = fileinfo.absoluteFilePath();
        gRes.relativePath = "/" + dstNode + "/" + geocode_name + ".h5";
        gRes.rankLevel = geocode_Rank_level;
        Q_EMIT geocodingGenerated(gRes);
    }

    InSARLogManager::LogInfo("GeocodingWorker", QStringLiteral("Geocoding progress: 100% - output processing completed."));
    emit updateProcess(100, QStringLiteral("完成……"));
    InSARLogManager::LogInfo("GeocodingWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}
