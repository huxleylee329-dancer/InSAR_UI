#include "DemWorker.h"

#include <Dem.h>
#include <FormatConversion.h>
#include <Utils.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <QThread>
#include <QUuid>
#include <cmath>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Dem_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Dem.lib")
#endif

using namespace cv;
using namespace std;

namespace {

InSARLogManager::LogLevel toApplicationLogLevel(DemLogLevel level)
{
    switch (level) {
    case DEM_LOG_ERROR: return InSARLogManager::LevelError;
    case DEM_LOG_WARNING: return InSARLogManager::LevelWarning;
    case DEM_LOG_INFO: return InSARLogManager::LevelInfo;
    default: return InSARLogManager::LevelDebug;
    }
}

QString diagnosticText(const char* value)
{
    return value ? QString::fromUtf8(value) : QString();
}

thread_local QString demDiagnosticCallId;
thread_local QStringList successfulHdf5Reads;

void __stdcall demDiagnosticCallback(const DemDiagnosticEvent* event, void*)
{
    if (!event) return;

    const QString callId = diagnosticText(event->callId);
    const QString stage = diagnosticText(event->stage);
    const QString message = diagnosticText(event->message);
    const InSARLogManager::LogLevel level = toApplicationLogLevel(event->level);
    if (stage == QStringLiteral("entry")) {
        demDiagnosticCallId = callId;
        successfulHdf5Reads.clear();
    }

    if (level == InSARLogManager::LevelDebug &&
        message == QStringLiteral("HDF5 array read succeeded.")) {
        successfulHdf5Reads.append(
            QStringLiteral("%1:%2[%3x%4,CV%5]")
                .arg(stage, diagnosticText(event->dataset))
                .arg(event->rows).arg(event->columns).arg(event->cvType));
        return;
    }

    QStringList fields;
    const QString detail = diagnosticText(event->detail);
    const QString h5File = diagnosticText(event->h5File);
    const QString dataset = diagnosticText(event->dataset);
    if (!callId.isEmpty()) fields.append(QStringLiteral("callId=%1").arg(callId));
    fields.append(QStringLiteral("demError=%1").arg(static_cast<int>(event->error)));
    if (!h5File.isEmpty()) fields.append(QStringLiteral("h5=%1").arg(h5File));
    if (!dataset.isEmpty()) fields.append(QStringLiteral("dataset=%1").arg(dataset));
    if (event->hdf5Status != 0) fields.append(QStringLiteral("hdf5Status=%1").arg(event->hdf5Status));
    if (event->rows >= 0 || event->columns >= 0 || event->cvType >= 0) {
        fields.append(QStringLiteral("shape=%1x%2").arg(event->rows).arg(event->columns));
        fields.append(QStringLiteral("cvType=%1").arg(event->cvType));
    }
    if (!detail.isEmpty()) fields.append(QStringLiteral("detail=%1").arg(detail));
    if (stage == QStringLiteral("complete") && callId == demDiagnosticCallId &&
        !successfulHdf5Reads.isEmpty()) {
        fields.append(QStringLiteral("hdf5ReadCount=%1").arg(successfulHdf5Reads.size()));
        fields.append(QStringLiteral("hdf5Reads=%1").arg(successfulHdf5Reads.join(QStringLiteral("|"))));
        successfulHdf5Reads.clear();
        demDiagnosticCallId.clear();
    }

    InSARLogManager::LogDiagnostic(level, QStringLiteral("DemDLL"),
        fields.isEmpty() ? message : QStringLiteral("%1. %2").arg(message, fields.join(QStringLiteral(", "))),
        LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile,
        QStringLiteral("dem.dll"), stage);
}

void logSourceDependency(const QString& inputH5, const QString& dataset, const QString& projectRoot)
{
    std::string source;
    const bool readOk = NodeUtils::readStringFromH5(inputH5, dataset, source);
    const QString raw = readOk ? QString::fromStdString(source) : QString();
    QString relative = raw;
    while (relative.startsWith('/') || relative.startsWith('\\')) relative.remove(0, 1);
    const QString resolved = raw.isEmpty() ? QString() : QDir::cleanPath(QDir(projectRoot).absoluteFilePath(relative));
    const bool exists = !resolved.isEmpty() && QFileInfo(resolved).exists();
    InSARLogManager::LogDebug(QStringLiteral("DemWorker"),
        QStringLiteral("DEM source preflight: phase=%1, dataset=%2, read=%3, raw=%4, projectRoot=%5, resolved=%6, exists=%7")
            .arg(inputH5, dataset).arg(readOk ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(raw, projectRoot, resolved).arg(exists ? QStringLiteral("true") : QStringLiteral("false")),
        QStringLiteral("dem.preflight.source"));
}

bool resolveSourceH5Path(const std::string& rawSourcePath,
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
    if (!PathResolver::resolve(rawSourcePath, projectRoot.toUtf8().toStdString(), resolution, &pathError)) {
        error = QStringLiteral("Unable to resolve source_1: %1")
                    .arg(QString::fromLatin1(PathResolver::errorMessage(pathError)));
        return false;
    }

    resolvedPath = QString::fromUtf8(resolution.utf8.data(), static_cast<int>(resolution.utf8.size()));
    if (!QFileInfo(resolvedPath).isFile()) {
        error = QStringLiteral("Resolved source_1 H5 does not exist: %1").arg(resolvedPath);
        return false;
    }
    return true;
}

bool sourceGeometryBounds(const QString& phaseH5,
                          const QString& projectRoot,
                          double& minLon,
                          double& maxLon,
                          double& minLat,
                          double& maxLat,
                          QString& error)
{
    std::string sourcePath;
    if (!NodeUtils::readStringFromH5(phaseH5, QStringLiteral("source_1"), sourcePath)) {
        error = QStringLiteral("Unable to read source_1 from DEM input: %1").arg(phaseH5);
        return false;
    }

    QString masterH5;
    if (!resolveSourceH5Path(sourcePath, projectRoot, masterH5, error)) {
        return false;
    }

    QString geometryContractError;
    if (!NodeUtils::validateSentinelGeometryContract(masterH5, &geometryContractError)) {
        error = geometryContractError;
        return false;
    }

    int sceneWidth = 0;
    int sceneHeight = 0;
    int offsetRow = 0;
    int offsetCol = 0;
    Mat lonCoefficient;
    Mat latCoefficient;
    QString metadataError;
    if (!NodeUtils::readScalarFromH5(masterH5, QStringLiteral("range_len"), sceneWidth, &metadataError) ||
        !NodeUtils::readScalarFromH5(masterH5, QStringLiteral("azimuth_len"), sceneHeight, &metadataError) ||
        !NodeUtils::readScalarFromH5(masterH5, QStringLiteral("offset_row"), offsetRow, &metadataError) ||
        !NodeUtils::readScalarFromH5(masterH5, QStringLiteral("offset_col"), offsetCol, &metadataError) ||
        !NodeUtils::readMatFromH5(masterH5, QStringLiteral("lon_coefficient"), lonCoefficient, -1, &metadataError) ||
        !NodeUtils::readMatFromH5(masterH5, QStringLiteral("lat_coefficient"), latCoefficient, -1, &metadataError) ||
        lonCoefficient.empty() || latCoefficient.empty()) {
        error = QStringLiteral("Unable to read source geometry metadata from %1: %2")
                    .arg(masterH5, metadataError);
        return false;
    }

    if (Utils::computeImageGeoBoundry(latCoefficient, lonCoefficient, sceneHeight, sceneWidth,
                                       offsetRow, offsetCol, &maxLon, &maxLat, &minLon, &minLat) != 0) {
        error = QStringLiteral("Unable to determine source geometry bounds: %1").arg(masterH5);
        return false;
    }
    return true;
}

class DemProgressCallContext
{
public:
    DemProgressCallContext(DemWorker* worker, QThread* workerThread,
                           int imageIndex, int imageCount, int imageStartProgress,
                           int imageEndProgress, const QString& callId)
        : m_worker(worker)
        , m_workerThread(workerThread)
        , m_imageIndex(imageIndex)
        , m_imageCount(imageCount)
        , m_imageEndProgress(imageEndProgress)
        , m_lastEmittedProgress(imageStartProgress)
        , m_callId(callId)
    {
    }

    static bool __stdcall callback(int progress, const char* message, void* userData)
    {
        DemProgressCallContext* context = static_cast<DemProgressCallContext*>(userData);
        return context ? context->report(progress, message) : false;
    }

    int completeImageProgress()
    {
        QMutexLocker locker(&m_mutex);
        m_lastEmittedProgress = qMax(m_lastEmittedProgress, m_imageEndProgress);
        return m_lastEmittedProgress;
    }

private:
    bool report(int progress, const char* message)
    {
        QMutexLocker locker(&m_mutex);
        if (!m_timerStarted) {
            m_callbackTimer.start();
            m_timerStarted = true;
        }
        if (progress != 0 && progress != 100 && m_callbackTimer.elapsed() < 100) {
            return true;
        }
        m_callbackTimer.restart();

        if (!m_worker || m_worker->isStopRequested() ||
            (m_workerThread && m_workerThread->isInterruptionRequested())) {
            return false;
        }

        const int imageStartProgress = 10 + m_imageIndex * 80 / m_imageCount;
        const int boundedProgress = qBound(0, progress, 100);
        const int rawMappedProgress = imageStartProgress +
            boundedProgress * (m_imageEndProgress - imageStartProgress) / 100;
        const int mappedProgress = qMax(m_lastEmittedProgress, rawMappedProgress);
        if (mappedProgress != rawMappedProgress && !m_nonMonotonicProgressObserved) {
            InSARLogManager::LogDebug("DemWorker", QString("Suppressed non-monotonic DEM progress: raw=%1%%, mapped=%2%%, retained=%3%%")
                .arg(progress).arg(rawMappedProgress).arg(m_lastEmittedProgress), "dem.progress.regression_suppressed");
            m_nonMonotonicProgressObserved = true;
        }
        m_lastEmittedProgress = mappedProgress;

        const QString messageText = message ? QString::fromLocal8Bit(message) : QString();
        emit m_worker->updateProcess(mappedProgress,
            QString("Generating DEM %1/%2 (%3%) %4")
                .arg(m_imageIndex + 1).arg(m_imageCount).arg(progress).arg(messageText));

        if (progress == 0 || progress == 100 || progress - m_lastLoggedProgress >= 10) {
            InSARLogManager::LogInfo("DemWorker", QString("DEM progress: callId=%1, %2% (total %3%)")
                .arg(m_callId).arg(progress).arg(mappedProgress));
            m_lastLoggedProgress = progress;
        }
        return true;
    }

    DemWorker* m_worker = nullptr;
    QThread* m_workerThread = nullptr;
    const int m_imageIndex;
    const int m_imageCount;
    const int m_imageEndProgress;
    QMutex m_mutex;
    QElapsedTimer m_callbackTimer;
    bool m_timerStarted = false;
    int m_lastLoggedProgress = -10;
    int m_lastEmittedProgress = 0;
    bool m_nonMonotonicProgressObserved = false;
    const QString m_callId;
};

} // namespace

DemWorker::DemWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<DemFileResult>("DemFileResult");
}

