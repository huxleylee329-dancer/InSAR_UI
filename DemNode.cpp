#include "DemNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include <Package.h>
#include "Utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QJsonDocument>
#include <QStandardItemModel>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include "QtNodes/internal/NodeDetailWindow.hpp"

namespace QtNodes {

namespace {

ProductDescriptor::Ptr descriptorWithDemGeometry(const ProductDescriptor::Ptr& descriptor,
                                                 const QStringList& h5Paths,
                                                 QString* errorMessage)
{
    if (!descriptor || h5Paths.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM product descriptor geometry cannot be built without H5 outputs.");
        return ProductDescriptor::Ptr();
    }
    double minLon = std::numeric_limits<double>::infinity();
    double maxLon = -std::numeric_limits<double>::infinity();
    double minLat = std::numeric_limits<double>::infinity();
    double maxLat = -std::numeric_limits<double>::infinity();
    for (const QString& h5Path : h5Paths) {
        double fileMinLon = 0.0, fileMaxLon = 0.0, fileMinLat = 0.0, fileMaxLat = 0.0;
        QString readError;
        if (!NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_min_lon"), fileMinLon, &readError) ||
            !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_max_lon"), fileMaxLon, &readError) ||
            !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_min_lat"), fileMinLat, &readError) ||
            !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_max_lat"), fileMaxLat, &readError) ||
            !std::isfinite(fileMinLon) || !std::isfinite(fileMaxLon) ||
            !std::isfinite(fileMinLat) || !std::isfinite(fileMaxLat) ||
            fileMaxLon <= fileMinLon || fileMaxLat <= fileMinLat) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output geometry metadata is missing or invalid: %1").arg(h5Path);
            return ProductDescriptor::Ptr();
        }
        minLon = std::min(minLon, fileMinLon);
        maxLon = std::max(maxLon, fileMaxLon);
        minLat = std::min(minLat, fileMinLat);
        maxLat = std::max(maxLat, fileMaxLat);
    }
    QMap<QString, QString> provenance = descriptor->provenance();
    provenance.insert(QStringLiteral("geometrySource"), QStringLiteral("dem_h5_bounds"));
    provenance.insert(QStringLiteral("crsWkt"), QStringLiteral("EPSG:4326"));
    provenance.insert(QStringLiteral("minLon"), QString::number(minLon, 'g', 17));
    provenance.insert(QStringLiteral("maxLon"), QString::number(maxLon, 'g', 17));
    provenance.insert(QStringLiteral("minLat"), QString::number(minLat, 'g', 17));
    provenance.insert(QStringLiteral("maxLat"), QString::number(maxLat, 'g', 17));
    return ProductDescriptor::create(descriptor->productType(), descriptor->schemaId(),
                                     descriptor->schemaVersion(), descriptor->state(),
                                     descriptor->source(), provenance);
}

bool captureH5ArtifactSnapshot(const QString& path,
                               DemH5ArtifactSnapshot& snapshot,
                               QString* errorMessage)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        if (errorMessage) *errorMessage = QStringLiteral("Required DEM v2 H5 input is missing or unreadable: %1").arg(path);
        return false;
    }
    std::string identity;
    if (!NodeUtils::readStringFromH5(info.absoluteFilePath(), QStringLiteral("semantic_product_descriptor"), identity) ||
        identity.empty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Required DEM v2 H5 input lacks a readable identity: %1").arg(path);
        return false;
    }
    if (!ProductDescriptor::fromJson(QJsonDocument::fromJson(QByteArray::fromStdString(identity)).object())) {
        if (errorMessage) *errorMessage = QStringLiteral("Required DEM v2 H5 input has an invalid identity: %1").arg(path);
        return false;
    }
    snapshot.absolutePath = info.absoluteFilePath();
    snapshot.sha256.clear();
    snapshot.semanticIdentityJson = QString::fromUtf8(QByteArray::fromStdString(identity));
    return true;
}

bool resolveAndCapturePhaseInputSnapshot(const QString& phasePath,
                                         const QString& projectRoot,
                                         DemPhaseAnchorInputSnapshot& snapshot,
                                         QString* errorMessage)
{
    QString resolvedPhasePath = phasePath;
    if (QDir::isRelativePath(resolvedPhasePath)) {
        resolvedPhasePath = QDir(projectRoot).absoluteFilePath(resolvedPhasePath);
    }
    if (!captureH5ArtifactSnapshot(resolvedPhasePath, snapshot.phase, errorMessage)) return false;

    cv::Mat retainedBurstIndices;
    if (!NodeUtils::readMatFromH5(snapshot.phase.absolutePath,
                                  QStringLiteral("s1_tops_retained_master_burst_indices"),
                                  retainedBurstIndices) ||
        retainedBurstIndices.type() != CV_32S || retainedBurstIndices.rows != 1 ||
        retainedBurstIndices.cols < 1) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM v2 phase input lacks retained master burst indices: %1")
            .arg(snapshot.phase.absolutePath);
        return false;
    }
    snapshot.retainedMasterBurstIndices.clear();
    for (int column = 0; column < retainedBurstIndices.cols; ++column) {
        const int burst = retainedBurstIndices.at<int>(0, column);
        if (burst < 1 || (!snapshot.retainedMasterBurstIndices.isEmpty() &&
                          burst != snapshot.retainedMasterBurstIndices.last() + 1)) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM v2 phase input has an invalid retained master burst sequence: %1")
                .arg(snapshot.phase.absolutePath);
            return false;
        }
        snapshot.retainedMasterBurstIndices.append(burst);
    }

    const auto resolveSource = [&](const QString& dataset, DemH5ArtifactSnapshot& destination) {
        std::string source;
        if (!NodeUtils::readStringFromH5(snapshot.phase.absolutePath, dataset, source) || source.empty()) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM v2 phase input lacks %1: %2")
                .arg(dataset, snapshot.phase.absolutePath);
            return false;
        }
        PathResolver::Resolution resolution;
        PathResolver::Error pathError = PathResolver::Error::None;
        if (!PathResolver::resolve(source, projectRoot.toUtf8().toStdString(), resolution, &pathError)) {
            if (errorMessage) *errorMessage = QStringLiteral("Unable to resolve %1 for DEM v2 snapshot: %2")
                .arg(dataset, QString::fromLatin1(PathResolver::errorMessage(pathError)));
            return false;
        }
        const QString resolved = QString::fromUtf8(resolution.utf8.data(), static_cast<int>(resolution.utf8.size()));
        return captureH5ArtifactSnapshot(resolved, destination, errorMessage);
    };
    if (!resolveSource(QStringLiteral("source_1"), snapshot.master) ||
        !resolveSource(QStringLiteral("source_2"), snapshot.slave)) {
        return false;
    }
    std::string geometryReferencePath;
    if (!NodeUtils::readStringFromH5(snapshot.phase.absolutePath,
                                     QStringLiteral("s1_tops_geometry_reference_path"),
                                     geometryReferencePath) || geometryReferencePath.empty()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM v2 phase input lacks a canonical geometry reference path: %1")
            .arg(snapshot.phase.absolutePath);
        return false;
    }
    PathResolver::Resolution geometryResolution;
    PathResolver::Error pathError = PathResolver::Error::None;
    if (!PathResolver::resolve(geometryReferencePath, projectRoot.toUtf8().toStdString(),
                               geometryResolution, &pathError) ||
        !captureH5ArtifactSnapshot(QString::fromUtf8(geometryResolution.utf8.data(),
                                                       static_cast<int>(geometryResolution.utf8.size())),
                                   snapshot.geometryReference, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("DEM v2 geometry reference cannot be resolved and frozen.");
        return false;
    }
    const QString canonicalGeometry = QFileInfo(snapshot.geometryReference.absolutePath).absoluteFilePath();
    if (canonicalGeometry.compare(snapshot.master.absolutePath, Qt::CaseInsensitive) != 0 ||
        canonicalGeometry.compare(snapshot.slave.absolutePath, Qt::CaseInsensitive) == 0) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM v2 geometry reference does not resolve to frozen source_1/master.");
        return false;
    }
    const struct PhaseProvenanceField { const char* dataset; QString* destination; } fields[] = {
        { "flat_earth_orbit_interpolation_strategy", &snapshot.orbitInterpolationStrategy },
        { "flat_earth_master_orbit_source", &snapshot.masterOrbitSource },
        { "flat_earth_master_orbit_selection_reason", &snapshot.masterOrbitSelectionReason },
        { "flat_earth_slave_orbit_source", &snapshot.slaveOrbitSource },
        { "flat_earth_slave_orbit_selection_reason", &snapshot.slaveOrbitSelectionReason } };
    for (const PhaseProvenanceField& field : fields) {
        std::string value;
        if (!NodeUtils::readStringFromH5(snapshot.phase.absolutePath, field.dataset, value) || value.empty()) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM v2 phase input lacks FEP provenance '%1'.")
                .arg(QString::fromLatin1(field.dataset));
            return false;
        }
        *field.destination = QString::fromStdString(value);
    }
    return true;
}

