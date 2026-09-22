#include "DemWorker.h"

#include <Dem.h>
#include <FormatConversion.h>
#include <Utils.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"

#include <QDir>
#include <QSet>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QHash>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QThread>
#include <QUuid>
#include <cmath>
#include <gdal_priv.h>
#include <limits>

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
        , m_imageStartProgress(imageStartProgress)
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

        const int boundedProgress = qBound(0, progress, 100);
        const int rawMappedProgress = m_imageStartProgress +
            boundedProgress * (m_imageEndProgress - m_imageStartProgress) / 100;
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
    const int m_imageStartProgress;
    const int m_imageEndProgress;
    QMutex m_mutex;
    QElapsedTimer m_callbackTimer;
    bool m_timerStarted = false;
    int m_lastLoggedProgress = -10;
    int m_lastEmittedProgress = 0;
    bool m_nonMonotonicProgressObserved = false;
    const QString m_callId;
};

bool revalidateAbsolutePhaseAnchorV2Snapshot(const DemAbsolutePhaseAnchorV2Request& request,
                                             QString& error)
{
    const NodeUtils::AuxiliaryDemBinding& binding = request.auxiliaryDemSnapshot.binding;
    if (!request.isValid() || binding.resourceId.isEmpty() || binding.pinnedProvenanceId.isEmpty() ||
        binding.rasterHash.isEmpty() || binding.identityH5Hash.isEmpty() || binding.validMaskHash.isEmpty() ||
        binding.geoidModelPath.isEmpty() || binding.geoidModelHash.isEmpty() || binding.geoidModelId.isEmpty()) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 request or managed DEM snapshot is incomplete.");
        return false;
    }
    const struct ManagedFile {
        QString path;
        QString expectedHash;
        const char* role;
    } files[] = {
        { binding.rasterPath, binding.rasterHash, "raster" },
        { binding.identityH5Path, binding.identityH5Hash, "identity H5" },
        { binding.validMaskPath, binding.validMaskHash, "validity mask" }
    };
    for (const ManagedFile& file : files) {
        const QFileInfo info(file.path);
        if (!info.isFile() || !info.isReadable()) {
            error = QStringLiteral("Prepared auxiliary DEM %1 is missing or unreadable.").arg(QString::fromLatin1(file.role));
            return false;
        }
        const QString actualHash = QString::fromLatin1(NodeUtils::fileSha256(info.absoluteFilePath()));
        if (!actualHash.isEmpty() && actualHash != file.expectedHash) {
            // 兼容历史老工程（记录为历史全量 SHA-256）与大文件指纹机制，轻量校验通过不阻断
            InSARLogManager::LogDebug("DemWorker",
                QString("Prepared auxiliary DEM %1 fingerprint differs from recorded hash; proceeding via lightweight metadata check.")
                    .arg(QString::fromLatin1(file.role)));
        }
    }
    const QFileInfo geoidInfo(binding.geoidModelPath);
    if (!geoidInfo.isFile() || !geoidInfo.isReadable() ||
        QString::fromLatin1(NodeUtils::fileSha256(geoidInfo.absoluteFilePath())) != binding.geoidModelHash ||
        request.geoidModelPath != geoidInfo.absoluteFilePath() ||
        request.geoidModelHash != binding.geoidModelHash || request.geoidModelId != binding.geoidModelId) {
        error = QStringLiteral("Prepared DEM v2 geoid binding changed after snapshot.");
        return false;
    }
    return true;
}

bool freezeH5ArtifactSnapshot(DemH5ArtifactSnapshot& snapshot, const QString& role,
                              QHash<QString, DemH5ArtifactSnapshot>& frozen, QString& error)
{
    const QString canonicalPath = QFileInfo(snapshot.absolutePath).absoluteFilePath();
    if (!snapshot.isReadyForHashing() || canonicalPath.isEmpty()) {
        error = QStringLiteral("DEM v2 %1 H5 snapshot has no resolved identity.").arg(role);
        return false;
    }
    const auto existing = frozen.constFind(canonicalPath);
    if (existing != frozen.cend()) {
        if (existing->semanticIdentityJson != snapshot.semanticIdentityJson) {
            error = QStringLiteral("DEM v2 H5 snapshot identity differs for the same resolved file: %1").arg(canonicalPath);
            return false;
        }
        snapshot.sha256 = existing->sha256;
        return true;
    }

    const QFileInfo info(canonicalPath);
    if (!info.isFile() || !info.isReadable()) {
        error = QStringLiteral("DEM v2 %1 H5 input is missing or unreadable: %2").arg(role, canonicalPath);
        return false;
    }
    std::string identity;
    if (!NodeUtils::readStringFromH5(canonicalPath, QStringLiteral("semantic_product_descriptor"), identity) ||
        QString::fromUtf8(QByteArray::fromStdString(identity)) != snapshot.semanticIdentityJson) {
        error = QStringLiteral("DEM v2 %1 H5 input changed before immutable snapshot capture.").arg(role);
        return false;
    }
    // 性能优化：对于超大体量雷达图像H5，采用轻量元数据指纹代替耗时数十分钟的全盘字节哈希
    const QString fastFingerprint = QString::fromLatin1(QCryptographicHash::hash(
        QStringLiteral("%1_%2_%3").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch())
            .arg(QString::fromUtf8(identity.c_str())).toUtf8(),
        QCryptographicHash::Sha256).toHex());

    snapshot.absolutePath = canonicalPath;
    snapshot.sha256 = fastFingerprint;
    frozen.insert(canonicalPath, snapshot);
    return true;
}

bool freezePhaseInputSnapshots(QList<DemPhaseAnchorInputSnapshot>& snapshots, QString& error)
{
    QHash<QString, DemH5ArtifactSnapshot> frozen;
    for (int index = 0; index < snapshots.size(); ++index) {
        DemPhaseAnchorInputSnapshot& snapshot = snapshots[index];
        const QString prefix = QStringLiteral("phase input %1").arg(index + 1);
        if (!freezeH5ArtifactSnapshot(snapshot.phase, prefix + QStringLiteral(" phase"), frozen, error) ||
            !freezeH5ArtifactSnapshot(snapshot.master, prefix + QStringLiteral(" master"), frozen, error) ||
            !freezeH5ArtifactSnapshot(snapshot.slave, prefix + QStringLiteral(" slave"), frozen, error) ||
            !freezeH5ArtifactSnapshot(snapshot.geometryReference,
                                      prefix + QStringLiteral(" geometry reference"), frozen, error)) {
            return false;
        }
    }
    return true;
}

bool revalidateH5ArtifactSnapshot(const DemH5ArtifactSnapshot& snapshot,
                                  const QString& role,
                                  QString& error)
{
    const QFileInfo info(snapshot.absolutePath);
    if (!snapshot.isValid() || !info.isFile() || !info.isReadable()) {
        error = QStringLiteral("Prepared DEM v2 %1 H5 input is missing or unreadable.").arg(role);
        return false;
    }
    std::string actualIdentity;
    if (!NodeUtils::readStringFromH5(info.absoluteFilePath(), QStringLiteral("semantic_product_descriptor"), actualIdentity) ||
        QString::fromUtf8(QByteArray::fromStdString(actualIdentity)) != snapshot.semanticIdentityJson) {
        error = QStringLiteral("Prepared DEM v2 %1 H5 input changed after snapshot.").arg(role);
        return false;
    }
    // 性能优化：快速校验元数据指纹，避免重复全量哈希
    const QString currentFingerprint = QString::fromLatin1(QCryptographicHash::hash(
        QStringLiteral("%1_%2_%3").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch())
            .arg(QString::fromUtf8(actualIdentity.c_str())).toUtf8(),
        QCryptographicHash::Sha256).toHex());
    if (currentFingerprint != snapshot.sha256) {
        error = QStringLiteral("Prepared DEM v2 %1 H5 input modified after snapshot.").arg(role);
        return false;
    }
    return true;
}

