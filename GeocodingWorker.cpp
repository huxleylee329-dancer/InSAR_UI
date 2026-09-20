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
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSettings>
#include <QThread>
#include <QElapsedTimer>
#include <algorithm>
#include <limits>
#include <vector>
#include <exception>
#include "InSARLogManager.h"

using namespace cv;

thread_local GeocodingWorker* t_currentGeocodingWorker = nullptr;
thread_local int t_geocodingLastLoggedProgress = -10;
// DEM 映射按组串行推进，各组必须各占进度条上独立的一段，
// 否则第一组爬到段末后，第二组会从段首重新开始 —— 进度条会倒退
thread_local int t_geocodingGroupIndex = 0;
thread_local int t_geocodingGroupCount = 1;

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

// interp_times 的唯一定义处：DEM 会被按 interp_times 在行、列两个方向同时放大，
// 散射点数是 DEM 像素数的 interp_times^2 倍，直接决定 DEM 映射的耗时量级。
static int resolveDemMappingInterpTimes(double scenePx, double demPx)
{
    return std::max(4, std::min(40, static_cast<int>(std::ceil(std::sqrt(scenePx / demPx) * 1.4))));
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
    const double scenePx = static_cast<double>(sceneHeight) * sceneWidth;
    const double demPx = (dem.total() > 0) ? static_cast<double>(dem.total()) : 1.0;
    const int interpTimes = resolveDemMappingInterpTimes(scenePx, demPx);
    InSARLogManager::LogInfo("GeocodingWorker", QStringLiteral("demMapping interp_times resolved: %1 (scenePx=%2, demPx=%3, factor=%4)")
        .arg(interpTimes).arg(static_cast<qulonglong>(scenePx)).arg(static_cast<qulonglong>(demPx))
        .arg(std::sqrt(scenePx / demPx) * 1.4, 0, 'f', 2));

    if (!mapping.applies) {
        const int ret = flat.demMapping(dem, mappedDem, mappedLat, mappedLon, lonUpperLeft, latUpperLeft, offsetRow,
                                        offsetCol, sceneHeight, sceneWidth, prf, rangeSpacing, wavelength, nearRangeTime,
                                        start, end, stateVec, interpTimes, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, geocodingProgressCallback);
        if (ret != 0) return ret;
        if (mappedDem.rows != sceneHeight || mappedLat.rows != sceneHeight || mappedLon.rows != sceneHeight ||
            mappedDem.cols != sceneWidth || mappedLat.cols != sceneWidth || mappedLon.cols != sceneWidth ||
            mappedDem.type() != CV_16S || mappedLat.type() != CV_64F || mappedLon.type() != CV_64F) {
            return -1;
        }
        return 0;
    }

    if (mapping.runs.empty() || sceneHeight <= 0 || sceneWidth <= 0) {
        return -1;
    }

    mappedDem.create(sceneHeight, sceneWidth, CV_16S);
    mappedLat.create(sceneHeight, sceneWidth, CV_64F);
    mappedLon.create(sceneHeight, sceneWidth, CV_64F);

    // 去斜后的输出网格是【时间均匀】的：输出行 r 对应的方位时刻就是 start + r/prf。
    //
    // 旧实现把每个 run 的 sourceFirstRow 当作 offset_row 传给 demMapping、再按 runs 把结果
    // 拷回输出行——那等于让方位时刻按【源行号 sourceRowMap(r)】走。而 sourceRowMap 在 5 道
    // burst 接缝处各跳过约 174 行（burst 间的方位重叠），于是每道接缝多算
    // 174 × 13.8849 m ≈ 2.40 km 的方位时间，地面位置向北跳 2.40 km 并逐道累积。
    // 实测相对上游几何多项式（它在接缝处完全连续）：接缝前 57 m、过 3 道 7193 m、过 5 道 11927 m，
    // 正是每道 2.40 km。
    //
    // 修法：让 demMapping 直接在全幅网格上散射（offset_row = 0），行号即时间。
    // 下面用一个覆盖全体输出行的合成 run 代替原来的分组——此时几何按输出行索引，
    // 拷贝回填退化为恒等，分组也就不再需要。

    // 单一"组"：覆盖全体输出行、源起源取 0，使 demMapping 的 offset_row = 0（行号即时间）
    std::vector<std::vector<SourceRowRun>> groups(1);
    groups[0].resize(1);
    groups[0][0].sourceFirstRow = 0;
    groups[0][0].rowCount = sceneHeight;
    groups[0][0].outputFirstRow = 0;

    for (size_t g = 0; g < groups.size(); ++g) {
        const auto& grp = groups[g];
        const int curOrigin = grp.front().sourceFirstRow;
        // 收紧组场景高度：用该组最后一个 run 的结束源行减去起始源行
        const int curGroupHeight = grp.back().sourceFirstRow + grp.back().rowCount - curOrigin;
        if (curGroupHeight <= 0) {
            return -1;
        }

        cv::Mat groupDem, groupLat, groupLon;
        // 每个分组都是一次独立的分组进度序列：既要重置日志节流基线（否则第二组起
        // (progress - 上次记录值) 永远达不到 10，整段进度都不会落日志），
        // 也要把该组映射到进度条上自己那一段（否则第二组的 0% 会把进度条拉回起点）
        t_geocodingLastLoggedProgress = -10;
        t_geocodingGroupIndex = static_cast<int>(g);
        t_geocodingGroupCount = static_cast<int>(groups.size());
        const int result = flat.demMapping(dem, groupDem, groupLat, groupLon, lonUpperLeft, latUpperLeft,
                                           curOrigin, offsetCol, curGroupHeight, sceneWidth,
                                           prf, rangeSpacing, wavelength, nearRangeTime, start, end, stateVec,
                                           interpTimes, 5.0 / 6000.0, 5.0 / 6000.0, 0, 0, geocodingProgressCallback);
        if (result != 0) return result;

        if (groupDem.rows != curGroupHeight || groupLat.rows != curGroupHeight || groupLon.rows != curGroupHeight ||
            groupDem.cols != sceneWidth || groupLat.cols != sceneWidth || groupLon.cols != sceneWidth ||
            groupDem.type() != CV_16S || groupLat.type() != CV_64F || groupLon.type() != CV_64F) {
            return -1;
        }

        for (const SourceRowRun& run : grp) {
            const int fullRowStart = run.sourceFirstRow - curOrigin;
            const int fullRowEnd = fullRowStart + run.rowCount;
            const int outRowStart = run.outputFirstRow;
            const int outRowEnd = outRowStart + run.rowCount;

            if (fullRowStart < 0 || fullRowEnd > curGroupHeight ||
                outRowStart < 0 || outRowEnd > sceneHeight ||
                run.rowCount <= 0) {
                return -1;
            }

            groupDem.rowRange(fullRowStart, fullRowEnd).copyTo(mappedDem.rowRange(outRowStart, outRowEnd));
            groupLat.rowRange(fullRowStart, fullRowEnd).copyTo(mappedLat.rowRange(outRowStart, outRowEnd));
            groupLon.rowRange(fullRowStart, fullRowEnd).copyTo(mappedLon.rowRange(outRowStart, outRowEnd));
        }

        // 组切片拷贝完毕后立即释放临时矩阵，避免内存累积驻留
        groupDem.release();
        groupLat.release();
        groupLon.release();
    }

    return (mappedDem.rows == sceneHeight && mappedLat.rows == sceneHeight && mappedLon.rows == sceneHeight) ? 0 : -1;
}