bool validateAcceptedDemAnchorProvenance(const QString& h5Path,
                                         const DemAbsolutePhaseAnchorV2Policy& policy,
                                         const QString& expectedReferenceResourceId,
                                         const QString& expectedReferenceCanonicalHash,
                                         const QString& expectedGeoidModelId,
                                         const QString& expectedGeoidModelHash,
                                         const QList<int>& expectedRetainedMasterBursts,
                                         QString* errorMessage,
                                         QString* qualityWarning = nullptr)
{
    int version = 0;
    int selectedK = 0;
    int candidateCount = 0;
    int componentCount = 0;
    int componentBurstEvidenceCount = 0;
    double consensus = 0.0;
    std::string status;
    cv::Mat countByBurst, validationCountByBurst, histogram, residualStats, candidateBurstIndices,
        selectionRangeCoverage, validationRangeCoverage, componentEvidence, storedPolicy;
    const QStringList requiredStrings = {
        QStringLiteral("dem_anchor_strategy"),
        QStringLiteral("dem_anchor_orbit_strategy"),
        QStringLiteral("dem_anchor_reference_resource_id"),
        QStringLiteral("dem_anchor_reference_hash"),
        QStringLiteral("dem_anchor_vertical_datum_source"),
        QStringLiteral("dem_anchor_geoid_model_id"),
        QStringLiteral("dem_anchor_geoid_model_hash") };
    if (!policy.isValid() || expectedReferenceResourceId.isEmpty() ||
        expectedReferenceCanonicalHash.isEmpty() || expectedGeoidModelHash.isEmpty() ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_absolute_phase_anchor_version"), version) ||
        version != 2 ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("dem_anchor_status"), status) ||
        QString::fromStdString(status) != QStringLiteral("accepted") ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_anchor_selected_k"), selectedK) ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_anchor_candidate_count"), candidateCount) ||
        candidateCount < 1 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_anchor_component_count"), componentCount) ||
        componentCount < 1 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_anchor_component_burst_evidence_count"),
                                      componentBurstEvidenceCount) ||
        componentBurstEvidenceCount < 1 ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_anchor_consensus_fraction"), consensus) ||
        !std::isfinite(consensus) || consensus < policy.minimumConsensusFraction || consensus > 1.0 ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_candidate_count_by_burst"), countByBurst) ||
        countByBurst.empty() || countByBurst.type() != CV_32S || countByBurst.cols != 1 ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_validation_count_by_burst"), validationCountByBurst) ||
        validationCountByBurst.empty() || validationCountByBurst.type() != CV_32S || validationCountByBurst.cols != 1 ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_selection_range_coverage"), selectionRangeCoverage) ||
        selectionRangeCoverage.empty() || selectionRangeCoverage.type() != CV_32S ||
        selectionRangeCoverage.cols != policy.rangeCellsPerBurst ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_validation_range_coverage"), validationRangeCoverage) ||
        validationRangeCoverage.empty() || validationRangeCoverage.type() != CV_32S ||
        validationRangeCoverage.cols != policy.rangeCellsPerBurst ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_component_evidence"), componentEvidence) ||
        componentEvidence.empty() || componentEvidence.type() != CV_32S || componentEvidence.cols != 4 ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_policy"), storedPolicy) ||
        storedPolicy.type() != CV_64F || storedPolicy.rows != 1 || storedPolicy.cols != 8 ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_k_histogram"), histogram) ||
        // Histogram v2 is CV_32S Nx2, one unique [K, candidate_count] pair
        // per row. This makes the stored consensus independently checkable.
        histogram.empty() || histogram.type() != CV_32S || histogram.cols != 2 ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_sparse_height_residual_stats"), residualStats) ||
        // Residual v2 is CV_64F 1x4: [validation_count, mean_m, rms_m, max_abs_m].
        residualStats.type() != CV_64F || residualStats.rows != 1 || residualStats.cols != 4 ||
        !cv::checkRange(residualStats, true, nullptr) ||
        !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem_anchor_candidate_burst_indices"), candidateBurstIndices) ||
        candidateBurstIndices.type() != CV_32S || candidateBurstIndices.cols != 1) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM output has no accepted, complete absolute-phase anchoring v2 provenance: %1").arg(h5Path);
        return false;
    }
    QList<int> expectedBursts = expectedRetainedMasterBursts;
    if (expectedBursts.isEmpty()) {
        cv::Mat retainedBurstIndices, outputRowMap, retainedRanges, dem;
        int sourceRowCount = 0;
        std::string productContract;
        if (!NodeUtils::readMatFromH5(h5Path, QStringLiteral("s1_tops_retained_master_burst_indices"), retainedBurstIndices) ||
            !NodeUtils::readMatFromH5(h5Path, QStringLiteral("s1_tops_output_source_row_map"), outputRowMap) ||
            !NodeUtils::readMatFromH5(h5Path, QStringLiteral("s1_tops_retained_source_row_ranges"), retainedRanges) ||
            !NodeUtils::readMatFromH5(h5Path, QStringLiteral("dem"), dem) ||
            !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("s1_tops_source_full_burst_row_count"), sourceRowCount) ||
            !NodeUtils::readStringFromH5(h5Path, QStringLiteral("s1_tops_product_contract"), productContract) ||
            productContract != "continuous_deburst_common_coverage_v1" ||
            retainedBurstIndices.type() != CV_32S || retainedBurstIndices.rows != 1 || retainedBurstIndices.cols < 1 ||
            outputRowMap.type() != CV_32S || outputRowMap.cols != 1 || outputRowMap.rows != dem.rows ||
            retainedRanges.type() != CV_32S || retainedRanges.rows != retainedBurstIndices.cols || retainedRanges.cols != 2 ||
            sourceRowCount < 1) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output cannot prove its retained master burst sequence: %1").arg(h5Path);
            return false;
        }
        for (int column = 0; column < retainedBurstIndices.cols; ++column) {
            const int burst = retainedBurstIndices.at<int>(0, column);
            const int rangeStart = retainedRanges.at<int>(column, 0);
            const int rangeEnd = retainedRanges.at<int>(column, 1);
            if (burst < 1 || (!expectedBursts.isEmpty() && burst != expectedBursts.last() + 1) ||
                rangeStart < 0 || rangeEnd <= rangeStart || rangeEnd > sourceRowCount) {
                if (errorMessage) *errorMessage = QStringLiteral("DEM output has an invalid retained master burst order or range: %1").arg(h5Path);
                return false;
            }
            expectedBursts.append(burst);
        }
        for (int row = 0; row < outputRowMap.rows; ++row) {
            const int sourceRow = outputRowMap.at<int>(row, 0);
            bool inRetainedRange = false;
            for (int burst = 0; burst < retainedRanges.rows; ++burst) {
                inRetainedRange = inRetainedRange ||
                    (sourceRow >= retainedRanges.at<int>(burst, 0) && sourceRow < retainedRanges.at<int>(burst, 1));
            }
            if (sourceRow < 0 || sourceRow >= sourceRowCount || !inRetainedRange ||
                (row > 0 && sourceRow <= outputRowMap.at<int>(row - 1, 0))) {
                if (errorMessage) *errorMessage = QStringLiteral("DEM output source-row map does not match retained master bursts: %1").arg(h5Path);
                return false;
            }
        }
    }
    if (expectedBursts.isEmpty() || countByBurst.rows != expectedBursts.size() ||
        validationCountByBurst.rows != expectedBursts.size() ||
        selectionRangeCoverage.rows != expectedBursts.size() || validationRangeCoverage.rows != expectedBursts.size() ||
        candidateBurstIndices.rows != expectedBursts.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM output burst candidate statistics do not cover every retained master burst: %1").arg(h5Path);
        return false;
    }
    long long countedCandidates = 0;
    long long countedValidationCandidates = 0;
    for (int row = 0; row < countByBurst.rows; ++row) {
        const int count = countByBurst.at<int>(row, 0);
        const int validationCount = validationCountByBurst.at<int>(row, 0);
        if (count < policy.minimumSelectionCandidatesPerBurst ||
            validationCount < policy.minimumValidationCandidatesPerBurst ||
            candidateBurstIndices.at<int>(row, 0) != expectedBursts.at(row)) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output has an uncovered burst in its absolute-phase anchor provenance: %1").arg(h5Path);
            return false;
        }
        bool hasSelectionRange = false;
        bool hasValidationRange = false;
        for (int range = 0; range < policy.rangeCellsPerBurst; ++range) {
            const int sel = selectionRangeCoverage.at<int>(row, range);
            const int val = validationRangeCoverage.at<int>(row, range);
            if ((sel != 0 && sel != 1) || (val != 0 && val != 1)) {
                if (errorMessage) *errorMessage = QStringLiteral("DEM output has invalid burst/range stratum coverage values: %1").arg(h5Path);
                return false;
            }
            if (sel == 1) hasSelectionRange = true;
            if (val == 1) hasValidationRange = true;
        }
        // 保留每 Burst 双集合非空与陆地覆盖约束，海域等无陆地像元分层允许为 0，不再强制所有距离分层必须有候选
        if (!hasSelectionRange || !hasValidationRange) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output lacks independent terrain-supported selection or validation range coverage for an active burst: %1").arg(h5Path);
            return false;
        }
        countedCandidates += count;
        countedValidationCandidates += validationCount;
    }
    const double expectedPolicy[] = {
        static_cast<double>(policy.version), static_cast<double>(policy.azimuthCellsPerBurst),
        static_cast<double>(policy.rangeCellsPerBurst), policy.minimumConsensusFraction,
        policy.maximumSparseHeightResidualMeters, policy.minimumComplexGamma,
        static_cast<double>(policy.minimumSelectionCandidatesPerBurst),
        static_cast<double>(policy.minimumValidationCandidatesPerBurst) };
    for (int index = 0; index < 8; ++index) {
        if (!std::isfinite(storedPolicy.at<double>(0, index)) ||
            storedPolicy.at<double>(0, index) != expectedPolicy[index]) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output absolute-phase anchor policy differs from the frozen transaction policy: %1").arg(h5Path);
            return false;
        }
    }
    cv::Mat outputPhaseValidMask, outputComponents;
    const int outputComponentCount =
        NodeUtils::readMatFromH5(h5Path, QStringLiteral("phase_valid_mask"), outputPhaseValidMask) &&
        outputPhaseValidMask.type() == CV_8U && !outputPhaseValidMask.empty()
        ? cv::connectedComponents(outputPhaseValidMask, outputComponents, 8, CV_32S) - 1 : -1;
    const int expectedComponentBurstEvidenceCount = outputComponentCount * expectedBursts.size();
    if (outputComponentCount < 1 || componentCount != outputComponentCount ||
        componentEvidence.rows != componentBurstEvidenceCount ||
        componentBurstEvidenceCount != expectedComponentBurstEvidenceCount) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM output cannot prove complete component x burst anchor evidence: %1").arg(h5Path);
        return false;
    }
    QSet<QPair<int, int>> componentBurstEvidenceSeen;
    for (int row = 0; row < componentEvidence.rows; ++row) {
        const int component = componentEvidence.at<int>(row, 0);
        const int burst = componentEvidence.at<int>(row, 1);
        if (component < 1 || component > outputComponentCount || !expectedBursts.contains(burst) ||
            componentEvidence.at<int>(row, 2) <= 0 || componentEvidence.at<int>(row, 3) <= 0) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output has incomplete component x burst selection/validation evidence: %1").arg(h5Path);
            return false;
        }
        const QPair<int, int> key(component, burst);
        if (componentBurstEvidenceSeen.contains(key)) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output has duplicate component x burst anchor evidence: %1").arg(h5Path);
            return false;
        }
        componentBurstEvidenceSeen.insert(key);
    }
    for (int component = 1; component <= outputComponentCount; ++component) {
        for (const int burst : expectedBursts) {
            if (!componentBurstEvidenceSeen.contains(qMakePair(component, burst))) {
                if (errorMessage) *errorMessage = QStringLiteral("DEM output omitted a phase-component/FEP-burst anchor evidence pair: %1").arg(h5Path);
                return false;
            }
        }
    }
    long long histogramCandidates = 0;
    int selectedKCount = 0;
    for (int row = 0; row < histogram.rows; ++row) {
        const int k = histogram.at<int>(row, 0);
        const int count = histogram.at<int>(row, 1);
        if (count <= 0) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output has an invalid absolute-phase K histogram count: %1").arg(h5Path);
            return false;
        }
        for (int previous = 0; previous < row; ++previous) {
            if (histogram.at<int>(previous, 0) == k) {
                if (errorMessage) *errorMessage = QStringLiteral("DEM output has duplicate K entries in absolute-phase anchor provenance: %1").arg(h5Path);
                return false;
            }
        }
        histogramCandidates += count;
        if (k == selectedK) selectedKCount = count;
    }
    const double residualValidationCount = residualStats.at<double>(0, 0);
    const double residualMean = residualStats.at<double>(0, 1);
    const double residualRms = residualStats.at<double>(0, 2);
    const double residualMaxAbs = residualStats.at<double>(0, 3);
    const double recomputedConsensus = static_cast<double>(selectedKCount) / candidateCount;
    if (countedCandidates != candidateCount || histogramCandidates != candidateCount || selectedKCount < 1 ||
        !std::isfinite(residualValidationCount) || residualValidationCount != countedValidationCandidates ||
        std::floor(residualValidationCount) != residualValidationCount ||
        !std::isfinite(residualMean) || !std::isfinite(residualRms) || residualRms < 0.0 ||
        !std::isfinite(residualMaxAbs) || residualMaxAbs < 0.0 ||
        std::fabs(residualMean) > residualMaxAbs || residualRms > residualMaxAbs ||
        recomputedConsensus < policy.minimumConsensusFraction ||
        std::fabs(consensus - recomputedConsensus) > 1e-12) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM output has invalid absolute-phase anchoring v2 burst coverage or K histogram: %1").arg(h5Path);
        return false;
    }
    // 残差超限不再拒绝出图，仅作为质量告警浮出到节点状态；K 共识等其余契约仍严格校验
    if (qualityWarning) {
        qualityWarning->clear();
        if (residualMaxAbs > policy.maximumSparseHeightResidualMeters) {
            *qualityWarning = QStringLiteral(
                "%1：绝对相位锚定残差超出策略——maxAbs=%2 m > 上限 %3 m（count=%4, mean=%5 m, rms=%6 m）。"
                "已按质量告警继续出图，结果精度可能不足。")
                .arg(h5Path)
                .arg(residualMaxAbs, 0, 'f', 3).arg(policy.maximumSparseHeightResidualMeters, 0, 'f', 1)
                .arg(residualValidationCount, 0, 'f', 0)
                .arg(residualMean, 0, 'f', 3).arg(residualRms, 0, 'f', 3);
        }
    }
    for (const QString& dataset : requiredStrings) {
        std::string value;
        if (!NodeUtils::readStringFromH5(h5Path, dataset, value) || value.empty()) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM output has an incomplete absolute-phase anchoring v2 field '%1': %2")
                .arg(dataset, h5Path);
            return false;
        }
    }
    std::string resourceId;
    std::string referenceHash;
    std::string geoidModelId;
    std::string geoidModelHash;
    if (!NodeUtils::readStringFromH5(h5Path, QStringLiteral("dem_anchor_reference_resource_id"), resourceId) ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("dem_anchor_reference_hash"), referenceHash) ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("dem_anchor_geoid_model_id"), geoidModelId) ||
        !NodeUtils::readStringFromH5(h5Path, QStringLiteral("dem_anchor_geoid_model_hash"), geoidModelHash) ||
        QString::fromStdString(resourceId) != expectedReferenceResourceId ||
        QString::fromStdString(referenceHash) != expectedReferenceCanonicalHash ||
        (!expectedGeoidModelId.isEmpty() && QString::fromStdString(geoidModelId) != expectedGeoidModelId) ||
        QString::fromStdString(geoidModelHash) != expectedGeoidModelHash) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM output absolute-phase anchor references a different auxiliary DEM resource or canonical metadata hash: %1").arg(h5Path);
        return false;
    }
    return true;
}

} // namespace

