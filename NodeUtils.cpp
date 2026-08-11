#include "include/NodeUtils.h"
#include "InSARLogManager.h"
#include <gdal_priv.h>
#include <QWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include "include/icon_source.h"
#include <QApplication>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QDir>
#include <QDateTime>
#include <QLineEdit>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QCryptographicHash>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "include/IApplicationInterface.h"
#include "include/MainWindow.h"
#include "include/WorkspaceUI.h"
#include "include/InterfaceManager.h"
#include "include/ImportDataTypes.h"
#include "tinyxml.h"
#include <FormatConversion.h>
#include <Hdf5IO.h>
#include <Utils.h>
#include <cmath>

#include <QMap>
#include <QList>
#include <memory>

namespace NodeUtils {

namespace {
QMutex g_resourceRegistryMutex(QMutex::Recursive);
QMutex g_projectXmlMutex(QMutex::Recursive);
struct ProjectXmlRevision
{
    QString hash;
    QString generation;
};
QMap<QString, ProjectXmlRevision> g_projectXmlRevisions;
QList<ResourceChangeCallback> g_resourceChangeCallbacks;
QList<AuxiliaryDemLabelTableChangedCallback> g_labelTableChangedCallbacks;
QList<AuxiliaryDemLabelReboundCallback> g_labelReboundCallbacks;
QMap<QString, AuxiliaryDemLabelBinding> g_pendingAuxiliaryDemLabels;
QString resourceRegistryPath(const QString& projectRoot)
{
    return QDir(projectRoot).absoluteFilePath(QStringLiteral(".dem_resource_registry.json"));
}

QString resourceLabelPath(const QString& projectRoot)
{
    return QDir(projectRoot).absoluteFilePath(QStringLiteral(".dem_resource_labels.json"));
}

QString normalizedDemLabel(const QString& label)
{
    return label.trimmed().toCaseFolded();
}

QByteArray sha256File(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(4 * 1024 * 1024);
        if (block.isEmpty() && file.error() != QFile::NoError) return QByteArray();
        hash.addData(block);
    }
    return hash.result().toHex();
}

QString fileGeneration(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isFile()) return QString();
    return QStringLiteral("%1:%2:%3")
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(QString::fromLatin1(sha256File(path)));
}

ProjectXmlRevision projectXmlRevision(const QString& path)
{
    ProjectXmlRevision revision;
    revision.hash = QString::fromLatin1(sha256File(path));
    revision.generation = fileGeneration(path);
    return revision;
}

bool sameProjectXmlRevision(const ProjectXmlRevision& left, const ProjectXmlRevision& right)
{
    return !left.hash.isEmpty() && !left.generation.isEmpty() &&
           left.hash == right.hash && left.generation == right.generation;
}

void rememberProjectXmlRevision(const QString& canonicalPath, const ProjectXmlRevision& revision)
{
    if (!canonicalPath.isEmpty() && !revision.hash.isEmpty() && !revision.generation.isEmpty()) {
        g_projectXmlRevisions.insert(QDir::cleanPath(canonicalPath), revision);
    }
}

void releaseMetadataCommitLock(OutputTransaction& transaction)
{
    if (!transaction.metadataCommitLockHeld) {
        return;
    }
    transaction.metadataCommitLockHeld = false;
    g_projectXmlMutex.unlock();
}

class MetadataCommitLockReleaseGuard
{
public:
    explicit MetadataCommitLockReleaseGuard(OutputTransaction& transaction)
        : m_transaction(transaction)
    {
    }

    ~MetadataCommitLockReleaseGuard()
    {
        if (m_active) {
            releaseMetadataCommitLock(m_transaction);
        }
    }

    void dismiss()
    {
        m_active = false;
    }

private:
    OutputTransaction& m_transaction;
    bool m_active = true;
};

void notifyResourceChange(const QString& resourceId, const QString& provenanceId, ResourceChangeKind kind)
{
    QList<ResourceChangeCallback> callbacks;
    {
        QMutexLocker locker(&g_resourceRegistryMutex);
        callbacks = g_resourceChangeCallbacks;
    }
    for (const ResourceChangeCallback& callback : callbacks) {
        if (callback) callback(resourceId, provenanceId, kind);
    }
}

void notifyLabelTableChanged()
{
    QList<AuxiliaryDemLabelTableChangedCallback> callbacks;
    {
        QMutexLocker locker(&g_resourceRegistryMutex);
        callbacks = g_labelTableChangedCallbacks;
    }
    for (const AuxiliaryDemLabelTableChangedCallback& callback : callbacks) if (callback) callback();
}

void notifyLabelRebound(const QString& label)
{
    QList<AuxiliaryDemLabelReboundCallback> callbacks;
    { QMutexLocker locker(&g_resourceRegistryMutex); callbacks = g_labelReboundCallbacks; }
    for (const AuxiliaryDemLabelReboundCallback& callback : callbacks) if (callback) callback(label);
}

bool auxiliaryDemCoversInput(const QJsonObject& metadata,
                             const QJsonObject& inputGeometry)
{
    // Geometry is required to authorize a DEM binding. An absent descriptor
    // must fail closed instead of bypassing coverage/CRS checks.
    if (inputGeometry.isEmpty()) return false;
    const QStringList required = {QStringLiteral("minLon"), QStringLiteral("maxLon"),
                                  QStringLiteral("minLat"), QStringLiteral("maxLat")};
    for (const QString& key : required) {
        if (!inputGeometry.contains(key) || !inputGeometry.value(key).isDouble() ||
            !std::isfinite(inputGeometry.value(key).toDouble())) return false;
    }
    const QString inputCrs = inputGeometry.value(QStringLiteral("crsWkt")).toString().trimmed();
    if (inputGeometry.value(QStringLiteral("maxLon")).toDouble() <= inputGeometry.value(QStringLiteral("minLon")).toDouble() ||
        inputGeometry.value(QStringLiteral("maxLat")).toDouble() <= inputGeometry.value(QStringLiteral("minLat")).toDouble() ||
        inputCrs.isEmpty()) {
        return false;
    }
    const QString demCrs = metadata.value(QStringLiteral("crsWkt")).toString().trimmed();
    const QString demVerticalDatum = metadata.value(QStringLiteral("verticalDatum")).toString().trimmed();
    const QString demResolutionUnit = metadata.value(QStringLiteral("resolutionUnit")).toString().trimmed();
    const QString demResolutionSemantic = metadata.value(QStringLiteral("resolutionCoordinateSemantic")).toString().trimmed();
    const double demResolutionX = metadata.value(QStringLiteral("resolutionX")).toDouble();
    const double demResolutionY = metadata.value(QStringLiteral("resolutionY")).toDouble();
    for (const QString& key : required) {
        if (!metadata.value(key).isDouble() || !std::isfinite(metadata.value(key).toDouble())) return false;
    }
    if (metadata.value(QStringLiteral("maxLon")).toDouble() <= metadata.value(QStringLiteral("minLon")).toDouble() ||
        metadata.value(QStringLiteral("maxLat")).toDouble() <= metadata.value(QStringLiteral("minLat")).toDouble() ||
        demCrs.isEmpty() || demVerticalDatum.isEmpty() ||
        !std::isfinite(demResolutionX) || !std::isfinite(demResolutionY) ||
        demResolutionX <= 0.0 || demResolutionY <= 0.0) {
        return false;
    }
    if (metadata.value(QStringLiteral("minLon")).toDouble() > inputGeometry.value(QStringLiteral("minLon")).toDouble() ||
        metadata.value(QStringLiteral("maxLon")).toDouble() < inputGeometry.value(QStringLiteral("maxLon")).toDouble() ||
        metadata.value(QStringLiteral("minLat")).toDouble() > inputGeometry.value(QStringLiteral("minLat")).toDouble() ||
        metadata.value(QStringLiteral("maxLat")).toDouble() < inputGeometry.value(QStringLiteral("maxLat")).toDouble()) {
        return false;
    }
    const auto isWgs84Geographic = [](const QString& crs) {
        const QString normalized = crs.toUpper();
        return normalized.contains(QStringLiteral("EPSG:4326")) ||
               (normalized.contains(QStringLiteral("WGS 84")) &&
                (normalized.contains(QStringLiteral("GEOGCS")) || normalized.contains(QStringLiteral("GEOGCRS"))));
    };
    if (demCrs != inputCrs && !(isWgs84Geographic(demCrs) && isWgs84Geographic(inputCrs))) {
        return false;
    }

    // Range/azimuth pixel spacing in Sentinel-1 H5 is in metres and is not
    // comparable with a geographic DEM's degree spacing. Enforce vertical or
    // resolution constraints only when the input explicitly declares one.
    if (inputGeometry.contains(QStringLiteral("verticalDatum")) &&
        demVerticalDatum != inputGeometry.value(QStringLiteral("verticalDatum")).toString().trimmed()) {
        return false;
    }
    const bool declaresResolutionX = inputGeometry.contains(QStringLiteral("resolutionX"));
    const bool declaresResolutionY = inputGeometry.contains(QStringLiteral("resolutionY"));
    const QString inputResolutionUnit = inputGeometry.value(QStringLiteral("resolutionUnit")).toString().trimmed();
    const QString inputResolutionSemantic = inputGeometry.value(QStringLiteral("resolutionCoordinateSemantic")).toString().trimmed();
    if (declaresResolutionX && declaresResolutionY &&
        !inputResolutionUnit.isEmpty() && !inputResolutionSemantic.isEmpty() &&
        !demResolutionUnit.isEmpty() && !demResolutionSemantic.isEmpty() &&
        inputResolutionUnit == demResolutionUnit &&
        inputResolutionSemantic == demResolutionSemantic) {
        const double inputResolutionX = inputGeometry.value(QStringLiteral("resolutionX")).toDouble();
        const double inputResolutionY = inputGeometry.value(QStringLiteral("resolutionY")).toDouble();
        if (!inputGeometry.value(QStringLiteral("resolutionX")).isDouble() ||
            !inputGeometry.value(QStringLiteral("resolutionY")).isDouble() ||
            !std::isfinite(inputResolutionX) || !std::isfinite(inputResolutionY) ||
            inputResolutionX <= 0.0 || inputResolutionY <= 0.0) {
            return false;
        }
        return demResolutionX <= inputResolutionX && demResolutionY <= inputResolutionY;
    }
    return true;
}
}

bool loadAuxiliaryDemRegistry(const QString& projectRoot,
                              QMap<QString, AuxiliaryDemRegistryEntry>& entries,
                              QString* errorMessage)
{
    QMutexLocker locker(&g_resourceRegistryMutex);
    entries.clear();
    const QString registryFilePath = resourceRegistryPath(projectRoot);
    QFile file(registryFilePath);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot read DEM resource registry: %1").arg(registryFilePath);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry JSON is invalid: %1 (offset %2)")
            .arg(registryFilePath).arg(parseError.offset);
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != 1.0 ||
        !root.contains(QStringLiteral("resources")) ||
        !root.value(QStringLiteral("resources")).isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry schema is invalid: %1")
            .arg(registryFilePath);
        return false;
    }
    const QJsonObject resources = root.value(QStringLiteral("resources")).toObject();
    for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isObject()) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry contains an invalid entry: resourceId=%1, path=%2")
                .arg(it.key(), registryFilePath);
            return false;
        }
        const QJsonObject object = it.value().toObject();
        const QString expectedRoot = QStringLiteral(".dem_resources/%1").arg(it.key());
        QString canonicalMetadataHash = object.value(QStringLiteral("canonicalMetadataHash")).toString();
        const QByteArray expectedMetadataHash = QCryptographicHash::hash(
            QJsonDocument(object.value(QStringLiteral("metadata")).toObject()).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256);
        bool legacyCanonicalHash = false;
        if (canonicalMetadataHash.size() != 64 && canonicalMetadataHash.toLatin1().size() == 32 &&
            canonicalMetadataHash.toLatin1() == expectedMetadataHash) {
            canonicalMetadataHash = QString::fromLatin1(expectedMetadataHash.toHex());
            legacyCanonicalHash = true;
        }
        if (object.value(QStringLiteral("role")).toString() != QStringLiteral("auxiliary_terrain_dem") ||
            object.value(QStringLiteral("rasterHash")).toString().isEmpty() ||
            object.value(QStringLiteral("identityH5Hash")).toString().isEmpty() ||
            object.value(QStringLiteral("validMaskHash")).toString().isEmpty() ||
            canonicalMetadataHash.isEmpty() ||
            object.value(QStringLiteral("managedRasterPath")).toString() != expectedRoot + QStringLiteral("/dem.tif") ||
            object.value(QStringLiteral("managedIdentityH5Path")).toString() != expectedRoot + QStringLiteral("/identity.h5") ||
            object.value(QStringLiteral("managedValidMaskPath")).toString() != expectedRoot + QStringLiteral("/dem_valid_mask.tif") ||
            !object.value(QStringLiteral("metadata")).isObject() ||
            !object.value(QStringLiteral("provenanceHistory")).isArray() ||
            (object.contains(QStringLiteral("tombstone")) && !object.value(QStringLiteral("tombstone")).isBool())) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry entry is incomplete or invalid: resourceId=%1, path=%2")
                .arg(it.key(), registryFilePath);
            return false;
        }
        const QJsonArray provenanceHistory = object.value(QStringLiteral("provenanceHistory")).toArray();
        QJsonArray normalizedProvenanceHistory;
        QSet<QString> provenanceIds;
        int historyIndex = 0;
        for (const QJsonValue& provenance : provenanceHistory) {
            QJsonObject provenanceObject = provenance.toObject();
            const QString pinnedId = provenanceObject.value(QStringLiteral("pinnedProvenanceId")).toString();
            const QString runId = provenanceObject.value(QStringLiteral("runId")).toString();
            const QString historyCanonicalHash = provenanceObject.value(QStringLiteral("canonicalMetadataHash")).toString();
            const bool invalid = !provenance.isObject() ||
                provenanceObject.value(QStringLiteral("role")).toString() != QStringLiteral("auxiliary_terrain_dem") ||
                provenanceObject.value(QStringLiteral("resourceId")).toString() != it.key() ||
                pinnedId.isEmpty() || runId != pinnedId || provenanceIds.contains(pinnedId) ||
                (!historyCanonicalHash.isEmpty() && historyCanonicalHash != canonicalMetadataHash) ||
                (historyCanonicalHash.isEmpty() && !legacyCanonicalHash);
            if (invalid) {
                if (errorMessage) *errorMessage = QStringLiteral(
                    "DEM resource registry provenance history is invalid: resourceId=%1, historyIndex=%2, pin=%3, runId=%4, path=%5")
                    .arg(it.key()).arg(historyIndex).arg(pinnedId, runId, registryFilePath);
                return false;
            }
            if (historyCanonicalHash.isEmpty()) {
                provenanceObject.insert(QStringLiteral("canonicalMetadataHash"), canonicalMetadataHash);
            }
            normalizedProvenanceHistory.append(provenanceObject);
            provenanceIds.insert(pinnedId);
            ++historyIndex;
        }
        AuxiliaryDemRegistryEntry entry;
        entry.resourceId = it.key();
        entry.role = object.value(QStringLiteral("role")).toString();
        entry.rasterHash = object.value(QStringLiteral("rasterHash")).toString();
        entry.identityH5Hash = object.value(QStringLiteral("identityH5Hash")).toString();
        entry.validMaskHash = object.value(QStringLiteral("validMaskHash")).toString();
        entry.canonicalMetadataHash = canonicalMetadataHash;
        entry.managedRasterPath = object.value(QStringLiteral("managedRasterPath")).toString();
        entry.managedIdentityH5Path = object.value(QStringLiteral("managedIdentityH5Path")).toString();
        entry.managedValidMaskPath = object.value(QStringLiteral("managedValidMaskPath")).toString();
        entry.metadata = object.value(QStringLiteral("metadata")).toObject();
        entry.provenanceHistory = normalizedProvenanceHistory;
        entry.tombstone = object.value(QStringLiteral("tombstone")).toBool(false);
        entries.insert(entry.resourceId, entry);
    }
    return true;
}

bool mergeAuxiliaryDemRegistryEntry(const QString& projectRoot,
                                    const AuxiliaryDemRegistryEntry& entry,
                                    QString* errorMessage)
{
    if (entry.resourceId.isEmpty() || entry.role != QStringLiteral("auxiliary_terrain_dem") ||
        entry.rasterHash.isEmpty() || entry.identityH5Hash.isEmpty() ||
        entry.validMaskHash.isEmpty() ||
        entry.canonicalMetadataHash.isEmpty() || entry.provenanceHistory.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM registry entry is incomplete.");
        return false;
    }
    QSet<QString> incomingProvenanceIds;
    for (const QJsonValue& value : entry.provenanceHistory) {
        const QJsonObject provenance = value.toObject();
        const QString pin = provenance.value(QStringLiteral("pinnedProvenanceId")).toString();
        if (!value.isObject() || provenance.value(QStringLiteral("role")).toString() != entry.role ||
            provenance.value(QStringLiteral("resourceId")).toString() != entry.resourceId ||
            pin.isEmpty() || incomingProvenanceIds.contains(pin) ||
            provenance.value(QStringLiteral("runId")).toString() != pin ||
            provenance.value(QStringLiteral("canonicalMetadataHash")).toString() != entry.canonicalMetadataHash ||
            provenance.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("path")).toString() != QStringLiteral("dem.tif") ||
            provenance.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("sha256")).toString() != entry.rasterHash ||
            provenance.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("path")).toString() != QStringLiteral("identity.h5") ||
            provenance.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("sha256")).toString() != entry.identityH5Hash ||
            provenance.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("path")).toString() != QStringLiteral("dem_valid_mask.tif") ||
            provenance.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("sha256")).toString() != entry.validMaskHash) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM registry provenance entry is incomplete or invalid.");
            return false;
        }
        incomingProvenanceIds.insert(pin);
    }
    QMutexLocker locker(&g_resourceRegistryMutex);
    QMap<QString, AuxiliaryDemRegistryEntry> entries;
    if (!loadAuxiliaryDemRegistry(projectRoot, entries, errorMessage)) {
        return false;
    }
    AuxiliaryDemRegistryEntry merged = entries.value(entry.resourceId);
    if (!merged.resourceId.isEmpty() && merged.tombstone) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot append provenance to a tombstoned DEM resource.");
        return false;
    }
    if (!merged.resourceId.isEmpty() &&
        (merged.rasterHash != entry.rasterHash || merged.identityH5Hash != entry.identityH5Hash ||
         merged.validMaskHash != entry.validMaskHash ||
         merged.canonicalMetadataHash != entry.canonicalMetadataHash)) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM registry identity conflict for resourceId.");
        return false;
    }
    merged.resourceId = entry.resourceId;
    merged.role = entry.role;
    merged.rasterHash = entry.rasterHash;
    merged.identityH5Hash = entry.identityH5Hash;
    merged.validMaskHash = entry.validMaskHash;
    merged.canonicalMetadataHash = entry.canonicalMetadataHash;
    merged.managedRasterPath = entry.managedRasterPath;
    merged.managedIdentityH5Path = entry.managedIdentityH5Path;
    merged.managedValidMaskPath = entry.managedValidMaskPath;
    merged.metadata = entry.metadata;
    merged.tombstone = false;
    for (const QJsonValue& provenance : entry.provenanceHistory) {
        bool duplicate = false;
        for (const QJsonValue& old : merged.provenanceHistory) {
            if (old.toObject().value(QStringLiteral("pinnedProvenanceId")) ==
                provenance.toObject().value(QStringLiteral("pinnedProvenanceId"))) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) merged.provenanceHistory.append(provenance);
    }
    entries.insert(merged.resourceId, merged);
    QJsonObject resources;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        QJsonObject object;
        object.insert(QStringLiteral("role"), it.value().role);
        object.insert(QStringLiteral("rasterHash"), it.value().rasterHash);
        object.insert(QStringLiteral("identityH5Hash"), it.value().identityH5Hash);
        object.insert(QStringLiteral("validMaskHash"), it.value().validMaskHash);
        object.insert(QStringLiteral("canonicalMetadataHash"), it.value().canonicalMetadataHash);
        object.insert(QStringLiteral("managedRasterPath"), it.value().managedRasterPath);
        object.insert(QStringLiteral("managedIdentityH5Path"), it.value().managedIdentityH5Path);
        object.insert(QStringLiteral("managedValidMaskPath"), it.value().managedValidMaskPath);
        object.insert(QStringLiteral("metadata"), it.value().metadata);
        object.insert(QStringLiteral("provenanceHistory"), it.value().provenanceHistory);
        object.insert(QStringLiteral("tombstone"), it.value().tombstone);
        resources.insert(it.key(), object);
    }
    QSaveFile output(resourceRegistryPath(projectRoot));
    QJsonObject registryRoot;
    registryRoot.insert(QStringLiteral("version"), 1);
    registryRoot.insert(QStringLiteral("resources"), resources);
    if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(registryRoot).toJson(QJsonDocument::Compact)) < 0 || !output.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot commit DEM resource registry.");
        return false;
    }
    notifyResourceChange(entry.resourceId,
                         entry.provenanceHistory.isEmpty() ? QString() : entry.provenanceHistory.last().toObject().value(QStringLiteral("pinnedProvenanceId")).toString(),
                         ResourceChangeKind::ProvenanceAdded);
    return true;
}

bool loadAuxiliaryDemLabels(const QString& projectRoot,
                            QMap<QString, AuxiliaryDemLabelBinding>& labels,
                            QString* errorMessage)
{
    QMutexLocker locker(&g_resourceRegistryMutex);
    labels.clear();
    QFile file(resourceLabelPath(projectRoot));
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot read DEM label registry.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if (parseError.error != QJsonParseError::NoError || (version != 1 && version != 2) ||
        !root.value(QStringLiteral("labels")).isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM label registry schema is invalid.");
        return false;
    }
    const QJsonObject stored = root.value(QStringLiteral("labels")).toObject();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        const QJsonObject value = it.value().toObject();
        const QString key = normalizedDemLabel(it.key());
        const QString resourceId = value.value(QStringLiteral("resourceId")).toString().trimmed();
        const QString provenanceId = value.value(QStringLiteral("pinnedProvenanceId")).toString().trimmed();
        AuxiliaryDemLabelBinding binding;
        binding.label = key;
        binding.resourceId = resourceId;
        binding.pinnedProvenanceId = provenanceId;
        if (version == 2 && value.value(QStringLiteral("mode")).toString() == QStringLiteral("workflow_output")) {
            binding.mode = AuxiliaryDemLabelMode::WorkflowOutput;
            binding.producerIdentity = value.value(QStringLiteral("producerIdentity")).toString().trimmed();
            binding.expectedProductType = value.value(QStringLiteral("expectedProductType")).toString().trimmed();
        }
        const bool fixedValid = binding.mode == AuxiliaryDemLabelMode::FixedResource &&
                                !resourceId.isEmpty() && !provenanceId.isEmpty();
        const bool workflowValid = binding.mode == AuxiliaryDemLabelMode::WorkflowOutput &&
                                   !binding.producerIdentity.isEmpty() &&
                                   binding.expectedProductType == QStringLiteral("auxiliary_terrain_dem") &&
                                   ((resourceId.isEmpty() && provenanceId.isEmpty()) ||
                                    (!resourceId.isEmpty() && !provenanceId.isEmpty()));
        if (key.isEmpty() || key != it.key() || !it.value().isObject() || (!fixedValid && !workflowValid)) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM label registry contains an invalid binding.");
            return false;
        }
        labels.insert(key, binding);
    }
    return true;
}

namespace {
bool saveAuxiliaryDemLabels(const QString& projectRoot,
                            const QMap<QString, AuxiliaryDemLabelBinding>& labels,
                            QString* errorMessage)
{
    QJsonObject values;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        const AuxiliaryDemLabelBinding& binding = it.value();
        QJsonObject value{{QStringLiteral("resourceId"), binding.resourceId},
                          {QStringLiteral("pinnedProvenanceId"), binding.pinnedProvenanceId}};
        if (binding.mode == AuxiliaryDemLabelMode::WorkflowOutput) {
            value.insert(QStringLiteral("mode"), QStringLiteral("workflow_output"));
            value.insert(QStringLiteral("producerIdentity"), binding.producerIdentity);
            value.insert(QStringLiteral("expectedProductType"), binding.expectedProductType);
        } else {
            value.insert(QStringLiteral("mode"), QStringLiteral("fixed_resource"));
        }
        values.insert(it.key(), value);
    }
    QSaveFile output(resourceLabelPath(projectRoot));
    const QJsonObject root{{QStringLiteral("version"), 2}, {QStringLiteral("labels"), values}};
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0 || !output.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot commit DEM label registry.");
        return false;
    }
    return true;
}
}

bool bindAuxiliaryDemLabel(const QString& projectRoot,
                           const AuxiliaryDemLabelBinding& requested,
                           bool explicitRebind,
                           QString* errorMessage)
{
    AuxiliaryDemLabelBinding binding = requested;
    binding.label = normalizedDemLabel(binding.label);
    binding.mode = AuxiliaryDemLabelMode::FixedResource;
    if (binding.label.isEmpty() || binding.resourceId.trimmed().isEmpty() || binding.pinnedProvenanceId.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM label, resource ID and pinned provenance are required.");
        return false;
    }
    QMap<QString, AuxiliaryDemRegistryEntry> resources;
    if (!loadAuxiliaryDemRegistry(projectRoot, resources, errorMessage) || !resources.contains(binding.resourceId) ||
        resources.value(binding.resourceId).tombstone) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("DEM label target is not a live managed resource.");
        return false;
    }
    bool provenanceFound = false;
    for (const QJsonValue& value : resources.value(binding.resourceId).provenanceHistory) {
        if (value.toObject().value(QStringLiteral("pinnedProvenanceId")).toString() == binding.pinnedProvenanceId) {
            provenanceFound = true;
            break;
        }
    }
    if (!provenanceFound) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM label target provenance is not registered.");
        return false;
    }
    QMutexLocker locker(&g_resourceRegistryMutex);
    QMap<QString, AuxiliaryDemLabelBinding> labels;
    if (!loadAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    const auto existing = labels.constFind(binding.label);
    if (existing != labels.constEnd() &&
        (existing->mode != AuxiliaryDemLabelMode::FixedResource ||
         existing->resourceId != binding.resourceId || existing->pinnedProvenanceId != binding.pinnedProvenanceId) &&
        !explicitRebind) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM label already exists; explicit rebind is required.");
        return false;
    }
    labels.insert(binding.label, binding);
    if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    notifyLabelTableChanged();
    if (existing != labels.constEnd() && explicitRebind) {
        notifyLabelRebound(binding.label);
    }
    return true;
}