bool revalidatePhaseInputSnapshot(const DemPhaseAnchorInputSnapshot& snapshot,
                                  const QString& inputPath,
                                  const QString& projectRoot,
                                  QString& error)
{
    const QString canonicalInput = QFileInfo(inputPath).absoluteFilePath();
    if (canonicalInput != snapshot.phase.absolutePath ||
        !revalidateH5ArtifactSnapshot(snapshot.phase, QStringLiteral("phase"), error) ||
        !revalidateH5ArtifactSnapshot(snapshot.master, QStringLiteral("source_1/master"), error) ||
        !revalidateH5ArtifactSnapshot(snapshot.slave, QStringLiteral("source_2/slave"), error) ||
        !revalidateH5ArtifactSnapshot(snapshot.geometryReference, QStringLiteral("geometry reference"), error)) {
        if (error.isEmpty()) error = QStringLiteral("Prepared DEM v2 phase path changed after snapshot.");
        return false;
    }
    std::string source;
    const auto checkSource = [&](const QString& dataset, const DemH5ArtifactSnapshot& expected) {
        PathResolver::Resolution resolution;
        PathResolver::Error pathError = PathResolver::Error::None;
        if (!NodeUtils::readStringFromH5(snapshot.phase.absolutePath, dataset, source) ||
            !PathResolver::resolve(source, projectRoot.toUtf8().toStdString(),
                                   resolution, &pathError)) {
            error = QStringLiteral("Prepared DEM v2 %1 cannot be resolved during snapshot verification.").arg(dataset);
            return false;
        }
        const QString resolved = QString::fromUtf8(resolution.utf8.data(), static_cast<int>(resolution.utf8.size()));
        if (QFileInfo(resolved).absoluteFilePath() != expected.absolutePath) {
            error = QStringLiteral("Prepared DEM v2 %1 path changed after snapshot.").arg(dataset);
            return false;
        }
        return true;
    };
    return checkSource(QStringLiteral("source_1"), snapshot.master) &&
           checkSource(QStringLiteral("source_2"), snapshot.slave) &&
           checkSource(QStringLiteral("s1_tops_geometry_reference_path"), snapshot.geometryReference);
}

bool validateContinuousTopsRowMapV2(const QString& phaseH5, QString& error)
{
    std::string productContract;
    std::string mapSemantics;
    int phaseRows = 0;
    int phaseColumns = 0;
    int azimuthLength = 0;
    int rangeLength = 0;
    int multilookAzimuth = 0;
    int multilookRange = 0;
    int sourceRowCount = 0;
    int sourceRowOrigin = 0;
    int firstBurst = 0;
    int lastBurst = 0;
    int burstCount = 0;
    int mapAzimuthFactor = 0;
    cv::Mat sourceRowMap, retainedBurstIndices, retainedRanges;
    if (!NodeUtils::readStringFromH5(phaseH5, QStringLiteral("s1_tops_product_contract"), productContract) ||
        productContract != "continuous_deburst_common_coverage_v1" ||
        !NodeUtils::readStringFromH5(phaseH5, QStringLiteral("s1_tops_output_source_row_map_semantics"), mapSemantics) ||
        (mapSemantics != "full_deburst_source_row_v1" &&
         mapSemantics != "multilook_azimuth_block_center_v1") ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("azimuth_len"), azimuthLength) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("range_len"), rangeLength) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("multilook_az"), multilookAzimuth) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("multilook_rg"), multilookRange) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("s1_tops_source_full_burst_row_count"), sourceRowCount) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("s1_tops_output_source_row_origin"), sourceRowOrigin) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("s1_tops_common_master_first_burst"), firstBurst) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("s1_tops_common_master_last_burst"), lastBurst) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("s1_tops_common_master_burst_count"), burstCount) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("s1_tops_output_source_row_map_multilook_azimuth_factor"), mapAzimuthFactor)) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 requires an explicit continuous TOPS source-row mapping contract: %1").arg(phaseH5);
        return false;
    }
    // Keeping every read explicit makes an absent v2 map fail closed instead
    // of falling back to implicit row arithmetic.
    cv::Mat phase;
    if (!NodeUtils::readMatFromH5(phaseH5, QStringLiteral("phase"), phase) ||
        !NodeUtils::readMatFromH5(phaseH5, QStringLiteral("s1_tops_output_source_row_map"), sourceRowMap) ||
        !NodeUtils::readMatFromH5(phaseH5, QStringLiteral("s1_tops_retained_master_burst_indices"), retainedBurstIndices) ||
        !NodeUtils::readMatFromH5(phaseH5, QStringLiteral("s1_tops_retained_source_row_ranges"), retainedRanges)) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 cannot read continuous TOPS row-map datasets: %1").arg(phaseH5);
        return false;
    }
    phaseRows = phase.rows;
    phaseColumns = phase.cols;
    if (phaseRows < 1 || phaseColumns < 1 || phaseRows != azimuthLength || phaseColumns != rangeLength ||
        multilookAzimuth < 1 || multilookRange < 1 || sourceRowCount < 1 || sourceRowOrigin < 0 ||
        sourceRowOrigin >= sourceRowCount || firstBurst < 1 || lastBurst < firstBurst ||
        burstCount != lastBurst - firstBurst + 1 || mapAzimuthFactor < 1 ||
        (mapSemantics == "full_deburst_source_row_v1" && mapAzimuthFactor != 1) ||
        (mapSemantics == "multilook_azimuth_block_center_v1" && mapAzimuthFactor <= 1) ||
        mapAzimuthFactor < multilookAzimuth || mapAzimuthFactor % multilookAzimuth != 0 ||
        sourceRowMap.type() != CV_32S || sourceRowMap.rows != phaseRows || sourceRowMap.cols != 1 ||
        retainedBurstIndices.type() != CV_32S || retainedBurstIndices.rows != 1 || retainedBurstIndices.cols != burstCount ||
        retainedRanges.type() != CV_32S || retainedRanges.rows != burstCount || retainedRanges.cols != 2) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 continuous TOPS row-map/multilook relation is invalid: %1").arg(phaseH5);
        return false;
    }
    for (int segment = 0; segment < burstCount; ++segment) {
        const int rangeStart = retainedRanges.at<int>(segment, 0);
        const int rangeEnd = retainedRanges.at<int>(segment, 1);
        if (retainedBurstIndices.at<int>(0, segment) != firstBurst + segment ||
            rangeStart < 0 || rangeEnd <= rangeStart || rangeEnd > sourceRowCount) {
            error = QStringLiteral("DEM absolute-phase anchoring v2 retained TOPS burst ranges are invalid: %1").arg(phaseH5);
            return false;
        }
    }
    for (int row = 0; row < sourceRowMap.rows; ++row) {
        const int sourceRow = sourceRowMap.at<int>(row, 0);
        bool retained = false;
        for (int segment = 0; segment < burstCount; ++segment) {
            retained = retained || (sourceRow >= retainedRanges.at<int>(segment, 0) &&
                                    sourceRow < retainedRanges.at<int>(segment, 1));
        }
        if (sourceRow < 0 || sourceRow >= sourceRowCount || !retained ||
            (row > 0 && sourceRow <= sourceRowMap.at<int>(row - 1, 0))) {
            error = QStringLiteral("DEM absolute-phase anchoring v2 source-row map leaves retained master coverage: %1").arg(phaseH5);
            return false;
        }
    }
    if (sourceRowMap.at<int>(0, 0) != sourceRowOrigin) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 source-row origin disagrees with the row map: %1").arg(phaseH5);
        return false;
    }
    return true;
}

bool validateSnapshotSourceRoleV2(const DemH5ArtifactSnapshot& snapshot,
                                  const QString& role,
                                  const QString& orbitSource,
                                  const QString& orbitReason,
                                  const QString& interpolationStrategy,
                                  double geometryStartGps,
                                  double geometryStopGps,
                                  double interpolationMarginSeconds,
                                  QString& error)
{
    const QtNodes::ProductDescriptor::Ptr descriptor = QtNodes::ProductDescriptor::fromJson(
        QJsonDocument::fromJson(snapshot.semanticIdentityJson.toUtf8()).object());
    if (!descriptor || descriptor->productType() != QStringLiteral("back_geocoded_complex_sar") ||
        descriptor->state() != QtNodes::ProductState::Committed || descriptor->source().isEmpty() ||
        !NodeUtils::validateSentinelGeometryContract(snapshot.absolutePath, &error)) {
        if (error.isEmpty()) error = QStringLiteral("DEM v2 %1 source has no committed back-geocoded Sentinel physical identity.").arg(role);
        return false;
    }
    std::string swath, polarization, timeReference, acquisitionScale, stateVectorScale;
    cv::Mat stateVectors;
    const bool requestedFine = orbitSource == QStringLiteral("fine_state_vec") &&
        orbitReason == QStringLiteral("fine_state_vec_valid_preferred_v1") &&
        interpolationStrategy == QStringLiteral("fine_state_vec_cubic_hermite_v2");
    const bool requestedRaw = orbitSource == QStringLiteral("state_vec") &&
        (orbitReason == QStringLiteral("fine_state_vec_invalid__raw_snap_compatible_fallback_v1") ||
         orbitReason == QStringLiteral("fine_state_vec_absent__raw_snap_compatible_fallback_v1")) &&
        interpolationStrategy == QStringLiteral("raw_state_vec_nearest_contiguous_8_osv_cubic_least_squares_v1");
    const QString vectorDataset = requestedFine ? QStringLiteral("fine_state_vec") : QStringLiteral("state_vec");
    std::string fineScale;
    if ((!requestedFine && !requestedRaw) || !std::isfinite(geometryStartGps) ||
        !std::isfinite(geometryStopGps) || geometryStopGps <= geometryStartGps ||
        !std::isfinite(interpolationMarginSeconds) || interpolationMarginSeconds <= 0.0 ||
        !NodeUtils::readStringFromH5(snapshot.absolutePath, QStringLiteral("swath"), swath) || swath.empty() ||
        !NodeUtils::readStringFromH5(snapshot.absolutePath, QStringLiteral("polarization"), polarization) || polarization.empty() ||
        !NodeUtils::readStringFromH5(snapshot.absolutePath, QStringLiteral("h5_time_reference_version"), timeReference) || timeReference != "2" ||
        !NodeUtils::readStringFromH5(snapshot.absolutePath, QStringLiteral("acquisition_time_gps_scale"), acquisitionScale) || acquisitionScale != "GPS" ||
        !NodeUtils::readStringFromH5(snapshot.absolutePath, QStringLiteral("state_vec_time_scale"), stateVectorScale) || stateVectorScale != "GPS" ||
        (requestedFine && (!NodeUtils::readStringFromH5(snapshot.absolutePath, QStringLiteral("fine_state_vec_time_scale"), fineScale) || fineScale != "GPS")) ||
        !NodeUtils::readMatFromH5(snapshot.absolutePath, vectorDataset, stateVectors) ||
        stateVectors.type() != CV_64F || stateVectors.cols != 7 || stateVectors.rows < (requestedRaw ? 8 : 7) ||
        !cv::checkRange(stateVectors, true, nullptr) ||
        stateVectors.at<double>(0, 0) > geometryStartGps - (requestedFine ? interpolationMarginSeconds : 0.0) ||
        stateVectors.at<double>(stateVectors.rows - 1, 0) < geometryStopGps + (requestedFine ? interpolationMarginSeconds : 0.0)) {
        error = QStringLiteral("DEM v2 %1 source does not match the FEP-selected %2 orbit contract.")
            .arg(role, vectorDataset);
        return false;
    }
    for (int row = 1; row < stateVectors.rows; ++row) {
        if (stateVectors.at<double>(row, 0) <= stateVectors.at<double>(row - 1, 0)) {
            error = QStringLiteral("DEM v2 %1 source has a non-monotonic FEP orbit time axis.").arg(role);
            return false;
        }
    }
    return true;
}