DemNode::DemNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_methodCombo(nullptr)
    , m_timesLabel(nullptr)
    , m_timesEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_method(1) // default: Newton
    , m_times(24) // v2 bisection needs at least 22 iterations for its height envelope
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    qRegisterMetaType<DemFileResult>("DemFileResult");
    qRegisterMetaType<DemAbsolutePhaseAnchorV2Request>("DemAbsolutePhaseAnchorV2Request");
    setExecutionMode(ExecutionMode::Automatic);
}

DemNode::~DemNode()
{
    stopExecution();
    cleanupThreadResources();
}

unsigned int DemNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType DemNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Unwrapped Phase"};
        if (portIndex == 1)
            return NodeDataType{"auxiliary_dem", "Auxiliary Terrain DEM"};
        return NodeDataType();
    }
    else
    {
        if (portIndex == 0)
            return NodeDataType{"insar_dem", "InSAR DEM"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool DemNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString DemNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0)
            return QStringLiteral("解缠相位 *");
        if (portIndex == 1)
            return QStringLiteral("外部 DEM *");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool DemNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void DemNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    } else if (port == 1) {
        m_auxiliaryDemData = std::dynamic_pointer_cast<AuxiliaryDemData>(data);
    }

    if (port == 0 && m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty() || !m_auxiliaryDemData) {
        m_outputData.reset();
        m_imageInfoData.reset();
    }
}

std::shared_ptr<NodeData> DemNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* DemNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject DemNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["method"] = m_method;
    modelJson["times"] = m_timesEdit ? m_timesEdit->text().toInt() : m_times;

    return modelJson;
}

void DemNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vMethod = json["method"];
    if (!vMethod.isUndefined()) m_method = vMethod.toInt();

    QJsonValue vTimes = json["times"];
    if (!vTimes.isUndefined()) m_times = vTimes.toInt();

    // SOP Rule 15: load parameters BEFORE triggering validateAndRestoreOutput in base load
    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_methodCombo) {
        m_methodCombo->setCurrentIndex(m_method - 1);
    }
    if (m_timesEdit) m_timesEdit->setText(QString::number(m_times));

    onMethodChanged(m_method - 1);
}

void DemNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void DemNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene);
        }
    };

    const int labelWidth = 100;



    // 3. DEM 方法
    auto* methodLayout = new QHBoxLayout();
    QLabel* methodLabel = new QLabel("DEM方法");
    methodLabel->setFixedWidth(labelWidth);
    methodLayout->addWidget(methodLabel);
    m_methodCombo = new QComboBox();
    m_methodCombo->setEditable(false);
    m_methodCombo->addItem("牛顿法");
    m_methodCombo->setCurrentIndex(m_method - 1);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        int val = index + 1;
        if (m_method != val) {
            if (!confirmParameterChange()) {
                m_methodCombo->blockSignals(true);
                m_methodCombo->setCurrentIndex(m_method - 1);
                m_methodCombo->blockSignals(false);
                return;
            }
            m_method = val;
            onMethodChanged(index);
            invalidateNodeData();
        }
    });
    methodLayout->addWidget(m_methodCombo);
    layout->addLayout(methodLayout);

    // 4. 迭代次数
    auto* timesLayout = new QHBoxLayout();
    m_timesLabel = new QLabel("迭代次数");
    m_timesLabel->setFixedWidth(labelWidth);
    timesLayout->addWidget(m_timesLabel);
    m_timesEdit = new QLineEdit();
    m_timesEdit->setText(QString::number(m_times));
    connect(m_timesEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        bool ok = false;
        int val = m_timesEdit->text().toInt(&ok);
        if (ok && val >= 22 && val <= 256 && m_times != val) {
            if (!confirmParameterChange()) {
                m_timesEdit->setText(QString::number(m_times));
                return;
            }
            m_times = val;
            invalidateNodeData();
        } else if (!ok || val < 22 || val > 256) {
            QMessageBox::warning(nullptr, "Warning", QStringLiteral("absolute-phase anchoring v2 的迭代次数必须在 22 到 256 之间。"));
            m_timesEdit->setText(QString::number(m_times));
        }
    });
    timesLayout->addWidget(m_timesEdit);
    layout->addLayout(timesLayout);

    // 5. 目标节点
    auto* outputLayout = new QHBoxLayout();
    QLabel* outputLabel = new QLabel("目标节点");
    outputLabel->setFixedWidth(labelWidth);
    outputLayout->addWidget(outputLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    outputLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(outputLayout);

    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    onMethodChanged(m_method - 1);
}

void DemNode::onMethodChanged(int index)
{
    bool isNewton = (index == 0);

    if (m_timesLabel) m_timesLabel->setVisible(isNewton);
    if (m_timesEdit) m_timesEdit->setVisible(isNewton);

    updateWidgetSize();
}

void DemNode::updateWidgetSize()
{
    if (_widget)
    {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString DemNode::generateDefaultOutputName() const
{
    if (m_inputData) {
        return m_inputData->nodeName() + "_Dem";
    }
    return "Dem_Data";
}

bool DemNode::validateInputs() const
{
    if (projectName().isEmpty()) {
        return false;
    }
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        return false;
    }
    if (!m_auxiliaryDemData || !m_auxiliaryDemData->isValid()) {
        return false;
    }

    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) {
        return false;
    }

    // Alphanumeric and underscore validation
    bool bFlag = dstNode.contains(QRegularExpression("^\\w+$"));
    if (!bFlag) {
        return false;
    }

    // Check parameters
    if (m_method == 1 && (m_times < 22 || m_times > 256)) {
        return false;
    }

    return true;
}

bool DemNode::commitWidgetParametersForExecution()
{
    if (m_outputNodeNameEdit) {
        m_outputNodeName = m_outputNodeNameEdit->text().trimmed();
    }

    if (!m_timesEdit) {
        return true;
    }

    bool ok = false;
    const int times = m_timesEdit->text().toInt(&ok);
    if (!ok || times < 22 || times > 256) {
        setStartFailureMessage(QStringLiteral("absolute-phase anchoring v2 的迭代次数必须在 22 到 256 之间。"));
        return false;
    }

    m_times = times;
    return true;
}