bool declareWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                      const QString& requestedLabel,
                                      const QString& producerIdentity,
                                      bool explicitConvert,
                                      QString* errorMessage)
{
    const QString label = normalizedDemLabel(requestedLabel);
    if (label.isEmpty() || producerIdentity.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label and producer identity are required.");
        return false;
    }
    QMutexLocker locker(&g_resourceRegistryMutex);
    QMap<QString, AuxiliaryDemLabelBinding> labels;
    if (!loadAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    const auto existing = labels.constFind(label);
    if (existing != labels.constEnd() &&
        existing->mode == AuxiliaryDemLabelMode::WorkflowOutput &&
        existing->producerIdentity == producerIdentity.trimmed()) {
        return true;
    }
    if (existing != labels.constEnd() &&
        (existing->mode != AuxiliaryDemLabelMode::WorkflowOutput ||
         existing->producerIdentity != producerIdentity) && !explicitConvert) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM label already exists; explicit conversion/rebind is required.");
        return false;
    }
    AuxiliaryDemLabelBinding binding;
    binding.label = label;
    binding.mode = AuxiliaryDemLabelMode::WorkflowOutput;
    binding.producerIdentity = producerIdentity.trimmed();
    binding.expectedProductType = QStringLiteral("auxiliary_terrain_dem");
    labels.insert(label, binding);
    if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    notifyLabelTableChanged();
    if (existing != labels.constEnd()) notifyLabelRebound(label);
    return true;
}

bool activateWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                       const QString& currentRequestedLabel,
                                       const QString& nextRequestedLabel,
                                       const QString& producerIdentity,
                                       bool explicitConvert,
                                       QString* errorMessage)
{
    const QString currentLabel = normalizedDemLabel(currentRequestedLabel);
    const QString nextLabel = normalizedDemLabel(nextRequestedLabel);
    const QString producer = producerIdentity.trimmed();
    if (nextLabel.isEmpty() || producer.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label and producer identity are required.");
        return false;
    }

    {
        QMutexLocker locker(&g_resourceRegistryMutex);
        QMap<QString, AuxiliaryDemLabelBinding> labels;
        if (!loadAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;

        if (!currentLabel.isEmpty()) {
            if (!labels.contains(currentLabel) ||
                labels.value(currentLabel).mode != AuxiliaryDemLabelMode::WorkflowOutput ||
                labels.value(currentLabel).producerIdentity != producer) {
                if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label producer does not match.");
                return false;
            }
        }

        const auto existing = labels.constFind(nextLabel);
        if (existing != labels.constEnd() &&
            (existing->mode != AuxiliaryDemLabelMode::WorkflowOutput ||
             existing->producerIdentity != producer) && !explicitConvert) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM label already exists; explicit conversion/rebind is required.");
            return false;
        }
        if (!currentLabel.isEmpty() && currentLabel == nextLabel &&
            existing != labels.constEnd() &&
            existing->mode == AuxiliaryDemLabelMode::WorkflowOutput &&
            existing->producerIdentity == producer) {
            return true;
        }

        if (!currentLabel.isEmpty() && currentLabel != nextLabel) {
            labels.remove(currentLabel);
        }

        AuxiliaryDemLabelBinding next;
        next.label = nextLabel;
        next.mode = AuxiliaryDemLabelMode::WorkflowOutput;
        next.producerIdentity = producer;
        next.expectedProductType = QStringLiteral("auxiliary_terrain_dem");
        labels.insert(nextLabel, next);
        if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    }

    if (!currentLabel.isEmpty() && currentLabel != nextLabel) {
        notifyLabelRebound(currentLabel);
    }
    notifyLabelRebound(nextLabel);
    notifyLabelTableChanged();
    return true;
}

bool restoreWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                      const QString& requestedLabel,
                                      const QString& producerIdentity,
                                      bool claimLegacyProducer,
                                      QString* errorMessage)
{
    const QString label = normalizedDemLabel(requestedLabel);
    const QString producer = producerIdentity.trimmed();
    if (label.isEmpty() || producer.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label and producer identity are required.");
        return false;
    }

    QMutexLocker locker(&g_resourceRegistryMutex);
    QMap<QString, AuxiliaryDemLabelBinding> labels;
    if (!loadAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    const auto existing = labels.constFind(label);
    if (existing == labels.constEnd()) {
        AuxiliaryDemLabelBinding binding;
        binding.label = label;
        binding.mode = AuxiliaryDemLabelMode::WorkflowOutput;
        binding.producerIdentity = producer;
        binding.expectedProductType = QStringLiteral("auxiliary_terrain_dem");
        labels.insert(label, binding);
        if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
        notifyLabelTableChanged();
        return true;
    }
    if (existing->mode != AuxiliaryDemLabelMode::WorkflowOutput) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM label already belongs to a fixed resource; explicit conversion is required.");
        return false;
    }
    if (existing->producerIdentity == producer) return true;
    if (!claimLegacyProducer) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label producer does not match this node.");
        return false;
    }

    // Older workflow JSON did not persist the producer UUID.  Preserve any
    // ready resource binding while assigning it to the restored source node.
    AuxiliaryDemLabelBinding binding = *existing;
    binding.producerIdentity = producer;
    labels.insert(label, binding);
    if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    notifyLabelRebound(label);
    notifyLabelTableChanged();
    return true;
}

bool invalidateWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                         const QString& requestedLabel,
                                         const QString& producerIdentity,
                                         QString* errorMessage)
{
    const QString label = normalizedDemLabel(requestedLabel);
    if (label.isEmpty()) return true;
    QMutexLocker locker(&g_resourceRegistryMutex);
    QMap<QString, AuxiliaryDemLabelBinding> labels;
    if (!loadAuxiliaryDemLabels(projectRoot, labels, errorMessage) || !labels.contains(label)) return false;
    AuxiliaryDemLabelBinding binding = labels.value(label);
    if (binding.mode != AuxiliaryDemLabelMode::WorkflowOutput ||
        binding.producerIdentity != producerIdentity) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label producer does not match.");
        return false;
    }
    binding.resourceId.clear();
    binding.pinnedProvenanceId.clear();
    labels.insert(label, binding);
    if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    notifyLabelRebound(label);
    notifyLabelTableChanged();
    return true;
}

bool resolveWorkflowAuxiliaryDemLabel(const QString& projectRoot,
                                      const QString& requestedLabel,
                                      const QString& producerIdentity,
                                      const QString& resourceId,
                                      const QString& pinnedProvenanceId,
                                      QString* errorMessage)
{
    const QString label = normalizedDemLabel(requestedLabel);
    if (label.isEmpty()) return true;
    if (resourceId.isEmpty() || pinnedProvenanceId.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label cannot resolve an empty resource binding.");
        return false;
    }
    QMap<QString, AuxiliaryDemRegistryEntry> resources;
    if (!loadAuxiliaryDemRegistry(projectRoot, resources, errorMessage) ||
        !resources.contains(resourceId) || resources.value(resourceId).tombstone) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Workflow DEM label target is not a live managed resource.");
        }
        return false;
    }
    bool provenanceFound = false;
    for (const QJsonValue& provenance : resources.value(resourceId).provenanceHistory) {
        if (provenance.toObject().value(QStringLiteral("pinnedProvenanceId")).toString() == pinnedProvenanceId) {
            provenanceFound = true;
            break;
        }
    }
    if (!provenanceFound) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label target provenance is not registered.");
        return false;
    }
    QMutexLocker locker(&g_resourceRegistryMutex);
    QMap<QString, AuxiliaryDemLabelBinding> labels;
    if (!loadAuxiliaryDemLabels(projectRoot, labels, errorMessage) || !labels.contains(label)) return false;
    AuxiliaryDemLabelBinding binding = labels.value(label);
    if (binding.mode != AuxiliaryDemLabelMode::WorkflowOutput || binding.producerIdentity != producerIdentity) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label producer does not match.");
        return false;
    }
    binding.resourceId = resourceId;
    binding.pinnedProvenanceId = pinnedProvenanceId;
    labels.insert(label, binding);
    if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    notifyLabelRebound(label);
    notifyLabelTableChanged();
    return true;
}

void registerPendingAuxiliaryDemLabel(const AuxiliaryDemLabelBinding& requested)
{
    AuxiliaryDemLabelBinding binding = requested;
    binding.label = normalizedDemLabel(binding.label);
    if (binding.label.isEmpty() || binding.resourceId.trimmed().isEmpty() || binding.pinnedProvenanceId.trimmed().isEmpty()) return;
    QMutexLocker locker(&g_resourceRegistryMutex);
    const auto existing = g_pendingAuxiliaryDemLabels.constFind(binding.label);
    if (existing == g_pendingAuxiliaryDemLabels.constEnd() ||
        (existing->resourceId == binding.resourceId && existing->pinnedProvenanceId == binding.pinnedProvenanceId)) {
        g_pendingAuxiliaryDemLabels.insert(binding.label, binding);
    }
}

bool resolveAuxiliaryDemLabel(const QString& projectRoot,
                              const QString& label,
                              AuxiliaryDemBinding& binding,
                              QString* errorMessage,
                              const QJsonObject& inputGeometry)
{
    QMap<QString, AuxiliaryDemLabelBinding> labels;
    const QString key = normalizedDemLabel(label);
    if (key.isEmpty() || !loadAuxiliaryDemLabels(projectRoot, labels, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("DEM label is not defined in this project.");
        return false;
    }
    if (!labels.contains(key)) {
        AuxiliaryDemLabelBinding pending;
        {
            QMutexLocker locker(&g_resourceRegistryMutex);
            pending = g_pendingAuxiliaryDemLabels.value(key);
        }
        if (pending.label.isEmpty() || !bindAuxiliaryDemLabel(projectRoot, pending, false, errorMessage) ||
            !loadAuxiliaryDemLabels(projectRoot, labels, errorMessage) || !labels.contains(key)) {
            if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("DEM label is not defined in this project.");
            return false;
        }
        QMutexLocker locker(&g_resourceRegistryMutex);
        g_pendingAuxiliaryDemLabels.remove(key);
    }
    const AuxiliaryDemLabelBinding value = labels.value(key);
    if (value.isPlanned()) {
        if (errorMessage) *errorMessage = QStringLiteral("Workflow DEM label @%1 is planned and waiting for its producer.").arg(key);
        return false;
    }
    QtNodes::AuxiliaryDemReferenceData reference(value.resourceId, value.pinnedProvenanceId, 1);
    return resolveAuxiliaryDemBinding(projectRoot, reference, binding, errorMessage, inputGeometry);
}

bool tombstoneAuxiliaryDemResource(const QString& projectRoot,
                                   const QString& resourceId,
                                   QString* errorMessage)
{
    QMap<QString, AuxiliaryDemRegistryEntry> entries;
    if (!loadAuxiliaryDemRegistry(projectRoot, entries, errorMessage) || !entries.contains(resourceId)) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("DEM resource is not registered.");
        return false;
    }
    QMutexLocker locker(&g_resourceRegistryMutex);
    QJsonObject root;
    QJsonObject resources;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        QJsonObject object;
        object.insert(QStringLiteral("role"), it.value().role);
        object.insert(QStringLiteral("rasterHash"), it.value().rasterHash);
        object.insert(QStringLiteral("identityH5Hash"), it.value().identityH5Hash);
        object.insert(QStringLiteral("validMaskHash"), it.value().validMaskHash);
        object.insert(QStringLiteral("canonicalMetadataHash"), it.value().canonicalMetadataHash);
        object.insert(QStringLiteral("managedRasterPath"), it.value().managedRasterPath);
        object.insert(QStringLiteral("managedIdentityH5Path"), it.value().managedIdentityH5Path);
        object.insert(QStringLiteral("managedValidMaskPath"), it.value().managedValidMaskPath);
        object.insert(QStringLiteral("metadata"), it.value().metadata);
        object.insert(QStringLiteral("provenanceHistory"), it.value().provenanceHistory);
        object.insert(QStringLiteral("tombstone"), it.key() == resourceId ? true : it.value().tombstone);
        resources.insert(it.key(), object);
    }
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("resources"), resources);
    QSaveFile output(resourceRegistryPath(projectRoot));
    if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0 || !output.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot tombstone DEM resource.");
        return false;
    }
    notifyResourceChange(resourceId, QString(), ResourceChangeKind::Tombstoned);
    return true;
}

bool removeAuxiliaryDemRegistryEntry(const QString& projectRoot,
                                     const QString& resourceId,
                                     QString* errorMessage)
{
    QMap<QString, AuxiliaryDemRegistryEntry> entries;
    if (!loadAuxiliaryDemRegistry(projectRoot, entries, errorMessage)) return false;
    entries.remove(resourceId);
    QJsonObject resources;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        QJsonObject object;
        object.insert(QStringLiteral("role"), it.value().role);
        object.insert(QStringLiteral("rasterHash"), it.value().rasterHash);
        object.insert(QStringLiteral("identityH5Hash"), it.value().identityH5Hash);
        object.insert(QStringLiteral("canonicalMetadataHash"), it.value().canonicalMetadataHash);
        object.insert(QStringLiteral("managedRasterPath"), it.value().managedRasterPath);
        object.insert(QStringLiteral("managedIdentityH5Path"), it.value().managedIdentityH5Path);
        object.insert(QStringLiteral("metadata"), it.value().metadata);
        object.insert(QStringLiteral("provenanceHistory"), it.value().provenanceHistory);
        object.insert(QStringLiteral("tombstone"), it.value().tombstone);
        resources.insert(it.key(), object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("resources"), resources);
    QSaveFile output(resourceRegistryPath(projectRoot));
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0 || !output.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot remove DEM resource registry entry.");
        return false;
    }
    notifyResourceChange(resourceId, QString(), ResourceChangeKind::Removed);
    return true;
}

void registerResourceChangeCallback(const ResourceChangeCallback& callback)
{
    QMutexLocker locker(&g_resourceRegistryMutex);
    g_resourceChangeCallbacks.append(callback);
}

void registerAuxiliaryDemLabelTableChangedCallback(const AuxiliaryDemLabelTableChangedCallback& callback)
{
    QMutexLocker locker(&g_resourceRegistryMutex);
    g_labelTableChangedCallbacks.append(callback);
}

void registerAuxiliaryDemLabelReboundCallback(const AuxiliaryDemLabelReboundCallback& callback)
{
    QMutexLocker locker(&g_resourceRegistryMutex);
    g_labelReboundCallbacks.append(callback);
}

void publishResourceChange(const QString& resourceId,
                           const QString& provenanceId,
                           ResourceChangeKind kind)
{
    notifyResourceChange(resourceId, provenanceId, kind);
}

QJsonObject inputGeometryFromProductDescriptor(const QtNodes::ProductDescriptor::Ptr& descriptor)
{
    QJsonObject geometry;
    if (!descriptor) return geometry;
    const QMap<QString, QString> provenance = descriptor->provenance();
    const QMap<QString, QString> aliases = {
        {QStringLiteral("minLon"), QStringLiteral("min_lon")},
        {QStringLiteral("maxLon"), QStringLiteral("max_lon")},
        {QStringLiteral("minLat"), QStringLiteral("min_lat")},
        {QStringLiteral("maxLat"), QStringLiteral("max_lat")},
        {QStringLiteral("resolutionX"), QStringLiteral("resolution_x")},
        {QStringLiteral("resolutionY"), QStringLiteral("resolution_y")}};
    for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
        const QString key = it.key();
        bool ok = false;
        const QString raw = provenance.contains(key) ? provenance.value(key) : provenance.value(it.value());
        const double value = raw.toDouble(&ok);
        if (ok) geometry.insert(key, value);
    }
    const QMap<QString, QString> textAliases = {
        {QStringLiteral("crsWkt"), QStringLiteral("crs_wkt")},
        {QStringLiteral("verticalDatum"), QStringLiteral("vertical_datum")},
        {QStringLiteral("resolutionUnit"), QStringLiteral("resolution_unit")},
        {QStringLiteral("resolutionCoordinateSemantic"), QStringLiteral("resolution_coordinate_semantic")}};
    for (auto it = textAliases.constBegin(); it != textAliases.constEnd(); ++it) {
        const QString key = it.key();
        const QString raw = provenance.contains(key) ? provenance.value(key) : provenance.value(it.value());
        if (!raw.isEmpty()) geometry.insert(key, raw);
    }
    return geometry;
}

bool resolveAuxiliaryDemBinding(const QString& projectRoot,
                                const QtNodes::AuxiliaryDemData& data,
                                AuxiliaryDemBinding& binding,
                                QString* errorMessage,
                                const QJsonObject& inputGeometry)
{
    if (!data.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM entity binding is incomplete.");
        return false;
    }
    if (data.pinnedProvenanceId().contains(QLatin1Char('/')) ||
        data.pinnedProvenanceId().contains(QLatin1Char('\\')) ||
        data.pinnedProvenanceId() == QStringLiteral(".") ||
        data.pinnedProvenanceId() == QStringLiteral("..")) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM pinned provenance identifier is unsafe.");
        return false;
    }
    const QFileInfo raster(data.rasterPath());
    const QFileInfo identity(data.identityH5Path());
    if (!raster.isFile() || !raster.isReadable() || !identity.isFile() || !identity.isReadable()) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM entity files are missing or unreadable.");
        return false;
    }
    QMap<QString, AuxiliaryDemRegistryEntry> registry;
    if (!loadAuxiliaryDemRegistry(projectRoot, registry, errorMessage) ||
        !registry.contains(data.resourceId()) || registry.value(data.resourceId()).tombstone) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Auxiliary DEM resource is not registered or is tombstoned.");
        return false;
    }
    const AuxiliaryDemRegistryEntry entry = registry.value(data.resourceId());
    const QString expectedRaster = QDir(projectRoot).absoluteFilePath(entry.managedRasterPath);
    const QString expectedIdentity = QDir(projectRoot).absoluteFilePath(entry.managedIdentityH5Path);
    if (QDir::cleanPath(expectedRaster).compare(QDir::cleanPath(raster.absoluteFilePath()), Qt::CaseInsensitive) != 0 ||
        QDir::cleanPath(expectedIdentity).compare(QDir::cleanPath(identity.absoluteFilePath()), Qt::CaseInsensitive) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM entity paths are not the registry-managed paths.");
        return false;
    }
    const QByteArray rasterHash = sha256File(raster.absoluteFilePath());
    const QByteArray identityHash = sha256File(identity.absoluteFilePath());
    if (QString::fromLatin1(rasterHash) != entry.rasterHash ||
        QString::fromLatin1(identityHash) != entry.identityH5Hash) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::ContentIntegrityFailed);
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM managed file hash does not match the registry.");
        return false;
    }
    Hdf5Locker identityLock(identity.absoluteFilePath(), 50);
    if (!identityLock.isLocked()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot lock auxiliary DEM identity H5.");
        return false;
    }
    std::string identityDescriptorJson;
    if (Hdf5IO::readString(QDir::toNativeSeparators(identity.absoluteFilePath()).toLocal8Bit().constData(),
                           "semantic_product_descriptor", identityDescriptorJson) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM identity H5 lacks semantic_product_descriptor.");
        return false;
    }
    QString identityDescriptorError;
    const QtNodes::ProductDescriptor::Ptr identityDescriptor = QtNodes::ProductDescriptor::fromJson(
        QJsonDocument::fromJson(QByteArray::fromStdString(identityDescriptorJson)).object(), &identityDescriptorError);
    if (!identityDescriptor || identityDescriptor->productType() != QStringLiteral("auxiliary_terrain_dem")) {
        if (errorMessage) *errorMessage = identityDescriptorError.isEmpty()
            ? QStringLiteral("Auxiliary DEM identity H5 has an invalid product descriptor.") : identityDescriptorError;
        return false;
    }
    const QJsonObject metadata = entry.metadata;
    const bool metadataValid = metadata.value(QStringLiteral("schemaVersion")).toInt() >= 1 &&
        (metadata.value(QStringLiteral("crsWkt")).toString().contains(QStringLiteral("GEOGCS"), Qt::CaseInsensitive) ||
         metadata.value(QStringLiteral("crsWkt")).toString().contains(QStringLiteral("GEOGCRS"), Qt::CaseInsensitive) ||
         metadata.value(QStringLiteral("crsWkt")).toString().contains(QStringLiteral("PROJCS"), Qt::CaseInsensitive)) &&
        metadata.value(QStringLiteral("verticalDatum")).toString().isEmpty() == false &&
        metadata.value(QStringLiteral("maxLon")).toDouble() > metadata.value(QStringLiteral("minLon")).toDouble() &&
        metadata.value(QStringLiteral("maxLat")).toDouble() > metadata.value(QStringLiteral("minLat")).toDouble() &&
        metadata.value(QStringLiteral("resolutionX")).toDouble() > 0.0 &&
        metadata.value(QStringLiteral("resolutionY")).toDouble() > 0.0 &&
        QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(metadata).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()) == entry.canonicalMetadataHash;
    if (!metadataValid) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM registry geometry/CRS metadata is invalid.");
        return false;
    }
    if (!auxiliaryDemCoversInput(metadata, inputGeometry)) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM does not cover the trusted input geometry or match its CRS/resolution/vertical datum.");
        return false;
    }
    bool pinnedKnown = false;
    for (const QJsonValue& provenance : entry.provenanceHistory) {
        if (provenance.toObject().value(QStringLiteral("pinnedProvenanceId")).toString() == data.pinnedProvenanceId()) {
            pinnedKnown = true;
            break;
        }
    }
    if (!pinnedKnown) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::PinnedProvenanceMissing);
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM pinned provenance is not registered.");
        return false;
    }
    const QFileInfo entityManifest(QDir(projectRoot).absoluteFilePath(
        QStringLiteral(".dem_resources/%1/provenance_%2.json").arg(data.resourceId(), data.pinnedProvenanceId())));
    QFile manifestFile(entityManifest.absoluteFilePath());
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::PinnedProvenanceMissing);
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM provenance manifest is unavailable.");
        return false;
    }
    const QJsonObject manifestObject = QJsonDocument::fromJson(manifestFile.readAll()).object();
    if (manifestObject.value(QStringLiteral("resourceId")).toString() != data.resourceId() ||
        manifestObject.value(QStringLiteral("pinnedProvenanceId")).toString() != data.pinnedProvenanceId() ||
        manifestObject.value(QStringLiteral("runId")).toString().isEmpty() ||
        manifestObject.value(QStringLiteral("runId")).toString() != data.pinnedProvenanceId() ||
        entityManifest.fileName() != QStringLiteral("provenance_%1.json").arg(data.pinnedProvenanceId()) ||
        manifestObject.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("path")).toString() != QStringLiteral("dem.tif") ||
        manifestObject.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("path")).toString() != QStringLiteral("identity.h5") ||
        manifestObject.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("sha256")).toString() != entry.rasterHash ||
        manifestObject.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("sha256")).toString() != entry.identityH5Hash ||
        manifestObject.value(QStringLiteral("canonicalMetadataHash")).toString() != entry.canonicalMetadataHash) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::ContentIntegrityFailed);
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM provenance manifest does not match registry hashes.");
        return false;
    }
    binding.rasterPath = raster.absoluteFilePath();
    binding.identityH5Path = identity.absoluteFilePath();
    binding.resourceId = data.resourceId();
    binding.pinnedProvenanceId = data.pinnedProvenanceId();
    binding.rasterHash = entry.rasterHash;
    binding.identityH5Hash = entry.identityH5Hash;
    binding.canonicalMetadataHash = entry.canonicalMetadataHash;
    binding.fromReference = false;
    return true;
}