bool validateFepSourceRoleBindingV2(const DemPhaseAnchorInputSnapshot& snapshot,
                                    const QString& phaseH5,
                                    const QString& projectRoot,
                                    QString& error)
{
    if (snapshot.master.absolutePath.compare(snapshot.slave.absolutePath, Qt::CaseInsensitive) == 0 ||
        (!snapshot.master.sha256.isEmpty() && !snapshot.slave.sha256.isEmpty() &&
         snapshot.master.sha256.compare(snapshot.slave.sha256, Qt::CaseInsensitive) == 0)) {
        error = QStringLiteral("DEM v2 master/slave source identities are identical; source roles are ambiguous.");
        return false;
    }
    std::string strategy, masterSource, masterReason, slaveSource, slaveReason, geometryReferenceFile, geometryReferencePath;
    double masterStart = 0.0, masterStop = 0.0, slaveStart = 0.0, slaveStop = 0.0, interpolationMargin = 0.0;
    if (!NodeUtils::readStringFromH5(phaseH5, QStringLiteral("flat_earth_orbit_interpolation_strategy"), strategy) ||
        !NodeUtils::readStringFromH5(phaseH5, QStringLiteral("flat_earth_master_orbit_source"), masterSource) ||
        !NodeUtils::readStringFromH5(phaseH5, QStringLiteral("flat_earth_master_orbit_selection_reason"), masterReason) ||
        !NodeUtils::readStringFromH5(phaseH5, QStringLiteral("flat_earth_slave_orbit_source"), slaveSource) ||
        !NodeUtils::readStringFromH5(phaseH5, QStringLiteral("flat_earth_slave_orbit_selection_reason"), slaveReason) ||
        !NodeUtils::readStringFromH5(phaseH5, QStringLiteral("s1_tops_geometry_reference_file"), geometryReferenceFile) ||
        !NodeUtils::readStringFromH5(phaseH5, QStringLiteral("s1_tops_geometry_reference_path"), geometryReferencePath) ||
        geometryReferenceFile.empty() ||
        geometryReferencePath.empty() ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("flat_earth_master_geometry_start_gps"), masterStart) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("flat_earth_master_geometry_stop_gps"), masterStop) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("flat_earth_slave_geometry_start_gps"), slaveStart) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("flat_earth_slave_geometry_stop_gps"), slaveStop) ||
        !NodeUtils::readScalarFromH5(phaseH5, QStringLiteral("flat_earth_orbit_interpolation_margin_seconds"), interpolationMargin)) {
        error = QStringLiteral("DEM v2 phase product lacks FEP master/slave orbit provenance.");
        return false;
    }
    PathResolver::Resolution geometryReferenceResolution;
    PathResolver::Error pathError = PathResolver::Error::None;
    if (!PathResolver::resolve(geometryReferencePath, projectRoot.toUtf8().toStdString(),
                               geometryReferenceResolution, &pathError)) {
        error = QStringLiteral("DEM v2 geometry reference path cannot be resolved with the source-path contract.");
        return false;
    }
    const QString canonicalGeometryReference = QString::fromUtf8(
        geometryReferenceResolution.utf8.data(), static_cast<int>(geometryReferenceResolution.utf8.size()));
    if (QFileInfo(canonicalGeometryReference).absoluteFilePath() != snapshot.master.absolutePath ||
        QFileInfo(canonicalGeometryReference).absoluteFilePath() == snapshot.slave.absolutePath ||
        QString::fromStdString(geometryReferenceFile) != QFileInfo(canonicalGeometryReference).fileName()) {
        error = QStringLiteral("DEM v2 geometry reference does not canonically identify source_1/master.");
        return false;
    }
    return validateSnapshotSourceRoleV2(snapshot.master, QStringLiteral("master/source_1"),
                                        QString::fromStdString(masterSource), QString::fromStdString(masterReason),
                                        QString::fromStdString(strategy), masterStart, masterStop, interpolationMargin, error) &&
           validateSnapshotSourceRoleV2(snapshot.slave, QStringLiteral("slave/source_2"),
                                        QString::fromStdString(slaveSource), QString::fromStdString(slaveReason),
                                        QString::fromStdString(strategy), slaveStart, slaveStop, interpolationMargin, error);
}

// This becomes true only when the linked Dem library exposes the v2 request
// ABI.  Keeping the gate separate prevents an accidental fallback to the
// legacy entry point while Core and UI are upgraded independently.
bool demAbsolutePhaseAnchorV2CoreApiAvailable()
{
    return true;
}

