#include "InterferometricFormationWorker.h"
#include <Utils.h>
#include <Deflat.h>
#include <FormatConversion.h>
#include <Hdf5IO.h>
#include "Package.h"
#include "icon_source.h"
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <cmath>
#include <limits>
#include <vector>
#include "InSARLogManager.h"
#include "NodeUtils.h"

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "Deflat_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "Deflat.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#endif

using namespace cv;
using namespace std;

static thread_local InterferometricFormationWorker* current_worker = nullptr;
static thread_local int g_substep_prog_start = 0;
static thread_local int g_substep_prog_end = 0;
static thread_local QString g_current_pair_info;

static bool __stdcall DeflatProgressCallbackImpl(int progress, const char* message);

struct CommonCoverageRun
{
    int outputFirstRow = 0;
    int sourceFirstRow = 0;
    int rowCount = 0;
};

struct CommonCoverageContract
{
    bool applies = false;
    QString signature;
    QString sourceFrameMapping;
    QString geometryReferenceFile;
    int sourceRowCount = 0;
    int sourceRowOrigin = 0;
    int sourceFirstBurst = 0;
    int sourceLastBurst = 0;
    int sourceBurstOffset = 0;
    int commonFirstBurst = 0;
    int commonLastBurst = 0;
    int commonBurstCount = 0;
    int partialCoverage = 0;
    QString sourceRowMapSemantics;
    int sourceRowMapAzimuthFactor = 1;
    cv::Mat sourceRowMap;
    cv::Mat retainedIndices;
    cv::Mat sourceRowRanges;
    std::vector<CommonCoverageRun> runs;
};

static void rebuildCommonCoverageRuns(CommonCoverageContract& contract)
{
	contract.runs.clear();
	if (contract.sourceRowMap.type() != CV_32S || contract.sourceRowMap.rows < 1 ||
		contract.sourceRowMap.cols != 1) return;
	int runStart = 0;
	for (int row = 1; row <= contract.sourceRowMap.rows; ++row) {
		if (row < contract.sourceRowMap.rows &&
			contract.sourceRowMap.at<int>(row, 0) == contract.sourceRowMap.at<int>(row - 1, 0) + 1) continue;
		CommonCoverageRun run;
		run.outputFirstRow = runStart;
		run.sourceFirstRow = contract.sourceRowMap.at<int>(runStart, 0);
		run.rowCount = row - runStart;
		contract.runs.push_back(run);
		runStart = row;
	}
}

static bool loadCommonCoverageContract(const QString& h5Path, int expectedRows,
                                       CommonCoverageContract& contract, QString& error)
{
    contract = CommonCoverageContract();
    FormatConversion conversion;
    std::string productContract;
    NodeUtils::Hdf5Locker locker(h5Path.toStdString());
    if (!locker.isLocked()) {
        error = QStringLiteral("Unable to lock common-coverage input: %1").arg(h5Path);
        return false;
    }
    int contractExists = 0;
    const QByteArray h5Utf8 = h5Path.toUtf8();
    if (Hdf5IO::datasetExists(h5Utf8.constData(), "s1_tops_product_contract", &contractExists) != 0) {
        error = QStringLiteral("Unable to inspect common-coverage contract: %1").arg(h5Path);
        return false;
    }
    if (contractExists == 0) {
        return true;
    }
    if (conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_product_contract", productContract) != 0 ||
        QString::fromStdString(productContract) != QStringLiteral("continuous_deburst_common_coverage_v1")) {
        error = QStringLiteral("Common-coverage product contract is missing or unknown: %1").arg(h5Path);
        return false;
    }

    std::string signature;
    std::string sourceFrameMapping;
    std::string geometryReferenceFile;
    if (conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_coverage_signature", signature) != 0 ||
        conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_source_frame_mapping", sourceFrameMapping) != 0 ||
        conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_geometry_reference_file", geometryReferenceFile) != 0 ||
        conversion.read_str_from_h5(h5Path.toStdString().c_str(), "s1_tops_output_source_row_map_semantics", productContract) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_output_source_row_map_multilook_azimuth_factor", &contract.sourceRowMapAzimuthFactor) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_source_full_burst_row_count", &contract.sourceRowCount) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_output_source_row_origin", &contract.sourceRowOrigin) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_source_burst_first", &contract.sourceFirstBurst) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_source_burst_last", &contract.sourceLastBurst) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_source_burst_offset", &contract.sourceBurstOffset) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_common_master_first_burst", &contract.commonFirstBurst) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_common_master_last_burst", &contract.commonLastBurst) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_common_master_burst_count", &contract.commonBurstCount) != 0 ||
        conversion.read_int_from_h5(h5Path.toStdString().c_str(), "s1_tops_partial_burst_coverage", &contract.partialCoverage) != 0 ||
        conversion.read_array_from_h5(h5Path.toStdString().c_str(), "s1_tops_output_source_row_map", contract.sourceRowMap) != 0 ||
        conversion.read_array_from_h5(h5Path.toStdString().c_str(), "s1_tops_retained_master_burst_indices", contract.retainedIndices) != 0 ||
        conversion.read_array_from_h5(h5Path.toStdString().c_str(), "s1_tops_retained_source_row_ranges", contract.sourceRowRanges) != 0 ||
        signature.empty() || sourceFrameMapping.empty() || geometryReferenceFile.empty() ||
        contract.sourceRowCount <= 0 || contract.sourceRowOrigin < 0 ||
        contract.sourceRowOrigin >= contract.sourceRowCount || contract.commonFirstBurst < 1 ||
        contract.commonLastBurst < contract.commonFirstBurst ||
        contract.commonBurstCount != contract.commonLastBurst - contract.commonFirstBurst + 1 ||
        contract.sourceFirstBurst != contract.commonFirstBurst + contract.sourceBurstOffset ||
        contract.sourceLastBurst != contract.commonLastBurst + contract.sourceBurstOffset ||
        (contract.partialCoverage != 0 && contract.partialCoverage != 1) ||
        contract.sourceRowMap.type() != CV_32S || contract.sourceRowMap.rows != expectedRows || contract.sourceRowMap.cols != 1 ||
        contract.retainedIndices.type() != CV_32S || contract.retainedIndices.rows != 1 ||
        contract.retainedIndices.cols != contract.commonBurstCount ||
        contract.sourceRowRanges.type() != CV_32S || contract.sourceRowRanges.rows != contract.commonBurstCount ||
        contract.sourceRowRanges.cols != 2 ||
        (QString::fromStdString(productContract) != QStringLiteral("full_deburst_source_row_v1") &&
         QString::fromStdString(productContract) != QStringLiteral("multilook_azimuth_block_center_v1")) ||
        contract.sourceRowMapAzimuthFactor < 1 ||
        (QString::fromStdString(productContract) == QStringLiteral("full_deburst_source_row_v1") && contract.sourceRowMapAzimuthFactor != 1) ||
        (QString::fromStdString(productContract) == QStringLiteral("multilook_azimuth_block_center_v1") && contract.sourceRowMapAzimuthFactor <= 1)) {
        error = QStringLiteral("Common-coverage contract is incomplete or invalid: %1").arg(h5Path);
        return false;
    }

    contract.signature = QString::fromStdString(signature);
    contract.sourceFrameMapping = QString::fromStdString(sourceFrameMapping);
    contract.geometryReferenceFile = QString::fromStdString(geometryReferenceFile);
    contract.sourceRowMapSemantics = QString::fromStdString(productContract);
    for (int segment = 0; segment < contract.commonBurstCount; ++segment) {
        const int rangeStart = contract.sourceRowRanges.at<int>(segment, 0);
        const int rangeEnd = contract.sourceRowRanges.at<int>(segment, 1);
        if (contract.retainedIndices.at<int>(0, segment) != contract.commonFirstBurst + segment ||
            rangeStart < 0 || rangeEnd <= rangeStart || rangeEnd > contract.sourceRowCount) {
            error = QStringLiteral("Common-coverage retained burst range is invalid: %1").arg(h5Path);
            return false;
        }
    }
    for (int row = 0; row < contract.sourceRowMap.rows; ++row) {
        const int sourceRow = contract.sourceRowMap.at<int>(row, 0);
        bool inRetainedRange = false;
        for (int segment = 0; segment < contract.commonBurstCount; ++segment) {
            if (sourceRow >= contract.sourceRowRanges.at<int>(segment, 0) &&
                sourceRow < contract.sourceRowRanges.at<int>(segment, 1)) {
                inRetainedRange = true;
                break;
            }
        }
        if (!inRetainedRange || (row > 0 && sourceRow <= contract.sourceRowMap.at<int>(row - 1, 0))) {
            error = QStringLiteral("Common-coverage source-row map does not match retained ranges: %1").arg(h5Path);
            return false;
        }
    }
	rebuildCommonCoverageRuns(contract);
    if (contract.sourceRowMap.rows != expectedRows || contract.runs.empty() ||
        contract.sourceRowMap.at<int>(0, 0) != contract.sourceRowOrigin) {
        error = QStringLiteral("Common-coverage source-row map has an invalid length: %1").arg(h5Path);
        return false;
    }
    contract.applies = true;
    return true;
}

static bool contractsMatch(const CommonCoverageContract& master, const CommonCoverageContract& slave)
{
    return master.applies == slave.applies && (!master.applies ||
        (master.signature == slave.signature && master.sourceFrameMapping == slave.sourceFrameMapping &&
         master.geometryReferenceFile == slave.geometryReferenceFile && master.sourceRowCount == slave.sourceRowCount &&
         master.sourceRowOrigin == slave.sourceRowOrigin &&
         master.sourceRowMapSemantics == slave.sourceRowMapSemantics &&
         master.sourceRowMapAzimuthFactor == slave.sourceRowMapAzimuthFactor &&
         master.commonFirstBurst == slave.commonFirstBurst &&
         master.commonLastBurst == slave.commonLastBurst &&
         master.commonBurstCount == slave.commonBurstCount &&
         master.partialCoverage == slave.partialCoverage &&
         master.retainedIndices.size() == slave.retainedIndices.size() &&
         master.sourceRowRanges.size() == slave.sourceRowRanges.size() &&
         cv::countNonZero(master.retainedIndices != slave.retainedIndices) == 0 &&
         cv::countNonZero(master.sourceRowRanges != slave.sourceRowRanges) == 0 &&
         master.sourceRowMap.size() == slave.sourceRowMap.size() &&
         cv::countNonZero(master.sourceRowMap != slave.sourceRowMap) == 0));
}