bool resolveAuxiliaryDemBinding(const QString& projectRoot,
                                const QtNodes::AuxiliaryDemReferenceData& data,
                                AuxiliaryDemBinding& binding,
                                QString* errorMessage,
                                const QJsonObject& inputGeometry)
{
    if (!data.isValid() || projectRoot.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM reference is incomplete.");
        return false;
    }
    if (data.pinnedProvenanceId().contains(QLatin1Char('/')) ||
        data.pinnedProvenanceId().contains(QLatin1Char('\\')) ||
        data.pinnedProvenanceId() == QStringLiteral(".") ||
        data.pinnedProvenanceId() == QStringLiteral("..")) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM pinned provenance identifier is unsafe.");
        return false;
    }
    QMap<QString, AuxiliaryDemRegistryEntry> registry;
    if (!loadAuxiliaryDemRegistry(projectRoot, registry, errorMessage) ||
        !registry.contains(data.resourceId()) || registry.value(data.resourceId()).tombstone) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Referenced DEM resource is missing or tombstoned.");
        return false;
    }
    const AuxiliaryDemRegistryEntry registryEntry = registry.value(data.resourceId());
    const QJsonObject metadata = registryEntry.metadata;
    if (metadata.value(QStringLiteral("schemaVersion")).toInt() < 1 ||
        metadata.value(QStringLiteral("crsWkt")).toString().isEmpty() ||
        metadata.value(QStringLiteral("verticalDatum")).toString().isEmpty() ||
        metadata.value(QStringLiteral("maxLon")).toDouble() <= metadata.value(QStringLiteral("minLon")).toDouble() ||
        metadata.value(QStringLiteral("maxLat")).toDouble() <= metadata.value(QStringLiteral("minLat")).toDouble() ||
        metadata.value(QStringLiteral("resolutionX")).toDouble() <= 0.0 ||
        metadata.value(QStringLiteral("resolutionY")).toDouble() <= 0.0 ||
        QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(metadata).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()) != registryEntry.canonicalMetadataHash) {
        if (errorMessage) *errorMessage = QStringLiteral("Referenced DEM registry metadata is incomplete or inconsistent.");
        return false;
    }
    if (!auxiliaryDemCoversInput(metadata, inputGeometry)) {
        if (errorMessage) *errorMessage = QStringLiteral("Referenced DEM does not cover the trusted input geometry or match its CRS/resolution/vertical datum.");
        return false;
    }
    bool pinnedKnown = false;
    for (const QJsonValue& provenance : registryEntry.provenanceHistory) {
        if (provenance.toObject().value(QStringLiteral("pinnedProvenanceId")).toString() == data.pinnedProvenanceId()) {
            pinnedKnown = true;
            break;
        }
    }
    if (!pinnedKnown) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::PinnedProvenanceMissing);
        if (errorMessage) *errorMessage = QStringLiteral("Pinned DEM provenance is not present in the registry history.");
        return false;
    }
    const QDir resourceDir(QDir(projectRoot).absoluteFilePath(
        QStringLiteral(".dem_resources/%1").arg(data.resourceId())));
    const QFileInfo raster(resourceDir.absoluteFilePath(QStringLiteral("dem.tif")));
    const QFileInfo identity(resourceDir.absoluteFilePath(QStringLiteral("identity.h5")));
    const QFileInfo manifest(resourceDir.absoluteFilePath(
        QStringLiteral("provenance_%1.json").arg(data.pinnedProvenanceId())));
    if (!manifest.isFile()) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::PinnedProvenanceMissing);
        if (errorMessage) *errorMessage = QStringLiteral("Referenced DEM pinned provenance manifest is unavailable.");
        return false;
    }
    if (!raster.isFile() || !identity.isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("Referenced DEM resource or pinned provenance is unavailable.");
        return false;
    }
    QFile manifestFile(manifest.absoluteFilePath());
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::PinnedProvenanceMissing);
        if (errorMessage) *errorMessage = QStringLiteral("Referenced DEM provenance manifest cannot be read.");
        return false;
    }
    const QJsonObject manifestObject = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const auto verifyManagedFile = [](const QFileInfo& file, const QJsonValue& value) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("path")).toString() != file.fileName()) return false;
        return QString::fromLatin1(sha256File(file.absoluteFilePath())) ==
            entry.value(QStringLiteral("sha256")).toString();
    };
    if (manifestObject.value(QStringLiteral("resourceId")).toString() != data.resourceId() ||
        manifestObject.value(QStringLiteral("pinnedProvenanceId")).toString() != data.pinnedProvenanceId() ||
        manifestObject.value(QStringLiteral("runId")).toString().isEmpty() ||
        manifestObject.value(QStringLiteral("runId")).toString() != data.pinnedProvenanceId() ||
        manifest.fileName() != QStringLiteral("provenance_%1.json").arg(data.pinnedProvenanceId()) ||
        manifestObject.value(QStringLiteral("canonicalMetadataHash")).toString() != registryEntry.canonicalMetadataHash ||
        !verifyManagedFile(raster, manifestObject.value(QStringLiteral("dem.tif"))) ||
        !verifyManagedFile(identity, manifestObject.value(QStringLiteral("identity.h5"))) ||
        manifestObject.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("sha256")).toString() != registryEntry.rasterHash ||
        manifestObject.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("sha256")).toString() != registryEntry.identityH5Hash) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::ContentIntegrityFailed);
        if (errorMessage) *errorMessage = QStringLiteral("DEM provenance manifest does not match managed files.");
        return false;
    }
    Hdf5Locker identityLock(identity.absoluteFilePath(), 50);
    if (!identityLock.isLocked()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot lock referenced DEM identity H5.");
        return false;
    }
    std::string identityDescriptorJson;
    if (Hdf5IO::readString(QDir::toNativeSeparators(identity.absoluteFilePath()).toLocal8Bit().constData(),
                           "semantic_product_descriptor", identityDescriptorJson) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Referenced DEM identity H5 lacks semantic_product_descriptor.");
        return false;
    }
    const QtNodes::ProductDescriptor::Ptr identityDescriptor = QtNodes::ProductDescriptor::fromJson(
        QJsonDocument::fromJson(QByteArray::fromStdString(identityDescriptorJson)).object());
    if (!identityDescriptor || identityDescriptor->productType() != QStringLiteral("auxiliary_terrain_dem")) {
        if (errorMessage) *errorMessage = QStringLiteral("Referenced DEM identity H5 descriptor is invalid.");
        return false;
    }
    binding.rasterPath = raster.absoluteFilePath();
    binding.identityH5Path = identity.absoluteFilePath();
    binding.resourceId = data.resourceId();
    binding.pinnedProvenanceId = data.pinnedProvenanceId();
    binding.rasterHash = registryEntry.rasterHash;
    binding.identityH5Hash = registryEntry.identityH5Hash;
    binding.canonicalMetadataHash = registryEntry.canonicalMetadataHash;
    binding.fromReference = true;
    return true;
}

bool revalidateAuxiliaryDemBinding(const QString& projectRoot,
                                   const QtNodes::AuxiliaryDemData* entity,
                                   const QtNodes::AuxiliaryDemReferenceData* reference,
                                   AuxiliaryDemBinding& binding,
                                   QString* errorMessage,
                                   const AuxiliaryDemBinding* expectedBinding,
                                   const QJsonObject& inputGeometry)
{
    if (!entity && !reference) {
        if (errorMessage) *errorMessage = QStringLiteral("No auxiliary DEM binding is active.");
        return false;
    }
    if (entity && reference &&
        (entity->resourceId() != reference->resourceId() ||
         entity->pinnedProvenanceId() != reference->pinnedProvenanceId())) {
        notifyResourceChange(entity->resourceId(), entity->pinnedProvenanceId(), ResourceChangeKind::ExplicitRebind);
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM entity/reference bindings changed independently.");
        return false;
    }
    const bool resolved = entity ? resolveAuxiliaryDemBinding(projectRoot, *entity, binding, errorMessage, inputGeometry)
                                 : resolveAuxiliaryDemBinding(projectRoot, *reference, binding, errorMessage, inputGeometry);
    if (!resolved || !expectedBinding) return resolved;
    if (binding.resourceId != expectedBinding->resourceId ||
        binding.pinnedProvenanceId != expectedBinding->pinnedProvenanceId ||
        binding.rasterHash != expectedBinding->rasterHash ||
         binding.identityH5Hash != expectedBinding->identityH5Hash ||
         binding.canonicalMetadataHash != expectedBinding->canonicalMetadataHash) {
        notifyResourceChange(binding.resourceId, binding.pinnedProvenanceId, ResourceChangeKind::ExplicitRebind);
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM binding changed after preparation.");
        return false;
    }
    return true;
}

bool revalidateDemExecutionSnapshot(const QString& projectRoot,
                                    const QtNodes::AuxiliaryDemData* entity,
                                    const QtNodes::AuxiliaryDemReferenceData* reference,
                                    const DemExecutionSnapshot& snapshot,
                                    const QJsonObject& currentInputGeometry,
                                    AuxiliaryDemBinding& binding,
                                    QString* errorMessage)
{
    if (!snapshot.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("Prepared DEM execution snapshot is incomplete.");
        return false;
    }
    if (currentInputGeometry != snapshot.inputGeometry) {
        if (errorMessage) *errorMessage = QStringLiteral("Input geometry changed after DEM preparation.");
        return false;
    }
    return revalidateAuxiliaryDemBinding(projectRoot, entity, reference, binding, errorMessage,
                                         &snapshot.binding, snapshot.inputGeometry);
}

bool resolveInsarDemProduct(const QtNodes::InsarDemData& data,
                            QStringList& h5Paths,
                            QString* errorMessage)
{
    if (!data.isExecutable()) {
        if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM is historical or incomplete and cannot execute.");
        return false;
    }
    const QtNodes::ProductDescriptor::Ptr descriptor = data.productDescriptor();
    if (!descriptor || descriptor->productType() != QStringLiteral("insar_dem") ||
        descriptor->state() != QtNodes::ProductState::Committed ||
        descriptor->provenance().value(QStringLiteral("runId")) != data.runId()) {
        if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM product descriptor/runId is invalid.");
        return false;
    }
    for (const QString& path : data.h5Paths()) {
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable()) {
            if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM H5 is missing or unreadable: %1").arg(path);
            return false;
        }
        QFile manifest(QDir(info.absolutePath()).absoluteFilePath(QStringLiteral(".node_output_manifest.json")));
        if (!manifest.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM committed manifest is missing: %1").arg(path);
            return false;
        }
        const QJsonObject manifestObject = QJsonDocument::fromJson(manifest.readAll()).object();
        if (manifestObject.value(QStringLiteral("runId")).toString() != data.runId()) {
            if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM manifest runId mismatch: %1").arg(path);
            return false;
        }
        bool listed = false;
        const QByteArray actualHash = sha256File(info.absoluteFilePath());
        for (const QJsonValue& output : manifestObject.value(QStringLiteral("outputs")).toArray()) {
            const QJsonObject item = output.toObject();
            if (item.value(QStringLiteral("name")).toString() == info.fileName() &&
                item.value(QStringLiteral("sha256")).toString() == QString::fromLatin1(actualHash)) {
                listed = true;
                break;
            }
        }
        if (!listed) {
            if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM H5 is not hash-bound by its committed manifest: %1").arg(path);
            return false;
        }
        Hdf5Locker h5Lock(info.absoluteFilePath(), 50);
        if (!h5Lock.isLocked()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot lock InSAR DEM H5: %1").arg(path);
            return false;
        }
        std::string storedDescriptor;
        if (Hdf5IO::readString(QDir::toNativeSeparators(info.absoluteFilePath()).toLocal8Bit().constData(),
                               "semantic_product_descriptor", storedDescriptor) != 0) {
            if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM H5 product descriptor is missing: %1").arg(path);
            return false;
        }
        QString h5DescriptorError;
        const QtNodes::ProductDescriptor::Ptr h5Descriptor = QtNodes::ProductDescriptor::fromJson(
            QJsonDocument::fromJson(QByteArray::fromStdString(storedDescriptor)).object(), &h5DescriptorError);
        if (!h5Descriptor || h5Descriptor->productType() != QStringLiteral("insar_dem") ||
            h5Descriptor->provenance().value(QStringLiteral("runId")) != data.runId()) {
            if (errorMessage) *errorMessage = h5DescriptorError.isEmpty()
                ? QStringLiteral("InSAR DEM H5 descriptor/runId mismatch: %1").arg(path) : h5DescriptorError;
            return false;
        }
        double minLon = 0.0, maxLon = 0.0, minLat = 0.0, maxLat = 0.0;
        if (!readScalarFromH5(path, QStringLiteral("dem_min_lon"), minLon) ||
            !readScalarFromH5(path, QStringLiteral("dem_max_lon"), maxLon) ||
            !readScalarFromH5(path, QStringLiteral("dem_min_lat"), minLat) ||
            !readScalarFromH5(path, QStringLiteral("dem_max_lat"), maxLat) ||
            !(maxLon > minLon && maxLat > minLat)) {
            if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM geometry metadata is missing or invalid: %1").arg(path);
            return false;
        }
    }
    h5Paths = data.h5Paths();
    return !h5Paths.isEmpty();
}

static QMutex g_hdf5GlobalMutex(QMutex::Recursive);
static QMutex g_fileLocksMapMutex(QMutex::Recursive);
static QMap<QString, std::shared_ptr<QMutex>> g_fileLocks;

namespace {

const char kProductDescriptorDataset[] = "semantic_product_descriptor";

bool writeProductDescriptorToH5(const QString& filePath, const QJsonObject& descriptor,
                                QString* errorMessage)
{
    const QByteArray path = filePath.toUtf8();
    const QByteArray json = QJsonDocument(descriptor).toJson(QJsonDocument::Compact);
    const int result = Hdf5IO::createString(path.constData(), kProductDescriptorDataset, json.constData());
    if (result != 0 && errorMessage) {
        *errorMessage = QStringLiteral("Cannot write staged H5 product descriptor: %1").arg(filePath);
    }
    return result == 0;
}

QtNodes::ProductDescriptor::Ptr readProductDescriptorFromH5(const QString& filePath,
                                                             QString* errorMessage)
{
    const QByteArray path = filePath.toUtf8();
    std::string storedDescriptor;
    if (Hdf5IO::readString(path.constData(), kProductDescriptorDataset, storedDescriptor) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot read H5 product descriptor: %1").arg(filePath);
        return QtNodes::ProductDescriptor::Ptr();
    }
    const QByteArray json = QByteArray::fromStdString(storedDescriptor);
    QString descriptorError;
    const QtNodes::ProductDescriptor::Ptr descriptor = QtNodes::ProductDescriptor::fromJson(
        QJsonDocument::fromJson(json).object(), &descriptorError);
    if (!descriptor && errorMessage) *errorMessage = descriptorError;
    return descriptor;
}

const char* const kTransactionDirectory = ".node_transactions";
const char* const kOutputManifestFile = ".node_output_manifest.json";

bool isDirectProjectChild(const QString& projectRoot, const QString& name,
                          QString* failureReason = nullptr);
bool writeJsonAtomically(const QString& path, const QJsonObject& object, QString* errorMessage);
QString transactionDirectoryPath(const QString& root);

bool isValidUtf8PathBytes(const std::string& value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    const size_t size = value.size();
    for (size_t i = 0; i < size;) {
        const unsigned char first = bytes[i];
        if (first == 0) return false;
        if (first <= 0x7f) {
            ++i;
            continue;
        }

        size_t length = 0;
        if (first >= 0xc2 && first <= 0xdf) length = 2;
        else if (first >= 0xe0 && first <= 0xef) length = 3;
        else if (first >= 0xf0 && first <= 0xf4) length = 4;
        else return false;
        if (i + length > size) return false;
        for (size_t offset = 1; offset < length; ++offset) {
            if ((bytes[i + offset] & 0xc0) != 0x80) return false;
        }
        if ((first == 0xe0 && bytes[i + 1] < 0xa0) ||
            (first == 0xed && bytes[i + 1] > 0x9f) ||
            (first == 0xf0 && bytes[i + 1] < 0x90) ||
            (first == 0xf4 && bytes[i + 1] > 0x8f)) {
            return false;
        }
        i += length;
    }
    return true;
}

enum class H5DatasetProbeResult
{
    Exists,
    Missing,
    Error
};

H5DatasetProbeResult probeH5Dataset(const QString& filePath,
                                    const QString& dataset,
                                    QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在：%1").arg(filePath);
        return H5DatasetProbeResult::Error;
    }

    cv::Mat value;
    Hdf5IO::Hdf5ReadDiagnostic diagnostic = {};
    const QByteArray utf8Path = filePath.toUtf8();
    const QByteArray utf8Dataset = dataset.toUtf8();
    const int result = Hdf5IO::readArrayDiagnosed(utf8Path.constData(), utf8Dataset.constData(),
                                                   value, &diagnostic);
    if (result == 0) {
        return H5DatasetProbeResult::Exists;
    }
    if (diagnostic.stage == Hdf5IO::HDF5_READ_STAGE_OPEN_DATASET && diagnostic.hdf5Status == 0) {
        return H5DatasetProbeResult::Missing;
    }

    if (errMsg) {
        const QString detail = QString::fromUtf8(diagnostic.errorStack).trimmed();
        *errMsg = detail.isEmpty()
            ? QStringLiteral("无法读取 H5 数据集 %1：%2 (stage=%3, status=%4)")
                  .arg(dataset, filePath).arg(diagnostic.stage).arg(diagnostic.hdf5Status)
            : QStringLiteral("无法读取 H5 数据集 %1：%2").arg(dataset, detail);
    }
    return H5DatasetProbeResult::Error;
}

bool isCompleteJpegFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() < 4) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray start = file.read(2);
    if (!file.seek(info.size() - 2)) {
        return false;
    }
    const QByteArray end = file.read(2);
    return start.size() == 2 && end.size() == 2 &&
        start[0] == static_cast<char>(0xff) && start[1] == static_cast<char>(0xd8) &&
        end[0] == static_cast<char>(0xff) && end[1] == static_cast<char>(0xd9);
}

bool validateGeoTiffOutput(const QFileInfo& info, QString* errorMessage)
{
    GDALAllRegister();
    const QByteArray nativePath = QDir::toNativeSeparators(info.absoluteFilePath()).toLocal8Bit();
    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(nativePath.constData(), GA_ReadOnly));
    if (!dataset) {
        if (errorMessage) *errorMessage = QStringLiteral("Staged GeoTIFF cannot be opened by GDAL: %1")
                                      .arg(info.absoluteFilePath());
        return false;
    }

    double geoTransform[6] = {};
    const char* projection = dataset->GetProjectionRef();
    const bool isGeoTiff = dataset->GetDriver() &&
        QString::fromLatin1(dataset->GetDriver()->GetDescription()) == QStringLiteral("GTiff");
    const bool hasValidGeoTransform = dataset->GetGeoTransform(geoTransform) == CE_None &&
        std::isfinite(geoTransform[0]) && std::isfinite(geoTransform[1]) &&
        std::isfinite(geoTransform[2]) && std::isfinite(geoTransform[3]) &&
        std::isfinite(geoTransform[4]) && std::isfinite(geoTransform[5]) &&
        (geoTransform[1] != 0.0 || geoTransform[2] != 0.0) &&
        (geoTransform[4] != 0.0 || geoTransform[5] != 0.0);
    const bool valid = isGeoTiff && dataset->GetRasterCount() > 0 &&
        dataset->GetRasterXSize() > 0 && dataset->GetRasterYSize() > 0 &&
        hasValidGeoTransform && projection && *projection;
    GDALClose(dataset);

    if (!valid && errorMessage) {
        *errorMessage = QStringLiteral("Staged GeoTIFF lacks a raster band, dimensions, or georeference: %1")
            .arg(info.absoluteFilePath());
    }
    return valid;
}

QString stageName(OutputTransaction::Stage stage)
{
    switch (stage) {
    case OutputTransaction::Stage::StagingCreatePrepared: return QStringLiteral("StagingCreatePrepared");
    case OutputTransaction::Stage::StagingPrepared: return QStringLiteral("StagingPrepared");
    case OutputTransaction::Stage::StagingValidated: return QStringLiteral("StagingValidated");
    case OutputTransaction::Stage::BackupMovePrepared: return QStringLiteral("BackupMovePrepared");
    case OutputTransaction::Stage::BackupMoved: return QStringLiteral("BackupMoved");
    case OutputTransaction::Stage::PromotionPrepared: return QStringLiteral("PromotionPrepared");
    case OutputTransaction::Stage::FinalPromoted: return QStringLiteral("FinalPromoted");
    case OutputTransaction::Stage::MetadataCommitPrepared: return QStringLiteral("MetadataCommitPrepared");
    case OutputTransaction::Stage::MetadataCommitted: return QStringLiteral("MetadataCommitted");
    case OutputTransaction::Stage::Completed: return QStringLiteral("Completed");
    case OutputTransaction::Stage::Failed: return QStringLiteral("Failed");
    default: return QStringLiteral("Inactive");
    }
}

bool journalNamesAreSafe(const QDir& root, const QString& nodeName, const QJsonObject& journal)
{
    const QString stagingName = journal.value(QStringLiteral("stagingDirectory")).toString();
    const QString backupName = journal.value(QStringLiteral("backupDirectory")).toString();
    const QString metadataXmlName = journal.value(QStringLiteral("metadataXmlName")).toString();
    const QString metadataBackupName = journal.value(QStringLiteral("metadataBackupName")).toString();
    const QString projectXmlPath = journal.value(QStringLiteral("projectXmlPath")).toString();
    const bool hasMetadataXmlName = !metadataXmlName.isEmpty();
    const bool hasMetadataBackupName = !metadataBackupName.isEmpty();
    const QFileInfo projectXmlInfo(projectXmlPath);
    const QString expectedProjectXmlPath = projectXmlInfo.canonicalFilePath();
    const bool projectXmlMatches = projectXmlPath.isEmpty()
        ? journal.value(QStringLiteral("version")).toInt() < 3
        : projectXmlInfo.isFile() &&
          QDir::cleanPath(projectXmlInfo.absolutePath()).compare(QDir::cleanPath(root.absolutePath()), Qt::CaseInsensitive) == 0 &&
          isDirectProjectChild(root.absolutePath(), projectXmlInfo.fileName()) &&
          !expectedProjectXmlPath.isEmpty() &&
          QDir::cleanPath(projectXmlPath).compare(QDir::cleanPath(expectedProjectXmlPath), Qt::CaseInsensitive) == 0;
    return journal.value(QStringLiteral("nodeName")).toString() == nodeName &&
           isDirectProjectChild(root.absolutePath(), nodeName) &&
           isDirectProjectChild(root.absolutePath(), stagingName) &&
           isDirectProjectChild(root.absolutePath(), backupName) &&
           stagingName.startsWith(QStringLiteral(".%1.staging-").arg(nodeName), Qt::CaseInsensitive) &&
           backupName.startsWith(QStringLiteral(".%1.backup-").arg(nodeName), Qt::CaseInsensitive) &&
           (!hasMetadataXmlName ||
            (isDirectProjectChild(root.absolutePath(), metadataXmlName) &&
             (projectXmlPath.isEmpty() || metadataXmlName == projectXmlInfo.fileName()))) &&
           (!hasMetadataBackupName ||
            (!metadataBackupName.contains('/') && !metadataBackupName.contains('\\') &&
             metadataBackupName.startsWith(QStringLiteral(".%1.metadata-backup-").arg(nodeName), Qt::CaseInsensitive))) &&
           projectXmlMatches;
}

bool updateJournalRecoveryState(const QString& journalPath, QJsonObject journal,
                                const QString& stage, const QString& action,
                                QString* errorMessage)
{
    journal.insert(QStringLiteral("stage"), stage);
    journal.insert(QStringLiteral("recoveryAction"), action);
    journal.insert(QStringLiteral("recoveredAtMs"),
                   static_cast<double>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()));
    return writeJsonAtomically(journalPath, journal, errorMessage);
}

bool isDirectProjectChild(const QString& projectRoot, const QString& name,
                          QString* failureReason)
{
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
        name.contains('/') || name.contains('\\')) {
        if (failureReason) *failureReason = QStringLiteral("node name is empty, reserved, or contains a path separator");
        return false;
    }
    const QDir root(projectRoot);
    const QString rootPath = QDir::cleanPath(QDir::fromNativeSeparators(root.absolutePath()));
    const QFileInfo rootInfo(rootPath);
    const QFileInfo candidate(root.absoluteFilePath(name));
    const QString candidatePath = QDir::cleanPath(QDir::fromNativeSeparators(candidate.absoluteFilePath()));
    const QString rootPrefix = rootPath.endsWith(QLatin1Char('/')) ? rootPath : rootPath + QLatin1Char('/');
    if (!candidatePath.startsWith(rootPrefix, Qt::CaseInsensitive)) {
        if (failureReason) *failureReason = QStringLiteral("candidate path is not a direct child of the project root");
        return false;
    }

    // Existing reparse points may redirect an apparently direct child outside
    // the project. New staging names do not exist yet and are safe once their
    // lexical target has passed the checks above.
    if (candidate.exists() && candidate.isSymLink()) {
        if (failureReason) *failureReason = QStringLiteral("existing output directory is a symbolic link");
        return false;
    }
    if (rootInfo.exists() && rootInfo.isSymLink()) {
        if (failureReason) *failureReason = QStringLiteral("project root is a symbolic link");
        return false;
    }
#ifdef Q_OS_WIN
    const auto isReparsePoint = [](const QString& path) {
        const DWORD attributes = ::GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    };
    if (candidate.exists() && isReparsePoint(candidatePath)) {
        if (failureReason) *failureReason = QStringLiteral("existing output directory is a Windows reparse point");
        return false;
    }
    if (rootInfo.exists() && isReparsePoint(rootPath)) {
        if (failureReason) *failureReason = QStringLiteral("project root is a Windows reparse point");
        return false;
    }
#endif
    return true;
}

bool writeJsonAtomically(const QString& path, const QJsonObject& object, QString* errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot open transaction record: %1").arg(path);
        return false;
    }
    const QByteArray serialized = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(serialized) != serialized.size() || !file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot commit transaction record: %1 (%2)")
                .arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

QJsonArray fingerprintInputs(const QStringList& paths)
{
    QJsonArray fingerprints;
    for (const QString& path : paths) {
        const QFileInfo requestedInfo(path);
        const QString absolutePath = requestedInfo.absoluteFilePath();
        QJsonObject item;
        const QFileInfo snapshot(absolutePath);
        item.insert(QStringLiteral("path"), QDir::cleanPath(snapshot.absoluteFilePath()));
        item.insert(QStringLiteral("size"), static_cast<double>(snapshot.isFile() ? snapshot.size() : -1));
        item.insert(QStringLiteral("modifiedMs"), static_cast<double>(
            snapshot.isFile() ? snapshot.lastModified().toMSecsSinceEpoch() : -1));
        fingerprints.append(item);
    }
    return fingerprints;
}

bool fingerprintsMatch(const QJsonArray& expected, const QStringList& paths)
{
    return QJsonDocument(expected).toJson(QJsonDocument::Compact) ==
           QJsonDocument(fingerprintInputs(paths)).toJson(QJsonDocument::Compact);
}

bool fingerprintsAreReadable(const QJsonArray& fingerprints, QString* errorMessage)
{
    for (const QJsonValue& value : fingerprints) {
        const QJsonObject fingerprint = value.toObject();
        if (fingerprint.value(QStringLiteral("path")).toString().isEmpty() ||
            fingerprint.value(QStringLiteral("size")).toDouble(-1.0) < 0.0 ||
            fingerprint.value(QStringLiteral("modifiedMs")).toDouble(-1.0) < 0.0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Cannot capture input file metadata for: %1")
                    .arg(fingerprint.value(QStringLiteral("path")).toString());
            }
            return false;
        }
    }
    return true;
}

bool validateOutputFile(const QFileInfo& info, QString* errorMessage)
{
    if (!info.isFile() || info.size() <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Missing or empty staged output: %1").arg(info.absoluteFilePath());
        return false;
    }
    const QString suffix = info.suffix();
    if (suffix.compare(QStringLiteral("tif"), Qt::CaseInsensitive) == 0 ||
        suffix.compare(QStringLiteral("tiff"), Qt::CaseInsensitive) == 0) {
        return validateGeoTiffOutput(info, errorMessage);
    }
    // Dataset-level validation remains node-specific because these nodes emit
    // different H5 products. The transaction layer verifies the complete,
    // non-empty candidate set without adding another HDF5 ABI dependency.
    return true;
}

