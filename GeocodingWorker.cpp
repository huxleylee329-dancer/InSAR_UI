#include "GeocodingWorker.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "Package.h"
#include <FormatConversion.h>
#include <Hdf5IO.h>
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
#include <vector>
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

static bool __stdcall geocodingProgressCallback(int progress, const char* message);

struct SourceRowRun
{
    int outputFirstRow = 0;
    int sourceFirstRow = 0;
    int rowCount = 0;
};

struct CommonCoverageRowMapping
{
    bool applies = false;
    QString signature;
    QString sourceFrameMapping;
    QString geometryReferenceFile;
    QString mapSemantics;
    int sourceRowCount = 0;
    int sourceRowOrigin = 0;
    int commonFirstBurst = 0;
    int commonLastBurst = 0;
    int commonBurstCount = 0;
    int mapAzimuthFactor = 1;
    cv::Mat sourceRowMap;
    cv::Mat sourceRowRanges;
    std::vector<SourceRowRun> runs;
};

static bool loadCommonCoverageRowMapping(const QString& h5Path,
                                         int expectedOutputRows,
                                         CommonCoverageRowMapping& mapping,
                                         QString& error)
{
    mapping = CommonCoverageRowMapping();
    FormatConversion conversion;
    NodeUtils::Hdf5Locker locker(h5Path.toStdString());
    if (!locker.isLocked()) {
        error = QStringLiteral("Unable to lock geocoding source H5: %1").arg(h5Path);
        return false;
    }
    int contractExists = 0;
    const QByteArray h5Utf8 = h5Path.toUtf8();
    if (Hdf5IO::datasetExists(h5Utf8.constData(), "s1_tops_product_contract", &contractExists) != 0) {
        error = QStringLiteral("Unable to inspect geocoding product contract: %1").arg(h5Path);
        return false;
    }
    if (contractExists == 0) {
        // Dataset absence is the only historical ordinary-product fallback.
        return true;
    }

    std::string contract;
    std::string signature;
    std::string sourceFrameMapping;
    std::string geometryReferenceFile;
    std::string mapSemantics;
    if (conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_product_contract", contract) != 0 ||
        QString::fromStdString(contract) != QStringLiteral("continuous_deburst_common_coverage_v1") ||
        conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_coverage_signature", signature) != 0 ||
        conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_source_frame_mapping", sourceFrameMapping) != 0 ||
        conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_geometry_reference_file", geometryReferenceFile) != 0 ||
        conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_output_source_row_map_semantics", mapSemantics) != 0) {
        error = QStringLiteral("Common-coverage product contract is missing, damaged, or unknown: %1").arg(h5Path);
        return false;
    }

    mapping.signature = QString::fromStdString(signature);
    mapping.sourceFrameMapping = QString::fromStdString(sourceFrameMapping);
    mapping.geometryReferenceFile = QString::fromStdString(geometryReferenceFile);
    mapping.mapSemantics = QString::fromStdString(mapSemantics);
    if (conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_source_full_burst_row_count", &mapping.sourceRowCount) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_output_source_row_origin", &mapping.sourceRowOrigin) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_common_master_first_burst", &mapping.commonFirstBurst) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_common_master_last_burst", &mapping.commonLastBurst) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_common_master_burst_count", &mapping.commonBurstCount) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_output_source_row_map_multilook_azimuth_factor", &mapping.mapAzimuthFactor) != 0 ||
        conversion.read_array_from_h5(h5Path.toStdString().c_str(), "s1_tops_output_source_row_map", mapping.sourceRowMap) != 0 ||
        conversion.read_array_from_h5(h5Path.toStdString().c_str(), "s1_tops_retained_source_row_ranges", mapping.sourceRowRanges) != 0 ||
        mapping.signature.isEmpty() || mapping.sourceFrameMapping.isEmpty() || mapping.geometryReferenceFile.isEmpty() ||
        mapping.sourceRowMap.type() != CV_32S || mapping.sourceRowMap.rows != expectedOutputRows || mapping.sourceRowMap.cols != 1 ||
        mapping.sourceRowMap.empty() || mapping.sourceRowCount <= 0 || mapping.sourceRowOrigin < 0 ||
        mapping.sourceRowOrigin >= mapping.sourceRowCount || mapping.commonFirstBurst < 1 ||
        mapping.commonLastBurst < mapping.commonFirstBurst ||
        mapping.commonBurstCount != mapping.commonLastBurst - mapping.commonFirstBurst + 1 ||
        mapping.sourceRowRanges.type() != CV_32S || mapping.sourceRowRanges.rows != mapping.commonBurstCount ||
        mapping.sourceRowRanges.cols != 2 || mapping.mapAzimuthFactor < 1 ||
        (mapping.mapSemantics != QStringLiteral("full_deburst_source_row_v1") &&
         mapping.mapSemantics != QStringLiteral("multilook_azimuth_block_center_v1")) ||
        (mapping.mapSemantics == QStringLiteral("full_deburst_source_row_v1") && mapping.mapAzimuthFactor != 1) ||
        (mapping.mapSemantics == QStringLiteral("multilook_azimuth_block_center_v1") && mapping.mapAzimuthFactor <= 1)) {
        error = QStringLiteral("Common-coverage source-row map is missing or invalid: %1").arg(h5Path);
        return false;
    }

    mapping.applies = true;
    int runStart = 0;
    for (int row = 0; row < mapping.sourceRowMap.rows; ++row) {
        const int sourceRow = mapping.sourceRowMap.at<int>(row, 0);
        if (sourceRow < 0 || sourceRow >= mapping.sourceRowCount ||
            (row > 0 && sourceRow <= mapping.sourceRowMap.at<int>(row - 1, 0))) {
            error = QStringLiteral("Common-coverage source-row map is not strictly increasing: %1").arg(h5Path);
            return false;
        }
        if (row > 0 && sourceRow != mapping.sourceRowMap.at<int>(row - 1, 0) + 1) {
            SourceRowRun completedRun;
            completedRun.outputFirstRow = runStart;
            completedRun.sourceFirstRow = mapping.sourceRowMap.at<int>(runStart, 0);
            completedRun.rowCount = row - runStart;
            mapping.runs.push_back(completedRun);
            runStart = row;
        }
    }
    SourceRowRun finalRun;
    finalRun.outputFirstRow = runStart;
    finalRun.sourceFirstRow = mapping.sourceRowMap.at<int>(runStart, 0);
    finalRun.rowCount = mapping.sourceRowMap.rows - runStart;
    mapping.runs.push_back(finalRun);

    for (int segment = 0; segment < mapping.commonBurstCount; ++segment) {
        const int rangeStart = mapping.sourceRowRanges.at<int>(segment, 0);
        const int rangeEnd = mapping.sourceRowRanges.at<int>(segment, 1);
        if (rangeStart < 0 || rangeEnd <= rangeStart || rangeEnd > mapping.sourceRowCount) {
            error = QStringLiteral("Common-coverage source-row range is incomplete or out of bounds: %1").arg(h5Path);
            return false;
        }
    }
    for (int row = 0; row < mapping.sourceRowMap.rows; ++row) {
        const int sourceRow = mapping.sourceRowMap.at<int>(row, 0);
        bool inRetainedRange = false;
        for (int segment = 0; segment < mapping.commonBurstCount; ++segment) {
            if (sourceRow >= mapping.sourceRowRanges.at<int>(segment, 0) &&
                sourceRow < mapping.sourceRowRanges.at<int>(segment, 1)) {
                inRetainedRange = true;
                break;
            }
        }
        if (!inRetainedRange) {
            error = QStringLiteral("Common-coverage source-row map is outside retained ranges: %1").arg(h5Path);
            return false;
        }
    }
    if (mapping.sourceRowMap.at<int>(0, 0) != mapping.sourceRowOrigin) {
        error = QStringLiteral("Common-coverage source-row origin does not match map: %1").arg(h5Path);
        return false;
    }
    return true;
}