static bool commonCoverageMapsInsideSlaveMetadata(const CommonCoverageContract& master,
                                                  const CommonCoverageContract& slave,
                                                  const TopsBurstPhaseMetadata& slaveTops)
{
    if (!master.applies) return true;
    if (!slave.applies || master.commonFirstBurst < 1 ||
        master.commonLastBurst < master.commonFirstBurst ||
        slaveTops.burstAzimuthTime.rows < 1 || slaveTops.linesPerBurst <= 0) {
        return false;
    }
    const int firstSlaveBurst = master.commonFirstBurst + slave.sourceBurstOffset;
    const int lastSlaveBurst = master.commonLastBurst + slave.sourceBurstOffset;
    return firstSlaveBurst >= 1 && lastSlaveBurst >= firstSlaveBurst &&
        lastSlaveBurst <= slaveTops.burstAzimuthTime.rows;
}

static bool writeCommonCoverageContract(const QString& outputPath, const CommonCoverageContract& contract,
                                        const QString& masterPath, QString& error)
{
    if (!contract.applies) return true;
    const QString masterName = QFileInfo(masterPath).fileName();
    if (contract.geometryReferenceFile != masterName) {
        error = QStringLiteral("Common-coverage geometry reference does not identify the master output: %1").arg(masterPath);
        return false;
    }
    return NodeUtils::writeStringToH5(outputPath, QStringLiteral("s1_tops_product_contract"),
                                      std::string("continuous_deburst_common_coverage_v1"), &error) &&
        NodeUtils::writeStringToH5(outputPath, QStringLiteral("s1_tops_coverage_signature"), contract.signature.toStdString(), &error) &&
        NodeUtils::writeStringToH5(outputPath, QStringLiteral("s1_tops_source_frame_mapping"), contract.sourceFrameMapping.toStdString(), &error) &&
        NodeUtils::writeStringToH5(outputPath, QStringLiteral("s1_tops_output_source_row_map_semantics"), contract.sourceRowMapSemantics.toStdString(), &error) &&
        NodeUtils::writeStringToH5(outputPath, QStringLiteral("s1_tops_geometry_reference_file"), masterName.toStdString(), &error) &&
        NodeUtils::writeStringToH5(outputPath, QStringLiteral("s1_tops_geometry_reference_path"), masterPath.toStdString(), &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_source_full_burst_row_count"), contract.sourceRowCount, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_output_source_row_origin"), contract.sourceRowOrigin, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_source_burst_first"), contract.sourceFirstBurst, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_source_burst_last"), contract.sourceLastBurst, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_source_burst_offset"), contract.sourceBurstOffset, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_common_master_first_burst"), contract.commonFirstBurst, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_common_master_last_burst"), contract.commonLastBurst, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_common_master_burst_count"), contract.commonBurstCount, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_partial_burst_coverage"), contract.partialCoverage, &error) &&
        NodeUtils::writeScalarToH5(outputPath, QStringLiteral("s1_tops_output_source_row_map_multilook_azimuth_factor"), contract.sourceRowMapAzimuthFactor, &error) &&
        NodeUtils::writeMatToH5(outputPath, QStringLiteral("s1_tops_output_source_row_map"), contract.sourceRowMap, &error) &&
        NodeUtils::writeMatToH5(outputPath, QStringLiteral("s1_tops_retained_master_burst_indices"), contract.retainedIndices, &error) &&
        NodeUtils::writeMatToH5(outputPath, QStringLiteral("s1_tops_retained_source_row_ranges"), contract.sourceRowRanges, &error);
}

static bool sourceRowMapForPhase(const CommonCoverageContract& contract, int rows, int sourceRowCount,
                                 int originalRowOffset, Mat& sourceRowMap)
{
    if (rows < 1 || sourceRowCount < 1) return false;
    if (contract.applies) {
        if (contract.sourceRowMap.type() != CV_32S || contract.sourceRowMap.cols != 1 ||
            contract.sourceRowMap.rows != rows || contract.sourceRowCount != sourceRowCount) return false;
        sourceRowMap = contract.sourceRowMap;
        return true;
    }
    sourceRowMap.create(rows, 1, CV_32S);
    for (int row = 0; row < rows; ++row) sourceRowMap.at<int>(row, 0) = originalRowOffset + row;
    return originalRowOffset >= 0 && sourceRowMap.at<int>(rows - 1, 0) < sourceRowCount;
}

enum class TopsMetadataReadResult
{
    Success,
    BaseFieldReadOrValueInvalid,
    RegistrationProvenanceReadFailed,
    RegistrationProvenanceSemanticsMismatch
};

static TopsMetadataReadResult readTopsBurstPhaseMetadata(const QString& h5Path, TopsBurstPhaseMetadata& metadata,
                                                          bool requireRegistrationReference = false)
{
	metadata = TopsBurstPhaseMetadata();
	const bool baseMetadata = NodeUtils::readMatFromH5(h5Path, "burstAzimuthTime", metadata.burstAzimuthTime) &&
        NodeUtils::readMatFromH5(h5Path, "azimuthFmRateList", metadata.azimuthFmRateList) &&
        NodeUtils::readMatFromH5(h5Path, "dcEstimateList", metadata.dcEstimateList) &&
        NodeUtils::readMatFromH5(h5Path, "firstValidLine", metadata.firstValidLine) &&
        NodeUtils::readMatFromH5(h5Path, "lastValidLine", metadata.lastValidLine) &&
        NodeUtils::readMatFromH5(h5Path, "firstValidSample", metadata.firstValidSample) &&
        NodeUtils::readMatFromH5(h5Path, "lastValidSample", metadata.lastValidSample) &&
        NodeUtils::readScalarFromH5(h5Path, "linesPerBurst", metadata.linesPerBurst) &&
        NodeUtils::readScalarFromH5(h5Path, "azimuthSteeringRate", metadata.azimuthSteeringRate) &&
		NodeUtils::readScalarFromH5(h5Path, "range_spacing", metadata.rangeSpacing) &&
		NodeUtils::readScalarFromH5(h5Path, "slant_range_first_pixel", metadata.slantRangeFirstPixel);
	if (!baseMetadata || metadata.linesPerBurst <= 0) {
        return TopsMetadataReadResult::BaseFieldReadOrValueInvalid;
    }
	if (!requireRegistrationReference) return TopsMetadataReadResult::Success;
	std::string mappingSemantics;
	const bool registrationMapping =
		NodeUtils::readMatFromH5(h5Path, "s1_tops_registration_mapping_coefficients", metadata.registrationMappingCoefficients) &&
		NodeUtils::readMatFromH5(h5Path, "s1_tops_retained_master_burst_indices", metadata.registrationMappingMasterBurstIndices) &&
		NodeUtils::readStringFromH5(h5Path, "s1_tops_registration_mapping_semantics", mappingSemantics);
	if (!registrationMapping) return TopsMetadataReadResult::RegistrationProvenanceReadFailed;
	if (mappingSemantics != "pull_source_row_and_column_offsets_a0_a1_column_a2_master_burst_line_v1") {
        return TopsMetadataReadResult::RegistrationProvenanceSemanticsMismatch;
    }
	// Reramp provenance belongs to the registration implementation.  v6 FEP
	// uses the mapping only as a local slave zero-Doppler seed, so an absent
	// reramp field is valid.  Read it only when the producer supplied it.
	NodeUtils::readMatFromH5(h5Path, "s1_tops_registration_reramp_phase", metadata.registrationRerampPhase);
	return TopsMetadataReadResult::Success;
}

static QString topsMetadataReadError(const QString& imageRole, TopsMetadataReadResult result, const QString& path)
{
    switch (result) {
    case TopsMetadataReadResult::BaseFieldReadOrValueInvalid:
        return QStringLiteral("%1影像 TOPS 字段读取失败或字段值不合法：%2").arg(imageRole, path);
    case TopsMetadataReadResult::RegistrationProvenanceReadFailed:
        return QStringLiteral("%1影像缺少注册 mapping provenance：%2").arg(imageRole, path);
    case TopsMetadataReadResult::RegistrationProvenanceSemanticsMismatch:
        return QStringLiteral("%1影像注册 mapping provenance 语义不一致：%2").arg(imageRole, path);
    case TopsMetadataReadResult::Success:
        break;
    }
    return QStringLiteral("%1影像 TOPS 元数据状态未知：%2").arg(imageRole, path);
}

static bool readGpsScalarOrSingleValue(const QString& h5Path, const QString& dataset, double& value)
{
    if (NodeUtils::readScalarFromH5(h5Path, dataset, value) && std::isfinite(value)) return true;
    Mat matrix;
    return NodeUtils::readMatFromH5(h5Path, dataset, matrix) && matrix.type() == CV_64F &&
           matrix.total() == 1 && std::isfinite(matrix.at<double>(0, 0)) && (value = matrix.at<double>(0, 0), true);
}

static bool validGpsOrbitMatrix(const Mat& stateVectors, double geometryStartGps, double geometryStopGps,
								double interpolationMarginSeconds, double& osvStartGps, double& osvStopGps)
{
    if (stateVectors.type() != CV_64F || stateVectors.cols != 7 || stateVectors.rows < 4 ||
		!std::isfinite(geometryStartGps) || !std::isfinite(geometryStopGps) ||
		!std::isfinite(interpolationMarginSeconds) || interpolationMarginSeconds <= 0.0 ||
		!(geometryStopGps > geometryStartGps)) return false;
    for (int row = 0; row < stateVectors.rows; ++row) {
        for (int column = 0; column < stateVectors.cols; ++column) {
            if (!std::isfinite(stateVectors.at<double>(row, column))) return false;
        }
        if (row > 0 && stateVectors.at<double>(row, 0) <= stateVectors.at<double>(row - 1, 0)) return false;
    }
    osvStartGps = stateVectors.at<double>(0, 0);
    osvStopGps = stateVectors.at<double>(stateVectors.rows - 1, 0);
    return osvStartGps <= geometryStartGps - interpolationMarginSeconds &&
		osvStopGps >= geometryStopGps + interpolationMarginSeconds;
}