bool validatePreviousCommittedFinal(const QDir& root, const QString& nodeName,
                                    const QJsonObject& interruptedJournal,
                                    QString* errorMessage)
{
    const QJsonObject committedJournal = interruptedJournal.value(QStringLiteral("previousCommittedJournal")).toObject();
    const QString committedRunId = committedJournal.value(QStringLiteral("runId")).toString();
    if (committedJournal.value(QStringLiteral("stage")).toString() != QStringLiteral("Completed") ||
        committedRunId.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Interrupted transaction has no previous completed journal.");
        return false;
    }

    const QString finalPath = root.absoluteFilePath(nodeName);
    QFile manifestFile(QDir(finalPath).absoluteFilePath(QString::fromLatin1(kOutputManifestFile)));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Previous committed output manifest is unavailable: %1")
            .arg(manifestFile.fileName());
        return false;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    if (manifest.value(QStringLiteral("nodeName")).toString() != nodeName ||
        manifest.value(QStringLiteral("runId")).toString() != committedRunId) {
        if (errorMessage) *errorMessage = QStringLiteral("Previous committed output manifest does not match its journal.");
        return false;
    }
    QString descriptorError;
    const QtNodes::ProductDescriptor::Ptr descriptor = QtNodes::ProductDescriptor::fromJson(
        manifest.value(QStringLiteral("productDescriptor")).toObject(), &descriptorError);
    if (!descriptor || descriptor->state() != QtNodes::ProductState::Committed) {
        if (errorMessage) *errorMessage = descriptorError.isEmpty()
            ? QStringLiteral("Previous committed output has no committed product descriptor.")
            : descriptorError;
        return false;
    }

    QSet<QString> expectedNames;
    for (const QJsonValue& value : committedJournal.value(QStringLiteral("expectedFiles")).toArray()) {
        const QString name = value.toString();
        if (name.isEmpty() || expectedNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Previous committed journal has an invalid output list.");
            return false;
        }
        expectedNames.insert(name);
    }
    QSet<QString> actualNames;
    for (const QJsonValue& value : manifest.value(QStringLiteral("outputs")).toArray()) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        const QFileInfo outputInfo(QDir(finalPath).absoluteFilePath(name));
        if (name.isEmpty() || !expectedNames.contains(name) || actualNames.contains(name) ||
            !validateOutputFile(outputInfo, errorMessage)) {
            return false;
        }
        if (nodeName.compare(QStringLiteral("Coregistration"), Qt::CaseInsensitive) == 0 &&
            outputInfo.suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0) {
            QString datasetError;
            if (probeH5Dataset(outputInfo.absoluteFilePath(), QStringLiteral("s_re"), &datasetError) != H5DatasetProbeResult::Exists ||
                probeH5Dataset(outputInfo.absoluteFilePath(), QStringLiteral("s_im"), &datasetError) != H5DatasetProbeResult::Exists) {
                if (errorMessage) *errorMessage = datasetError.isEmpty()
                    ? QStringLiteral("Previous Coregistration H5 output lacks s_re or s_im: %1").arg(outputInfo.fileName())
                    : datasetError;
                return false;
            }
        }
        actualNames.insert(name);
    }
    if (actualNames != expectedNames) {
        if (errorMessage) *errorMessage = QStringLiteral("Previous committed output manifest is incomplete.");
        return false;
    }

    const QString metadataName = committedJournal.value(QStringLiteral("metadataXmlName")).toString();
    if (!metadataName.isEmpty()) {
        TiXmlDocument document(QDir(root.absolutePath()).absoluteFilePath(metadataName).toLocal8Bit().constData());
        if (!document.LoadFile()) {
            if (errorMessage) *errorMessage = QStringLiteral("Previous committed project XML is unavailable: %1").arg(metadataName);
            return false;
        }
        TiXmlElement* xmlRoot = document.RootElement();
        TiXmlElement* dataNode = nullptr;
        for (TiXmlElement* element = xmlRoot ? xmlRoot->FirstChildElement() : nullptr;
             element; element = element->NextSiblingElement()) {
            if (element->Attribute("name") && nodeName == QString::fromUtf8(element->Attribute("name"))) {
                dataNode = element;
                break;
            }
        }
        if (!dataNode) {
            if (errorMessage) *errorMessage = QStringLiteral("Previous committed project XML has no node: %1").arg(nodeName);
            return false;
        }
        const auto normalizedProjectRelativePath = [](const QString& path) {
            QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
            if (!normalized.startsWith(QLatin1Char('/'))) normalized.prepend(QLatin1Char('/'));
            return normalized;
        };
        // The output manifest includes every transaction artifact (for
        // example JPG previews and the Apply Orbit reference marker), while
        // project XML only publishes primary H5 products.  Compare only the
        // latter here; a reference-only output deliberately points its XML
        // entries at the source H5 files instead of its marker file.
        QSet<QString> expectedXmlPaths;
        for (const QString& name : expectedNames) {
            if (QFileInfo(name).suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0) {
                expectedXmlPaths.insert(normalizedProjectRelativePath(
                    QStringLiteral("/%1/%2").arg(nodeName, name)));
            }
        }
        QSet<QString> xmlPaths;
        for (TiXmlElement* data = dataNode->FirstChildElement("Data"); data; data = data->NextSiblingElement("Data")) {
            TiXmlElement* path = data->FirstChildElement("Data_Path");
            if (path && path->GetText()) {
                xmlPaths.insert(normalizedProjectRelativePath(QString::fromUtf8(path->GetText())));
            }
        }
        if (!expectedXmlPaths.isEmpty() && xmlPaths != expectedXmlPaths) {
            if (errorMessage) *errorMessage = QStringLiteral("Previous committed project XML does not match output manifest.");
            return false;
        }
    }
    return true;
}

QString metadataBackupPath(const OutputTransaction& transaction)
{
    return QDir(transactionDirectoryPath(transaction.projectRoot)).absoluteFilePath(transaction.metadataBackupName);
}

QString metadataXmlPath(const OutputTransaction& transaction)
{
    if (!transaction.projectXmlPath.isEmpty()) return transaction.projectXmlPath;
    return QDir(transaction.projectRoot).absoluteFilePath(transaction.metadataXmlName);
}

bool restoreFileAtomically(const QString& sourcePath, const QString& targetPath, QString* errorMessage)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot open transaction XML backup: %1").arg(sourcePath);
        return false;
    }

    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot restore project XML: %1").arg(targetPath);
        return false;
    }

    while (!source.atEnd()) {
        const QByteArray block = source.read(64 * 1024);
        if (block.isEmpty() && source.error() != QFile::NoError) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot read transaction XML backup: %1").arg(sourcePath);
            return false;
        }
        if (!block.isEmpty() && target.write(block) != block.size()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot write restored project XML: %1").arg(targetPath);
            return false;
        }
    }
    if (!target.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot commit restored project XML: %1").arg(targetPath);
        return false;
    }
    return true;
}

bool restoreMetadataBackup(const OutputTransaction& transaction, XMLFile* xml, QString* errorMessage)
{
    if (!transaction.metadataBackupReady) return true;
    const QString backupPath = metadataBackupPath(transaction);
    const QString xmlPath = metadataXmlPath(transaction);
    const QFileInfo backupInfo(backupPath);
    if (!backupInfo.isFile() || !restoreFileAtomically(backupPath, xmlPath, errorMessage)) {
        return false;
    }
    if (xml) {
        const QByteArray nativePath = QDir::toNativeSeparators(xmlPath).toLocal8Bit();
        if (xml->XMLFile_load(nativePath.constData()) < 0) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot reload restored project XML: %1").arg(xmlPath);
            return false;
        }
    }
    {
        QMutexLocker projectXmlLocker(&g_projectXmlMutex);
        const QFileInfo restoredInfo(xmlPath);
        const QString canonicalXmlPath = QDir::cleanPath(restoredInfo.canonicalFilePath());
        rememberProjectXmlRevision(canonicalXmlPath, projectXmlRevision(canonicalXmlPath));
    }
    return true;
}

bool persistTransaction(OutputTransaction& transaction, QString* errorMessage)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), 3);
    object.insert(QStringLiteral("runId"), transaction.runId);
    object.insert(QStringLiteral("transactionId"), transaction.transactionId);
    object.insert(QStringLiteral("resourceAction"), transaction.resourceAction);
    object.insert(QStringLiteral("installedPath"), transaction.installedPath);
    object.insert(QStringLiteral("resourceStagingPath"), transaction.resourceStagingPath);
    object.insert(QStringLiteral("resourceRegistryBackupPath"), transaction.resourceRegistryBackupPath);
    object.insert(QStringLiteral("resourceRegistryBackupHash"), transaction.resourceRegistryBackupHash);
    object.insert(QStringLiteral("resourceRegistryCommittedHash"), transaction.resourceRegistryCommittedHash);
    object.insert(QStringLiteral("baseRegistryHash"), transaction.baseRegistryHash);
    object.insert(QStringLiteral("baseXmlHash"), transaction.baseXmlHash);
    object.insert(QStringLiteral("newMetadataHash"), transaction.newMetadataHash);
    object.insert(QStringLiteral("baseRegistryGeneration"), transaction.baseRegistryGeneration);
    object.insert(QStringLiteral("baseXmlGeneration"), transaction.baseXmlGeneration);
    object.insert(QStringLiteral("newRegistryGeneration"), transaction.newRegistryGeneration);
    object.insert(QStringLiteral("newMetadataGeneration"), transaction.newMetadataGeneration);
    object.insert(QStringLiteral("provenanceDelta"), transaction.provenanceDelta);
    object.insert(QStringLiteral("provenanceManifestPath"), transaction.provenanceManifestPath);
    object.insert(QStringLiteral("provenanceOnlyUpdate"), transaction.provenanceOnlyUpdate);
    object.insert(QStringLiteral("resourceRegistryMutationPrepared"), transaction.resourceRegistryMutationPrepared);
    object.insert(QStringLiteral("resourceRegistryCommitted"), transaction.resourceRegistryCommitted);
    QJsonArray dependencies;
    for (const QString& dependency : transaction.dependencyTransactions) dependencies.append(dependency);
    object.insert(QStringLiteral("dependencyTransactions"), dependencies);
    object.insert(QStringLiteral("executionRevision"), static_cast<double>(transaction.executionRevision));
    object.insert(QStringLiteral("nodeName"), transaction.nodeName);
    object.insert(QStringLiteral("projectXmlPath"), transaction.projectXmlPath);
    object.insert(QStringLiteral("stage"), stageName(transaction.stage));
    object.insert(QStringLiteral("finalDirectory"), transaction.nodeName);
    object.insert(QStringLiteral("stagingDirectory"), transaction.stagingName);
    object.insert(QStringLiteral("backupDirectory"), transaction.backupName);
    object.insert(QStringLiteral("metadataXmlName"), transaction.metadataXmlName);
    object.insert(QStringLiteral("metadataBackupName"), transaction.metadataBackupName);
    object.insert(QStringLiteral("metadataBackupReady"), transaction.metadataBackupReady);
    object.insert(QStringLiteral("hasPreviousFinal"), transaction.hasPreviousFinal);
    object.insert(QStringLiteral("backupCleanupDeferred"), transaction.backupCleanupDeferred);
    object.insert(QStringLiteral("previousFinalState"), transaction.hasPreviousFinal
        ? QStringLiteral("Present") : QStringLiteral("NoPreviousFinal"));
    if (!transaction.previousFinalManifest.isEmpty()) {
        object.insert(QStringLiteral("previousFinalManifest"), transaction.previousFinalManifest);
    }
    if (!transaction.previousCommittedJournal.isEmpty()) {
        object.insert(QStringLiteral("previousCommittedJournal"), transaction.previousCommittedJournal);
    }
    object.insert(QStringLiteral("inputs"), transaction.inputFingerprints);
    if (!transaction.productDescriptor.isEmpty()) {
        object.insert(QStringLiteral("productDescriptor"), transaction.productDescriptor);
    }
    QJsonArray files;
    for (const QString& name : transaction.expectedFileNames) files.append(name);
    object.insert(QStringLiteral("expectedFiles"), files);
    return writeJsonAtomically(transaction.journalPath, object, errorMessage);
}

bool setTransactionStage(OutputTransaction& transaction, OutputTransaction::Stage stage, QString* errorMessage)
{
    const OutputTransaction::Stage previousStage = transaction.stage;
    transaction.stage = stage;
    if (persistTransaction(transaction, errorMessage)) return true;
    transaction.stage = previousStage;
    return false;
}

QString transactionDirectoryPath(const QString& root)
{
    return QDir(root).absoluteFilePath(QString::fromLatin1(kTransactionDirectory));
}

} // namespace

bool persistOutputTransactionState(OutputTransaction& transaction, QString* errorMessage)
{
    return persistTransaction(transaction, errorMessage);
}

QJsonArray fingerprintInputPaths(const QStringList& paths)
{
    return fingerprintInputs(paths);
}

QMutex* getHdf5Mutex()
{
    return &g_hdf5GlobalMutex;
}

// 获取或创建特定文件的局部锁
static QMutex* getMutexForFile(const QString& filePath)
{
    if (filePath.isEmpty()) return &g_hdf5GlobalMutex;
    
    // 路径标准化：消除斜杠差异、统一小写，避免物理上的同一文件因路径写法差异造成锁失效
    QString normPath = QDir::toNativeSeparators(filePath).toLower();
    
    QMutexLocker mapLocker(&g_fileLocksMapMutex);
    if (!g_fileLocks.contains(normPath)) {
        g_fileLocks[normPath] = std::make_shared<QMutex>(QMutex::Recursive);
    }
    return g_fileLocks[normPath].get();
}

Hdf5Locker::Hdf5Locker()
    : m_mutex(&g_hdf5GlobalMutex)
    , m_isLocked(false)
{
    m_mutex->lock();
    m_isLocked = true;
}

Hdf5Locker::Hdf5Locker(const QString& filePath, int timeoutMs)
    : m_isLocked(false)
{
    m_mutex = getMutexForFile(filePath);
    if (timeoutMs < 0) {
        m_mutex->lock();
        m_isLocked = true;
    } else {
        m_isLocked = m_mutex->tryLock(timeoutMs);
    }
}

Hdf5Locker::Hdf5Locker(const std::string& filePath, int timeoutMs)
    : m_isLocked(false)
{
    m_mutex = getMutexForFile(QString::fromStdString(filePath));
    if (timeoutMs < 0) {
        m_mutex->lock();
        m_isLocked = true;
    } else {
        m_isLocked = m_mutex->tryLock(timeoutMs);
    }
}

Hdf5Locker::~Hdf5Locker()
{
    if (m_isLocked && m_mutex) {
        m_mutex->unlock();
    }
}

IApplicationInterface* getProjectContext(QWidget* widget)
{
    // 1. Try parent widget traversal
    if (widget)
    {
        QWidget* parent = widget->parentWidget();
        while (parent)
        {
            auto* iface = dynamic_cast<IApplicationInterface*>(parent);
            if (iface) {
                return iface;
            }
            parent = parent->parentWidget();
        }
    }

    // 2. Fallback to MainWindow -> workspaceUI
    foreach(QWidget * topLevelWidget, QApplication::topLevelWidgets()) {
        MainWindow* mainWin = qobject_cast<MainWindow*>(topLevelWidget);
        if (mainWin) {
            // Prefer workspaceUI as it's the source of truth for project data
            if (mainWin->workspaceUI()) {
                return mainWin->workspaceUI();
            }
            // Fallback to interface manager's current interface
            if (mainWin->interfaceManager()) {
                auto* iface = mainWin->interfaceManager()->currentInterface();
                if (iface) {
                    return iface;
                }
            }
        }
    }

    return nullptr;
}

void removeDataNodeFromProjectTree(IApplicationInterface* iface, const QString& nodeName)
{
    if (!iface || nodeName.isEmpty()) return;
    QStandardItemModel* model = iface->projectModel();
    const QString projectName = iface->projectName();
    if (!model || projectName.isEmpty()) return;

    QList<QStandardItem*> projectItems = model->findItems(projectName);
    if (projectItems.isEmpty()) {
        for (int row = 0; row < model->rowCount(); ++row) {
            if (QStandardItem* item = model->item(row, 0)) projectItems.append(item);
        }
    }
    for (QStandardItem* projectItem : projectItems) {
        for (int row = projectItem->rowCount() - 1; row >= 0; --row) {
            QStandardItem* nodeItem = projectItem->child(row, 0);
            if (nodeItem && nodeItem->text() == nodeName) {
                projectItem->removeRow(row);
            }
        }
    }
}

void removeDataNodeFromProject(IApplicationInterface* iface, const QString& oldNodeName,
                                      bool saveXmlImmediately,
                                      bool updateTreeImmediately)
{
    if (!iface || oldNodeName.isEmpty()) return;

    QStandardItemModel* model = iface->projectModel();
    QString projPath = iface->projectPath();
    QString projName = iface->projectName();

    if (projPath.isEmpty() || projName.isEmpty()) return;

    // projectPath() from ImportNodeBase returns QFileInfo(fullPath).absolutePath()
    // i.e. the directory containing the .Insar file.
    // projName is the .Insar filename (e.g. "myproject.Insar")
    // XML path = projPath + "/" + projName  (but projPath may already be the dir)

    if (updateTreeImmediately && model) {
        removeDataNodeFromProjectTree(iface, oldNodeName);
    }

    // 同时从 XML 文件中移除
    // 确定 XML 文件路径。IApplicationInterface 的 projectPath()
    // 返回 .Insar 文件的全路径 (例如 "D:/projects/test.Insar")
    QString xmlPath = projPath;
    // 如果 projPath 不以 projName 结尾，则构建它
    if (!xmlPath.endsWith(projName)) {
        // projPath 为目录，projName 为文件名
        xmlPath = projPath + "/" + projName;
    }

    // 优先获取并修改全局内存中的 XMLFile 实例，确保内存与磁盘实时同步
    XMLFile* xml = iface->projectXml();
    bool isGlobalXml = (xml != nullptr);
    XMLFile localXml;

    if (!isGlobalXml) {
        if (localXml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
            return;
        }
        xml = &localXml;
    }

    // 查找并移除匹配名称的 DataNode 元素
    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (root) {
        for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; ) {
            const char* nameAttr = p->Attribute("name");
            if (nameAttr && QString(nameAttr) == oldNodeName) {
                TiXmlElement* toDelete = p;
                p = p->NextSiblingElement();
                root->RemoveChild(toDelete);
            } else {
                p = p->NextSiblingElement();
            }
        }
    }
    if (saveXmlImmediately) {
        QString saveError;
        if (!saveProjectXmlAtomically(xml, xmlPath, &saveError)) {
            InSARLogManager::LogError("NodeUtils", QStringLiteral("删除数据节点后保存工程 XML 失败：%1").arg(saveError));
        }
    }
}

bool addOriginNodeToProjectXml(IApplicationInterface* iface,
                               const QString& nodeName,
                               const QString& displayName,
                               const QString& relativePath,
                               const QString& tag)
{
    if (!iface || nodeName.isEmpty()) return false;

    QString projPath = iface->projectPath();
    QString projName = iface->projectName();
    if (projPath.isEmpty() || projName.isEmpty()) return false;

    QString xmlPath = projPath;
    if (!xmlPath.endsWith(projName)) {
        xmlPath = projPath + "/" + projName;
    }

    XMLFile* xml = iface->projectXml();
    bool isGlobalXml = (xml != nullptr);
    XMLFile localXml;

    if (!isGlobalXml) {
        if (localXml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
            return false;
        }
        xml = &localXml;
    }

    int ret = xml->XMLFile_add_origin(
        nodeName.toStdString().c_str(),
        displayName.toStdString().c_str(),
        relativePath.toStdString().c_str(),
        tag.toStdString().c_str()
    );

    if (ret >= 0) {
        QString saveError;
        return saveProjectXmlAtomically(xml, xmlPath, &saveError);
    }
    return false;
}

bool addSBASNodeToProjectXml(IApplicationInterface* iface,
                             const QString& nodeName,
                             const QString& dataName,
                             const QString& relativePath)
{
    if (!iface || nodeName.isEmpty()) return false;

    QString projPath = iface->projectPath();
    QString projName = iface->projectName();
    if (projPath.isEmpty() || projName.isEmpty()) return false;

    QString xmlPath = projPath;
    if (!xmlPath.endsWith(projName)) {
        xmlPath = projPath + "/" + projName;
    }

    XMLFile* xml = iface->projectXml();
    bool isGlobalXml = (xml != nullptr);
    XMLFile localXml;

    if (!isGlobalXml) {
        if (localXml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
            return false;
        }
        xml = &localXml;
    }

    int ret = xml->XMLFile_add_SBAS(
        nodeName.toStdString().c_str(),
        dataName.toStdString().c_str(),
        relativePath.toStdString().c_str()
    );

    if (ret >= 0) {
        QString saveError;
        return saveProjectXmlAtomically(xml, xmlPath, &saveError);
    }
    return false;
}

OverwriteResult checkAndPromptOverwrite(IApplicationInterface* iface, const QString& nodeName, const QStringList& filePaths, QWidget* parent)
{
    bool hasConflict = false;
    QStringList conflictDetails;

    // 1. Check if node exists in the project tree
    if (iface && !nodeName.isEmpty()) {
        QStandardItemModel* model = iface->projectModel();
        QString projName = iface->projectName();
        if (model && !projName.isEmpty()) {
            QList<QStandardItem*> projItems = model->findItems(projName);
            if (projItems.isEmpty()) {
                for (int r = 0; r < model->rowCount(); ++r) {
                    QStandardItem* item = model->item(r, 0);
                    if (item) projItems.append(item);
                }
            }
            for (QStandardItem* projItem : projItems) {
                for (int i = 0; i < projItem->rowCount(); ++i) {
                    QStandardItem* nodeItem = projItem->child(i, 0);
                    if (nodeItem && nodeItem->text() == nodeName) {
                        hasConflict = true;
                        conflictDetails.append("- 已存在同名节点: " + nodeName);
                        break;
                    }
                }
                if (hasConflict) break;
            }
        }
    }

    // 2. Check if physical files exist
    QStringList existingFiles;
    bool allFilesExist = !filePaths.isEmpty();
    for (const QString& path : filePaths) {
        if (QFile::exists(path)) {
            existingFiles.append(QFileInfo(path).fileName());
        } else {
            allFilesExist = false;
        }
    }
    
    if (!existingFiles.isEmpty()) {
        existingFiles.removeDuplicates();
        hasConflict = true;
        conflictDetails.append("- 已存在同名文件:\n    " + existingFiles.join("\n    "));
    }

    // 3. Prompt user if conflicts were found
    if (hasConflict) {
        QMessageBox msgBox(parent);
        msgBox.setWindowTitle("冲突处理");
        msgBox.setIcon(QMessageBox::Warning);
        
        QString msg = "检测到冲突：\n\n" + conflictDetails.join("\n\n") + 
                      "\n\n请选择后续操作：\n";
        msgBox.setText(msg);

        QPushButton* overwriteBtn = msgBox.addButton("重新运行并覆盖", QMessageBox::AcceptRole);
        QPushButton* loadBtn = nullptr;
        if (allFilesExist) {
            loadBtn = msgBox.addButton("加载已存在文件", QMessageBox::AcceptRole);
        }
        QPushButton* cancelBtn = msgBox.addButton("取消", QMessageBox::RejectRole);

        msgBox.setDefaultButton(cancelBtn);
        msgBox.exec();

        if (msgBox.clickedButton() == overwriteBtn) {
            return OverwriteResult::Overwrite;
        } else if (loadBtn && msgBox.clickedButton() == loadBtn) {
            return OverwriteResult::LoadExisting;
        } else {
            return OverwriteResult::Cancel;
        }
    }

    return OverwriteResult::NoConflict;
}