bool DemNode::prepareToStart()
{
    const auto logV2Preflight = [](const QString& message, const QString& category) {
        InSARLogManager::LogDiagnostic(InSARLogManager::LevelDebug, QStringLiteral("DemNode"),
            message, LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile, category);
    };
    const auto describeDescriptor = [](const QString& label, const ProductDescriptor::Ptr& descriptor) {
        if (!descriptor) {
            return QStringLiteral("%1=<null>").arg(label);
        }
        QStringList provenance;
        const QMap<QString, QString> values = descriptor->provenance();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            provenance.append(QStringLiteral("%1=%2").arg(it.key(), it.value()));
        }
        return QStringLiteral("%1={type=%2, schema=%3/%4, state=%5, source=%6, provenance=[%7]}")
            .arg(label, descriptor->productType(), descriptor->schemaId())
            .arg(descriptor->schemaVersion())
            .arg(static_cast<int>(descriptor->state()))
            .arg(descriptor->source(), provenance.join(QStringLiteral(", ")));
    };

    logV2Preflight(QStringLiteral("DEM v2 preflight entered: project=%1, output=%2, method=%3, iterations=%4, phaseInputPresent=%5, auxiliaryDemPresent=%6")
                      .arg(projectPath(), m_outputNodeName)
                      .arg(m_method)
                      .arg(m_times)
                      .arg(m_inputData ? QStringLiteral("true") : QStringLiteral("false"))
                      .arg(m_auxiliaryDemData ? QStringLiteral("true") : QStringLiteral("false")),
                  QStringLiteral("dem.v2.preflight"));
    if (!commitWidgetParametersForExecution()) {
        logV2Preflight(QStringLiteral("DEM v2 preflight rejected the iteration parameter."),
                      QStringLiteral("dem.v2.preflight.reject"));
        return false;
    }

    if (m_outputNodeName.trimmed().isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    if (!validateInputs()) {
        logV2Preflight(QStringLiteral("DEM v2 preflight rejected basic inputs: phasePaths=%1, auxiliaryDemValid=%2, output=%3")
                          .arg(m_inputData ? m_inputData->filePaths().join(QStringLiteral(";")) : QStringLiteral("<none>"))
                          .arg(m_auxiliaryDemData && m_auxiliaryDemData->isValid()
                              ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(m_outputNodeName),
                      QStringLiteral("dem.v2.preflight.reject"));
        setStartFailureMessage(QStringLiteral("请检查输入数据和输出配置是否完整。"));
        return false;
    }

    m_preparedDstNode = m_outputNodeName.trimmed();
    m_outputNodeName = m_preparedDstNode;
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }

    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();

    m_preparedMethod = m_method;
    m_preparedTimes = m_times;
    m_preparedAnchorPolicy = DemAbsolutePhaseAnchorV2Policy();
    if (!m_preparedAnchorPolicy.isValid()) {
        setStartFailureMessage(QStringLiteral("DEM absolute-phase anchoring v2 policy is invalid."));
        return false;
    }

    QStringList srcPaths = m_inputData->filePaths();
    m_preparedPhasePaths = srcPaths;
    m_preparedPhaseInputSnapshots.clear();
    const ProductDescriptor::Ptr logicalDescriptor = m_inputData->productDescriptor();
    const ProductDescriptor::Ptr physicalDescriptor = m_inputData->physicalProductDescriptor();
    const bool sameDescriptor = (logicalDescriptor == physicalDescriptor);
    logV2Preflight(QStringLiteral("DEM v2 phase input: node=%1, paths=[%2], logical=%3, physical=%4, sameDescriptor=%5")
                      .arg(m_inputData->nodeName(), srcPaths.join(QStringLiteral(";")))
                      .arg(describeDescriptor(QStringLiteral("logical"), logicalDescriptor))
                      .arg(sameDescriptor ? QStringLiteral("<identical_to_logical>")
                                          : describeDescriptor(QStringLiteral("physical"), physicalDescriptor))
                      .arg(sameDescriptor ? QStringLiteral("true") : QStringLiteral("false")),
                  QStringLiteral("dem.v2.input_descriptor"));
    logV2Preflight(QStringLiteral("DEM v2 auxiliary DEM input: resourceId=%1, provenanceId=%2, raster=%3, identityH5=%4, validMask=%5")
                      .arg(m_auxiliaryDemData->resourceId(), m_auxiliaryDemData->pinnedProvenanceId(),
                           m_auxiliaryDemData->rasterPath(), m_auxiliaryDemData->identityH5Path(),
                           m_auxiliaryDemData->validMaskPath()),
                  QStringLiteral("dem.v2.auxiliary_dem"));
    QString identityError;
    if (!NodeUtils::validateH5Identities(srcPaths, m_inputData->physicalProductDescriptor(), &identityError)) {
        logV2Preflight(QStringLiteral("DEM v2 phase identity validation failed: %1").arg(identityError),
                      QStringLiteral("dem.v2.input_identity.reject"));
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }

    // v2 accepts only the versioned full-grid flat-earth reference contract.
    // The former six-coefficient model cannot be reconciled with a sampled
    // external-DEM absolute-phase anchor and is intentionally not a fallback.
    for (const QString& srcPath : srcPaths) {
        int phaseSchemaVersion = 0;
        QString phaseContractError;
        if (!NodeUtils::readScalarFromH5(srcPath, QStringLiteral("phase_processing_schema_version"),
                                         phaseSchemaVersion, &phaseContractError) ||
            phaseSchemaVersion != 2) {
            if (phaseContractError.isEmpty()) {
                phaseContractError = QStringLiteral("DEM absolute-phase anchoring v2 requires phase_processing_schema_version=2: %1")
                    .arg(srcPath);
            }
            setStartFailureMessage(phaseContractError);
            setLastErrorMessage(phaseContractError);
            return false;
        }
        DemPhaseAnchorInputSnapshot inputSnapshot;
        if (!resolveAndCapturePhaseInputSnapshot(srcPath, m_preparedSavePath, inputSnapshot, &phaseContractError)) {
            setStartFailureMessage(phaseContractError);
            setLastErrorMessage(phaseContractError);
            return false;
        }
        m_preparedPhaseInputSnapshots.append(inputSnapshot);
    }

    const QJsonObject inputGeometry =
        NodeUtils::inputGeometryFromProductDescriptor(m_inputData->physicalProductDescriptor());
    QStringList missingGeometryFields;
    for (const QString& key : {QStringLiteral("minLon"), QStringLiteral("maxLon"),
                               QStringLiteral("minLat"), QStringLiteral("maxLat"),
                               QStringLiteral("crsWkt")}) {
        if (!inputGeometry.contains(key) || inputGeometry.value(key).isNull() ||
            (inputGeometry.value(key).isString() && inputGeometry.value(key).toString().trimmed().isEmpty())) {
            missingGeometryFields.append(key);
        }
    }
    logV2Preflight(QStringLiteral("DEM v2 parsed input geometry: geometry=%1, missing=[%2]")
                      .arg(QString::fromUtf8(QJsonDocument(inputGeometry).toJson(QJsonDocument::Compact)),
                           missingGeometryFields.join(QStringLiteral(","))),
                  QStringLiteral("dem.v2.input_geometry"));
    if (inputGeometry.isEmpty()) {
        const QString geometryError = QStringLiteral(
            "DEM absolute-phase anchoring v2 requires trusted minLon/maxLon/minLat/maxLat/crsWkt provenance on the unwrapped_phase input.");
        logV2Preflight(QStringLiteral("DEM v2 rejected phase input because trusted geometry provenance is absent: %1")
                          .arg(geometryError),
                      QStringLiteral("dem.v2.input_geometry.reject"));
        setStartFailureMessage(geometryError);
        setLastErrorMessage(geometryError);
        return false;
    }
    NodeUtils::AuxiliaryDemBinding resolvedBinding;
    QString bindingError;
    if (!NodeUtils::resolveAuxiliaryDemBinding(m_preparedSavePath, *m_auxiliaryDemData,
                                               resolvedBinding, &bindingError, inputGeometry, true)) {
        if (bindingError.isEmpty()) {
            bindingError = QStringLiteral("DEM absolute-phase anchoring v2 requires a managed auxiliary_terrain_dem binding with trusted geometry.");
        }
        logV2Preflight(QStringLiteral("DEM v2 auxiliary DEM binding rejected: %1").arg(bindingError),
                      QStringLiteral("dem.v2.auxiliary_dem.reject"));
        setStartFailureMessage(bindingError);
        setLastErrorMessage(bindingError);
        return false;
    }
    logV2Preflight(QStringLiteral("DEM v2 auxiliary DEM binding resolved: resourceId=%1, canonicalHash=%2, geoidId=%3, geoidPath=%4, geoidHash=%5")
                      .arg(resolvedBinding.resourceId, resolvedBinding.canonicalMetadataHash,
                           resolvedBinding.geoidModelId, resolvedBinding.geoidModelPath,
                           resolvedBinding.geoidModelHash),
                  QStringLiteral("dem.v2.auxiliary_dem.accept"));
    m_preparedAuxiliaryDemSnapshot.binding = resolvedBinding;
    m_preparedAuxiliaryDemSnapshot.inputGeometry = inputGeometry;

    // The full phase contract reads and scans several full-resolution rasters.
    // DemWorker validates it before invoking the DLL, off the UI thread.

    // Precalculate output file paths for overwrite check
    m_preparedOutputPaths.clear();
    for (const QString& srcPath : srcPaths) {
        QFileInfo fi(srcPath);
        QString changeName = fi.baseName() + "_dem";
        m_preparedOutputPaths.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + changeName + ".h5");
    }

    // 自动触发时（上游数据更新），强制覆盖，保证数据链路一致性
    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void DemNode::executeProcessing()
{
    InSARLogManager::LogInfo("DemNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        
        m_outputNodeNameEdit->setEnabled(true);
        m_methodCombo->setEnabled(true);
        onMethodChanged(m_method - 1);

        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) {
            if (!m_remedyWatcher.isRunning()) {
                finishDemExecution();
            }
        } else {
            setState(ExecutionState::Error);
        }
        return;
    }

    setProgress(0);
    setState(ExecutionState::Running);
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode,
                                           m_preparedOutputPaths, m_preparedPhasePaths,
                                           m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> descriptorProvenance;
    descriptorProvenance.insert(QStringLiteral("producer"), name());
    descriptorProvenance.insert(QStringLiteral("output_port"),
                                QStringLiteral("dem_generation.output.insar_dem"));
    descriptorProvenance.insert(QStringLiteral("runId"), m_outputTransaction.runId);
    descriptorProvenance.insert(QStringLiteral("demAbsolutePhaseAnchorVersion"), QStringLiteral("2"));
    descriptorProvenance.insert(QStringLiteral("demAnchorStrategy"),
                                QStringLiteral("external_dem_stratified_consensus_v1"));
    descriptorProvenance.insert(QStringLiteral("demAnchorReferenceResourceId"),
                                m_preparedAuxiliaryDemSnapshot.binding.resourceId);
    descriptorProvenance.insert(QStringLiteral("demAnchorReferenceProvenanceId"),
                                m_preparedAuxiliaryDemSnapshot.binding.pinnedProvenanceId);
    descriptorProvenance.insert(QStringLiteral("demAnchorReferenceHash"),
                                m_preparedAuxiliaryDemSnapshot.binding.canonicalMetadataHash);
    descriptorProvenance.insert(QStringLiteral("demAnchorRasterHash"),
                                m_preparedAuxiliaryDemSnapshot.binding.rasterHash);
    descriptorProvenance.insert(QStringLiteral("demAnchorIdentityH5Hash"),
                                m_preparedAuxiliaryDemSnapshot.binding.identityH5Hash);
    descriptorProvenance.insert(QStringLiteral("demAnchorValidityMaskHash"),
                                m_preparedAuxiliaryDemSnapshot.binding.validMaskHash);
    descriptorProvenance.insert(QStringLiteral("demAnchorGeoidModelId"),
                                m_preparedAuxiliaryDemSnapshot.binding.geoidModelId);
    descriptorProvenance.insert(QStringLiteral("demAnchorGeoidModelHash"),
                                m_preparedAuxiliaryDemSnapshot.binding.geoidModelHash);
    if (!NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction,
            ProductDescriptor::create(QStringLiteral("insar_dem"),
                                      QStringLiteral("sat-explorer-product"), 1,
                                      ProductState::Committed, name(), descriptorProvenance),
            &transactionError)) {
        onError(transactionError);
        return;
    }
    m_pendingDemResults.clear();
    m_xmlDirty = false;
    m_previewGenerationPending = false;
    ++m_previewGenerationId;

    m_thread = new QThread();
    m_workerThread = new DemWorker();
    m_workerThread->moveToThread(m_thread);

    DemAbsolutePhaseAnchorV2Request request;
    request.method = m_preparedMethod;
    request.iterations = m_preparedTimes;
    request.projectRoot = m_preparedSavePath;
    request.outputNode = m_outputTransaction.stagingName;
    request.phasePaths = m_preparedPhasePaths;
    request.phaseInputSnapshots = m_preparedPhaseInputSnapshots;
    request.policy = m_preparedAnchorPolicy;
    request.auxiliaryDemSnapshot = m_preparedAuxiliaryDemSnapshot;
    request.geoidModelPath = m_preparedAuxiliaryDemSnapshot.binding.geoidModelPath;
    request.geoidModelHash = m_preparedAuxiliaryDemSnapshot.binding.geoidModelHash;
    request.geoidModelId = m_preparedAuxiliaryDemSnapshot.binding.geoidModelId;
    for (const QString& phasePath : request.phasePaths) {
        request.phaseNames.append(QFileInfo(phasePath).baseName());
    }
    if (!request.isValid()) {
        onError(QStringLiteral("DEM absolute-phase anchoring v2 execution request is incomplete."));
        return;
    }

    connect(this, &DemNode::startDem, m_workerThread, &DemWorker::Dem);
    connect(m_workerThread, &DemWorker::demFileGenerated, this, &DemNode::handleDemFileGenerated);
    connect(m_thread, &QThread::started, [this, request]() {
        Q_EMIT startDem(request);
    });
    connect(m_workerThread, &DemWorker::updateProcess, this, &DemNode::onProgressUpdate);
    connect(m_workerThread, &DemWorker::endProcess, this, &DemNode::onProcessingFinished);
    connect(m_workerThread, &DemWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &DemWorker::cancelled, this, &DemNode::onCancelled);
    connect(m_workerThread, &DemWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &DemWorker::errorProcess, this, &DemNode::onError);
    connect(m_workerThread, &DemWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &DemWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Dynamic recovery of Running state for Automatic execution mode (SOP Rule 5)
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
            setState(ExecutionState::Running);
    });

    // Disable inputs during execution
    m_outputNodeNameEdit->setEnabled(false);
    m_methodCombo->setEnabled(false);
    if (m_timesEdit) m_timesEdit->setEnabled(false);

    deferAutomaticCompletion();
    m_thread->start();
}

void DemNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void DemNode::finishDemExecution()
{
    if (m_anchorQualityWarnings.isEmpty()) {
        finishExecution();
        return;
    }
    setLastWarningMessage(m_anchorQualityWarnings.join('\n'));
    finishExecutionWithWarning();
}

void DemNode::onProcessingFinished()
{
    m_anchorQualityWarnings.clear();
    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;
    QList<DemFileResult> committedResults;
    const QString dstNode = m_preparedDstNode;

    // Clean up worker thread
    releaseFinishedThreadResources();

    if (discardObsoleteAutomaticExecution()) {
        m_previewGenerationPending = false;
        ++m_previewGenerationId;
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
        return;
    }

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }

    QString transactionError;
    QStringList workerPaths;
    for (const DemFileResult& result : m_pendingDemResults) workerPaths.append(result.absoluteDemPath);
    ProductDescriptor::Ptr stagedDescriptor = ProductDescriptor::fromJson(
        m_outputTransaction.productDescriptor, &transactionError);
    stagedDescriptor = descriptorWithDemGeometry(stagedDescriptor, workerPaths, &transactionError);
    if (!stagedDescriptor || !NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction, stagedDescriptor, &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("DEM output geometry metadata could not be committed.") : transactionError);
        return;
    }
    const QMap<QString, QString> stagedAnchorProvenance = stagedDescriptor->provenance();
    if (stagedAnchorProvenance.value(QStringLiteral("demAnchorReferenceResourceId")) !=
            m_preparedAuxiliaryDemSnapshot.binding.resourceId ||
        stagedAnchorProvenance.value(QStringLiteral("demAnchorReferenceHash")) !=
            m_preparedAuxiliaryDemSnapshot.binding.canonicalMetadataHash ||
        stagedAnchorProvenance.value(QStringLiteral("demAnchorGeoidModelId")) !=
            m_preparedAuxiliaryDemSnapshot.binding.geoidModelId ||
        stagedAnchorProvenance.value(QStringLiteral("demAnchorGeoidModelHash")) !=
            m_preparedAuxiliaryDemSnapshot.binding.geoidModelHash) {
        onError(QStringLiteral("DEM output descriptor no longer matches the frozen auxiliary terrain DEM/geoid binding."));
        return;
    }
    for (int resultIndex = 0; resultIndex < workerPaths.size(); ++resultIndex) {
        QString anchorQualityWarning;
        if (resultIndex >= m_preparedPhaseInputSnapshots.size() ||
            !validateAcceptedDemAnchorProvenance(
                workerPaths.at(resultIndex), m_preparedAnchorPolicy,
                m_preparedAuxiliaryDemSnapshot.binding.resourceId,
                m_preparedAuxiliaryDemSnapshot.binding.canonicalMetadataHash,
                m_preparedAuxiliaryDemSnapshot.binding.geoidModelId,
                m_preparedAuxiliaryDemSnapshot.binding.geoidModelHash,
                m_preparedPhaseInputSnapshots.at(resultIndex).retainedMasterBurstIndices,
                &transactionError, &anchorQualityWarning)) {
            onError(transactionError);
            return;
        }
        if (!anchorQualityWarning.isEmpty()) m_anchorQualityWarnings.append(anchorQualityWarning);
    }
    if (!projectXml() || !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(
            m_outputTransaction,
            QStringList() << QStringLiteral("dem")
                          << QStringLiteral("dem_min_lon")
                          << QStringLiteral("dem_max_lon")
                          << QStringLiteral("dem_min_lat")
                          << QStringLiteral("dem_max_lat")
                          << QStringLiteral("dem_absolute_phase_anchor_version")
                          << QStringLiteral("dem_anchor_strategy")
                          << QStringLiteral("dem_anchor_orbit_strategy")
                          << QStringLiteral("dem_anchor_reference_resource_id")
                          << QStringLiteral("dem_anchor_reference_hash")
                          << QStringLiteral("dem_anchor_vertical_datum_source")
                          << QStringLiteral("dem_anchor_geoid_model_id")
                          << QStringLiteral("dem_anchor_geoid_model_hash")
                          << QStringLiteral("dem_anchor_policy")
                          << QStringLiteral("dem_anchor_candidate_count")
                          << QStringLiteral("dem_anchor_candidate_count_by_burst")
                          << QStringLiteral("dem_anchor_validation_count_by_burst")
                          << QStringLiteral("dem_anchor_candidate_burst_indices")
                          << QStringLiteral("dem_anchor_selection_range_coverage")
                          << QStringLiteral("dem_anchor_validation_range_coverage")
                          << QStringLiteral("dem_anchor_component_evidence")
                          << QStringLiteral("dem_anchor_component_count")
                          << QStringLiteral("dem_anchor_component_burst_evidence_count")
                          << QStringLiteral("dem_anchor_k_histogram")
                          << QStringLiteral("dem_anchor_selected_k")
                          << QStringLiteral("dem_anchor_consensus_fraction")
                          << QStringLiteral("dem_anchor_sparse_height_residual_stats")
                          << QStringLiteral("dem_anchor_status"),
            &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("Project XML context is unavailable for DEM output commit.") : transactionError);
        return;
    }
    if (!NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, workerPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    m_xmlDirty = false;
    for (DemFileResult result : m_pendingDemResults) {
        const QString fileName = QFileInfo(result.absoluteDemPath).fileName();
        result.absoluteDemPath = QDir(projectPath() + "/" + dstNode).absoluteFilePath(fileName);
        result.relativeDemPath = QStringLiteral("/%1/%2").arg(dstNode, fileName);
        commitDemResult(result); committedResults.append(result);
    }
    if (!m_xmlDirty || !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("DEM output metadata was not produced.") : transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    for (const DemFileResult& result : committedResults) publishDemResultToProjectTree(result);
    if (auto* iface = NodeUtils::getProjectContext(_widget)) iface->refreshProjectTree();
    for (const QString& h5Path : h5Paths) { jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg"); types.append(QStringLiteral("dem")); }

    m_outputData = std::make_shared<InsarDemData>(h5Paths, m_outputTransaction.runId);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(
        m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);

    if (!h5Paths.isEmpty())
    {
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
        }
        m_previewGenerationPending = true;
        const quint64 previewGenerationId = ++m_previewGenerationId;
        QStringList previewJpgPaths;
        for (const QString& jpgPath : jpgPaths) {
            const QFileInfo info(jpgPath);
            previewJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
                QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
            QFile::remove(jpgPath);
        }

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, jpgPaths, previewJpgPaths, previewGenerationId]() {
            if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
                for (const QString& previewJpgPath : previewJpgPaths) {
                    QFile::remove(previewJpgPath);
                }
                return;
            }
            m_previewGenerationPending = false;
            if (discardObsoleteAutomaticExecution()) {
                ++m_previewGenerationId;
                m_outputData.reset();
                m_imageInfoData.reset();
                setOutputData(0, nullptr);
                setOutputData(1, nullptr);
                return;
            }

            QStringList validJpgPaths;
            for (int i = 0; i < jpgPaths.size() && i < previewJpgPaths.size(); ++i) {
                if (QFile::exists(previewJpgPaths[i]) &&
                    QFile::rename(previewJpgPaths[i], jpgPaths[i])) {
                    validJpgPaths.append(jpgPaths[i]);
                }
            }
            if (!validJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(validJpgPaths);
                setOutputData(1, m_imageInfoData);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
            }
            m_outputNodeNameEdit->setEnabled(true);
            m_methodCombo->setEnabled(true);
            if (m_timesEdit) m_timesEdit->setEnabled(true);
            onMethodChanged(m_method - 1);

            setState(ExecutionState::Running);
            setProgress(100);
            InSARLogManager::LogInfo("DemNode", "executeProcessing completed.");
            finishDemExecution();
        });

        QFuture<void> future = QtConcurrent::run([h5Paths, previewJpgPaths, types]() {
            for (int i = 0; i < h5Paths.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(h5Paths[i], previewJpgPaths[i], types[i]);
            }
        });
        m_remedyWatcher.setFuture(future);
    }
    else
    {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);

        m_outputNodeNameEdit->setEnabled(true);
        m_methodCombo->setEnabled(true);
        if (m_timesEdit) m_timesEdit->setEnabled(true);
        onMethodChanged(m_method - 1);

        setState(ExecutionState::Running);
        setProgress(100);
        InSARLogManager::LogInfo("DemNode", "executeProcessing completed (empty output list).");
        finishDemExecution();
    }
}