static bool inspectH5DatasetPresence(const QString& h5Path,
                                     const char* dataset,
                                     bool& present,
                                     QString& error)
{
    present = false;
    const QByteArray h5Utf8 = h5Path.toUtf8();
    int exists = 0;
    if (Hdf5IO::datasetExists(h5Utf8.constData(), dataset, &exists) != 0) {
        error = QStringLiteral("Unable to inspect dataset %1 in %2")
                    .arg(QString::fromLatin1(dataset), h5Path);
        return false;
    }
    present = exists != 0;
    return true;
}

static bool commonCoverageMappingsMatch(const CommonCoverageRowMapping& current,
                                        const CommonCoverageRowMapping& reference,
                                        const QString& inputPath,
                                        QString& error)
{
    if (!current.applies || !reference.applies ||
        current.signature != reference.signature ||
        current.sourceFrameMapping != reference.sourceFrameMapping ||
        current.geometryReferenceFile != reference.geometryReferenceFile ||
        current.sourceRowCount != reference.sourceRowCount ||
        current.commonFirstBurst != reference.commonFirstBurst ||
        current.commonLastBurst != reference.commonLastBurst ||
        current.commonBurstCount != reference.commonBurstCount ||
        current.sourceRowRanges.size() != reference.sourceRowRanges.size() ||
        current.sourceRowRanges.type() != reference.sourceRowRanges.type() ||
        cv::countNonZero(current.sourceRowRanges != reference.sourceRowRanges) != 0) {
        error = QStringLiteral("Common-coverage geometry reference contract/signature/source mapping/ranges do not match: %1")
                    .arg(inputPath);
        return false;
    }

    if (current.mapAzimuthFactor < reference.mapAzimuthFactor ||
        current.mapAzimuthFactor % reference.mapAzimuthFactor != 0) {
        error = QStringLiteral("Common-coverage source-row map azimuth factor is incompatible with its geometry reference: %1")
                    .arg(inputPath);
        return false;
    }
    if (current.mapAzimuthFactor == reference.mapAzimuthFactor) {
        if (current.mapSemantics != reference.mapSemantics ||
            current.sourceRowMap.size() != reference.sourceRowMap.size() ||
            current.sourceRowMap.type() != reference.sourceRowMap.type() ||
            cv::countNonZero(current.sourceRowMap != reference.sourceRowMap) != 0) {
            error = QStringLiteral("Common-coverage source-row map does not match its geometry reference: %1")
                        .arg(inputPath);
            return false;
        }
        return true;
    }

    if (current.mapSemantics != QStringLiteral("multilook_azimuth_block_center_v1")) {
        error = QStringLiteral("Common-coverage reduced source-row map has unknown semantics: %1")
                    .arg(inputPath);
        return false;
    }
    const int factorRatio = current.mapAzimuthFactor / reference.mapAzimuthFactor;
    std::vector<int> expectedMap;
    expectedMap.reserve(static_cast<size_t>(current.sourceRowMap.rows));
    for (const SourceRowRun& run : reference.runs) {
        const int outputRows = run.rowCount / factorRatio;
        for (int block = 0; block < outputRows; ++block) {
            const int representativeRow = run.outputFirstRow +
                block * factorRatio + factorRatio / 2;
            if (representativeRow < run.outputFirstRow ||
                representativeRow >= run.outputFirstRow + run.rowCount ||
                representativeRow >= reference.sourceRowMap.rows) {
                error = QStringLiteral("Common-coverage geometry reference map cannot derive the reduced source-row map: %1")
                            .arg(inputPath);
                return false;
            }
            expectedMap.push_back(reference.sourceRowMap.at<int>(representativeRow, 0));
        }
    }
    if (static_cast<int>(expectedMap.size()) != current.sourceRowMap.rows) {
        error = QStringLiteral("Common-coverage reduced source-row map length does not match its geometry reference: %1")
                    .arg(inputPath);
        return false;
    }
    for (int row = 0; row < current.sourceRowMap.rows; ++row) {
        if (current.sourceRowMap.at<int>(row, 0) != expectedMap[static_cast<size_t>(row)]) {
            error = QStringLiteral("Common-coverage reduced source-row map is inconsistent with its geometry reference: %1")
                        .arg(inputPath);
            return false;
        }
    }
    return true;
}