bool recoverOutputTransaction(const QString& projectRoot, const QString& nodeName, QString* errorMessage)
{
    QDir root(projectRoot);
    if (!root.exists() || !isDirectProjectChild(root.absolutePath(), nodeName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid output transaction recovery target.");
        return false;
    }
    const QString journalPath = QDir(transactionDirectoryPath(root.absolutePath()))
        .absoluteFilePath(nodeName + QStringLiteral(".json"));
    if (!QFileInfo::exists(journalPath)) return true;

    QFile journalFile(journalPath);
    if (!journalFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot inspect interrupted output transaction: %1").arg(journalPath);
        return false;
    }
    const QJsonObject journal = QJsonDocument::fromJson(journalFile.readAll()).object();
    journalFile.close();
    if (!journalNamesAreSafe(root, nodeName, journal)) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction journal has unsafe paths and was left untouched: %1").arg(journalPath);
        return false;
    }
    const QString stage = journal.value(QStringLiteral("stage")).toString();
    const auto dependenciesAreRecoverable = [&]() {
        const QJsonObject previous = journal.value(QStringLiteral("previousCommittedJournal")).toObject();
        const QString previousId = previous.value(QStringLiteral("transactionId")).toString();
        const QDir transactionDir(transactionDirectoryPath(root.absolutePath()));
        for (const QJsonValue& dependencyValue : journal.value(QStringLiteral("dependencyTransactions")).toArray()) {
            const QString dependencyId = dependencyValue.toString();
            if (dependencyId.isEmpty()) return false;
            if (dependencyId == previousId && previous.value(QStringLiteral("stage")).toString() == QStringLiteral("Completed")) {
                continue;
            }
            bool found = false;
            for (const QFileInfo& info : transactionDir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files)) {
                QFile dependencyFile(info.absoluteFilePath());
                if (!dependencyFile.open(QIODevice::ReadOnly)) continue;
                const QJsonObject dependency = QJsonDocument::fromJson(dependencyFile.readAll()).object();
                if (dependency.value(QStringLiteral("transactionId")).toString() == dependencyId &&
                    dependency.value(QStringLiteral("stage")).toString() == QStringLiteral("Completed")) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    };
    // A terminal journal is self-contained: its manifest and current journal
    // are the recovery authority. Historical dependencies are only required
    // while resolving an interrupted promotion/metadata transition.
    if (stage != QStringLiteral("Completed") && stage != QStringLiteral("Failed") &&
        !dependenciesAreRecoverable()) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction dependency journal is missing or incomplete; recovery was left isolated.");
        return false;
    }

    const QString stagingName = journal.value(QStringLiteral("stagingDirectory")).toString();
    const QString backupName = journal.value(QStringLiteral("backupDirectory")).toString();
    const bool hasPreviousFinal = journal.value(QStringLiteral("hasPreviousFinal")).toBool(false);
    const QString finalPath = root.absoluteFilePath(nodeName);
    const QString stagingPath = root.absoluteFilePath(stagingName);
    const QString backupPath = root.absoluteFilePath(backupName);
    const auto rollbackResourceDelta = [&]() -> bool {
        const QString action = journal.value(QStringLiteral("resourceAction")).toString();
        const QString resourcePath = QDir::cleanPath(journal.value(QStringLiteral("installedPath")).toString());
        const QString resourcesRoot = QDir::cleanPath(root.absoluteFilePath(QStringLiteral(".dem_resources")));
        bool ok = true;
        if (action == QStringLiteral("installed") && !resourcePath.isEmpty() &&
            (resourcePath == resourcesRoot || resourcePath.startsWith(resourcesRoot + QDir::separator())) &&
            QDir(resourcePath).exists()) {
            ok = QDir(resourcePath).removeRecursively() && ok;
        }
        const QString resourceStagingPath = QDir::cleanPath(journal.value(QStringLiteral("resourceStagingPath")).toString());
        if (!resourceStagingPath.isEmpty() &&
            resourceStagingPath.startsWith(resourcesRoot + QDir::separator()) &&
            QDir(resourceStagingPath).exists()) {
            ok = QDir(resourceStagingPath).removeRecursively() && ok;
        }
        const QString manifestPath = QDir::cleanPath(journal.value(QStringLiteral("provenanceManifestPath")).toString());
        if (action == QStringLiteral("reused") && !manifestPath.isEmpty() &&
            manifestPath.startsWith(resourcesRoot + QDir::separator()) && QFileInfo::exists(manifestPath)) {
            ok = QFile::remove(manifestPath) && ok;
        }
        const QString backupRegistry = QDir::cleanPath(journal.value(QStringLiteral("resourceRegistryBackupPath")).toString());
        const QString registryPath = QDir::cleanPath(root.absoluteFilePath(QStringLiteral(".dem_resource_registry.json")));
        if (!backupRegistry.isEmpty() && QFileInfo::exists(backupRegistry) &&
            backupRegistry.startsWith(registryPath + QStringLiteral(".backup-"))) {
            const QString committedHash = journal.value(QStringLiteral("resourceRegistryCommittedHash")).toString();
            const QString committedGeneration = journal.value(QStringLiteral("newRegistryGeneration")).toString();
            if ((!committedHash.isEmpty() && QString::fromLatin1(sha256File(registryPath)) != committedHash) ||
                (!committedGeneration.isEmpty() && fileGeneration(registryPath) != committedGeneration)) {
                ok = false;
            } else {
                QFile::remove(registryPath);
                ok = QFile::copy(backupRegistry, registryPath) && ok;
                if (ok) QFile::remove(backupRegistry);
            }
        } else if ((journal.value(QStringLiteral("resourceRegistryCommitted")).toBool(false) ||
                    journal.value(QStringLiteral("resourceRegistryMutationPrepared")).toBool(false)) &&
                   (action == QStringLiteral("installed") || action == QStringLiteral("reused"))) {
            const QString resourceId = journal.value(QStringLiteral("provenanceDelta")).toObject()
                .value(QStringLiteral("resourceId")).toString();
            if (!resourceId.isEmpty()) {
                QString ignored;
                ok = removeAuxiliaryDemRegistryEntry(root.absolutePath(), resourceId, &ignored) && ok;
                if (ok && journal.value(QStringLiteral("baseRegistryHash")).toString().isEmpty() &&
                    QFileInfo::exists(registryPath)) {
                    ok = QFile::remove(registryPath) && ok;
                }
            }
        }
        return ok;
    };
    const auto removeStaging = [&]() {
        return !QDir(stagingPath).exists() || QDir(stagingPath).removeRecursively();
    };
    const auto markFailed = [&](const QString& action) {
        return updateJournalRecoveryState(journalPath, journal, QStringLiteral("Failed"), action, errorMessage);
    };
    const auto restorePreviousCommittedJournal = [&]() {
        const QJsonObject previous = journal.value(QStringLiteral("previousCommittedJournal")).toObject();
        if (previous.isEmpty()) return false;
        if (!journalNamesAreSafe(root, nodeName, previous)) {
            if (errorMessage) *errorMessage = QStringLiteral("Interrupted transaction has an unsafe previous committed journal.");
            return false;
        }
        return writeJsonAtomically(journalPath, previous, errorMessage);
    };

    if (stage == QStringLiteral("Completed") || stage == QStringLiteral("Failed")) return true;
    if (stage != QStringLiteral("MetadataCommitted") && !rollbackResourceDelta()) {
        if (errorMessage) *errorMessage = QStringLiteral("Interrupted DEM resource transaction could not be rolled back safely.");
        return false;
    }

    if (stage == QStringLiteral("MetadataCommitted")) {
        const QString metadataXmlName = journal.value(QStringLiteral("metadataXmlName")).toString();
        const QString newMetadataHash = journal.value(QStringLiteral("newMetadataHash")).toString();
        const QString newMetadataGeneration = journal.value(QStringLiteral("newMetadataGeneration")).toString();
        const QString metadataXmlPath = root.absoluteFilePath(metadataXmlName);
        if (metadataXmlName.isEmpty() || newMetadataHash.isEmpty() ||
            QString::fromLatin1(sha256File(metadataXmlPath)) != newMetadataHash ||
            (!newMetadataGeneration.isEmpty() && fileGeneration(metadataXmlPath) != newMetadataGeneration)) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed project XML no longer matches the output transaction journal.");
            return false;
        }
        const QString committedRegistryHash = journal.value(QStringLiteral("resourceRegistryCommittedHash")).toString();
        const QString committedRegistryGeneration = journal.value(QStringLiteral("newRegistryGeneration")).toString();
        const QString registryPath = root.absoluteFilePath(QStringLiteral(".dem_resource_registry.json"));
        if ((!committedRegistryHash.isEmpty() &&
             QString::fromLatin1(sha256File(registryPath)) != committedRegistryHash) ||
            (!committedRegistryGeneration.isEmpty() &&
             fileGeneration(registryPath) != committedRegistryGeneration)) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed DEM resource registry no longer matches the output transaction journal.");
            return false;
        }
        QFile manifestFile(QDir(finalPath).absoluteFilePath(QString::fromLatin1(kOutputManifestFile)));
        if (!manifestFile.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest is unavailable during recovery.");
            return false;
        }
        const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        if (manifest.value(QStringLiteral("nodeName")).toString() != nodeName ||
            manifest.value(QStringLiteral("runId")).toString() != journal.value(QStringLiteral("runId")).toString()) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest does not match the interrupted transaction.");
            return false;
        }
        QSet<QString> expectedNames;
        for (const QJsonValue& value : journal.value(QStringLiteral("expectedFiles")).toArray()) {
            const QString name = value.toString();
            if (name.isEmpty() || expectedNames.contains(name)) {
                if (errorMessage) *errorMessage = QStringLiteral("Committed transaction has an invalid output manifest contract.");
                return false;
            }
            expectedNames.insert(name);
        }
        const QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
        QSet<QString> actualNames;
        for (const QJsonValue& value : outputs) {
            const QString name = value.toObject().value(QStringLiteral("name")).toString();
            const QFileInfo outputInfo(QDir(finalPath).absoluteFilePath(name));
            if (name.isEmpty() || !expectedNames.contains(name) || actualNames.contains(name) ||
                !validateOutputFile(outputInfo, errorMessage)) {
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("Committed output manifest is invalid during recovery.");
                }
                return false;
            }
            actualNames.insert(name);
        }
        if (actualNames != expectedNames) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest is incomplete during recovery.");
            return false;
        }
        QJsonObject completedJournal = journal;
        completedJournal.remove(QStringLiteral("previousCommittedJournal"));
        if (QDir(backupPath).exists() && !QDir(backupPath).removeRecursively()) {
            completedJournal.insert(QStringLiteral("backupCleanupDeferred"), true);
            InSARLogManager::LogWarning("NodeUtils", QString("Deferred interrupted-transaction backup cleanup: %1").arg(backupName));
        }
        const QString resourceRegistryBackup = journal.value(QStringLiteral("resourceRegistryBackupPath")).toString();
        if (!resourceRegistryBackup.isEmpty() && QFileInfo::exists(resourceRegistryBackup) &&
            !QFile::remove(resourceRegistryBackup)) {
            completedJournal.insert(QStringLiteral("resourceRegistryBackupCleanupDeferred"), true);
        }
        return updateJournalRecoveryState(journalPath, completedJournal, QStringLiteral("Completed"),
                                          QStringLiteral("metadata already committed"), errorMessage);
    }

    if (stage == QStringLiteral("StagingCreatePrepared")) {
        if (QDir(backupPath).exists()) {
            if (errorMessage) *errorMessage = QStringLiteral("Staging creation recovery found an unexpected backup directory.");
            return false;
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove interrupted staging directory: %1").arg(stagingPath);
            return false;
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("discarded interrupted staging creation"));
    }

    if (stage == QStringLiteral("StagingPrepared") || stage == QStringLiteral("StagingValidated")) {
        if ((!hasPreviousFinal && QDir(finalPath).exists()) ||
            (hasPreviousFinal && QDir(backupPath).exists())) {
            if (errorMessage) *errorMessage = QStringLiteral("Staging recovery found an unexpected final or backup directory.");
            return false;
        }
        if (hasPreviousFinal && !validatePreviousCommittedFinal(root, nodeName, journal, errorMessage)) {
            return false;
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove isolated staging directory: %1").arg(stagingPath);
            return false;
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("discarded unpromoted staging output"));
    }

    if (stage == QStringLiteral("BackupMovePrepared") || stage == QStringLiteral("BackupMoved")) {
        if ((!hasPreviousFinal && (QDir(finalPath).exists() || QDir(backupPath).exists())) ||
            (hasPreviousFinal && QDir(finalPath).exists() && QDir(backupPath).exists())) {
            if (errorMessage) *errorMessage = QStringLiteral("Backup recovery found an ambiguous final/backup directory combination.");
            return false;
        }
        if (hasPreviousFinal && !QDir(finalPath).exists() && QDir(backupPath).exists() &&
            !root.rename(backupName, nodeName)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot restore previous final directory from backup.");
            return false;
        }
        if (!QDir(finalPath).exists() && hasPreviousFinal) {
            if (errorMessage) *errorMessage = QStringLiteral("Interrupted backup move cannot be proven or restored.");
            return false;
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove staging directory after backup recovery.");
            return false;
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("restored or retained previous final before promotion"));
    }

    if (stage == QStringLiteral("PromotionPrepared") || stage == QStringLiteral("FinalPromoted")) {
        const bool finalExists = QDir(finalPath).exists();
        const bool stagingExists = QDir(stagingPath).exists();
        if (hasPreviousFinal) {
            if (!QDir(backupPath).exists()) {
                if (errorMessage) *errorMessage = QStringLiteral("Promotion recovery is ambiguous because the previous backup is missing.");
                return false;
            }
            if (finalExists && !stagingExists && !root.rename(nodeName, stagingName)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot isolate promoted candidate during rollback.");
                return false;
            }
            if (!QDir(finalPath).exists() && !root.rename(backupName, nodeName)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot restore previous final after interrupted promotion.");
                return false;
            }
        } else if (finalExists && !stagingExists) {
            if (!QDir(finalPath).removeRecursively()) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot remove uncommitted promoted candidate.");
                return false;
            }
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove isolated candidate after promotion recovery.");
            return false;
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("rolled back promoted output before metadata commit"));
    }

    if (stage == QStringLiteral("MetadataCommitPrepared")) {
        OutputTransaction transaction;
        transaction.projectRoot = root.absolutePath();
        transaction.nodeName = nodeName;
        transaction.projectXmlPath = QDir::cleanPath(journal.value(QStringLiteral("projectXmlPath")).toString());
        transaction.metadataXmlName = journal.value(QStringLiteral("metadataXmlName")).toString();
        transaction.metadataBackupName = journal.value(QStringLiteral("metadataBackupName")).toString();
        transaction.metadataBackupReady = journal.value(QStringLiteral("metadataBackupReady")).toBool(false);
        const QString expectedXmlPath = QFileInfo(root.absoluteFilePath(transaction.metadataXmlName)).canonicalFilePath();
        if (transaction.metadataXmlName.isEmpty() || transaction.metadataBackupName.isEmpty() ||
            transaction.projectXmlPath.isEmpty() || expectedXmlPath.isEmpty() ||
            transaction.projectXmlPath.compare(QDir::cleanPath(expectedXmlPath), Qt::CaseInsensitive) != 0) {
            if (errorMessage) *errorMessage = QStringLiteral("Metadata recovery record is incomplete.");
            return false;
        }
        const QString xmlPath = transaction.projectXmlPath;
        const QString baseXmlHash = journal.value(QStringLiteral("baseXmlHash")).toString();
        const QString baseXmlGeneration = journal.value(QStringLiteral("baseXmlGeneration")).toString();
        const QString newMetadataHash = journal.value(QStringLiteral("newMetadataHash")).toString();
        const QString currentXmlHash = QString::fromLatin1(sha256File(xmlPath));
        const QString currentXmlGeneration = fileGeneration(xmlPath);
        const bool matchesBase = !baseXmlHash.isEmpty() && currentXmlHash == baseXmlHash &&
            (baseXmlGeneration.isEmpty() || currentXmlGeneration == baseXmlGeneration);
        const bool matchesNew = !newMetadataHash.isEmpty() && currentXmlHash == newMetadataHash;
        if (!matchesBase) {
            if (matchesNew) {
                if (errorMessage) *errorMessage = QStringLiteral("Project XML already contains the pending metadata commit; recovery requires explicit journal reconciliation.");
            } else if (errorMessage) {
                *errorMessage = QStringLiteral("Project XML changed after transaction baseline capture; recovery will not overwrite it.");
            }
            return false;
        }
        if (!restoreMetadataBackup(transaction, nullptr, errorMessage)) {
            return false;
        }

        const bool finalExists = QDir(finalPath).exists();
        const bool stagingExists = QDir(stagingPath).exists();
        if (stagingExists) {
            if (errorMessage) *errorMessage = QStringLiteral("Metadata recovery found an unexpected staging directory.");
            return false;
        }
        if (hasPreviousFinal) {
            if (!QDir(backupPath).exists()) {
                if (errorMessage) *errorMessage = QStringLiteral("Metadata recovery cannot restore the previous output backup.");
                return false;
            }
            if (finalExists && !root.rename(nodeName, stagingName)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot isolate output candidate during metadata recovery.");
                return false;
            }
            if (!QDir(finalPath).exists() && !root.rename(backupName, nodeName)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot restore the previous output after metadata recovery.");
                return false;
            }
        } else if (finalExists && !QDir(finalPath).removeRecursively()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove uncommitted output candidate after metadata recovery.");
            return false;
        }

        if (QDir(stagingPath).exists() && !QDir(stagingPath).removeRecursively()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove isolated output candidate after metadata recovery.");
            return false;
        }
        if (!transaction.metadataBackupName.isEmpty() &&
            QFileInfo(metadataBackupPath(transaction)).exists() &&
            !QFile::remove(metadataBackupPath(transaction))) {
            InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for interrupted XML backup: %1")
                .arg(transaction.metadataBackupName));
        }
        if (!journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty())
            return restorePreviousCommittedJournal();
        return markFailed(QStringLiteral("restored XML backup and rolled back uncommitted metadata"));
    }

    if (errorMessage) *errorMessage = QStringLiteral("Interrupted output transaction is ambiguous at stage %1; output remains isolated until controlled recovery.")
        .arg(stage.isEmpty() ? QStringLiteral("unknown") : stage);
    return false;
}

bool beginOutputTransaction(const QString& projectRoot,
                            const QString& nodeName,
                            const QStringList& expectedFinalPaths,
                            const QStringList& inputPaths,
                            OutputTransaction& transaction,
                            QString* errorMessage,
                            OutputTransactionRecoveryInfo* recoveryInfo,
                            const QString& projectXmlPath)
{
    transaction = OutputTransaction();
    if (recoveryInfo) *recoveryInfo = OutputTransactionRecoveryInfo();
    const QDir root(projectRoot);
    if (!root.exists()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project output root does not exist: %1").arg(projectRoot);
        return false;
    }
    if (expectedFinalPaths.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No expected output files were prepared for node: %1").arg(nodeName);
        return false;
    }
    QString targetReason;
    if (!isDirectProjectChild(root.absolutePath(), nodeName, &targetReason)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid project output transaction target: %1. root=%2, node=%3")
                .arg(targetReason)
                .arg(root.absolutePath())
                .arg(nodeName);
        }
        return false;
    }

    const QString transactionDirectory = transactionDirectoryPath(root.absolutePath());
    if (!QDir().mkpath(transactionDirectory)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot create transaction directory: %1").arg(transactionDirectory);
        return false;
    }

    transaction.projectRoot = root.absolutePath();
    transaction.nodeName = nodeName;
    const QFileInfo projectXmlInfo(projectXmlPath);
    if (!projectXmlInfo.isFile() ||
        QDir::cleanPath(projectXmlInfo.absolutePath()).compare(QDir::cleanPath(root.absolutePath()), Qt::CaseInsensitive) != 0 ||
        !isDirectProjectChild(root.absolutePath(), projectXmlInfo.fileName())) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction requires the active project XML path in its project root.");
        return false;
    }
    transaction.projectXmlPath = QDir::cleanPath(projectXmlInfo.canonicalFilePath());
    if (transaction.projectXmlPath.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot canonicalize active project XML path for output transaction.");
        return false;
    }
    transaction.baseXmlHash = QString::fromLatin1(sha256File(transaction.projectXmlPath));
    transaction.baseXmlGeneration = fileGeneration(transaction.projectXmlPath);
    if (transaction.baseXmlHash.isEmpty() || transaction.baseXmlGeneration.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot capture initial project XML baseline for output transaction.");
        return false;
    }
    {
        QMutexLocker projectXmlLocker(&g_projectXmlMutex);
        if (!g_projectXmlRevisions.contains(transaction.projectXmlPath)) {
            rememberProjectXmlRevision(transaction.projectXmlPath,
                                       {transaction.baseXmlHash, transaction.baseXmlGeneration});
        }
    }
    transaction.journalPath = QDir(transactionDirectory).absoluteFilePath(nodeName + QStringLiteral(".json"));
    if (QFileInfo::exists(transaction.journalPath)) {
        QFile recoveryJournal(transaction.journalPath);
        if (!recoveryJournal.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot inspect interrupted output transaction: %1").arg(transaction.journalPath);
            return false;
        }
        const QJsonObject recoveryRecord = QJsonDocument::fromJson(recoveryJournal.readAll()).object();
        const QString recoveryStage = recoveryRecord.value(QStringLiteral("stage")).toString();
        recoveryJournal.close();
        QString recoveryError;
        if (!recoverOutputTransaction(root.absolutePath(), nodeName, &recoveryError)) {
            if (errorMessage) *errorMessage = recoveryError;
            return false;
        }
        if (recoveryInfo && recoveryStage != QStringLiteral("Completed") && recoveryStage != QStringLiteral("Failed")) {
            recoveryInfo->transactionRecovered = true;
            recoveryInfo->projectXmlRestored = recoveryStage == QStringLiteral("MetadataCommitPrepared") &&
                recoveryRecord.value(QStringLiteral("metadataBackupReady")).toBool(false);
        }
        QFile journal(transaction.journalPath);
        if (!journal.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot inspect unresolved output transaction: %1").arg(transaction.journalPath);
            return false;
        }
        QJsonObject previous = QJsonDocument::fromJson(journal.readAll()).object();
        journal.close();
        const QString previousStage = previous.value(QStringLiteral("stage")).toString();
        if (previousStage != QStringLiteral("Completed") && previousStage != QStringLiteral("Failed")) {
            if (errorMessage) *errorMessage = QStringLiteral("Unresolved output transaction blocks a new run: %1").arg(transaction.journalPath);
            return false;
        }
        if (previousStage == QStringLiteral("Completed")) {
            if (!journalNamesAreSafe(root, nodeName, previous)) {
                if (errorMessage) *errorMessage = QStringLiteral("Previous committed output transaction record has unsafe paths.");
                return false;
            }
            previous.remove(QStringLiteral("previousCommittedJournal"));
            transaction.previousCommittedJournal = previous;
        }
    }

    QSet<QString> uniqueNames;
    for (const QString& expectedPath : expectedFinalPaths) {
        const QFileInfo info(expectedPath);
        if (QDir::cleanPath(info.absolutePath()).compare(
                QDir::cleanPath(root.absoluteFilePath(nodeName)), Qt::CaseInsensitive) != 0 ||
            info.fileName().isEmpty() || uniqueNames.contains(info.fileName())) {
            if (errorMessage) *errorMessage = QStringLiteral("Expected output is not a direct child of the node output directory: %1").arg(expectedPath);
            return false;
        }
        uniqueNames.insert(info.fileName());
        transaction.expectedFileNames.append(info.fileName());
    }

    transaction.inputPaths = inputPaths;
    transaction.inputFingerprints = fingerprintInputs(inputPaths);
    if (!fingerprintsAreReadable(transaction.inputFingerprints, errorMessage)) {
        return false;
    }
    transaction.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    transaction.transactionId = transaction.runId;
    const QString registryPath = root.absoluteFilePath(QStringLiteral(".dem_resource_registry.json"));
    transaction.baseRegistryHash = QString::fromLatin1(sha256File(registryPath));
    transaction.baseRegistryGeneration = fileGeneration(registryPath);
    const QString previousTransactionId = transaction.previousCommittedJournal
        .value(QStringLiteral("transactionId")).toString();
    if (!previousTransactionId.isEmpty()) transaction.dependencyTransactions.append(previousTransactionId);
    transaction.resourceAction = QStringLiteral("installed");
    transaction.provenanceOnlyUpdate = false;
    transaction.stagingName = QStringLiteral(".%1.staging-%2").arg(nodeName, transaction.runId);
    transaction.backupName = QStringLiteral(".%1.backup-%2").arg(nodeName, transaction.runId);
    transaction.hasPreviousFinal = QFileInfo(root.absoluteFilePath(nodeName)).exists();
    if (transaction.hasPreviousFinal) {
        QFile previousManifest(root.absoluteFilePath(nodeName + "/" + QString::fromLatin1(kOutputManifestFile)));
        if (previousManifest.open(QIODevice::ReadOnly)) {
            transaction.previousFinalManifest = QJsonDocument::fromJson(previousManifest.readAll()).object();
        }
    }
    if (!isDirectProjectChild(root.absolutePath(), transaction.stagingName) ||
        !isDirectProjectChild(root.absolutePath(), transaction.backupName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot prepare a safe staging directory for output transaction.");
        return false;
    }
    if (!setTransactionStage(transaction, OutputTransaction::Stage::StagingCreatePrepared, errorMessage)) {
        return false;
    }
    if (!root.mkdir(transaction.stagingName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot create staging directory for output transaction.");
        return false;
    }

    return setTransactionStage(transaction, OutputTransaction::Stage::StagingPrepared, errorMessage);
}

bool validateStagedOutputTransaction(OutputTransaction& transaction, QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::StagingPrepared) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction is not in staging state.");
        return false;
    }

    const QDir staging(QDir(transaction.projectRoot).absoluteFilePath(transaction.stagingName));
    QString descriptorError;
    const QtNodes::ProductDescriptor::Ptr descriptor = QtNodes::ProductDescriptor::fromJson(
        transaction.productDescriptor, &descriptorError);
    if (!descriptor || descriptor->state() != QtNodes::ProductState::Committed) {
        if (errorMessage) {
            *errorMessage = descriptorError.isEmpty()
                ? QStringLiteral("Staged output has no committed product descriptor.")
                : descriptorError;
        }
        return false;
    }
    if (!fingerprintsMatch(transaction.inputFingerprints, transaction.inputPaths)) {
        if (errorMessage) *errorMessage = QStringLiteral("Input files changed while the output transaction was running.");
        return false;
    }

    QSet<QString> expectedNames;
    for (const QString& name : transaction.expectedFileNames) expectedNames.insert(name);
    const QFileInfoList stagedEntries = staging.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo& entry : stagedEntries) {
        if (entry.isDir() || !expectedNames.contains(entry.fileName())) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Staged output contains an artifact outside the expected manifest: %1")
                    .arg(entry.absoluteFilePath());
            }
            return false;
        }
    }

    QJsonArray files;
    for (const QString& name : transaction.expectedFileNames) {
        const QFileInfo info(staging.absoluteFilePath(name));
        if (!validateOutputFile(info, errorMessage)) return false;
        if (info.suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0 &&
            !writeProductDescriptorToH5(info.absoluteFilePath(), transaction.productDescriptor, errorMessage)) {
            return false;
        }
        QJsonObject file;
        file.insert(QStringLiteral("name"), name);
        file.insert(QStringLiteral("size"), static_cast<double>(info.size()));
        file.insert(QStringLiteral("modifiedMs"), static_cast<double>(info.lastModified().toMSecsSinceEpoch()));
        QFile artifact(info.absoluteFilePath());
        if (!artifact.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot hash staged output: %1").arg(info.absoluteFilePath());
            return false;
        }
        const QByteArray artifactHash = sha256File(info.absoluteFilePath());
        if (artifactHash.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot hash staged output: %1").arg(info.absoluteFilePath());
            return false;
        }
        file.insert(QStringLiteral("sha256"), QString::fromLatin1(artifactHash));
        files.append(file);
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 2);
    manifest.insert(QStringLiteral("runId"), transaction.runId);
    manifest.insert(QStringLiteral("nodeName"), transaction.nodeName);
    manifest.insert(QStringLiteral("inputs"), fingerprintInputs(transaction.inputPaths));
    manifest.insert(QStringLiteral("productDescriptor"), transaction.productDescriptor);
    manifest.insert(QStringLiteral("outputs"), files);
    if (!writeJsonAtomically(staging.absoluteFilePath(QString::fromLatin1(kOutputManifestFile)), manifest, errorMessage)) {
        return false;
    }
    return setTransactionStage(transaction, OutputTransaction::Stage::StagingValidated, errorMessage);
}