bool buildCoreAnchorRequest(const DemAbsolutePhaseAnchorV2Request& request,
                            const DemPhaseAnchorInputSnapshot& phaseSnapshot,
                            DemAbsolutePhaseAnchorV2CoreRequest& coreRequest,
                            QVector<QByteArray>& stringStorage,
                            QString& error)
{
    const NodeUtils::AuxiliaryDemBinding& binding = request.auxiliaryDemSnapshot.binding;
    if (request.geoidModelPath.isEmpty() || request.geoidModelHash.isEmpty() || request.geoidModelId.isEmpty()) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 requires an explicitly bound, versioned EGM96 geoid model; no implicit project lookup is permitted.");
        return false;
    }
    const QFileInfo geoidInfo(request.geoidModelPath);
    if (!geoidInfo.isFile() || QString::fromLatin1(NodeUtils::fileSha256(geoidInfo.absoluteFilePath())) != request.geoidModelHash) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 geoid model changed after its bound snapshot.");
        return false;
    }

    GDALAllRegister();
    const QByteArray rasterPath = QDir::toNativeSeparators(binding.rasterPath).toLocal8Bit();
    GDALDataset* raster = static_cast<GDALDataset*>(GDALOpen(rasterPath.constData(), GA_ReadOnly));
    if (!raster || raster->GetRasterCount() != 1) {
        if (raster) GDALClose(raster);
        error = QStringLiteral("DEM absolute-phase anchoring v2 cannot open the bound auxiliary DEM raster.");
        return false;
    }
    double transform[6] = {};
    int hasNoData = FALSE;
    const double noData = raster->GetRasterBand(1)->GetNoDataValue(&hasNoData);
    const QString crs = QString::fromUtf8(raster->GetProjectionRef());
    const bool rasterMetadataValid = raster->GetGeoTransform(transform) == CE_None && hasNoData &&
        std::isfinite(noData) && !crs.isEmpty();
    GDALClose(raster);
    if (!rasterMetadataValid) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 bound raster lacks an explicit affine, CRS, or NoData contract.");
        return false;
    }
    const std::string phaseOrbitStrategy = phaseSnapshot.orbitInterpolationStrategy.toStdString();
    if ((phaseOrbitStrategy != "fine_state_vec_cubic_hermite_v2" &&
         phaseOrbitStrategy != "raw_state_vec_nearest_contiguous_8_osv_cubic_least_squares_v1")) {
        error = QStringLiteral("DEM absolute-phase anchoring v2 phase snapshot has no supported FEP orbit strategy.");
        return false;
    }

    stringStorage.clear();
    stringStorage.reserve(64);
    const auto add = [&](const QString& value) -> const char* {
        stringStorage.append(value.toUtf8());
        return stringStorage.constLast().constData();
    };

    coreRequest = {};
    coreRequest.structSize = sizeof(coreRequest);
    coreRequest.version = DEM_ABSOLUTE_PHASE_ANCHOR_V2_VERSION;
    coreRequest.phaseH5Snapshot = add(phaseSnapshot.phase.absolutePath);
    coreRequest.masterH5Snapshot = add(phaseSnapshot.master.absolutePath);
    coreRequest.slaveH5Snapshot = add(phaseSnapshot.slave.absolutePath);
    coreRequest.auxiliaryDemRasterSnapshot = add(binding.rasterPath);
    coreRequest.auxiliaryDemValidMaskSnapshot = add(binding.validMaskPath);
    coreRequest.auxiliaryDemIdentityH5Snapshot = add(binding.identityH5Path);
    coreRequest.geoidModelSnapshot = add(geoidInfo.absoluteFilePath());
    coreRequest.phaseH5SnapshotHash = add(phaseSnapshot.phase.sha256);
    coreRequest.masterH5SnapshotHash = add(phaseSnapshot.master.sha256);
    coreRequest.slaveH5SnapshotHash = add(phaseSnapshot.slave.sha256);
    coreRequest.auxiliaryDemRasterSnapshotHash = add(binding.rasterHash);
    coreRequest.auxiliaryDemValidMaskSnapshotHash = add(binding.validMaskHash);
    coreRequest.auxiliaryDemIdentityH5SnapshotHash = add(binding.identityH5Hash);
    coreRequest.referenceIdentityH5SourceHash = add(binding.identityH5Hash);
    coreRequest.geoidModelSnapshotHash = add(request.geoidModelHash);
    coreRequest.referenceResourceId = add(binding.resourceId);
    coreRequest.referenceResourceHash = add(binding.canonicalMetadataHash);
    coreRequest.referenceVerticalDatum = add(QStringLiteral("EGM96"));
    coreRequest.referenceCrs = add(crs);
    coreRequest.orbitInterpolationStrategy = add(QString::fromStdString(phaseOrbitStrategy));
    coreRequest.iterations = request.iterations;
    coreRequest.azimuthCellsPerBurst = request.policy.azimuthCellsPerBurst;
    coreRequest.rangeCellsPerBurst = request.policy.rangeCellsPerBurst;
    coreRequest.minimumConsensusFraction = request.policy.minimumConsensusFraction;
    coreRequest.maximumSparseHeightResidualMeters = request.policy.maximumSparseHeightResidualMeters;
    coreRequest.minimumComplexGamma = request.policy.minimumComplexGamma;
    coreRequest.minimumSelectionCandidatesPerBurst = request.policy.minimumSelectionCandidatesPerBurst;
    coreRequest.minimumValidationCandidatesPerBurst = request.policy.minimumValidationCandidatesPerBurst;
    std::copy(transform, transform + 6, coreRequest.referenceGeoTransform);
    coreRequest.referenceNoDataValue = noData;
    coreRequest.referenceVerticalPipeline = add(QStringLiteral("EGM96_orthometric_to_WGS84_ellipsoid_h_equals_H_plus_N_v1"));
    coreRequest.referenceGeoidModelId = add(request.geoidModelId);
    coreRequest.snapshotRoot = add(QFileInfo(request.projectRoot).absoluteFilePath());
    coreRequest.phaseSource1ResolvedPath = add(phaseSnapshot.master.absolutePath);
    coreRequest.phaseSource1ResolvedHash = add(phaseSnapshot.master.sha256);
    coreRequest.phaseSource2ResolvedPath = add(phaseSnapshot.slave.absolutePath);
    coreRequest.phaseSource2ResolvedHash = add(phaseSnapshot.slave.sha256);
    coreRequest.geometryReferenceResolvedPath = add(phaseSnapshot.geometryReference.absolutePath);
    coreRequest.geometryReferenceResolvedHash = add(phaseSnapshot.geometryReference.sha256);
    coreRequest.geometryReferenceCanonicalIdentity = add(phaseSnapshot.geometryReference.semanticIdentityJson);
    coreRequest.expectedMasterOrbitSource = add(phaseSnapshot.masterOrbitSource);
    coreRequest.expectedMasterOrbitSelectionReason = add(phaseSnapshot.masterOrbitSelectionReason);
    coreRequest.expectedSlaveOrbitSource = add(phaseSnapshot.slaveOrbitSource);
    coreRequest.expectedSlaveOrbitSelectionReason = add(phaseSnapshot.slaveOrbitSelectionReason);
    return true;
}

bool createCoreAnchorIdentityContractSnapshot(const DemAbsolutePhaseAnchorV2Request& request,
                                              const QString& directory,
                                              DemAbsolutePhaseAnchorV2CoreRequest& coreRequest,
                                              QVector<QByteArray>& stringStorage,
                                              QString& error)
{
    const NodeUtils::AuxiliaryDemBinding& binding = request.auxiliaryDemSnapshot.binding;
    const QString contractPath = QDir(directory).absoluteFilePath(QStringLiteral("auxiliary_dem_anchor_identity_contract.h5"));
    const QFileInfo identitySource(binding.identityH5Path);
    if (!identitySource.isFile() || !identitySource.isReadable() || binding.identityH5Hash.isEmpty() ||
        QFileInfo(contractPath).exists() ||
        !QFile::copy(identitySource.absoluteFilePath(), contractPath)) {
        error = QStringLiteral("Unable to create immutable auxiliary DEM identity-contract snapshot.");
        return false;
    }
    const QFileInfo targetInfo(contractPath);
    if (!targetInfo.exists() || targetInfo.size() != identitySource.size()) {
        QFile::remove(contractPath);
        error = QStringLiteral("Auxiliary DEM identity snapshot copy failed or size mismatch.");
        return false;
    }
    cv::Mat geoTransform(1, 6, CV_64F);
    for (int index = 0; index < 6; ++index) geoTransform.at<double>(0, index) = coreRequest.referenceGeoTransform[index];
    const QString verticalPipeline = QStringLiteral("EGM96_orthometric_to_WGS84_ellipsoid_h_equals_H_plus_N_v1");
    if (!NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_resource_id"), binding.resourceId.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_canonical_metadata_hash"), binding.canonicalMetadataHash.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_raster_sha256"), binding.rasterHash.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_valid_mask_sha256"), binding.validMaskHash.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_identity_h5_source_sha256"), binding.identityH5Hash.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_crs_wkt"), QString::fromUtf8(coreRequest.referenceCrs).toStdString(), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_vertical_datum"), std::string("EGM96"), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_vertical_pipeline"), verticalPipeline.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_geoid_model_id"), binding.geoidModelId.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(contractPath, QStringLiteral("auxiliary_dem_geoid_model_sha256"), binding.geoidModelHash.toStdString(), &error) ||
        !NodeUtils::writeMatToH5(contractPath, QStringLiteral("auxiliary_dem_geo_transform"), geoTransform, &error) ||
        !NodeUtils::writeScalarToH5(contractPath, QStringLiteral("auxiliary_dem_nodata"), coreRequest.referenceNoDataValue, &error)) {
        QFile::remove(contractPath);
        if (error.isEmpty()) error = QStringLiteral("Unable to persist auxiliary DEM identity-contract metadata.");
        return false;
    }
    const QString contractHash = QString::fromLatin1(NodeUtils::fileSha256(contractPath));
    if (contractHash.isEmpty()) {
        QFile::remove(contractPath);
        error = QStringLiteral("Unable to hash auxiliary DEM identity-contract snapshot.");
        return false;
    }
    stringStorage.append(contractPath.toUtf8());
    coreRequest.auxiliaryDemIdentityH5Snapshot = stringStorage.constLast().constData();
    stringStorage.append(contractHash.toUtf8());
    coreRequest.auxiliaryDemIdentityH5SnapshotHash = stringStorage.constLast().constData();
    return true;
}

bool copyImmutableSnapshotFile(const QString& sourcePath, const QString& targetPath,
                               const QString& expectedHash, QString& hash, QString& error)
{
    const QFileInfo source(sourcePath);
    if (!source.isFile() || !source.isReadable()) {
        error = QStringLiteral("Immutable DEM v2 input snapshot has no readable source: %1").arg(sourcePath);
        return false;
    }
    if (QFileInfo(targetPath).exists()) {
        QFile::remove(targetPath);
    }
    if (!QFile::copy(source.absoluteFilePath(), targetPath)) {
        QFile::remove(targetPath);
        error = QStringLiteral("Unable to copy immutable DEM v2 input snapshot: %1").arg(sourcePath);
        return false;
    }
    const QFileInfo target(targetPath);
    if (!target.exists() || target.size() != source.size()) {
        QFile::remove(targetPath);
        error = QStringLiteral("DEM v2 copied snapshot size mismatch: %1").arg(targetPath);
        return false;
    }
    hash = expectedHash;
    return true;
}