// SNAP-compatible interpolation evaluates the raw state_vec directly with a
// clamped contiguous 8-OSV window.  Unlike the fine 1-second orbit grid, it
// does not need an extra padding interval outside the actual geometry query.
static bool validRawSnapCompatibleOrbitMatrix(const Mat& stateVectors, double geometryStartGps,
											  double geometryStopGps, double& osvStartGps, double& osvStopGps)
{
	if (stateVectors.type() != CV_64F || stateVectors.cols != 7 || stateVectors.rows < 8 ||
		!std::isfinite(geometryStartGps) || !std::isfinite(geometryStopGps) ||
		!(geometryStopGps > geometryStartGps)) return false;
	for (int row = 0; row < stateVectors.rows; ++row) {
		for (int column = 0; column < stateVectors.cols; ++column) {
			if (!std::isfinite(stateVectors.at<double>(row, column))) return false;
		}
		if (row > 0 && stateVectors.at<double>(row, 0) <= stateVectors.at<double>(row - 1, 0)) return false;
	}
	osvStartGps = stateVectors.at<double>(0, 0);
	osvStopGps = stateVectors.at<double>(stateVectors.rows - 1, 0);
	return osvStartGps <= geometryStartGps && osvStopGps >= geometryStopGps;
}

static bool readStrictTopsV5Orbit(const QString& h5Path, double geometryStartGps, double geometryStopGps,
							  double interpolationMarginSeconds, TopsFepV5Orbit& orbit, QString& error)
{
    orbit = TopsFepV5Orbit();
	std::string timeReferenceVersion;
    std::string acquisitionScale;
    std::string stateVectorScale;
	if (!NodeUtils::readStringFromH5(h5Path, QStringLiteral("h5_time_reference_version"), timeReferenceVersion) ||
		timeReferenceVersion != "2" ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("acquisition_time_gps_scale"), acquisitionScale) ||
        acquisitionScale != "GPS" ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("state_vec_time_scale"), stateVectorScale) ||
        stateVectorScale != "GPS" ||
        !readGpsScalarOrSingleValue(h5Path, QStringLiteral("acquisition_start_time_gps"), orbit.acquisitionStartGps) ||
		!readGpsScalarOrSingleValue(h5Path, QStringLiteral("acquisition_stop_time_gps"), orbit.acquisitionStopGps) ||
		!std::isfinite(geometryStartGps) || !std::isfinite(geometryStopGps) || !(geometryStopGps > geometryStartGps) ||
		!std::isfinite(interpolationMarginSeconds) || interpolationMarginSeconds <= 0.0) {
        error = QStringLiteral("v5 平地相位要求明确的 GPS 时间字段和 state_vec 时间尺度：%1").arg(h5Path);
        return false;
    }
	orbit.geometryStartGps = geometryStartGps;
	orbit.geometryStopGps = geometryStopGps;
	orbit.interpolationMarginSeconds = interpolationMarginSeconds;
    Mat fineStateVectors;
    int fineExists = 0;
    const QByteArray utf8Path = h5Path.toUtf8();
    if (Hdf5IO::datasetExists(utf8Path.constData(), "fine_state_vec", &fineExists) != 0) {
        error = QStringLiteral("无法检查 fine_state_vec：%1").arg(h5Path);
        return false;
    }
    std::string fineScale;
    bool fineValid = false;
    if (fineExists != 0 && NodeUtils::readMatFromH5(h5Path, QStringLiteral("fine_state_vec"), fineStateVectors) &&
        NodeUtils::readStringFromH5(h5Path, QStringLiteral("fine_state_vec_time_scale"), fineScale) && fineScale == "GPS") {
        double fineStart = 0.0;
        double fineStop = 0.0;
		fineValid = validGpsOrbitMatrix(fineStateVectors, geometryStartGps, geometryStopGps, interpolationMarginSeconds,
                                        fineStart, fineStop);
        if (fineValid) {
            orbit.stateVectors = fineStateVectors;
            orbit.osvStartGps = fineStart;
            orbit.osvStopGps = fineStop;
            orbit.source = "fine_state_vec";
			orbit.selectionReason = "fine_state_vec_valid_preferred_v1";
			orbit.timeScale = "GPS";
			orbit.interpolationStrategy = "fine_state_vec_cubic_hermite_v2";
			return true;
        }
    }
	if (!NodeUtils::readMatFromH5(h5Path, QStringLiteral("state_vec"), orbit.stateVectors) ||
		!validRawSnapCompatibleOrbitMatrix(orbit.stateVectors, geometryStartGps, geometryStopGps,
			orbit.osvStartGps, orbit.osvStopGps) || orbit.stateVectors.rows < 8) {
		error = QStringLiteral("v5 平地相位要求有效 fine_state_vec，或覆盖任务时窗的原始 8 点 GPS state_vec：%1").arg(h5Path);
		return false;
	}
	orbit.source = "state_vec";
	orbit.selectionReason = fineExists != 0
		? "fine_state_vec_invalid__raw_snap_compatible_fallback_v1"
		: "fine_state_vec_absent__raw_snap_compatible_fallback_v1";
	orbit.timeScale = "GPS";
	orbit.interpolationStrategy = "raw_state_vec_nearest_contiguous_8_osv_cubic_least_squares_v1";
	return true;
}

static bool readStrictLookSide(const QString& h5Path, int& lookSide, QString& source, QString& error)
{
    source.clear();
    std::string value;
    const QByteArray utf8Path = h5Path.toUtf8();
    int looksideExists = 0;
    if (Hdf5IO::datasetExists(utf8Path.constData(), "lookside", &looksideExists) != 0) {
        error = QStringLiteral("v5 平地相位无法检查主影像观测侧字段：%1").arg(h5Path);
        return false;
    }
    if (looksideExists != 0) {
        if (!NodeUtils::readStringFromH5(h5Path, QStringLiteral("lookside"), value)) {
            error = QStringLiteral("v5 平地相位主影像观测侧字段无法读取：%1").arg(h5Path);
            return false;
        }
        const QString normalized = QString::fromStdString(value).trimmed().toUpper();
        if (normalized == QStringLiteral("RIGHT")) {
            lookSide = 1;
            source = QStringLiteral("h5_lookside_v1");
            return true;
        }
        if (normalized == QStringLiteral("LEFT")) {
            lookSide = -1;
            source = QStringLiteral("h5_lookside_v1");
            return true;
        }
        error = QStringLiteral("v5 平地相位主影像观测侧无效：%1").arg(h5Path);
        return false;
    }

    std::string sensor;
    if (!NodeUtils::readStringFromH5(h5Path, QStringLiteral("sensor"), sensor)) {
        error = QStringLiteral("v5 平地相位缺少主影像观测侧约束和传感器身份：%1").arg(h5Path);
        return false;
    }
    const QString normalizedSensor = QString::fromStdString(sensor).trimmed().toLower();
    if (normalizedSensor == QStringLiteral("sentinel") || normalizedSensor == QStringLiteral("sentinel-1") ||
        normalizedSensor == QStringLiteral("sentinel1")) {
        // Sentinel-1 SAR is right-looking; preserve that this is a derived, not stored, constraint.
        lookSide = 1;
        source = QStringLiteral("sentinel1_fixed_right_looking_v1");
        return true;
    }
    error = QStringLiteral("v5 平地相位缺少可验证的主影像观测侧约束：%1").arg(h5Path);
    return false;
}

static bool nativeTopsGeometryCoverage(const TopsBurstPhaseMetadata& tops, int masterLinesPerBurst, const Mat& sourceRowMap,
								   int slaveBurstOffset, bool useSlaveBurst, double extraSearchSeconds,
								   double& startGps, double& stopGps)
{
	if (sourceRowMap.type() != CV_32S || sourceRowMap.cols != 1 || sourceRowMap.rows < 1 ||
		tops.burstAzimuthTime.type() != CV_64F || tops.burstAzimuthTime.cols != 1 ||
		tops.linesPerBurst < 1 || masterLinesPerBurst < 1 || !std::isfinite(tops.azimuthIntervalSeconds) ||
		tops.azimuthIntervalSeconds <= 0.0 || !std::isfinite(extraSearchSeconds) || extraSearchSeconds < 0.0) return false;
	startGps = std::numeric_limits<double>::infinity();
	stopGps = -std::numeric_limits<double>::infinity();
	for (int row = 0; row < sourceRowMap.rows; ++row) {
		const int sourceRow = sourceRowMap.at<int>(row, 0);
		const int masterBurst = sourceRow / masterLinesPerBurst;
		const int nativeLine = sourceRow % masterLinesPerBurst;
		const int burst = useSlaveBurst ? masterBurst + slaveBurstOffset : masterBurst;
		if (sourceRow < 0 || masterBurst < 0 || burst < 0 || burst >= tops.burstAzimuthTime.rows) return false;
		const double burstStart = tops.burstAzimuthTime.at<double>(burst, 0);
		const double first = useSlaveBurst ? burstStart : burstStart + nativeLine * tops.azimuthIntervalSeconds;
		const double last = useSlaveBurst ? burstStart + (tops.linesPerBurst - 1) * tops.azimuthIntervalSeconds : first;
		if (!std::isfinite(first) || !std::isfinite(last)) return false;
		startGps = std::min(startGps, first);
		stopGps = std::max(stopGps, last);
	}
	startGps -= extraSearchSeconds;
	stopGps += extraSearchSeconds;
	return std::isfinite(startGps) && std::isfinite(stopGps) && stopGps > startGps;
}

static bool validateTopographyRuns(const CommonCoverageContract& contract, const Mat& sourceRowMap)
{
    if (!contract.applies) return true;
    for (const CommonCoverageRun& run : contract.runs) {
        if (run.outputFirstRow < 0 || run.rowCount < 1 ||
            run.outputFirstRow + run.rowCount > sourceRowMap.rows) return false;
        for (int row = 0; row < run.rowCount; ++row) {
            if (sourceRowMap.at<int>(run.outputFirstRow + row, 0) != run.sourceFirstRow + row) return false;
        }
    }
    return true;
}