bool setOutputTransactionProductDescriptor(OutputTransaction& transaction,
                                           const QtNodes::ProductDescriptor::Ptr& descriptor,
                                           QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::StagingPrepared) {
        if (errorMessage) *errorMessage = QStringLiteral("Product descriptor must be set before staged output validation.");
        return false;
    }
    if (!descriptor || descriptor->state() != QtNodes::ProductState::Committed) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction requires a committed product descriptor.");
        return false;
    }
    transaction.productDescriptor = descriptor->toJson();
    return persistTransaction(transaction, errorMessage);
}

bool validateStagedH5Datasets(const OutputTransaction& transaction,
                              const QStringList& requiredDatasets,
                              QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::StagingValidated) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 output validation requires a validated staging transaction.");
        return false;
    }
    if (requiredDatasets.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No required H5 datasets were provided for staged output validation.");
        return false;
    }

    const QDir staging(QDir(transaction.projectRoot).absoluteFilePath(transaction.stagingName));
    bool hasH5Output = false;
    for (const QString& name : transaction.expectedFileNames) {
        if (QFileInfo(name).suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        hasH5Output = true;
        const QString h5Path = staging.absoluteFilePath(name);
        for (const QString& dataset : requiredDatasets) {
            if (dataset.isEmpty()) {
                if (errorMessage) *errorMessage = QStringLiteral("Staged H5 validation contains an empty dataset name.");
                return false;
            }
            cv::Mat value;
            QString readError;
            if (!readMatFromH5(h5Path, dataset, value, -1, &readError) || value.empty()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Staged H5 output is missing required dataset '%1': %2 (%3)")
                        .arg(dataset, h5Path, readError);
                }
                return false;
            }
        }
    }
    if (!hasH5Output) {
        if (errorMessage) *errorMessage = QStringLiteral("Staged output transaction does not contain an H5 artifact.");
        return false;
    }
    return true;
}

bool promoteOutputTransaction(OutputTransaction& transaction, QStringList& finalPaths, QString* errorMessage)
{
    finalPaths.clear();
    if (transaction.stage != OutputTransaction::Stage::StagingValidated) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction has not passed staging validation.");
        return false;
    }

    QDir root(transaction.projectRoot);
    if (transaction.hasPreviousFinal) {
        if (!setTransactionStage(transaction, OutputTransaction::Stage::BackupMovePrepared, errorMessage) ||
            !root.rename(transaction.nodeName, transaction.backupName)) {
            if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Cannot move previous output directory to backup.");
            return false;
        }
        if (!setTransactionStage(transaction, OutputTransaction::Stage::BackupMoved, errorMessage)) return false;
    }

    if (!setTransactionStage(transaction, OutputTransaction::Stage::PromotionPrepared, errorMessage) ||
        !root.rename(transaction.stagingName, transaction.nodeName)) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Cannot promote staged output directory.");
        return false;
    }
    if (!setTransactionStage(transaction, OutputTransaction::Stage::FinalPromoted, errorMessage)) return false;

    const QDir finalDirectory(root.absoluteFilePath(transaction.nodeName));
    for (const QString& name : transaction.expectedFileNames) {
        const QFileInfo info(finalDirectory.absoluteFilePath(name));
        if (!validateOutputFile(info, errorMessage)) return false;
        finalPaths.append(info.absoluteFilePath());
    }
    return true;
}

bool completeOutputTransactionWithoutMetadata(OutputTransaction& transaction, QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::FinalPromoted) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot complete output transaction before output promotion.");
        return false;
    }

    const OutputTransaction::Stage previousStage = transaction.stage;
    const QJsonObject previousCommittedJournal = transaction.previousCommittedJournal;
    const QStringList dependencyTransactions = transaction.dependencyTransactions;
    transaction.previousCommittedJournal = QJsonObject();
    transaction.dependencyTransactions.clear();
    if (!setTransactionStage(transaction, OutputTransaction::Stage::Completed, errorMessage)) {
        transaction.stage = previousStage;
        transaction.previousCommittedJournal = previousCommittedJournal;
        transaction.dependencyTransactions = dependencyTransactions;
        return false;
    }

    QDir root(transaction.projectRoot);
    if (transaction.hasPreviousFinal && QDir(root.absoluteFilePath(transaction.backupName)).exists() &&
        !QDir(root.absoluteFilePath(transaction.backupName)).removeRecursively()) {
        transaction.backupCleanupDeferred = true;
        InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for output transaction backup: %1")
            .arg(transaction.backupName));
    }
    return true;
}

bool completeAuxiliaryDemResourceTransaction(OutputTransaction& transaction,
                                             QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::StagingPrepared) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot complete auxiliary DEM resource transaction before resource installation.");
        }
        return false;
    }

    const QDir root(transaction.projectRoot);
    const QString stagingPath = root.absoluteFilePath(transaction.stagingName);
    if (QDir(stagingPath).exists() && !QDir(stagingPath).removeRecursively()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot remove auxiliary DEM resource transaction staging directory.");
        }
        return false;
    }

    const QJsonObject previousCommittedJournal = transaction.previousCommittedJournal;
    const QStringList dependencyTransactions = transaction.dependencyTransactions;
    transaction.previousCommittedJournal = QJsonObject();
    transaction.dependencyTransactions.clear();
    if (!setTransactionStage(transaction, OutputTransaction::Stage::Completed, errorMessage)) {
        transaction.previousCommittedJournal = previousCommittedJournal;
        transaction.dependencyTransactions = dependencyTransactions;
        return false;
    }

    if (!transaction.resourceRegistryBackupPath.isEmpty() &&
        QFileInfo::exists(transaction.resourceRegistryBackupPath) &&
        !QFile::remove(transaction.resourceRegistryBackupPath)) {
        InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for auxiliary DEM registry backup: %1")
            .arg(transaction.resourceRegistryBackupPath));
    }
    return true;
}

bool prepareOutputTransactionMetadataCommit(OutputTransaction& transaction,
                                            XMLFile* xml,
                                            const QString& xmlPath,
                                            QString* errorMessage)
{
    if (transaction.stage != OutputTransaction::Stage::FinalPromoted) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot prepare metadata before output promotion.");
        return false;
    }
    if (transaction.metadataCommitLockHeld) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction already owns the project XML metadata commit lock.");
        return false;
    }

    // Hold the lease through prepare -> caller XML mutation -> save -> mark/abandon.
    // QMutex::Recursive permits saveProjectXmlAtomically() during the lease.
    g_projectXmlMutex.lock();
    transaction.metadataCommitLockHeld = true;
    MetadataCommitLockReleaseGuard releaseOnFailure(transaction);

    const QFileInfo xmlInfo(xmlPath);
    const QDir root(transaction.projectRoot);
    if (!xml || !xmlInfo.isFile() ||
        QDir::cleanPath(xmlInfo.absolutePath()).compare(QDir::cleanPath(root.absolutePath()), Qt::CaseInsensitive) != 0 ||
        !isDirectProjectChild(root.absolutePath(), xmlInfo.fileName())) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML is not a direct project-root file: %1").arg(xmlPath);
        return false;
    }

    const QString canonicalXmlPath = QDir::cleanPath(xmlInfo.canonicalFilePath());
    if (transaction.projectXmlPath.isEmpty() || canonicalXmlPath.isEmpty() ||
        canonicalXmlPath.compare(QDir::cleanPath(transaction.projectXmlPath), Qt::CaseInsensitive) != 0 ||
        (!transaction.metadataXmlName.isEmpty() && xmlInfo.fileName() != transaction.metadataXmlName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML differs from the active XML bound to this output transaction.");
        return false;
    }

    const ProjectXmlRevision currentXmlRevision = projectXmlRevision(transaction.projectXmlPath);
    if (currentXmlRevision.hash.isEmpty() || currentXmlRevision.generation.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot capture the current project XML metadata baseline.");
        return false;
    }

    const QString revisionKey = QDir::cleanPath(transaction.projectXmlPath);
    const auto knownRevision = g_projectXmlRevisions.constFind(revisionKey);
    if (knownRevision == g_projectXmlRevisions.constEnd() ||
        !sameProjectXmlRevision(knownRevision.value(), currentXmlRevision)) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML changed outside the coordinated XML save path.");
        return false;
    }

    // Other nodes may have committed while this worker was processing. Reload
    // their persisted metadata and make it this transaction's rollback base.
    const QByteArray nativeXmlPath = QDir::toNativeSeparators(transaction.projectXmlPath).toLocal8Bit();
    if (xml->XMLFile_load(nativeXmlPath.constData()) < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot reload project XML before metadata commit: %1")
            .arg(transaction.projectXmlPath);
        return false;
    }
    const ProjectXmlRevision reloadedXmlRevision = projectXmlRevision(transaction.projectXmlPath);
    if (!sameProjectXmlRevision(currentXmlRevision, reloadedXmlRevision)) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML changed while preparing its metadata commit.");
        return false;
    }
    transaction.baseXmlHash = reloadedXmlRevision.hash;
    transaction.baseXmlGeneration = reloadedXmlRevision.generation;
    const QString registryPath = root.absoluteFilePath(QStringLiteral(".dem_resource_registry.json"));
    if (!transaction.resourceRegistryCommitted) {
        transaction.baseRegistryHash = QString::fromLatin1(sha256File(registryPath));
        transaction.baseRegistryGeneration = fileGeneration(registryPath);
    }
    const QString expectedRegistryHash = transaction.resourceRegistryCommitted
        ? transaction.resourceRegistryCommittedHash : transaction.baseRegistryHash;
    const QString expectedRegistryGeneration = transaction.resourceRegistryCommitted
        ? transaction.newRegistryGeneration : transaction.baseRegistryGeneration;
    if (!expectedRegistryHash.isEmpty() &&
        QString::fromLatin1(sha256File(registryPath)) != expectedRegistryHash) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry changed after output transaction preparation.");
        return false;
    }
    if (!expectedRegistryGeneration.isEmpty() &&
        fileGeneration(registryPath) != expectedRegistryGeneration) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry generation changed after output transaction preparation.");
        return false;
    }
    if (transaction.resourceRegistryCommitted) {
        transaction.newRegistryGeneration = fileGeneration(registryPath);
        if (transaction.newRegistryGeneration.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot capture committed DEM resource registry generation.");
            return false;
        }
    }
    transaction.metadataXmlName = xmlInfo.fileName();
    transaction.metadataBackupName = QStringLiteral(".%1.metadata-backup-%2.xml")
        .arg(transaction.nodeName, transaction.runId);
    transaction.metadataBackupReady = false;
    if (!setTransactionStage(transaction, OutputTransaction::Stage::MetadataCommitPrepared, errorMessage)) {
        return false;
    }

    const QString backupPath = metadataBackupPath(transaction);
    if (!saveProjectXmlAtomically(xml, backupPath, errorMessage)) {
        return false;
    }
    transaction.metadataBackupReady = true;
    if (!persistTransaction(transaction, errorMessage)) {
        return false;
    }

    // The successful caller owns the lease until mark/abandon releases it.
    releaseOnFailure.dismiss();
    return true;
}

bool markOutputTransactionMetadataCommitted(OutputTransaction& transaction, QString* errorMessage)
{
    if (!transaction.metadataCommitLockHeld) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction does not own the project XML metadata commit lock.");
        return false;
    }
    if (transaction.stage != OutputTransaction::Stage::MetadataCommitPrepared ||
        !transaction.metadataBackupReady || !QFileInfo(metadataBackupPath(transaction)).isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot commit metadata without a prepared XML backup.");
        return false;
    }
    const QDir root(transaction.projectRoot);
    const QString xmlPath = QDir::cleanPath(transaction.projectXmlPath);
    const QString expectedXmlPath = QFileInfo(root.absoluteFilePath(transaction.metadataXmlName)).canonicalFilePath();
    if (xmlPath.isEmpty() || expectedXmlPath.isEmpty() ||
        xmlPath.compare(QDir::cleanPath(expectedXmlPath), Qt::CaseInsensitive) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Output transaction lost its bound project XML identity before metadata commit.");
        return false;
    }
    transaction.newMetadataHash = QString::fromLatin1(sha256File(xmlPath));
    transaction.newMetadataGeneration = fileGeneration(xmlPath);
    const QString registryPath = root.absoluteFilePath(QStringLiteral(".dem_resource_registry.json"));
    const QString expectedRegistryGeneration = transaction.resourceRegistryCommitted
        ? transaction.newRegistryGeneration : transaction.baseRegistryGeneration;
    if (!expectedRegistryGeneration.isEmpty() &&
        fileGeneration(registryPath) != expectedRegistryGeneration) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry generation changed before metadata commit.");
        return false;
    }
    transaction.resourceRegistryCommittedHash = QString::fromLatin1(sha256File(registryPath));
    transaction.newRegistryGeneration = fileGeneration(registryPath);
    if (transaction.newMetadataHash.isEmpty() ||
        !persistTransaction(transaction, errorMessage) ||
        !setTransactionStage(transaction, OutputTransaction::Stage::MetadataCommitted, errorMessage)) return false;

    if (!QFile::remove(metadataBackupPath(transaction))) {
        InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for output transaction XML backup: %1")
            .arg(transaction.metadataBackupName));
    }

    if (transaction.hasPreviousFinal && QDir(root.absoluteFilePath(transaction.backupName)).exists() &&
        !QDir(root.absoluteFilePath(transaction.backupName)).removeRecursively()) {
        transaction.backupCleanupDeferred = true;
        InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for output transaction backup: %1").arg(transaction.backupName));
        transaction.previousCommittedJournal = QJsonObject();
        transaction.dependencyTransactions.clear();
        QString completionError;
        if (!setTransactionStage(transaction, OutputTransaction::Stage::Completed, &completionError)) {
            InSARLogManager::LogWarning("NodeUtils", QString("Metadata committed but completion journal update was deferred: %1")
                .arg(completionError));
        }
        releaseMetadataCommitLock(transaction);
        return true;
    }
    transaction.previousCommittedJournal = QJsonObject();
    transaction.dependencyTransactions.clear();
    QString completionError;
    if (!setTransactionStage(transaction, OutputTransaction::Stage::Completed, &completionError)) {
        InSARLogManager::LogWarning("NodeUtils", QString("Metadata committed but completion journal update was deferred: %1")
            .arg(completionError));
    }
    releaseMetadataCommitLock(transaction);
    return true;
}

void abandonOutputTransaction(OutputTransaction& transaction, const QString& reason, XMLFile* xml)
{
    MetadataCommitLockReleaseGuard releaseOnExit(transaction);
    if (transaction.stage == OutputTransaction::Stage::Inactive || transaction.projectRoot.isEmpty()) return;
    QDir root(transaction.projectRoot);
    const QString finalPath = root.absoluteFilePath(transaction.nodeName);
    const QString stagingPath = root.absoluteFilePath(transaction.stagingName);
    const QString backupPath = root.absoluteFilePath(transaction.backupName);
    bool rollbackOk = true;

    // Resource installation/provenance is part of the same transaction.  An
    // installed resource is removed on failure; a reused resource keeps its
    // files and only loses the newly appended provenance manifest/registry
    // delta.
    if (transaction.stage != OutputTransaction::Stage::MetadataCommitted &&
        transaction.stage != OutputTransaction::Stage::Completed) {
        if (!transaction.provenanceManifestPath.isEmpty() &&
            transaction.provenanceOnlyUpdate && QFileInfo::exists(transaction.provenanceManifestPath) &&
            !QFile::remove(transaction.provenanceManifestPath)) {
            rollbackOk = false;
        }
        if (transaction.resourceAction == QStringLiteral("installed") &&
            !transaction.installedPath.isEmpty() && QDir(transaction.installedPath).exists() &&
            !QDir(transaction.installedPath).removeRecursively()) {
            rollbackOk = false;
        }
        if (!transaction.resourceStagingPath.isEmpty() &&
            QDir(transaction.resourceStagingPath).exists() &&
            !QDir(transaction.resourceStagingPath).removeRecursively()) {
            rollbackOk = false;
        }
        QString registryError;
        if (!transaction.resourceRegistryBackupPath.isEmpty() &&
            QFileInfo::exists(transaction.resourceRegistryBackupPath)) {
            const QString registryPath = QDir(transaction.projectRoot).absoluteFilePath(QStringLiteral(".dem_resource_registry.json"));
            const QByteArray currentHash = sha256File(registryPath);
            const QString currentGeneration = fileGeneration(registryPath);
            if (!transaction.resourceRegistryCommittedHash.isEmpty() &&
                QString::fromLatin1(currentHash) != transaction.resourceRegistryCommittedHash) {
                registryError = QStringLiteral("DEM resource registry changed after this transaction; recovery was deferred.");
            } else if (!transaction.newRegistryGeneration.isEmpty() &&
                       currentGeneration != transaction.newRegistryGeneration) {
                registryError = QStringLiteral("DEM resource registry changed after this transaction; recovery was deferred.");
            } else {
                QFile::remove(registryPath);
                if (!QFile::copy(transaction.resourceRegistryBackupPath, registryPath)) {
                    registryError = QStringLiteral("Cannot restore DEM resource registry backup.");
                }
            }
            if (registryError.isEmpty()) QFile::remove(transaction.resourceRegistryBackupPath);
        } else if ((transaction.resourceRegistryCommitted || transaction.resourceRegistryMutationPrepared) &&
                   !transaction.provenanceDelta.isEmpty() &&
                   (transaction.resourceAction == QStringLiteral("installed") ||
                    transaction.resourceAction == QStringLiteral("reused"))) {
            if (!removeAuxiliaryDemRegistryEntry(transaction.projectRoot,
                                                  transaction.provenanceDelta.value(QStringLiteral("resourceId")).toString(),
                                                  &registryError)) {
                rollbackOk = false;
            } else if (transaction.baseRegistryHash.isEmpty()) {
                const QString registryPath = QDir(transaction.projectRoot).absoluteFilePath(
                    QStringLiteral(".dem_resource_registry.json"));
                if (QFileInfo::exists(registryPath) && !QFile::remove(registryPath)) {
                    registryError = QStringLiteral("Cannot remove DEM registry created by the abandoned transaction.");
                    rollbackOk = false;
                }
            }
        }
        if (!registryError.isEmpty()) {
            InSARLogManager::LogWarning("NodeUtils", registryError);
            rollbackOk = false;
        }
    }

    if (transaction.stage == OutputTransaction::Stage::StagingCreatePrepared) {
        if (QDir(stagingPath).exists() && !QDir(stagingPath).removeRecursively()) {
            rollbackOk = false;
        }
        if (rollbackOk && !transaction.previousCommittedJournal.isEmpty()) {
            QString restoreError;
            rollbackOk = writeJsonAtomically(transaction.journalPath,
                                              transaction.previousCommittedJournal,
                                              &restoreError);
            if (!rollbackOk) {
                InSARLogManager::LogError("NodeUtils", QString("Cannot restore previous committed output journal: %1")
                    .arg(restoreError));
            }
        }
        if (rollbackOk && transaction.previousCommittedJournal.isEmpty()) {
            transaction.stage = OutputTransaction::Stage::Failed;
            QString ignored;
            persistTransaction(transaction, &ignored);
        }
        if (!reason.isEmpty()) InSARLogManager::LogDiagnostic(InSARLogManager::LevelDebug, "NodeUtils",
            QString("Output transaction abandoned: %1").arg(reason),
            LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile, "output.transaction");
        return;
    }

    if (transaction.stage == OutputTransaction::Stage::MetadataCommitPrepared &&
        !restoreMetadataBackup(transaction, xml, nullptr)) {
        rollbackOk = false;
    }

    // A post-promotion metadata failure must not silently publish the new
    // directory. Restore the previous final when available; otherwise remove
    // the uncommitted candidate. Metadata preparation has already restored
    // the previous XML before this filesystem rollback is attempted.
    if (rollbackOk && (transaction.stage == OutputTransaction::Stage::FinalPromoted ||
                       transaction.stage == OutputTransaction::Stage::MetadataCommitPrepared)) {
        if (transaction.hasPreviousFinal && QDir(backupPath).exists()) {
            if (QDir(finalPath).exists() && !root.rename(transaction.nodeName, transaction.stagingName)) {
                rollbackOk = false;
            }
            if (rollbackOk && !root.rename(transaction.backupName, transaction.nodeName)) {
                rollbackOk = false;
            }
        } else if (!transaction.hasPreviousFinal && QDir(finalPath).exists()) {
            rollbackOk = QDir(finalPath).removeRecursively();
        }
    } else if ((transaction.stage == OutputTransaction::Stage::BackupMoved ||
                transaction.stage == OutputTransaction::Stage::PromotionPrepared) &&
               transaction.hasPreviousFinal && !QDir(finalPath).exists() && QDir(backupPath).exists()) {
        rollbackOk = root.rename(transaction.backupName, transaction.nodeName);
    }

    if (transaction.stage != OutputTransaction::Stage::MetadataCommitted &&
        transaction.stage != OutputTransaction::Stage::Completed) {
        if (QDir(stagingPath).exists() && !QDir(stagingPath).removeRecursively()) {
            rollbackOk = false;
        }
        if (rollbackOk) {
            if (!transaction.metadataBackupName.isEmpty() &&
                QFileInfo(metadataBackupPath(transaction)).exists() &&
                !QFile::remove(metadataBackupPath(transaction))) {
                InSARLogManager::LogWarning("NodeUtils", QString("Deferred cleanup for output transaction XML backup: %1")
                    .arg(transaction.metadataBackupName));
            }
            if (!transaction.previousCommittedJournal.isEmpty()) {
                QString restoreError;
                rollbackOk = writeJsonAtomically(transaction.journalPath,
                                                  transaction.previousCommittedJournal,
                                                  &restoreError);
                if (!rollbackOk) {
                    InSARLogManager::LogError("NodeUtils", QString("Cannot restore previous committed output journal: %1")
                        .arg(restoreError));
                }
            } else {
                transaction.stage = OutputTransaction::Stage::Failed;
                QString ignored;
                persistTransaction(transaction, &ignored);
            }
        } else {
            InSARLogManager::LogError("NodeUtils", QString("Output transaction rollback requires recovery: %1").arg(transaction.journalPath));
        }
        if (!reason.isEmpty()) InSARLogManager::LogDiagnostic(InSARLogManager::LevelDebug, "NodeUtils",
            QString("Output transaction abandoned: %1").arg(reason),
            LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile, "output.transaction");
    }
}

bool loadCommittedOutputManifest(const QString& projectRoot,
                                 const QString& nodeName,
                                 QStringList& outputPaths,
                                 QString* errorMessage)
{
    outputPaths.clear();
    const QDir root(projectRoot);
    if (!root.exists() || !isDirectProjectChild(root.absolutePath(), nodeName)) return false;
    const QString journalPath = QDir(transactionDirectoryPath(root.absolutePath())).absoluteFilePath(nodeName + QStringLiteral(".json"));
    if (!QFileInfo::exists(journalPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("No committed output transaction record exists.");
        return false;
    }
    QFile journal(journalPath);
    if (!journal.open(QIODevice::ReadOnly)) return false;
    const QJsonObject journalObject = QJsonDocument::fromJson(journal.readAll()).object();
    journal.close();
    if (!journalNamesAreSafe(root, nodeName, journalObject)) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output transaction record has unsafe paths.");
        return false;
    }
    const QString stage = journalObject.value(QStringLiteral("stage")).toString();
    if (stage != QStringLiteral("MetadataCommitted") && stage != QStringLiteral("Completed")) {
        QString recoveryError;
        const bool recovered = recoverOutputTransaction(root.absolutePath(), nodeName, &recoveryError);
        if (recovered) {
            QFile recoveredJournal(journalPath);
            if (recoveredJournal.open(QIODevice::ReadOnly)) {
                const QJsonObject recoveredObject = QJsonDocument::fromJson(recoveredJournal.readAll()).object();
                recoveredJournal.close();
                const QString recoveredStage = recoveredObject.value(QStringLiteral("stage")).toString();
                if (recoveredStage == QStringLiteral("MetadataCommitted") ||
                    recoveredStage == QStringLiteral("Completed")) {
                    return loadCommittedOutputManifest(projectRoot, nodeName, outputPaths, errorMessage);
                }
            }
        }
        if (errorMessage) {
            *errorMessage = recovered
                ? QStringLiteral("Interrupted output transaction was cleaned up; output remains invalid until rerun.")
                : recoveryError;
        }
        return false;
    }
    if (journalObject.value(QStringLiteral("nodeName")).toString() != nodeName ||
        journalObject.value(QStringLiteral("runId")).toString().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output transaction record is invalid.");
        return false;
    }

    QFile manifest(QDir(root.absoluteFilePath(nodeName)).absoluteFilePath(QString::fromLatin1(kOutputManifestFile)));
    if (!manifest.open(QIODevice::ReadOnly)) return false;
    const QJsonObject object = QJsonDocument::fromJson(manifest.readAll()).object();
    if (object.value(QStringLiteral("nodeName")).toString() != nodeName ||
        object.value(QStringLiteral("runId")).toString() != journalObject.value(QStringLiteral("runId")).toString()) {
        if (errorMessage) *errorMessage = QStringLiteral("Output manifest does not match the committed transaction.");
        return false;
    }
    QString descriptorError;
    const QtNodes::ProductDescriptor::Ptr descriptor = QtNodes::ProductDescriptor::fromJson(
        object.value(QStringLiteral("productDescriptor")).toObject(), &descriptorError);
    if (!descriptor || descriptor->state() != QtNodes::ProductState::Committed) {
        if (errorMessage) *errorMessage = descriptorError.isEmpty()
            ? QStringLiteral("Committed output manifest has no committed product descriptor.")
            : descriptorError;
        return false;
    }
    const QJsonArray outputs = object.value(QStringLiteral("outputs")).toArray();
    QSet<QString> expectedNames;
    for (const QJsonValue& value : journalObject.value(QStringLiteral("expectedFiles")).toArray()) {
        const QString name = value.toString();
        if (name.isEmpty() || expectedNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output transaction has an invalid expected file list.");
            return false;
        }
        expectedNames.insert(name);
    }
    if (expectedNames.isEmpty() || outputs.size() != expectedNames.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("Output manifest file count does not match the committed transaction.");
        return false;
    }
    QSet<QString> actualNames;
    for (const QJsonValue& value : outputs) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (name.isEmpty() || !expectedNames.contains(name) || actualNames.contains(name)) return false;
        actualNames.insert(name);
        const QFileInfo info(root.absoluteFilePath(nodeName + "/" + name));
        if (!validateOutputFile(info, errorMessage)) return false;
        outputPaths.append(info.absoluteFilePath());
    }
    return !outputPaths.isEmpty();
}