void DemNode::handleDemFileGenerated(const DemFileResult& result)
{
    m_pendingDemResults.append(result);
}

ProductInputContract DemNode::productInputContract(PortIndex portIndex) const
{
    ProductInputContract contract;
    if (portIndex == 0) {
        contract.semanticId = QStringLiteral("dem_generation.input.unwrapped_phase.v2");
        contract.allowedProductTypes = QStringList() << QStringLiteral("unwrapped_phase");
        contract.requiredProvenanceFields = QStringList()
            << QStringLiteral("producer") << QStringLiteral("output_port");
    } else if (portIndex == 1) {
        contract.semanticId = QStringLiteral("dem_generation.input.auxiliary_terrain_dem.v2");
        contract.allowedProductTypes = QStringList() << QStringLiteral("auxiliary_terrain_dem");
        contract.requiredProvenanceFields = QStringList()
            << QStringLiteral("resourceId") << QStringLiteral("pinnedProvenanceId")
            << QStringLiteral("canonicalMetadataHash");
    }
    return contract;
}

ProductOutputContract DemNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0 ? QStringLiteral("dem_generation.output.insar_dem")
                                         : QStringLiteral("dem_generation.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList{QStringLiteral("insar_dem")} : QStringList{QStringLiteral("preview")};
    return contract;
}

void DemNode::commitDemResult(const DemFileResult& result)
{
    XMLFile* xml = projectXml();
    if (xml) {
        xml->XMLFile_add_dem(m_preparedDstNode.toStdString().c_str(), result.demName.toStdString().c_str(),
            result.relativeDemPath.toStdString().c_str(), result.offsetRow, result.offsetCol, "Iteration", m_preparedTimes);
        m_xmlDirty = true;
    }
}

void DemNode::publishDemResultToProjectTree(const DemFileResult& result)
{
    QStandardItemModel* model = projectModel();
    if (!model) return;
    const QList<QStandardItem*> projects = model->findItems(m_preparedProjectName);
    if (projects.isEmpty()) return;
    QStandardItem* demNode = NodeUtils::findOrCreateProjectNode(projects.first(), m_preparedDstNode, "dem-1.0", FOLDER_ICON);
    if (demNode) NodeUtils::findOrCreateChildItem(demNode, result.demName, "dem", result.absoluteDemPath, IMAGEDATA_ICON);
}


void DemNode::cleanupThreadResources()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
    }
    m_workerThread = nullptr;
}

void DemNode::releaseFinishedThreadResources()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

void DemNode::onError(const QString& error)
{
    releaseFinishedThreadResources();
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_timesEdit) m_timesEdit->setEnabled(true);
    onMethodChanged(m_method - 1);

    setLastErrorMessage(error);
    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
}

void DemNode::onCancelled()
{
    releaseFinishedThreadResources();
    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset(); m_imageInfoData.reset(); setOutputData(0, nullptr); setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_timesEdit) m_timesEdit->setEnabled(true);
}

void DemNode::onModelUpdated(QStandardItemModel* model)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

bool DemNode::validateAndRestoreOutput()
{
    m_anchorQualityWarnings.clear();
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        return false;
    }
    ProductDescriptor::Ptr descriptor;
    if (!NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), dstNode, descriptor)) {
        return false;
    }
    if (!descriptor || descriptor->provenance().value(QStringLiteral("demAbsolutePhaseAnchorVersion")) != QStringLiteral("2") ||
        descriptor->provenance().value(QStringLiteral("demAnchorReferenceResourceId")).isEmpty() ||
        descriptor->provenance().value(QStringLiteral("demAnchorReferenceHash")).isEmpty() ||
        descriptor->provenance().value(QStringLiteral("demAnchorGeoidModelId")).isEmpty() ||
        descriptor->provenance().value(QStringLiteral("demAnchorGeoidModelHash")).isEmpty()) {
        setLastErrorMessage(QStringLiteral("Committed DEM output predates absolute-phase anchoring v2 and cannot be restored as executable."));
        return false;
    }
    const QMap<QString, QString> committedProvenance = descriptor->provenance();
    const QString committedResourceId = committedProvenance.value(QStringLiteral("demAnchorReferenceResourceId"));
    const QString committedCanonicalHash = committedProvenance.value(QStringLiteral("demAnchorReferenceHash"));
    const QString committedGeoidId = committedProvenance.value(QStringLiteral("demAnchorGeoidModelId"));
    const QString committedGeoidHash = committedProvenance.value(QStringLiteral("demAnchorGeoidModelHash"));
    NodeUtils::AuxiliaryDemBinding currentBinding;
    QString currentBindingError;
    const QJsonObject restoreGeometry = m_inputData
        ? NodeUtils::inputGeometryFromProductDescriptor(m_inputData->physicalProductDescriptor()) : QJsonObject();
    if (!m_auxiliaryDemData || restoreGeometry.isEmpty() ||
        !NodeUtils::resolveAuxiliaryDemBinding(projectPath(), *m_auxiliaryDemData,
                                               currentBinding, &currentBindingError, restoreGeometry, true) ||
        currentBinding.resourceId != committedResourceId ||
        currentBinding.canonicalMetadataHash != committedCanonicalHash ||
        currentBinding.geoidModelId != committedGeoidId ||
        currentBinding.geoidModelHash != committedGeoidHash) {
        setLastErrorMessage(currentBindingError.isEmpty()
            ? QStringLiteral("Committed DEM absolute-phase anchor provenance no longer matches the current auxiliary terrain DEM binding.")
            : currentBindingError);
        return false;
    }
    for (const QString& h5Path : h5Paths) {
        QString anchorError;
        QString anchorQualityWarning;
        const DemAbsolutePhaseAnchorV2Policy restorePolicy;
        if (!validateAcceptedDemAnchorProvenance(h5Path, restorePolicy,
                                                 committedResourceId, committedCanonicalHash,
                                                 committedGeoidId, committedGeoidHash,
                                                 QList<int>(), &anchorError, &anchorQualityWarning)) {
            setLastErrorMessage(anchorError);
            return false;
        }
        if (!anchorQualityWarning.isEmpty()) m_anchorQualityWarnings.append(anchorQualityWarning);
    }
    QString descriptorError;
    descriptor = descriptorWithDemGeometry(descriptor, h5Paths, &descriptorError);
    if (!descriptor) {
        setLastErrorMessage(descriptorError);
        return false;
    }
    QStringList expectedJpgPaths;
    QStringList types;

    for (const QString& h5Path : h5Paths) {
        const QFileInfo info(h5Path);
        expectedJpgPaths.append(info.absolutePath() + "/" + info.baseName() + ".jpg");
        types.append("dem");
    }

    QString committedRunId;
    NodeUtils::loadCommittedOutputManifestRunId(projectPath(), dstNode, committedRunId);
    bool geometryAvailable = !h5Paths.isEmpty();
    for (const QString& h5Path : h5Paths) {
        double minLon = 0.0, maxLon = 0.0, minLat = 0.0, maxLat = 0.0;
        geometryAvailable = geometryAvailable &&
            NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_min_lon"), minLon) &&
            NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_max_lon"), maxLon) &&
            NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_min_lat"), minLat) &&
            NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_max_lat"), maxLat) &&
            maxLon > minLon && maxLat > minLat;
    }
    m_outputData = std::make_shared<InsarDemData>(
        h5Paths, committedRunId,
        geometryAvailable ? InsarDemData::Availability::Executable
                          : InsarDemData::Availability::HistoricalReadOnly);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);
    if (executionState() != ExecutionState::Running) {
        Q_EMIT dataUpdated(0);
    }

    // Remedy missing JPG previews in background (SOP Rule 15)
    QStringList existingJpgPaths;
    QStringList missingH5s;
    QStringList missingJpgs;
    QStringList missingTypes;

    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) {
            existingJpgPaths.append(expectedJpgPaths[i]);
        } else {
            missingH5s.append(h5Paths[i]);
            missingJpgs.append(expectedJpgPaths[i]);
            missingTypes.append(types[i]);
        }
    }

    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
        setOutputData(1, m_imageInfoData);
        if (executionState() != ExecutionState::Running) {
            Q_EMIT dataUpdated(1);
        }
    } else {
        m_remedyWatcher.disconnect();
        if (m_remedyWatcher.isRunning()) {
            m_remedyWatcher.cancel();
        }
        m_previewGenerationPending = true;
        const quint64 previewGenerationId = ++m_previewGenerationId;
        QStringList previewJpgPaths;
        for (const QString& missingJpg : missingJpgs) {
            const QFileInfo info(missingJpg);
            previewJpgPaths.append(info.absolutePath() + "/." + info.baseName() +
                QStringLiteral(".preview-%1.jpg").arg(previewGenerationId));
            QFile::remove(missingJpg);
        }

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Paths, expectedJpgPaths, missingJpgs, previewJpgPaths, previewGenerationId]() {
            if (!m_previewGenerationPending || previewGenerationId != m_previewGenerationId) {
                for (const QString& previewJpgPath : previewJpgPaths) {
                    QFile::remove(previewJpgPath);
                }
                return;
            }
            m_previewGenerationPending = false;
            for (int i = 0; i < previewJpgPaths.size() && i < missingJpgs.size(); ++i) {
                if (QFile::exists(previewJpgPaths[i])) {
                    QFile::rename(previewJpgPaths[i], missingJpgs[i]);
                }
            }
            QStringList validJpgPaths;
            for (int i = 0; i < h5Paths.size() && i < expectedJpgPaths.size(); ++i) {
                if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) {
                    validJpgPaths.append(expectedJpgPaths[i]);
                }
            }
            if (!validJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(validJpgPaths);
                setOutputData(1, m_imageInfoData);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
            }
            InSARLogManager::LogInfo("DemNode", "validateAndRestoreOutput background rendering completed.");

            if (executionState() == ExecutionState::Running) {
                setProgress(100);
                finishDemExecution();
            } else {
                Q_EMIT dataUpdated(1);
            }
        });

        QFuture<void> future = QtConcurrent::run([missingH5s, previewJpgPaths, missingTypes]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(missingH5s[i], previewJpgPaths[i], missingTypes[i]);
            }
        });
        m_remedyWatcher.setFuture(future);
    }

    return true;
}

QStringList DemNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return list;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        for (const QString& h5Path : h5Paths) {
            const QFileInfo info(h5Path);
            QString jpgPath = info.absolutePath() + "/" + info.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                list.append(jpgPath);
            }
        }
    }
    return list;
}

QStandardItemModel* DemNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString DemNode::projectPath() const
{
    IApplicationInterface* iface = nullptr;
    if (_widget) iface = NodeUtils::getProjectContext(_widget);
    if (!iface) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* mainWin = qobject_cast<MainWindow*>(w)) {
                if (mainWin->workspaceUI()) { iface = mainWin->workspaceUI(); break; }
                if (mainWin->interfaceManager()) { iface = mainWin->interfaceManager()->currentInterface(); if (iface) break; }
            }
        }
    }
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString DemNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* DemNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void DemNode::execute()
{
    executeProcessing();
}