static int computeSourceRowAwareTopography(Deflat& flat, const CommonCoverageContract& contract,
                                    Mat& stateVec1, Mat& stateVec2, Mat& lonCoef, Mat& latCoef, Mat& incCoef,
                                    double prf1, double prf2, int sceneWidth, int offsetRow, int offsetCol,
                                    double nearRangeTime, double rangeSpacing, double wavelength,
                                    double acquisitionStartTime, double acquisitionStopTime, const QString& demPath,
                                    const Mat& sourceRowMap, Mat& topographyPhase)
{
    if (!contract.applies) {
        return flat.topography_simulation(topographyPhase, stateVec1, stateVec2, lonCoef, latCoef, incCoef,
            prf1, prf2, sourceRowMap.rows, sceneWidth, offsetRow, offsetCol, nearRangeTime, rangeSpacing, wavelength,
            acquisitionStartTime, acquisitionStopTime, demPath.toStdString().c_str(), 20, DeflatProgressCallbackImpl);
    }
    if (!validateTopographyRuns(contract, sourceRowMap)) return -1;
    topographyPhase.create(sourceRowMap.rows, sceneWidth, CV_64F);
    for (const CommonCoverageRun& run : contract.runs) {
        Mat topographySegment;
        const int result = flat.topography_simulation(topographySegment, stateVec1, stateVec2, lonCoef, latCoef, incCoef,
            prf1, prf2, run.rowCount, sceneWidth, run.sourceFirstRow, offsetCol, nearRangeTime, rangeSpacing, wavelength,
            acquisitionStartTime, acquisitionStopTime, demPath.toStdString().c_str(), 20, DeflatProgressCallbackImpl);
        if (result != 0 || topographySegment.rows != run.rowCount || topographySegment.cols != sceneWidth) {
            return result == 0 ? -1 : result;
        }
        topographySegment.copyTo(topographyPhase.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount));
    }
    return 0;
}

static bool __stdcall DeflatProgressCallbackImpl(int progress, const char* message) {
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

    if (current_worker) {
        if (current_worker->isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
            return false;
        }
        int mapped_prog = g_substep_prog_start + (progress * (g_substep_prog_end - g_substep_prog_start)) / 100;
        QString info = g_current_pair_info;
        if (message && message[0] != '\0') {
            info += QString(" (%1)").arg(QString::fromUtf8(message));
        }
        emit current_worker->updateProcess(mapped_prog, info);
    }
    return true;
}

struct WorkerResetGuard {
    ~WorkerResetGuard() {
        current_worker = nullptr;
    }
};

static bool buildSlcPairValidMask(const ComplexMat& master, const ComplexMat& slave, Mat& validMask)
{
    validMask.release();
    if (master.GetRows() != slave.GetRows() || master.GetCols() != slave.GetCols() ||
        master.type() != CV_32F || slave.type() != CV_32F ||
        master.re.empty() || master.im.empty() || slave.re.empty() || slave.im.empty()) {
        return false;
    }

    validMask.create(master.GetRows(), master.GetCols(), CV_8U);
#pragma omp parallel for schedule(static)
    for (int row = 0; row < master.GetRows(); ++row) {
        const float* masterRe = master.re.ptr<float>(row);
        const float* masterIm = master.im.ptr<float>(row);
        const float* slaveRe = slave.re.ptr<float>(row);
        const float* slaveIm = slave.im.ptr<float>(row);
        uchar* valid = validMask.ptr<uchar>(row);
        for (int column = 0; column < master.GetCols(); ++column) {
            const bool finite = std::isfinite(masterRe[column]) && std::isfinite(masterIm[column]) &&
                std::isfinite(slaveRe[column]) && std::isfinite(slaveIm[column]);
            const bool masterHasEnergy = masterRe[column] != 0.0f || masterIm[column] != 0.0f;
            const bool slaveHasEnergy = slaveRe[column] != 0.0f || slaveIm[column] != 0.0f;
            valid[column] = finite && masterHasEnergy && slaveHasEnergy ? 1 : 0;
        }
    }
    return true;
}

static bool reduceStrictValidMask(const Mat& inputMask, int multilookRg, int multilookAz, Mat& outputMask)
{
    outputMask.release();
    if (inputMask.empty() || inputMask.type() != CV_8U ||
        multilookRg < 1 || multilookAz < 1 ||
        inputMask.cols < multilookRg || inputMask.rows < multilookAz) {
        return false;
    }
    if (multilookRg == 1 && multilookAz == 1) {
        inputMask.copyTo(outputMask);
        return true;
    }

    const int outputRows = inputMask.rows / multilookAz;
    const int outputColumns = inputMask.cols / multilookRg;
    Mat integralMask;
    integral(inputMask, integralMask, CV_32S);
    outputMask.create(outputRows, outputColumns, CV_8U);
    const int required = multilookRg * multilookAz;
#pragma omp parallel for schedule(static)
    for (int row = 0; row < outputRows; ++row) {
        const int top = row * multilookAz;
        const int bottom = top + multilookAz;
        const int* integralTop = integralMask.ptr<int>(top);
        const int* integralBottom = integralMask.ptr<int>(bottom);
        uchar* output = outputMask.ptr<uchar>(row);
        for (int column = 0; column < outputColumns; ++column) {
            const int left = column * multilookRg;
            const int right = left + multilookRg;
            const int count = integralBottom[right] - integralBottom[left] -
                integralTop[right] + integralTop[left];
            output[column] = count == required ? 1 : 0;
        }
    }
    return true;
}

static bool reduceStrictValidMaskCommonCoverage(const CommonCoverageContract& contract,
                                                const Mat& inputMask,
                                                int multilookRg,
                                                int multilookAz,
                                                Mat& outputMask)
{
    if (!contract.applies || multilookAz <= 1) {
        return reduceStrictValidMask(inputMask, multilookRg, multilookAz, outputMask);
    }
    if (inputMask.empty() || inputMask.rows != contract.sourceRowMap.rows ||
        contract.runs.empty()) {
        return false;
    }

    std::vector<Mat> runOutputs;
    runOutputs.reserve(contract.runs.size());
    for (const CommonCoverageRun& run : contract.runs) {
        const int runOutputRows = run.rowCount / multilookAz;
        if (run.outputFirstRow < 0 || run.rowCount <= 0 ||
            run.outputFirstRow + run.rowCount > inputMask.rows) {
            return false;
        }
        if (runOutputRows <= 0) continue;
        Mat runOutput;
        if (!reduceStrictValidMask(inputMask.rowRange(run.outputFirstRow,
                                                     run.outputFirstRow + run.rowCount),
                                   multilookRg, multilookAz, runOutput) ||
            runOutput.rows != runOutputRows) {
            return false;
        }
        runOutputs.push_back(runOutput);
    }
    if (runOutputs.empty()) return false;
    cv::vconcat(runOutputs, outputMask);
    return !outputMask.empty();
}

static bool buildCoherenceSupportCount(
    const Mat& phaseValidMask, int windowRg, int windowAz, Mat& validSampleCount)
{
    validSampleCount.release();
    if (phaseValidMask.empty() || phaseValidMask.type() != CV_8U ||
        windowRg < 3 || windowAz < 3 || windowRg % 2 == 0 || windowAz % 2 == 0 ||
        phaseValidMask.cols < windowRg || phaseValidMask.rows < windowAz) {
        return false;
    }

    Mat fullSupport16;
    boxFilter(phaseValidMask, fullSupport16, CV_16U, Size(windowRg, windowAz),
              Point(-1, -1), false);
    const int radiusRg = windowRg / 2;
    const int radiusAz = windowAz / 2;
    const Rect innerRect(radiusRg, radiusAz,
                         phaseValidMask.cols - 2 * radiusRg,
                         phaseValidMask.rows - 2 * radiusAz);
    Mat reflectedSupport16;
    copyMakeBorder(fullSupport16(innerRect), reflectedSupport16,
                   radiusAz, radiusAz, radiusRg, radiusRg, BORDER_REFLECT);
    reflectedSupport16.convertTo(validSampleCount, CV_32S);
    return validSampleCount.size() == phaseValidMask.size();
}

InterferometricFormationWorker::InterferometricFormationWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<InterferogramFileResult>("InterferogramFileResult");
}

InterferometricFormationWorker::~InterferometricFormationWorker()
{
}

void InterferometricFormationWorker::Interferometric(bool isdeflat, bool istopo_removal, bool iscoherence,
                                                     int master_index, int win_width, int win_height,
                                                     int multilook_rg, int multilook_az, QString save_path,
                                                     QString file_name, QStringList input_paths,
                                                     bool outputDirectoryIsStaging)
{
    InterferometricWithDem(isdeflat, istopo_removal, iscoherence, master_index, win_width, win_height,
                           multilook_rg, multilook_az, save_path, file_name, input_paths,
                           QString(), outputDirectoryIsStaging);
}

