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
    contract.runs.clear();
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

static int applySegmentedDeflat(Deflat& flat, const CommonCoverageContract& contract,
                                const Mat& stateVec1, const Mat& stateVec2, const Mat& lonCoef, const Mat& latCoef,
                                const Mat& phase, int offsetRow, int offsetCol, double prf1, double prf2,
                                double wavelength, Mat& correctedPhase, Mat& segmentCoefficients)
{
    if (!contract.applies) {
        return flat.deflat(stateVec1, stateVec2, lonCoef, latCoef, phase, offsetRow, offsetCol, 0,
                           1 / prf1, 1 / prf2, 1, wavelength, correctedPhase, segmentCoefficients,
                           DeflatProgressCallbackImpl);
    }
    correctedPhase.create(phase.rows, phase.cols, phase.type());
    segmentCoefficients.create(static_cast<int>(contract.runs.size()), 6, CV_64F);
    for (int index = 0; index < static_cast<int>(contract.runs.size()); ++index) {
        const CommonCoverageRun& run = contract.runs[index];
        Mat segmentPhase = phase.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount);
        Mat correctedSegment;
        Mat coefficient;
        const int result = flat.deflat(stateVec1, stateVec2, lonCoef, latCoef, segmentPhase,
                                       run.sourceFirstRow, offsetCol, 0, 1 / prf1, 1 / prf2, 1, wavelength,
                                       correctedSegment, coefficient, DeflatProgressCallbackImpl);
        if (result != 0 || correctedSegment.size() != segmentPhase.size() || coefficient.type() != CV_64F ||
            coefficient.rows != 1 || coefficient.cols != 6) return result == 0 ? -1 : result;
        correctedSegment.copyTo(correctedPhase.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount));
        coefficient.copyTo(segmentCoefficients.row(index));
    }
    return 0;
}

static int applySegmentedTopography(Deflat& flat, const CommonCoverageContract& contract,
                                    Mat& stateVec1, Mat& stateVec2, Mat& lonCoef, Mat& latCoef, Mat& incCoef,
                                    double prf1, double prf2, int sceneWidth, int offsetRow, int offsetCol,
                                    double nearRangeTime, double rangeSpacing, double wavelength,
                                    double acquisitionStartTime, double acquisitionStopTime, const QString& demPath,
                                    const Mat& inputPhase, Mat& outputPhase)
{
    if (!contract.applies) {
        Mat topographyPhase;
        const int result = flat.topography_simulation(topographyPhase, stateVec1, stateVec2, lonCoef, latCoef, incCoef,
            prf1, prf2, inputPhase.rows, sceneWidth, offsetRow, offsetCol, nearRangeTime, rangeSpacing, wavelength,
            acquisitionStartTime, acquisitionStopTime, demPath.toStdString().c_str(), 20, DeflatProgressCallbackImpl);
        if (result != 0 || topographyPhase.size() != inputPhase.size()) return result == 0 ? -1 : result;
        outputPhase = inputPhase - topographyPhase;
        Utils util;
        return util.wrap(outputPhase, outputPhase);
    }
    outputPhase.create(inputPhase.rows, inputPhase.cols, inputPhase.type());
    for (const CommonCoverageRun& run : contract.runs) {
        Mat inputSegment = inputPhase.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount);
        Mat topographySegment;
        const int result = flat.topography_simulation(topographySegment, stateVec1, stateVec2, lonCoef, latCoef, incCoef,
            prf1, prf2, run.rowCount, sceneWidth, run.sourceFirstRow, offsetCol, nearRangeTime, rangeSpacing, wavelength,
            acquisitionStartTime, acquisitionStopTime, demPath.toStdString().c_str(), 20, DeflatProgressCallbackImpl);
        if (result != 0 || topographySegment.size() != inputSegment.size()) return result == 0 ? -1 : result;
        Mat correctedSegment = inputSegment - topographySegment;
        Utils util;
        if (util.wrap(correctedSegment, correctedSegment) != 0) return -1;
        correctedSegment.copyTo(outputPhase.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount));
    }
    return 0;
}