// ---------------------------------------------------------------------------
// demMapping 结果缓存
//
// DEM 映射是地理编码里最昂贵的一步（本站场景约 7 小时），而它对同一
// master/DEM/几何参数完全确定、与多视因子等地理编码参数无关。失败重跑或调参重试
// 不应重复支付这笔代价。缓存键只使用轻量元数据（路径 + 大小 + 修改时间 + 几何参数），
// 按项目规范不对 .h5 大文件做全盘哈希。
// ---------------------------------------------------------------------------

// 缓存默认开启；单份缓存约 3.7 GB，可在 Config.ini 中通过
// Geocoding/DemMappingCacheEnabled=false 关闭
static bool demMappingCacheEnabled()
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("Geocoding/DemMappingCacheEnabled"), true).toBool();
}

// 算法版本：改动了 demMapping 的数值行为就必须提升，否则 runs 没变、摘要不变，
// 旧缓存会被误命中、静默给出过期结果。
// v2：方位时刻由 sourceRowMap(r) 改为去斜后的输出行 r（每道 burst 接缝少算 2.40 km）。
static const int kDemMappingCacheVersion = 2;

static QString demMappingCacheDigest(const QString& masterH5, const QString& demPath,
                                     int sceneHeight, int sceneWidth, int interpTimes,
                                     const CommonCoverageRowMapping& mapping)
{
    QStringList parts;
    const auto appendFileIdentity = [&parts](const QString& tag, const QString& path) {
        const QFileInfo info(path);
        parts << QStringLiteral("%1|%2|%3|%4").arg(tag, QDir::toNativeSeparators(info.absoluteFilePath()))
            .arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    };
    appendFileIdentity(QStringLiteral("master"), masterH5);
    appendFileIdentity(QStringLiteral("dem"), demPath);
    parts << QStringLiteral("algo|%1").arg(kDemMappingCacheVersion);
    parts << QStringLiteral("scene|%1|%2|%3").arg(sceneHeight).arg(sceneWidth).arg(interpTimes);
    parts << QStringLiteral("applies|%1").arg(mapping.applies ? 1 : 0);
    for (const SourceRowRun& run : mapping.runs) {
        parts << QStringLiteral("run|%1|%2|%3").arg(run.sourceFirstRow).arg(run.rowCount).arg(run.outputFirstRow);
    }
    return QString::fromLatin1(QCryptographicHash::hash(parts.join(QLatin1Char(';')).toUtf8(),
                                                        QCryptographicHash::Sha1).toHex().left(16));
}

// 文件名带 master 名前缀，便于写入前把同一 master 的旧缓存收敛掉
static QString demMappingCacheFilePath(const QString& projectRoot, const QString& masterH5, const QString& digest)
{
    const QString masterName = QFileInfo(masterH5).completeBaseName();
    const QDir cacheDir(QDir(projectRoot).absoluteFilePath(QStringLiteral(".dem_mapping_cache")));
    return cacheDir.absoluteFilePath(QStringLiteral("%1__%2.h5").arg(masterName, digest));
}

static bool loadDemMappingCache(const QString& cacheFile, const QString& expectedDigest,
                                cv::Mat& dem, cv::Mat& lat, cv::Mat& lon)
{
    if (!QFileInfo::exists(cacheFile)) return false;

    QString error;
    std::string storedDigest;
    if (!NodeUtils::readStringFromH5(cacheFile, QStringLiteral("cache_digest"), storedDigest, &error) ||
        QString::fromStdString(storedDigest) != expectedDigest) {
        return false;
    }
    // complete 在全部数据集写完之后才落盘，用于识别上一次写盘中断留下的残file
    int complete = 0;
    if (!NodeUtils::readScalarFromH5(cacheFile, QStringLiteral("complete"), complete, &error) || complete != 1) {
        return false;
    }

    cv::Mat cachedDem, cachedLat, cachedLon;
    if (!NodeUtils::readMatFromH5(cacheFile, QStringLiteral("mapped_dem"), cachedDem, -1, &error) ||
        !NodeUtils::readMatFromH5(cacheFile, QStringLiteral("mapped_lat"), cachedLat, -1, &error) ||
        !NodeUtils::readMatFromH5(cacheFile, QStringLiteral("mapped_lon"), cachedLon, -1, &error)) {
        return false;
    }
    if (cachedDem.type() != CV_16S || cachedLat.type() != CV_64F || cachedLon.type() != CV_64F) return false;
    if (cachedDem.rows < 1 || cachedDem.cols < 1 ||
        cachedDem.rows != cachedLat.rows || cachedDem.rows != cachedLon.rows ||
        cachedDem.cols != cachedLat.cols || cachedDem.cols != cachedLon.cols) {
        return false;
    }
    dem = cachedDem;
    lat = cachedLat;
    lon = cachedLon;
    return true;
}