void InterferometricFormationWorker::InterferometricWithDem(bool isdeflat, bool istopo_removal, bool iscoherence,
                                                            int master_index, int win_width, int win_height,
                                                            int multilook_rg, int multilook_az, QString save_path,
                                                            QString file_name, QStringList input_paths,
                                                            QString dem_path,
                                                            bool outputDirectoryIsStaging)
{
    ScopedTaskLogContext taskLogContextGuard(m_taskLogContext);
    current_worker = this;
    WorkerResetGuard reset_guard;

    InSARLogManager::LogTaskEvent(m_taskLogContext, InSARLogManager::LevelDebug,
                                  "InterferometricFormationWorker", QStringLiteral("干涉形成 Worker 已启动。"),
                                  LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile,
                                  QStringLiteral("worker_started"), QStringLiteral("running"));
    InSARLogManager::LogInfo("InterferometricFormationWorker", QString("Interferometric task started. Output folder: %1").arg(file_name));

    FormatConversion FC;
    Deflat flat; 
    Utils util;
    const auto writeArray = [this, &FC](const QString& h5Path, const char* dataset, const Mat& value) {
        if (FC.write_array_to_h5(h5Path.toStdString().c_str(), dataset, value) >= 0) {
            return true;
        }
        emit errorProcess(QStringLiteral("写入干涉H5数据集失败: %1 (%2)")
                          .arg(QString::fromLatin1(dataset), h5Path));
        return false;
    };
    if (save_path.isEmpty() || file_name.isEmpty() || input_paths.isEmpty()) {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        emit cancelled();
        return;
    }

    QDir dir(save_path);
    if (outputDirectoryIsStaging) {
        if (!dir.exists(file_name)) {
            emit errorProcess(QStringLiteral("staging输出目录不存在: %1").arg(dir.absoluteFilePath(file_name)));
            return;
        }
    } else if (!dir.exists(file_name) && !dir.mkpath(file_name)) {
        emit errorProcess(QStringLiteral("无法创建输出目录: %1").arg(dir.absoluteFilePath(file_name)));
        return;
    }
    QString absolute_path = save_path + "/" + file_name;

    const QString demPath = dem_path;
    const QString demValidMaskPath = m_demValidMaskPath;
    qint64 demMaskTotalPixelCount = 0;
    qint64 demMaskValidPixelCount = 0;
    if (istopo_removal && (demPath.isEmpty() || !QFileInfo(demPath).isFile())) {
        emit errorProcess(QStringLiteral("Interferometric formation requires a resolved Auxiliary DEM file."));
        return;
    }
    if (istopo_removal) {
        if (!m_hasDemRequiredBounds) {
            emit errorProcess(QStringLiteral(
                "地形相位处理已停止：缺少可信的输入影像地理范围，无法审核 DEM 有效覆盖。"));
            return;
        }
        QString demMaskError;
        if (!NodeUtils::validateDemValidityMaskForScene(
                demPath, demValidMaskPath,
                m_demRequiredMinLon, m_demRequiredMaxLon,
                m_demRequiredMinLat, m_demRequiredMaxLat,
                &demMaskTotalPixelCount,
                &demMaskValidPixelCount, &demMaskError)) {
            InSARLogManager::LogError("InterferometricFormationWorker", demMaskError);
            emit errorProcess(QStringLiteral("地形相位处理已停止：%1").arg(demMaskError));
            return;
        }
    }

    if (master_index < 0 || master_index >= input_paths.size()) {
        emit errorProcess(QStringLiteral("无效的主图像索引: %1").arg(master_index));
        return;
    }

    QString master_path = input_paths.at(master_index);

    QFileInfo fileinfo(master_path);
    QString master_name = fileinfo.baseName();

    emit updateProcess(2, QStringLiteral("正在读取主影像SLC数据……"));

    ComplexMat Master;
    int ret = 0;
    {
        NodeUtils::Hdf5Locker locker;
        ret = FC.read_slc_from_h5(master_path.toStdString().c_str(), Master);
    }
    if (ret < 0) {
        emit errorProcess(QStringLiteral("读取主图像数据失败: ") + master_path);
        return;
    }
    
    emit updateProcess(5, QStringLiteral("正在解析主影像元数据……"));
    Mat statevec, lon_coef, lat_coef, inc_coef, statevec2;
    TopsBurstPhaseMetadata masterTops, slaveTops;
    TopsFepV5Orbit masterFepOrbit;
    int masterLookSide = 0;
    QString strictOrbitError;
    QString masterLookSideSource;
    double prf = 0.0, prf2 = 0.0, rangeSpacing = 0.0, wavelength = 0.0;
    double nearRangeTime = 0.0, acquisitionStartTime = 0.0, acquisitionStopTime = 0.0;
    string start, end;
    int offset_row = 0, offset_col = 0, sceneHeight = 0, sceneWidth = 0;
    bool masterMetadataRead = false;
    
    {
        NodeUtils::Hdf5Locker locker;
        masterMetadataRead =
            NodeUtils::readMatFromH5(master_path, "state_vec", statevec) &&
            NodeUtils::readMatFromH5(master_path, "lon_coefficient", lon_coef) &&
            NodeUtils::readMatFromH5(master_path, "lat_coefficient", lat_coef) &&
            NodeUtils::readMatFromH5(master_path, "inc_coefficient", inc_coef) &&
            NodeUtils::readScalarFromH5(master_path, "prf", prf) &&
            NodeUtils::readScalarFromH5(master_path, "range_spacing", rangeSpacing) &&
            NodeUtils::readScalarFromH5(master_path, "carrier_frequency", wavelength) &&
            NodeUtils::readScalarFromH5(master_path, "offset_row", offset_row) &&
            NodeUtils::readScalarFromH5(master_path, "offset_col", offset_col) &&
            NodeUtils::readScalarFromH5(master_path, "range_len", sceneWidth) &&
            NodeUtils::readScalarFromH5(master_path, "azimuth_len", sceneHeight) &&
            NodeUtils::readScalarFromH5(master_path, "slant_range_first_pixel", nearRangeTime) &&
            NodeUtils::readStringFromH5(master_path, "acquisition_start_time", start) &&
            NodeUtils::readStringFromH5(master_path, "acquisition_stop_time", end);
    }

    if (!masterMetadataRead || statevec.empty() || lon_coef.empty() || lat_coef.empty() || inc_coef.empty() ||
        !std::isfinite(prf) || !std::isfinite(rangeSpacing) || !std::isfinite(wavelength) || !std::isfinite(nearRangeTime) ||
        prf <= 0.0 || rangeSpacing <= 0.0 || wavelength <= 0.0 || nearRangeTime <= 0.0 ||
        sceneHeight <= 0 || sceneWidth <= 0) {
        const QString error = QStringLiteral("Master image metadata is incomplete or invalid: %1").arg(master_path);
        InSARLogManager::LogError("InterferometricFormationWorker", error);
        emit errorProcess(error);
        return;
    }

    wavelength = VEL_C / wavelength;
    nearRangeTime = nearRangeTime / VEL_C * 2.0;
    if (FC.utc2gps(start.c_str(), &acquisitionStartTime) != 0 ||
        FC.utc2gps(end.c_str(), &acquisitionStopTime) != 0 ||
        !std::isfinite(acquisitionStartTime) || !std::isfinite(acquisitionStopTime) ||
        (istopo_removal && acquisitionStopTime <= acquisitionStartTime)) {
        const QString error = QStringLiteral("Invalid master acquisition time range: start=%1, stop=%2, file=%3")
            .arg(QString::fromStdString(start), QString::fromStdString(end), master_path);
        InSARLogManager::LogError("InterferometricFormationWorker", error);
        emit errorProcess(error);
        return;
    }

    CommonCoverageContract commonCoverage;
    QString commonCoverageError;
    if (!loadCommonCoverageContract(master_path, sceneHeight, commonCoverage, commonCoverageError)) {
        emit errorProcess(commonCoverageError);
        return;
    }
    if (commonCoverage.applies && commonCoverage.geometryReferenceFile != QFileInfo(master_path).fileName()) {
        emit errorProcess(QStringLiteral("共同 burst 几何参考不是实际主图输出：%1").arg(master_path));
        return;
    }
    if (isdeflat) {
        NodeUtils::Hdf5Locker locker;
        const TopsMetadataReadResult masterTopsResult = readTopsBurstPhaseMetadata(master_path, masterTops);
        if (masterTopsResult != TopsMetadataReadResult::Success) {
            emit errorProcess(topsMetadataReadError(QStringLiteral("主"), masterTopsResult, master_path));
            return;
        }
		strictOrbitError.clear();
		if (!readStrictLookSide(master_path, masterLookSide, masterLookSideSource, strictOrbitError)) {
			emit errorProcess(strictOrbitError);
			return;
		}
		masterTops.azimuthIntervalSeconds = 1.0 / prf;
    }
    Mat sourceRowMap;
    const int sourceRowCount = commonCoverage.applies ? commonCoverage.sourceRowCount : offset_row + Master.GetRows();
    if (!sourceRowMapForPhase(commonCoverage, Master.GetRows(), sourceRowCount, offset_row, sourceRowMap)) {
        emit errorProcess(QStringLiteral("共同 burst source-row map 与主影像行数或源场景不一致"));
        return;
    }
    
    int total_pairs = input_paths.size() - 1;
    if (total_pairs <= 0) total_pairs = 1;
    int pair = 1;

    for (int i = 0; i < input_paths.size(); i++)
    {
        if (i == master_index)
        {
            continue;
        }
        else
        {
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                InSARLogManager::LogInfo("InterferometricFormationWorker", "Task cancelled by interruption request.");
                emit cancelled();
                return;
            }
            QString slave_path = input_paths.at(i);
            CommonCoverageContract outputCommonCoverage = commonCoverage;

            CommonCoverageContract slaveCoverage;
            if (!loadCommonCoverageContract(slave_path, sceneHeight, slaveCoverage, commonCoverageError)) {
                emit errorProcess(QStringLiteral("辅影像共同 burst coverage contract 字段读取失败或不合法：%1").arg(slave_path));
                return;
            }
            if (!contractsMatch(commonCoverage, slaveCoverage)) {
                emit errorProcess(QStringLiteral("主辅影像共同 burst mapping 不一致：%1").arg(slave_path));
                return;
            }

            QFileInfo slave_fileinfo(slave_path);
            QString slave_name = slave_fileinfo.baseName();

            QString h5_name = QString("%1_%2").arg(master_name).arg(slave_name);
            QString h5_path = absolute_path + "/" + h5_name + ".h5";
            QString phase_name = QString("%1_%2_phase").arg(master_name).arg(slave_name);
            QString coh_name = QString("%1_%2_coh").arg(master_name).arg(slave_name);
            
            int pair_span = 90 / total_pairs;
            int pair_prog_start = 10 + (pair - 1) * pair_span;
            int pair_prog_end = 10 + pair * pair_span;
            
            emit updateProcess(pair_prog_start, QStringLiteral("生成第%1/%2幅干涉图：正在读取辅影像数据……").arg(pair).arg(total_pairs));
            ComplexMat Slave;
            Mat phase;
            {
                NodeUtils::Hdf5Locker locker;
                ret = FC.read_slc_from_h5(slave_path.toStdString().c_str(), Slave);
            }
            if (ret < 0) {
                const QString error = QStringLiteral("读取辅影像数据失败: %1").arg(slave_path);
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
            
            emit updateProcess(pair_prog_start + pair_span * 0.1, QStringLiteral("生成第%1/%2幅干涉图：正在计算多视相干乘积……").arg(pair).arg(total_pairs));
            if (Master.type() != CV_32F) Master.convertTo(Master, CV_32F);
            if (Slave.type() != CV_32F) Slave.convertTo(Slave, CV_32F);
            Mat slcPairValidMask;
            if (!buildSlcPairValidMask(Master, Slave, slcPairValidMask)) {
                emit errorProcess(QStringLiteral("无法构建主辅影像有效样本掩膜"));
                return;
            }
            TopsMetadataReadResult slaveTopsResult = TopsMetadataReadResult::Success;
			TopsFepV5Orbit slaveFepOrbit;
			QString strictSlaveOrbitError;
            {
                NodeUtils::Hdf5Locker locker;
                ret = NodeUtils::readMatFromH5(slave_path, "state_vec", statevec2) ? 0 : -1;
                if (ret == 0) {
                    ret = NodeUtils::readScalarFromH5(slave_path, "prf", prf2) ? 0 : -1;
                }
                if (ret == 0 && isdeflat) {
                    slaveTopsResult = readTopsBurstPhaseMetadata(slave_path, slaveTops, true);
                }
            }
            if (ret < 0 || statevec2.empty() || !std::isfinite(prf2) || prf2 <= 0.0) {
                const QString error = strictSlaveOrbitError.isEmpty()
                    ? QStringLiteral("辅影像基础字段读取失败或字段值不合法：%1").arg(slave_path)
                    : strictSlaveOrbitError;
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
            if (isdeflat && slaveTopsResult != TopsMetadataReadResult::Success) {
                const QString error = topsMetadataReadError(QStringLiteral("辅"), slaveTopsResult, slave_path);
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
			if (isdeflat) slaveTops.azimuthIntervalSeconds = 1.0 / prf2;
            if (isdeflat && !commonCoverageMapsInsideSlaveMetadata(commonCoverage, slaveCoverage, slaveTops)) {
                const QString error = QStringLiteral("共同 burst 映射越界：主 burst 范围经辅影像 burst offset 后超出辅影像 TOPS 元数据范围：%1")
                    .arg(slave_path);
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
			Mat flatEarthPhase;
			TopsFepV5Provenance flatEarthProvenance;
			Mat correctionReference = Mat::zeros(Master.GetRows(), Master.GetCols(), CV_64F);
            
            if (isdeflat)
            {
                g_substep_prog_start = pair_prog_start + pair_span * 0.2;
                g_substep_prog_end = pair_prog_start + pair_span * 0.4;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在消除平地相位").arg(pair).arg(total_pairs);

                TopsFepV5Options fepOptions;
                fepOptions.masterLookSide = masterLookSide;
				constexpr double kInterpolationMarginSeconds = 10.0;
				double masterGeometryStart = 0.0;
				double masterGeometryStop = 0.0;
				double slaveGeometryStart = 0.0;
				double slaveGeometryStop = 0.0;
				if (!nativeTopsGeometryCoverage(masterTops, masterTops.linesPerBurst, sourceRowMap, 0, false, 0.0,
						masterGeometryStart, masterGeometryStop) ||
					!nativeTopsGeometryCoverage(slaveTops, masterTops.linesPerBurst, sourceRowMap, slaveCoverage.sourceBurstOffset, true,
						fepOptions.slaveSearchMaximumHalfWindowSeconds, slaveGeometryStart, slaveGeometryStop) ||
					!readStrictTopsV5Orbit(master_path, masterGeometryStart, masterGeometryStop,
						kInterpolationMarginSeconds, masterFepOrbit, strictOrbitError) ||
					!readStrictTopsV5Orbit(slave_path, slaveGeometryStart, slaveGeometryStop,
						kInterpolationMarginSeconds, slaveFepOrbit, strictSlaveOrbitError)) {
					const QString error = !strictOrbitError.isEmpty() ? strictOrbitError : strictSlaveOrbitError;
					InSARLogManager::LogError("InterferometricFormationWorker", error);
					emit errorProcess(error);
					return;
				}
				InSARLogManager::LogInfo("InterferometricFormationWorker",
					QStringLiteral("平地相位轨道策略已锁定：主=%1，从=%2。").arg(
						QString::fromStdString(masterFepOrbit.interpolationStrategy),
						QString::fromStdString(slaveFepOrbit.interpolationStrategy)));
				int ret_deflat = flat.computeSentinel1FlatEarthPhaseV5(
					masterFepOrbit, slaveFepOrbit, masterTops, slaveTops, sourceRowMap, slcPairValidMask,
					sourceRowCount, Master.GetCols(), offset_col, slaveCoverage.sourceBurstOffset, 1, wavelength,
					fepOptions, flatEarthPhase, flatEarthProvenance, DeflatProgressCallbackImpl);

                if (ret_deflat == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Deflat process cancelled by user.");
                    emit cancelled();
                    return;
                }
                else if (ret_deflat < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Deflat process failed.");
                    emit errorProcess(QStringLiteral("平地相位消除失败"));
                    return;
                }
                correctionReference = flatEarthPhase;
            }

            if (istopo_removal)
            {
                g_substep_prog_start = pair_prog_start + pair_span * 0.6;
                g_substep_prog_end = pair_prog_start + pair_span * 0.8;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在进行地形相位模拟").arg(pair).arg(total_pairs);

                Mat topographyPhase;
                int ret_topo = computeSourceRowAwareTopography(flat, commonCoverage, statevec, statevec2, lon_coef, lat_coef,
                    inc_coef, prf, prf2, sceneWidth, offset_row, offset_col, nearRangeTime, rangeSpacing, wavelength,
                    acquisitionStartTime, acquisitionStopTime, demPath, sourceRowMap, topographyPhase);

                if (ret_topo == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Topography simulation cancelled by user.");
                    emit cancelled();
                    return;
                }
                else if (ret_topo < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Topography simulation failed.");
                    emit errorProcess(QStringLiteral("地形相位模拟失败"));
                    return;
                }
                else {
                    correctionReference += topographyPhase;
                }
            }

            Mat outputFlatEarthReference;
            Mat phaseValidMask;
            Mat phaseValidSampleCount;
			Mat correctedInterferogramReal;
			Mat correctedInterferogramImaginary;
            const int correctedMultilookResult = util.multilookCorrectedInterferogram(
                Master, Slave, correctionReference, flatEarthPhase, sourceRowMap, slcPairValidMask,
                multilook_rg, multilook_az, phase, outputFlatEarthReference, phaseValidMask,
				phaseValidSampleCount, DeflatProgressCallbackImpl,
				&correctedInterferogramReal, &correctedInterferogramImaginary);
            if (correctedMultilookResult == -2) {
                InSARLogManager::LogInfo("InterferometricFormationWorker", "Corrected interferogram multilooking cancelled by user.");
                emit cancelled();
                return;
            }
            if (correctedMultilookResult != 0 || phase.empty()) {
                emit errorProcess(QStringLiteral("复干涉量参考校正或多视处理失败"));
                return;
            }
            if (phaseValidMask.type() != CV_8U || phaseValidMask.size() != phase.size() ||
                phaseValidSampleCount.type() != CV_32S || phaseValidSampleCount.size() != phase.size()) {
                emit errorProcess(QStringLiteral("相位有效性契约与多视输出网格不一致"));
                return;
            }
			if (correctedInterferogramReal.type() != CV_32F ||
				correctedInterferogramImaginary.type() != CV_32F ||
				correctedInterferogramReal.size() != phase.size() ||
				correctedInterferogramImaginary.size() != phase.size()) {
				emit errorProcess(QStringLiteral("校正多视复干涉图 I/Q 与相位输出网格不一致"));
				return;
			}
			Mat complexGamma;
			Mat complexGammaValidMask;
			Mat complexGammaValidSampleCount;
			const int complexGammaResult = util.complex_coherence_corrected_multilooked(
				Master, Slave, correctionReference, sourceRowMap, slcPairValidMask,
				multilook_rg, multilook_az, win_width, win_height,
				complexGamma, complexGammaValidMask, complexGammaValidSampleCount,
				DeflatProgressCallbackImpl);
			if (complexGammaResult == -2) {
				InSARLogManager::LogInfo("InterferometricFormationWorker", "Complex-gamma calculation cancelled by user.");
				emit cancelled();
				return;
			}
			if (complexGammaResult != 0 || complexGamma.type() != CV_64F ||
				complexGammaValidMask.type() != CV_8U || complexGammaValidSampleCount.type() != CV_32S ||
				complexGamma.size() != phase.size() || complexGammaValidMask.size() != phase.size() ||
				complexGammaValidSampleCount.size() != phase.size()) {
				emit errorProcess(QStringLiteral("校正多视复相干系数 gamma 与相位输出网格不一致或计算失败"));
				return;
			}

            if (outputCommonCoverage.applies && multilook_az == 1 &&
                outputCommonCoverage.sourceRowMap.rows != phase.rows) {
                emit errorProcess(QStringLiteral("共同 burst source-row map 与相位行数不一致"));
                return;
            }
            if (outputCommonCoverage.applies && multilook_az > 1)
            {
                const cv::Mat sourceRowMapBeforeMultilook = outputCommonCoverage.sourceRowMap;
                std::vector<int> representatives;
                representatives.reserve(static_cast<size_t>(phase.rows));
                for (const CommonCoverageRun& run : outputCommonCoverage.runs) {
                    const int runOutputRows = run.rowCount / multilook_az;
                    for (int block = 0; block < runOutputRows; ++block) {
                        const int representativeRow = run.outputFirstRow +
                            block * multilook_az + multilook_az / 2;
                        if (representativeRow < run.outputFirstRow ||
                            representativeRow >= run.outputFirstRow + run.rowCount ||
                            representativeRow >= sourceRowMapBeforeMultilook.rows) {
                            emit errorProcess(QStringLiteral("共同 burst 多视 source-row representative 越界"));
                            return;
                        }
                        representatives.push_back(
                            sourceRowMapBeforeMultilook.at<int>(representativeRow, 0));
                    }
                }
                if (representatives.empty() || static_cast<int>(representatives.size()) != phase.rows) {
                    emit errorProcess(QStringLiteral("共同 burst source-row map 与多视相位行数不一致"));
                    return;
                }
                cv::Mat multilookSourceRowMap(static_cast<int>(representatives.size()), 1, CV_32S);
                for (int row = 0; row < multilookSourceRowMap.rows; ++row) {
                    multilookSourceRowMap.at<int>(row, 0) = representatives[row];
                }
                outputCommonCoverage.sourceRowMap = multilookSourceRowMap;
                outputCommonCoverage.sourceRowOrigin = multilookSourceRowMap.at<int>(0, 0);
                outputCommonCoverage.sourceRowMapSemantics = QStringLiteral("multilook_azimuth_block_center_v1");
                outputCommonCoverage.sourceRowMapAzimuthFactor *= multilook_az;
            }

            if (isdeflat && (outputFlatEarthReference.type() != CV_64F ||
                             outputFlatEarthReference.size() != phase.size()))
            {
                emit errorProcess(QStringLiteral("平地相位参考场与多视输出网格不一致"));
                return;
            }
			Mat flatEarthBurstStatistics;
			Mat flatEarthSolverBurstStatistics;
			Mat flatEarthFailureBurstStatistics;
			const bool hasRegistrationRerampProvenance =
				slaveTops.registrationRerampPhase.type() == CV_64F &&
				slaveTops.registrationRerampPhase.size() == phase.size() &&
				cv::checkRange(slaveTops.registrationRerampPhase, true, nullptr);
			if (isdeflat) {
				flatEarthBurstStatistics = Mat::zeros(static_cast<int>(flatEarthProvenance.burstStatistics.size()), 13, CV_64F);
				flatEarthSolverBurstStatistics = Mat::zeros(flatEarthBurstStatistics.rows, 5, CV_64F);
				flatEarthFailureBurstStatistics = Mat::zeros(flatEarthBurstStatistics.rows, 3, CV_64F);
				for (int burst = 0; burst < flatEarthBurstStatistics.rows; ++burst) {
					const TopsFepV5BurstStatistics& stats = flatEarthProvenance.burstStatistics[burst];
					flatEarthBurstStatistics.at<double>(burst, 0) = static_cast<double>(stats.solvedSamples);
					flatEarthBurstStatistics.at<double>(burst, 1) = static_cast<double>(stats.nativeSupportMaskedSamples);
					flatEarthBurstStatistics.at<double>(burst, 2) = stats.maxRdeIterations;
					flatEarthBurstStatistics.at<double>(burst, 3) = stats.maxZeroDopplerIterations;
					flatEarthBurstStatistics.at<double>(burst, 4) = stats.maxMasterRangeResidual;
					flatEarthBurstStatistics.at<double>(burst, 5) = stats.maxMasterZeroDopplerResidual;
					flatEarthBurstStatistics.at<double>(burst, 6) = stats.maxSlaveZeroDopplerResidual;
					flatEarthBurstStatistics.at<double>(burst, 7) = stats.maxEllipsoidResidual;
					flatEarthBurstStatistics.at<double>(burst, 8) = stats.maxJacobianCondition;
					flatEarthBurstStatistics.at<double>(burst, 9) = stats.maxLastGeometryPhaseChange;
					flatEarthBurstStatistics.at<double>(burst, 10) = stats.maxDifferentialRangeErrorBound;
					flatEarthBurstStatistics.at<double>(burst, 11) = stats.maxSlaveSearchHalfWindowSeconds;
					flatEarthBurstStatistics.at<double>(burst, 12) = stats.maxSlaveSearchExpansions;
					flatEarthSolverBurstStatistics.at<double>(burst, 0) = static_cast<double>(stats.solvedSamples);
					flatEarthSolverBurstStatistics.at<double>(burst, 1) = static_cast<double>(stats.totalRdeIterations);
					flatEarthSolverBurstStatistics.at<double>(burst, 2) = static_cast<double>(stats.totalZeroDopplerIterations);
					flatEarthSolverBurstStatistics.at<double>(burst, 3) = static_cast<double>(stats.totalSlaveDopplerEvaluations);
					flatEarthSolverBurstStatistics.at<double>(burst, 4) = stats.maxSlaveDopplerEvaluations;
					flatEarthFailureBurstStatistics.at<double>(burst, 0) = static_cast<double>(stats.masterRdeFailureCount);
					flatEarthFailureBurstStatistics.at<double>(burst, 1) = static_cast<double>(stats.slaveZeroDopplerFailureCount);
					flatEarthFailureBurstStatistics.at<double>(burst, 2) = static_cast<double>(stats.closureFailureCount);
				}
			}

            {
                NodeUtils::Hdf5Locker locker;
                const int outputSceneHeight = phase.rows;
                const int outputSceneWidth = phase.cols;
                int outputSceneHeightValue = outputSceneHeight;
                int outputSceneWidthValue = outputSceneWidth;
                ret = FC.creat_new_h5(h5_path.toStdString().c_str());
                if (ret >= 0) {
                    if ((isdeflat &&
                         (!writeArray(h5_path, "flat_earth_reference_phase", outputFlatEarthReference) ||
						  !writeArray(h5_path, "flat_earth_rde_burst_statistics", flatEarthBurstStatistics) ||
						  !writeArray(h5_path, "flat_earth_solver_burst_statistics", flatEarthSolverBurstStatistics) ||
						  !writeArray(h5_path, "flat_earth_failure_burst_statistics", flatEarthFailureBurstStatistics) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_model_version", 6) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_model_source_row_count", sourceRowCount) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_orbit_osv_start_gps", flatEarthProvenance.masterOrbit.osvStartGps) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_orbit_osv_stop_gps", flatEarthProvenance.masterOrbit.osvStopGps) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_geometry_start_gps", flatEarthProvenance.masterOrbit.geometryStartGps) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_geometry_stop_gps", flatEarthProvenance.masterOrbit.geometryStopGps) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_orbit_osv_start_gps", flatEarthProvenance.slaveOrbit.osvStartGps) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_orbit_osv_stop_gps", flatEarthProvenance.slaveOrbit.osvStopGps) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_geometry_start_gps", flatEarthProvenance.slaveOrbit.geometryStartGps) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_geometry_stop_gps", flatEarthProvenance.slaveOrbit.geometryStopGps) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_orbit_interpolation_margin_seconds", flatEarthProvenance.masterOrbit.interpolationMarginSeconds) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_rde_epsilon_phase", flatEarthProvenance.options.epsilonPhase) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_rde_max_residual", flatEarthProvenance.options.maxRdeResidual) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_zero_doppler_max_residual", flatEarthProvenance.options.maxZeroDopplerResidual) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_rde_max_jacobian_condition", flatEarthProvenance.options.maxJacobianCondition) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_search_initial_half_window_seconds", flatEarthProvenance.options.slaveSearchHalfWindowSeconds) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_search_maximum_half_window_seconds", flatEarthProvenance.options.slaveSearchMaximumHalfWindowSeconds) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_search_expansion_factor", flatEarthProvenance.options.slaveSearchExpansionFactor) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_search_max_expansions", flatEarthProvenance.options.maxSlaveSearchExpansions) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_rde_max_iterations", flatEarthProvenance.options.maxRdeIterations) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_zero_doppler_max_iterations", flatEarthProvenance.options.maxZeroDopplerIterations) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_look_side", flatEarthProvenance.options.masterLookSide) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_master_look_side_source"),
							  masterLookSideSource.toStdString()) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_wavelength_meters", wavelength) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_transmit_receive_mode", 1) ||
						  !writeArray(h5_path, "flat_earth_master_burst_azimuth_time", masterTops.burstAzimuthTime) ||
						  !writeArray(h5_path, "flat_earth_slave_burst_azimuth_time", slaveTops.burstAzimuthTime) ||
						  !writeArray(h5_path, "flat_earth_master_azimuth_fm_rate_list", masterTops.azimuthFmRateList) ||
						  !writeArray(h5_path, "flat_earth_slave_azimuth_fm_rate_list", slaveTops.azimuthFmRateList) ||
						  !writeArray(h5_path, "flat_earth_master_dc_estimate_list", masterTops.dcEstimateList) ||
						  !writeArray(h5_path, "flat_earth_slave_dc_estimate_list", slaveTops.dcEstimateList) ||
						  !writeArray(h5_path, "flat_earth_master_first_valid_line", masterTops.firstValidLine) ||
						  !writeArray(h5_path, "flat_earth_master_last_valid_line", masterTops.lastValidLine) ||
						  !writeArray(h5_path, "flat_earth_slave_first_valid_line", slaveTops.firstValidLine) ||
						  !writeArray(h5_path, "flat_earth_slave_last_valid_line", slaveTops.lastValidLine) ||
						  !writeArray(h5_path, "flat_earth_master_first_valid_sample", masterTops.firstValidSample) ||
						  !writeArray(h5_path, "flat_earth_master_last_valid_sample", masterTops.lastValidSample) ||
						  !writeArray(h5_path, "flat_earth_slave_first_valid_sample", slaveTops.firstValidSample) ||
						  !writeArray(h5_path, "flat_earth_slave_last_valid_sample", slaveTops.lastValidSample) ||
						  (hasRegistrationRerampProvenance &&
						   !writeArray(h5_path, "flat_earth_slave_registration_reramp_phase", slaveTops.registrationRerampPhase)) ||
						  !writeArray(h5_path, "flat_earth_slave_registration_mapping_coefficients", slaveTops.registrationMappingCoefficients) ||
						  !writeArray(h5_path, "flat_earth_slave_registration_mapping_master_burst_indices", slaveTops.registrationMappingMasterBurstIndices) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_lines_per_burst", masterTops.linesPerBurst) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_lines_per_burst", slaveTops.linesPerBurst) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_azimuth_steering_rate", masterTops.azimuthSteeringRate) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_azimuth_steering_rate", slaveTops.azimuthSteeringRate) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_range_spacing", masterTops.rangeSpacing) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_range_spacing", slaveTops.rangeSpacing) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_azimuth_interval_seconds", masterTops.azimuthIntervalSeconds) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_azimuth_interval_seconds", slaveTops.azimuthIntervalSeconds) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_master_slant_range_first_pixel", masterTops.slantRangeFirstPixel) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_slant_range_first_pixel", slaveTops.slantRangeFirstPixel) ||
						  !NodeUtils::writeScalarToH5(h5_path, "flat_earth_slave_source_burst_offset", slaveCoverage.sourceBurstOffset) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_model_source_row_semantics"),
							  std::string("source_row_map_selects_master_native_burst_line_only_v1")) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_geolocation_coordinate_semantics"),
							  std::string("master_native_line_sample_to_h0_rde__slave_zero_doppler_range_v1")) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_algorithm_entry"),
							  std::string("deflat_compute_sentinel1_flat_earth_phase_v5")) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_geometry_branch"),
							  std::string("master_native_range_doppler_h0__slave_independent_zero_doppler__registration_time_seed_only_v1")) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_model_timing_semantics"),
							  std::string("strict_gps_h5_time_v2__registration_time_seed_not_geometry_truth_v1")) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_processing_phase_semantics"),
							  std::string("master_native_phase;slave_registration_mapping_seed_only_m_conjugate_s_v2")) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_slave_registration_mapping_semantics"),
							  std::string("pull_source_row_and_column_offsets_a0_a1_column_a2_master_burst_line_v1")) ||
						  (hasRegistrationRerampProvenance &&
						   !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_slave_registration_reramp_phase_semantics"),
							   std::string("resampled_slave_deramp_demod_phase_registration_only_v1"))) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_model_status"),
							  std::string("tops_native_range_doppler_h0_geometry_only_reference_v2")) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_master_orbit_source"),
							  flatEarthProvenance.masterOrbit.source) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_master_orbit_selection_reason"),
							  flatEarthProvenance.masterOrbit.selectionReason) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_slave_orbit_source"),
							  flatEarthProvenance.slaveOrbit.source) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_slave_orbit_selection_reason"),
							  flatEarthProvenance.slaveOrbit.selectionReason) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_orbit_time_scale"),
							  std::string("GPS")) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_orbit_interpolation_strategy"),
							  flatEarthProvenance.masterOrbit.interpolationStrategy) ||
						  !NodeUtils::writeStringToH5(h5_path, QStringLiteral("flat_earth_reference_phase_semantics"),
							  std::string("unwrapped_master_native_h0_rde_geometry_only_reference_v6")))) ||
                        !NodeUtils::writeScalarToH5(h5_path, "phase_processing_schema_version", isdeflat ? 2 : 1) ||
                        !NodeUtils::writeScalarToH5(h5_path, "phase_flat_earth_removed", isdeflat ? 1 : 0) ||
                        !NodeUtils::writeScalarToH5(h5_path, "phase_topography_removed", istopo_removal ? 1 : 0) ||
                        (istopo_removal &&
                         (!NodeUtils::writeScalarToH5(h5_path, "terrain_dem_coverage_schema_version", 2) ||
                          !NodeUtils::writeScalarToH5(h5_path, "terrain_dem_mask_total_pixel_count",
                              static_cast<double>(demMaskTotalPixelCount)) ||
                          !NodeUtils::writeScalarToH5(h5_path, "terrain_dem_mask_valid_pixel_count",
                              static_cast<double>(demMaskValidPixelCount)) ||
                          !NodeUtils::writeStringToH5(h5_path, "terrain_dem_coverage_status",
                              std::string("full_source_support_verified")))) ||
                        !writeArray(h5_path, "range_len", Mat(1, 1, CV_32S, &outputSceneWidthValue)) ||
                        !writeArray(h5_path, "azimuth_len", Mat(1, 1, CV_32S, &outputSceneHeightValue)) ||
                        !writeArray(h5_path, "multilook_rg", Mat(1, 1, CV_32S, &multilook_rg)) ||
                        !writeArray(h5_path, "multilook_az", Mat(1, 1, CV_32S, &multilook_az)) ||
                        !writeArray(h5_path, "phase_valid_mask", phaseValidMask) ||
                        !writeArray(h5_path, "phase_valid_sample_count", phaseValidSampleCount) ||
						!writeArray(h5_path, "interferogram_i", correctedInterferogramReal) ||
						!writeArray(h5_path, "interferogram_q", correctedInterferogramImaginary) ||
						!writeArray(h5_path, "complex_gamma", complexGamma) ||
						!writeArray(h5_path, "complex_gamma_valid_mask", complexGammaValidMask) ||
						!writeArray(h5_path, "complex_gamma_valid_sample_count", complexGammaValidSampleCount) ||
						!NodeUtils::writeScalarToH5(h5_path, "complex_gamma_window_range", win_width) ||
						!NodeUtils::writeScalarToH5(h5_path, "complex_gamma_window_azimuth", win_height) ||
						!NodeUtils::writeStringToH5(h5_path, "complex_gamma_semantics",
							NodeUtils::CoherenceSemantics::kComplexGamma) ||
						!NodeUtils::writeStringToH5(h5_path, "complex_gamma_algorithm",
							"corrected_multilooked_source_row_aware_v1") ||
                        !writeArray(h5_path, "phase", phase)) {
                        return;
                    }
                    QString sourcePathMetadataError;
                    if (!NodeUtils::writeSourcePathMetadata(h5_path, master_path.toStdString(),
                                                            slave_path.toStdString(), &sourcePathMetadataError)) {
                        emit errorProcess(QStringLiteral("写入源路径元数据失败: %1").arg(sourcePathMetadataError));
                        return;
                    }
                    if (!writeCommonCoverageContract(h5_path, outputCommonCoverage, master_path, sourcePathMetadataError)) {
                        emit errorProcess(QStringLiteral("写入共同 burst coverage provenance 失败: %1").arg(sourcePathMetadataError));
                        return;
                    }
                }
            }
            if (ret < 0) {
                emit errorProcess(QStringLiteral("创建干涉H5文件失败: ") + h5_path);
                return;
            }

            if (iscoherence)
            {
                g_substep_prog_start = pair_prog_start + pair_span * 0.8;
                g_substep_prog_end = pair_prog_start + pair_span * 0.98;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在计算二倍角相位集中度").arg(pair).arg(total_pairs);

                if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
                {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Task cancelled by interruption request inside coherence block.");
                    emit cancelled();
                    return;
                }
                Mat coherence;
                Mat coherencePhaseValidMask;
                Mat validSampleCount;
                if (!reduceStrictValidMaskCommonCoverage(
                        outputCommonCoverage, slcPairValidMask, multilook_rg,
                        multilook_az, coherencePhaseValidMask) ||
                    coherencePhaseValidMask.size() != phase.size()) {
                    emit errorProcess(QStringLiteral("相干性有效样本掩膜与相位网格不一致"));
                    return;
                }
                cv::bitwise_and(coherencePhaseValidMask, phaseValidMask, coherencePhaseValidMask);
                int ret_coh = util.phase_axial_concentration(phase, win_width, win_height, coherence, DeflatProgressCallbackImpl);
                if (ret_coh == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Coherence calculation cancelled by user.");
                    emit cancelled();
                    return;
                }
                else if (ret_coh < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Coherence calculation failed.");
                    emit errorProcess(QStringLiteral("二倍角相位集中度计算失败"));
                    return;
                }
                if (!buildCoherenceSupportCount(
                        coherencePhaseValidMask, win_width, win_height, validSampleCount) ||
                    validSampleCount.size() != coherence.size()) {
                    emit errorProcess(QStringLiteral("相干性有效样本支持数计算失败"));
                    return;
                }

                {
                    NodeUtils::Hdf5Locker locker;
                    if (!writeArray(h5_path, "coherence", coherence) ||
                        !writeArray(h5_path,
                                    NodeUtils::CoherenceSupport::kValidSampleCountDataset,
                                    validSampleCount) ||
                        !NodeUtils::writeScalarToH5(
                            h5_path,
                            QString::fromLatin1(NodeUtils::CoherenceSupport::kWindowRangeDataset),
                            win_width) ||
                        !NodeUtils::writeScalarToH5(
                            h5_path,
                            QString::fromLatin1(NodeUtils::CoherenceSupport::kWindowAzimuthDataset),
                            win_height)) {
                        return;
                    }
                    // 标注该数据集的语义：当前由 Utils::phase_axial_concentration() 生成，
                    // 即二倍角轴向集中度 R2，并非复相干系数 gamma。
                    // 下游必须按标签解释数值，不可假定为 gamma。
                    QString semanticsError;
                    if (!NodeUtils::writeCoherenceSemantics(
                            h5_path,
                            QString::fromLatin1(NodeUtils::CoherenceSemantics::kPhaseAxialR2),
                            &semanticsError)) {
                        emit errorProcess(QStringLiteral("写入相干性语义标签失败: %1").arg(semanticsError));
                        return;
                    }
                }
            }

            InterferogramFileResult fileRes;
            fileRes.phaseName = phase_name;
            fileRes.cohName = coh_name;
            fileRes.h5Path = h5_path;
            fileRes.relativePath = "/" + file_name + "/" + h5_name + ".h5";
            fileRes.masterName = master_name;
            fileRes.offsetRow = offset_row;
            fileRes.offsetCol = offset_col;
            fileRes.isDeflat = isdeflat;
            fileRes.isTopoRemoval = istopo_removal;
            fileRes.isCoherence = iscoherence;
            fileRes.winWidth = win_width;
            fileRes.winHeight = win_height;
            fileRes.multilookRg = multilook_rg;
            fileRes.multilookAz = multilook_az;
            Q_EMIT interferogramGenerated(fileRes);
            
            emit updateProcess(pair_prog_end, QStringLiteral("生成第%1/%2幅干涉图已完成").arg(pair).arg(total_pairs));
            pair++;
        }
    }
    
    InSARLogManager::LogInfo("InterferometricFormationWorker", "Task completed: Interferometric");
    emit endProcess();
}
