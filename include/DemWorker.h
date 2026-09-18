#pragma once

#include "BaseWorker.h"
#include "NodeUtils.h"
#include <QList>
#include <QMetaType>
#include <QStringList>
#include <algorithm>

struct DemAbsolutePhaseAnchorV2Policy {
    static constexpr int kVersion = 2;

    int version = kVersion;
    int azimuthCellsPerBurst = 12;
    int rangeCellsPerBurst = 12;
    double minimumConsensusFraction = 0.45;
    double maximumSparseHeightResidualMeters = 50.0;
    double minimumComplexGamma = 0.7;
    int minimumSelectionCandidatesPerBurst = 12;
    int minimumValidationCandidatesPerBurst = 12;
    bool requireAllPhaseComponents = true;
    bool requireEllipsoidHeightConversion = true;

    bool isValid() const
    {
        return version == kVersion && azimuthCellsPerBurst >= 12 &&
               rangeCellsPerBurst >= 12 && minimumConsensusFraction > 0.0 &&
               minimumConsensusFraction <= 1.0 && requireAllPhaseComponents &&
               requireEllipsoidHeightConversion &&
               maximumSparseHeightResidualMeters > 0.0 &&
               minimumComplexGamma > 0.0 && minimumComplexGamma <= 1.0 &&
               minimumSelectionCandidatesPerBurst >= 12 &&
               minimumValidationCandidatesPerBurst >= 12;
    }
};

struct DemH5ArtifactSnapshot {
    QString absolutePath;
    QString sha256;
    QString semanticIdentityJson;

    bool isValid() const
    {
        return !absolutePath.isEmpty() && !sha256.isEmpty() && !semanticIdentityJson.isEmpty();
    }

    bool isReadyForHashing() const
    {
        return !absolutePath.isEmpty() && !semanticIdentityJson.isEmpty();
    }
};

struct DemPhaseAnchorInputSnapshot {
    DemH5ArtifactSnapshot phase;
    DemH5ArtifactSnapshot master;
    DemH5ArtifactSnapshot slave;
    QList<int> retainedMasterBurstIndices;
    DemH5ArtifactSnapshot geometryReference;
    QString orbitInterpolationStrategy;
    QString masterOrbitSource;
    QString masterOrbitSelectionReason;
    QString slaveOrbitSource;
    QString slaveOrbitSelectionReason;

    bool isValid() const
    {
        return phase.isValid() && master.isValid() && slave.isValid() &&
               geometryReference.isValid() && !orbitInterpolationStrategy.isEmpty() &&
               !masterOrbitSource.isEmpty() && !masterOrbitSelectionReason.isEmpty() &&
               !slaveOrbitSource.isEmpty() && !slaveOrbitSelectionReason.isEmpty() &&
               !retainedMasterBurstIndices.isEmpty();
    }

    bool isReadyForHashing() const
    {
        return phase.isReadyForHashing() && master.isReadyForHashing() &&
               slave.isReadyForHashing() && geometryReference.isReadyForHashing() &&
               !orbitInterpolationStrategy.isEmpty() && !masterOrbitSource.isEmpty() &&
               !masterOrbitSelectionReason.isEmpty() && !slaveOrbitSource.isEmpty() &&
               !slaveOrbitSelectionReason.isEmpty() && !retainedMasterBurstIndices.isEmpty();
    }
};

// Immutable execution request for DEM absolute-phase anchoring v2.  The
// resource paths are content-addressed managed files; their hashes are checked
// again on the worker thread before any Core entry point is allowed to read
// them.
struct DemAbsolutePhaseAnchorV2Request {
    int method = 1;
    int iterations = 20;
    QString projectRoot;
    QString outputNode;
    QStringList phaseNames;
    QStringList phasePaths;
    QList<DemPhaseAnchorInputSnapshot> phaseInputSnapshots;
    DemAbsolutePhaseAnchorV2Policy policy;
    NodeUtils::DemExecutionSnapshot auxiliaryDemSnapshot;
    // These are supplied by the bound auxiliary-terrain resource contract,
    // never discovered from the project directory at execution time.
    QString geoidModelPath;
    QString geoidModelHash;
    QString geoidModelId;

    bool isValid() const
    {
        return method == 1 && iterations > 0 && !projectRoot.isEmpty() &&
               !outputNode.isEmpty() && !phaseNames.isEmpty() &&
               phaseNames.size() == phasePaths.size() &&
               phaseInputSnapshots.size() == phasePaths.size() &&
               policy.isValid() &&
               auxiliaryDemSnapshot.isValid() &&
               !geoidModelPath.isEmpty() && !geoidModelHash.isEmpty() && !geoidModelId.isEmpty() &&
               std::all_of(phaseInputSnapshots.cbegin(), phaseInputSnapshots.cend(),
                           [](const DemPhaseAnchorInputSnapshot& snapshot) { return snapshot.isReadyForHashing(); });
    }
};
Q_DECLARE_METATYPE(DemAbsolutePhaseAnchorV2Request)

struct DemFileResult {
    QString demName;
    QString relativeDemPath;
    QString absoluteDemPath;
    int offsetRow = 0;
    int offsetCol = 0;
};
Q_DECLARE_METATYPE(DemFileResult)

class DemWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit DemWorker(QObject* parent = nullptr);
    ~DemWorker();

public slots:
    void Dem(DemAbsolutePhaseAnchorV2Request request);
    void DemLegacy(int method, int times, QString savePath, QString outputNode,
                   QStringList phaseNames, QStringList phasePaths);

signals:
    void cancelled();
    void demFileGenerated(const DemFileResult& result);
};