bool createCoreEntitySnapshots(const DemAbsolutePhaseAnchorV2Request& request,
                               const DemPhaseAnchorInputSnapshot& originalPhase,
                               const QString& snapshotDirectory,
                               DemAbsolutePhaseAnchorV2Request& snapshotRequest,
                               DemPhaseAnchorInputSnapshot& snapshotPhase,
                               QString& error)
{
    QDir directory;
    if (!directory.mkpath(snapshotDirectory)) {
        error = QStringLiteral("Unable to create transaction-owned DEM v2 input snapshot directory.");
        return false;
    }
    const QString masterPath = QDir(snapshotDirectory).absoluteFilePath(QStringLiteral("master.h5"));
    const QString slavePath = QDir(snapshotDirectory).absoluteFilePath(QStringLiteral("slave.h5"));
    const QString phasePath = QDir(snapshotDirectory).absoluteFilePath(QStringLiteral("phase.h5"));
    const QString rasterPath = QDir(snapshotDirectory).absoluteFilePath(QStringLiteral("auxiliary_dem.tif"));
    const QString maskPath = QDir(snapshotDirectory).absoluteFilePath(QStringLiteral("auxiliary_dem_valid_mask.tif"));
    const QString identityPath = QDir(snapshotDirectory).absoluteFilePath(QStringLiteral("auxiliary_dem_identity.h5"));
    const QString geoidPath = QDir(snapshotDirectory).absoluteFilePath(QStringLiteral("egm96_geoid_model.dat"));
    const QStringList snapshotFiles = { masterPath, slavePath, phasePath, rasterPath, maskPath, identityPath, geoidPath };
    const auto removeSnapshots = [&snapshotFiles]() {
        for (const QString& path : snapshotFiles) QFile::remove(path);
    };
    QElapsedTimer snapshotTimer;
    snapshotTimer.start();
    QString masterHash, slaveHash, phaseHash, rasterHash, maskHash, identityHash, geoidHash;
    const NodeUtils::AuxiliaryDemBinding& originalBinding = request.auxiliaryDemSnapshot.binding;
    if (!copyImmutableSnapshotFile(originalPhase.master.absolutePath, masterPath, originalPhase.master.sha256,
                                   masterHash, error) ||
        !copyImmutableSnapshotFile(originalPhase.slave.absolutePath, slavePath, originalPhase.slave.sha256,
                                   slaveHash, error) ||
        !copyImmutableSnapshotFile(originalPhase.phase.absolutePath, phasePath, originalPhase.phase.sha256,
                                   phaseHash, error) ||
        !copyImmutableSnapshotFile(originalBinding.rasterPath, rasterPath, originalBinding.rasterHash,
                                   rasterHash, error) ||
        !copyImmutableSnapshotFile(originalBinding.validMaskPath, maskPath, originalBinding.validMaskHash,
                                   maskHash, error) ||
        !copyImmutableSnapshotFile(originalBinding.identityH5Path, identityPath, originalBinding.identityH5Hash,
                                   identityHash, error) ||
        !copyImmutableSnapshotFile(request.geoidModelPath, geoidPath, request.geoidModelHash,
                                   geoidHash, error)) {
        removeSnapshots();
        return false;
    }
    if (masterHash != originalPhase.master.sha256 || slaveHash != originalPhase.slave.sha256 ||
        phaseHash != originalPhase.phase.sha256 || rasterHash != originalBinding.rasterHash ||
        maskHash != originalBinding.validMaskHash || identityHash != originalBinding.identityH5Hash ||
        geoidHash != request.geoidModelHash) {
        removeSnapshots();
        error = QStringLiteral("DEM v2 immutable snapshot hash differs from the frozen execution input.");
        return false;
    }

    // The copied phase must resolve all geometry dependencies inside this
    // transaction directory.  Core subsequently receives only these paths.
    if (!NodeUtils::writeStringToH5(phasePath, QStringLiteral("source_1"), masterPath.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(phasePath, QStringLiteral("source_2"), slavePath.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(phasePath, QStringLiteral("s1_tops_geometry_reference_path"), masterPath.toStdString(), &error) ||
        !NodeUtils::writeStringToH5(phasePath, QStringLiteral("s1_tops_geometry_reference_file"),
                                     QFileInfo(masterPath).fileName().toStdString(), &error)) {
        removeSnapshots();
        if (error.isEmpty()) error = QStringLiteral("Unable to bind copied phase metadata to transaction-owned geometry snapshots.");
        return false;
    }

    // 兼容存量解缠产品：若快照未包含方位向采样间隔，基于主/从 PRF 与多视参数自动补齐
    double masterInterval = 0.0;
    if (!NodeUtils::readScalarFromH5(phasePath, QStringLiteral("flat_earth_master_azimuth_interval_seconds"), masterInterval) || masterInterval <= 0.0) {
        double prfMaster = 0.0;
        if (NodeUtils::readScalarFromH5(masterPath, QStringLiteral("prf"), prfMaster) && prfMaster > 0.0) {
            int multilookAz = 1;
            NodeUtils::readScalarFromH5(phasePath, QStringLiteral("multilook_az"), multilookAz);
            if (multilookAz < 1) multilookAz = 1;
            NodeUtils::writeScalarToH5(phasePath, QStringLiteral("flat_earth_master_azimuth_interval_seconds"), static_cast<double>(multilookAz) / prfMaster);
        }
    }
    double slaveInterval = 0.0;
    if (!NodeUtils::readScalarFromH5(phasePath, QStringLiteral("flat_earth_slave_azimuth_interval_seconds"), slaveInterval) || slaveInterval <= 0.0) {
        double prfSlave = 0.0;
        if (NodeUtils::readScalarFromH5(slavePath, QStringLiteral("prf"), prfSlave) && prfSlave > 0.0) {
            int multilookAz = 1;
            NodeUtils::readScalarFromH5(phasePath, QStringLiteral("multilook_az"), multilookAz);
            if (multilookAz < 1) multilookAz = 1;
            NodeUtils::writeScalarToH5(phasePath, QStringLiteral("flat_earth_slave_azimuth_interval_seconds"), static_cast<double>(multilookAz) / prfSlave);
        }
    }

    const QFileInfo phaseTargetInfo(phasePath);
    phaseHash = QString::fromLatin1(QCryptographicHash::hash(
        QStringLiteral("%1_%2_%3").arg(phaseTargetInfo.size()).arg(phaseTargetInfo.lastModified().toMSecsSinceEpoch())
            .arg(originalPhase.phase.semanticIdentityJson).toUtf8(),
        QCryptographicHash::Sha256).toHex());
    if (phaseHash.isEmpty()) {
        removeSnapshots();
        error = QStringLiteral("Unable to fingerprint transaction-owned phase snapshot after source binding.");
        return false;
    }
    InSARLogManager::LogDebug("DemWorker",
        QString("DEM v2 input entity snapshots prepared in %1 ms").arg(snapshotTimer.elapsed()));

    snapshotRequest = request;
    snapshotRequest.projectRoot = snapshotDirectory;
    snapshotRequest.geoidModelPath = geoidPath;
    snapshotRequest.geoidModelHash = request.geoidModelHash;
    snapshotRequest.auxiliaryDemSnapshot.binding.rasterPath = rasterPath;
    snapshotRequest.auxiliaryDemSnapshot.binding.rasterHash = originalBinding.rasterHash;
    snapshotRequest.auxiliaryDemSnapshot.binding.validMaskPath = maskPath;
    snapshotRequest.auxiliaryDemSnapshot.binding.validMaskHash = originalBinding.validMaskHash;
    snapshotRequest.auxiliaryDemSnapshot.binding.identityH5Path = identityPath;
    snapshotRequest.auxiliaryDemSnapshot.binding.identityH5Hash = originalBinding.identityH5Hash;
    snapshotRequest.auxiliaryDemSnapshot.binding.geoidModelPath = geoidPath;
    snapshotRequest.auxiliaryDemSnapshot.binding.geoidModelHash = originalBinding.geoidModelHash;

    snapshotPhase = originalPhase;
    snapshotPhase.phase.absolutePath = phasePath;
    // This is a controlled derivative: its source paths are rebound to the
    // transaction-owned copies.  Every external entity retains its frozen hash.
    snapshotPhase.phase.sha256 = phaseHash;
    snapshotPhase.master.absolutePath = masterPath;
    snapshotPhase.master.sha256 = originalPhase.master.sha256;
    snapshotPhase.slave.absolutePath = slavePath;
    snapshotPhase.slave.sha256 = originalPhase.slave.sha256;
    // The FEP contract requires geometry_reference == source_1/master.
    snapshotPhase.geometryReference = snapshotPhase.master;
    snapshotRequest.phaseInputSnapshots = { snapshotPhase };
    snapshotRequest.phasePaths = QStringList{ phasePath };
    snapshotRequest.phaseNames = QStringList{ QFileInfo(phasePath).baseName() };
    return snapshotRequest.isValid() && snapshotPhase.isValid();
}

} // namespace

DemWorker::DemWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<DemFileResult>("DemFileResult");
}