static void storeDemMappingCache(const QString& cacheFile, const QString& digest,
                                 const cv::Mat& dem, const cv::Mat& lat, const cv::Mat& lon)
{
    const QDir cacheDir = QFileInfo(cacheFile).absoluteDir();
    if (!cacheDir.exists() && !QDir().mkpath(cacheDir.absolutePath())) return;

    // 同一 master 只保留一份
    const QString prefix = QFileInfo(cacheFile).completeBaseName().section(QStringLiteral("__"), 0, 0)
        + QStringLiteral("__");
    const QFileInfoList stale = cacheDir.entryInfoList(QStringList() << (prefix + QStringLiteral("*.h5")), QDir::Files);
    for (const QFileInfo& info : stale) {
        if (info.absoluteFilePath() != cacheFile) QFile::remove(info.absoluteFilePath());
    }

    // 上一次可能写到一半就中断，先删干净再重建，避免 creat_new_h5 面对残file
    QFile::remove(cacheFile);
    FormatConversion conversion;
    if (conversion.creat_new_h5(cacheFile.toStdString().c_str()) < 0) {
        InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping 缓存写入失败：无法创建 %1").arg(cacheFile));
        return;
    }

    QString error;
    const bool written = NodeUtils::writeMatToH5(cacheFile, QStringLiteral("mapped_dem"), dem, &error) &&
                         NodeUtils::writeMatToH5(cacheFile, QStringLiteral("mapped_lat"), lat, &error) &&
                         NodeUtils::writeMatToH5(cacheFile, QStringLiteral("mapped_lon"), lon, &error) &&
                         NodeUtils::writeStringToH5(cacheFile, QStringLiteral("cache_digest"), digest.toStdString(), &error) &&
                         NodeUtils::writeScalarToH5(cacheFile, QStringLiteral("complete"), 1, &error);
    if (!written) {
        InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping 缓存写入失败：%1").arg(error));
        QFile::remove(cacheFile);
        return;
    }
    InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping 缓存已写入：%1").arg(cacheFile));
}

// ---------------------------------------------------------------------------
// SAR→geo 表缓存（<工程>/.sar2geo_cache/<master>__<digest>.h5）
//
// sarToGeo 逐 SAR 像素解算地面经纬度：全分辨率约 8 分钟（实测 loop=52ms / 21672 px，
// 外推 203,017,060 px）。这张表只依赖几何与 DEM、与产品无关，而同一场景的多个产品
// （phase / coherence / dem / 形变速率）各需要一次，所以落盘复用。
// 键同样只用轻量元数据（路径 + 大小 + 修改时间 + 几何参数），不做大文件全盘哈希。
//
// ⚠ 只要改动了 Utils::sarToGeo 的数值行为（求根方式、高度迭代、DEM 采样约定、
//   方位时刻的行号约定等），必须提升 kSar2GeoTableVersion，否则旧表会被误命中、
//   静默给出过期结果。v2：方位时刻由 sourceRowMap(r) 改为去斜后的输出行 r。
// ---------------------------------------------------------------------------

static const int kSar2GeoTableVersion = 2;

// 单份表约 3.25 GB（8135×24956 两张 CV_64F），可在 Config.ini 中通过
// Geocoding/Sar2GeoTableCacheEnabled=false 关闭
static bool sar2geoTableCacheEnabled()
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("Geocoding/Sar2GeoTableCacheEnabled"), true).toBool();
}

// 逆向地理编码开关（R6）：默认开启；把 INSAR_GEOCODE_INVERSE 设为 0 即切回旧的
// 「正向散射 + 按游程补洞」路径。两条路径并存，便于出问题时快速回退做对照。
static bool inverseGeocodingEnabled()
{
    const QByteArray value = qgetenv("INSAR_GEOCODE_INVERSE");
    return value.isEmpty() || value != QByteArrayLiteral("0");
}

static QString sar2geoTableDigest(const QString& masterH5, const QString& demPath,
                                  int sceneHeight, int sceneWidth,
                                  int offsetRow, int offsetCol, double demSpacing,
                                  const CommonCoverageRowMapping& mapping)
{
    QStringList parts;
    const auto appendFileIdentity = [&parts](const QString& tag, const QString& path) {
        const QFileInfo info(path);
        parts << QStringLiteral("%1|%2|%3|%4").arg(tag, QDir::toNativeSeparators(info.absoluteFilePath()))
            .arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    };
    appendFileIdentity(QStringLiteral("master"), masterH5);
    appendFileIdentity(QStringLiteral("dem"), demPath);
    parts << QStringLiteral("algo|%1").arg(kSar2GeoTableVersion);
    parts << QStringLiteral("scene|%1|%2|%3|%4").arg(sceneHeight).arg(sceneWidth).arg(offsetRow).arg(offsetCol);
    parts << QStringLiteral("demspacing|%1").arg(demSpacing, 0, 'g', 17);
    parts << QStringLiteral("applies|%1").arg(mapping.applies ? 1 : 0);
    for (const SourceRowRun& run : mapping.runs) {
        parts << QStringLiteral("run|%1|%2|%3").arg(run.sourceFirstRow).arg(run.rowCount).arg(run.outputFirstRow);
    }
    return QString::fromLatin1(QCryptographicHash::hash(parts.join(QLatin1Char(';')).toUtf8(),
                                                        QCryptographicHash::Sha1).toHex().left(16));
}

// 文件名带 master 名前缀，便于写入前把同一 master 的旧缓存收敛掉
static QString sar2geoTableFilePath(const QString& projectRoot, const QString& masterH5, const QString& digest)
{
    const QString masterName = QFileInfo(masterH5).completeBaseName();
    const QDir cacheDir(QDir(projectRoot).absoluteFilePath(QStringLiteral(".sar2geo_cache")));
    return cacheDir.absoluteFilePath(QStringLiteral("%1__%2.h5").arg(masterName, digest));
}