DemWorker::~DemWorker()
{
}

void DemWorker::Dem(int method,
                    int times,
                    QString savePath,
                    QString outputNode,
                    QStringList phaseNames,
                    QStringList phasePaths)
{
    const auto finishCancelled = [this]() { emit cancelled(); };
    if (savePath.isEmpty() || outputNode.isEmpty() || phaseNames.isEmpty() ||
        phaseNames.size() != phasePaths.size()) {
        emit errorProcess(QStringLiteral("Invalid DEM parameters or input paths."));
        return;
    }
    if (method != 1) {
        emit errorProcess(QStringLiteral("Unsupported DEM generation method."));
        return;
    }

    InSARLogManager::LogInfo("DemWorker", QString("DEM generation started: %1 images.").arg(phasePaths.size()));
    const QString outputDirectory = savePath + "/" + outputNode;
    QDir targetDirectory(outputDirectory);
    if (!targetDirectory.exists() && !QDir(savePath).mkdir(outputNode)) {
        emit errorProcess(QStringLiteral("Failed to create DEM output directory."));
        return;
    }

    ::Dem dem;
    FormatConversion conversion;

    for (int i = 0; i < phasePaths.size(); ++i) {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            finishCancelled();
            return;
        }

        QString inputH5 = phasePaths.at(i);
        if (QDir::isRelativePath(inputH5)) {
            inputH5 = savePath + "/" + inputH5;
        }
        const QString demName = phaseNames.at(i) + "_dem";
        const QString outputH5 = outputDirectory + "/" + demName + ".h5";
        const int imageStartProgress = 10 + i * 80 / phasePaths.size();
        const int imageEndProgress = 10 + (i + 1) * 80 / phasePaths.size();
        emit updateProcess(imageStartProgress,
                            QString("Generating DEM %1/%2").arg(i + 1).arg(phasePaths.size()));

        int offsetRow = 0;
        int offsetCol = 0;
        {
            NodeUtils::Hdf5Locker locker;
            QString phaseContractError;
            if (!NodeUtils::validateDemPhaseInput(inputH5, &phaseContractError)) {
                emit errorProcess(phaseContractError);
                return;
            }
            Mat phase;
            if (!NodeUtils::readMatFromH5(inputH5, "phase", phase)) {
                emit errorProcess(QStringLiteral("Failed to read input phase data: ") + inputH5);
                return;
            }

            Mat flatPhaseCoefficient;
            if (!NodeUtils::readMatFromH5(inputH5, "flat_phase_coefficient", flatPhaseCoefficient) ||
                flatPhaseCoefficient.empty()) {
                emit errorProcess(QStringLiteral("Input phase file has no valid flat_phase_coefficient: ") + inputH5);
                return;
            }

            InSARLogManager::LogDebug("DemWorker", QString("DEM input preflight: phase=%1, phaseShape=%2x%3, phaseType=%4, flatPhaseShape=%5x%6, flatPhaseType=%7, projectRoot=%8, iterations=%9, method=%10")
                .arg(inputH5).arg(phase.rows).arg(phase.cols).arg(phase.type())
                .arg(flatPhaseCoefficient.rows).arg(flatPhaseCoefficient.cols).arg(flatPhaseCoefficient.type())
                .arg(savePath).arg(times).arg(method), "dem.preflight.input");
            logSourceDependency(inputH5, QStringLiteral("source_1"), savePath);
            logSourceDependency(inputH5, QStringLiteral("source_2"), savePath);

            Mat phaseDem;
            const TaskLogContext taskContext = InSARLogManager::currentTaskContext();
            const QByteArray callId = QStringLiteral("%1:dem:%2")
                .arg(taskContext.runId.isEmpty() ? QStringLiteral("no-run") : taskContext.runId,
                     QUuid::createUuid().toString(QUuid::WithoutBraces)).toUtf8();
            DemDiagnosticOptions diagnostics = {};
            diagnostics.structSize = sizeof(DemDiagnosticOptions);
            diagnostics.version = DEM_DIAGNOSTIC_OPTIONS_VERSION;
            diagnostics.callId = callId.constData();
            diagnostics.logLevel = DEM_LOG_DEBUG;
            diagnostics.callback = demDiagnosticCallback;
            diagnostics.userData = nullptr;
            DemProgressCallContext progressContext(this, QThread::currentThread(), i,
                phasePaths.size(), imageStartProgress, imageEndProgress, QString::fromUtf8(callId));
            diagnostics.progressCallback = DemProgressCallContext::callback;
            diagnostics.progressUserData = &progressContext;
            InSARLogManager::LogDebug("DemWorker", QString("Calling dem_newton_iter_ex: callId=%1, phase=%2, projectRoot=%3, iterations=%4, mode=1, progressContext=explicit")
                .arg(QString::fromUtf8(callId), inputH5, savePath).arg(times), "dem.dll.call");
            const int result = dem.dem_newton_iter_ex(inputH5.toStdString().c_str(), phaseDem,
                savePath.toStdString().c_str(), times, 1, &diagnostics, nullptr);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (result == DEM_ERROR_CANCELLED) {
                finishCancelled();
                return;
            }
            if (result < 0) {
                emit errorProcess(QStringLiteral("DEM Newton iteration failed (callId=%1, code=%2).").arg(QString::fromUtf8(callId)).arg(result));
                return;
            }

            // The DLL guarantees a successful call reports 100%; preserve the
            // image boundary as a defensive UI invariant.
            emit updateProcess(progressContext.completeImageProgress(),
                QString("Generating DEM %1/%2 (completed)").arg(i + 1).arg(phasePaths.size()));

            if (conversion.creat_new_h5(outputH5.toStdString().c_str()) < 0 ||
                !NodeUtils::writeMatToH5(outputH5, "dem", phaseDem)) {
                emit errorProcess(QStringLiteral("Failed to create DEM output: ") + outputH5);
                return;
            }

            string sourcePath;
            Mat value;
            NodeUtils::readStringFromH5(inputH5, "source_1", sourcePath);
            conversion.write_str_to_h5(outputH5.toStdString().c_str(), "source_1", sourcePath.c_str());
            QString masterPath;
            QString sourceResolutionError;
            if (!resolveSourceH5Path(sourcePath, savePath, masterPath, sourceResolutionError)) {
                emit errorProcess(QStringLiteral("Failed to resolve DEM master source: %1").arg(sourceResolutionError));
                return;
            }
            NodeUtils::readStringFromH5(inputH5, "source_2", sourcePath);
            conversion.write_str_to_h5(outputH5.toStdString().c_str(), "source_2", sourcePath.c_str());
            QString sourcePathMetadataError;
            if (!NodeUtils::copySourcePathMetadata(inputH5, outputH5, &sourcePathMetadataError)) {
                emit errorProcess(QStringLiteral("Failed to preserve source-path metadata: %1").arg(sourcePathMetadataError));
                return;
            }
            NodeUtils::writeScalarToH5(outputH5, "dem_generation_method", method);
            NodeUtils::writeScalarToH5(outputH5, "dem_generation_iterations", times);

            // Phase products normally carry mapped_lon/mapped_lat when a
            // geographic mapping has already been calculated.  Older valid
            // phase chains do not: derive their bounds from source_1's SAR
            // geometry coefficients instead of expecting DEM-source lon/lat.
            Mat lonGrid, latGrid;
            double minLon = 0.0, maxLon = 0.0, minLat = 0.0, maxLat = 0.0;
            const bool hasMappedGeometry =
                NodeUtils::readMatFromH5(inputH5, "mapped_lon", lonGrid) &&
                NodeUtils::readMatFromH5(inputH5, "mapped_lat", latGrid) &&
                !lonGrid.empty() && !latGrid.empty() && lonGrid.size() == latGrid.size();
            if (hasMappedGeometry) {
                cv::minMaxLoc(lonGrid, &minLon, &maxLon);
                cv::minMaxLoc(latGrid, &minLat, &maxLat);
            } else {
                QString geometryError;
                if (!sourceGeometryBounds(inputH5, savePath, minLon, maxLon, minLat, maxLat, geometryError)) {
                    emit errorProcess(QStringLiteral("Unable to determine DEM input geometry: %1").arg(geometryError));
                    return;
                }
            }
            if (!(std::isfinite(minLon) && std::isfinite(maxLon) &&
                  std::isfinite(minLat) && std::isfinite(maxLat) &&
                  maxLon > minLon && maxLat > minLat) ||
                !NodeUtils::writeScalarToH5(outputH5, "dem_min_lon", minLon) ||
                !NodeUtils::writeScalarToH5(outputH5, "dem_max_lon", maxLon) ||
                !NodeUtils::writeScalarToH5(outputH5, "dem_min_lat", minLat) ||
                !NodeUtils::writeScalarToH5(outputH5, "dem_max_lat", maxLat)) {
                emit errorProcess(QStringLiteral("Failed to persist DEM geometry metadata: ") + outputH5);
                return;
            }

            if (NodeUtils::readMatFromH5(inputH5, "flat_phase_coefficient", value) ||
                NodeUtils::readMatFromH5(inputH5, "flat_phase_coefficientficient", value)) {
                NodeUtils::writeMatToH5(outputH5, "flat_phase_coefficient", value);
            }
            const char* const copiedDatasets[] = {
                "range_len", "azimuth_len", "multilook_rg", "multilook_az"
            };
            for (const char* dataset : copiedDatasets) {
                NodeUtils::readMatFromH5(inputH5, dataset, value);
                NodeUtils::writeMatToH5(outputH5, dataset, value);
            }

            Mat offsetValue = Mat::zeros(1, 1, CV_32SC1);
            NodeUtils::readMatFromH5(masterPath, "offset_row", offsetValue);
            offsetRow = offsetValue.at<int>(0, 0);
            NodeUtils::readMatFromH5(masterPath, "offset_col", offsetValue);
            offsetCol = offsetValue.at<int>(0, 0);
        }

        DemFileResult fileResult;
        fileResult.demName = demName;
        fileResult.relativeDemPath = "/" + outputNode + "/" + demName + ".h5";
        fileResult.absoluteDemPath = outputH5;
        fileResult.offsetRow = offsetRow;
        fileResult.offsetCol = offsetCol;
        emit demFileGenerated(fileResult);
    }

    InSARLogManager::LogInfo("DemWorker", "DEM generation completed.");
    emit endProcess();
}