void DemNode::stopExecution()
{
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
        return;
    }

    if (!m_previewGenerationPending) {
        return;
    }

    m_previewGenerationPending = false;
    ++m_previewGenerationId;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_timesEdit) m_timesEdit->setEnabled(true);
    onMethodChanged(m_method - 1);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void DemNode::processAutomatically()
{
    if (prepareToStart())
    {
        executeProcessing();
    }
    else
    {
        // Keep preparation failures visible instead of disguising them as a
        // missing-upstream-input Pending state. The full phase contract is
        // checked by DemWorker after execution has started.
        if (executionState() == ExecutionState::Running) {
            setState(ExecutionState::Pending);
        }
    }
}

namespace {

QString demGenerationMethodName(int method)
{
    switch (method) {
    case 1: return QObject::tr("Newton 迭代反演");
    default: return QObject::tr("未知方法 (%1)").arg(method);
    }
}

struct DemGenerationImageDiagnostics
{
    QString inputName;
    bool outputFound = false;
    bool inputRead = false;
    bool outputRead = false;
    bool dimensionsMatch = false;
    int inputRows = 0;
    int inputCols = 0;
    int outputRows = 0;
    int outputCols = 0;
    bool hasRecordedMethod = false;
    int recordedMethod = 0;
    bool hasRecordedIterations = false;
    int recordedIterations = 0;
    bool dependenciesComplete = false;
    QStringList missingDependencies;
    qint64 totalPixels = 0;
    qint64 finitePixels = 0;
    double minHeight = std::numeric_limits<double>::infinity();
    double maxHeight = -std::numeric_limits<double>::infinity();
    double sumHeight = 0.0;
    double sumSquaredHeight = 0.0;
};

struct DemGenerationValidationResults
{
    bool success = false;
    QString errorMessage;
    bool inputConnected = false;
    int expectedMethod = 1;
    int expectedIterations = 24;
    bool hasRecordedMethod = false;
    int recordedMethod = 0;
    bool hasRecordedIterations = false;
    int recordedIterations = 0;
    bool metadataConsistent = true;
    QList<DemGenerationImageDiagnostics> images;
};

class DemGenerationValidationWidget : public BaseValidationWidget
{
public:
    DemGenerationValidationWidget(DemNode* node, QWidget* parent)
        : BaseValidationWidget(node, parent)
        , m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }

private:
    void setupUI()
    {
        setupBaseUI(QObject::tr("正在诊断 DEM 反演结果..."),
                    QObject::tr("正在核对输出完整性、参数记录、几何尺寸和高程有限值统计。"),
                    QObject::tr("DEM 反演诊断汇总"),
                    QObject::tr("DEM 参数与结果诊断"));

        m_validPixelsLabel = createFeatureLabel();
        m_heightRangeLabel = createFeatureLabel();
        m_heightMomentsLabel = createFeatureLabel();
        m_dimensionsLabel = createFeatureLabel();
        m_dependenciesLabel = createFeatureLabel();
        m_issuesLabel = createFeatureLabel();

        m_featureLayout->addRow(createHeaderLabel(QObject::tr("有限值比例（非覆盖率）:")), m_validPixelsLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("高程数值范围:")), m_heightRangeLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("均值 / 标准差:")), m_heightMomentsLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("尺寸匹配结果:")), m_dimensionsLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("下游关键数据:")), m_dependenciesLabel);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("缺失或异常结果:")), m_issuesLabel);
    }

    void setNotExecutedState()
    {
        m_statusTitle->setText(QObject::tr("诊断不可用"));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
        m_statusDesc->setText(QObject::tr("请先成功执行 DEM Generation 节点，再查看诊断结果。"));
        m_compTable->clearComparison();
        m_compTable->setEnabled(false);
        m_validPixelsLabel->setText(QObject::tr("未执行"));
        m_heightRangeLabel->setText(QObject::tr("未执行"));
        m_heightMomentsLabel->setText(QObject::tr("未执行"));
        m_dimensionsLabel->setText(QObject::tr("未执行"));
        m_dependenciesLabel->setText(QObject::tr("未执行"));
        m_issuesLabel->setText(QObject::tr("未执行"));
    }

    void setInputDisconnectedState()
    {
        m_statusTitle->setText(QObject::tr("输入相位未连接"));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
        m_statusDesc->setText(QObject::tr("未找到输入相位 H5 文件（上游未连接或尚未重新流转）。仍可对已有 DEM 成果执行自包含诊断，仅无法核对尺寸与相位输入。"));
        m_compTable->clearComparison();
        m_compTable->setEnabled(false);
        m_validPixelsLabel->setText(QObject::tr("待连接输入相位"));
        m_heightRangeLabel->setText(QObject::tr("待连接输入相位"));
        m_heightMomentsLabel->setText(QObject::tr("待连接输入相位"));
        m_dimensionsLabel->setText(QObject::tr("待连接输入相位"));
        m_dependenciesLabel->setText(QObject::tr("待连接输入相位"));
        m_issuesLabel->setText(QObject::tr("待连接输入相位"));
    }

    void startAsyncValidation() override
    {
        const quint64 currentEpoch = ++m_validationEpoch;
        m_isTimedOut = false;
        if (m_node->executionState() != ExecutionState::Completed) {
            setNotExecutedState();
            return;
        }

        const auto inputData = m_node->inputDataForValidation();
        const auto outputData = std::dynamic_pointer_cast<InsarDemData>(m_node->outData(0));
        if (!outputData || outputData->h5Paths().isEmpty()) {
            m_statusTitle->setText(QObject::tr("诊断失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未找到完整的输出 DEM H5 文件列表。"));
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);
            m_validPixelsLabel->setText(QObject::tr("诊断失败"));
            m_heightRangeLabel->setText(QObject::tr("诊断失败"));
            m_heightMomentsLabel->setText(QObject::tr("诊断失败"));
            m_dimensionsLabel->setText(QObject::tr("诊断失败"));
            m_dependenciesLabel->setText(QObject::tr("诊断失败"));
            m_issuesLabel->setText(QObject::tr("诊断失败"));
            return;
        }

        const QJsonObject settings = m_node->save();
        const QStringList inputPaths = inputData ? inputData->filePaths() : QStringList();
        const QStringList outputPaths = outputData->h5Paths();
        const int expectedMethod = settings.value("method").toInt(1);
        const int expectedIterations = settings.value("times").toInt(24);

        if (inputPaths.isEmpty()) {
            setInputDisconnectedState();
        }

        m_loadingOverlay->startLoading(QObject::tr("正在读取全部 DEM 结果并计算数值统计..."));

        QFuture<DemGenerationValidationResults> future = QtConcurrent::run(
            [inputPaths, outputPaths, expectedMethod, expectedIterations]() {
                NodeUtils::Hdf5Locker locker;
                DemGenerationValidationResults result;
                result.expectedMethod = expectedMethod;
                result.expectedIterations = expectedIterations;
                result.inputConnected = !inputPaths.isEmpty();

                const auto readIntScalar = [](const QString& filePath, const QString& dataset, int& value) {
                    return NodeUtils::readScalarFromH5(filePath, dataset, value);
                };
                const auto readDoubleScalar = [](const QString& filePath, const QString& dataset, double& value) {
                    return NodeUtils::readScalarFromH5(filePath, dataset, value);
                };

                QHash<QString, QString> inputsByBaseName;
                for (const QString& inputPath : inputPaths) {
                    inputsByBaseName.insert(QFileInfo(inputPath).baseName(), inputPath);
                }

                for (const QString& outputPath : outputPaths) {
                    const QString outputBase = QFileInfo(outputPath).baseName();
                    DemGenerationImageDiagnostics image;
                    image.inputName = outputBase.endsWith(QStringLiteral("_dem"))
                        ? outputBase.left(outputBase.size() - 4)
                        : outputBase;
                    image.outputFound = true;

                    const QString pairedInputPath = inputsByBaseName.value(image.inputName);
                    if (!pairedInputPath.isEmpty()) {
                        cv::Mat inputPhase;
                        image.inputRead = NodeUtils::readMatFromH5(pairedInputPath, "phase", inputPhase) && !inputPhase.empty();
                        if (image.inputRead) {
                            image.inputRows = inputPhase.rows;
                            image.inputCols = inputPhase.cols;
                        }
                    }

                    cv::Mat dem;
                    image.outputRead = NodeUtils::readMatFromH5(outputPath, "dem", dem) && !dem.empty();
                    if (image.outputRead) {
                        image.outputRows = dem.rows;
                        image.outputCols = dem.cols;
                    }
                    image.dimensionsMatch = image.inputRead && image.outputRead
                        && image.inputRows == image.outputRows && image.inputCols == image.outputCols;

                    image.hasRecordedMethod = readIntScalar(outputPath, "dem_generation_method", image.recordedMethod);
                    image.hasRecordedIterations = readIntScalar(outputPath, "dem_generation_iterations", image.recordedIterations);
                    if (image.hasRecordedMethod) {
                        if (!result.hasRecordedMethod) {
                            result.hasRecordedMethod = true;
                            result.recordedMethod = image.recordedMethod;
                        } else if (result.recordedMethod != image.recordedMethod) {
                            result.metadataConsistent = false;
                        }
                    }
                    if (image.hasRecordedIterations) {
                        if (!result.hasRecordedIterations) {
                            result.hasRecordedIterations = true;
                            result.recordedIterations = image.recordedIterations;
                        } else if (result.recordedIterations != image.recordedIterations) {
                            result.metadataConsistent = false;
                        }
                    }

                    std::string source;
                    cv::Mat auxiliary;
                    const bool source1Present = NodeUtils::readStringFromH5(outputPath, "source_1", source);
                    const bool source2Present = NodeUtils::readStringFromH5(outputPath, "source_2", source);
                    const bool flatPhasePresent =
                        (NodeUtils::readMatFromH5(outputPath, "flat_earth_reference_phase", auxiliary) && !auxiliary.empty()) ||
                        (NodeUtils::readMatFromH5(outputPath, "flat_phase_coefficient", auxiliary) && !auxiliary.empty());
					QString flatEarthContractError;
					const bool flatEarthContractValid = NodeUtils::validateFlatEarthReferenceContract(
						outputPath, &flatEarthContractError);
                    const bool rangeLengthPresent = NodeUtils::readMatFromH5(outputPath, "range_len", auxiliary) && !auxiliary.empty();
                    const bool azimuthLengthPresent = NodeUtils::readMatFromH5(outputPath, "azimuth_len", auxiliary) && !auxiliary.empty();
                    const bool multilookRangePresent = NodeUtils::readMatFromH5(outputPath, "multilook_rg", auxiliary) && !auxiliary.empty();
                    const bool multilookAzimuthPresent = NodeUtils::readMatFromH5(outputPath, "multilook_az", auxiliary) && !auxiliary.empty();
                    double minLon = 0.0, maxLon = 0.0, minLat = 0.0, maxLat = 0.0;
                    const bool geometryPresent =
                        readDoubleScalar(outputPath, "dem_min_lon", minLon) &&
                        readDoubleScalar(outputPath, "dem_max_lon", maxLon) &&
                        readDoubleScalar(outputPath, "dem_min_lat", minLat) &&
                        readDoubleScalar(outputPath, "dem_max_lat", maxLat) &&
                        maxLon > minLon && maxLat > minLat;
                    if (!source1Present) image.missingDependencies.append(QStringLiteral("source_1"));
                    if (!source2Present) image.missingDependencies.append(QStringLiteral("source_2"));
                    if (!flatPhasePresent) image.missingDependencies.append(QStringLiteral("flat_earth_reference_phase/flat_phase_coefficient"));
					if (!flatEarthContractValid) image.missingDependencies.append(QStringLiteral("flat_earth_contract: %1").arg(flatEarthContractError));
                    if (!rangeLengthPresent) image.missingDependencies.append(QStringLiteral("range_len"));
                    if (!azimuthLengthPresent) image.missingDependencies.append(QStringLiteral("azimuth_len"));
                    if (!multilookRangePresent) image.missingDependencies.append(QStringLiteral("multilook_rg"));
                    if (!multilookAzimuthPresent) image.missingDependencies.append(QStringLiteral("multilook_az"));
                    if (!geometryPresent) image.missingDependencies.append(QStringLiteral("dem_geometry(lon/lat)"));
                    image.dependenciesComplete = image.missingDependencies.isEmpty();

                    if (image.outputRead) {
                        cv::Mat demDouble;
                        dem.convertTo(demDouble, CV_64F);
                        image.totalPixels = static_cast<qint64>(demDouble.total());
                        for (int row = 0; row < demDouble.rows; ++row) {
                            const double* values = demDouble.ptr<double>(row);
                            for (int column = 0; column < demDouble.cols; ++column) {
                                const double value = values[column];
                                if (!std::isfinite(value)) {
                                    continue;
                                }
                                ++image.finitePixels;
                                image.minHeight = std::min(image.minHeight, value);
                                image.maxHeight = std::max(image.maxHeight, value);
                                image.sumHeight += value;
                                image.sumSquaredHeight += value * value;
                            }
                        }
                    }

                    result.images.append(image);
                }

                result.success = !result.images.isEmpty();
                if (!result.success) {
                    result.errorMessage = QObject::tr("没有可用于诊断的 DEM 影像。");
                }
                return result;
            });

        auto* watcher = new QFutureWatcher<DemGenerationValidationResults>(this);
        connect(watcher, &QFutureWatcher<DemGenerationValidationResults>::finished, this, [this, watcher, currentEpoch]() {
            if (currentEpoch != m_validationEpoch || m_isTimedOut) {
                watcher->deleteLater();
                return;
            }

            const DemGenerationValidationResults result = watcher->result();
            m_loadingOverlay->stopLoading();
            if (!result.success) {
                m_statusTitle->setText(QObject::tr("诊断失败"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                m_statusDesc->setText(result.errorMessage);
                m_compTable->clearComparison();
                m_compTable->setEnabled(false);
                m_validPixelsLabel->setText(QObject::tr("诊断失败"));
                m_heightRangeLabel->setText(QObject::tr("诊断失败"));
                m_heightMomentsLabel->setText(QObject::tr("诊断失败"));
                m_dimensionsLabel->setText(QObject::tr("诊断失败"));
                m_dependenciesLabel->setText(QObject::tr("诊断失败"));
                m_issuesLabel->setText(QObject::tr("诊断失败"));
                watcher->deleteLater();
                return;
            }

            m_compTable->clearComparison();
            m_compTable->setEnabled(true);
            if (result.hasRecordedMethod) {
                m_compTable->addComparison(QObject::tr("反演方法"), demGenerationMethodName(result.expectedMethod),
                    demGenerationMethodName(result.recordedMethod));
            } else {
                m_compTable->addDiagnostic(QObject::tr("反演方法"),
                    QObject::tr("当前设置：%1；输出未记录（旧结果）").arg(demGenerationMethodName(result.expectedMethod)));
            }
            if (result.hasRecordedIterations) {
                m_compTable->addComparison(QObject::tr("Newton 迭代次数"), QString::number(result.expectedIterations),
                    QString::number(result.recordedIterations));
            } else {
                m_compTable->addDiagnostic(QObject::tr("Newton 迭代次数"),
                    QObject::tr("当前设置：%1；输出未记录（旧结果）").arg(result.expectedIterations));
            }

            int foundOutputs = 0;
            int dimensionsMatch = 0;
            int completeDependencies = 0;
            int invalidResults = 0;
            qint64 totalPixels = 0;
            qint64 finitePixels = 0;
            double minimumHeight = std::numeric_limits<double>::infinity();
            double maximumHeight = -std::numeric_limits<double>::infinity();
            double sumHeight = 0.0;
            double sumSquaredHeight = 0.0;
            QStringList issues;

            for (const DemGenerationImageDiagnostics& image : result.images) {
                foundOutputs += image.outputFound ? 1 : 0;
                const QString expectedSize = !image.inputRead
                    ? (result.inputConnected ? QObject::tr("输入 phase 不可读") : QObject::tr("未连接输入相位"))
                    : QStringLiteral("%1 x %2").arg(image.inputCols).arg(image.inputRows);
                QString actualSize;
                if (!image.outputRead) {
                    actualSize = QObject::tr("输出 dem 不可读");
                    issues.append(image.inputName + QObject::tr(": 输出 dem 不可读"));
                    ++invalidResults;
                } else {
                    actualSize = QStringLiteral("%1 x %2").arg(image.outputCols).arg(image.outputRows);
                    if (!image.inputRead) {
                        issues.append(image.inputName + QObject::tr(": 未连接或无法读取输入相位"));
                        ++invalidResults;
                    } else if (!image.dimensionsMatch) {
                        issues.append(image.inputName + QObject::tr(": 尺寸不匹配"));
                        ++invalidResults;
                    } else {
                        ++dimensionsMatch;
                    }
                    if (image.finitePixels == 0) {
                        issues.append(image.inputName + QObject::tr(": 无有限 DEM 数值"));
                        ++invalidResults;
                    }
                }
                m_compTable->addComparison(image.inputName, expectedSize, actualSize);

                if (image.dependenciesComplete) {
                    ++completeDependencies;
                } else if (image.outputFound) {
                    issues.append(image.inputName + QObject::tr(": 缺少 ") + image.missingDependencies.join(QStringLiteral(", ")));
                    ++invalidResults;
                }

                if (image.outputRead) {
                    totalPixels += image.totalPixels;
                    finitePixels += image.finitePixels;
                    minimumHeight = std::min(minimumHeight, image.minHeight);
                    maximumHeight = std::max(maximumHeight, image.maxHeight);
                    sumHeight += image.sumHeight;
                    sumSquaredHeight += image.sumSquaredHeight;
                }
            }

            m_compTable->addComparison(QObject::tr("输入 / 输出影像数"),
                QString::number(result.images.size()), QString::number(foundOutputs));
            const double validRatio = totalPixels > 0 ? 100.0 * finitePixels / totalPixels : 0.0;
            m_validPixelsLabel->setText(totalPixels > 0
                ? QObject::tr("%1 / %2 (%3%)").arg(finitePixels).arg(totalPixels).arg(QString::number(validRatio, 'f', 2))
                : QObject::tr("无可读取的 DEM 数值"));
            m_heightRangeLabel->setText(finitePixels > 0
                ? QObject::tr("%1 ~ %2").arg(QString::number(minimumHeight, 'g', 7), QString::number(maximumHeight, 'g', 7))
                : QObject::tr("无有限 DEM 数值"));
            if (finitePixels > 0) {
                const double meanHeight = sumHeight / finitePixels;
                const double variance = std::max(0.0, sumSquaredHeight / finitePixels - meanHeight * meanHeight);
                m_heightMomentsLabel->setText(QObject::tr("%1 / %2")
                    .arg(QString::number(meanHeight, 'g', 7), QString::number(std::sqrt(variance), 'g', 7)));
            } else {
                m_heightMomentsLabel->setText(QObject::tr("无有限 DEM 数值"));
            }
            m_dimensionsLabel->setText(result.inputConnected
                ? QObject::tr("%1 / %2 匹配").arg(dimensionsMatch).arg(result.images.size())
                : QObject::tr("未连接输入相位，无法核对尺寸"));
            m_dependenciesLabel->setText(QObject::tr("%1 / %2 齐全").arg(completeDependencies).arg(result.images.size()));
            m_issuesLabel->setText(issues.isEmpty() ? QObject::tr("未发现缺失或尺寸异常") : issues.join(QStringLiteral("\n")));

            const bool parametersMatch = result.metadataConsistent
                && (!result.hasRecordedMethod || result.recordedMethod == result.expectedMethod)
                && (!result.hasRecordedIterations || result.recordedIterations == result.expectedIterations);
            if (!result.inputConnected) {
                m_statusTitle->setText(QObject::tr("需要复查"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                m_statusDesc->setText(QObject::tr("未连接输入相位，无法核对尺寸与相位输入。已完成对已有 DEM 输出的自包含诊断：输出、元数据与高程数值统计结果仅反映固有产出，请连接上游相位节点后重新执行以获取完整诊断。"));
            } else if (invalidResults > 0 || finitePixels == 0) {
                m_statusTitle->setText(QObject::tr("需要复查"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                m_statusDesc->setText(QObject::tr("发现输出缺失、尺寸异常、无有限高程数值或下游关键数据不完整。请检查对应影像和处理日志。"));
            } else if (!parametersMatch) {
                m_statusTitle->setText(QObject::tr("参数与结果不一致"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                m_statusDesc->setText(QObject::tr("当前反演参数与输出 H5 中记录的参数不一致；修改参数后需要重新执行节点。"));
            } else {
                m_statusTitle->setText(QObject::tr("诊断完成"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                m_statusDesc->setText((!result.hasRecordedMethod || !result.hasRecordedIterations)
                    ? QObject::tr("输出完整且数值可读。旧结果未记录反演参数，无法确认其与当前设置是否一致。")
                    : QObject::tr("输出完整、尺寸匹配且高程数值可读。有限值比例仅用于数值完整性诊断，不代表地理覆盖率，也不构成绝对高程精度评估。"));
            }
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }

    DemNode* m_node = nullptr;
    QLabel* m_validPixelsLabel = nullptr;
    QLabel* m_heightRangeLabel = nullptr;
    QLabel* m_heightMomentsLabel = nullptr;
    QLabel* m_dimensionsLabel = nullptr;
    QLabel* m_dependenciesLabel = nullptr;
    QLabel* m_issuesLabel = nullptr;
};

} // namespace

::QWidget* DemNode::createValidationWidget(::QWidget* parent)
{
    return new DemGenerationValidationWidget(this, parent);
}

} // namespace QtNodes