static bool resolveCommonCoverageGeometryReference(const QString& h5Path,
                                                   QString& geometryH5Path,
                                                   QString& error)
{
    geometryH5Path = h5Path;
    FormatConversion conversion;
    std::string contract;
    NodeUtils::Hdf5Locker locker(h5Path.toStdString());
    if (!locker.isLocked()) {
        error = QStringLiteral("Unable to lock geocoding source H5: %1").arg(h5Path);
        return false;
    }
    int contractExists = 0;
    const QByteArray h5Utf8 = h5Path.toUtf8();
    if (Hdf5IO::datasetExists(h5Utf8.constData(), "s1_tops_product_contract", &contractExists) != 0) {
        error = QStringLiteral("Unable to inspect geometry reference contract: %1").arg(h5Path);
        return false;
    }
    if (contractExists == 0) {
        return true;
    }
    if (conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_product_contract", contract) != 0 ||
        QString::fromStdString(contract) != QStringLiteral("continuous_deburst_common_coverage_v1")) {
        error = QStringLiteral("Geometry reference contract is missing or unknown: %1").arg(h5Path);
        return false;
    }

    std::string referenceFile;
    if (conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_geometry_reference_file", referenceFile) != 0 ||
        referenceFile.empty()) {
        error = QStringLiteral("Common-coverage geometry reference is missing: %1").arg(h5Path);
        return false;
    }
    const QString referenceName = QString::fromStdString(referenceFile);
    if (QFileInfo(referenceName).fileName() != referenceName ||
        !referenceName.endsWith(QStringLiteral(".h5"), Qt::CaseInsensitive)) {
        error = QStringLiteral("Common-coverage geometry reference must be a sibling H5 file name: %1").arg(h5Path);
        return false;
    }
    geometryH5Path = QFileInfo(h5Path).dir().absoluteFilePath(referenceName);
    if (!QFileInfo(geometryH5Path).isFile()) {
        error = QStringLiteral("Common-coverage geometry reference does not exist: %1").arg(geometryH5Path);
        return false;
    }

    std::string sourceFrameMapping;
    std::string referenceContract;
    std::string signature;
    std::string referenceSignature;
    std::string referenceSourceFrameMapping;
    if (conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_coverage_signature", signature) != 0 ||
        conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_source_frame_mapping", sourceFrameMapping) != 0 ||
        conversion.read_str_from_h5(geometryH5Path.toStdString().c_str(), "s1_tops_product_contract", referenceContract) != 0 ||
        conversion.read_str_from_h5(geometryH5Path.toStdString().c_str(), "s1_tops_coverage_signature", referenceSignature) != 0 ||
        conversion.read_str_from_h5(geometryH5Path.toStdString().c_str(), "s1_tops_source_frame_mapping", referenceSourceFrameMapping) != 0 ||
        referenceContract != "continuous_deburst_common_coverage_v1" || signature.empty() ||
        signature != referenceSignature || sourceFrameMapping.empty() ||
        sourceFrameMapping != referenceSourceFrameMapping) {
        error = QStringLiteral("Common-coverage geometry reference identity is inconsistent: %1").arg(h5Path);
        return false;
    }

    int sourceRows = 0;
    int referenceRows = 0;
    if (!NodeUtils::readScalarFromH5(h5Path, "azimuth_len", sourceRows, &error) ||
        !NodeUtils::readScalarFromH5(geometryH5Path, "azimuth_len", referenceRows, &error)) {
        return false;
    }
    CommonCoverageRowMapping sourceMapping;
    CommonCoverageRowMapping referenceMapping;
    if (!loadCommonCoverageRowMapping(h5Path, sourceRows, sourceMapping, error) ||
        !loadCommonCoverageRowMapping(geometryH5Path, referenceRows, referenceMapping, error) ||
        !commonCoverageMappingsMatch(sourceMapping, referenceMapping, h5Path, error)) {
        return false;
    }
    return true;
}