bool loadCommittedOutputProductDescriptor(const QString& projectRoot,
                                          const QString& nodeName,
                                          QtNodes::ProductDescriptor::Ptr& descriptor,
                                          QString* errorMessage)
{
    descriptor.reset();
    QStringList outputPaths;
    if (!loadCommittedOutputManifest(projectRoot, nodeName, outputPaths, errorMessage)) {
        return false;
    }

    const QDir root(projectRoot);
    QFile manifest(QDir(root.absoluteFilePath(nodeName)).absoluteFilePath(
        QString::fromLatin1(kOutputManifestFile)));
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest is unavailable.");
        return false;
    }
    QString descriptorError;
    descriptor = QtNodes::ProductDescriptor::fromJson(
        QJsonDocument::fromJson(manifest.readAll()).object()
            .value(QStringLiteral("productDescriptor")).toObject(),
        &descriptorError);
    if (!descriptor || descriptor->state() != QtNodes::ProductState::Committed) {
        descriptor.reset();
        if (errorMessage) *errorMessage = descriptorError.isEmpty()
            ? QStringLiteral("Committed output descriptor is invalid.")
            : descriptorError;
        return false;
    }
    return true;
}

bool validateH5Identity(const QString& h5Path,
                        const QtNodes::ProductDescriptor::Ptr& expectedDescriptor,
                        QtNodes::ProductDescriptor::Ptr* observedDescriptor,
                        QString* errorMessage)
{
    if (observedDescriptor) observedDescriptor->reset();
    const QFileInfo h5Info(h5Path);
    if (!h5Info.isFile() || h5Info.suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Input is not a readable H5 artifact: %1").arg(h5Path);
        return false;
    }
    const QDir outputDirectory(h5Info.absolutePath());
    const QString nodeName = outputDirectory.dirName();
    QDir projectRoot = outputDirectory;
    if (!projectRoot.cdUp() || !isDirectProjectChild(projectRoot.absolutePath(), nodeName)) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 artifact is not in a direct committed output directory: %1").arg(h5Path);
        return false;
    }
    QFile journal(QDir(transactionDirectoryPath(projectRoot.absolutePath())).absoluteFilePath(
        nodeName + QStringLiteral(".json")));
    if (!journal.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 artifact has no output transaction journal: %1").arg(h5Path);
        return false;
    }
    const QJsonObject journalObject = QJsonDocument::fromJson(journal.readAll()).object();
    const QString journalStage = journalObject.value(QStringLiteral("stage")).toString();
    if (!journalNamesAreSafe(projectRoot, nodeName, journalObject) ||
        (journalStage != QStringLiteral("MetadataCommitted") && journalStage != QStringLiteral("Completed")) ||
        journalObject.value(QStringLiteral("runId")).toString().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 artifact journal is not a committed transaction: %1").arg(h5Path);
        return false;
    }
    QFile manifest(outputDirectory.absoluteFilePath(QString::fromLatin1(kOutputManifestFile)));
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 artifact has no committed product identity record: %1").arg(h5Path);
        return false;
    }
    const QJsonObject manifestObject = QJsonDocument::fromJson(manifest.readAll()).object();
    if (manifestObject.value(QStringLiteral("nodeName")).toString() != nodeName ||
        manifestObject.value(QStringLiteral("runId")).toString() !=
            journalObject.value(QStringLiteral("runId")).toString()) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 manifest does not match its committed journal: %1").arg(h5Path);
        return false;
    }
    QString descriptorError;
    const QtNodes::ProductDescriptor::Ptr descriptor = QtNodes::ProductDescriptor::fromJson(
        manifestObject.value(QStringLiteral("productDescriptor")).toObject(), &descriptorError);
    if (!descriptor || descriptor->state() != QtNodes::ProductState::Committed) {
        if (errorMessage) *errorMessage = descriptorError.isEmpty()
            ? QStringLiteral("H5 product identity record is not committed.") : descriptorError;
        return false;
    }
    QString h5DescriptorError;
    const QtNodes::ProductDescriptor::Ptr h5Descriptor = readProductDescriptorFromH5(
        h5Info.absoluteFilePath(), &h5DescriptorError);
    if (!h5Descriptor || QJsonDocument(h5Descriptor->toJson()).toJson(QJsonDocument::Compact) !=
        QJsonDocument(descriptor->toJson()).toJson(QJsonDocument::Compact)) {
        if (errorMessage) *errorMessage = h5DescriptorError.isEmpty()
            ? QStringLiteral("H5 product descriptor does not match its committed manifest.")
            : h5DescriptorError;
        return false;
    }
    bool listed = false;
    for (const QJsonValue& output : manifestObject.value(QStringLiteral("outputs")).toArray()) {
        if (output.toObject().value(QStringLiteral("name")).toString() == h5Info.fileName()) {
            listed = true;
            break;
        }
    }
    if (!listed) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 artifact is not claimed by its committed manifest: %1").arg(h5Path);
        return false;
    }
    if (expectedDescriptor &&
        QJsonDocument(descriptor->toJson()).toJson(QJsonDocument::Compact) !=
            QJsonDocument(expectedDescriptor->toJson()).toJson(QJsonDocument::Compact)) {
        if (errorMessage) *errorMessage = QStringLiteral("H5 product identity does not match the bound descriptor.");
        return false;
    }
    if (observedDescriptor) *observedDescriptor = descriptor;
    return true;
}

bool validateH5Identities(const QStringList& h5Paths,
                          const QtNodes::ProductDescriptor::Ptr& expectedDescriptor,
                          QString* errorMessage)
{
    if (!expectedDescriptor) {
        if (errorMessage) *errorMessage = QStringLiteral("Input has no bound product descriptor.");
        return false;
    }
    for (const QString& h5Path : h5Paths) {
        if (!validateH5Identity(h5Path, expectedDescriptor, nullptr, errorMessage)) {
            return false;
        }
    }
    return !h5Paths.isEmpty();
}

bool loadCommittedOutputManifestRunId(const QString& projectRoot,
                                      const QString& nodeName,
                                      QString& runId,
                                      QString* errorMessage)
{
    runId.clear();
    QStringList outputPaths;
    if (!loadCommittedOutputManifest(projectRoot, nodeName, outputPaths, errorMessage)) {
        return false;
    }

    const QDir root(projectRoot);
    const QString journalPath = QDir(transactionDirectoryPath(root.absolutePath())).absoluteFilePath(
        nodeName + QStringLiteral(".json"));
    QFile journal(journalPath);
    if (!journal.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output transaction record is unavailable.");
        return false;
    }
    const QJsonObject journalObject = QJsonDocument::fromJson(journal.readAll()).object();
    runId = journalObject.value(QStringLiteral("runId")).toString();
    if (runId.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output transaction record has no run identifier.");
        return false;
    }
    return true;
}

bool workerOutputsMatchManifest(const QStringList& manifestPaths,
                                const QStringList& workerPaths,
                                QString* errorMessage)
{
    if (manifestPaths.size() != workerPaths.size() || manifestPaths.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Worker output count does not match the transaction manifest.");
        return false;
    }

    QSet<QString> expectedNames;
    for (const QString& path : manifestPaths) {
        const QString name = QFileInfo(path).fileName();
        if (name.isEmpty() || expectedNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Transaction manifest has duplicate or invalid output names.");
            return false;
        }
        expectedNames.insert(name);
    }

    QSet<QString> workerNames;
    for (const QString& path : workerPaths) {
        const QString name = QFileInfo(path).fileName();
        if (name.isEmpty() || !expectedNames.contains(name) || workerNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Worker output is missing from the transaction manifest: %1").arg(path);
            return false;
        }
        workerNames.insert(name);
    }
    return workerNames == expectedNames;
}