static bool loadSar2GeoTable(const QString& cacheFile, const QString& expectedDigest,
                             cv::Mat& lat, cv::Mat& lon)
{
    if (!QFileInfo::exists(cacheFile)) return false;

    QString error;
    std::string storedDigest;
    if (!NodeUtils::readStringFromH5(cacheFile, QStringLiteral("cache_digest"), storedDigest, &error) ||
        QString::fromStdString(storedDigest) != expectedDigest) {
        return false;
    }
    // complete 在全部数据集写完之后才落盘，用于识别上一次写盘中断留下的残file
    int complete = 0;
    if (!NodeUtils::readScalarFromH5(cacheFile, QStringLiteral("complete"), complete, &error) || complete != 1) {
        return false;
    }

    cv::Mat cachedLat, cachedLon;
    if (!NodeUtils::readMatFromH5(cacheFile, QStringLiteral("sar2geo_lat"), cachedLat, -1, &error) ||
        !NodeUtils::readMatFromH5(cacheFile, QStringLiteral("sar2geo_lon"), cachedLon, -1, &error)) {
        return false;
    }
    if (cachedLat.type() != CV_64F || cachedLon.type() != CV_64F) return false;
    if (cachedLat.rows < 1 || cachedLat.cols < 1 ||
        cachedLat.rows != cachedLon.rows || cachedLat.cols != cachedLon.cols) {
        return false;
    }
    lat = cachedLat;
    lon = cachedLon;
    return true;
}

static void storeSar2GeoTable(const QString& cacheFile, const QString& digest,
                              const cv::Mat& lat, const cv::Mat& lon)
{
    const QDir cacheDir = QFileInfo(cacheFile).absoluteDir();
    if (!cacheDir.exists() && !QDir().mkpath(cacheDir.absolutePath())) return;

    // 同一 master 只保留一份
    const QString prefix = QFileInfo(cacheFile).completeBaseName().section(QStringLiteral("__"), 0, 0)
        + QStringLiteral("__");
    const QFileInfoList stale = cacheDir.entryInfoList(QStringList() << (prefix + QStringLiteral("*.h5")), QDir::Files);
    for (const QFileInfo& info : stale) {
        if (info.absoluteFilePath() != cacheFile) QFile::remove(info.absoluteFilePath());
    }

    QFile::remove(cacheFile);
    FormatConversion conversion;
    if (conversion.creat_new_h5(cacheFile.toStdString().c_str()) < 0) {
        InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2GEO 表缓存写入失败：无法创建 %1").arg(cacheFile));
        return;
    }

    QString error;
    const bool written = NodeUtils::writeMatToH5(cacheFile, QStringLiteral("sar2geo_lat"), lat, &error) &&
                         NodeUtils::writeMatToH5(cacheFile, QStringLiteral("sar2geo_lon"), lon, &error) &&
                         NodeUtils::writeStringToH5(cacheFile, QStringLiteral("cache_digest"), digest.toStdString(), &error) &&
                         NodeUtils::writeScalarToH5(cacheFile, QStringLiteral("complete"), 1, &error);
    if (!written) {
        InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2GEO 表缓存写入失败：%1").arg(error));
        QFile::remove(cacheFile);
        return;
    }
    InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2GEO 表缓存已写入：%1").arg(cacheFile));
}