static int computeCommonCoverageBounds(const CommonCoverageRowMapping& mapping,
                                       cv::Mat& latCoef,
                                       cv::Mat& lonCoef,
                                       int sceneHeight,
                                       int sceneWidth,
                                       int offsetRow,
                                       int offsetCol,
                                       double* lonMax,
                                       double* latMax,
                                       double* lonMin,
                                       double* latMin)
{
    if (!mapping.applies) {
        return Utils::computeImageGeoBoundry(latCoef, lonCoef, sceneHeight, sceneWidth, offsetRow, offsetCol,
                                             lonMax, latMax, lonMin, latMin);
    }
    bool haveBounds = false;
    for (const SourceRowRun& run : mapping.runs) {
        double runLonMax = 0.0, runLatMax = 0.0, runLonMin = 0.0, runLatMin = 0.0;
        if (Utils::computeImageGeoBoundry(latCoef, lonCoef, run.rowCount, sceneWidth, run.sourceFirstRow, offsetCol,
                                          &runLonMax, &runLatMax, &runLonMin, &runLatMin) != 0) {
            return -1;
        }
        if (!haveBounds) {
            *lonMax = runLonMax; *latMax = runLatMax; *lonMin = runLonMin; *latMin = runLatMin;
            haveBounds = true;
        } else {
            *lonMax = qMax(*lonMax, runLonMax); *latMax = qMax(*latMax, runLatMax);
            *lonMin = qMin(*lonMin, runLonMin); *latMin = qMin(*latMin, runLatMin);
        }
    }
    return haveBounds ? 0 : -1;
}