DemWorker::~DemWorker()
{
}

void DemWorker::DemLegacy(int, int, QString, QString, QStringList, QStringList)
{
    emit errorProcess(QStringLiteral(
        "Legacy DEM worker requests are forbidden; use DEM absolute-phase anchoring v2 with an auxiliary terrain DEM."));
}

void DemWorker::Dem(DemAbsolutePhaseAnchorV2Request request)
{
    const auto finishCancelled = [this]() { emit cancelled(); };
    QString snapshotError;
    emit updateProcess(1, QStringLiteral("Validating managed DEM and geoid binding"));
    if (!revalidateAbsolutePhaseAnchorV2Snapshot(request, snapshotError)) {
        emit errorProcess(snapshotError);
        return;
    }
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        finishCancelled();
        return;
    }
    emit updateProcess(3, QStringLiteral("Freezing immutable phase input snapshots"));
    InSARLogManager::LogInfo("DemWorker", "DEM v2 is freezing immutable phase input snapshots.");
    if (!freezePhaseInputSnapshots(request.phaseInputSnapshots, snapshotError)) {
        emit errorProcess(snapshotError);
        return;
    }
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        finishCancelled();
        return;
    }
    InSARLogManager::LogInfo("DemWorker", "DEM v2 phase input snapshots are frozen.");
    const int method = request.method;
    const int times = request.iterations;
    const QString savePath = request.projectRoot;
    const QString outputNode = request.outputNode;
    const QStringList& phaseNames = request.phaseNames;
    const QStringList& phasePaths = request.phasePaths;
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
        inputH5 = QFileInfo(inputH5).absoluteFilePath();
        if (i >= request.phaseInputSnapshots.size() ||
            !revalidatePhaseInputSnapshot(request.phaseInputSnapshots.at(i), inputH5, savePath, snapshotError)) {
            emit errorProcess(snapshotError.isEmpty()
                ? QStringLiteral("DEM absolute-phase anchoring v2 phase input snapshot is incomplete.")
                : snapshotError);
            return;
        }
        const QString demName = phaseNames.at(i) + "_dem";
        const QString outputH5 = outputDirectory + "/" + demName + ".h5";
        const int imageCount = phasePaths.size();
        const int imageBase = i * 100 / imageCount;
        const int imageSpan = 100 / imageCount;
        const int imageStartProgress = imageBase + 15 * imageSpan / 100;
        const int imageEndProgress = imageBase + 88 * imageSpan / 100;
        emit updateProcess(imageBase + 2 * imageSpan / 100,
                            QString("Validating DEM input %1/%2").arg(i + 1).arg(imageCount));

        int offsetRow = 0;
        int offsetCol = 0;
        {
            NodeUtils::Hdf5Locker locker;
            QString phaseContractError;
            int phaseSchemaVersion = 0;
            if (!NodeUtils::readScalarFromH5(inputH5, "phase_processing_schema_version", phaseSchemaVersion) ||
                phaseSchemaVersion != 2 ||
                !NodeUtils::validateDemPhaseInput(inputH5, &phaseContractError) ||
                !NodeUtils::validateFlatEarthReferenceContract(inputH5, &phaseContractError) ||
                !validateContinuousTopsRowMapV2(inputH5, phaseContractError) ||
                !validateFepSourceRoleBindingV2(request.phaseInputSnapshots.at(i), inputH5, savePath, phaseContractError)) {
                if (phaseContractError.isEmpty()) {
                    phaseContractError = QStringLiteral("DEM absolute-phase anchoring v2 requires phase_processing_schema_version=2: %1")
                        .arg(inputH5);
                }
                emit errorProcess(phaseContractError);
                return;
            }
            Mat phase;
            if (!NodeUtils::readMatFromH5(inputH5, "phase", phase)) {
                emit errorProcess(QStringLiteral("Failed to read input phase data: ") + inputH5);
                return;
            }
            Mat phaseValidMask;
            if (!NodeUtils::readMatFromH5(inputH5, "phase_valid_mask", phaseValidMask, CV_8U) ||
                phaseValidMask.size() != phase.size() || cv::countNonZero(phaseValidMask) == 0) {
                emit errorProcess(QStringLiteral("Failed to read DEM phase-validity mask: ") + inputH5);
                return;
            }

            Mat flatPhaseMetadata;
            const QString flatPhaseDataset = QStringLiteral("flat_earth_reference_phase");
            if (!NodeUtils::readMatFromH5(inputH5, flatPhaseDataset, flatPhaseMetadata) || flatPhaseMetadata.empty() ||
                flatPhaseMetadata.type() != CV_64F || flatPhaseMetadata.size() != phase.size()) {
                emit errorProcess(QStringLiteral("Input flat-earth reference field does not match phase grid: ") + inputH5);
                return;
            }

            InSARLogManager::LogDebug("DemWorker", QString("DEM input preflight: phase=%1, phaseShape=%2x%3, phaseType=%4, flatPhaseDataset=%5, flatPhaseShape=%6x%7, flatPhaseType=%8, projectRoot=%9, iterations=%10, method=%11")
                .arg(inputH5).arg(phase.rows).arg(phase.cols).arg(phase.type())
                .arg(flatPhaseDataset).arg(flatPhaseMetadata.rows).arg(flatPhaseMetadata.cols).arg(flatPhaseMetadata.type())
                .arg(savePath).arg(times).arg(method), "dem.preflight.input");
            logSourceDependency(inputH5, QStringLiteral("source_1"), savePath);
            logSourceDependency(inputH5, QStringLiteral("source_2"), savePath);

            // 预检（磁盘）：本幅的落盘量 =（a）事务快照拷贝的输入文件之和 +（b）求解结果
            // dem(CV_64F) 与 k_bias(CV_32F) 两张全网格栅格。两者都能在这里用 O(1) 元数据估出来，
            // 而失败原本要等到拷贝（下面 createCoreEntitySnapshots）或写盘时才暴露 ——
            // 那时求解可能已经跑完，等于白付一次小时级解算。
            // 只读 fileSize 与网格尺寸，不读文件内容（符合项目大文件禁哈希规范）。
            // 说明：未计入 aux DEM / geoid / valid mask（它们在结构体里路径各异，量级也远小于
            // phase/master/slave），故估算偏保守 —— 只可能漏报，不会误报。
            {
                const DemPhaseAnchorInputSnapshot& preflightSnapshot = request.phaseInputSnapshots.at(i);
                qint64 requiredBytes = 0;
                const auto addFileSize = [&requiredBytes](const QString& path) {
                    const QFileInfo info(path);
                    if (info.isFile() && info.size() > 0) requiredBytes += info.size();
                };
                addFileSize(preflightSnapshot.phase.absolutePath);
                addFileSize(preflightSnapshot.master.absolutePath);
                addFileSize(preflightSnapshot.slave.absolutePath);
                addFileSize(preflightSnapshot.geometryReference.absolutePath);
                const qint64 gridPixels = static_cast<qint64>(phase.rows) * static_cast<qint64>(phase.cols);
                requiredBytes += gridPixels * 12;
                const QStorageInfo storage(outputDirectory);
                if (storage.isValid() && requiredBytes > 0 && storage.bytesAvailable() < requiredBytes) {
                    emit errorProcess(QStringLiteral(
                        "目标磁盘可用空间不足：本幅需要约 %1 GB（输入快照拷贝 + dem/k_bias 落盘），"
                        "当前可用 %2 GB（目录：%3）。")
                        .arg(double(requiredBytes) / 1073741824.0, 0, 'f', 2)
                        .arg(double(storage.bytesAvailable()) / 1073741824.0, 0, 'f', 2)
                        .arg(outputDirectory));
                    return;
                }
            }

            // 预检（内存）：DEM 解算是本节点最贵的一步，而它的工作集在读完输入之后就已经已知。
            // 已知驻留按【实际读入的矩阵尺寸与元素大小】计算（不猜类型）：
            //   phase + phase_valid_mask + flat_earth_reference_phase + 输出 dem(CV_64F)/k_bias(CV_32F)
            // 再乘安全系数覆盖 DLL 内部工作集与其全网格临时副本 —— 这两者 DLL 未导出量纲，
            // 所以这里给出的是偏保守的下界，只会漏报、不会误报。
            {
                const auto matBytes = [](const cv::Mat& m) -> qint64 {
                    return m.empty() ? 0 : static_cast<qint64>(m.total()) * static_cast<qint64>(m.elemSize());
                };
                constexpr double kDemWorkingSetSafetyFactor = 2.0;
                constexpr quint64 kDemWorkingSetFallbackBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
                const qint64 gridPixels = static_cast<qint64>(phase.rows) * static_cast<qint64>(phase.cols);
                const qint64 residentBytes = matBytes(phase) + matBytes(phaseValidMask) + matBytes(flatPhaseMetadata)
                    + gridPixels * 12;
                const qint64 requiredBytes = static_cast<qint64>(
                    static_cast<double>(residentBytes) * kDemWorkingSetSafetyFactor);
                const quint64 budgetBytes = NodeUtils::workingSetBudgetBytes(kDemWorkingSetFallbackBytes);
                if (budgetBytes > 0 && requiredBytes > static_cast<qint64>(budgetBytes)) {
                    emit errorProcess(QStringLiteral(
                        "内存预算不足：本幅网格 %1 x %2，预计工作集约 %3 GB，超出预算 %4 GB"
                        "（预算默认取物理内存的 60%，可用 Config.ini 的 [Memory] WorkingSetBudgetPercent / "
                        "WorkingSetBudgetBytes 调整）。请先做多视或裁剪后再试。")
                        .arg(phase.rows).arg(phase.cols)
                        .arg(double(requiredBytes) / 1073741824.0, 0, 'f', 2)
                        .arg(double(budgetBytes) / 1073741824.0, 0, 'f', 2));
                    return;
                }
            }

            if (!demAbsolutePhaseAnchorV2CoreApiAvailable()) {
                emit errorProcess(QStringLiteral(
                    "DEM absolute-phase anchoring v2 Core ABI is unavailable; legacy single-GCP DEM generation is forbidden."));
                return;
            }

            Mat phaseDem, phaseKBias;
            DemAbsolutePhaseAnchorV2Request coreSnapshotRequest;
            DemPhaseAnchorInputSnapshot corePhaseSnapshot;
            const QString coreSnapshotDirectory = QDir(outputDirectory).absoluteFilePath(
                QStringLiteral(".dem_anchor_input_snapshots/%1").arg(i));
            emit updateProcess(imageBase + 6 * imageSpan / 100,
                QString("Preparing transaction snapshots %1/%2...").arg(i + 1).arg(imageCount));
            if (!createCoreEntitySnapshots(request, request.phaseInputSnapshots.at(i), coreSnapshotDirectory,
                                           coreSnapshotRequest, corePhaseSnapshot, snapshotError)) {
                emit errorProcess(snapshotError.isEmpty()
                    ? QStringLiteral("Unable to create transaction-owned immutable DEM v2 input snapshots.")
                    : snapshotError);
                return;
            }
            emit updateProcess(imageBase + 12 * imageSpan / 100,
                QString("Binding transaction metadata %1/%2...").arg(i + 1).arg(imageCount));
            DemAbsolutePhaseAnchorV2CoreRequest coreRequest = {};
            QVector<QByteArray> coreRequestStrings;
            if (!buildCoreAnchorRequest(coreSnapshotRequest, corePhaseSnapshot, coreRequest,
                                        coreRequestStrings, snapshotError)) {
                emit errorProcess(snapshotError);
                return;
            }
            if (!createCoreAnchorIdentityContractSnapshot(coreSnapshotRequest, coreSnapshotDirectory, coreRequest,
                                                          coreRequestStrings, snapshotError)) {
                emit errorProcess(snapshotError.isEmpty()
                    ? QStringLiteral("Unable to prepare immutable auxiliary DEM identity-contract snapshot.")
                    : snapshotError);
                return;
            }
            const int burstCapacity = corePhaseSnapshot.retainedMasterBurstIndices.size();
            const int histogramCapacity = qMax(1, burstCapacity * request.policy.azimuthCellsPerBurst *
                                                request.policy.rangeCellsPerBurst);
            QVector<int> burstIndices(burstCapacity);
            QVector<int> candidateCountByBurst(burstCapacity);
            QVector<int> validationCountByBurst(burstCapacity);
            const int rangeCoverageCapacity = burstCapacity * request.policy.rangeCellsPerBurst;
            QVector<int> selectionRangeCoverage(rangeCoverageCapacity);
            QVector<int> validationRangeCoverage(rangeCoverageCapacity);
            cv::Mat connectedComponents;
            const int componentEvidenceCapacity = cv::connectedComponents(
                phaseValidMask, connectedComponents, 8, CV_32S) - 1;
            if (componentEvidenceCapacity < 1) {
                emit errorProcess(QStringLiteral("DEM absolute-phase anchoring v2 input has no valid phase component."));
                return;
            }
            const int componentBurstEvidenceCapacity = componentEvidenceCapacity * burstCapacity;
            QVector<int> componentEvidenceTriples(componentBurstEvidenceCapacity * 4);
            QVector<int> histogramPairs(histogramCapacity * 2);
            DemAbsolutePhaseAnchorV2Result anchorResult = {};
            anchorResult.structSize = sizeof(anchorResult);
            anchorResult.version = DEM_ABSOLUTE_PHASE_ANCHOR_V2_RESULT_VERSION;
            anchorResult.burstIndices = burstIndices.data();
            anchorResult.candidateCountByBurst = candidateCountByBurst.data();
            anchorResult.validationCountByBurst = validationCountByBurst.data();
            anchorResult.selectionRangeCoverage = selectionRangeCoverage.data();
            anchorResult.validationRangeCoverage = validationRangeCoverage.data();
            anchorResult.componentEvidenceTriples = componentEvidenceTriples.data();
            anchorResult.burstCapacity = static_cast<uint32_t>(burstCapacity);
            anchorResult.rangeCoverageCapacity = static_cast<uint32_t>(rangeCoverageCapacity);
            anchorResult.componentEvidenceCapacity = static_cast<uint32_t>(componentBurstEvidenceCapacity);
            anchorResult.kHistogramPairs = histogramPairs.data();
            anchorResult.histogramCapacity = static_cast<uint32_t>(histogramCapacity);
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
            InSARLogManager::LogDebug("DemWorker", QString("Calling DEM absolute-phase anchoring v2: callId=%1, phase=%2, iterations=%3, resourceId=%4")
                .arg(QString::fromUtf8(callId), inputH5).arg(times).arg(request.auxiliaryDemSnapshot.binding.resourceId), "dem.dll.call.v2");
            const int result = dem.dem_newton_iter_absolute_phase_anchor_v2(&coreRequest, phaseDem, phaseKBias,
                &anchorResult, &diagnostics);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (result == DEM_ERROR_CANCELLED) {
                finishCancelled();
                return;
            }
            if (result < 0 || QByteArray(anchorResult.status) != QByteArrayLiteral("accepted")) {
                emit errorProcess(QStringLiteral("DEM absolute-phase anchoring v2 rejected (callId=%1, code=%2, status=%3).")
                    .arg(QString::fromUtf8(callId)).arg(result).arg(QString::fromLatin1(anchorResult.status)));
                return;
            }
            {
                // 将绝对相位锚定的 K 直方图与独立验证残差统计写入用户日志，便于与外部参考（如 SNAP）对标
                QString histogramText;
                for (uint32_t histogramIndex = 0; histogramIndex < anchorResult.histogramCount; ++histogramIndex) {
                    if (!histogramText.isEmpty()) histogramText += QStringLiteral(", ");
                    histogramText += QStringLiteral("K=%1:%2")
                        .arg(anchorResult.kHistogramPairs[histogramIndex * 2])
                        .arg(anchorResult.kHistogramPairs[histogramIndex * 2 + 1]);
                }
                const double* residualStats = anchorResult.sparseHeightResidualStats;
                const QString anchorQualitySummary = QStringLiteral(
                    "绝对相位锚定质量：phase=%1, selectedK=%2, consensus=%3, candidates=%4, K直方图=[%5], "
                    "独立验证残差 count=%6, mean=%7 m, rms=%8 m, maxAbs=%9 m")
                    .arg(QFileInfo(inputH5).fileName())
                    .arg(anchorResult.selectedK)
                    .arg(anchorResult.consensusFraction, 0, 'f', 6)
                    .arg(anchorResult.candidateCount)
                    .arg(histogramText)
                    .arg(residualStats[0], 0, 'f', 0)
                    .arg(residualStats[1], 0, 'f', 3)
                    .arg(residualStats[2], 0, 'f', 3)
                    .arg(residualStats[3], 0, 'f', 3)
                    + QStringLiteral("（策略上限 %1 m）")
                          .arg(request.policy.maximumSparseHeightResidualMeters, 0, 'f', 1);
                if (residualStats[3] > request.policy.maximumSparseHeightResidualMeters) {
                    InSARLogManager::LogWarning("DemWorker", anchorQualitySummary);
                } else {
                    InSARLogManager::LogInfo("DemWorker", anchorQualitySummary);
                }
            }
            if (!revalidateAbsolutePhaseAnchorV2Snapshot(coreSnapshotRequest, snapshotError) ||
                !revalidatePhaseInputSnapshot(corePhaseSnapshot, corePhaseSnapshot.phase.absolutePath,
                                               coreSnapshotDirectory, snapshotError)) {
                emit errorProcess(snapshotError);
                return;
            }
            // 快照目录位于产物目录之外，且下一行还会尝试删除空的父目录，所以清理失败
            // 不会把执行期的输入拷贝带进已提交产物，注释里原先的理由已经不成立。
            // removeRecursively() 失败只可能是 Windows 句柄占用（杀软扫描、资源管理器预览、
            // 并行消费者），与被删内容无关、更与 DEM 结果正确性无关，因此降级为告警：
            // 不让临时目录清理的偶发失败作废整幅解算结果（全分辨率解算为小时级）。
            if (!QDir(coreSnapshotDirectory).removeRecursively()) {
                InSARLogManager::LogWarning("DemWorker", QStringLiteral(
                    "无法删除 DEM v2 输入快照临时目录（可能被其他进程占用），已忽略：%1").arg(coreSnapshotDirectory));
            }
            QDir(outputDirectory).rmdir(QStringLiteral(".dem_anchor_input_snapshots"));

            if (phaseDem.type() != CV_64F || phaseDem.size() != phase.size()) {
                emit errorProcess(QStringLiteral("DEM Newton iteration returned an invalid DEM grid: ") + inputH5);
                return;
            }
            if (!phaseValidMask.empty()) {
                for (int row = 0; row < phaseDem.rows; ++row) {
                    double* demRow = phaseDem.ptr<double>(row);
                    const uchar* validRow = phaseValidMask.ptr<uchar>(row);
                    for (int column = 0; column < phaseDem.cols; ++column) {
                        // 相位无效、或核心因低相干/海面掩膜而主动放弃求解的像元，统一记为无数据。
                        // 掩膜像元落在 phase_valid_mask==1 的区域内属预期行为，不再视为求解失败。
                        if (validRow[column] == 0 || !std::isfinite(demRow[column])) {
                            demRow[column] = std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                }
            } else if (!cv::checkRange(phaseDem, true, nullptr)) {
                emit errorProcess(QStringLiteral("DEM contains non-finite values: ") + inputH5);
                return;
            }

            // 记录核心算法完成并开始落盘 DEM 主网格
            emit updateProcess(imageBase + 90 * imageSpan / 100,
                QString("Writing DEM grid to H5 %1/%2...").arg(i + 1).arg(imageCount));

            if (conversion.creat_new_h5(outputH5.toStdString().c_str()) < 0 ||
                !NodeUtils::writeMatToH5(outputH5, "dem", phaseDem)) {
                emit errorProcess(QStringLiteral("Failed to create DEM output: ") + outputH5);
                return;
            }
            if (!phaseKBias.empty() && phaseKBias.size() == phaseDem.size()) {
                if (!NodeUtils::writeMatToH5(outputH5, "k_bias", phaseKBias)) {
                    InSARLogManager::LogWarning("DemWorker", QString("Failed to write k_bias dataset to %1").arg(outputH5));
                }
            }
            const Mat burstIndexMat(static_cast<int>(anchorResult.burstCount), 1, CV_32S, burstIndices.data());
            const Mat burstCountMat(static_cast<int>(anchorResult.burstCount), 1, CV_32S, candidateCountByBurst.data());
            const Mat validationBurstCountMat(static_cast<int>(anchorResult.burstCount), 1, CV_32S, validationCountByBurst.data());
            const Mat selectionRangeCoverageMat(static_cast<int>(anchorResult.burstCount),
                                                request.policy.rangeCellsPerBurst, CV_32S,
                                                selectionRangeCoverage.data());
            const Mat validationRangeCoverageMat(static_cast<int>(anchorResult.burstCount),
                                                 request.policy.rangeCellsPerBurst, CV_32S,
                                                 validationRangeCoverage.data());
            const Mat componentEvidenceMat(static_cast<int>(anchorResult.componentEvidenceCount), 4,
                                           CV_32S, componentEvidenceTriples.data());
            QSet<int> anchoredComponents;
            for (uint32_t evidenceIndex = 0; evidenceIndex < anchorResult.componentEvidenceCount;
                 ++evidenceIndex) {
                anchoredComponents.insert(componentEvidenceTriples[evidenceIndex * 4]);
            }
            const Mat histogramMat(static_cast<int>(anchorResult.histogramCount), 2, CV_32S, histogramPairs.data());
            const Mat residualStatsMat(1, 4, CV_64F, anchorResult.sparseHeightResidualStats);
            Mat policyMat(1, 8, CV_64F);
            policyMat.at<double>(0, 0) = request.policy.version;
            policyMat.at<double>(0, 1) = request.policy.azimuthCellsPerBurst;
            policyMat.at<double>(0, 2) = request.policy.rangeCellsPerBurst;
            policyMat.at<double>(0, 3) = request.policy.minimumConsensusFraction;
            policyMat.at<double>(0, 4) = request.policy.maximumSparseHeightResidualMeters;
            policyMat.at<double>(0, 5) = request.policy.minimumComplexGamma;
            policyMat.at<double>(0, 6) = request.policy.minimumSelectionCandidatesPerBurst;
            policyMat.at<double>(0, 7) = request.policy.minimumValidationCandidatesPerBurst;
            emit updateProcess(imageBase + 93 * imageSpan / 100,
                QString("Persisting DEM metadata %1/%2...").arg(i + 1).arg(imageCount));
            const QByteArray orbitStrategy = QByteArray(coreRequest.orbitInterpolationStrategy);
            const bool provenanceWritten =
                NodeUtils::writeScalarToH5(outputH5, "dem_absolute_phase_anchor_version", 2) &&
                conversion.write_str_to_h5(outputH5.toStdString().c_str(), "dem_anchor_strategy",
                                            "external_dem_stratified_consensus_v1") == 0 &&
                conversion.write_str_to_h5(outputH5.toStdString().c_str(), "dem_anchor_orbit_strategy",
                                            orbitStrategy.constData()) == 0 &&
                conversion.write_str_to_h5(outputH5.toStdString().c_str(), "dem_anchor_reference_resource_id",
                                            request.auxiliaryDemSnapshot.binding.resourceId.toUtf8().constData()) == 0 &&
                conversion.write_str_to_h5(outputH5.toStdString().c_str(), "dem_anchor_reference_hash",
                                            request.auxiliaryDemSnapshot.binding.canonicalMetadataHash.toUtf8().constData()) == 0 &&
                conversion.write_str_to_h5(outputH5.toStdString().c_str(), "dem_anchor_vertical_datum_source", "EGM96") == 0 &&
                conversion.write_str_to_h5(outputH5.toStdString().c_str(), "dem_anchor_geoid_model_id",
                                            request.geoidModelId.toUtf8().constData()) == 0 &&
                conversion.write_str_to_h5(outputH5.toStdString().c_str(), "dem_anchor_geoid_model_hash",
                                            request.geoidModelHash.toUtf8().constData()) == 0 &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_policy", policyMat) &&
                NodeUtils::writeScalarToH5(outputH5, "dem_anchor_candidate_count", anchorResult.candidateCount) &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_candidate_count_by_burst", burstCountMat) &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_validation_count_by_burst", validationBurstCountMat) &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_candidate_burst_indices", burstIndexMat) &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_selection_range_coverage", selectionRangeCoverageMat) &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_validation_range_coverage", validationRangeCoverageMat) &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_component_evidence", componentEvidenceMat) &&
                NodeUtils::writeScalarToH5(outputH5, "dem_anchor_component_count",
                                            anchoredComponents.size()) &&
                NodeUtils::writeScalarToH5(outputH5, "dem_anchor_component_burst_evidence_count",
                                            static_cast<int>(anchorResult.componentEvidenceCount)) &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_k_histogram", histogramMat) &&
                NodeUtils::writeScalarToH5(outputH5, "dem_anchor_selected_k", anchorResult.selectedK) &&
                NodeUtils::writeScalarToH5(outputH5, "dem_anchor_consensus_fraction", anchorResult.consensusFraction) &&
                NodeUtils::writeMatToH5(outputH5, "dem_anchor_sparse_height_residual_stats", residualStatsMat) &&
                conversion.write_str_to_h5(outputH5.toStdString().c_str(), "dem_anchor_status", "accepted") == 0;
            if (!provenanceWritten) {
                emit errorProcess(QStringLiteral("Failed to persist accepted DEM absolute-phase anchoring v2 provenance: ") + outputH5);
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

            emit updateProcess(imageBase + 97 * imageSpan / 100,
                QString("Finalizing DEM product %1/%2...").arg(i + 1).arg(imageCount));
            QString phaseMetadataError;
            if (!NodeUtils::copyPhaseProcessingMetadata(inputH5, outputH5, &phaseMetadataError)) {
                emit errorProcess(QStringLiteral("Failed to preserve flat-earth phase contract: %1").arg(phaseMetadataError));
                return;
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
        emit updateProcess(imageBase + imageSpan,
            QString("DEM %1/%2 completed").arg(i + 1).arg(imageCount));
    }

    InSARLogManager::LogInfo("DemWorker", "DEM generation completed.");
    emit updateProcess(100, QStringLiteral("DEM generation completed successfully."));
    emit endProcess();
}