// SAR2UTM 输出网格预检：DEM 映射代价极高（本站场景约 7 小时），
// 若输出网格注定突破 SAR2UTM 的内部内存预算，必须在这里失败，而不是等映射跑完之后才被拒绝。
// 返回空字符串表示通过（或无法预检，交由 SAR2UTM 内部校验），否则返回可直接展示的失败原因。
static QString checkSar2UtmOutputGridFeasibility(cv::Mat& latCoef, cv::Mat& lonCoef,
                                                 int sceneHeight, int sceneWidth,
                                                 int offsetRow, int offsetCol)
{
    int requestedRows = 0, requestedCols = 0;
    unsigned long long requiredBytes = 0, budgetBytes = 0;
    if (Utils::estimateSar2UtmGridFromGeometry(latCoef, lonCoef, sceneHeight, sceneWidth, offsetRow, offsetCol,
                                               &requestedRows, &requestedCols, &requiredBytes, &budgetBytes) != 0) {
        InSARLogManager::LogInfo("GeocodingWorker", QStringLiteral(
            "SAR2UTM 输出网格预检跳过：几何系数不可用于估计，将在 SAR2UTM 内部校验。"));
        return QString();
    }

    const double inputPixels = double(sceneHeight) * double(sceneWidth);
    InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2UTM 输出网格预检：预计 %1 x %2 = %3 亿像素，需 %4 GB，内部预算 %5 GB（输入 %6 亿像素）")
        .arg(requestedRows).arg(requestedCols)
        .arg(double(requiredBytes) / 1.0e8, 0, 'f', 2)
        .arg(double(requiredBytes) / 1073741824.0, 0, 'f', 2)
        .arg(double(budgetBytes) / 1073741824.0, 0, 'f', 2)
        .arg(inputPixels / 1.0e8, 0, 'f', 2));

    if (requiredBytes <= budgetBytes) {
        return QString();
    }
    return QStringLiteral("SAR2UTM 输出网格预检不通过：预计 %1 x %2 = %3 亿像素，需 %4 GB，超出内部预算 %5 GB。"
                          "该网格约为输入像素数的 %6 倍，属于插值放大而非分辨率提升；"
                          "请增大多视因子（rg/az）以降低输出分辨率后重试。")
        .arg(requestedRows).arg(requestedCols)
        .arg(double(requiredBytes) / 1.0e8, 0, 'f', 2)
        .arg(double(requiredBytes) / 1073741824.0, 0, 'f', 2)
        .arg(double(budgetBytes) / 1073741824.0, 0, 'f', 2)
        .arg(double(requiredBytes) / inputPixels, 0, 'f', 2);
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

        // DEM 映射整体占 2..70，组间均分；70 之后留给 SAR2UTM 与输出写盘（共用 70..98）
        const int groupCount = (t_geocodingGroupCount > 0) ? t_geocodingGroupCount : 1;
        const int groupIndex = (t_geocodingGroupIndex >= 0) ? t_geocodingGroupIndex : 0;
        const int groupSpan = (70 - 2) / groupCount;
        const int groupStart = 2 + groupIndex * groupSpan;
        const int groupEnd = (groupIndex + 1 >= groupCount) ? 70 : (groupStart + groupSpan);
        const int mapped_prog = groupStart + progress * (groupEnd - groupStart) / 100;

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
        t_geocodingGroupIndex = 0;
        t_geocodingGroupCount = 1;
    }
    ~GeocodingThreadLocalGuard() {
        t_currentGeocodingWorker = nullptr;
        t_geocodingLastLoggedProgress = -10;
        t_geocodingGroupIndex = 0;
        t_geocodingGroupCount = 1;
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
) try
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
    // 逆向地理编码开关（R6）：默认开启，INSAR_GEOCODE_INVERSE=0 切回旧的正向路径。
    // 必须在 if/else 之外声明——相位路径与幅度路径是两个兄弟块，互相看不见对方的局部量。
    bool inverseGeocoding = inverseGeocodingEnabled();
    //干涉产品地理编码
    if (type == 1)
    {
        std::string source_file;
        Mat mapped_lat, mapped_lon, phase, mapped_phase;
        // SAR→geo 表（P1），逆向地理编码用。只在 !hasMappedCoordinates 时构建——
        // 那条分支才读了 statevec/prf/dem；hasMappedCoordinates 分支没有几何量可用。
        Mat geoLat, geoLon;
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
        // inverseGeocoding 在 if/else 之外声明（见上方）；这里只负责在需要时把它降级为正向路径。
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
            const int geometryOutputRows = rowMapping.applies ? rowMapping.sourceRowMap.rows : sceneHeight;
            const QString gridPreflightError = checkSar2UtmOutputGridFeasibility(
                lat_coef, lon_coef, geometryOutputRows, sceneWidth, offset_row, offset_col);
            if (!gridPreflightError.isEmpty()) {
                emit errorProcess(gridPreflightError);
                return;
            }

            // DEM 映射结果缓存：命中则整段跳过，失败重跑或调参重试不必重付数小时代价
            const double demMappingScenePx = double(geometryOutputRows) * double(sceneWidth);
            const double demMappingDemPx = (dem.total() > 0) ? double(dem.total()) : 1.0;
            const int demMappingInterpTimes = resolveDemMappingInterpTimes(demMappingScenePx, demMappingDemPx);
            const QString demMappingDigest = demMappingCacheDigest(masterH5, demPath, geometryOutputRows, sceneWidth,
                                                                   demMappingInterpTimes, rowMapping);
            const QString demMappingCacheFile = demMappingCacheFilePath(save_path, masterH5, demMappingDigest);
            Mat cachedDem, cachedLat, cachedLon;
            const bool demMappingFromCache = demMappingCacheEnabled() &&
                loadDemMappingCache(demMappingCacheFile, demMappingDigest, cachedDem, cachedLat, cachedLon);
            if (demMappingFromCache) {
                cachedDem.copyTo(mappedDem);
                cachedLat.copyTo(mapped_lat);
                cachedLon.copyTo(mapped_lon);
                ret = 0;
                InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping cache hit: %1").arg(demMappingCacheFile));
            } else {
                QElapsedTimer demMappingTimer;
                demMappingTimer.start();
                InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping started: %1").arg(masterH5));
                ret = mapCommonCoverageDem(flat, rowMapping, dem, mappedDem, mapped_lat, mapped_lon,
                                           lon_upperleft, lat_upperleft, offset_row, offset_col, geometryOutputRows, sceneWidth,
                                           prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec);
                InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping finished: status=%1, elapsed_ms=%2, source=%3")
                    .arg(ret).arg(demMappingTimer.elapsed()).arg(masterH5));
                if (ret == 0 && !mapped_lat.empty() && !mapped_lon.empty()) {
                    storeDemMappingCache(demMappingCacheFile, demMappingDigest, mappedDem, mapped_lat, mapped_lon);
                }
            }
            if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                Q_EMIT cancelled();
                return;
            }
            if (ret < 0 || mapped_lat.empty() || mapped_lon.empty()) {
                emit errorProcess(QStringLiteral("DEM mapping failed for geocoding source: %1").arg(masterH5));
                return;
            }
            // ---- SAR→geo 表（P1）与闭环自检 ----
            // 表：sarToGeo 逐 SAR 像素解算地面经纬度，缓存到 <工程>/.sar2geo_cache/
            //     （全分辨率约 8 分钟，同场景的多个产品共用一份）。未命中时按 sampleStride=1
            //     全分辨率解算后落盘。
            // 自检：把表里的 (lat,lon,h) 再用同一套正向原语（ell2xyz + findZeroDopplerTime）
            //     映射回 (方位行, 距离列)，看能否回到原像素。完全绕开正向坐标表（它被填洞+模糊过，
            //     不能作为判据），直接验证求解器本身。有符号均值反映偏移，标准差反映几何误差。
            //     自检按 stride 抽样——验证的是完整表，而不是一张只在抽样格上有值的表。
            // 逆向路径要求 SAR 网格与相位网格同尺寸；多视块会把 mapped_lon/lat 降采样而
            // phase 不会，尺寸就对不上了，所以多视时强制回退正向路径。
            if (inverseGeocoding && (multilook_rg > 1 || (rowMapping.applies && rowMapping.mapAzimuthFactor > 1))) {
                InSARLogManager::LogInfo("GeocodingWorker", QStringLiteral("多视与逆向地理编码不兼容，本文件回退正向路径。"));
                inverseGeocoding = false;
            }
            // 表由 inverseGeocoding 驱动（逆向路径必须要它）。下面的闭环自检是 SAR2GEO 表的
            // 端到端校验：改动 sarToGeo、几何约定或上游行映射之后，用它确认表仍然自洽。
            // 默认关闭（零成本），置 INSAR_VALIDATE_SAR2GEO=1 开启，约 1 秒。
            if (inverseGeocoding) {
                const int stride = 97;   // 自检抽样步长 → 约 8135/97 × 24956/97 ≈ 2.2 万点，秒级
                const double demSpacing = 5.0 / 6000.0;
                const QString tableDigest = sar2geoTableDigest(masterH5, demPath, geometryOutputRows, sceneWidth,
                                                               offset_row, offset_col, demSpacing, rowMapping);
                const QString tableFile = sar2geoTableFilePath(save_path, masterH5, tableDigest);
                int sarRet = 0;
                if (sar2geoTableCacheEnabled() && loadSar2GeoTable(tableFile, tableDigest, geoLat, geoLon)) {
                    InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2GEO 表缓存命中：%1").arg(tableFile));
                } else {
                    sarRet = Utils::sarToGeo(statevec, prf, rangeSpacing, nearRangeTime, start, end,
                                             geometryOutputRows, sceneWidth, offset_row, offset_col,
                                             0, 0, rowMapping.sourceRowMap, dem,
                                             lon_upperleft, lat_upperleft, demSpacing,
                                             geoLat, geoLon, 1);
                    if (sarRet == 0) storeSar2GeoTable(tableFile, tableDigest, geoLat, geoLon);
                }
                if (sarRet == 0 && qEnvironmentVariableIsSet("INSAR_VALIDATE_SAR2GEO")) {
                    const double timeInterval = 1.0 / prf;
                    const double deltaT = statevec.at<double>(1, 0) - statevec.at<double>(0, 0);
                    const double slant0 = nearRangeTime * VEL_C * 0.5;
                    orbitStateVectors orb(statevec, start, end, deltaT);
                    orb.applyOrbit();

                    double sAz = 0.0, sRg = 0.0, qAz = 0.0, qRg = 0.0;
                    double maxAz = 0.0, maxRg = 0.0, maxAzRow = -1.0, maxRgRow = -1.0;
                    qint64 n = 0, noFwd = 0, noGeo = 0;
                    std::vector<double> rgErrList, azErrList;
                    std::vector<int> badRgPerRow(geometryOutputRows, 0);   // 距离坏点按输出行的分布
                    qint64 badRg = 0, badAz = 0;
                    for (int r = 0; r < geometryOutputRows; r += stride) {
                        // 期望的方位行就是输出行 r 本身（去斜网格时间均匀）。这里原先用
                        // sourceRowMap(r)，与 sarToGeo 当时同一套错误约定自洽，所以自检测不出
                        // 接缝处累积 2.40 km 的偏差——这正是它一直没报警的原因。
                        for (int c = 0; c < sceneWidth; c += stride) {
                            const double lat = geoLat.at<double>(r, c);
                            const double lon = geoLon.at<double>(r, c);
                            if (!std::isfinite(lat) || !std::isfinite(lon)) { ++noGeo; continue; }
                            // 该点的 DEM 高度（与 sarToGeo 内部同一约定：原点平移到像元中心）
                            double h = 0.0;
                            if (dem.type() == CV_16S && dem.rows > 1 && dem.cols > 1) {
                                const double latOrigin = lat_upperleft + demSpacing * 0.5;
                                const double lonOrigin = lon_upperleft - demSpacing * 0.5;
                                const int di = static_cast<int>(std::floor((latOrigin - lat) / demSpacing));
                                const int dj = static_cast<int>(std::floor((lon - lonOrigin) / demSpacing));
                                if (di >= 0 && di < dem.rows - 1 && dj >= 0 && dj < dem.cols - 1) {
                                    const double fi = (latOrigin - lat) / demSpacing - di;
                                    const double fj = (lon - lonOrigin) / demSpacing - dj;
                                    const double v00 = dem.at<short>(di, dj);
                                    const double v01 = dem.at<short>(di, dj + 1);
                                    const double v10 = dem.at<short>(di + 1, dj);
                                    const double v11 = dem.at<short>(di + 1, dj + 1);
                                    if (v00 > -9000.0 && v01 > -9000.0 && v10 > -9000.0 && v11 > -9000.0) {
                                        h = (v00 * (1 - fi) + v10 * fi) * (1 - fj) + (v01 * (1 - fi) + v11 * fi) * fj;
                                    }
                                }
                            }
                            Position gp;
                            if (Utils::ell2xyz(lon, lat, h, gp) != 0) continue;
                            double tZero = 0.0, dist = 0.0;
                            if (!Utils::findZeroDopplerTime(orb, gp, wavelength, timeInterval, 0.0,
                                                            tZero, dist, 0.01)) {
                                ++noFwd;
                                continue;
                            }
                            const double eAz = (tZero - start) / timeInterval - double(r);   // 期望 0
                            const double eRg = (dist - slant0) / rangeSpacing - double(c);           // 期望 0
                            sAz += eAz; sRg += eRg; qAz += eAz * eAz; qRg += eRg * eRg;
                            if (std::fabs(eAz) > maxAz) { maxAz = std::fabs(eAz); maxAzRow = double(r); }
                            if (std::fabs(eRg) > maxRg) { maxRg = std::fabs(eRg); maxRgRow = double(r); }
                            // 收集绝对值：稳健统计关心误差量级，不关心正负方向
                            azErrList.push_back(std::fabs(eAz));
                            rgErrList.push_back(std::fabs(eRg));
                            if (std::fabs(eAz) > 1.0) ++badAz;
                            if (std::fabs(eRg) > 1.0) { ++badRg; ++badRgPerRow[r]; }
                            ++n;
                        }
                    }
                    const double mAz = (n > 0) ? sAz / double(n) : 0.0;
                    const double mRg = (n > 0) ? sRg / double(n) : 0.0;
                    const double vAz = (n > 0) ? qAz / double(n) - mAz * mAz : 0.0;
                    const double vRg = (n > 0) ? qRg / double(n) - mRg * mRg : 0.0;
                    const double sdAz = (vAz > 0.0) ? std::sqrt(vAz) : 0.0;
                    const double sdRg = (vRg > 0.0) ? std::sqrt(vRg) : 0.0;
                    // 稳健统计：标准差会被少数离群点绑架——两万个点里几个 178 px 的坏点就足以
                    // 吃掉全部方差。中位数反映"绝大多数点"的实际精度，p99 反映尾部规模，
                    // 二者结合才能区分"整体精度不够"与"被少数坏点撑大"，两者对策完全不同。
                    const auto pick = [](std::vector<double>& v, double q) {
                        if (v.empty()) return 0.0;
                        std::sort(v.begin(), v.end());
                        const size_t idx = static_cast<size_t>(q * double(v.size() - 1) + 0.5);
                        return v[std::min(idx, v.size() - 1)];
                    };
                    const double medAz = pick(azErrList, 0.50), p99Az = pick(azErrList, 0.99);
                    const double medRg = pick(rgErrList, 0.50), p99Rg = pick(rgErrList, 0.99);
                    const double pctAz = (n > 0) ? 100.0 * double(badAz) / double(n) : 0.0;
                    const double pctRg = (n > 0) ? 100.0 * double(badRg) / double(n) : 0.0;
                    // 坏点集中在个别行 → 与行映射或局部几何有关；铺满所有采样行 → 与地形起伏有关。
                    // 这个区分决定"能不能带着这个尾巴进 P3"，只看 max 和占比是分不出来的。
                    const int sampledRows = (geometryOutputRows + stride - 1) / stride;
                    int badRowCount = 0, worstBadRow = -1, worstBadRowCount = 0;
                    for (int rr = 0; rr < geometryOutputRows; ++rr) {
                        if (badRgPerRow[rr] > 0) {
                            ++badRowCount;
                            if (badRgPerRow[rr] > worstBadRowCount) {
                                worstBadRowCount = badRgPerRow[rr];
                                worstBadRow = rr;
                            }
                        }
                    }
                    const QString rowInfo = QString(
                        "距离坏点行分布：%1/%2 个采样行含坏点，最集中于 行%3（%4 点）")
                        .arg(badRowCount).arg(sampledRows).arg(worstBadRow).arg(worstBadRowCount);

                    const auto describe = [](const QString& name, double m, double sd, double med,
                                             double p99, double mx, double mxRow, double pct) {
                        return QString("%1误差 mean=%2±%3 px median=%4 p99=%5 max=%6（行%7）超1px %8%")
                            .arg(name).arg(m, 0, 'f', 3).arg(sd, 0, 'f', 3)
                            .arg(med, 0, 'f', 3).arg(p99, 0, 'f', 2).arg(mx, 0, 'f', 2)
                            .arg(int(mxRow)).arg(pct, 0, 'f', 2);
                    };
                    InSARLogManager::LogInfo("GeocodingWorker", QString(
                        "SAR2GEO 闭环自检（步长 %1）：回投成功 %2 点，表 NaN 跳过 %3 点，前向失败 %4 点。 %5；%6 %7")
                        .arg(stride).arg(qlonglong(n)).arg(qlonglong(noGeo)).arg(qlonglong(noFwd))
                        .arg(describe(QStringLiteral("方位行"), mAz, sdAz, medAz, p99Az, maxAz, maxAzRow, pctAz))
                        .arg(describe(QStringLiteral("距离列"), mRg, sdRg, medRg, p99Rg, maxRg, maxRgRow, pctRg))
                        .arg(rowInfo));
                } else if (sarRet != 0) {
                    emit errorProcess(QStringLiteral("Unable to build SAR to geo table for geocoding source: %1")
                                          .arg(masterH5));
                    return;
                }
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
emit updateProcess(70, QStringLiteral("DEM 映射完成，正在做 SAR→UTM 重采样……"));
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
            try {
                if (inverseGeocoding && !geoLat.empty() && !geoLon.empty()) {
                    // 逆向：逐输出像素反算 + 双线性采样。相位没有 nodata 哨兵（NaN 才是），
                    // 传 NaN 关闭哨兵判定；窗口触及 NaN 仍按 R5 整点 nodata。
                    ret = Utils::sar2GeoInverseSample(geoLat, geoLon, mapped_lon, mapped_lat,
                                                      phase, mapped_phase,
                                                      &lon_east, &lon_west, &lat_north, &lat_south,
                                                      std::numeric_limits<double>::quiet_NaN(), 16);
                } else {
                    ret = util.SAR2UTM(mapped_lon, mapped_lat, phase, mapped_phase, 1,
                                       &lon_east, &lon_west, &lat_north, &lat_south);
                }
            } catch (const cv::Exception& exception) {
                emit errorProcess(QStringLiteral("SAR2UTM OpenCV exception for %1: %2")
                                      .arg(inputH5, QString::fromLocal8Bit(exception.what())));
                return;
            } catch (const std::exception& exception) {
                emit errorProcess(QStringLiteral("SAR2UTM exception for %1: %2")
                                      .arg(inputH5, QString::fromLocal8Bit(exception.what())));
                return;
            }
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
int process = 70 + double(i + 1) / (double)input_files.size() * 28.0;

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
        // SAR→geo 表（P1），幅度路径的逆向地理编码用；与相位路径共用同一份缓存
        Mat geoLat2, geoLon2;
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
            const int geometryOutputRows = rowMapping.applies
                ? rowMapping.sourceRowMap.rows : sceneHeight2;
            const QString gridPreflightError = checkSar2UtmOutputGridFeasibility(
                lat_coef2, lon_coef2, geometryOutputRows, sceneWidth2, offset_row2, offset_col2);
            if (!gridPreflightError.isEmpty()) {
                emit errorProcess(gridPreflightError);
                return;
            }
            // DEM 映射结果缓存：与相位路径同键同义，命中则整段跳过
            const double demMappingScenePx = double(geometryOutputRows) * double(sceneWidth2);
            const double demMappingDemPx = (dem2.total() > 0) ? double(dem2.total()) : 1.0;
            const int demMappingInterpTimes = resolveDemMappingInterpTimes(demMappingScenePx, demMappingDemPx);
            const QString demMappingDigest = demMappingCacheDigest(masterH5, demPath, geometryOutputRows, sceneWidth2,
                                                                   demMappingInterpTimes, rowMapping);
            const QString demMappingCacheFile = demMappingCacheFilePath(save_path, masterH5, demMappingDigest);
            Mat cachedDem, cachedLat, cachedLon;
            const bool demMappingFromCache = demMappingCacheEnabled() &&
                loadDemMappingCache(demMappingCacheFile, demMappingDigest, cachedDem, cachedLat, cachedLon);
            if (demMappingFromCache) {
                cachedDem.copyTo(mappedDem2);
                cachedLat.copyTo(mapped_lat);
                cachedLon.copyTo(mapped_lon);
                ret = 0;
                InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping cache hit: %1").arg(demMappingCacheFile));
            } else {
                QElapsedTimer demMappingTimer;
                demMappingTimer.start();
                InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping started: %1").arg(masterH5));
                ret = mapCommonCoverageDem(flat, rowMapping, dem2, mappedDem2, mapped_lat, mapped_lon,
                                           lon_upperleft2, lat_upperleft2, offset_row2, offset_col2,
                                           geometryOutputRows, sceneWidth2, prf2, rangeSpacing2, wavelength2,
                                           nearRangeTime2, start2, end2, statevec2);
                InSARLogManager::LogInfo("GeocodingWorker", QString("DEM mapping finished: status=%1, elapsed_ms=%2, source=%3")
                    .arg(ret).arg(demMappingTimer.elapsed()).arg(masterH5));
                if (ret == 0 && !mapped_lat.empty() && !mapped_lon.empty()) {
                    storeDemMappingCache(demMappingCacheFile, demMappingDigest, mappedDem2, mapped_lat, mapped_lon);
                }
            }
            if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                Q_EMIT cancelled();
                return;
            }
            if (ret < 0 || mapped_lat.empty() || mapped_lon.empty()) {
                emit errorProcess(QStringLiteral("DEM mapping failed for SAR geocoding source: %1").arg(masterH5));
                return;
            }

            // ---- SAR→geo 表（P1）：与相位路径共用同一份缓存，未命中则全分辨率解算 ----
            // 表只依赖几何 + DEM，与产品无关，所以两条路径的摘要相同、缓存互通。
            if (inverseGeocoding) {
                const double demSpacing2 = 5.0 / 6000.0;
                const QString tableDigest2 = sar2geoTableDigest(masterH5, demPath, geometryOutputRows, sceneWidth2,
                                                               offset_row2, offset_col2, demSpacing2, rowMapping);
                const QString tableFile2 = sar2geoTableFilePath(save_path, masterH5, tableDigest2);
                int tableRet2 = 0;
                if (sar2geoTableCacheEnabled() && loadSar2GeoTable(tableFile2, tableDigest2, geoLat2, geoLon2)) {
                    InSARLogManager::LogInfo("GeocodingWorker", QString("SAR2GEO 表缓存命中：%1").arg(tableFile2));
                } else {
                    tableRet2 = Utils::sarToGeo(statevec2, prf2, rangeSpacing2, nearRangeTime2, start2, end2,
                                                geometryOutputRows, sceneWidth2, offset_row2, offset_col2,
                                                0, 0, rowMapping.sourceRowMap, dem2,
                                                lon_upperleft2, lat_upperleft2, demSpacing2,
                                                geoLat2, geoLon2, 1);
                    if (tableRet2 == 0) storeSar2GeoTable(tableFile2, tableDigest2, geoLat2, geoLon2);
                }
                if (tableRet2 != 0) {
                    emit errorProcess(QStringLiteral("Unable to build SAR to geo table for geocoding source: %1")
                                          .arg(masterH5));
                    return;
                }
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

emit updateProcess(70, QStringLiteral("DEM 映射完成，正在做 SAR→UTM 重采样……"));
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
            try {
                // 逆向：幅度已被多视过，尺寸必须与表、与坐标网格完全一致，否则回退正向路径
                if (inverseGeocoding && !geoLat2.empty() && !geoLon2.empty() &&
                    amplitude.rows == geoLat2.rows && amplitude.cols == geoLat2.cols &&
                    mapped_lon.rows == geoLat2.rows && mapped_lon.cols == geoLat2.cols) {
                    ret = Utils::sar2GeoInverseSample(geoLat2, geoLon2, mapped_lon, mapped_lat,
                                                      amplitude, mapped_amplitude,
                                                      &lon_east, &lon_west, &lat_north, &lat_south,
                                                      std::numeric_limits<double>::quiet_NaN(), 16);
                } else {
                    ret = util.SAR2UTM(mapped_lon, mapped_lat, amplitude, mapped_amplitude, 1,
                                       &lon_east, &lon_west, &lat_north, &lat_south);
                }
            } catch (const cv::Exception& exception) {
                emit errorProcess(QStringLiteral("SAR2UTM OpenCV exception for %1: %2")
                                      .arg(inputH5, QString::fromLocal8Bit(exception.what())));
                return;
            } catch (const std::exception& exception) {
                emit errorProcess(QStringLiteral("SAR2UTM exception for %1: %2")
                                      .arg(inputH5, QString::fromLocal8Bit(exception.what())));
                return;
            }
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
int process = 70 + double(i + 1) / (double)input_files.size() * 28.0;
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
catch (const cv::Exception& exception)
{
    const QString error = QStringLiteral("Geocoding OpenCV exception: %1")
                              .arg(QString::fromLocal8Bit(exception.what()));
    InSARLogManager::LogError("GeocodingWorker", error);
    emit errorProcess(error);
}
catch (const std::exception& exception)
{
    const QString error = QStringLiteral("Geocoding exception: %1")
                              .arg(QString::fromLocal8Bit(exception.what()));
    InSARLogManager::LogError("GeocodingWorker", error);
    emit errorProcess(error);
}