static int mapCommonCoverageDem(Deflat& flat,
                                const CommonCoverageRowMapping& mapping,
                                cv::Mat& dem,
                                cv::Mat& mappedDem,
                                cv::Mat& mappedLat,
                                cv::Mat& mappedLon,
                                double lonUpperLeft,
                                double latUpperLeft,
                                int offsetRow,
                                int offsetCol,
                                int sceneHeight,
                                int sceneWidth,
                                double prf,
                                double rangeSpacing,
                                double wavelength,
                                double nearRangeTime,
                                double start,
                                double end,
                                cv::Mat& stateVec)
{
    if (!mapping.applies) {
        return flat.demMapping(dem, mappedDem, mappedLat, mappedLon, lonUpperLeft, latUpperLeft, offsetRow,
                               offsetCol, sceneHeight, sceneWidth, prf, rangeSpacing, wavelength, nearRangeTime,
                               start, end, stateVec, 20, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, geocodingProgressCallback);
    }

    mappedDem.release();
    mappedLat.release();
    mappedLon.release();
    for (const SourceRowRun& run : mapping.runs) {
        cv::Mat runDem, runLat, runLon;
        const int result = flat.demMapping(dem, runDem, runLat, runLon, lonUpperLeft, latUpperLeft,
                                           run.sourceFirstRow, offsetCol, run.rowCount, sceneWidth,
                                           prf, rangeSpacing, wavelength, nearRangeTime, start, end, stateVec,
                                           20, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, geocodingProgressCallback);
        if (result != 0) return result;
        if (runDem.rows != run.rowCount || runLat.rows != run.rowCount || runLon.rows != run.rowCount ||
            runDem.cols != sceneWidth || runLat.cols != sceneWidth || runLon.cols != sceneWidth ||
            runDem.type() != runLat.type() || runDem.type() != runLon.type()) {
            return -1;
        }
        if (mappedDem.empty()) {
            mappedDem.create(sceneHeight, sceneWidth, runDem.type());
            mappedLat.create(sceneHeight, sceneWidth, runLat.type());
            mappedLon.create(sceneHeight, sceneWidth, runLon.type());
        }
        runDem.copyTo(mappedDem.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount));
        runLat.copyTo(mappedLat.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount));
        runLon.copyTo(mappedLon.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount));
    }
    return mappedDem.rows == sceneHeight && mappedLat.rows == sceneHeight && mappedLon.rows == sceneHeight ? 0 : -1;
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
        QString coverageDataset = QStringLiteral("phase");
        if (product_level == QStringLiteral("coherence-1.0")) coverageDataset = QStringLiteral("coherence");
        else if (product_level == QStringLiteral("dem-1.0")) coverageDataset = QStringLiteral("dem");
        else if (product_level == QStringLiteral("SBAS-1.0")) coverageDataset = QStringLiteral("defomation_velocity");
        cv::Mat coverageGrid;
        if (!NodeUtils::readMatFromH5(inputH5, coverageDataset, coverageGrid, -1, &inputError) || coverageGrid.empty()) {
            emit errorProcess(QStringLiteral("Unable to read common-coverage validation grid from %1: %2")
                                  .arg(inputH5, inputError));
            return;
        }
        const int coverageOutputRows = coverageGrid.rows;
        bool mappedLonPresent = false;
        bool mappedLatPresent = false;
        if (!inspectH5DatasetPresence(inputH5, "mapped_lon", mappedLonPresent, inputError) ||
            !inspectH5DatasetPresence(inputH5, "mapped_lat", mappedLatPresent, inputError)) {
            emit errorProcess(inputError);
            return;
        }
        if (mappedLonPresent != mappedLatPresent) {
            emit errorProcess(QStringLiteral("Geocoding input contains only one of mapped_lon/mapped_lat: %1")
                                  .arg(inputH5));
            return;
        }
        const bool hasMappedCoordinates = mappedLonPresent && mappedLatPresent;
        CommonCoverageRowMapping mappedInputContract;
        if (!loadCommonCoverageRowMapping(inputH5, coverageOutputRows,
                                           mappedInputContract, inputError)) {
            emit errorProcess(inputError);
            return;
        }
        if (hasMappedCoordinates) {
            if (!NodeUtils::readMatFromH5(inputH5, "mapped_lon", mapped_lon, -1, &inputError) ||
                !NodeUtils::readMatFromH5(inputH5, "mapped_lat", mapped_lat, -1, &inputError)) {
                emit errorProcess(QStringLiteral("Unable to read mapped geocoding grids from %1: %2")
                                      .arg(inputH5, inputError));
                return;
            }
        }
        if (mappedInputContract.applies) {
            std::string mappedSource;
            if (!NodeUtils::readStringFromH5(inputH5, "source_1", mappedSource, &inputError)) {
                emit errorProcess(QStringLiteral("Common-coverage geocoding input is missing source_1: %1")
                                      .arg(inputH5));
                return;
            }
            QString masterH5;
            if (!resolveSourceH5Path(mappedSource, save_path, masterH5, inputError)) {
                emit errorProcess(QStringLiteral("Unable to resolve common-coverage master for %1: %2")
                                      .arg(inputH5, inputError));
                return;
            }
            QString geometryH5;
            if (!resolveCommonCoverageGeometryReference(masterH5, geometryH5, inputError)) {
                emit errorProcess(inputError);
                return;
            }
            int geometryRows = 0;
            CommonCoverageRowMapping referenceContract;
            if (!NodeUtils::readScalarFromH5(geometryH5, "azimuth_len", geometryRows, &inputError) ||
                !loadCommonCoverageRowMapping(geometryH5, geometryRows,
                                               referenceContract, inputError) ||
                mappedInputContract.geometryReferenceFile != QFileInfo(geometryH5).fileName() ||
                !commonCoverageMappingsMatch(mappedInputContract, referenceContract,
                                              inputH5, inputError)) {
                emit errorProcess(inputError);
                return;
            }
        }
        if (hasMappedCoordinates)
        {
            if (mapped_lon.empty() || mapped_lat.empty() || mapped_lon.size() != mapped_lat.size()) {
                emit errorProcess(QStringLiteral("Geocoding input mapped_lon/mapped_lat grids are empty or have different dimensions: %1")
                                      .arg(inputH5));
                return;
            }
            sceneHeight = mapped_lon.rows;
            sceneWidth = mapped_lon.cols;
            NodeUtils::readScalarFromH5(inputH5, "multilook_az", multilook_az);
            NodeUtils::readScalarFromH5(inputH5, "multilook_rg", multilook_rg);
            if (mappedInputContract.applies &&
                mappedInputContract.sourceRowMap.rows != sceneHeight) {
                emit errorProcess(QStringLiteral("Common-coverage map length differs from mapped product rows: %1")
                                  .arg(inputH5));
                return;
            }
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
            QString geometryH5;
            if (!resolveCommonCoverageGeometryReference(masterH5, geometryH5, inputError)) {
                emit errorProcess(inputError);
                return;
            }
            if (!NodeUtils::readScalarFromH5(inputH5, "multilook_az", multilook_az, &inputError) ||
                !NodeUtils::readScalarFromH5(inputH5, "multilook_rg", multilook_rg, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "range_len", sceneWidth, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "azimuth_len", sceneHeight, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "offset_row", offset_row, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "offset_col", offset_col, &inputError) ||
                !NodeUtils::readMatFromH5(geometryH5, "lon_coefficient", lon_coef, -1, &inputError) ||
                !NodeUtils::readMatFromH5(geometryH5, "lat_coefficient", lat_coef, -1, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "prf", prf, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "carrier_frequency", wavelength, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "range_spacing", rangeSpacing, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "slant_range_first_pixel", nearRangeTime, &inputError) ||
                !NodeUtils::readStringFromH5(geometryH5, "acquisition_start_time", start_time, &inputError) ||
                !NodeUtils::readStringFromH5(geometryH5, "acquisition_stop_time", end_time, &inputError) ||
                !NodeUtils::readMatFromH5(geometryH5, "state_vec", statevec, -1, &inputError)) {
                emit errorProcess(QStringLiteral("Unable to read geocoding source metadata from %1: %2")
                                      .arg(geometryH5, inputError));
                return;
            }

            CommonCoverageRowMapping rowMapping;
            if (!loadCommonCoverageRowMapping(inputH5, coverageOutputRows, rowMapping, inputError)) {
                emit errorProcess(inputError);
                return;
            }
            CommonCoverageRowMapping geometryMapping;
            if (!loadCommonCoverageRowMapping(geometryH5, sceneHeight, geometryMapping, inputError)) {
                emit errorProcess(inputError);
                return;
            }
            if (rowMapping.applies != geometryMapping.applies ||
                (rowMapping.applies &&
                 (rowMapping.geometryReferenceFile != QFileInfo(geometryH5).fileName() ||
                  !commonCoverageMappingsMatch(rowMapping, geometryMapping, inputH5, inputError)))) {
                emit errorProcess(QStringLiteral("Geocoding input and geometry reference common-coverage provenance is inconsistent: %1")
                                  .arg(inputH5));
                return;
            }
            if (rowMapping.applies &&
                ((multilook_az > 1 && rowMapping.mapAzimuthFactor != multilook_az) ||
                 (rowMapping.mapAzimuthFactor > 1 && rowMapping.sourceRowMap.rows != coverageOutputRows))) {
                emit errorProcess(QStringLiteral("Common-coverage multilook source-row map does not match the geocoding grid: %1")
                                      .arg(inputH5));
                return;
            }
            if (rowMapping.applies) {
                InSARLogManager::LogInfo("GeocodingWorker", QStringLiteral("Using common-burst source-row mapping for %1 (%2 contiguous source segments).")
                    .arg(geometryH5).arg(static_cast<qulonglong>(rowMapping.runs.size())));
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
            if (computeCommonCoverageBounds(rowMapping, lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
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
            const int geometryOutputRows = rowMapping.applies ? rowMapping.sourceRowMap.rows : sceneHeight;
            ret = mapCommonCoverageDem(flat, rowMapping, dem, mappedDem, mapped_lat, mapped_lon,
                                       lon_upperleft, lat_upperleft, offset_row, offset_col, geometryOutputRows, sceneWidth,
                                       prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec);
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
            const bool mapAlreadyContainsAzimuthMultilook = rowMapping.applies && rowMapping.mapAzimuthFactor > 1;
            const int geometryMultilookAz = mapAlreadyContainsAzimuthMultilook ? 1 : multilook_az;
            if (mapAlreadyContainsAzimuthMultilook && mapped_lat.rows != coverageOutputRows) {
                emit errorProcess(QStringLiteral("Common-coverage geometry row map length differs from product rows: %1")
                                      .arg(inputH5));
                return;
            }
            if (multilook_rg > 1 || geometryMultilookAz > 1)
            {
                int rows_mapped = mapped_lon.rows / geometryMultilookAz;
                int cols_mapped = sceneWidth / multilook_rg;
                Mat lon_new(rows_mapped, cols_mapped, CV_32F);
                for (int i = 0; i < rows_mapped; i++)
                {
                    for (int j = 0; j < cols_mapped; j++)
                    {
                        lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * geometryMultilookAz, i * geometryMultilookAz + geometryMultilookAz),
                            cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
                    }
                }
                lon_new.copyTo(mapped_lon);
                for (int i = 0; i < rows_mapped; i++)
                {
                    for (int j = 0; j < cols_mapped; j++)
                    {
                        lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * geometryMultilookAz, i * geometryMultilookAz + geometryMultilookAz),
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
                    NodeUtils::writeScalarToH5(outputH5, "lat_south", lat_south, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "geocode_type", type, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "multi_rg", multilook_rg, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "multi_az", multilook_az, &outputError) &&
                    NodeUtils::writeStringToH5(outputH5, "product_level", geocode_Rank_level.toStdString(), &outputError);
                if (!demPath.isEmpty()) {
                    writeSucceeded = writeSucceeded && NodeUtils::writeStringToH5(outputH5, "geocode_dem", QFileInfo(demPath).fileName().toStdString(), &outputError);
                }
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
                    // 地理编码只做重采样，不改变指标语义，随之传播标签
                    writeSucceeded = writeSucceeded && NodeUtils::copyCoherenceSemantics(inputH5, outputH5, &outputError);
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
        bool mappedLonPresent = false;
        bool mappedLatPresent = false;
        if (!inspectH5DatasetPresence(masterH5, "mapped_lon", mappedLonPresent, inputError) ||
            !inspectH5DatasetPresence(masterH5, "mapped_lat", mappedLatPresent, inputError)) {
            emit errorProcess(inputError);
            return;
        }
        if (mappedLonPresent != mappedLatPresent) {
            emit errorProcess(QStringLiteral("SAR geocoding master contains only one of mapped_lon/mapped_lat: %1")
                                  .arg(masterH5));
            return;
        }
        const bool hasMappedCoordinates = mappedLonPresent && mappedLatPresent;
        CommonCoverageRowMapping rowMapping;
        if (hasMappedCoordinates) {
            if (!NodeUtils::readMatFromH5(masterH5, "mapped_lon", mapped_lon, -1, &inputError) ||
                !NodeUtils::readMatFromH5(masterH5, "mapped_lat", mapped_lat, -1, &inputError) ||
                mapped_lon.empty() || mapped_lat.empty() || mapped_lon.size() != mapped_lat.size()) {
                emit errorProcess(QStringLiteral("Unable to read valid mapped_lon/mapped_lat grids from %1: %2")
                                      .arg(masterH5, inputError));
                return;
            }
            if (!loadCommonCoverageRowMapping(masterH5, mapped_lon.rows, rowMapping, inputError)) {
                emit errorProcess(inputError);
                return;
            }
            if (rowMapping.applies) {
                QString geometryH5;
                if (!resolveCommonCoverageGeometryReference(masterH5, geometryH5, inputError)) {
                    emit errorProcess(inputError);
                    return;
                }
                int geometryRows = 0;
                CommonCoverageRowMapping referenceMapping;
                if (!NodeUtils::readScalarFromH5(geometryH5, "azimuth_len", geometryRows, &inputError) ||
                    !loadCommonCoverageRowMapping(geometryH5, geometryRows,
                                                   referenceMapping, inputError) ||
                    rowMapping.geometryReferenceFile != QFileInfo(geometryH5).fileName() ||
                    !commonCoverageMappingsMatch(rowMapping, referenceMapping,
                                                  masterH5, inputError)) {
                    emit errorProcess(inputError);
                    return;
                }
            }
        }
        if (!hasMappedCoordinates)
        {
            QString geometryH5;
            if (!resolveCommonCoverageGeometryReference(masterH5, geometryH5, inputError)) {
                emit errorProcess(inputError);
                return;
            }
            if (!NodeUtils::readScalarFromH5(geometryH5, "range_len", sceneWidth2, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "azimuth_len", sceneHeight2, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "offset_row", offset_row2, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "offset_col", offset_col2, &inputError) ||
                !NodeUtils::readMatFromH5(geometryH5, "lon_coefficient", lon_coef2, -1, &inputError) ||
                !NodeUtils::readMatFromH5(geometryH5, "lat_coefficient", lat_coef2, -1, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "prf", prf2, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "carrier_frequency", wavelength2, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "range_spacing", rangeSpacing2, &inputError) ||
                !NodeUtils::readScalarFromH5(geometryH5, "slant_range_first_pixel", nearRangeTime2, &inputError) ||
                !NodeUtils::readStringFromH5(geometryH5, "acquisition_start_time", start_time2, &inputError) ||
                !NodeUtils::readStringFromH5(geometryH5, "acquisition_stop_time", end_time2, &inputError) ||
                !NodeUtils::readMatFromH5(geometryH5, "state_vec", statevec2, -1, &inputError)) {
                emit errorProcess(QStringLiteral("Unable to read SAR geocoding source metadata from %1: %2")
                                      .arg(geometryH5, inputError));
                return;
            }

            if (!loadCommonCoverageRowMapping(masterH5, sceneHeight2, rowMapping, inputError)) {
                emit errorProcess(inputError);
                return;
            }
            if (rowMapping.applies) {
                InSARLogManager::LogInfo("GeocodingWorker", QStringLiteral("Using common-burst source-row mapping for %1 (%2 contiguous source segments).")
                    .arg(geometryH5).arg(static_cast<qulonglong>(rowMapping.runs.size())));
            }

            Deflat flat;
            wavelength2 = VEL_C / wavelength2;
            nearRangeTime2 = 2.0 * nearRangeTime2 / VEL_C;
            if (conversion.utc2gps(start_time2.c_str(), &start2) != 0 ||
                conversion.utc2gps(end_time2.c_str(), &end2) != 0) {
                emit errorProcess(QStringLiteral("Unable to convert SAR geocoding acquisition time: %1").arg(masterH5));
                return;
            }
            if (computeCommonCoverageBounds(rowMapping, lat_coef2, lon_coef2, sceneHeight2, sceneWidth2,
                                            offset_row2, offset_col2, &lonMax2, &latMax2, &lonMin2, &latMin2) != 0) {
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
            const int geometryOutputRows = rowMapping.applies
                ? rowMapping.sourceRowMap.rows : sceneHeight2;
            ret = mapCommonCoverageDem(flat, rowMapping, dem2, mappedDem2, mapped_lat, mapped_lon,
                                       lon_upperleft2, lat_upperleft2, offset_row2, offset_col2,
                                       geometryOutputRows, sceneWidth2, prf2, rangeSpacing2, wavelength2,
                                       nearRangeTime2, start2, end2, statevec2);
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

        // Common-coverage coordinates may already carry azimuth block-center
        // semantics.  In that case their rows are final output rows and must
        // not be reduced a second time.
        const bool coordinatesAlreadyAzimuthMultilooked =
            rowMapping.applies && rowMapping.mapAzimuthFactor > 1;
        const int coordinateMultilookAz = coordinatesAlreadyAzimuthMultilooked ? 1 : multi_az;
        int outputMultiAz = multi_az;
        if (coordinatesAlreadyAzimuthMultilooked) {
            if (multi_az > 1 && multi_az != rowMapping.mapAzimuthFactor) {
                emit errorProcess(QStringLiteral("Requested azimuth multilook factor conflicts with the common-coverage mapped row contract: %1")
                                      .arg(masterH5));
                return;
            }
            outputMultiAz = rowMapping.mapAzimuthFactor;
        }
        if (coordinatesAlreadyAzimuthMultilooked && multi_az > 1) {
            InSARLogManager::LogInfo(
                "GeocodingWorker",
                QStringLiteral("Common-coverage mapped coordinates already use azimuth factor %1; skipping a second coordinate azimuth multilook.")
                    .arg(rowMapping.mapAzimuthFactor));
        }

        // 多视操作
        if (multi_rg > 1 || coordinateMultilookAz > 1)
        {
            int rows_mapped = mapped_lon.rows / coordinateMultilookAz;
            int cols_mapped = mapped_lon.cols / multi_rg;
            if (rows_mapped <= 0 || cols_mapped <= 0) {
                emit errorProcess(QStringLiteral("SAR geocoding coordinate grid is smaller than the requested multilook factor."));
                return;
            }
            Mat lon_new(rows_mapped, cols_mapped, CV_32F);
            for (int i = 0; i < rows_mapped; i++)
            {
                for (int j = 0; j < cols_mapped; j++)
                {
                    lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * coordinateMultilookAz, i * coordinateMultilookAz + coordinateMultilookAz),
                        cv::Range(j * multi_rg, j * multi_rg + multi_rg)))[0];
                }
            }
            lon_new.copyTo(mapped_lon);
            for (int i = 0; i < rows_mapped; i++)
            {
                for (int j = 0; j < cols_mapped; j++)
                {
                    lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * coordinateMultilookAz, i * coordinateMultilookAz + coordinateMultilookAz),
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
            const int amplitudeMultilookAz = coordinatesAlreadyAzimuthMultilooked ? 1 : multi_az;
            ret = util.multilook_SAR(amplitude, amplitude, multi_rg, amplitudeMultilookAz);
            if (ret < 0 || amplitude.empty()) {
                emit errorProcess(QStringLiteral("SAR amplitude multilooking failed for geocoding input: %1").arg(inputH5));
                return;
            }
            if (coordinatesAlreadyAzimuthMultilooked &&
                (amplitude.rows != mapped_lon.rows || amplitude.cols / multi_rg != mapped_lon.cols / multi_rg)) {
                emit errorProcess(QStringLiteral("Common-coverage mapped coordinates and SAR amplitude do not share the same output-row semantics: %1")
                                      .arg(inputH5));
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
                bool writeSucceeded =
                    NodeUtils::writeScalarToH5(outputH5, "lon_east", lon_east, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "lon_west", lon_west, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "lat_north", lat_north, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "lat_south", lat_south, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "geocode_type", type, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "multi_rg", multi_rg, &outputError) &&
                    NodeUtils::writeScalarToH5(outputH5, "multi_az", outputMultiAz, &outputError) &&
                    NodeUtils::writeStringToH5(outputH5, "product_level", geocode_Rank_level.toStdString(), &outputError);
                if (!demPath.isEmpty()) {
                    writeSucceeded = writeSucceeded && NodeUtils::writeStringToH5(outputH5, "geocode_dem", QFileInfo(demPath).fileName().toStdString(), &outputError);
                }
                writeSucceeded = writeSucceeded && NodeUtils::writeMatToH5(outputH5, "amplitude", mapped_amplitude, &outputError);
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
        if (product_level == QStringLiteral("coherence-1.0")) {
            QString semanticsError;
            if (!NodeUtils::readCoherenceSemantics(
                    QString::fromStdString(input_files[i]), gRes.coherenceSemantics, &semanticsError)) {
                emit errorProcess(QStringLiteral("Unable to read coherence semantics for %1: %2")
                                      .arg(QString::fromStdString(input_files[i]), semanticsError));
                return;
            }
        }
        Q_EMIT geocodingGenerated(gRes);
    }

    InSARLogManager::LogInfo("GeocodingWorker", QStringLiteral("Geocoding progress: 100% - output processing completed."));
    emit updateProcess(100, QStringLiteral("完成……"));
    InSARLogManager::LogInfo("GeocodingWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}
