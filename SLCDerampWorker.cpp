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
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <mutex>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "Deflat_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "Deflat.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif

using namespace cv;
using namespace std;

namespace {
struct ProgressContext { SLCDerampWorker* worker; int start = 0; int end = 100; };
std::array<std::shared_ptr<ProgressContext>, 8> g_progressContexts;
std::mutex g_progressMutex;

bool reportDerampProgress(int slot, int progress, const char* message)
{
    std::shared_ptr<ProgressContext> context;
    {
        std::lock_guard<std::mutex> guard(g_progressMutex);
        context = g_progressContexts[slot];
    }
    if (!context || !context->worker || context->worker->thread()->isInterruptionRequested() || context->worker->isStopRequested()) {
        return false;
    }
    const int mapped = context->start + (context->end - context->start) * qBound(0, progress, 100) / 100;
    emit context->worker->updateProcess(mapped, message ? QString::fromLocal8Bit(message)
                                                         : QStringLiteral("正在进行 SLC 去斜坡"));
    return true;
}
template <int Slot> bool __stdcall derampProgressCallback(int progress, const char* message)
{ return reportDerampProgress(Slot, progress, message); }
const std::array<DeflatProgressCallback, 8> g_progressCallbacks = {
    derampProgressCallback<0>, derampProgressCallback<1>, derampProgressCallback<2>, derampProgressCallback<3>,
    derampProgressCallback<4>, derampProgressCallback<5>, derampProgressCallback<6>, derampProgressCallback<7> };
class ScopedProgress {
public:
    explicit ScopedProgress(SLCDerampWorker* worker) : m_context(std::make_shared<ProgressContext>()) { m_context->worker = worker; }
    ~ScopedProgress() { if (m_slot >= 0) { std::lock_guard<std::mutex> guard(g_progressMutex); g_progressContexts[m_slot].reset(); } }
    bool acquire() { std::lock_guard<std::mutex> guard(g_progressMutex); for (int i = 0; i < 8; ++i) if (!g_progressContexts[i]) { g_progressContexts[i] = m_context; m_slot = i; return true; } return false; }
    void setStage(int start, int end) { m_context->start = start; m_context->end = end; }
    DeflatProgressCallback callback() const { return m_slot < 0 ? nullptr : g_progressCallbacks[m_slot]; }
private: std::shared_ptr<ProgressContext> m_context; int m_slot = -1;
};
}

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
    QStringList inputPaths,
    QString demPath)
{
    InSARLogManager::LogInfo("SLCDerampWorker",
        QString("SLC deramp task started. Project: %1, destination: %2").arg(projectName, dstNode));

    const auto fail = [this](const QString& error) { emit errorProcess(error); };
    const auto cancel = [this]() {
        if (!QThread::currentThread()->isInterruptionRequested() && !isStopRequested()) return false;
        emit cancelled();
        return true;
    };
    if (masterIndex < 1 || masterIndex > inputPaths.size() || savePath.isEmpty() || dstNode.isEmpty() ||
        inputPaths.isEmpty() || demPath.isEmpty()) {
        fail(QStringLiteral("Invalid SLC deramp parameters."));
        return;
    }
    for (const QString& path : inputPaths) {
        if (!QFileInfo(path).isReadable()) {
            fail(QStringLiteral("Input SLC is unavailable: %1").arg(path));
            return;
        }
    }
    if (!QFileInfo(demPath).isReadable()) {
        fail(QStringLiteral("DEM is unavailable: %1").arg(demPath));
        return;
    }

    QDir projectDir(savePath);
    if (!projectDir.exists(dstNode) && !projectDir.mkdir(dstNode)) {
        fail(QStringLiteral("Unable to create SLC deramp output directory."));
        return;
    }

    vector<string> sourceImages;
    vector<string> outputImages;
    QStringList originNames;
    for (const QString& inputPath : inputPaths) {
        const QFileInfo inputInfo(inputPath);
        sourceImages.push_back(inputInfo.absoluteFilePath().toStdString());
        outputImages.push_back(projectDir.absoluteFilePath(dstNode + "/" + inputInfo.baseName() + "_deramp.h5").toStdString());
        originNames.append(inputInfo.baseName());
    }

    FormatConversion conversion;
    Utils util;
    Deflat flat;
    ScopedProgress progress(this);
    if (!progress.acquire()) {
        fail(QStringLiteral("SLC deramp progress context is unavailable."));
        return;
    }
    for (const string& outputPath : outputImages) {
        NodeUtils::Hdf5Locker locker;
        if (conversion.creat_new_h5(outputPath.c_str()) < 0) {
            fail(QStringLiteral("Unable to create SLC deramp output."));
            return;
        }
    }

    int sceneWidth = 0, sceneHeight = 0, offsetRow = 0, offsetCol = 0;
    double prf = 0.0, rangeSpacing = 0.0, wavelength = 0.0, nearRangeTime = 0.0, start = 0.0, end = 0.0;
    Mat lonCoef, latCoef, statevec;
    string startTime, endTime;
    const QString masterPath = inputPaths.at(masterIndex - 1);
    {
        NodeUtils::Hdf5Locker locker;
        if (!NodeUtils::readScalarFromH5(masterPath, "range_len", sceneWidth) ||
            !NodeUtils::readScalarFromH5(masterPath, "azimuth_len", sceneHeight) ||
            !NodeUtils::readScalarFromH5(masterPath, "offset_row", offsetRow) ||
            !NodeUtils::readScalarFromH5(masterPath, "offset_col", offsetCol) ||
            !NodeUtils::readMatFromH5(masterPath, "lon_coefficient", lonCoef) ||
            !NodeUtils::readMatFromH5(masterPath, "lat_coefficient", latCoef) ||
            !NodeUtils::readScalarFromH5(masterPath, "prf", prf) ||
            !NodeUtils::readScalarFromH5(masterPath, "carrier_frequency", wavelength) ||
            !NodeUtils::readScalarFromH5(masterPath, "range_spacing", rangeSpacing) ||
            !NodeUtils::readScalarFromH5(masterPath, "slant_range_first_pixel", nearRangeTime) ||
            !NodeUtils::readStringFromH5(masterPath, "acquisition_start_time", startTime) ||
            !NodeUtils::readStringFromH5(masterPath, "acquisition_stop_time", endTime) ||
            !NodeUtils::readMatFromH5(masterPath, "state_vec", statevec)) {
            fail(QStringLiteral("Unable to read master SLC metadata."));
            return;
        }
    }
    wavelength = VEL_C / wavelength;
    nearRangeTime = 2.0 * nearRangeTime / VEL_C;
    if (conversion.utc2gps(startTime.c_str(), &start) != 0 || conversion.utc2gps(endTime.c_str(), &end) != 0) {
        fail(QStringLiteral("Unable to convert master SLC acquisition time."));
        return;
    }

    double lonMax = 0.0, lonMin = 0.0, latMax = 0.0, latMin = 0.0, lonUpperLeft = 0.0, latUpperLeft = 0.0;
    Mat dem, mappedDem, mappedLat, mappedLon;
    util.computeImageGeoBoundry(latCoef, lonCoef, sceneHeight, sceneWidth, offsetRow, offsetCol,
                                &lonMax, &latMax, &lonMin, &latMin);
    const int demResult = util.getSRTMDEM(demPath.toStdString().c_str(), dem, &lonUpperLeft, &latUpperLeft, lonMin, lonMax, latMin, latMax);
    if (demResult < 0 || dem.empty()) {
        fail(QStringLiteral("Unable to load DEM data."));
        return;
    }
    progress.setStage(10, 45);
    const double scenePx = static_cast<double>(sceneHeight) * sceneWidth;
    const double demPx = (dem.total() > 0) ? static_cast<double>(dem.total()) : 1.0;
    const int interpTimes = std::max(10, std::min(40,
        static_cast<int>(std::ceil(std::sqrt(scenePx / demPx) * 1.4))));
    InSARLogManager::LogInfo("SLCDerampWorker", QStringLiteral("SLC deramp demMapping interp_times resolved: %1 (scenePx=%2, demPx=%3, factor=%4)")
        .arg(interpTimes).arg(static_cast<qulonglong>(scenePx)).arg(static_cast<qulonglong>(demPx))
        .arg(std::sqrt(scenePx / demPx) * 1.4, 0, 'f', 2));
    const int mappingResult = flat.demMapping(dem, mappedDem, mappedLat, mappedLon, lonUpperLeft, latUpperLeft,
                                              offsetRow, offsetCol, sceneHeight, sceneWidth, prf, rangeSpacing,
                                              wavelength, nearRangeTime, start, end, statevec, interpTimes, 5.0 / 6000.0,
                                              5.0 / 6000.0, 0, 0, progress.callback());
    if (mappingResult == -2) {
        emit cancelled();
        return;
    }
    if (mappingResult < 0 || mappedDem.empty() || mappedLat.empty() || mappedLon.empty() ||
        mappedDem.type() != CV_16S || mappedLat.type() != CV_64F || mappedLon.type() != CV_64F) {
        if (cancel()) return;
        fail(QStringLiteral("DEM mapping failed."));
        return;
    }
    if (cancel()) return;
    {
        NodeUtils::Hdf5Locker locker;
        const QString outputPath = QString::fromStdString(outputImages.at(masterIndex - 1));
        if (!NodeUtils::writeMatToH5(outputPath, "mapped_lat", mappedLat) ||
            !NodeUtils::writeMatToH5(outputPath, "mapped_lon", mappedLon)) {
            fail(QStringLiteral("Unable to write master DEM mapping metadata."));
            return;
        }
    }

    QStringList resultH5Paths;
    ComplexMat slc;
    for (const QString& inputPath : inputPaths) {
        int inputWidth = 0, inputHeight = 0;
        if (!NodeUtils::readScalarFromH5(inputPath, "range_len", inputWidth) ||
            !NodeUtils::readScalarFromH5(inputPath, "azimuth_len", inputHeight) ||
            inputWidth != sceneWidth || inputHeight != sceneHeight) {
            fail(QStringLiteral("Input SLC dimensions do not match the master scene: %1").arg(inputPath));
            return;
        }
    }
    for (int i = 0; i < inputPaths.size(); ++i) {
        if (cancel()) return;
        progress.setStage(45 + 50 * i / inputPaths.size(), 45 + 50 * (i + 1) / inputPaths.size());
        const QString outputPath = QString::fromStdString(outputImages.at(i));
        NodeUtils::Hdf5Locker locker;
        const int derampResult = flat.SLC_deramp(slc, mappedDem, mappedLat, mappedLon, sourceImages.at(i).c_str(),
                                                  TR_MODE_SINGLE_TX_SINGLE_RX, progress.callback());
        if (derampResult == -2) {
            emit cancelled();
            return;
        }
        if (derampResult < 0 ||
            conversion.write_slc_to_h5(outputImages.at(i).c_str(), slc) < 0 ||
            conversion.Copy_para_from_h5_2_h5(sourceImages.at(i).c_str(), outputImages.at(i).c_str()) < 0) {
            fail(QStringLiteral("SLC deramp failed: %1").arg(inputPaths.at(i)));
            return;
        }
        int sourceOffsetRow = 0, sourceOffsetCol = 0;
        if (!NodeUtils::readScalarFromH5(inputPaths.at(i), "offset_row", sourceOffsetRow) ||
            !NodeUtils::readScalarFromH5(inputPaths.at(i), "offset_col", sourceOffsetCol) ||
            !NodeUtils::writeScalarToH5(outputPath, "offset_row", sourceOffsetRow) ||
            !NodeUtils::writeScalarToH5(outputPath, "offset_col", sourceOffsetCol) ||
            !NodeUtils::writeScalarToH5(outputPath, "range_len", sceneWidth) ||
            !NodeUtils::writeScalarToH5(outputPath, "azimuth_len", sceneHeight)) {
            fail(QStringLiteral("Unable to preserve SLC metadata: %1").arg(outputPath));
            return;
        }
        resultH5Paths.append(outputPath);
    }
    emit sendResults(dstNode, resultH5Paths, originNames, savePath, projectName);
    InSARLogManager::LogInfo("SLCDerampWorker",
        QString("SLC deramp completed. Total images: %1").arg(inputPaths.size()));
    emit endProcess();
}