bool saveProjectXmlAtomically(XMLFile* xml, const QString& xmlPath, QString* errorMessage)
{
    QMutexLocker projectXmlLocker(&g_projectXmlMutex);
    if (!xml || xmlPath.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML context is unavailable.");
        return false;
    }

    const QFileInfo targetInfo(xmlPath);
    const QDir targetDirectory(targetInfo.absolutePath());
    if (!targetDirectory.exists()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project XML directory does not exist: %1").arg(targetInfo.absolutePath());
        return false;
    }

    const QString temporaryPath = targetDirectory.absoluteFilePath(
        QStringLiteral(".%1.transaction-%2.tmp").arg(targetInfo.fileName(),
            QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const QByteArray temporaryName = QDir::toNativeSeparators(temporaryPath).toLocal8Bit();
    if (xml->XMLFile_save(temporaryName.constData()) < 0 ||
        !QFileInfo(temporaryPath).isFile() || QFileInfo(temporaryPath).size() <= 0) {
        QFile::remove(temporaryPath);
        xml->XMLFile_load(QDir::toNativeSeparators(xmlPath).toLocal8Bit().constData());
        if (errorMessage) *errorMessage = QStringLiteral("Failed to write temporary project XML: %1").arg(temporaryPath);
        return false;
    }

#ifdef Q_OS_WIN
    if (!::MoveFileExW(reinterpret_cast<LPCWSTR>(temporaryPath.utf16()),
                       reinterpret_cast<LPCWSTR>(xmlPath.utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = ::GetLastError();
        QFile::remove(temporaryPath);
        xml->XMLFile_load(QDir::toNativeSeparators(xmlPath).toLocal8Bit().constData());
        if (errorMessage) *errorMessage = QStringLiteral("Failed to replace project XML (%1): Win32 error %2")
            .arg(xmlPath).arg(static_cast<qulonglong>(error));
        return false;
    }
#else
    QFile::remove(xmlPath);
    if (!QFile::rename(temporaryPath, xmlPath)) {
        QFile::remove(temporaryPath);
        xml->XMLFile_load(QDir::toNativeSeparators(xmlPath).toLocal8Bit().constData());
        if (errorMessage) *errorMessage = QStringLiteral("Failed to replace project XML: %1").arg(xmlPath);
        return false;
    }
#endif
    const QFileInfo savedInfo(xmlPath);
    const QString canonicalXmlPath = QDir::cleanPath(savedInfo.canonicalFilePath());
    rememberProjectXmlRevision(canonicalXmlPath, projectXmlRevision(canonicalXmlPath));
    return true;
}

bool removeOutputFiles(const QStringList& filePaths)
{
    bool allSuccess = true;
    for (const QString& path : filePaths) {
        if (path.isEmpty()) continue;
        if (QFile::exists(path)) {
            if (!QFile::remove(path)) {
                InSARLogManager::LogWarning("NodeUtils", QString("无法物理删除旧文件：%1，文件可能正被占用。").arg(path));
                allSuccess = false;
            }
        }

        if (path.endsWith(".h5", Qt::CaseInsensitive)) {
            QString jpgPath = path;
            jpgPath.chop(3);
            jpgPath += ".jpg";
            if (QFile::exists(jpgPath)) {
                if (!QFile::remove(jpgPath)) {
                    InSARLogManager::LogWarning("NodeUtils", QString("无法物理删除旧预览图：%1，文件可能正被占用。").arg(jpgPath));
                    allSuccess = false;
                }
            }
        }
    }
    return allSuccess;
}

bool isJpgPreviewCurrent(const QStringList& inputPaths, const QString& jpgPath)
{
    QFileInfo jpgInfo(jpgPath);
    if (inputPaths.isEmpty() || !jpgInfo.exists() || jpgInfo.size() <= 0) {
        return false;
    }

    for (const QString& inputPath : inputPaths) {
        QFileInfo inputInfo(inputPath);
        if (inputPath.isEmpty() || !inputInfo.exists() ||
            jpgInfo.lastModified() < inputInfo.lastModified()) {
            return false;
        }
    }
    return true;
}

bool isJpgPreviewCurrent(const QString& h5Path, const QString& jpgPath)
{
    return isJpgPreviewCurrent(QStringList() << h5Path, jpgPath);
}

bool replaceJpgPreviewAtomically(const QString& temporaryJpgPath, const QString& jpgPath)
{
    // JPGs are written by OpenCV/Utils. Do not validate them through Qt's
    // optional image-format plugins, which are not deployed in every runtime.
    if (!isCompleteJpegFile(temporaryJpgPath)) {
        return false;
    }

#ifdef Q_OS_WIN
    return ::MoveFileExW(reinterpret_cast<LPCWSTR>(temporaryJpgPath.utf16()),
                         reinterpret_cast<LPCWSTR>(jpgPath.utf16()),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (QFile::exists(jpgPath) && !QFile::remove(jpgPath)) {
        return false;
    }
    return QFile::rename(temporaryJpgPath, jpgPath);
#endif
}

static bool generateJpgPreviewFromH5Direct(const QString& h5Path, const QString& jpgPath,
                                            const QString& type, std::function<void(int, int)> cb);

bool generateJpgPreviewFromH5(const QString& h5Path, const QString& jpgPath, const QString& type)
{
    return generateJpgPreviewFromH5WithProgress(h5Path, jpgPath, type, nullptr);
}

bool generateJpgPreviewFromH5WithProgress(const QString& h5Path, const QString& jpgPath, const QString& type, std::function<void(int, int)> cb)
{
    if (h5Path.isEmpty() || jpgPath.isEmpty()) {
        InSARLogManager::LogWarning("NodeUtils", "JPG preview generation skipped because the H5 or JPG path is empty.");
        return false;
    }

    const QFileInfo jpgInfo(jpgPath);
    const QString tempPath = jpgInfo.absolutePath() + "/." + jpgInfo.completeBaseName() +
        "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
    QFile::remove(tempPath);

    if (!generateJpgPreviewFromH5Direct(h5Path, tempPath, type, cb)) {
        InSARLogManager::LogWarning("NodeUtils", QString("JPG preview rendering failed: %1").arg(h5Path));
        QFile::remove(tempPath);
        return false;
    }

    // Do not publish a preview if its H5 changed while rendering.
    if (!isJpgPreviewCurrent(h5Path, tempPath)) {
        InSARLogManager::LogWarning("NodeUtils", QString("JPG preview discarded because its source changed: %1").arg(h5Path));
        QFile::remove(tempPath);
        return false;
    }

    if (!replaceJpgPreviewAtomically(tempPath, jpgPath)) {
        InSARLogManager::LogWarning("NodeUtils", QString("JPG preview validation or atomic publication failed: %1").arg(jpgPath));
        QFile::remove(tempPath);
        return false;
    }
    return true;
}

static bool generateJpgPreviewFromH5Direct(const QString& h5Path, const QString& jpgPath, const QString& type, std::function<void(int, int)> cb)
{
    Hdf5Locker locker;
    if (h5Path.isEmpty() || jpgPath.isEmpty())
        return false;

    if (QFile::exists(jpgPath) && !QFile::remove(jpgPath))
        return false;

    Utils util;
    FormatConversion FC;

    // 局部 Lambda 帮助函数：手动归一化与保存相位 JPG（用于 savephase 失败时的备用逻辑）
    auto savePhaseFallback = [&](const cv::Mat& mat_to_save, const QString& type_str) -> int {
        cv::Mat phase_normalized;
        if (type_str == "coherence")
        {
            phase_normalized = mat_to_save * 255.0;
        }
        else if (type_str == "dem")
        {
            double minVal, maxVal;
            cv::minMaxLoc(mat_to_save, &minVal, &maxVal);
            if (maxVal - minVal > 1e-6)
            {
                phase_normalized = (mat_to_save - minVal) * (255.0 / (maxVal - minVal));
            }
            else
            {
                phase_normalized = cv::Mat::zeros(mat_to_save.size(), CV_64F);
            }
        }
        else
        {
            phase_normalized = (mat_to_save + 3.141592653589793) * (255.0 / (2.0 * 3.141592653589793));
        }
        
        phase_normalized.convertTo(phase_normalized, CV_8U);
        
        cv::Mat color_image;
        if (type_str == "coherence")
        {
            color_image = phase_normalized;
        }
        else
        {
            cv::applyColorMap(phase_normalized, color_image, cv::COLORMAP_JET);
        }
        
        bool success_write = cv::imwrite(jpgPath.toStdString(), color_image);
        return success_write ? 0 : -1;
    };

    if (type == "complex")
    {
        int rows = 0, cols = 0;
        if (FC.get_dataset_dims(h5Path.toLocal8Bit().constData(), "s_re", &rows, &cols) != 0)
            return false;

        double totalPixels = double(rows) * cols;
        int down_sample_times = 1;
        if (totalPixels > 25e6)
        {
            down_sample_times = (int)std::sqrt(std::floor(totalPixels / 25e6));
        }

        if (down_sample_times <= 1)
        {
            ComplexMat SLC64;
            if (FC.read_slc_from_h5(h5Path.toLocal8Bit().constData(), SLC64) != 0)
                return false;
                
            util.saveSLC(jpgPath.toLocal8Bit().constData(), 65, SLC64);
            if (cb) cb(rows, rows);
            return true;
        }

        // For previews, average intensity rather than complex samples.  TOPS
        // azimuth phase changes can otherwise cancel during complex averaging.
        int dst_rows = rows / down_sample_times;
        int dst_cols = cols / down_sample_times;
        cv::Mat downsampled_power(dst_rows, dst_cols, CV_64F);

        int block_height_read = (1024 / down_sample_times) * down_sample_times;
        if (block_height_read == 0) block_height_read = down_sample_times;

        for (int r = 0; r < rows; r += block_height_read)
        {
            int rows_to_read = std::min(block_height_read, rows - r);
            cv::Mat block_re, block_im;

            if (FC.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_re", r, 0, rows_to_read, cols, block_re) != 0 ||
                FC.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_im", r, 0, rows_to_read, cols, block_im) != 0)
            {
                return false;
            }

            int block_dst_rows = rows_to_read / down_sample_times;
            int block_dst_cols = cols / down_sample_times;

            if (block_dst_rows > 0 && block_dst_cols > 0)
            {
                cv::multiply(block_re, block_re, block_re);
                cv::multiply(block_im, block_im, block_im);
                block_re += block_im;

                cv::Mat down_power;
                cv::resize(block_re, down_power, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);

                int r_dst = r / down_sample_times;
                if (r_dst + block_dst_rows <= dst_rows)
                {
                    down_power.copyTo(downsampled_power(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                }
            }

            if (cb)
            {
                cb(r + rows_to_read, rows);
            }
        }

        cv::Mat downsampled_amplitude;
        cv::sqrt(downsampled_power, downsampled_amplitude);

        // saveSLC owns the existing display normalization.  Put the amplitude
        // in the real component so its magnitude is preserved.
        ComplexMat preview_SLC(dst_rows, dst_cols);
        downsampled_amplitude.copyTo(preview_SLC.re);
        preview_SLC.im.setTo(0.0);
        util.saveSLC(jpgPath.toLocal8Bit().constData(), 65, preview_SLC);
        return true;
    }
    else if (type == "phase" || type == "coherence" || type == "dem")
    {
        int rows = 0, cols = 0;
        if (FC.get_dataset_dims(h5Path.toStdString().c_str(), type.toStdString().c_str(), &rows, &cols) != 0)
            return false;

        double totalPixels = double(rows) * cols;
        int down_sample_times = 1;
        if (totalPixels > 25e6)
        {
            down_sample_times = (int)std::sqrt(std::floor(totalPixels / 25e6));
        }

        cv::Mat phase;
        int ret = -1;

        if (down_sample_times <= 1)
        {
            if (FC.read_array_from_h5(h5Path.toStdString().c_str(), type.toStdString().c_str(), phase) != 0)
                return false;
                
            if (phase.type() != CV_64F)
            {
                phase.convertTo(phase, CV_64F);
            }
            phase = phase.clone();
                
            if (type == "phase")
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "jet", phase);
            }
            else if (type == "coherence")
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "gray", phase);
            }
            else if (type == "dem")
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "jet", phase);
            }
            
            if (ret != 0)
            {
                ret = savePhaseFallback(phase, type);
            }
            if (cb) cb(rows, rows);
            return ret == 0;
        }

        // 分块读取并下采样拼装
        int dst_rows = rows / down_sample_times;
        int dst_cols = cols / down_sample_times;
        cv::Mat downsampled_phase(dst_rows, dst_cols, CV_64F);

        int block_height_read = (1024 / down_sample_times) * down_sample_times;
        if (block_height_read == 0) block_height_read = down_sample_times;

        for (int r = 0; r < rows; r += block_height_read)
        {
            int rows_to_read = std::min(block_height_read, rows - r);
            cv::Mat block_phase;

            if (FC.read_subarray_from_h5(h5Path.toStdString().c_str(), type.toStdString().c_str(), r, 0, rows_to_read, cols, block_phase) != 0)
                return false;

            if (block_phase.type() != CV_64F)
            {
                block_phase.convertTo(block_phase, CV_64F);
            }

            int block_dst_rows = rows_to_read / down_sample_times;
            int block_dst_cols = cols / down_sample_times;

            if (block_dst_rows > 0 && block_dst_cols > 0)
            {
                cv::Mat down_block;
                cv::resize(block_phase, down_block, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);

                int r_dst = r / down_sample_times;
                if (r_dst + block_dst_rows <= dst_rows)
                {
                    down_block.copyTo(downsampled_phase(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                }
            }

            if (cb)
            {
                cb(r + rows_to_read, rows);
            }
        }

        if (type == "phase")
        {
            ret = util.savephase(jpgPath.toStdString().c_str(), "jet", downsampled_phase);
        }
        else if (type == "coherence")
        {
            ret = util.savephase(jpgPath.toStdString().c_str(), "gray", downsampled_phase);
        }
        else if (type == "dem")
        {
            ret = util.savephase(jpgPath.toStdString().c_str(), "jet", downsampled_phase);
        }

        if (ret != 0)
        {
            ret = savePhaseFallback(downsampled_phase, type);
        }
        return ret == 0;
    }
    else if (type == "amplitude")
    {
        int rows = 0, cols = 0;
        if (FC.get_dataset_dims(h5Path.toStdString().c_str(), "amplitude", &rows, &cols) != 0)
            return false;

        double totalPixels = double(rows) * cols;
        int down_sample_times = 1;
        if (totalPixels > 25e6)
        {
            down_sample_times = (int)std::sqrt(std::floor(totalPixels / 25e6));
        }

        cv::Mat phase;
        int ret = -1;

        if (down_sample_times <= 1)
        {
            if (FC.read_array_from_h5(h5Path.toStdString().c_str(), "amplitude", phase) != 0)
                return false;
                
            if (phase.type() != CV_32F)
            {
                phase.convertTo(phase, CV_32F);
            }
            phase = phase.clone();
            
            ret = util.saveAmplitude(jpgPath.toStdString().c_str(), phase);
            if (cb) cb(rows, rows);
            return ret == 0;
        }

        // 分块读取并下采样拼装
        int dst_rows = rows / down_sample_times;
        int dst_cols = cols / down_sample_times;
        cv::Mat downsampled_amplitude(dst_rows, dst_cols, CV_32F);

        int block_height_read = (1024 / down_sample_times) * down_sample_times;
        if (block_height_read == 0) block_height_read = down_sample_times;

        for (int r = 0; r < rows; r += block_height_read)
        {
            int rows_to_read = std::min(block_height_read, rows - r);
            cv::Mat block_amp;

            if (FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "amplitude", r, 0, rows_to_read, cols, block_amp) != 0)
                return false;

            if (block_amp.type() != CV_32F)
            {
                block_amp.convertTo(block_amp, CV_32F);
            }

            int block_dst_rows = rows_to_read / down_sample_times;
            int block_dst_cols = cols / down_sample_times;

            if (block_dst_rows > 0 && block_dst_cols > 0)
            {
                cv::Mat down_block;
                cv::resize(block_amp, down_block, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);

                int r_dst = r / down_sample_times;
                if (r_dst + block_dst_rows <= dst_rows)
                {
                    down_block.copyTo(downsampled_amplitude(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                }
            }

            if (cb)
            {
                cb(r + rows_to_read, rows);
            }
        }

        ret = util.saveAmplitude(jpgPath.toStdString().c_str(), downsampled_amplitude);
        return ret == 0;
    }
    else if (type == "SBAS")
    {
        int rows = 0, cols = 0;
        if (FC.get_dataset_dims(h5Path.toStdString().c_str(), "defomation_velocity", &rows, &cols) != 0)
            return false;

        double totalPixels = double(rows) * cols;
        int down_sample_times = 1;
        if (totalPixels > 25e6)
        {
            down_sample_times = (int)std::sqrt(std::floor(totalPixels / 25e6));
        }

        cv::Mat defomation_velocity, mask;
        int ret_vel = -1, ret_mask = -1;

        if (down_sample_times <= 1)
        {
            ret_vel = FC.read_array_from_h5(h5Path.toStdString().c_str(), "defomation_velocity", defomation_velocity);
            if (ret_vel != 0)
                return false;
                
            ret_mask = FC.read_array_from_h5(h5Path.toStdString().c_str(), "mask", mask);
            
            if (defomation_velocity.type() != CV_64F)
            {
                defomation_velocity.convertTo(defomation_velocity, CV_64F);
            }
            defomation_velocity = defomation_velocity.clone();
            
            int ret = -1;
            if (ret_mask == 0)
            {
                ret = util.savephase_white(jpgPath.toStdString().c_str(), "jet", defomation_velocity, mask);
            }
            else
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "jet", defomation_velocity);
            }
            if (cb) cb(rows, rows);
            return ret == 0;
        }

        // 分块读取并下采样拼装
        int dst_rows = rows / down_sample_times;
        int dst_cols = cols / down_sample_times;
        cv::Mat downsampled_vel(dst_rows, dst_cols, CV_64F);
        cv::Mat downsampled_mask;

        int mask_rows = 0, mask_cols = 0;
        bool has_mask = (FC.get_dataset_dims(h5Path.toStdString().c_str(), "mask", &mask_rows, &mask_cols) == 0);
        if (has_mask)
        {
            downsampled_mask.create(dst_rows, dst_cols, CV_32S);
        }

        int block_height_read = (1024 / down_sample_times) * down_sample_times;
        if (block_height_read == 0) block_height_read = down_sample_times;

        for (int r = 0; r < rows; r += block_height_read)
        {
            int rows_to_read = std::min(block_height_read, rows - r);
            cv::Mat block_vel;

            if (FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "defomation_velocity", r, 0, rows_to_read, cols, block_vel) != 0)
                return false;

            if (block_vel.type() != CV_64F)
            {
                block_vel.convertTo(block_vel, CV_64F);
            }

            cv::Mat block_mask;
            if (has_mask)
            {
                if (FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mask", r, 0, rows_to_read, cols, block_mask) != 0)
                {
                    has_mask = false;
                }
            }

            int block_dst_rows = rows_to_read / down_sample_times;
            int block_dst_cols = cols / down_sample_times;

            if (block_dst_rows > 0 && block_dst_cols > 0)
            {
                cv::Mat down_vel;
                cv::resize(block_vel, down_vel, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_AREA);

                int r_dst = r / down_sample_times;
                if (r_dst + block_dst_rows <= dst_rows)
                {
                    down_vel.copyTo(downsampled_vel(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                }

                if (has_mask && !block_mask.empty())
                {
                    cv::Mat down_mask;
                    cv::resize(block_mask, down_mask, cv::Size(block_dst_cols, block_dst_rows), 0, 0, cv::INTER_NEAREST);
                    if (r_dst + block_dst_rows <= dst_rows)
                    {
                        down_mask.copyTo(downsampled_mask(cv::Rect(0, r_dst, block_dst_cols, block_dst_rows)));
                    }
                }
            }

            if (cb)
            {
                cb(r + rows_to_read, rows);
            }
        }

        int ret = -1;
        if (has_mask && !downsampled_mask.empty())
        {
            ret = util.savephase_white(jpgPath.toStdString().c_str(), "jet", downsampled_vel, downsampled_mask);
        }
        else
        {
            ret = util.savephase(jpgPath.toStdString().c_str(), "jet", downsampled_vel);
        }
        return ret == 0;
    }
    return false;
}

QStandardItem* findOrCreateProjectNode(
    QStandardItem* project,
    const QString& nodeName,
    const QString& rankType,
    const QString& iconPath,
    bool* created)
{
    if (!project) return nullptr;

    // 1. 查找是否已存在相同名称和 Rank 的节点
    for (int i = 0; i < project->rowCount(); ++i) {
        if (project->child(i, 0)->text() == nodeName &&
            project->child(i, 1) && project->child(i, 1)->text() == rankType) {
            if (created) *created = false;
            return project->child(i, 0);
        }
    }

    if (created) *created = true;

    // 2. 统一定义项目树所有阶段 of Rank 排序顺序
    static const QStringList RANK_ORDER = {
        // === 1. 复数图像数据阶段 ===
        "complex-0.0",     // 原始导入图像
        "complex-1.0",     // 裁剪后图像 / 去爆
        "complex-2.0",     // 配准后图像
        "complex-3.0",     // 去斜率图像
        
        // === 2. 相位数据阶段 ===
        "phase-1.0",       // 干涉相位图
        "phase-1.1",       // 【地理编码】干涉相位图
        
        // === 3. 相干性数据阶段 ===
        "coherence-1.0",   // 相干系数图
        "coherence-1.1",   // 【地理编码】相干系数图
        
        // === 4. 滤波与解缠相位阶段 ===
        "phase-2.0",       // 滤波相位图
        "phase-2.1",       // 【地理编码】滤波相位图
        "phase-3.0",       // 解缠相位图
        "phase-3.1",       // 【地理编码】解缠相位图
        
        // === 5. 高程与最终产品阶段 ===
        "dem-1.0",         // 雷达坐标系 DEM
        "dem-1.1",         // 【地理编码】地理坐标系 DEM
        "SBAS-1.0",        // 时序形变速率
        "SBAS-1.1"         // 【地理编码】时序形变速率
    };

    int targetIdx = RANK_ORDER.indexOf(rankType);
    if (targetIdx == -1) targetIdx = 999; // 未知 rank 默认放最后

    // 3. 寻找正确的排序插入位置
    int insert = 0;
    for (; insert < project->rowCount(); ++insert) {
        QStandardItem* rankCol = project->child(insert, 1);
        QString childRank = rankCol ? rankCol->text() : "";
        int childIdx = RANK_ORDER.indexOf(childRank);
        if (childIdx == -1) childIdx = 999;

        if (childIdx <= targetIdx) {
            continue;
        } else {
            break;
        }
    }

    // 4. 创建并插入节点
    QStandardItem* node = new QStandardItem(nodeName);
    QString finalIcon = iconPath.isEmpty() ? FOLDER_ICON : iconPath;
    node->setIcon(QIcon(finalIcon));
    project->insertRow(insert, node);

    QStandardItem* rankItem = new QStandardItem(rankType);
    project->setChild(insert, 1, rankItem);

    return node;
}

QStandardItem* findOrCreateChildItem(
    QStandardItem* parent,
    const QString& childName,
    const QString& tooltip,
    const QString& h5Path,
    const QString& iconPath,
    bool* created)
{
    if (!parent) return nullptr;

    // 1. 查找是否已存在该子项
    for (int i = 0; i < parent->rowCount(); ++i) {
        if (parent->child(i, 0)->text() == childName) {
            if (created) *created = false;
            return parent->child(i, 0);
        }
    }

    if (created) *created = true;

    // 2. 创建子项
    QStandardItem* item = new QStandardItem(childName);
    item->setToolTip(tooltip);
    
    QString finalIcon = iconPath.isEmpty() ? IMAGEDATA_ICON : iconPath;
    item->setIcon(QIcon(finalIcon));

    // 3. 插入子项并绑定路径到第二列
    parent->appendRow(item);
    
    QStandardItem* pathItem = new QStandardItem(h5Path);
    parent->setChild(parent->rowCount() - 1, 1, pathItem);

    return item;
}

// ---------------------------------------------------------------------
// 读取 cv::Mat 矩阵数据
// ---------------------------------------------------------------------
bool readMatFromH5(const QString& filePath,
                   const QString& dataset,
                   cv::Mat& mat,
                   int targetType,
                   QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(filePath);
        return false;
    }
    FormatConversion FC;

    int rc = FC.read_array_from_h5(filePath.toStdString().c_str(),
                                   dataset.toStdString().c_str(),
                                   mat);
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 H5 数据集 %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }

    if (targetType >= 0 && mat.type() != targetType) {
        mat.convertTo(mat, targetType);
    }
    return true;
}

// ---------------------------------------------------------------------
// 读取标量数据（重载实现，支持 int, double, float, qint64）
// ---------------------------------------------------------------------
bool readScalarFromH5(const QString& filePath, const QString& dataset, int& value, QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(filePath);
        return false;
    }
    FormatConversion FC;

    int rc = FC.read_int_from_h5(filePath.toStdString().c_str(),
                                 dataset.toStdString().c_str(),
                                 &value);
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 H5 标量(int) %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

bool readScalarFromH5(const QString& filePath, const QString& dataset, double& value, QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(filePath);
        return false;
    }
    FormatConversion FC;

    int rc = FC.read_double_from_h5(filePath.toStdString().c_str(),
                                    dataset.toStdString().c_str(),
                                    &value);
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 H5 标量(double) %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

bool readScalarFromH5(const QString& filePath, const QString& dataset, float& value, QString* errMsg)
{
    double tmp = 0.0;
    if (!readScalarFromH5(filePath, dataset, tmp, errMsg)) {
        return false;
    }
    value = static_cast<float>(tmp);
    return true;
}

bool readScalarFromH5(const QString& filePath, const QString& dataset, qint64& value, QString* errMsg)
{
    int tmp = 0;
    if (!readScalarFromH5(filePath, dataset, tmp, errMsg)) {
        return false;
    }
    value = static_cast<qint64>(tmp);
    return true;
}

// ---------------------------------------------------------------------
// 读取字符串数据
// ---------------------------------------------------------------------
bool readStringFromH5(const QString& filePath,
                      const QString& dataset,
                      std::string& out,
                      QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(filePath);
        return false;
    }
    FormatConversion FC;

    int rc = FC.read_str_from_h5(filePath.toStdString().c_str(),
                                 dataset.toStdString().c_str(),
                                 out);
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 H5 字符串 %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

bool writeStringToH5(const QString& filePath,
                     const QString& dataset,
                     const std::string& value,
                     QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 file does not exist: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("H5 string dataset name is empty.");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("Failed to lock H5 for string write: %1").arg(filePath);
        return false;
    }
    FormatConversion conversion;
    const int rc = conversion.write_str_to_h5(filePath.toStdString().c_str(),
                                               dataset.toStdString().c_str(),
                                               value.c_str());
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("Failed to write H5 string dataset %1 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

bool copySourcePathMetadata(const QString& inputPath,
                            const QString& outputPath,
                            QString* errMsg)
{
    std::string source1;
    std::string source2;
    if (!readStringFromH5(inputPath, QStringLiteral("source_1"), source1, errMsg) ||
        !readStringFromH5(inputPath, QStringLiteral("source_2"), source2, errMsg)) {
        return false;
    }

    if (!isValidUtf8PathBytes(source1) || !isValidUtf8PathBytes(source2)) {
        if (errMsg) *errMsg = QStringLiteral("Source paths are not valid UTF-8 in %1.").arg(inputPath);
        return false;
    }

    std::string encoding;
    std::string formatVersion;
    const bool hasEncoding = readStringFromH5(inputPath, QStringLiteral("source_path_encoding"), encoding);
    const bool hasFormatVersion = readStringFromH5(inputPath, QStringLiteral("source_path_format_version"), formatVersion);
    if (hasEncoding != hasFormatVersion) {
        if (errMsg) *errMsg = QStringLiteral("Source-path metadata is incomplete in %1.").arg(inputPath);
        return false;
    }
    if (hasEncoding && (encoding != "UTF-8" || formatVersion != "2")) {
        if (errMsg) *errMsg = QStringLiteral("Unsupported source-path metadata in %1.").arg(inputPath);
        return false;
    }

    std::string outputSource1;
    std::string outputSource2;
    std::string outputEncoding;
    std::string outputFormatVersion;
    if (!readStringFromH5(outputPath, QStringLiteral("source_1"), outputSource1, errMsg) ||
        !readStringFromH5(outputPath, QStringLiteral("source_2"), outputSource2, errMsg) ||
        !readStringFromH5(outputPath, QStringLiteral("source_path_encoding"), outputEncoding, errMsg) ||
        !readStringFromH5(outputPath, QStringLiteral("source_path_format_version"), outputFormatVersion, errMsg)) {
        return false;
    }
    if (outputSource1 != source1 || outputSource2 != source2) {
        if (errMsg) *errMsg = QStringLiteral("Derived H5 source paths differ from %1.").arg(inputPath);
        return false;
    }
    if (outputEncoding != "UTF-8" || outputFormatVersion != "2") {
        if (errMsg) *errMsg = QStringLiteral("Derived H5 has invalid UTF-8/v2 source-path metadata: %1.").arg(outputPath);
        return false;
    }
    return true;
}

bool writeSourcePathMetadata(const QString& outputPath,
                             const std::string& source1,
                             const std::string& source2,
                             QString* errMsg)
{
    if (!isValidUtf8PathBytes(source1) || !isValidUtf8PathBytes(source2)) {
        if (errMsg) *errMsg = QStringLiteral("Source paths are not valid UTF-8 for %1.").arg(outputPath);
        return false;
    }
    Hdf5Locker locker(outputPath);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("Failed to lock output H5 for source-path metadata: %1").arg(outputPath);
        return false;
    }
    FormatConversion conversion;
    const std::string outputUtf8 = outputPath.toStdString();
    const int source1Result = conversion.write_str_to_h5(outputUtf8.c_str(), "source_1", source1.c_str());
    if (source1Result != 0) {
        if (errMsg) *errMsg = QStringLiteral("Failed to write source_1 metadata (rc=%1): %2")
                               .arg(source1Result).arg(outputPath);
        return false;
    }

    // FormatConversion creates the UTF-8 encoding and format-version datasets
    // whenever either source path is written.
    const int source2Result = conversion.write_str_to_h5(outputUtf8.c_str(), "source_2", source2.c_str());
    if (source2Result != 0) {
        if (errMsg) *errMsg = QStringLiteral("Failed to write source_2 metadata (rc=%1): %2")
                               .arg(source2Result).arg(outputPath);
        return false;
    }
    return true;
}

bool copyPhaseProcessingMetadata(const QString& inputPath,
                                 const QString& outputPath,
                                 QString* errMsg)
{
    constexpr int kPhaseProcessingSchemaVersion = 1;
    constexpr const char* kSchemaDataset = "phase_processing_schema_version";
    constexpr const char* kFlatDataset = "phase_flat_earth_removed";
    constexpr const char* kTopoDataset = "phase_topography_removed";
    constexpr const char* kCoefficientDataset = "flat_phase_coefficient";

    cv::Mat flatPhaseCoefficient;
    QString coefficientError;
    const bool hasCoefficient = readMatFromH5(inputPath, QString::fromLatin1(kCoefficientDataset),
                                              flatPhaseCoefficient, -1, &coefficientError);
    if (!hasCoefficient) {
        const H5DatasetProbeResult coefficientProbe = probeH5Dataset(
            inputPath, QString::fromLatin1(kCoefficientDataset), &coefficientError);
        if (coefficientProbe == H5DatasetProbeResult::Error) {
            if (errMsg) *errMsg = coefficientError;
            return false;
        }
        if (coefficientProbe == H5DatasetProbeResult::Exists) {
            if (errMsg) *errMsg = coefficientError;
            return false;
        }
    }
    if (hasCoefficient && flatPhaseCoefficient.empty()) {
        if (errMsg) *errMsg = QStringLiteral("平地相位系数为空：%1").arg(inputPath);
        return false;
    }

    QString schemaError;
    const H5DatasetProbeResult schemaProbe = probeH5Dataset(
        inputPath, QString::fromLatin1(kSchemaDataset), &schemaError);
    if (schemaProbe == H5DatasetProbeResult::Error) {
        if (errMsg) *errMsg = schemaError;
        return false;
    }
    if (schemaProbe == H5DatasetProbeResult::Missing) {
        // Legacy products can continue through generic phase-processing nodes.
        if (hasCoefficient && !writeMatToH5(outputPath, QString::fromLatin1(kCoefficientDataset),
                                             flatPhaseCoefficient, errMsg)) {
            return false;
        }
        return true;
    }

    int schemaVersion = 0;
    if (!readScalarFromH5(inputPath, QString::fromLatin1(kSchemaDataset),
                          schemaVersion, &schemaError)) {
        if (errMsg) *errMsg = schemaError;
        return false;
    }
    if (schemaVersion != kPhaseProcessingSchemaVersion) {
        if (errMsg) *errMsg = QStringLiteral("不支持的相位处理契约版本：%1").arg(schemaVersion);
        return false;
    }

    int flatRemoved = 0;
    int topoRemoved = 0;
    if (!readScalarFromH5(inputPath, QString::fromLatin1(kFlatDataset), flatRemoved, errMsg) ||
        !readScalarFromH5(inputPath, QString::fromLatin1(kTopoDataset), topoRemoved, errMsg)) {
        return false;
    }
    if ((flatRemoved != 0 && flatRemoved != 1) || (topoRemoved != 0 && topoRemoved != 1)) {
        if (errMsg) *errMsg = QStringLiteral("相位处理状态无效：%1").arg(inputPath);
        return false;
    }
    if (flatRemoved == 1 && !hasCoefficient) {
        if (errMsg) *errMsg = QStringLiteral("已去平地的相位缺少平地相位系数：%1").arg(inputPath);
        return false;
    }
    if (flatRemoved == 0 && hasCoefficient) {
        if (errMsg) *errMsg = QStringLiteral("未去平地的相位不应包含平地相位系数：%1").arg(inputPath);
        return false;
    }

    if ((hasCoefficient && !writeMatToH5(outputPath, QString::fromLatin1(kCoefficientDataset),
                                          flatPhaseCoefficient, errMsg)) ||
        !writeScalarToH5(outputPath, QString::fromLatin1(kSchemaDataset), schemaVersion, errMsg) ||
        !writeScalarToH5(outputPath, QString::fromLatin1(kFlatDataset), flatRemoved, errMsg) ||
        !writeScalarToH5(outputPath, QString::fromLatin1(kTopoDataset), topoRemoved, errMsg)) {
        return false;
    }
    return true;
}

bool validateDemPhaseInput(const QString& inputPath, QString* errMsg)
{
    constexpr int kPhaseProcessingSchemaVersion = 1;
    int schemaVersion = 0;
    int flatRemoved = 0;
    int topoRemoved = 0;
    cv::Mat flatPhaseCoefficient;

    if (!readScalarFromH5(inputPath, QStringLiteral("phase_processing_schema_version"), schemaVersion, errMsg)) {
        if (errMsg) *errMsg = QStringLiteral("输入相位缺少处理状态契约，请从干涉形成节点重新运行：%1").arg(inputPath);
        return false;
    }
    if (schemaVersion != kPhaseProcessingSchemaVersion) {
        if (errMsg) *errMsg = QStringLiteral("不支持的相位处理契约版本：%1").arg(schemaVersion);
        return false;
    }
    if (!readScalarFromH5(inputPath, QStringLiteral("phase_flat_earth_removed"), flatRemoved, errMsg) ||
        !readScalarFromH5(inputPath, QStringLiteral("phase_topography_removed"), topoRemoved, errMsg)) {
        return false;
    }
    if ((flatRemoved != 0 && flatRemoved != 1) || (topoRemoved != 0 && topoRemoved != 1)) {
        if (errMsg) *errMsg = QStringLiteral("输入相位的处理状态无效：%1").arg(inputPath);
        return false;
    }
    if (flatRemoved != 1) {
        if (errMsg) *errMsg = QStringLiteral("DEM 反演要求输入相位已消除平地相位：%1").arg(inputPath);
        return false;
    }
    if (topoRemoved != 0) {
        if (errMsg) *errMsg = QStringLiteral("DEM 反演要求保留地形相位，当前输入已去地形：%1").arg(inputPath);
        return false;
    }
    if (!readMatFromH5(inputPath, QStringLiteral("flat_phase_coefficient"), flatPhaseCoefficient, -1, errMsg) ||
        flatPhaseCoefficient.empty()) {
        if (errMsg) *errMsg = QStringLiteral("输入相位缺少有效的平地相位系数：%1").arg(inputPath);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// 写入 cv::Mat 矩阵数据
// ---------------------------------------------------------------------
bool writeMatToH5(const QString& filePath,
                  const QString& dataset,
                  const cv::Mat& mat,
                  QString* errMsg)
{
    if (!QFileInfo::exists(filePath)) {
        if (errMsg) *errMsg = QStringLiteral("H5 文件不存在: %1").arg(filePath);
        return false;
    }
    if (dataset.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("数据集名称为空");
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath);
    FormatConversion FC;

    // write_array_to_h5 底层接口接收 cv::Mat&，我们使用 const_cast 去除 const 限制
    int rc = FC.write_array_to_h5(filePath.toStdString().c_str(),
                                  dataset.toStdString().c_str(),
                                  const_cast<cv::Mat&>(mat));
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("写入 H5 数据集 %1 失败 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// 写入标量数据
// ---------------------------------------------------------------------
bool writeScalarToH5(const QString& filePath, const QString& dataset, int value, QString* errMsg)
{
    cv::Mat tmp = cv::Mat::zeros(1, 1, CV_32SC1);
    tmp.at<int>(0, 0) = value;
    return writeMatToH5(filePath, dataset, tmp, errMsg);
}

bool writeScalarToH5(const QString& filePath, const QString& dataset, double value, QString* errMsg)
{
    cv::Mat tmp = cv::Mat::zeros(1, 1, CV_64FC1);
    tmp.at<double>(0, 0) = value;
    return writeMatToH5(filePath, dataset, tmp, errMsg);
}

QString getGlobalDemPath(IApplicationInterface* iface)
{
    if (!iface) return QString();
    XMLFile* xml = iface->projectXml();
    if (!xml) return QString();
    
    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (!root) return QString();
    
    TiXmlElement* pnode = nullptr;
    xml->_find_node(root, "globalDemPath", pnode);
    if (pnode && pnode->GetText()) {
        return QString::fromUtf8(pnode->GetText());
    }
    
    // 缺省时返回默认的项目级缓存路径项目目录/.dem_cache
    QString projPath = iface->projectPath();
    if (projPath.isEmpty()) return QString();
    
    QFileInfo fi(projPath);
    QString projectDir = fi.absolutePath();
    return QDir::toNativeSeparators(projectDir + "/.dem_cache");
}

bool setGlobalDemPath(IApplicationInterface* iface, const QString& path, bool askUser)
{
    if (!iface || path.isEmpty()) return false;
    
    XMLFile* xml = iface->projectXml();
    if (!xml) return false;
    
    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (!root) return false;
    
    QString cleanPath = QDir::toNativeSeparators(path);
    
    // 判断是否已经是该全局路径，避免重复设置
    QString currentGlobal = getGlobalDemPath(iface);
    if (QDir::toNativeSeparators(currentGlobal) == cleanPath) {
        return true;
    }
    
    if (askUser) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            nullptr,
            QStringLiteral("设置全局高程数据"),
            QStringLiteral("是否将该路径应用为本项目的全局默认高程数据？\n\n新路径：%1").arg(cleanPath),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply != QMessageBox::Yes) {
            return false;
        }
    }
    
    TiXmlElement* pnode = nullptr;
    xml->_find_node(root, "globalDemPath", pnode);
    if (!pnode) {
        pnode = new TiXmlElement("globalDemPath");
        root->LinkEndChild(pnode);
    }
    
    pnode->Clear();
    pnode->LinkEndChild(new TiXmlText(cleanPath.toUtf8().constData()));
    
    // 保存项目 XML，避免直接覆盖已提交文件。
    QString saveError;
    if (!saveProjectXmlAtomically(xml, iface->projectPath(), &saveError)) {
        InSARLogManager::LogError("NodeUtils", QStringLiteral("保存全局 DEM 路径失败：%1").arg(saveError));
        return false;
    }
    
    // 联动更新整个应用程序中所有已启用（未连线）的 demPathEdit 控件
    foreach (QWidget* widget, QApplication::allWidgets()) {
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget);
        if (lineEdit && lineEdit->objectName() == "demPathEdit") {
            if (lineEdit->isEnabled()) {
                lineEdit->setText(cleanPath);
                emit lineEdit->editingFinished();
            }
        }
    }
    
    return true;
}

QString getConfigPath()
{
    return QCoreApplication::applicationDirPath() + "/Config.ini";
}

QString getModelPath(const QString& modelName)
{
    // 优先尝试从可执行文件同级目录 (bin/) 查找
    QString binPath = QCoreApplication::applicationDirPath() + "/" + modelName;
    if (QFileInfo::exists(binPath)) {
        return binPath;
    }
    // 调试回退：如果 bin/ 里没有，从当前工作目录查找
    return QDir::currentPath() + "/" + modelName;
}

void safeThreadWait(QThread* thread, int timeoutMs)
{
    if (!thread) return;
    while (thread->isRunning())
    {
        thread->wait(timeoutMs);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

QString getProjectFilePath(QWidget* widget)
{
    auto* iface = getProjectContext(widget);
    return iface ? iface->projectPath() : QString();
}

QString getProjectDirectory(QWidget* widget)
{
    return projectDirectory(getProjectFilePath(widget));
}

QString projectDirectory(const QString& projectPath)
{
    if (projectPath.isEmpty()) return QString();
    if (projectPath.endsWith(".insar", Qt::CaseInsensitive))
    {
        return QFileInfo(projectPath).absolutePath();
    }
    return projectPath;
}

bool writeDemToTif(const QString& tifPath, const cv::Mat& dem, const double* gt, const char* wkt)
{
    Hdf5Locker locker(tifPath);

    GDALAllRegister();
    GDALDriver* poDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!poDriver) return false;

    int cols = dem.cols;
    int rows = dem.rows;
    GDALDataset* poDstDS = poDriver->Create(tifPath.toLocal8Bit().constData(), cols, rows, 1, GDT_Float32, nullptr);
    if (!poDstDS) return false;

    poDstDS->SetGeoTransform(const_cast<double*>(gt));
    if (wkt) poDstDS->SetProjection(wkt);

    GDALRasterBand* poBand = poDstDS->GetRasterBand(1);
    poBand->SetNoDataValue(-32767.0);
    
    cv::Mat floatDem;
    if (dem.type() != CV_32F) {
        dem.convertTo(floatDem, CV_32F);
    } else {
        floatDem = dem;
    }

    CPLErr err = poBand->RasterIO(GF_Write, 0, 0, cols, rows, floatDem.data, cols, rows, GDT_Float32, 0, 0);
    GDALClose(poDstDS);

    if (err != CE_None) {
        QFile::remove(tifPath);
        return false;
    }

    return true;
}

bool writeDemValidityMaskToTif(const QString& tifPath, const cv::Mat& validMask,
                               const double* gt, const char* wkt)
{
    if (validMask.empty() || validMask.type() != CV_8UC1) return false;

    Hdf5Locker locker(tifPath);
    GDALAllRegister();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) return false;

    GDALDataset* dataset = driver->Create(tifPath.toLocal8Bit().constData(), validMask.cols,
                                          validMask.rows, 1, GDT_Byte, nullptr);
    if (!dataset) return false;
    dataset->SetGeoTransform(const_cast<double*>(gt));
    if (wkt) dataset->SetProjection(wkt);

    GDALRasterBand* band = dataset->GetRasterBand(1);
    band->SetNoDataValue(0.0);
    const CPLErr error = band->RasterIO(GF_Write, 0, 0, validMask.cols, validMask.rows,
                                        const_cast<uchar*>(validMask.ptr<uchar>()), validMask.cols,
                                        validMask.rows, GDT_Byte, 0, 0);
    GDALClose(dataset);
    if (error != CE_None) {
        QFile::remove(tifPath);
        return false;
    }
    return true;
}

} // namespace NodeUtils