static int multilookCommonCoveragePhase(Utils& util,
                                        const CommonCoverageContract& contract,
                                        const Mat& inputPhase,
                                        Mat& outputPhase,
                                        int multilook_rg,
                                        int multilook_az)
{
    if (!contract.applies || multilook_az <= 1) {
        return util.multilook(inputPhase, outputPhase, multilook_rg, multilook_az);
    }
    if (inputPhase.empty() || inputPhase.rows != contract.sourceRowMap.rows ||
        multilook_rg < 1 || multilook_az < 1 || contract.runs.empty()) {
        return -1;
    }

    std::vector<Mat> runOutputs;
    runOutputs.reserve(contract.runs.size());
    int expectedRows = 0;
    for (const CommonCoverageRun& run : contract.runs) {
        if (run.outputFirstRow < 0 || run.rowCount <= 0 ||
            run.outputFirstRow + run.rowCount > inputPhase.rows) {
            return -1;
        }
        const int runOutputRows = run.rowCount / multilook_az;
        if (runOutputRows <= 0) continue;

        Mat runOutput;
        const int result = util.multilook(
            inputPhase.rowRange(run.outputFirstRow, run.outputFirstRow + run.rowCount),
            runOutput, multilook_rg, multilook_az);
        if (result != 0 || runOutput.empty() || runOutput.rows != runOutputRows) {
            return result == 0 ? -1 : result;
        }
        runOutputs.push_back(runOutput);
        expectedRows += runOutputRows;
    }
    if (runOutputs.empty() || expectedRows <= 0) return -1;
    cv::vconcat(runOutputs, outputPhase);
    return outputPhase.rows == expectedRows ? 0 : -1;
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
            if (!loadCommonCoverageContract(slave_path, sceneHeight, slaveCoverage, commonCoverageError) ||
                !contractsMatch(commonCoverage, slaveCoverage)) {
                emit errorProcess(QStringLiteral("主辅干涉输入的共同 burst coverage contract 不一致：%1").arg(slave_path));
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
            if (iscoherence && !buildSlcPairValidMask(Master, Slave, slcPairValidMask)) {
                emit errorProcess(QStringLiteral("无法构建主辅影像有效样本掩膜"));
                return;
            }
            ret = util.Multilook(Master, Slave, 1, 1, phase);
            if (ret < 0 || phase.empty()) {
                const QString error = QStringLiteral("干涉相位计算失败: %1").arg(slave_path);
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
            
            {
                NodeUtils::Hdf5Locker locker;
                ret = NodeUtils::readMatFromH5(slave_path, "state_vec", statevec2) ? 0 : -1;
                if (ret == 0) {
                    ret = NodeUtils::readScalarFromH5(slave_path, "prf", prf2) ? 0 : -1;
                }
            }
            if (ret < 0 || statevec2.empty() || !std::isfinite(prf2) || prf2 <= 0.0) {
                const QString error = QStringLiteral("Slave image metadata is incomplete or invalid: %1").arg(slave_path);
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
            Mat phase_deflatted, flat_phase_coefficient;
            
            if (isdeflat)
            {
                g_substep_prog_start = pair_prog_start + pair_span * 0.2;
                g_substep_prog_end = pair_prog_start + pair_span * 0.4;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在消除平地相位").arg(pair).arg(total_pairs);

                {
                    NodeUtils::Hdf5Locker locker;
                    NodeUtils::readMatFromH5(slave_path, "state_vec", statevec2);
                    NodeUtils::readScalarFromH5(slave_path, "prf", prf2);
                }

                int ret_deflat = applySegmentedDeflat(flat, commonCoverage, statevec, statevec2, lon_coef, lat_coef,
                    phase, offset_row, offset_col, prf, prf2, wavelength, phase_deflatted, flat_phase_coefficient);
                phase_deflatted.copyTo(phase);

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
            }

            if (istopo_removal)
            {
                g_substep_prog_start = pair_prog_start + pair_span * 0.6;
                g_substep_prog_end = pair_prog_start + pair_span * 0.8;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在进行地形相位模拟").arg(pair).arg(total_pairs);

                if (!isdeflat) phase_deflatted = phase.clone();
                Mat topographyCorrected;
                int ret_topo = applySegmentedTopography(flat, commonCoverage, statevec, statevec2, lon_coef, lat_coef,
                    inc_coef, prf, prf2, sceneWidth, offset_row, offset_col, nearRangeTime, rangeSpacing, wavelength,
                    acquisitionStartTime, acquisitionStopTime, demPath, phase_deflatted, topographyCorrected);

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
                    phase_deflatted = topographyCorrected;
                    phase_deflatted.copyTo(phase);
                }
            }

            if (multilook_rg > 1 || multilook_az > 1)
            {
                if (multilookCommonCoveragePhase(util, outputCommonCoverage, phase,
                                                 phase_deflatted, multilook_rg, multilook_az) < 0 ||
                    phase_deflatted.empty()) {
                    emit errorProcess(QStringLiteral("干涉相位多视处理失败"));
                    return;
                }
                phase_deflatted.copyTo(phase);
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

            if (isdeflat && flat_phase_coefficient.empty())
            {
                emit errorProcess(QStringLiteral("平地相位消除未生成有效系数"));
                return;
            }

            {
                NodeUtils::Hdf5Locker locker;
                const int outputSceneHeight = commonCoverage.applies ? phase.rows : sceneHeight;
                int outputSceneHeightValue = outputSceneHeight;
                ret = FC.creat_new_h5(h5_path.toStdString().c_str());
                if (ret >= 0) {
                    if ((!flat_phase_coefficient.empty() &&
                         !writeArray(h5_path, "flat_phase_coefficient", flat_phase_coefficient)) ||
                        (commonCoverage.applies && !flat_phase_coefficient.empty() &&
                         !writeArray(h5_path, "s1_tops_segment_flat_phase_coefficients", flat_phase_coefficient)) ||
                        (commonCoverage.applies && !flat_phase_coefficient.empty() &&
                         !NodeUtils::writeStringToH5(h5_path,
                             QStringLiteral("s1_tops_flat_phase_coefficient_contract"),
                             std::string("per_source_row_run_v1; flat_phase_coefficient rows are not a single-scene model; use s1_tops_output_source_row_map"))) ||
                        !NodeUtils::writeScalarToH5(h5_path, "phase_processing_schema_version", 1) ||
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
                        !writeArray(h5_path, "range_len", Mat(1, 1, CV_32S, &sceneWidth)) ||
                        !writeArray(h5_path, "azimuth_len", Mat(1, 1, CV_32S, &outputSceneHeightValue)) ||
                        !writeArray(h5_path, "multilook_rg", Mat(1, 1, CV_32S, &multilook_rg)) ||
                        !writeArray(h5_path, "multilook_az", Mat(1, 1, CV_32S, &multilook_az)) ||
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
                Mat phaseValidMask;
                Mat validSampleCount;
                if (!reduceStrictValidMaskCommonCoverage(
                        outputCommonCoverage, slcPairValidMask, multilook_rg,
                        multilook_az, phaseValidMask) ||
                    phaseValidMask.size() != phase.size()) {
                    emit errorProcess(QStringLiteral("相干性有效样本掩膜与相位网格不一致"));
                    return;
                }
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
                        phaseValidMask, win_width, win_height, validSampleCount) ||
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
