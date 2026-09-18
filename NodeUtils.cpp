#include "include/NodeUtils.h"
#include "InSARLogManager.h"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
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
#include <QtDebug>
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
#include <cfloat>
#include <algorithm>

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
}

QString normalizedDemLabel(const QString& label)
{
    return label.trimmed().toCaseFolded();
}

namespace {
// 大文件阈值：超过 8MB 的数据文件（雷达图像 H5/TIFF/DAT 等）采用 O(1) 轻量元数据指纹，严禁全盘逐字节读取
constexpr qint64 kLargeFileHashThreshold = 8 * 1024 * 1024;

QByteArray sha256File(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) return QByteArray();

    // 针对大文件使用大小与修改时间戳生成 O(1) 快速指纹，避免数十分钟的 IO 灾难与主线程阻塞
    if (info.size() > kLargeFileHashThreshold) {
        const QString token = QStringLiteral("fast_fingerprint:%1:%2")
            .arg(info.size())
            .arg(info.lastModified().toMSecsSinceEpoch());
        return QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex();
    }

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

bool matchesPersistedFileGeneration(const QFileInfo& file, const QJsonObject& entry)
{
    const QJsonValue size = entry.value(QStringLiteral("size"));
    const QJsonValue modifiedMs = entry.value(QStringLiteral("modifiedMs"));
    return file.isFile() && size.isDouble() && modifiedMs.isDouble() &&
        static_cast<qint64>(size.toDouble()) == file.size() &&
        static_cast<qint64>(modifiedMs.toDouble()) == file.lastModified().toMSecsSinceEpoch();
}

bool matchesPersistedFileGeneration(const QFileInfo& file, qint64 size, qint64 modifiedMs)
{
    return file.isFile() && size >= 0 && modifiedMs >= 0 &&
        file.size() == size && file.lastModified().toMSecsSinceEpoch() == modifiedMs;
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
    QJsonObject resourcesForMigration = resources;
    bool anyNormalization = false;
    for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isObject()) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry contains an invalid entry: resourceId=%1, path=%2")
                .arg(it.key(), registryFilePath);
            return false;
        }
        const QJsonObject object = it.value().toObject();
        const QString expectedRoot = QStringLiteral(".dem_resources/%1").arg(it.key());
        QJsonObject normalizedObject = object;
        const QString managedRasterPath = normalizedObject.value(QStringLiteral("managedRasterPath")).toString();
        const QString managedIdentityPath = normalizedObject.value(QStringLiteral("managedIdentityH5Path")).toString();
        const QString managedMaskPath = normalizedObject.value(QStringLiteral("managedValidMaskPath")).toString();
        const QFileInfo managedRaster(QDir(projectRoot).absoluteFilePath(managedRasterPath));
        const QFileInfo managedIdentity(QDir(projectRoot).absoluteFilePath(managedIdentityPath));
        const QFileInfo managedMask(QDir(projectRoot).absoluteFilePath(managedMaskPath));
        const bool missingGeneration =
            !normalizedObject.value(QStringLiteral("rasterSize")).isDouble() ||
            !normalizedObject.value(QStringLiteral("rasterModifiedMs")).isDouble() ||
            !normalizedObject.value(QStringLiteral("identityH5Size")).isDouble() ||
            !normalizedObject.value(QStringLiteral("identityH5ModifiedMs")).isDouble() ||
            !normalizedObject.value(QStringLiteral("validMaskSize")).isDouble() ||
            !normalizedObject.value(QStringLiteral("validMaskModifiedMs")).isDouble();
        if (missingGeneration && managedRaster.isFile() && managedIdentity.isFile() && managedMask.isFile() &&
            QString::fromLatin1(sha256File(managedRaster.absoluteFilePath())) ==
                normalizedObject.value(QStringLiteral("rasterHash")).toString() &&
            QString::fromLatin1(sha256File(managedIdentity.absoluteFilePath())) ==
                normalizedObject.value(QStringLiteral("identityH5Hash")).toString() &&
            QString::fromLatin1(sha256File(managedMask.absoluteFilePath())) ==
                normalizedObject.value(QStringLiteral("validMaskHash")).toString()) {
            normalizedObject.insert(QStringLiteral("rasterSize"), static_cast<double>(managedRaster.size()));
            normalizedObject.insert(QStringLiteral("rasterModifiedMs"), static_cast<double>(managedRaster.lastModified().toMSecsSinceEpoch()));
            normalizedObject.insert(QStringLiteral("identityH5Size"), static_cast<double>(managedIdentity.size()));
            normalizedObject.insert(QStringLiteral("identityH5ModifiedMs"), static_cast<double>(managedIdentity.lastModified().toMSecsSinceEpoch()));
            normalizedObject.insert(QStringLiteral("validMaskSize"), static_cast<double>(managedMask.size()));
            normalizedObject.insert(QStringLiteral("validMaskModifiedMs"), static_cast<double>(managedMask.lastModified().toMSecsSinceEpoch()));
        }
        const QJsonObject effectiveObject = normalizedObject;
        QString canonicalMetadataHash = effectiveObject.value(QStringLiteral("canonicalMetadataHash")).toString();
        const QByteArray expectedMetadataHash = QCryptographicHash::hash(
            QJsonDocument(effectiveObject.value(QStringLiteral("metadata")).toObject()).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256);
        bool legacyCanonicalHash = false;
        if (canonicalMetadataHash.size() != 64 && canonicalMetadataHash.toLatin1().size() == 32 &&
            canonicalMetadataHash.toLatin1() == expectedMetadataHash) {
            canonicalMetadataHash = QString::fromLatin1(expectedMetadataHash.toHex());
            legacyCanonicalHash = true;
        }
        const bool legacyWithoutMask = effectiveObject.value(QStringLiteral("role")).toString() == QStringLiteral("auxiliary_terrain_dem") &&
            effectiveObject.value(QStringLiteral("validMaskHash")).toString().isEmpty() &&
            effectiveObject.value(QStringLiteral("managedValidMaskPath")).toString().isEmpty();
        if (legacyWithoutMask) {
            // Keep old resources visible for migration/history, but never allow
            // them to satisfy a current auxiliary DEM binding.
            AuxiliaryDemRegistryEntry legacy;
            legacy.resourceId = it.key();
            legacy.role = effectiveObject.value(QStringLiteral("role")).toString();
            legacy.rasterHash = effectiveObject.value(QStringLiteral("rasterHash")).toString();
            legacy.identityH5Hash = effectiveObject.value(QStringLiteral("identityH5Hash")).toString();
            legacy.canonicalMetadataHash = canonicalMetadataHash;
            legacy.managedRasterPath = effectiveObject.value(QStringLiteral("managedRasterPath")).toString();
            legacy.managedIdentityH5Path = effectiveObject.value(QStringLiteral("managedIdentityH5Path")).toString();
            legacy.metadata = effectiveObject.value(QStringLiteral("metadata")).toObject();
            legacy.provenanceHistory = effectiveObject.value(QStringLiteral("provenanceHistory")).toArray();
            legacy.tombstone = effectiveObject.value(QStringLiteral("tombstone")).toBool(false);
            legacy.legacyUnverified = true;
            entries.insert(legacy.resourceId, legacy);
            continue;
        }
        if (effectiveObject.value(QStringLiteral("role")).toString() != QStringLiteral("auxiliary_terrain_dem") ||
            effectiveObject.value(QStringLiteral("rasterHash")).toString().isEmpty() ||
            effectiveObject.value(QStringLiteral("identityH5Hash")).toString().isEmpty() ||
            effectiveObject.value(QStringLiteral("validMaskHash")).toString().isEmpty() ||
            !effectiveObject.value(QStringLiteral("rasterSize")).isDouble() ||
            !effectiveObject.value(QStringLiteral("rasterModifiedMs")).isDouble() ||
            !effectiveObject.value(QStringLiteral("identityH5Size")).isDouble() ||
            !effectiveObject.value(QStringLiteral("identityH5ModifiedMs")).isDouble() ||
            !effectiveObject.value(QStringLiteral("validMaskSize")).isDouble() ||
            !effectiveObject.value(QStringLiteral("validMaskModifiedMs")).isDouble() ||
            canonicalMetadataHash.isEmpty() ||
            effectiveObject.value(QStringLiteral("managedRasterPath")).toString() != expectedRoot + QStringLiteral("/dem.tif") ||
            effectiveObject.value(QStringLiteral("managedIdentityH5Path")).toString() != expectedRoot + QStringLiteral("/identity.h5") ||
            effectiveObject.value(QStringLiteral("managedValidMaskPath")).toString() != expectedRoot + QStringLiteral("/dem_valid_mask.tif") ||
            !effectiveObject.value(QStringLiteral("metadata")).isObject() ||
            !effectiveObject.value(QStringLiteral("provenanceHistory")).isArray() ||
            (effectiveObject.contains(QStringLiteral("tombstone")) && !effectiveObject.value(QStringLiteral("tombstone")).isBool())) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM resource registry entry is incomplete or invalid: resourceId=%1, path=%2")
                .arg(it.key(), registryFilePath);
            return false;
        }
        const QJsonArray provenanceHistory = effectiveObject.value(QStringLiteral("provenanceHistory")).toArray();
        QJsonArray normalizedProvenanceHistory;
        bool historyGenerationNormalized = false;
        bool historyCanonicalNormalized = false;
        QSet<QString> provenanceIds;
        int historyIndex = 0;
        for (const QJsonValue& provenance : provenanceHistory) {
            QJsonObject provenanceObject = provenance.toObject();
            const QString historyRasterPath = QDir(projectRoot).absoluteFilePath(
                expectedRoot + QStringLiteral("/dem.tif"));
            const QString historyIdentityPath = QDir(projectRoot).absoluteFilePath(
                expectedRoot + QStringLiteral("/identity.h5"));
            const QString historyMaskPath = QDir(projectRoot).absoluteFilePath(
                expectedRoot + QStringLiteral("/dem_valid_mask.tif"));
            auto normalizeHistoryGeneration = [&historyGenerationNormalized](QJsonObject& fileObject, const QString& path,
                                                                             const QString& expectedHash) {
                const QFileInfo info(path);
                if (!info.isFile() || !fileObject.value(QStringLiteral("size")).isDouble() ||
                    !fileObject.value(QStringLiteral("modifiedMs")).isDouble()) {
                    if (info.isFile() &&
                        fileObject.value(QStringLiteral("size")).isUndefined() &&
                        fileObject.value(QStringLiteral("modifiedMs")).isUndefined() &&
                        QString::fromLatin1(sha256File(info.absoluteFilePath())) == expectedHash) {
                        fileObject.insert(QStringLiteral("size"), static_cast<double>(info.size()));
                        fileObject.insert(QStringLiteral("modifiedMs"), static_cast<double>(info.lastModified().toMSecsSinceEpoch()));
                        historyGenerationNormalized = true;
                    }
                }
            };
            QJsonObject historyRaster = provenanceObject.value(QStringLiteral("dem.tif")).toObject();
            QJsonObject historyIdentity = provenanceObject.value(QStringLiteral("identity.h5")).toObject();
            QJsonObject historyMask = provenanceObject.value(QStringLiteral("dem_valid_mask.tif")).toObject();
            normalizeHistoryGeneration(historyRaster, historyRasterPath,
                                       effectiveObject.value(QStringLiteral("rasterHash")).toString());
            normalizeHistoryGeneration(historyIdentity, historyIdentityPath,
                                       effectiveObject.value(QStringLiteral("identityH5Hash")).toString());
            normalizeHistoryGeneration(historyMask, historyMaskPath,
                                       effectiveObject.value(QStringLiteral("validMaskHash")).toString());
            provenanceObject.insert(QStringLiteral("dem.tif"), historyRaster);
            provenanceObject.insert(QStringLiteral("identity.h5"), historyIdentity);
            provenanceObject.insert(QStringLiteral("dem_valid_mask.tif"), historyMask);
            const QString pinnedId = provenanceObject.value(QStringLiteral("pinnedProvenanceId")).toString();
            const QString runId = provenanceObject.value(QStringLiteral("runId")).toString();
            const QString historyCanonicalHash = provenanceObject.value(QStringLiteral("canonicalMetadataHash")).toString();
            const bool invalid = !provenance.isObject() ||
                provenanceObject.value(QStringLiteral("role")).toString() != QStringLiteral("auxiliary_terrain_dem") ||
                provenanceObject.value(QStringLiteral("resourceId")).toString() != it.key() ||
                pinnedId.isEmpty() || runId != pinnedId || provenanceIds.contains(pinnedId) ||
                !provenanceObject.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("size")).isDouble() ||
                !provenanceObject.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("modifiedMs")).isDouble() ||
                !provenanceObject.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("size")).isDouble() ||
                !provenanceObject.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("modifiedMs")).isDouble() ||
                !provenanceObject.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("size")).isDouble() ||
                !provenanceObject.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("modifiedMs")).isDouble() ||
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
                historyCanonicalNormalized = true;
            }
            normalizedProvenanceHistory.append(provenanceObject);
            provenanceIds.insert(pinnedId);
            ++historyIndex;
        }
        const bool entryNormalized = missingGeneration || historyGenerationNormalized ||
                                     historyCanonicalNormalized;
        if (entryNormalized) {
            QJsonObject migratedEntryObject = effectiveObject;
            migratedEntryObject.insert(QStringLiteral("provenanceHistory"), normalizedProvenanceHistory);
            resourcesForMigration.insert(it.key(), migratedEntryObject);
            anyNormalization = true;
        }
        AuxiliaryDemRegistryEntry entry;
        entry.resourceId = it.key();
        entry.role = effectiveObject.value(QStringLiteral("role")).toString();
        entry.rasterHash = effectiveObject.value(QStringLiteral("rasterHash")).toString();
        entry.identityH5Hash = effectiveObject.value(QStringLiteral("identityH5Hash")).toString();
        entry.validMaskHash = effectiveObject.value(QStringLiteral("validMaskHash")).toString();
        entry.rasterSize = static_cast<qint64>(effectiveObject.value(QStringLiteral("rasterSize")).toDouble());
        entry.rasterModifiedMs = static_cast<qint64>(effectiveObject.value(QStringLiteral("rasterModifiedMs")).toDouble());
        entry.identityH5Size = static_cast<qint64>(effectiveObject.value(QStringLiteral("identityH5Size")).toDouble());
        entry.identityH5ModifiedMs = static_cast<qint64>(effectiveObject.value(QStringLiteral("identityH5ModifiedMs")).toDouble());
        entry.validMaskSize = static_cast<qint64>(effectiveObject.value(QStringLiteral("validMaskSize")).toDouble());
        entry.validMaskModifiedMs = static_cast<qint64>(effectiveObject.value(QStringLiteral("validMaskModifiedMs")).toDouble());
        entry.canonicalMetadataHash = canonicalMetadataHash;
        entry.managedRasterPath = effectiveObject.value(QStringLiteral("managedRasterPath")).toString();
        entry.managedIdentityH5Path = effectiveObject.value(QStringLiteral("managedIdentityH5Path")).toString();
        entry.managedValidMaskPath = effectiveObject.value(QStringLiteral("managedValidMaskPath")).toString();
        entry.metadata = effectiveObject.value(QStringLiteral("metadata")).toObject();
        entry.provenanceHistory = normalizedProvenanceHistory;
        entry.tombstone = effectiveObject.value(QStringLiteral("tombstone")).toBool(false);
        entry.legacyUnverified = false;
        entries.insert(entry.resourceId, entry);
    }
    if (anyNormalization) {
        QJsonObject migratedRoot;
        migratedRoot.insert(QStringLiteral("version"), 1);
        migratedRoot.insert(QStringLiteral("resources"), resourcesForMigration);
        QSaveFile migratedOutput(resourceRegistryPath(projectRoot));
        if (!migratedOutput.open(QIODevice::WriteOnly) ||
            migratedOutput.write(QJsonDocument(migratedRoot).toJson(QJsonDocument::Compact)) < 0 ||
            !migratedOutput.commit()) {
            qWarning() << "DEM resource registry migration failed to persist reconstructed size/mtime;"
                       << "it will be retried on the next load."
                       << migratedOutput.errorString();
        }
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
        entry.rasterSize < 0 || entry.rasterModifiedMs < 0 ||
        entry.identityH5Size < 0 || entry.identityH5ModifiedMs < 0 ||
        entry.validMaskSize < 0 || entry.validMaskModifiedMs < 0 ||
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
            !provenance.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("size")).isDouble() ||
            !provenance.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("modifiedMs")).isDouble() ||
            provenance.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("path")).toString() != QStringLiteral("identity.h5") ||
            provenance.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("sha256")).toString() != entry.identityH5Hash ||
            !provenance.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("size")).isDouble() ||
            !provenance.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("modifiedMs")).isDouble() ||
            provenance.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("path")).toString() != QStringLiteral("dem_valid_mask.tif") ||
            provenance.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("sha256")).toString() != entry.validMaskHash ||
            !provenance.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("size")).isDouble() ||
            !provenance.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("modifiedMs")).isDouble()) {
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
    merged.rasterSize = entry.rasterSize;
    merged.rasterModifiedMs = entry.rasterModifiedMs;
    merged.identityH5Size = entry.identityH5Size;
    merged.identityH5ModifiedMs = entry.identityH5ModifiedMs;
    merged.validMaskSize = entry.validMaskSize;
    merged.validMaskModifiedMs = entry.validMaskModifiedMs;
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
        object.insert(QStringLiteral("rasterSize"), static_cast<double>(it.value().rasterSize));
        object.insert(QStringLiteral("rasterModifiedMs"), static_cast<double>(it.value().rasterModifiedMs));
        object.insert(QStringLiteral("identityH5Size"), static_cast<double>(it.value().identityH5Size));
        object.insert(QStringLiteral("identityH5ModifiedMs"), static_cast<double>(it.value().identityH5ModifiedMs));
        object.insert(QStringLiteral("validMaskSize"), static_cast<double>(it.value().validMaskSize));
        object.insert(QStringLiteral("validMaskModifiedMs"), static_cast<double>(it.value().validMaskModifiedMs));
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

bool invalidateWorkflowAuxiliaryDemLabelBinding(const QString& projectRoot,
                                                const QString& requestedLabel,
                                                QString* errorMessage)
{
    const QString label = normalizedDemLabel(requestedLabel);
    if (label.isEmpty()) return true;
    QMutexLocker locker(&g_resourceRegistryMutex);
    QMap<QString, AuxiliaryDemLabelBinding> labels;
    if (!loadAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    const auto existing = labels.constFind(label);
    if (existing == labels.constEnd()) return true;
    if (existing->mode != AuxiliaryDemLabelMode::WorkflowOutput ||
        (existing->resourceId.isEmpty() && existing->pinnedProvenanceId.isEmpty())) return true;
    AuxiliaryDemLabelBinding binding = *existing;
    binding.resourceId.clear();
    binding.pinnedProvenanceId.clear();
    labels.insert(label, binding);
    if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return false;
    notifyLabelRebound(label);
    notifyLabelTableChanged();
    return true;
}

QStringList reconcileWorkflowAuxiliaryDemLabels(const QString& projectRoot,
                                                const QMap<QString, QString>& declaredLabels,
                                                QString* errorMessage)
{
    QStringList invalidatedLabels;
    QMap<QString, QString> declared;
    for (auto it = declaredLabels.constBegin(); it != declaredLabels.constEnd(); ++it) {
        declared.insert(normalizedDemLabel(it.key()), it.value().trimmed());
    }
    QMutexLocker locker(&g_resourceRegistryMutex);
    QMap<QString, AuxiliaryDemLabelBinding> labels;
    if (!loadAuxiliaryDemLabels(projectRoot, labels, errorMessage)) return invalidatedLabels;
    bool changed = false;
    for (auto it = labels.begin(); it != labels.end(); ++it) {
        AuxiliaryDemLabelBinding& binding = it.value();
        if (binding.mode != AuxiliaryDemLabelMode::WorkflowOutput) continue;
        const auto declaredIt = declared.constFind(it.key());
        const bool isDeclared = declaredIt != declared.constEnd() &&
                                declaredIt.value() == binding.producerIdentity;
        if (isDeclared) continue;
        if (binding.resourceId.isEmpty() && binding.pinnedProvenanceId.isEmpty()) continue;
        binding.resourceId.clear();
        binding.pinnedProvenanceId.clear();
        invalidatedLabels.append(it.key());
        changed = true;
    }
    if (changed) {
        if (!saveAuxiliaryDemLabels(projectRoot, labels, errorMessage)) {
            invalidatedLabels.clear();
            return invalidatedLabels;
        }
        for (const QString& label : invalidatedLabels) notifyLabelRebound(label);
        notifyLabelTableChanged();
    }
    return invalidatedLabels;
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
        object.insert(QStringLiteral("rasterSize"), static_cast<double>(it.value().rasterSize));
        object.insert(QStringLiteral("rasterModifiedMs"), static_cast<double>(it.value().rasterModifiedMs));
        object.insert(QStringLiteral("identityH5Size"), static_cast<double>(it.value().identityH5Size));
        object.insert(QStringLiteral("identityH5ModifiedMs"), static_cast<double>(it.value().identityH5ModifiedMs));
        object.insert(QStringLiteral("validMaskSize"), static_cast<double>(it.value().validMaskSize));
        object.insert(QStringLiteral("validMaskModifiedMs"), static_cast<double>(it.value().validMaskModifiedMs));
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
        object.insert(QStringLiteral("validMaskHash"), it.value().validMaskHash);
        object.insert(QStringLiteral("rasterSize"), static_cast<double>(it.value().rasterSize));
        object.insert(QStringLiteral("rasterModifiedMs"), static_cast<double>(it.value().rasterModifiedMs));
        object.insert(QStringLiteral("identityH5Size"), static_cast<double>(it.value().identityH5Size));
        object.insert(QStringLiteral("identityH5ModifiedMs"), static_cast<double>(it.value().identityH5ModifiedMs));
        object.insert(QStringLiteral("validMaskSize"), static_cast<double>(it.value().validMaskSize));
        object.insert(QStringLiteral("validMaskModifiedMs"), static_cast<double>(it.value().validMaskModifiedMs));
        object.insert(QStringLiteral("canonicalMetadataHash"), it.value().canonicalMetadataHash);
        object.insert(QStringLiteral("managedRasterPath"), it.value().managedRasterPath);
        object.insert(QStringLiteral("managedIdentityH5Path"), it.value().managedIdentityH5Path);
        object.insert(QStringLiteral("managedValidMaskPath"), it.value().managedValidMaskPath);
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

void pruneAuxiliaryDemResources(const QString& projectRoot, int keepCount)
{
    if (keepCount < 1) keepCount = 1;
    const QDir resourcesRoot(QDir(projectRoot).absoluteFilePath(QStringLiteral(".dem_resources")));
    if (!resourcesRoot.exists()) return;

    // 仅处理内容寻址的受管 DEM 资源目录，避免误删其他目录。
    QFileInfoList managedDirs;
    const QFileInfoList entries = resourcesRoot.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo& info : entries) {
        if (info.fileName().startsWith(QStringLiteral("auxiliary_terrain_dem-"))) {
            managedDirs.append(info);
        }
    }
    if (managedDirs.size() <= keepCount) return;

    // 以资源内 dem.tif（缺失时回退目录）的修改时间排序，最新在前。
    std::sort(managedDirs.begin(), managedDirs.end(), [](const QFileInfo& a, const QFileInfo& b) {
        const QFileInfo tifA(a.absoluteFilePath() + QStringLiteral("/dem.tif"));
        const QFileInfo tifB(b.absoluteFilePath() + QStringLiteral("/dem.tif"));
        return (tifA.isFile() ? tifA.lastModified() : a.lastModified()) >
               (tifB.isFile() ? tifB.lastModified() : b.lastModified());
    });

    for (int i = keepCount; i < managedDirs.size(); ++i) {
        const QString resourceId = managedDirs[i].fileName();
        const QString resourcePath = managedDirs[i].absoluteFilePath();
        InSARLogManager::LogInfo("NodeUtils",
            QStringLiteral("清理旧 DEM 资源以限制保留份数（%1/%2）：%3")
                .arg(keepCount).arg(managedDirs.size()).arg(resourceId));
        if (QDir(resourcePath).removeRecursively()) {
            QString ignored;
            removeAuxiliaryDemRegistryEntry(projectRoot, resourceId, &ignored);
        }
    }
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

void emitAuxiliaryDemLabelTableChanged()
{
    notifyLabelTableChanged();
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

bool ensureProjectGeoidModelInstalled(const QString& projectRoot, QString* errorMessage)
{
    QString root = projectRoot.trimmed();
    if (root.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("工程根目录路径为空。");
        return false;
    }
    if (QFileInfo(root).isFile()) {
        root = QFileInfo(root).path();
    }

    static QMutex s_installMutex;
    QMutexLocker locker(&s_installMutex);

    const QDir geoidDir(QDir(root).absoluteFilePath(QStringLiteral(".dem_resources/geoid_models")));
    const QString targetTif = geoidDir.absoluteFilePath(QStringLiteral("us_nga_egm96_15.tif"));
    const QString targetRegistry = geoidDir.absoluteFilePath(QStringLiteral("registry.json"));
    const QString expectedHash = QStringLiteral("db493027562c9b004d7220fa881f5603adada4e1c5029b933fa7de4547b0e78d");

    // 检查目标文件是否已存在且哈希一致
    bool tifValid = false;
    if (QFileInfo::exists(targetTif)) {
        const QString currentHash = QString::fromLatin1(sha256File(targetTif)).toLower();
        if (currentHash == expectedHash) {
            tifValid = true;
        } else {
            QFile::remove(targetTif);
        }
    }

    bool registryValid = false;
    if (QFileInfo::exists(targetRegistry)) {
        QFile regFile(targetRegistry);
        if (regFile.open(QIODevice::ReadOnly)) {
            const QJsonObject regObj = QJsonDocument::fromJson(regFile.readAll()).object();
            if (regObj.value(QStringLiteral("verticalDatums")).toObject().contains(QStringLiteral("EGM96"))) {
                registryValid = true;
            }
        }
        if (!registryValid) {
            QFile::remove(targetRegistry);
        }
    }

    if (tifValid && registryValid) {
        return true;
    }

    // 确保目标目录存在
    if (!geoidDir.exists() && !QDir().mkpath(geoidDir.absolutePath())) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建大地水准面模型目录：%1").arg(geoidDir.absolutePath());
        return false;
    }

    // 安装/修复 us_nga_egm96_15.tif
    if (!tifValid) {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList tifCandidates = {
            QStringLiteral(":/SatExplorer/geoid/us_nga_egm96_15.tif"),
            QDir(appDir).absoluteFilePath(QStringLiteral("../resources/geoid/us_nga_egm96_15.tif")),
            QDir(appDir).absoluteFilePath(QStringLiteral("resources/geoid/us_nga_egm96_15.tif")),
            QDir(appDir).absoluteFilePath(QStringLiteral("geoid/us_nga_egm96_15.tif"))
        };

        QString sourceTif;
        for (const QString& cand : tifCandidates) {
            if (QFile::exists(cand)) {
                sourceTif = cand;
                break;
            }
        }

        if (sourceTif.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("未找到内置或本地 EGM96 大地水准面模型文件 (us_nga_egm96_15.tif)。");
            return false;
        }

        if (QFile::exists(targetTif)) {
            QFile::remove(targetTif);
        }

        if (!QFile::copy(sourceTif, targetTif)) {
            if (errorMessage) *errorMessage = QStringLiteral("复制 EGM96 大地水准面模型到工程目录失败：%1").arg(targetTif);
            return false;
        }

        // 恢复读写权限
        QFile::setPermissions(targetTif, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                         QFileDevice::ReadUser | QFileDevice::WriteUser |
                                         QFileDevice::ReadGroup | QFileDevice::ReadOther);

        const QString installedHash = QString::fromLatin1(sha256File(targetTif)).toLower();
        if (installedHash != expectedHash) {
            QFile::remove(targetTif);
            if (errorMessage) *errorMessage = QStringLiteral("安装的 EGM96 大地水准面模型哈希校验失败。");
            return false;
        }
    }

    // 安装/修复 registry.json
    if (!registryValid) {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList regCandidates = {
            QStringLiteral(":/SatExplorer/geoid/registry.json"),
            QDir(appDir).absoluteFilePath(QStringLiteral("../resources/geoid/registry.json")),
            QDir(appDir).absoluteFilePath(QStringLiteral("resources/geoid/registry.json")),
            QDir(appDir).absoluteFilePath(QStringLiteral("geoid/registry.json"))
        };

        QString sourceReg;
        for (const QString& cand : regCandidates) {
            if (QFile::exists(cand)) {
                sourceReg = cand;
                break;
            }
        }

        bool installedReg = false;
        if (!sourceReg.isEmpty()) {
            if (QFile::exists(targetRegistry)) {
                QFile::remove(targetRegistry);
            }
            if (QFile::copy(sourceReg, targetRegistry)) {
                QFile::setPermissions(targetRegistry, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                     QFileDevice::ReadUser | QFileDevice::WriteUser |
                                                     QFileDevice::ReadGroup | QFileDevice::ReadOther);
                installedReg = true;
            }
        }

        if (!installedReg) {
            static const char s_defaultRegistryJson[] =
                "{\"version\":1,\"verticalDatums\":{\"EGM96\":{\"id\":\"proj-us_nga_egm96_15-20200128\","
                "\"managedPath\":\".dem_resources/geoid_models/us_nga_egm96_15.tif\","
                "\"sha256\":\"db493027562c9b004d7220fa881f5603adada4e1c5029b933fa7de4547b0e78d\","
                "\"source\":\"https://cdn.proj.org/us_nga_egm96_15.tif\","
                "\"sourceVersion\":\"PROJ CDN object version dYTifQAFpLE.qztPUkWH0BO4idouus8.\"}}}";
            QSaveFile sf(targetRegistry);
            if (sf.open(QIODevice::WriteOnly)) {
                sf.write(s_defaultRegistryJson);
                if (sf.commit()) {
                    QFile::setPermissions(targetRegistry, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                         QFileDevice::ReadUser | QFileDevice::WriteUser |
                                                         QFileDevice::ReadGroup | QFileDevice::ReadOther);
                    installedReg = true;
                }
            }
        }

        if (!installedReg) {
            if (errorMessage) *errorMessage = QStringLiteral("无法写入大地水准面注册清单 (registry.json)。");
            return false;
        }
    }

    InSARLogManager::LogInfo("NodeUtils", QStringLiteral("成功安装/校验工程 EGM96 大地水准面模型：%1").arg(targetTif));
    return true;
}

bool bindRegisteredGeoidModel(const QString& projectRoot, const QJsonObject& metadata,
                              AuxiliaryDemBinding& binding, QString* errorMessage)
{
    // 自愈兜底：若工程受管大地水准面模型或注册表缺失，自动从内嵌资源部署
    ensureProjectGeoidModelInstalled(projectRoot, nullptr);

    // Registry metadata is the sole configuration surface for v2 geoid data.
    // A relative, managed path makes the model reproducible without scanning
    // project directories or relying on a machine-global installation.
    QJsonObject geoid = metadata.value(QStringLiteral("geoidModel")).toObject();
    if (geoid.isEmpty()) {
        // Backward-compatible resource metadata may predate v2.  The only
        // permitted fallback is this exact, project-managed registry file;
        // there is no directory enumeration and no machine-global default.
        QFile registryFile(QDir(projectRoot).absoluteFilePath(
            QStringLiteral(".dem_resources/geoid_models/registry.json")));
        if (registryFile.open(QIODevice::ReadOnly)) {
            const QJsonObject registry = QJsonDocument::fromJson(registryFile.readAll()).object();
            geoid = registry.value(QStringLiteral("verticalDatums")).toObject()
                .value(metadata.value(QStringLiteral("verticalDatum")).toString()).toObject();
        }
    }
    const QString id = geoid.value(QStringLiteral("id")).toString().trimmed();
    const QString managedPath = QDir::cleanPath(geoid.value(QStringLiteral("managedPath")).toString());
    const QString expectedHash = geoid.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    if (id.isEmpty() || managedPath.isEmpty() || QDir::isAbsolutePath(managedPath) ||
        managedPath == QStringLiteral(".") || managedPath == QStringLiteral("..") ||
        managedPath.startsWith(QStringLiteral("../")) ||
        !managedPath.startsWith(QStringLiteral(".dem_resources/geoid_models/")) ||
        expectedHash.size() != 64) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "Auxiliary DEM registry metadata requires geoidModel {id, managedPath, sha256}; no implicit geoid lookup is permitted.");
        return false;
    }
    const QFileInfo geoidFile(QDir(projectRoot).absoluteFilePath(managedPath));
    if (!geoidFile.isFile() || !geoidFile.isReadable() ||
        QString::fromLatin1(fileSha256(geoidFile.absoluteFilePath())).compare(expectedHash, Qt::CaseInsensitive) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Registered geoid model is missing or its immutable hash no longer matches.");
        return false;
    }
    binding.geoidModelPath = geoidFile.absoluteFilePath();
    binding.geoidModelHash = expectedHash;
    binding.geoidModelId = id;
    return true;
}

bool resolveAuxiliaryDemBinding(const QString& projectRoot,
                                const QtNodes::AuxiliaryDemData& data,
                                AuxiliaryDemBinding& binding,
                                QString* errorMessage,
                                const QJsonObject& inputGeometry,
                                bool requireGeoidModel)
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
    const bool registryLoaded = loadAuxiliaryDemRegistry(projectRoot, registry, errorMessage);
    const auto registryIt = registry.constFind(data.resourceId());
    const bool resourceFound = registryIt != registry.constEnd();
    const bool resourceTombstoned = resourceFound && registryIt->tombstone;
    if (!registryLoaded || !resourceFound || resourceTombstoned) {
        InSARLogManager::LogDebug("NodeUtils",
            QStringLiteral("Auxiliary DEM registry lookup failed: projectRoot=%1, registryPath=%2, requestedResourceId=%3, requestedProvenanceId=%4, registryLoaded=%5, resourceFound=%6, tombstone=%7, registeredResourceIds=[%8], resolverError=%9.")
                .arg(projectRoot,
                     resourceRegistryPath(projectRoot),
                     data.resourceId(),
                     data.pinnedProvenanceId())
                .arg(registryLoaded)
                .arg(resourceFound)
                .arg(resourceTombstoned)
                .arg(registry.keys().join(QStringLiteral(",")),
                     errorMessage ? *errorMessage : QString()),
            QStringLiteral("dem.binding"));
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Auxiliary DEM resource is not registered or is tombstoned.");
        return false;
    }
    const AuxiliaryDemRegistryEntry entry = *registryIt;
    if (entry.legacyUnverified) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM resource is legacy and lacks a verifiable validity mask; regenerate it before use.");
        return false;
    }
    const QString expectedRaster = QDir(projectRoot).absoluteFilePath(entry.managedRasterPath);
    const QString expectedIdentity = QDir(projectRoot).absoluteFilePath(entry.managedIdentityH5Path);
    const QString expectedMask = QDir(projectRoot).absoluteFilePath(entry.managedValidMaskPath);
    const QFileInfo mask(expectedMask);
    if (QDir::cleanPath(expectedRaster).compare(QDir::cleanPath(raster.absoluteFilePath()), Qt::CaseInsensitive) != 0 ||
        QDir::cleanPath(expectedIdentity).compare(QDir::cleanPath(identity.absoluteFilePath()), Qt::CaseInsensitive) != 0 ||
        !mask.isFile() || !mask.isReadable()) {
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM entity paths are not the registry-managed paths.");
        return false;
    }
    if (!matchesPersistedFileGeneration(raster, entry.rasterSize, entry.rasterModifiedMs) ||
        !matchesPersistedFileGeneration(identity, entry.identityH5Size, entry.identityH5ModifiedMs) ||
        !matchesPersistedFileGeneration(mask, entry.validMaskSize, entry.validMaskModifiedMs)) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::ContentIntegrityFailed);
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM managed file generation does not match the registry.");
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
    if (!auxiliaryDemCoversInput(metadata, inputGeometry) ||
        (requireGeoidModel && !bindRegisteredGeoidModel(projectRoot, metadata, binding, errorMessage))) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Auxiliary DEM does not cover the trusted input geometry or match its CRS/resolution/vertical datum.");
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
        manifestObject.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("path")).toString() != QStringLiteral("dem_valid_mask.tif") ||
        manifestObject.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("sha256")).toString() != entry.rasterHash ||
        manifestObject.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("sha256")).toString() != entry.identityH5Hash ||
        manifestObject.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("sha256")).toString() != entry.validMaskHash ||
        !matchesPersistedFileGeneration(raster, manifestObject.value(QStringLiteral("dem.tif")).toObject()) ||
        !matchesPersistedFileGeneration(identity, manifestObject.value(QStringLiteral("identity.h5")).toObject()) ||
        !matchesPersistedFileGeneration(mask, manifestObject.value(QStringLiteral("dem_valid_mask.tif")).toObject()) ||
        manifestObject.value(QStringLiteral("canonicalMetadataHash")).toString() != entry.canonicalMetadataHash) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::ContentIntegrityFailed);
        if (errorMessage) *errorMessage = QStringLiteral("Auxiliary DEM provenance manifest does not match registry hashes.");
        return false;
    }
    binding.rasterPath = raster.absoluteFilePath();
    binding.identityH5Path = identity.absoluteFilePath();
    binding.validMaskPath = mask.absoluteFilePath();
    binding.resourceId = data.resourceId();
    binding.pinnedProvenanceId = data.pinnedProvenanceId();
    binding.rasterHash = entry.rasterHash;
    binding.identityH5Hash = entry.identityH5Hash;
    binding.validMaskHash = entry.validMaskHash;
    binding.canonicalMetadataHash = entry.canonicalMetadataHash;
    binding.fromReference = false;
    return true;
}

bool resolveAuxiliaryDemBinding(const QString& projectRoot,
                                const QtNodes::AuxiliaryDemReferenceData& data,
                                AuxiliaryDemBinding& binding,
                                QString* errorMessage,
                                const QJsonObject& inputGeometry,
                                bool requireGeoidModel)
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
    if (!auxiliaryDemCoversInput(metadata, inputGeometry) ||
        (requireGeoidModel && !bindRegisteredGeoidModel(projectRoot, metadata, binding, errorMessage))) {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Referenced DEM does not cover the trusted input geometry or match its CRS/resolution/vertical datum.");
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
    const QFileInfo mask(resourceDir.absoluteFilePath(QStringLiteral("dem_valid_mask.tif")));
    const QFileInfo manifest(resourceDir.absoluteFilePath(
        QStringLiteral("provenance_%1.json").arg(data.pinnedProvenanceId())));
    if (!manifest.isFile()) {
        notifyResourceChange(data.resourceId(), data.pinnedProvenanceId(), ResourceChangeKind::PinnedProvenanceMissing);
        if (errorMessage) *errorMessage = QStringLiteral("Referenced DEM pinned provenance manifest is unavailable.");
        return false;
    }
    if (!raster.isFile() || !identity.isFile() || !mask.isFile()) {
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
        return entry.value(QStringLiteral("path")).toString() == file.fileName() &&
            matchesPersistedFileGeneration(file, entry);
    };
    if (manifestObject.value(QStringLiteral("resourceId")).toString() != data.resourceId() ||
        manifestObject.value(QStringLiteral("pinnedProvenanceId")).toString() != data.pinnedProvenanceId() ||
        manifestObject.value(QStringLiteral("runId")).toString().isEmpty() ||
        manifestObject.value(QStringLiteral("runId")).toString() != data.pinnedProvenanceId() ||
        manifest.fileName() != QStringLiteral("provenance_%1.json").arg(data.pinnedProvenanceId()) ||
        manifestObject.value(QStringLiteral("canonicalMetadataHash")).toString() != registryEntry.canonicalMetadataHash ||
        !verifyManagedFile(raster, manifestObject.value(QStringLiteral("dem.tif"))) ||
        !verifyManagedFile(identity, manifestObject.value(QStringLiteral("identity.h5"))) ||
        !verifyManagedFile(mask, manifestObject.value(QStringLiteral("dem_valid_mask.tif"))) ||
        manifestObject.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("sha256")).toString() != registryEntry.rasterHash ||
        manifestObject.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("sha256")).toString() != registryEntry.identityH5Hash ||
        manifestObject.value(QStringLiteral("dem_valid_mask.tif")).toObject().value(QStringLiteral("sha256")).toString() != registryEntry.validMaskHash) {
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
    binding.validMaskPath = mask.absoluteFilePath();
    binding.resourceId = data.resourceId();
    binding.pinnedProvenanceId = data.pinnedProvenanceId();
    binding.rasterHash = registryEntry.rasterHash;
    binding.identityH5Hash = registryEntry.identityH5Hash;
    binding.validMaskHash = registryEntry.validMaskHash;
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
                                   const QJsonObject& inputGeometry,
                                   bool requireGeoidModel)
{
    if (!entity && !reference) {
        // 注意：entity / reference 两个空指针语义是"当前没有任何辅助 DEM 输入变体"，
        // 而不是"可以无输入地重校验"。本函数必须至少拿到一个活输入对象才能解析绑定，
        // 因此它不适用于工程加载等输入尚未传播到位的场景；那种场景请改为
        // 用已提交 provenance 构造 AuxiliaryDemReferenceData 后调用
        // resolveAuxiliaryDemBinding 的 reference 重载（纯磁盘解析，见 DemNode 的恢复路径）。
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
    const bool resolved = entity ? resolveAuxiliaryDemBinding(projectRoot, *entity, binding, errorMessage, inputGeometry,
                                                               requireGeoidModel)
                                 : resolveAuxiliaryDemBinding(projectRoot, *reference, binding, errorMessage, inputGeometry,
                                                               requireGeoidModel);
    if (!resolved || !expectedBinding) return resolved;
    if (binding.resourceId != expectedBinding->resourceId ||
        binding.pinnedProvenanceId != expectedBinding->pinnedProvenanceId ||
        binding.rasterHash != expectedBinding->rasterHash ||
         binding.identityH5Hash != expectedBinding->identityH5Hash ||
        binding.validMaskHash != expectedBinding->validMaskHash ||
         binding.canonicalMetadataHash != expectedBinding->canonicalMetadataHash ||
         binding.geoidModelPath != expectedBinding->geoidModelPath ||
         binding.geoidModelHash != expectedBinding->geoidModelHash ||
         binding.geoidModelId != expectedBinding->geoidModelId) {
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
    const bool expectGeoid = !snapshot.binding.geoidModelPath.isEmpty();
    return revalidateAuxiliaryDemBinding(projectRoot, entity, reference, binding, errorMessage,
                                         &snapshot.binding, snapshot.inputGeometry, expectGeoid);
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
        for (const QJsonValue& output : manifestObject.value(QStringLiteral("outputs")).toArray()) {
            const QJsonObject item = output.toObject();
            if (item.value(QStringLiteral("name")).toString() == info.fileName() &&
                item.value(QStringLiteral("sha256")).toString().size() == 64 &&
                matchesPersistedFileGeneration(info, item)) {
                listed = true;
                break;
            }
        }
        if (!listed) {
            if (errorMessage) *errorMessage = QStringLiteral("InSAR DEM H5 generation is missing from or does not match its committed manifest: %1").arg(path);
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

bool writeStagedProductDescriptorToH5(const QString& filePath, const QJsonObject& descriptor,
                                      QString* errorMessage)
{
    const QByteArray path = filePath.toUtf8();
    const QByteArray json = QJsonDocument(descriptor).toJson(QJsonDocument::Compact);
    // Staged H5 files may already carry the descriptor from an upstream node
    // (for example, orbit application copies an imported H5 before promotion).
    // createString() intentionally fails when the dataset exists, so use the
    // replacement API for both first-write and update cases.
    const int result = Hdf5IO::writeString(path.constData(), kProductDescriptorDataset, json.constData());
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

QByteArray fileSha256(const QString& path)
{
    return sha256File(path);
}

bool writeProductDescriptorToH5(const QString& filePath, const QJsonObject& descriptor,
                                QString* errorMessage)
{
    return writeStagedProductDescriptorToH5(filePath, descriptor, errorMessage);
}

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

namespace {
bool recoverDescriptorGeometryMigration(const QDir& root, const QString& nodeName,
                                        QString* errorMessage);
}

bool recoverOutputTransaction(const QString& projectRoot, const QString& nodeName, QString* errorMessage)
{
    QDir root(projectRoot);
    if (!root.exists() || !isDirectProjectChild(root.absolutePath(), nodeName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid output transaction recovery target.");
        return false;
    }
    if (QFileInfo::exists(root.absoluteFilePath(QStringLiteral(".descriptor_geometry_migration.lock")))) {
        if (errorMessage) *errorMessage = QStringLiteral("A descriptor geometry migration holds the project lease.");
        return false;
    }
    QString migrationRecoveryError;
    if (!recoverDescriptorGeometryMigration(root, nodeName, &migrationRecoveryError)) {
        if (errorMessage) *errorMessage = migrationRecoveryError;
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
                   (action == QStringLiteral("installed") ||
                    (action == QStringLiteral("reused") &&
                     journal.value(QStringLiteral("baseRegistryHash")).toString().isEmpty()))) {
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
        const bool hasPreviousJournal = !journal.value(QStringLiteral("previousCommittedJournal")).toObject().isEmpty();
        if (hasPreviousFinal && hasPreviousJournal &&
            !validatePreviousCommittedFinal(root, nodeName, journal, errorMessage)) {
            return false;
        }
        if (!removeStaging()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot remove isolated staging directory: %1").arg(stagingPath);
            return false;
        }
        if (hasPreviousJournal)
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
    if (QFileInfo::exists(root.absoluteFilePath(QStringLiteral(".descriptor_geometry_migration.lock")))) {
        if (errorMessage) *errorMessage = QStringLiteral("A descriptor geometry migration holds the project lease.");
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
    QString migrationRecoveryError;
    if (!recoverDescriptorGeometryMigration(root, nodeName, &migrationRecoveryError)) {
        if (errorMessage) *errorMessage = migrationRecoveryError;
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
            info.fileName().isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("Expected output is not a direct child of the node output directory: %1").arg(expectedPath);
            return false;
        }
        if (uniqueNames.contains(info.fileName())) {
            if (errorMessage) *errorMessage = QStringLiteral("Duplicate expected output file name in transaction: %1").arg(info.fileName());
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
            // 预览 JPG 是派生产物，允许不在声明清单中（Worker 可直接写入预生成预览，
            // 避免最终化阶段再从 H5 全量读取）。
            if (entry.suffix().compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0) {
                continue;
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral("Staged output contains an artifact outside the expected manifest: %1")
                    .arg(entry.absoluteFilePath());
            }
            return false;
        }
    }

    QJsonArray files;
    for (const QString& name : transaction.expectedFileNames) {
        const QString stagedPath = staging.absoluteFilePath(name);
        // Worker 已写入 descriptor 并预计算哈希时（仅 DEM 路径），跳过重复的
        // descriptor 写入与整文件哈希，避免最终化阶段再次读取数百 MB 的 H5。
        const auto precomputedIt = transaction.precomputedOutputHashes.constFind(name);
        const bool hasPrecomputedHash = precomputedIt != transaction.precomputedOutputHashes.constEnd();
        // 写入 descriptor 前先预检文件存在且非空，缺失产物直接报清晰错误，
        // 避免落到 descriptor 写入失败这类不够直观的消息上。
        const QFileInfo preflight(stagedPath);
        if (!preflight.isFile() || preflight.size() <= 0) {
            if (errorMessage) *errorMessage = QStringLiteral("Missing or empty staged output: %1").arg(stagedPath);
            return false;
        }
        // 嵌入 product descriptor 会就地改写 H5（约 +2.4 KB），因此必须先完成该
        // 写入再对文件取 stat，保证 outputs 清单中记录的 size/修改时间与哈希一致。
        if (preflight.suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0 &&
            !hasPrecomputedHash &&
            !writeStagedProductDescriptorToH5(stagedPath, transaction.productDescriptor, errorMessage)) {
            return false;
        }
        const QFileInfo info(stagedPath);
        if (!validateOutputFile(info, errorMessage)) return false;
        QJsonObject file;
        file.insert(QStringLiteral("name"), name);
        file.insert(QStringLiteral("size"), static_cast<double>(info.size()));
        file.insert(QStringLiteral("modifiedMs"), static_cast<double>(info.lastModified().toMSecsSinceEpoch()));
        QByteArray artifactHash;
        if (hasPrecomputedHash) {
            artifactHash = precomputedIt.value();
        } else {
            QFile artifact(stagedPath);
            if (!artifact.open(QIODevice::ReadOnly)) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot hash staged output: %1").arg(stagedPath);
                return false;
            }
            artifactHash = sha256File(stagedPath);
        }
        if (artifactHash.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot hash staged output: %1").arg(stagedPath);
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
        NodeUtils::Hdf5Locker locker(h5Path, 50);
        if (!locker.isLocked()) {
            if (errorMessage) *errorMessage = QStringLiteral("获取 H5 文件锁超时 (文件忙): %1").arg(h5Path);
            return false;
        }
        for (const QString& dataset : requiredDatasets) {
            if (dataset.isEmpty()) {
                if (errorMessage) *errorMessage = QStringLiteral("Staged H5 validation contains an empty dataset name.");
                return false;
            }
            // 仅探测数据集尺寸元数据，避免对数百 MB 的 H5 输出做全量读取，
            // 显著缩短最终化阶段的阻塞时间。数据由 Worker 写入并已校验，
            // 此处只需确认数据集存在且非空。
            int rows = 0;
            int columns = 0;
            QString probeError;
            if (!probeH5DatasetMetadata(h5Path, dataset, &rows, &columns, &probeError) ||
                rows <= 0 || columns <= 0) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Staged H5 output is missing required dataset '%1': %2 (%3)")
                        .arg(dataset, h5Path, probeError);
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
                    (transaction.resourceAction == QStringLiteral("reused") &&
                     transaction.baseRegistryHash.isEmpty()))) {
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

namespace {

const char kDescriptorGeometryMigrationFile[] = ".descriptor_geometry_migration.json";
const char kDescriptorGeometryMigrationKind[] = "descriptor_geometry_provenance_migration_v1";

bool descriptorGeometryMigrationIsIncomplete(const QDir& root, const QString& nodeName,
                                             QString* errorMessage)
{
    const QString migrationPath = QDir(root.absoluteFilePath(nodeName)).absoluteFilePath(
        QString::fromLatin1(kDescriptorGeometryMigrationFile));
    if (!QFileInfo::exists(migrationPath)) return false;
    QFile migrationFile(migrationPath);
    if (!migrationFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration record cannot be inspected.");
        return true;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(migrationFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration record is invalid.");
        return true;
    }
    const QString status = document.object().value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("completed") || status == QStringLiteral("rolled_back")) return false;
    if (errorMessage) *errorMessage = QStringLiteral(
        "Descriptor geometry migration is incomplete; normal recovery must run before the artifact can be inspected.");
    return true;
}

bool recoverDescriptorGeometryMigration(const QDir& root, const QString& nodeName,
                                        QString* errorMessage)
{
    const QDir outputDirectory(root.absoluteFilePath(nodeName));
    const QString migrationPath = outputDirectory.absoluteFilePath(
        QString::fromLatin1(kDescriptorGeometryMigrationFile));
    if (!QFileInfo::exists(migrationPath)) return true;

    QFile migrationFile(migrationPath);
    if (!migrationFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot inspect descriptor geometry migration record: %1")
            .arg(migrationPath);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument migrationDocument = QJsonDocument::fromJson(migrationFile.readAll(), &parseError);
    migrationFile.close();
    if (parseError.error != QJsonParseError::NoError || !migrationDocument.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration record is invalid: %1")
            .arg(migrationPath);
        return false;
    }

    QJsonObject record = migrationDocument.object();
    const QString status = record.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("completed") || status == QStringLiteral("rolled_back")) return true;

    const QString id = record.value(QStringLiteral("id")).toString();
    const QUuid migrationId(id);
    const QString normalizedId = migrationId.toString(QUuid::WithoutBraces);
    const QString targetPath = QDir::cleanPath(record.value(QStringLiteral("targetH5")).toString());
    const QFileInfo requestedTarget(targetPath);
    const QString targetName = requestedTarget.fileName();
    const QString expectedTarget = QDir::cleanPath(outputDirectory.absoluteFilePath(targetName));
    const QString expectedStaged = QDir::cleanPath(outputDirectory.absoluteFilePath(
        QStringLiteral(".%1.descriptor-geometry-staging-%2").arg(targetName, normalizedId)));
    const QString expectedBackup = QDir::cleanPath(outputDirectory.absoluteFilePath(
        QStringLiteral(".%1.descriptor-geometry-backup-%2").arg(targetName, normalizedId)));
    const QString expectedManifestBackup = QDir::cleanPath(outputDirectory.absoluteFilePath(
        QStringLiteral(".%1.descriptor-geometry-backup-%2.json")
            .arg(QString::fromLatin1(kOutputManifestFile), normalizedId)));
    const QString expectedJournalBackup = QDir::cleanPath(root.absoluteFilePath(
        QStringLiteral(".%1.descriptor-geometry-backup-%2.json").arg(nodeName, normalizedId)));
    const auto matchesExpected = [](const QString& actual, const QString& expected) {
        return QDir::cleanPath(QFileInfo(actual).absoluteFilePath()) == expected;
    };
    if (record.value(QStringLiteral("version")).toInt() != 1 ||
        record.value(QStringLiteral("kind")).toString() != QString::fromLatin1(kDescriptorGeometryMigrationKind) ||
        migrationId.isNull() || id != normalizedId || targetName.isEmpty() ||
        requestedTarget.suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) != 0 ||
        !matchesExpected(targetPath, expectedTarget) ||
        !matchesExpected(record.value(QStringLiteral("stagedH5")).toString(), expectedStaged) ||
        !matchesExpected(record.value(QStringLiteral("backupH5")).toString(), expectedBackup) ||
        !matchesExpected(record.value(QStringLiteral("manifestBackup")).toString(), expectedManifestBackup) ||
        !matchesExpected(record.value(QStringLiteral("journalBackup")).toString(), expectedJournalBackup) ||
        record.value(QStringLiteral("preMigrationSha256")).toString().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration record has unsafe paths or identity.");
        return false;
    }

    const QString backupHash = record.value(QStringLiteral("preMigrationSha256")).toString();
    const QFileInfo backupInfo(expectedBackup);
    const QFileInfo targetInfo(expectedTarget);
    if (backupInfo.isFile()) {
        if (QString::fromLatin1(sha256File(expectedBackup)) != backupHash) {
            if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration backup hash is invalid.");
            return false;
        }
        const QString rejectedPath = expectedStaged + QStringLiteral(".recovery-rejected");
        if (targetInfo.exists() && !QFileInfo::exists(rejectedPath) && !QFile::rename(expectedTarget, rejectedPath)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot isolate partially migrated H5 during descriptor migration recovery.");
            return false;
        }
        if (!QFileInfo::exists(expectedTarget) && !QFile::rename(expectedBackup, expectedTarget)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot restore original H5 during descriptor migration recovery.");
            return false;
        }
    } else {
        QFile manifestBackupFile(expectedManifestBackup);
        QFile journalBackupFile(expectedJournalBackup);
        if (!targetInfo.isFile() || QString::fromLatin1(sha256File(expectedTarget)) != backupHash ||
            !manifestBackupFile.open(QIODevice::ReadOnly) || !journalBackupFile.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration cannot safely restore its original H5.");
            return false;
        }
        const QJsonObject originalManifest = QJsonDocument::fromJson(manifestBackupFile.readAll()).object();
        const QJsonObject originalJournal = QJsonDocument::fromJson(journalBackupFile.readAll()).object();
        manifestBackupFile.close();
        journalBackupFile.close();
        QFile currentManifestFile(outputDirectory.absoluteFilePath(QString::fromLatin1(kOutputManifestFile)));
        QFile currentJournalFile(QDir(transactionDirectoryPath(root.absolutePath())).absoluteFilePath(
            nodeName + QStringLiteral(".json")));
        if (!currentManifestFile.open(QIODevice::ReadOnly) || !currentJournalFile.open(QIODevice::ReadOnly) ||
            QJsonDocument::fromJson(currentManifestFile.readAll()).object() != originalManifest ||
            QJsonDocument::fromJson(currentJournalFile.readAll()).object() != originalJournal) {
            if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration has no verified completed rollback state.");
            return false;
        }
    }

    QJsonObject manifestBackup;
    QJsonObject journalBackup;
    QFile manifestFile(expectedManifestBackup);
    QFile journalFile(expectedJournalBackup);
    if (!manifestFile.open(QIODevice::ReadOnly) || !journalFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration backup metadata is unavailable.");
        return false;
    }
    const QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    const QJsonDocument journalDocument = QJsonDocument::fromJson(journalFile.readAll(), &parseError);
    manifestFile.close();
    journalFile.close();
    if (!manifestDocument.isObject() || !journalDocument.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Descriptor geometry migration backup metadata is invalid.");
        return false;
    }
    manifestBackup = manifestDocument.object();
    journalBackup = journalDocument.object();
    QString writeError;
    const QString manifestPath = outputDirectory.absoluteFilePath(QString::fromLatin1(kOutputManifestFile));
    const QString journalPath = QDir(transactionDirectoryPath(root.absolutePath())).absoluteFilePath(nodeName + QStringLiteral(".json"));
    if (!writeJsonAtomically(manifestPath, manifestBackup, &writeError) ||
        !writeJsonAtomically(journalPath, journalBackup, &writeError)) {
        if (errorMessage) *errorMessage = writeError;
        return false;
    }
    if (QFileInfo::exists(expectedStaged) && !QFile::remove(expectedStaged)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot remove staged descriptor migration H5.");
        return false;
    }
    record.insert(QStringLiteral("status"), QStringLiteral("rolled_back"));
    record.insert(QStringLiteral("recoveredUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!writeJsonAtomically(migrationPath, record, &writeError)) {
        if (errorMessage) *errorMessage = writeError;
        return false;
    }
    InSARLogManager::LogWarning("NodeUtils", QString("Recovered interrupted descriptor geometry migration for node: %1")
        .arg(nodeName));
    return true;
}

} // namespace

bool loadCommittedOutputManifest(const QString& projectRoot,
                                 const QString& nodeName,
                                 QStringList& outputPaths,
                                 QString* errorMessage)
{
    outputPaths.clear();
    const QDir root(projectRoot);
    if (!root.exists() || !isDirectProjectChild(root.absolutePath(), nodeName)) return false;
    if (QFileInfo::exists(root.absoluteFilePath(QStringLiteral(".descriptor_geometry_migration.lock")))) {
        if (errorMessage) *errorMessage = QStringLiteral("A descriptor geometry migration holds the project lease.");
        return false;
    }
    QString migrationRecoveryError;
    if (!recoverDescriptorGeometryMigration(root, nodeName, &migrationRecoveryError)) {
        if (errorMessage) *errorMessage = migrationRecoveryError;
        return false;
    }
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

bool loadCommittedOutputManifestReadOnly(const QString& projectRoot,
                                         const QString& nodeName,
                                         QStringList& outputPaths,
                                         QString& runId,
                                         std::uint64_t& executionRevision,
                                         int& manifestVersion,
                                         QString* errorMessage)
{
    outputPaths.clear();
    runId.clear();
    executionRevision = 0;
    manifestVersion = 0;
    const QDir root(projectRoot);
    if (!root.exists() || !isDirectProjectChild(root.absolutePath(), nodeName)) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output target is unavailable.");
        return false;
    }
    if (QFileInfo::exists(root.absoluteFilePath(QStringLiteral(".descriptor_geometry_migration.lock")))) {
        if (errorMessage) *errorMessage = QStringLiteral("A descriptor geometry migration holds the project lease.");
        return false;
    }
    QString migrationStateError;
    if (descriptorGeometryMigrationIsIncomplete(root, nodeName, &migrationStateError)) {
        if (errorMessage) *errorMessage = migrationStateError;
        return false;
    }

    const QString journalPath = QDir(transactionDirectoryPath(root.absolutePath()))
        .absoluteFilePath(nodeName + QStringLiteral(".json"));
    QFile journal(journalPath);
    if (!journal.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output journal is unavailable.");
        return false;
    }
    const QByteArray journalBytes = journal.readAll();
    QJsonParseError journalParseError;
    const QJsonDocument journalDocument = QJsonDocument::fromJson(journalBytes, &journalParseError);
    journal.close();
    if (journalParseError.error != QJsonParseError::NoError || !journalDocument.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output journal is malformed.");
        return false;
    }
    const QJsonObject journalObject = journalDocument.object();
    if (!journalNamesAreSafe(root, nodeName, journalObject)) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output journal has unsafe paths.");
        return false;
    }
    const QString stage = journalObject.value(QStringLiteral("stage")).toString();
    if (stage != QStringLiteral("MetadataCommitted") && stage != QStringLiteral("Completed")) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output is not in a terminal state.");
        return false;
    }
    const QJsonValue revisionValue = journalObject.value(QStringLiteral("executionRevision"));
    if (!revisionValue.isDouble() || revisionValue.toDouble() < 0.0) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output journal revision is invalid.");
        return false;
    }
    executionRevision = static_cast<std::uint64_t>(revisionValue.toDouble());
    runId = journalObject.value(QStringLiteral("runId")).toString();
    if (journalObject.value(QStringLiteral("nodeName")).toString() != nodeName || runId.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output journal identity is invalid.");
        runId.clear();
        return false;
    }

    QFile manifest(QDir(root.absoluteFilePath(nodeName)).absoluteFilePath(
        QString::fromLatin1(kOutputManifestFile)));
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest is unavailable.");
        runId.clear();
        return false;
    }
    const QByteArray manifestBytes = manifest.readAll();
    QJsonParseError manifestParseError;
    const QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestBytes, &manifestParseError);
    manifest.close();
    if (manifestParseError.error != QJsonParseError::NoError || !manifestDocument.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest is malformed.");
        runId.clear();
        return false;
    }
    const QJsonObject manifestObject = manifestDocument.object();
    const QJsonValue versionValue = manifestObject.value(QStringLiteral("version"));
    if (!versionValue.isDouble() || versionValue.toInt() <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest version is invalid.");
        runId.clear();
        executionRevision = 0;
        return false;
    }
    manifestVersion = versionValue.toInt();
    if (manifestObject.value(QStringLiteral("nodeName")).toString() != nodeName ||
        manifestObject.value(QStringLiteral("runId")).toString() != runId) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest identity does not match its journal.");
        outputPaths.clear();
        runId.clear();
        return false;
    }
    QString descriptorError;
    const QtNodes::ProductDescriptor::Ptr descriptor = QtNodes::ProductDescriptor::fromJson(
        manifestObject.value(QStringLiteral("productDescriptor")).toObject(), &descriptorError);
    if (!descriptor || descriptor->state() != QtNodes::ProductState::Committed) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest has no committed product descriptor.");
        outputPaths.clear();
        runId.clear();
        return false;
    }

    QSet<QString> expectedNames;
    for (const QJsonValue& value : journalObject.value(QStringLiteral("expectedFiles")).toArray()) {
        const QString name = value.toString();
        if (name.isEmpty() || expectedNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output file list is invalid.");
            outputPaths.clear();
            runId.clear();
            return false;
        }
        expectedNames.insert(name);
    }
    const QJsonArray outputs = manifestObject.value(QStringLiteral("outputs")).toArray();
    if (expectedNames.isEmpty() || outputs.size() != expectedNames.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output file list is incomplete.");
        outputPaths.clear();
        runId.clear();
        return false;
    }
    QSet<QString> actualNames;
    for (const QJsonValue& value : outputs) {
        const QJsonObject outputObject = value.toObject();
        const QString name = outputObject.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || !expectedNames.contains(name) || actualNames.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output contains an invalid artifact list.");
            outputPaths.clear();
            runId.clear();
            return false;
        }
        actualNames.insert(name);
        const QFileInfo info(root.absoluteFilePath(nodeName + "/" + name));
        const QJsonValue expectedSize = outputObject.value(QStringLiteral("size"));
        const QJsonValue expectedModified = outputObject.value(QStringLiteral("modifiedMs"));
        if (!expectedSize.isDouble() || !expectedModified.isDouble() ||
            expectedSize.toDouble() < 0.0 || expectedModified.toDouble() < 0.0 ||
            static_cast<qint64>(expectedSize.toDouble()) != info.size() ||
            static_cast<qint64>(expectedModified.toDouble()) != info.lastModified().toMSecsSinceEpoch()) {
            if (errorMessage) *errorMessage = QStringLiteral("Committed output metadata changed.");
            outputPaths.clear();
            runId.clear();
            executionRevision = 0;
            manifestVersion = 0;
            return false;
        }
        if (!validateOutputFile(info, errorMessage)) {
            outputPaths.clear();
            runId.clear();
            executionRevision = 0;
            manifestVersion = 0;
            return false;
        }
        outputPaths.append(info.absoluteFilePath());
    }
    if (outputPaths.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Committed output manifest has no artifacts.");
        runId.clear();
        executionRevision = 0;
        manifestVersion = 0;
        return false;
    }
    return true;
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

// 手动归一化并保存预览 JPG 的备用逻辑，供 H5 预览与直接矩阵预览共用。
static int savePhaseFallbackJpg(const cv::Mat& matToSave, const QString& type, const QString& jpgPath)
{
    cv::Mat phaseNormalized;
    if (type == QStringLiteral("coherence"))
    {
        phaseNormalized = matToSave * 255.0;
    }
    else if (type == QStringLiteral("dem"))
    {
        // 排除 NoData 像元 (-32767)，避免归一化时被极端值拉偏色阶
        cv::Mat noDataMask = (matToSave <= -32700.0);
        cv::Mat validMask = (matToSave > -32700.0);
        double minVal = 0.0, maxVal = 0.0;
        if (cv::countNonZero(validMask) > 0)
        {
            cv::minMaxLoc(matToSave, &minVal, &maxVal, 0, 0, validMask);
            if (maxVal - minVal > 1e-6)
            {
                phaseNormalized = (matToSave - minVal) * (255.0 / (maxVal - minVal));
            }
            else
            {
                phaseNormalized = cv::Mat::zeros(matToSave.size(), CV_64F);
            }
            phaseNormalized.setTo(0.0, noDataMask);
        }
        else
        {
            phaseNormalized = cv::Mat::zeros(matToSave.size(), CV_64F);
        }
    }
    else
    {
        phaseNormalized = (matToSave + 3.141592653589793) * (255.0 / (2.0 * 3.141592653589793));
    }

    phaseNormalized.convertTo(phaseNormalized, CV_8U);

    cv::Mat colorImage;
    if (type == QStringLiteral("coherence"))
    {
        colorImage = phaseNormalized;
    }
    else
    {
        cv::applyColorMap(phaseNormalized, colorImage, cv::COLORMAP_JET);
    }

    return cv::imwrite(jpgPath.toStdString(), colorImage) ? 0 : -1;
}

// 直接从内存中的高程矩阵生成 DEM 预览 JPG，供 Worker 在写入输出后复用矩阵，
// 避免最终化阶段再从 H5 全量读取生成预览。
bool generateDemJpgFromMat(const cv::Mat& dem, const QString& jpgPath)
{
    if (dem.empty() || jpgPath.isEmpty()) return false;

    cv::Mat demForPreview = dem.clone();
    if (demForPreview.type() != CV_64F) demForPreview.convertTo(demForPreview, CV_64F);
    // 与 H5 预览渲染一致：NoData 像元以 NaN 参与 savephase，避免极端值拉偏色阶
    cv::Mat noDataMask = (demForPreview <= -32700.0);
    demForPreview.setTo(std::numeric_limits<double>::quiet_NaN(), noDataMask);

    const QFileInfo jpgInfo(jpgPath);
    const QString tempPath = jpgInfo.absolutePath() + "/." + jpgInfo.completeBaseName() +
        "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
    QFile::remove(tempPath);

    Utils util;
    int ret = util.savephase(tempPath.toStdString().c_str(), "jet", demForPreview);
    if (ret != 0) ret = savePhaseFallbackJpg(demForPreview, QStringLiteral("dem"), tempPath);
    if (ret != 0 || !replaceJpgPreviewAtomically(tempPath, jpgPath)) {
        QFile::remove(tempPath);
        return false;
    }
    return true;
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
    // 手动归一化与保存预览 JPG 的备用逻辑（savephase 失败时兜底），与
    // generateDemJpgFromMat 共用实现，避免逻辑重复。
    auto savePhaseFallback = [&](const cv::Mat& mat_to_save, const QString& type_str) -> int {
        return savePhaseFallbackJpg(mat_to_save, type_str, jpgPath);
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
                // 注意：savephase 内部将相位折叠映射至 [-pi, pi] 区间渲染伪彩色，解缠相位在预览图展示层折叠为缠绕色呈现
                ret = util.savephase(jpgPath.toStdString().c_str(), "jet", phase);
            }
            else if (type == "coherence")
            {
                ret = util.savephase(jpgPath.toStdString().c_str(), "gray", phase);
            }
            else if (type == "dem")
            {
                // 将 NoData 像元替换为 NaN，避免 -32767 极端值拉偏色阶
                cv::Mat demNoDataMask = (phase <= -32700.0);
                phase.setTo(std::numeric_limits<double>::quiet_NaN(), demNoDataMask);
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

bool probeH5DatasetMetadata(const QString& filePath,
                            const QString& dataset,
                            int* rows,
                            int* columns,
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
    int localRows = 0;
    int localColumns = 0;
    const QByteArray utf8Path = filePath.toUtf8();
    const QByteArray utf8Dataset = dataset.toUtf8();
    if (Hdf5IO::getDatasetDims(utf8Path.constData(), utf8Dataset.constData(), &localRows, &localColumns) != 0) {
        if (errMsg) *errMsg = QStringLiteral("H5 数据集不存在或元数据读取失败: %1").arg(dataset);
        return false;
    }
    if (rows) *rows = localRows;
    if (columns) *columns = localColumns;
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

    const bool isSourcePath = (dataset == QStringLiteral("source_1") || dataset == QStringLiteral("source_2"));
    if (isSourcePath && !isValidUtf8PathBytes(value)) {
        if (errMsg) *errMsg = QStringLiteral("Source path is not valid UTF-8 for %1 in %2.")
                                   .arg(dataset, filePath);
        return false;
    }

    NodeUtils::Hdf5Locker locker(filePath);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("Failed to lock H5 for string write: %1").arg(filePath);
        return false;
    }
    const QByteArray fileUtf8 = filePath.toUtf8();
    const QByteArray datasetUtf8 = dataset.toUtf8();
    const int rc = Hdf5IO::writeString(fileUtf8.constData(),
                                       datasetUtf8.constData(),
                                       value.c_str());
    if (rc != 0) {
        if (errMsg) *errMsg = QStringLiteral("Failed to write H5 string dataset %1 (rc=%2)")
                                   .arg(dataset).arg(rc);
        return false;
    }
    if (isSourcePath) {
        const int encRc = Hdf5IO::writeString(fileUtf8.constData(), "source_path_encoding", "UTF-8");
        const int verRc = Hdf5IO::writeString(fileUtf8.constData(), "source_path_format_version", "2");
        if (encRc != 0 || verRc != 0) {
            if (errMsg) *errMsg = QStringLiteral("Failed to write source-path encoding metadata for %1").arg(filePath);
            return false;
        }
    }
    return true;
}

bool validateSentinelGeometryContract(const QString& filePath, QString* errMsg)
{
    Hdf5Locker identityProbeLocker(filePath, 50);
    if (!identityProbeLocker.isLocked()) {
        if (errMsg) {
            *errMsg = QStringLiteral("获取 Sentinel-1 几何契约校验锁超时：%1").arg(filePath);
        }
        return false;
    }
    const QByteArray utf8Path = filePath.toUtf8();
    int swathExists = 0;
    int polarizationExists = 0;
    if (Hdf5IO::datasetExists(utf8Path.constData(), "swath", &swathExists) != 0 ||
        Hdf5IO::datasetExists(utf8Path.constData(), "polarization", &polarizationExists) != 0) {
        if (errMsg) {
            *errMsg = QStringLiteral("无法检查 Sentinel-1 身份数据集：%1").arg(filePath);
        }
        return false;
    }

    // Products without either Sentinel identity field belong to another
    // sensor/product family and are outside this contract.
    if (swathExists == 0 && polarizationExists == 0) {
        return true;
    }

    std::string swath;
    std::string polarization;
    QString swathError;
    QString polarizationError;
    const bool hasSwath = readStringFromH5(filePath, QStringLiteral("swath"), swath, &swathError);
    const bool hasPolarization = readStringFromH5(filePath, QStringLiteral("polarization"), polarization, &polarizationError);
    if (!hasSwath || !hasPolarization || swath.empty() || polarization.empty()) {
        if (errMsg) {
            *errMsg = QStringLiteral("Sentinel-1 H5 的 swath/polarization 身份不完整：%1")
                .arg(filePath);
        }
        return false;
    }

    std::string contract;
    QString contractError;
    if (!readStringFromH5(filePath,
                          QStringLiteral("sentinel_geometry_coefficient_contract"),
                          contract, &contractError) ||
        QString::fromStdString(contract).trimmed() !=
            QStringLiteral("row_col_scene_dimensions_v2")) {
        if (errMsg) {
            *errMsg = QStringLiteral(
                "Sentinel-1 H5 使用了旧的或缺失的地理多项式契约，必须重新导入原始 SAFE：%1")
                .arg(filePath);
        }
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
    const QByteArray outputUtf8 = outputPath.toUtf8();
    const int source1Result = Hdf5IO::writeString(outputUtf8.constData(), "source_1", source1.c_str());
    if (source1Result != 0) {
        if (errMsg) *errMsg = QStringLiteral("Failed to write source_1 metadata (rc=%1): %2")
                               .arg(source1Result).arg(outputPath);
        return false;
    }
    const int source2Result = Hdf5IO::writeString(outputUtf8.constData(), "source_2", source2.c_str());
    if (source2Result != 0) {
        if (errMsg) *errMsg = QStringLiteral("Failed to write source_2 metadata (rc=%1): %2")
                               .arg(source2Result).arg(outputPath);
        return false;
    }
    const int encResult = Hdf5IO::writeString(outputUtf8.constData(), "source_path_encoding", "UTF-8");
    const int verResult = Hdf5IO::writeString(outputUtf8.constData(), "source_path_format_version", "2");
    if (encResult != 0 || verResult != 0) {
        if (errMsg) *errMsg = QStringLiteral("Failed to write source-path encoding metadata for %1").arg(outputPath);
        return false;
    }
    return true;
}

namespace {
bool validateVersionedFlatEarthContract(const QString& inputPath, QString* errMsg)
{
    int modelVersion = 0;
    if (!readScalarFromH5(inputPath, QStringLiteral("flat_earth_model_version"), modelVersion, errMsg) ||
        (modelVersion != 5 && modelVersion != 6)) {
        if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("不支持的平地相位模型版本：%1").arg(inputPath);
        return false;
    }
    const bool geometryOnly = modelVersion == 6;
    const char* const expectedProcessing = geometryOnly ?
        "master_native_phase;slave_registration_mapping_seed_only_m_conjugate_s_v2" :
        "master_native_phase;slave_registered_mapping_and_resampled_reramp_reference_m_conjugate_s_v1";
    const char* const expectedStatus = geometryOnly ?
        "tops_native_range_doppler_h0_geometry_only_reference_v2" :
        "tops_native_range_doppler_h0_reference_v1";
    const char* const expectedReferenceSemantics = geometryOnly ?
        "unwrapped_master_native_h0_rde_geometry_only_reference_v6" :
        "unwrapped_master_native_h0_rde_geometry_plus_slave_native_processing_effective_complex_block_reference_v5";
    const auto readRequiredInt = [&](const QString& dataset, int expected) {
        int value = 0;
        return readScalarFromH5(inputPath, dataset, value, errMsg) && value == expected;
    };
	const auto readRequiredString = [&](const QString& dataset, const char* expected) {
		std::string value;
		return readStringFromH5(inputPath, dataset, value, errMsg) && value == expected;
	};
	const auto readSupportedFineOrbitStrategy = [&]() {
		std::string value;
		return readStringFromH5(inputPath, QStringLiteral("flat_earth_orbit_interpolation_strategy"), value, errMsg) &&
			(value == "fine_state_vec_cubic_hermite_v2" ||
			 value == "raw_state_vec_nearest_contiguous_8_osv_cubic_least_squares_v1");
	};
	const auto readOrbitSourceAndReason = [&](const QString& sourceDataset, const QString& reasonDataset) {
		std::string source;
		std::string reason;
		if (!readStringFromH5(inputPath, sourceDataset, source, errMsg) ||
			!readStringFromH5(inputPath, reasonDataset, reason, errMsg)) return false;
		return (source == "fine_state_vec" && reason == "fine_state_vec_valid_preferred_v1") ||
			(source == "state_vec" && (reason == "fine_state_vec_invalid__raw_snap_compatible_fallback_v1" ||
				reason == "fine_state_vec_absent__raw_snap_compatible_fallback_v1"));
	};
	const auto readLookSideSource = [&]() {
		std::string source;
		return readStringFromH5(inputPath, QStringLiteral("flat_earth_master_look_side_source"), source, errMsg) &&
			(source == "h5_lookside_v1" || source == "sentinel1_fixed_right_looking_v1");
	};
    if (!readRequiredString(QStringLiteral("flat_earth_model_source_row_semantics"),
                            "source_row_map_selects_master_native_burst_line_only_v1") ||
		!readRequiredString(QStringLiteral("flat_earth_model_timing_semantics"),
							"strict_gps_h5_time_v2__registration_time_seed_not_geometry_truth_v1") ||
		!readRequiredString(QStringLiteral("flat_earth_processing_phase_semantics"),
							expectedProcessing) ||
		!readRequiredString(QStringLiteral("flat_earth_slave_registration_mapping_semantics"),
							"pull_source_row_and_column_offsets_a0_a1_column_a2_master_burst_line_v1") ||
		(!geometryOnly && !readRequiredString(QStringLiteral("flat_earth_slave_registration_reramp_phase_semantics"),
							"resampled_slave_deramp_demod_phase_before_conjugated_reramp_v1")) ||
		!readRequiredString(QStringLiteral("flat_earth_model_status"), expectedStatus) ||
		!readRequiredString(QStringLiteral("flat_earth_geolocation_coordinate_semantics"), "master_native_line_sample_to_h0_rde__slave_zero_doppler_range_v1") ||
		!readRequiredString(QStringLiteral("flat_earth_orbit_time_scale"), "GPS") ||
		!readSupportedFineOrbitStrategy() ||
		!readOrbitSourceAndReason(QStringLiteral("flat_earth_master_orbit_source"), QStringLiteral("flat_earth_master_orbit_selection_reason")) ||
		!readOrbitSourceAndReason(QStringLiteral("flat_earth_slave_orbit_source"), QStringLiteral("flat_earth_slave_orbit_selection_reason")) ||
		!readLookSideSource() ||
		!readRequiredString(QStringLiteral("flat_earth_reference_phase_semantics"),
							expectedReferenceSemantics)) {
		if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("版本化平地相位描述符缺失、类型错误或值不受支持：%1").arg(inputPath);
        return false;
    }
    int sourceRowCount = 0;
    if (!readScalarFromH5(inputPath, QStringLiteral("flat_earth_model_source_row_count"), sourceRowCount, errMsg) ||
        sourceRowCount < 1) {
		if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("版本化 source-row 计数无效：%1").arg(inputPath);
        return false;
    }
	int masterLinesPerBurst = 0;
	int slaveLinesPerBurst = 0;
	int slaveBurstOffset = 0;
	cv::Mat masterBurstTimes;
	cv::Mat slaveBurstTimes;
	if (!readScalarFromH5(inputPath, QStringLiteral("flat_earth_master_lines_per_burst"), masterLinesPerBurst, errMsg) ||
		!readScalarFromH5(inputPath, QStringLiteral("flat_earth_slave_lines_per_burst"), slaveLinesPerBurst, errMsg) ||
		!readScalarFromH5(inputPath, QStringLiteral("flat_earth_slave_source_burst_offset"), slaveBurstOffset, errMsg) ||
		masterLinesPerBurst < 1 || slaveLinesPerBurst < 1 ||
		!readMatFromH5(inputPath, QStringLiteral("flat_earth_master_burst_azimuth_time"), masterBurstTimes, -1, errMsg) ||
		!readMatFromH5(inputPath, QStringLiteral("flat_earth_slave_burst_azimuth_time"), slaveBurstTimes, -1, errMsg) ||
		masterBurstTimes.type() != CV_64F || masterBurstTimes.cols != 1 || masterBurstTimes.rows < 1 ||
		slaveBurstTimes.type() != CV_64F || slaveBurstTimes.cols != 1 ||
		sourceRowCount > masterBurstTimes.rows * masterLinesPerBurst ||
		!cv::checkRange(masterBurstTimes, true, nullptr) ||
		!cv::checkRange(slaveBurstTimes, true, nullptr)) {
		if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("版本化 TOPS burst 时序 provenance 无效：%1").arg(inputPath);
		return false;
	}
	const QStringList phaseMatrices64 = {
		QStringLiteral("flat_earth_master_azimuth_fm_rate_list"), QStringLiteral("flat_earth_slave_azimuth_fm_rate_list"),
		QStringLiteral("flat_earth_master_dc_estimate_list"), QStringLiteral("flat_earth_slave_dc_estimate_list") };
	const QStringList phaseMatrices32 = {
		QStringLiteral("flat_earth_master_first_valid_line"), QStringLiteral("flat_earth_master_last_valid_line"),
		QStringLiteral("flat_earth_slave_first_valid_line"), QStringLiteral("flat_earth_slave_last_valid_line"),
		QStringLiteral("flat_earth_master_first_valid_sample"), QStringLiteral("flat_earth_master_last_valid_sample"),
		QStringLiteral("flat_earth_slave_first_valid_sample"), QStringLiteral("flat_earth_slave_last_valid_sample") };
	for (const QString& dataset : phaseMatrices64) {
		cv::Mat value;
		if (!readMatFromH5(inputPath, dataset, value, -1, errMsg) || value.type() != CV_64F ||
			value.rows < 1 || value.cols < 5 || !cv::checkRange(value, true, nullptr)) return false;
	}
	for (const QString& dataset : phaseMatrices32) {
		cv::Mat value;
		if (!readMatFromH5(inputPath, dataset, value, -1, errMsg) || value.type() != CV_32S ||
			value.cols != 1 || value.rows < 1) return false;
	}
	const auto validateBurstVector = [&](const QString& dataset, int expectedRows) {
		cv::Mat value;
		return readMatFromH5(inputPath, dataset, value, -1, errMsg) && value.type() == CV_32S &&
			value.rows == expectedRows && value.cols == 1;
	};
	if (!validateBurstVector(QStringLiteral("flat_earth_master_first_valid_line"), masterBurstTimes.rows) ||
		!validateBurstVector(QStringLiteral("flat_earth_master_last_valid_line"), masterBurstTimes.rows) ||
		!validateBurstVector(QStringLiteral("flat_earth_master_first_valid_sample"), masterBurstTimes.rows) ||
		!validateBurstVector(QStringLiteral("flat_earth_master_last_valid_sample"), masterBurstTimes.rows) ||
		!validateBurstVector(QStringLiteral("flat_earth_slave_first_valid_line"), slaveBurstTimes.rows) ||
		!validateBurstVector(QStringLiteral("flat_earth_slave_last_valid_line"), slaveBurstTimes.rows) ||
		!validateBurstVector(QStringLiteral("flat_earth_slave_first_valid_sample"), slaveBurstTimes.rows) ||
		!validateBurstVector(QStringLiteral("flat_earth_slave_last_valid_sample"), slaveBurstTimes.rows)) return false;
	const QStringList phaseScalars = {
		QStringLiteral("flat_earth_master_azimuth_steering_rate"), QStringLiteral("flat_earth_slave_azimuth_steering_rate"),
		QStringLiteral("flat_earth_master_range_spacing"), QStringLiteral("flat_earth_slave_range_spacing"),
		QStringLiteral("flat_earth_master_slant_range_first_pixel"), QStringLiteral("flat_earth_slave_slant_range_first_pixel") };
	for (const QString& dataset : phaseScalars) {
		double value = 0.0;
		if (!readScalarFromH5(inputPath, dataset, value, errMsg) || !std::isfinite(value) ||
			(dataset.contains(QStringLiteral("range")) && value <= 0.0) ||
			(dataset.contains(QStringLiteral("steering")) && std::fabs(value) <= DBL_EPSILON)) return false;
	}
	int rdeMaxIterations = 0;
	int zeroDopplerMaxIterations = 0;
	int transmitReceiveMode = 0;
	if (!readScalarFromH5(inputPath, QStringLiteral("flat_earth_rde_max_iterations"), rdeMaxIterations, errMsg) || rdeMaxIterations < 1 ||
		!readScalarFromH5(inputPath, QStringLiteral("flat_earth_zero_doppler_max_iterations"), zeroDopplerMaxIterations, errMsg) || zeroDopplerMaxIterations < 1 ||
		!readScalarFromH5(inputPath, QStringLiteral("flat_earth_transmit_receive_mode"), transmitReceiveMode, errMsg) ||
		(transmitReceiveMode != 1 && transmitReceiveMode != 2)) return false;
	cv::Mat phase, reference, rerampPhase, mappingCoefficients, mappingBurstIndices, rdeStatistics;
    const bool hasPhaseGrid = readMatFromH5(inputPath, QStringLiteral("phase"), phase, -1, errMsg) && !phase.empty();
    if (!hasPhaseGrid) {
        if (errMsg) errMsg->clear();
        if (!readMatFromH5(inputPath, QStringLiteral("dem"), phase, -1, errMsg) || phase.empty()) {
			if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("版本化平地相位契约缺少 phase 或 DEM 输出网格：%1").arg(inputPath);
            return false;
        }
    }
    if (
        !readMatFromH5(inputPath, QStringLiteral("flat_earth_reference_phase"), reference, -1, errMsg) ||
        reference.type() != CV_64F || reference.size() != phase.size() || !cv::checkRange(reference, true, nullptr) ||
		!readMatFromH5(inputPath, QStringLiteral("flat_earth_rde_burst_statistics"), rdeStatistics, -1, errMsg) ||
		rdeStatistics.type() != CV_64F || rdeStatistics.rows != masterBurstTimes.rows || rdeStatistics.cols != 13 ||
		!cv::checkRange(rdeStatistics, true, nullptr)) {
		if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("版本化平地相位参考场或 RDE 汇总统计形状无效：%1").arg(inputPath);
		return false;
    }
	if ((!geometryOnly &&
		 (!readMatFromH5(inputPath, QStringLiteral("flat_earth_slave_registration_reramp_phase"), rerampPhase, -1, errMsg) ||
		  rerampPhase.type() != CV_64F || rerampPhase.size() != phase.size() || !cv::checkRange(rerampPhase, true, nullptr))) ||
		!readMatFromH5(inputPath, QStringLiteral("flat_earth_slave_registration_mapping_coefficients"), mappingCoefficients, -1, errMsg) ||
		mappingCoefficients.type() != CV_64F || mappingCoefficients.rows < 1 || mappingCoefficients.cols != 6 || !cv::checkRange(mappingCoefficients, true, nullptr) ||
		!readMatFromH5(inputPath, QStringLiteral("flat_earth_slave_registration_mapping_master_burst_indices"), mappingBurstIndices, -1, errMsg) ||
		mappingBurstIndices.type() != CV_32S || mappingBurstIndices.rows != 1 ||
		mappingBurstIndices.cols != mappingCoefficients.rows) {
		if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("版本化注册 mapping provenance 无效：%1").arg(inputPath);
		return false;
	}
	for (int index = 0; index < mappingBurstIndices.cols; ++index) {
		const int burst = mappingBurstIndices.at<int>(0, index);
		if (burst < 1) return false;
		for (int previous = 0; previous < index; ++previous) {
			if (mappingBurstIndices.at<int>(0, previous) == burst) return false;
		}
	}
	if (geometryOnly) {
		int rerampExists = 0;
		int rerampSemanticsExists = 0;
		const QByteArray utf8Path = inputPath.toUtf8();
		if (Hdf5IO::datasetExists(utf8Path.constData(), "flat_earth_slave_registration_reramp_phase", &rerampExists) != 0 ||
			Hdf5IO::datasetExists(utf8Path.constData(), "flat_earth_slave_registration_reramp_phase_semantics", &rerampSemanticsExists) != 0 ||
			rerampExists != rerampSemanticsExists) return false;
		if (rerampExists != 0) {
			std::string rerampSemantics;
			if (!readMatFromH5(inputPath, QStringLiteral("flat_earth_slave_registration_reramp_phase"), rerampPhase, -1, errMsg) ||
				rerampPhase.type() != CV_64F || rerampPhase.size() != phase.size() || !cv::checkRange(rerampPhase, true, nullptr) ||
				!readStringFromH5(inputPath, QStringLiteral("flat_earth_slave_registration_reramp_phase_semantics"), rerampSemantics, errMsg) ||
				rerampSemantics != "resampled_slave_deramp_demod_phase_registration_only_v1") return false;
		}
	}
    const QStringList scalarNames = {
        QStringLiteral("flat_earth_master_orbit_osv_start_gps"), QStringLiteral("flat_earth_master_orbit_osv_stop_gps"),
		QStringLiteral("flat_earth_master_geometry_start_gps"), QStringLiteral("flat_earth_master_geometry_stop_gps"),
        QStringLiteral("flat_earth_slave_orbit_osv_start_gps"), QStringLiteral("flat_earth_slave_orbit_osv_stop_gps"),
		QStringLiteral("flat_earth_slave_geometry_start_gps"), QStringLiteral("flat_earth_slave_geometry_stop_gps"),
		QStringLiteral("flat_earth_orbit_interpolation_margin_seconds"),
        QStringLiteral("flat_earth_rde_epsilon_phase"), QStringLiteral("flat_earth_rde_max_residual"),
        QStringLiteral("flat_earth_zero_doppler_max_residual"), QStringLiteral("flat_earth_rde_max_jacobian_condition"),
        QStringLiteral("flat_earth_slave_search_initial_half_window_seconds"),
        QStringLiteral("flat_earth_slave_search_maximum_half_window_seconds"),
		QStringLiteral("flat_earth_slave_search_expansion_factor"), QStringLiteral("flat_earth_wavelength_meters") };
	double epsilonPhase = 0.0;
	double jacobianCondition = 0.0;
	double initialWindow = 0.0;
	double maximumWindow = 0.0;
	double expansionFactor = 0.0;
	double wavelength = 0.0;
	double masterOrbitStart = 0.0;
	double masterOrbitStop = 0.0;
	double masterGeometryStart = 0.0;
	double masterGeometryStop = 0.0;
	double slaveOrbitStart = 0.0;
	double slaveOrbitStop = 0.0;
	double slaveGeometryStart = 0.0;
	double slaveGeometryStop = 0.0;
	double interpolationMargin = 0.0;
    for (const QString& dataset : scalarNames) {
        double value = 0.0;
		if (!readScalarFromH5(inputPath, dataset, value, errMsg) || !std::isfinite(value) ||
			(dataset.endsWith(QStringLiteral("epsilon_phase")) && value <= 0.0) || value <= 0.0) {
			if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("v5 RDE 数值描述符无效：%1 (%2)").arg(inputPath, dataset);
            return false;
        }
		if (dataset == QStringLiteral("flat_earth_rde_epsilon_phase")) epsilonPhase = value;
		else if (dataset == QStringLiteral("flat_earth_rde_max_jacobian_condition")) jacobianCondition = value;
		else if (dataset == QStringLiteral("flat_earth_slave_search_initial_half_window_seconds")) initialWindow = value;
		else if (dataset == QStringLiteral("flat_earth_slave_search_maximum_half_window_seconds")) maximumWindow = value;
		else if (dataset == QStringLiteral("flat_earth_slave_search_expansion_factor")) expansionFactor = value;
		else if (dataset == QStringLiteral("flat_earth_wavelength_meters")) wavelength = value;
		else if (dataset == QStringLiteral("flat_earth_master_orbit_osv_start_gps")) masterOrbitStart = value;
		else if (dataset == QStringLiteral("flat_earth_master_orbit_osv_stop_gps")) masterOrbitStop = value;
		else if (dataset == QStringLiteral("flat_earth_master_geometry_start_gps")) masterGeometryStart = value;
		else if (dataset == QStringLiteral("flat_earth_master_geometry_stop_gps")) masterGeometryStop = value;
		else if (dataset == QStringLiteral("flat_earth_slave_orbit_osv_start_gps")) slaveOrbitStart = value;
		else if (dataset == QStringLiteral("flat_earth_slave_orbit_osv_stop_gps")) slaveOrbitStop = value;
		else if (dataset == QStringLiteral("flat_earth_slave_geometry_start_gps")) slaveGeometryStart = value;
		else if (dataset == QStringLiteral("flat_earth_slave_geometry_stop_gps")) slaveGeometryStop = value;
		else if (dataset == QStringLiteral("flat_earth_orbit_interpolation_margin_seconds")) interpolationMargin = value;
    }
	int maxSlaveSearchExpansions = 0;
	if (!readScalarFromH5(inputPath, QStringLiteral("flat_earth_slave_search_max_expansions"), maxSlaveSearchExpansions, errMsg) ||
		maxSlaveSearchExpansions < 0 || maximumWindow < initialWindow || expansionFactor <= 1.0 ||
		!(masterGeometryStop > masterGeometryStart) || !(slaveGeometryStop > slaveGeometryStart) ||
		masterOrbitStart > masterGeometryStart - interpolationMargin || masterOrbitStop < masterGeometryStop + interpolationMargin ||
		slaveOrbitStart > slaveGeometryStart - interpolationMargin || slaveOrbitStop < slaveGeometryStop + interpolationMargin) return false;
	const double differentialRangeBudget = epsilonPhase * wavelength * transmitReceiveMode / (4.0 * CV_PI);
	if (!std::isfinite(differentialRangeBudget) || differentialRangeBudget <= 0.0) return false;
	for (int burst = 0; burst < rdeStatistics.rows; ++burst) {
		if (rdeStatistics.at<double>(burst, 0) == 0.0) continue;
		if (rdeStatistics.at<double>(burst, 2) > rdeMaxIterations ||
			rdeStatistics.at<double>(burst, 3) > zeroDopplerMaxIterations ||
			rdeStatistics.at<double>(burst, 8) > jacobianCondition ||
			rdeStatistics.at<double>(burst, 9) > epsilonPhase ||
			rdeStatistics.at<double>(burst, 10) > differentialRangeBudget ||
			rdeStatistics.at<double>(burst, 11) < initialWindow ||
			rdeStatistics.at<double>(burst, 11) > maximumWindow ||
			rdeStatistics.at<double>(burst, 12) > maxSlaveSearchExpansions) return false;
	}
    return true;
}
} // namespace

bool validateFlatEarthReferenceContract(const QString& inputPath, QString* errMsg)
{
    const QByteArray path = inputPath.toUtf8();
    int schemaExists = 0;
    int modelExists = 0;
    if (Hdf5IO::datasetExists(path.constData(), "phase_processing_schema_version", &schemaExists) != 0 ||
        Hdf5IO::datasetExists(path.constData(), "flat_earth_model_version", &modelExists) != 0) {
        if (errMsg) *errMsg = QStringLiteral("无法检查平地相位契约版本：%1").arg(inputPath);
        return false;
    }
    int schemaVersion = 0;
    if (schemaExists != 0 && !readScalarFromH5(inputPath, QStringLiteral("phase_processing_schema_version"), schemaVersion, errMsg)) {
        return false;
    }
    const bool v2 = schemaVersion == 2 || modelExists != 0;
    if (v2) {
        if (schemaVersion != 2) {
            if (errMsg) *errMsg = QStringLiteral("版本化平地相位模型必须使用 phase_processing_schema_version=2：%1").arg(inputPath);
            return false;
        }
        return validateVersionedFlatEarthContract(inputPath, errMsg) &&
               validatePhaseValidityContract(inputPath, false, errMsg);
    }
    if (schemaExists != 0 && schemaVersion != 1) {
        if (errMsg) *errMsg = QStringLiteral("不支持的相位处理契约版本：%1").arg(schemaVersion);
        return false;
    }
    cv::Mat coefficient;
    if (!readMatFromH5(inputPath, QStringLiteral("flat_phase_coefficient"), coefficient, -1, errMsg) ||
        coefficient.type() != CV_64F || coefficient.rows != 1 || coefficient.cols != 6) {
        if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("旧平地相位模型必须为 CV_64F 1x6：%1").arg(inputPath);
        return false;
    }
    return true;
}

bool validatePhaseValidityContract(const QString& inputPath,
                                   bool requireAllValid,
                                   QString* errMsg)
{
    const QByteArray path = inputPath.toUtf8();
    int schemaExists = 0;
    if (Hdf5IO::datasetExists(path.constData(), "phase_processing_schema_version", &schemaExists) != 0) {
        if (errMsg) *errMsg = QStringLiteral("无法检查相位有效性契约版本：%1").arg(inputPath);
        return false;
    }
    if (schemaExists == 0) return true;

    int schemaVersion = 0;
    if (!readScalarFromH5(inputPath, QStringLiteral("phase_processing_schema_version"), schemaVersion, errMsg)) {
        return false;
    }
    if (schemaVersion != 2) return true;

    cv::Mat phase, validMask, validSampleCount;
    if (!readMatFromH5(inputPath, QStringLiteral("phase"), phase, -1, errMsg) || phase.empty() ||
        !readMatFromH5(inputPath, QStringLiteral("phase_valid_mask"), validMask, -1, errMsg) ||
        validMask.type() != CV_8U || validMask.size() != phase.size() ||
        !readMatFromH5(inputPath, QStringLiteral("phase_valid_sample_count"), validSampleCount, -1, errMsg) ||
        validSampleCount.type() != CV_32S || validSampleCount.size() != phase.size()) {
        if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("v2 相位有效性契约缺失或网格不一致：%1").arg(inputPath);
        return false;
    }
	if (phase.channels() != 1 || (phase.type() != CV_32F && phase.type() != CV_64F)) {
		if (errMsg) *errMsg = QStringLiteral("v2 相位必须为单通道浮点矩阵：%1").arg(inputPath);
		return false;
	}
	const bool phaseIsFloat = phase.type() == CV_32F;

    int invalidCount = 0;
    for (int row = 0; row < phase.rows; ++row) {
        const uchar* valid = validMask.ptr<uchar>(row);
        const int* samples = validSampleCount.ptr<int>(row);
        for (int column = 0; column < phase.cols; ++column) {
            if (valid[column] != 0 && valid[column] != 1) {
                if (errMsg) *errMsg = QStringLiteral("v2 相位有效掩膜不是二值：%1").arg(inputPath);
                return false;
            }
            if (samples[column] < 0 || (valid[column] != 0 && samples[column] == 0)) {
                if (errMsg) *errMsg = QStringLiteral("v2 相位有效样本数无效：%1").arg(inputPath);
                return false;
            }
			if (valid[column] != 0) {
				const double phaseValue = phaseIsFloat
					? static_cast<double>(phase.ptr<float>(row)[column])
					: phase.ptr<double>(row)[column];
				if (!std::isfinite(phaseValue)) {
					if (errMsg) *errMsg = QStringLiteral("v2 有效相位包含非有限像元：%1").arg(inputPath);
					return false;
				}
			}
            if (valid[column] == 0) ++invalidCount;
        }
    }
    if (requireAllValid && invalidCount != 0) {
        if (errMsg) *errMsg = QStringLiteral("输入相位包含 %1 个无效像元，当前处理不支持掩膜相位：%2")
                               .arg(invalidCount).arg(inputPath);
        return false;
    }
    return true;
}

bool validateDenoiseFilterSupportContract(const QString& inputPath, QString* errMsg)
{
    const QByteArray path = inputPath.toUtf8();
    int methodExists = 0;
    if (Hdf5IO::datasetExists(path.constData(), "denoise_method", &methodExists) != 0) {
        if (errMsg) *errMsg = QStringLiteral("无法检查 Denoise 方法元数据：%1").arg(inputPath);
        return false;
    }
    if (methodExists == 0) return true;

    int method = 0;
    if (!readScalarFromH5(inputPath, QStringLiteral("denoise_method"), method, errMsg)) return false;
    if (method != 2 && method != 4) return true;

    int schemaVersion = 0;
	int schemaExists = 0;
	if (Hdf5IO::datasetExists(path.constData(), "phase_processing_schema_version", &schemaExists) != 0) {
		if (errMsg) *errMsg = QStringLiteral("无法检查相位处理契约版本：%1").arg(inputPath);
		return false;
	}
	if (schemaExists == 0 && method == 2) return true;
	if (schemaExists == 0 && method == 4) {
		if (errMsg) *errMsg = QStringLiteral("GoldsteinSnapCompatibleV1 requires an explicit phase-processing contract: %1").arg(inputPath);
		return false;
	}
	if (!readScalarFromH5(inputPath, QStringLiteral("phase_processing_schema_version"), schemaVersion, errMsg)) {
		return false;
	}
	if (method == 4 && schemaVersion != 2) {
		if (errMsg) *errMsg = QStringLiteral("GoldsteinSnapCompatibleV1 requires phase_processing_schema_version=2: %1").arg(inputPath);
		return false;
	}
	if (schemaVersion != 2 && method == 2) return true;

	if (method == 4) {
		cv::Mat phase, validMask, supportMask, filteredI, filteredQ, gamma, gammaMask, gammaCount;
		int contractVersion = 0;
		int supportCount = 0;
		int window = 0;
		int nPad = -1;
		std::string profile, alphaSemantics, spectralSmoothing, overlapWindow;
		std::string supportSemantics, fallbackSemantics, gammaSemantics, gammaAlgorithm;
		if (!validatePhaseValidityContract(inputPath, false, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("phase"), phase, CV_64F, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("phase_valid_mask"), validMask, CV_8U, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("denoise_filter_support_mask"), supportMask, CV_8U, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("interferogram_i"), filteredI, CV_32F, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("interferogram_q"), filteredQ, CV_32F, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("complex_gamma"), gamma, CV_64F, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("complex_gamma_valid_mask"), gammaMask, CV_8U, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("complex_gamma_valid_sample_count"), gammaCount, CV_32S, errMsg) ||
			phase.empty() || validMask.size() != phase.size() || supportMask.size() != phase.size() ||
			filteredI.size() != phase.size() || filteredQ.size() != phase.size() ||
			gamma.size() != phase.size() || gammaMask.size() != phase.size() || gammaCount.size() != phase.size() ||
			!readScalarFromH5(inputPath, QStringLiteral("denoise_mask_contract_version"), contractVersion, errMsg) || contractVersion != 2 ||
			!readScalarFromH5(inputPath, QStringLiteral("denoise_filter_support_count"), supportCount, errMsg) ||
			!readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_win"), window, errMsg) || window != 64 ||
			!readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_npad"), nPad, errMsg) || nPad != 0 ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_goldstein_profile"), profile, errMsg) || profile != "GoldsteinSnapCompatibleV1" ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_goldstein_alpha_semantics"), alphaSemantics, errMsg) ||
			alphaSemantics != "clamp_1_minus_mean_complex_gamma_0.2_1.0_v1" ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_goldstein_spectral_smoothing"), spectralSmoothing, errMsg) ||
			spectralSmoothing != "mean_3x3_skip_zero_power_v1" ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_goldstein_overlap_window"), overlapWindow, errMsg) ||
			overlapWindow != "separable_triangular_v1" ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_filter_support_semantics"), supportSemantics, errMsg) ||
			supportSemantics != "original_valid_pixel_with_at_least_one_processed_fft_window_v2" ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_filter_fallback_semantics"), fallbackSemantics, errMsg) ||
			fallbackSemantics != "input_phase_passthrough_when_unsupported_v2" ||
			!readStringFromH5(inputPath, QStringLiteral("complex_gamma_semantics"), gammaSemantics, errMsg) ||
			gammaSemantics != CoherenceSemantics::kComplexGamma ||
			!readStringFromH5(inputPath, QStringLiteral("complex_gamma_algorithm"), gammaAlgorithm, errMsg) ||
			gammaAlgorithm != "corrected_multilooked_source_row_aware_v1") {
			if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("GoldsteinSnapCompatibleV1 contract is missing or invalid: %1").arg(inputPath);
			return false;
		}
		int gammaWindowRange = 0;
		int gammaWindowAzimuth = 0;
		if (!readScalarFromH5(inputPath, QStringLiteral("complex_gamma_window_range"), gammaWindowRange, errMsg) ||
			!readScalarFromH5(inputPath, QStringLiteral("complex_gamma_window_azimuth"), gammaWindowAzimuth, errMsg) ||
			gammaWindowRange < 3 || gammaWindowAzimuth < 3 ||
			gammaWindowRange % 2 == 0 || gammaWindowAzimuth % 2 == 0) {
			if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("GoldsteinSnapCompatibleV1 complex-gamma window contract is invalid: %1").arg(inputPath);
			return false;
		}
		for (int row = 0; row < gamma.rows; ++row) {
			const double* gammaRow = gamma.ptr<double>(row);
			const uchar* gammaMaskRow = gammaMask.ptr<uchar>(row);
			const int* gammaCountRow = gammaCount.ptr<int>(row);
			for (int column = 0; column < gamma.cols; ++column) {
				const bool invalidMask = gammaMaskRow[column] != 0 && gammaMaskRow[column] != 1;
				const bool invalidCount = gammaCountRow[column] < 0 ||
					(gammaMaskRow[column] != 0 && gammaCountRow[column] <= 0) ||
					(gammaMaskRow[column] == 0 && gammaCountRow[column] != 0);
				const bool invalidGamma = gammaMaskRow[column] != 0 &&
					(!std::isfinite(gammaRow[column]) || gammaRow[column] < 0.0 || gammaRow[column] > 1.0);
				if (invalidMask || invalidCount || invalidGamma) {
					if (errMsg) *errMsg = QStringLiteral(
						"GoldsteinSnapCompatibleV1 complex-gamma mask/count/value contract is invalid at (%1,%2): %3")
						.arg(row).arg(column).arg(inputPath);
					return false;
				}
			}
		}
		int actualCount = 0;
		for (int row = 0; row < phase.rows; ++row) {
			const uchar* valid = validMask.ptr<uchar>(row);
			const uchar* support = supportMask.ptr<uchar>(row);
			for (int column = 0; column < phase.cols; ++column) {
				if ((support[column] != 0 && support[column] != 1) || (support[column] != 0 && valid[column] == 0)) return false;
				actualCount += support[column] != 0 ? 1 : 0;
			}
		}
		if (actualCount != supportCount) {
			if (errMsg) *errMsg = QStringLiteral("GoldsteinSnapCompatibleV1 support count is inconsistent: %1").arg(inputPath);
			return false;
		}
		return true;
	}

    cv::Mat phase, validMask, supportMask;
    int contractVersion = 0;
    int supportCount = 0;
    int window = 0;
    int nPad = 0;
    double alpha = 0.0;
    std::string supportSemantics;
    std::string fallbackSemantics;
    if (!validatePhaseValidityContract(inputPath, false, errMsg) ||
        !readMatFromH5(inputPath, QStringLiteral("phase"), phase, -1, errMsg) || phase.empty() ||
        !readMatFromH5(inputPath, QStringLiteral("phase_valid_mask"), validMask, -1, errMsg) ||
        !readMatFromH5(inputPath, QStringLiteral("denoise_filter_support_mask"), supportMask, -1, errMsg) ||
        supportMask.type() != CV_8U || supportMask.channels() != 1 || supportMask.size() != phase.size() ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_mask_contract_version"), contractVersion, errMsg) ||
        contractVersion != 1 ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_filter_support_count"), supportCount, errMsg) ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_win"), window, errMsg) || window < 5 ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_npad"), nPad, errMsg) || nPad < 0 ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_alpha"), alpha, errMsg) ||
        !std::isfinite(alpha) || alpha <= 0.0 ||
        !readStringFromH5(inputPath, QStringLiteral("denoise_filter_support_semantics"), supportSemantics, errMsg) ||
        supportSemantics != "fully_valid_fft_window_coverage_v1" ||
        !readStringFromH5(inputPath, QStringLiteral("denoise_filter_fallback_semantics"), fallbackSemantics, errMsg) ||
        fallbackSemantics != "input_phase_passthrough_when_unsupported_v1") {
        if (errMsg && errMsg->isEmpty()) {
            *errMsg = QStringLiteral("Goldstein Denoise 掩膜支持契约缺失或无效，请重新执行滤波：%1").arg(inputPath);
        }
        return false;
    }

    int actualCount = 0;
    for (int row = 0; row < supportMask.rows; ++row) {
        const uchar* valid = validMask.ptr<uchar>(row);
        const uchar* support = supportMask.ptr<uchar>(row);
        for (int column = 0; column < supportMask.cols; ++column) {
            if ((support[column] != 0 && support[column] != 1) ||
                (support[column] != 0 && valid[column] == 0)) {
                if (errMsg) *errMsg = QStringLiteral("Goldstein Denoise 支持掩膜必须是 phase_valid_mask 的二值子集：%1").arg(inputPath);
                return false;
            }
            actualCount += support[column] != 0 ? 1 : 0;
        }
    }
    if (supportCount != actualCount) {
        if (errMsg) *errMsg = QStringLiteral("Goldstein Denoise 支持像元计数不一致，请重新执行滤波：%1").arg(inputPath);
        return false;
    }
    return true;
}

bool copyDenoiseFilterSupportContract(const QString& inputPath,
                                      const QString& outputPath,
                                      QString* errMsg)
{
    const QByteArray path = inputPath.toUtf8();
    int methodExists = 0;
    if (Hdf5IO::datasetExists(path.constData(), "denoise_method", &methodExists) != 0) {
        if (errMsg) *errMsg = QStringLiteral("无法检查 Denoise 方法元数据：%1").arg(inputPath);
        return false;
    }
    if (methodExists == 0) return true;

    int method = 0;
    if (!readScalarFromH5(inputPath, QStringLiteral("denoise_method"), method, errMsg)) return false;
    if (method != 2 && method != 4) return true;
    if (!validateDenoiseFilterSupportContract(inputPath, errMsg)) return false;
	if (method == 4) {
		cv::Mat supportMask, filteredI, filteredQ, gamma, gammaMask, gammaCount;
		int contractVersion = 0, supportCount = 0, window = 0, nPad = -1, gammaWindow = 0;
		std::string profile, alphaSemantics, spectralSmoothing, overlapWindow;
		std::string supportSemantics, fallbackSemantics, gammaSemantics, gammaAlgorithm;
		if (!readMatFromH5(inputPath, QStringLiteral("denoise_filter_support_mask"), supportMask, CV_8U, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("interferogram_i"), filteredI, CV_32F, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("interferogram_q"), filteredQ, CV_32F, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("complex_gamma"), gamma, CV_64F, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("complex_gamma_valid_mask"), gammaMask, CV_8U, errMsg) ||
			!readMatFromH5(inputPath, QStringLiteral("complex_gamma_valid_sample_count"), gammaCount, CV_32S, errMsg) ||
			!readScalarFromH5(inputPath, QStringLiteral("denoise_mask_contract_version"), contractVersion, errMsg) ||
			!readScalarFromH5(inputPath, QStringLiteral("denoise_filter_support_count"), supportCount, errMsg) ||
			!readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_win"), window, errMsg) ||
			!readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_npad"), nPad, errMsg) ||
			!readScalarFromH5(inputPath, QStringLiteral("complex_gamma_window_range"), gammaWindow, errMsg) ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_goldstein_profile"), profile, errMsg) ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_goldstein_alpha_semantics"), alphaSemantics, errMsg) ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_goldstein_spectral_smoothing"), spectralSmoothing, errMsg) ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_goldstein_overlap_window"), overlapWindow, errMsg) ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_filter_support_semantics"), supportSemantics, errMsg) ||
			!readStringFromH5(inputPath, QStringLiteral("denoise_filter_fallback_semantics"), fallbackSemantics, errMsg) ||
			!readStringFromH5(inputPath, QStringLiteral("complex_gamma_semantics"), gammaSemantics, errMsg) ||
			!readStringFromH5(inputPath, QStringLiteral("complex_gamma_algorithm"), gammaAlgorithm, errMsg)) return false;
		int gammaWindowAzimuth = 0;
		if (!readScalarFromH5(inputPath, QStringLiteral("complex_gamma_window_azimuth"), gammaWindowAzimuth, errMsg) ||
			!writeScalarToH5(outputPath, QStringLiteral("denoise_method"), method, errMsg) ||
			!writeStringToH5(outputPath, QStringLiteral("denoise_goldstein_profile"), profile, errMsg) ||
			!writeStringToH5(outputPath, QStringLiteral("denoise_goldstein_alpha_semantics"), alphaSemantics, errMsg) ||
			!writeStringToH5(outputPath, QStringLiteral("denoise_goldstein_spectral_smoothing"), spectralSmoothing, errMsg) ||
			!writeStringToH5(outputPath, QStringLiteral("denoise_goldstein_overlap_window"), overlapWindow, errMsg) ||
			!writeScalarToH5(outputPath, QStringLiteral("denoise_goldstein_win"), window, errMsg) ||
			!writeScalarToH5(outputPath, QStringLiteral("denoise_goldstein_npad"), nPad, errMsg) ||
			!writeMatToH5(outputPath, QStringLiteral("denoise_filter_support_mask"), supportMask, errMsg) ||
			!writeScalarToH5(outputPath, QStringLiteral("denoise_mask_contract_version"), contractVersion, errMsg) ||
			!writeScalarToH5(outputPath, QStringLiteral("denoise_filter_support_count"), supportCount, errMsg) ||
			!writeStringToH5(outputPath, QStringLiteral("denoise_filter_support_semantics"), supportSemantics, errMsg) ||
			!writeStringToH5(outputPath, QStringLiteral("denoise_filter_fallback_semantics"), fallbackSemantics, errMsg) ||
			!writeMatToH5(outputPath, QStringLiteral("interferogram_i"), filteredI, errMsg) ||
			!writeMatToH5(outputPath, QStringLiteral("interferogram_q"), filteredQ, errMsg) ||
			!writeMatToH5(outputPath, QStringLiteral("complex_gamma"), gamma, errMsg) ||
			!writeMatToH5(outputPath, QStringLiteral("complex_gamma_valid_mask"), gammaMask, errMsg) ||
			!writeMatToH5(outputPath, QStringLiteral("complex_gamma_valid_sample_count"), gammaCount, errMsg) ||
			!writeStringToH5(outputPath, QStringLiteral("complex_gamma_semantics"), gammaSemantics, errMsg) ||
			!writeStringToH5(outputPath, QStringLiteral("complex_gamma_algorithm"), gammaAlgorithm, errMsg) ||
			!writeScalarToH5(outputPath, QStringLiteral("complex_gamma_window_range"), gammaWindow, errMsg) ||
			!writeScalarToH5(outputPath, QStringLiteral("complex_gamma_window_azimuth"), gammaWindowAzimuth, errMsg)) return false;
		return true;
	}
	int schemaExists = 0;
	if (Hdf5IO::datasetExists(path.constData(), "phase_processing_schema_version", &schemaExists) != 0) {
		if (errMsg) *errMsg = QStringLiteral("无法检查相位处理契约版本：%1").arg(inputPath);
		return false;
	}
	if (schemaExists == 0) return true;
	int schemaVersion = 0;
	if (!readScalarFromH5(inputPath, QStringLiteral("phase_processing_schema_version"), schemaVersion, errMsg)) {
		return false;
	}
	if (schemaVersion != 2) return true;

    cv::Mat supportMask;
    int contractVersion = 0;
    int supportCount = 0;
    int window = 0;
    int nPad = 0;
    double alpha = 0.0;
    std::string supportSemantics;
    std::string fallbackSemantics;
    if (!readMatFromH5(inputPath, QStringLiteral("denoise_filter_support_mask"), supportMask, -1, errMsg) ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_mask_contract_version"), contractVersion, errMsg) ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_filter_support_count"), supportCount, errMsg) ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_win"), window, errMsg) ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_npad"), nPad, errMsg) ||
        !readScalarFromH5(inputPath, QStringLiteral("denoise_goldstein_alpha"), alpha, errMsg) ||
        !readStringFromH5(inputPath, QStringLiteral("denoise_filter_support_semantics"), supportSemantics, errMsg) ||
        !readStringFromH5(inputPath, QStringLiteral("denoise_filter_fallback_semantics"), fallbackSemantics, errMsg) ||
        !writeScalarToH5(outputPath, QStringLiteral("denoise_method"), method, errMsg) ||
        !writeScalarToH5(outputPath, QStringLiteral("denoise_goldstein_win"), window, errMsg) ||
        !writeScalarToH5(outputPath, QStringLiteral("denoise_goldstein_npad"), nPad, errMsg) ||
        !writeScalarToH5(outputPath, QStringLiteral("denoise_goldstein_alpha"), alpha, errMsg) ||
        !writeMatToH5(outputPath, QStringLiteral("denoise_filter_support_mask"), supportMask, errMsg) ||
        !writeScalarToH5(outputPath, QStringLiteral("denoise_mask_contract_version"), contractVersion, errMsg) ||
        !writeScalarToH5(outputPath, QStringLiteral("denoise_filter_support_count"), supportCount, errMsg) ||
        !writeStringToH5(outputPath, QStringLiteral("denoise_filter_support_semantics"), supportSemantics, errMsg) ||
        !writeStringToH5(outputPath, QStringLiteral("denoise_filter_fallback_semantics"), fallbackSemantics, errMsg)) {
        return false;
    }
    return true;
}

bool copyPhaseProcessingMetadata(const QString& inputPath,
                                 const QString& outputPath,
                                 QString* errMsg)
{
    constexpr int kLegacyPhaseProcessingSchemaVersion = 1;
    constexpr int kFlatEarthReferenceSchemaVersion = 2;
    constexpr const char* kSchemaDataset = "phase_processing_schema_version";
    constexpr const char* kFlatDataset = "phase_flat_earth_removed";
    constexpr const char* kTopoDataset = "phase_topography_removed";
    constexpr const char* kCoefficientDataset = "flat_phase_coefficient";
    constexpr const char* kReferenceDataset = "flat_earth_reference_phase";
    constexpr const char* kPhaseValidMaskDataset = "phase_valid_mask";
    constexpr const char* kPhaseValidSampleCountDataset = "phase_valid_sample_count";

    const auto copyCommonCoverageContract = [&]() -> bool {
        int contractExists = 0;
        const QByteArray inputUtf8 = inputPath.toUtf8();
        if (Hdf5IO::datasetExists(inputUtf8.constData(), "s1_tops_product_contract", &contractExists) != 0) {
            if (errMsg) *errMsg = QStringLiteral("无法检查共同 burst 产品契约：%1").arg(inputPath);
            return false;
        }
        if (contractExists == 0) {
            return true;
        }

        std::string contract;
        if (!readStringFromH5(inputPath, QStringLiteral("s1_tops_product_contract"), contract, errMsg)) {
            return false;
        }
        if (QString::fromStdString(contract) != QStringLiteral("continuous_deburst_common_coverage_v1")) {
            // An unrelated product contract remains outside this specialised propagation path.
            return true;
        }

        const QStringList stringDatasets = {
            QStringLiteral("s1_tops_product_contract"),
            QStringLiteral("s1_tops_coverage_signature"),
            QStringLiteral("s1_tops_source_frame_mapping"),
            QStringLiteral("s1_tops_geometry_reference_file"),
            QStringLiteral("s1_tops_geometry_reference_path"),
            QStringLiteral("s1_tops_output_source_row_map_semantics")
        };
        const QStringList scalarDatasets = {
            QStringLiteral("s1_tops_source_full_burst_row_count"),
            QStringLiteral("s1_tops_common_master_first_burst"),
            QStringLiteral("s1_tops_common_master_last_burst"),
            QStringLiteral("s1_tops_common_master_burst_count"),
            QStringLiteral("s1_tops_partial_burst_coverage"),
            QStringLiteral("s1_tops_output_source_row_origin"),
            QStringLiteral("s1_tops_source_burst_first"),
            QStringLiteral("s1_tops_source_burst_last"),
            QStringLiteral("s1_tops_source_burst_offset"),
            QStringLiteral("s1_tops_output_source_row_map_multilook_azimuth_factor")
        };
        const QStringList matrixDatasets = {
            QStringLiteral("s1_tops_output_source_row_map"),
            QStringLiteral("s1_tops_retained_master_burst_indices"),
            QStringLiteral("s1_tops_retained_source_row_ranges")
        };

        for (const QString& dataset : stringDatasets) {
            std::string value;
            if (!readStringFromH5(inputPath, dataset, value, errMsg) ||
                value.empty() || !writeStringToH5(outputPath, dataset, value, errMsg)) {
                if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("共同 burst 字符串 provenance 无效：%1").arg(dataset);
                return false;
            }
        }
        for (const QString& dataset : scalarDatasets) {
            int value = 0;
            if (!readScalarFromH5(inputPath, dataset, value, errMsg) ||
                !writeScalarToH5(outputPath, dataset, value, errMsg)) {
                if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("共同 burst 标量 provenance 无效：%1").arg(dataset);
                return false;
            }
        }
        for (const QString& dataset : matrixDatasets) {
            cv::Mat value;
            if (!readMatFromH5(inputPath, dataset, value, -1, errMsg) || value.empty() ||
                !writeMatToH5(outputPath, dataset, value, errMsg)) {
                if (errMsg && errMsg->isEmpty()) *errMsg = QStringLiteral("共同 burst 映射 provenance 无效：%1").arg(dataset);
                return false;
            }
        }
        int coefficientContractExists = 0;
        const QByteArray optionalContractPath = inputPath.toUtf8();
        if (Hdf5IO::datasetExists(optionalContractPath.constData(),
                                  "s1_tops_flat_phase_coefficient_contract",
                                  &coefficientContractExists) != 0) {
            if (errMsg) *errMsg = QStringLiteral("无法检查分段平地相位系数契约：%1").arg(inputPath);
            return false;
        }
        if (coefficientContractExists != 0) {
            std::string value;
            if (!readStringFromH5(inputPath, QStringLiteral("s1_tops_flat_phase_coefficient_contract"), value, errMsg) ||
                value.empty() || !writeStringToH5(outputPath, QStringLiteral("s1_tops_flat_phase_coefficient_contract"), value, errMsg)) {
                return false;
            }
            cv::Mat segmentCoefficients;
            if (!readMatFromH5(inputPath, QStringLiteral("s1_tops_segment_flat_phase_coefficients"),
                              segmentCoefficients, -1, errMsg) || segmentCoefficients.empty() ||
                !writeMatToH5(outputPath, QStringLiteral("s1_tops_segment_flat_phase_coefficients"),
                              segmentCoefficients, errMsg)) {
                return false;
            }
        }
        return true;
    };

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
    cv::Mat flatEarthReference;
    QString referenceError;
    const bool hasReference = readMatFromH5(inputPath, QString::fromLatin1(kReferenceDataset),
                                            flatEarthReference, -1, &referenceError);
    if (hasReference && flatEarthReference.empty()) {
        if (errMsg) *errMsg = QStringLiteral("平地相位参考场为空：%1").arg(inputPath);
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
		int versionedModelExists = 0;
		const QByteArray versionedModelPath = inputPath.toUtf8();
		if (Hdf5IO::datasetExists(versionedModelPath.constData(), "flat_earth_model_version", &versionedModelExists) != 0) {
			if (errMsg) *errMsg = QStringLiteral("无法检查版本化平地相位模型标记：%1").arg(inputPath);
			return false;
		}
		if (versionedModelExists != 0) {
			if (errMsg) *errMsg = QStringLiteral("版本化平地相位模型缺少 phase_processing_schema_version=2：%1").arg(inputPath);
			return false;
		}
        // Legacy products can continue through generic phase-processing nodes.
        if (hasCoefficient && !writeMatToH5(outputPath, QString::fromLatin1(kCoefficientDataset),
                                             flatPhaseCoefficient, errMsg)) {
            return false;
        }
        return copyCommonCoverageContract();
    }

    int schemaVersion = 0;
    if (!readScalarFromH5(inputPath, QString::fromLatin1(kSchemaDataset),
                          schemaVersion, &schemaError)) {
        if (errMsg) *errMsg = schemaError;
        return false;
    }
    if (schemaVersion != kLegacyPhaseProcessingSchemaVersion && schemaVersion != kFlatEarthReferenceSchemaVersion) {
        if (errMsg) *errMsg = QStringLiteral("不支持的相位处理契约版本：%1").arg(schemaVersion);
        return false;
    }
    cv::Mat phaseValidMask;
    cv::Mat phaseValidSampleCount;
    if (schemaVersion == kFlatEarthReferenceSchemaVersion) {
        if (!validatePhaseValidityContract(inputPath, false, errMsg) ||
            !readMatFromH5(inputPath, QString::fromLatin1(kPhaseValidMaskDataset), phaseValidMask, -1, errMsg) ||
            !readMatFromH5(inputPath, QString::fromLatin1(kPhaseValidSampleCountDataset), phaseValidSampleCount, -1, errMsg)) {
            return false;
        }
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
	if (flatRemoved == 1 && !validateFlatEarthReferenceContract(inputPath, errMsg)) {
		return false;
	}
    if (flatRemoved == 1 && schemaVersion == kLegacyPhaseProcessingSchemaVersion && !hasCoefficient) {
        if (errMsg) *errMsg = QStringLiteral("已去平地的相位缺少平地相位系数：%1").arg(inputPath);
        return false;
    }
    if (flatRemoved == 1 && schemaVersion == kFlatEarthReferenceSchemaVersion && !hasReference) {
        if (errMsg) *errMsg = QStringLiteral("已去平地的 v2 相位缺少同网格平地相位参考场：%1").arg(inputPath);
        return false;
    }
    if (flatRemoved == 0 && (hasCoefficient || hasReference)) {
        if (errMsg) *errMsg = QStringLiteral("未去平地的相位不应包含平地相位系数：%1").arg(inputPath);
        return false;
    }

    if ((hasCoefficient && !writeMatToH5(outputPath, QString::fromLatin1(kCoefficientDataset),
                                           flatPhaseCoefficient, errMsg)) ||
        (hasReference && !writeMatToH5(outputPath, QString::fromLatin1(kReferenceDataset), flatEarthReference, errMsg)) ||
        (schemaVersion == kFlatEarthReferenceSchemaVersion &&
         (!writeMatToH5(outputPath, QString::fromLatin1(kPhaseValidMaskDataset), phaseValidMask, errMsg) ||
          !writeMatToH5(outputPath, QString::fromLatin1(kPhaseValidSampleCountDataset), phaseValidSampleCount, errMsg))) ||
        !writeScalarToH5(outputPath, QString::fromLatin1(kSchemaDataset), schemaVersion, errMsg) ||
        !writeScalarToH5(outputPath, QString::fromLatin1(kFlatDataset), flatRemoved, errMsg) ||
        !writeScalarToH5(outputPath, QString::fromLatin1(kTopoDataset), topoRemoved, errMsg)) {
        return false;
    }
	if (schemaVersion == kFlatEarthReferenceSchemaVersion && flatRemoved == 1) {
		const QString rerampDataset = QStringLiteral("flat_earth_slave_registration_reramp_phase");
		const QString rerampSemanticsDataset = QStringLiteral("flat_earth_slave_registration_reramp_phase_semantics");
		QStringList matrixDatasets = {
			QStringLiteral("flat_earth_rde_burst_statistics"),
			QStringLiteral("flat_earth_master_burst_azimuth_time"),
			QStringLiteral("flat_earth_slave_burst_azimuth_time"),
			QStringLiteral("flat_earth_master_azimuth_fm_rate_list"), QStringLiteral("flat_earth_slave_azimuth_fm_rate_list"),
			QStringLiteral("flat_earth_master_dc_estimate_list"), QStringLiteral("flat_earth_slave_dc_estimate_list"),
			QStringLiteral("flat_earth_master_first_valid_line"), QStringLiteral("flat_earth_master_last_valid_line"),
			QStringLiteral("flat_earth_slave_first_valid_line"), QStringLiteral("flat_earth_slave_last_valid_line"),
			QStringLiteral("flat_earth_master_first_valid_sample"), QStringLiteral("flat_earth_master_last_valid_sample"),
			QStringLiteral("flat_earth_slave_first_valid_sample"), QStringLiteral("flat_earth_slave_last_valid_sample"),
			QStringLiteral("flat_earth_slave_registration_mapping_coefficients"),
			QStringLiteral("flat_earth_slave_registration_mapping_master_burst_indices") };
		const QStringList intDatasets = {
			QStringLiteral("flat_earth_model_version"), QStringLiteral("flat_earth_model_source_row_count"),
			QStringLiteral("flat_earth_rde_max_iterations"), QStringLiteral("flat_earth_zero_doppler_max_iterations"),
			QStringLiteral("flat_earth_slave_search_max_expansions"),
			QStringLiteral("flat_earth_transmit_receive_mode"),
			QStringLiteral("flat_earth_master_look_side"),
			QStringLiteral("flat_earth_master_lines_per_burst"), QStringLiteral("flat_earth_slave_lines_per_burst"),
			QStringLiteral("flat_earth_slave_source_burst_offset") };
		const QStringList doubleDatasets = {
			QStringLiteral("flat_earth_master_orbit_osv_start_gps"), QStringLiteral("flat_earth_master_orbit_osv_stop_gps"),
			QStringLiteral("flat_earth_master_geometry_start_gps"), QStringLiteral("flat_earth_master_geometry_stop_gps"),
			QStringLiteral("flat_earth_slave_orbit_osv_start_gps"), QStringLiteral("flat_earth_slave_orbit_osv_stop_gps"),
			QStringLiteral("flat_earth_slave_geometry_start_gps"), QStringLiteral("flat_earth_slave_geometry_stop_gps"),
			QStringLiteral("flat_earth_orbit_interpolation_margin_seconds"),
			QStringLiteral("flat_earth_rde_epsilon_phase"),
			QStringLiteral("flat_earth_rde_max_residual"), QStringLiteral("flat_earth_zero_doppler_max_residual"),
			QStringLiteral("flat_earth_rde_max_jacobian_condition"),
			QStringLiteral("flat_earth_slave_search_initial_half_window_seconds"),
			QStringLiteral("flat_earth_slave_search_maximum_half_window_seconds"),
			QStringLiteral("flat_earth_slave_search_expansion_factor"),
			QStringLiteral("flat_earth_wavelength_meters"),
			QStringLiteral("flat_earth_master_azimuth_steering_rate"), QStringLiteral("flat_earth_slave_azimuth_steering_rate"),
			QStringLiteral("flat_earth_master_azimuth_interval_seconds"), QStringLiteral("flat_earth_slave_azimuth_interval_seconds"),
			QStringLiteral("flat_earth_master_range_spacing"), QStringLiteral("flat_earth_slave_range_spacing"),
			QStringLiteral("flat_earth_master_slant_range_first_pixel"), QStringLiteral("flat_earth_slave_slant_range_first_pixel") };
		QStringList stringDatasets = {
			QStringLiteral("flat_earth_reference_phase_semantics"),
			QStringLiteral("flat_earth_model_status"),
			QStringLiteral("flat_earth_model_source_row_semantics"),
			QStringLiteral("flat_earth_model_timing_semantics"), QStringLiteral("flat_earth_processing_phase_semantics"),
			QStringLiteral("flat_earth_geolocation_coordinate_semantics"),
			QStringLiteral("flat_earth_master_orbit_source"), QStringLiteral("flat_earth_slave_orbit_source"),
			QStringLiteral("flat_earth_master_orbit_selection_reason"), QStringLiteral("flat_earth_slave_orbit_selection_reason"),
			QStringLiteral("flat_earth_master_look_side_source"),
			QStringLiteral("flat_earth_orbit_time_scale"), QStringLiteral("flat_earth_orbit_interpolation_strategy"),
			QStringLiteral("flat_earth_slave_registration_mapping_semantics") };
		const auto datasetExists = [&](const QString& dataset, int& exists) {
			const QByteArray inputUtf8 = inputPath.toUtf8();
			const QByteArray datasetUtf8 = dataset.toUtf8();
			if (Hdf5IO::datasetExists(inputUtf8.constData(), datasetUtf8.constData(), &exists) == 0) {
				return true;
			}
			if (errMsg) *errMsg = QStringLiteral("无法检查 H5 数据集 %1：%2").arg(dataset, inputPath);
			return false;
		};
		int rerampExists = 0;
		int rerampSemanticsExists = 0;
		if (!datasetExists(rerampDataset, rerampExists) ||
			!datasetExists(rerampSemanticsDataset, rerampSemanticsExists)) {
			return false;
		}

		const bool hasCompleteRerampProvenance = rerampExists != 0 && rerampSemanticsExists != 0;
		if (rerampExists != rerampSemanticsExists) {
			if (errMsg) *errMsg = QStringLiteral("注册 reramp provenance 不完整：%1").arg(inputPath);
			return false;
		}
        for (const QString& dataset : matrixDatasets) {
            cv::Mat value;
            if (!readMatFromH5(inputPath, dataset, value, -1, errMsg) || value.empty() ||
                !writeMatToH5(outputPath, dataset, value, errMsg)) return false;
        }
        for (const QString& dataset : intDatasets) {
            int value = 0;
            if (!readScalarFromH5(inputPath, dataset, value, errMsg) ||
                !writeScalarToH5(outputPath, dataset, value, errMsg)) return false;
        }
        for (const QString& dataset : doubleDatasets) {
            double value = 0.0;
            if (!readScalarFromH5(inputPath, dataset, value, errMsg)) {
                // 兼容未包含方位向采样时间间隔的历史存量数据
                if (dataset == QStringLiteral("flat_earth_master_azimuth_interval_seconds") ||
                    dataset == QStringLiteral("flat_earth_slave_azimuth_interval_seconds")) {
                    if (errMsg) errMsg->clear();
                    continue;
                }
                return false;
            }
            if (!writeScalarToH5(outputPath, dataset, value, errMsg)) return false;
        }
        for (const QString& dataset : stringDatasets) {
            std::string value;
            if (!readStringFromH5(inputPath, dataset, value, errMsg) || value.empty() ||
                !writeStringToH5(outputPath, dataset, value, errMsg)) return false;
        }
		if (hasCompleteRerampProvenance) {
			cv::Mat reramp;
			std::string rerampSemantics;
			if (!readMatFromH5(inputPath, rerampDataset, reramp, -1, errMsg) ||
				reramp.empty() || !readStringFromH5(inputPath, rerampSemanticsDataset, rerampSemantics, errMsg) ||
				rerampSemantics != "resampled_slave_deramp_demod_phase_registration_only_v1" ||
				!writeMatToH5(outputPath, rerampDataset, reramp, errMsg) ||
				!writeStringToH5(outputPath, rerampSemanticsDataset, rerampSemantics, errMsg)) return false;
		}
	}
    return copyCommonCoverageContract();
}

// --------------------------------------------------------------------------
// coherence 语义标签
//
// 背景：H5 中的 "coherence" 数据集在不同来源下含义不同：
//   - 干涉形成节点与 Core SBAS 当前写入的是 Utils::phase_axial_concentration() 的输出，
//     即二倍角轴向集中度 R2 = |mean(exp(i*2*phi))|，并不是复相干系数 gamma；
//   - 后续可能改为常规圆统计集中度 R1，或真正的 gamma。
// 三者量纲不同且**不可相互换算**（R2 = R1^4 仅在包裹高斯下近似成立，实测在
// 低相干区偏差可达 43%），故必须显式标注，不能依赖推断。
//
// 存量文件没有该标签，一律视为 kCoherenceSemanticsLegacyUnknown，不静态断言
// 为 R2 —— 写入方包括 UI 与 Core 多处，来源不可穷举。
// --------------------------------------------------------------------------

namespace CoherenceSemantics {
const char* const kComplexGamma     = "complex_gamma";
const char* const kPhaseCircularR1  = "phase_circular_r1";
const char* const kPhaseAxialR2     = "phase_axial_r2";
const char* const kLegacyUnknown    = "legacy_unknown";
}  // namespace CoherenceSemantics

namespace CoherenceSupport {
const char* const kValidSampleCountDataset = "coherence_valid_sample_count";
const char* const kWindowRangeDataset      = "coherence_window_rg";
const char* const kWindowAzimuthDataset    = "coherence_window_az";
}  // namespace CoherenceSupport

namespace {
// H5 中承载语义标签的数据集名
constexpr const char* kCoherenceSemanticsDataset = "coherence_semantics";

// 判断标签取值是否为已知的具体语义（不含 legacy_unknown）
bool isConcreteCoherenceSemantics(const QString& semantics)
{
    return semantics == QString::fromLatin1(CoherenceSemantics::kComplexGamma) ||
           semantics == QString::fromLatin1(CoherenceSemantics::kPhaseCircularR1) ||
           semantics == QString::fromLatin1(CoherenceSemantics::kPhaseAxialR2);
}
}  // namespace

bool writeCoherenceSemantics(const QString& filePath,
                             const QString& semantics,
                             QString* errMsg)
{
    if (!isConcreteCoherenceSemantics(semantics) &&
        semantics != QString::fromLatin1(CoherenceSemantics::kLegacyUnknown)) {
        if (errMsg) *errMsg = QStringLiteral("非法的 coherence 语义标签：%1").arg(semantics);
        return false;
    }
    return writeStringToH5(filePath,
                           QString::fromLatin1(kCoherenceSemanticsDataset),
                           semantics.toStdString(),
                           errMsg);
}

bool readCoherenceSemantics(const QString& filePath,
                            QString& semantics,
                            QString* errMsg)
{
    Hdf5Locker locker(filePath, 50);
    if (!locker.isLocked()) {
        if (errMsg) *errMsg = QStringLiteral("读取 coherence 语义标签时获取文件锁超时：%1").arg(filePath);
        return false;
    }

    int exists = 0;
    const QByteArray nativePath = filePath.toUtf8();
    const int existsRc = Hdf5IO::datasetExists(
        nativePath.constData(), kCoherenceSemanticsDataset, &exists);
    if (existsRc != 0) {
        if (errMsg) *errMsg = QStringLiteral("检查 coherence 语义标签失败：%1 (rc=%2)")
            .arg(filePath).arg(existsRc);
        return false;
    }
    if (exists == 0) {
        // 缺标签属正常情况（存量产品），不视为读取错误。
        semantics = QString::fromLatin1(CoherenceSemantics::kLegacyUnknown);
        return true;
    }

    std::string value;
    FormatConversion conversion;
    const int readRc = conversion.read_str_from_h5(
        nativePath.constData(), kCoherenceSemanticsDataset, value);
    if (readRc != 0) {
        if (errMsg) *errMsg = QStringLiteral("读取 coherence 语义标签失败：%1 (rc=%2)")
            .arg(filePath).arg(readRc);
        return false;
    }
    const QString parsed = QString::fromStdString(value).trimmed();
    // 标签存在但取值无法识别（例如更高版本写入的新取值）时退回未知，
    // 避免按错误语义解释数值
    semantics = isConcreteCoherenceSemantics(parsed)
                    ? parsed
                    : QString::fromLatin1(CoherenceSemantics::kLegacyUnknown);
    return true;
}

bool copyCoherenceSemantics(const QString& inputPath,
                            const QString& outputPath,
                            QString* errMsg)
{
    QString semantics;
    if (!readCoherenceSemantics(inputPath, semantics, errMsg)) {
        return false;
    }
    // 输入无标签时显式写入 legacy_unknown：派生产品若完全不带标签，
    // 下游无法区分“上游未标注”与“该文件未经标注流程”
    return writeCoherenceSemantics(outputPath, semantics, errMsg);
}

QString coherenceSemanticsDisplayName(const QString& semantics)
{
    if (semantics == QString::fromLatin1(CoherenceSemantics::kComplexGamma)) {
        return QStringLiteral("相干系数");
    }
    if (semantics == QString::fromLatin1(CoherenceSemantics::kPhaseCircularR1)) {
        return QStringLiteral("相位集中度");
    }
    if (semantics == QString::fromLatin1(CoherenceSemantics::kPhaseAxialR2)) {
        return QStringLiteral("二倍角相位集中度");
    }
    return QStringLiteral("相干性指标（语义未标注）");
}

bool validateDemPhaseInput(const QString& inputPath, QString* errMsg)
{
    constexpr int kLegacyPhaseProcessingSchemaVersion = 1;
    constexpr int kFlatEarthReferenceSchemaVersion = 2;
    int schemaVersion = 0;
    int flatRemoved = 0;
    int topoRemoved = 0;
    cv::Mat flatPhaseCoefficient, flatEarthReference;

    if (!readScalarFromH5(inputPath, QStringLiteral("phase_processing_schema_version"), schemaVersion, errMsg)) {
        if (errMsg) *errMsg = QStringLiteral("输入相位缺少处理状态契约，请从干涉形成节点重新运行：%1").arg(inputPath);
        return false;
    }
    if (schemaVersion != kLegacyPhaseProcessingSchemaVersion && schemaVersion != kFlatEarthReferenceSchemaVersion) {
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
	if (!validateFlatEarthReferenceContract(inputPath, errMsg) ||
		!validatePhaseValidityContract(inputPath, false, errMsg)) {
		return false;
	}
	if (schemaVersion == kFlatEarthReferenceSchemaVersion) {
		cv::Mat phaseValidMask;
		if (!readMatFromH5(inputPath, QStringLiteral("phase_valid_mask"), phaseValidMask, CV_8U, errMsg) ||
			phaseValidMask.empty() || cv::countNonZero(phaseValidMask) == 0) {
			if (errMsg) *errMsg = QStringLiteral("DEM 反演要求相位有效掩膜至少包含一个有效像元：%1").arg(inputPath);
			return false;
		}
	}
    if (topoRemoved != 0) {
        if (errMsg) *errMsg = QStringLiteral("DEM 反演要求保留地形相位，当前输入已去地形：%1").arg(inputPath);
        return false;
    }
    if (schemaVersion == kFlatEarthReferenceSchemaVersion) {
        if (!readMatFromH5(inputPath, QStringLiteral("flat_earth_reference_phase"), flatEarthReference, -1, errMsg) ||
            flatEarthReference.empty()) {
            if (errMsg) *errMsg = QStringLiteral("输入相位缺少有效的平地相位参考场：%1").arg(inputPath);
            return false;
        }
        cv::Mat phase;
        if (!readMatFromH5(inputPath, QStringLiteral("phase"), phase, -1, errMsg) || phase.empty() ||
            flatEarthReference.type() != CV_64F || flatEarthReference.size() != phase.size()) {
            if (errMsg) *errMsg = QStringLiteral("v2 平地相位参考场必须为与 phase 同尺寸的 CV_64F 数据集：%1").arg(inputPath);
            return false;
        }
    } else if (!readMatFromH5(inputPath, QStringLiteral("flat_phase_coefficient"), flatPhaseCoefficient, -1, errMsg) ||
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

bool validateDemValidityMaskForScene(const QString& demTifPath,
                                     const QString& validMaskTifPath,
                                     double requiredMinLon,
                                     double requiredMaxLon,
                                     double requiredMinLat,
                                     double requiredMaxLat,
                                     qint64* totalPixelCount,
                                     qint64* validPixelCount,
                                     QString* errorMessage)
{
    if (totalPixelCount) *totalPixelCount = 0;
    if (validPixelCount) *validPixelCount = 0;
    if (errorMessage) errorMessage->clear();
    if (!std::isfinite(requiredMinLon) || !std::isfinite(requiredMaxLon) ||
        !std::isfinite(requiredMinLat) || !std::isfinite(requiredMaxLat) ||
        requiredMaxLon <= requiredMinLon || requiredMaxLat <= requiredMinLat) {
        if (errorMessage) *errorMessage = QStringLiteral("输入影像地理范围无效，无法审核 DEM 覆盖。");
        return false;
    }

    const QFileInfo demInfo(demTifPath);
    const QFileInfo maskInfo(validMaskTifPath);
    if (!demInfo.isFile() || !demInfo.isReadable() ||
        !maskInfo.isFile() || !maskInfo.isReadable()) {
        if (errorMessage) *errorMessage = QStringLiteral("辅助 DEM 或其有效性 mask 不可读。");
        return false;
    }

    Hdf5Locker locker;
    GDALAllRegister();
    GDALDataset* demDataset = static_cast<GDALDataset*>(
        GDALOpen(QDir::toNativeSeparators(demInfo.absoluteFilePath()).toLocal8Bit().constData(), GA_ReadOnly));
    GDALDataset* maskDataset = static_cast<GDALDataset*>(
        GDALOpen(QDir::toNativeSeparators(maskInfo.absoluteFilePath()).toLocal8Bit().constData(), GA_ReadOnly));
    if (!demDataset || !maskDataset) {
        if (demDataset) GDALClose(demDataset);
        if (maskDataset) GDALClose(maskDataset);
        if (errorMessage) *errorMessage = QStringLiteral("无法打开辅助 DEM 或其有效性 mask。");
        return false;
    }

    const int columns = demDataset->GetRasterXSize();
    const int rows = demDataset->GetRasterYSize();
    bool compatible = columns > 0 && rows > 0 && demDataset->GetRasterCount() >= 1 &&
        maskDataset->GetRasterCount() == 1 && maskDataset->GetRasterXSize() == columns &&
        maskDataset->GetRasterYSize() == rows;

    double demTransform[6] = {0.0};
    double maskTransform[6] = {0.0};
    compatible = compatible && demDataset->GetGeoTransform(demTransform) == CE_None &&
        maskDataset->GetGeoTransform(maskTransform) == CE_None;
    if (compatible) {
        for (int index = 0; index < 6; ++index) {
            const double scale = std::max(1.0, std::max(std::abs(demTransform[index]),
                                                        std::abs(maskTransform[index])));
            if (std::abs(demTransform[index] - maskTransform[index]) > 1e-10 * scale) {
                compatible = false;
                break;
            }
        }
    }

    const char* demProjection = demDataset->GetProjectionRef();
    const char* maskProjection = maskDataset->GetProjectionRef();
    OGRSpatialReference demSrs;
    OGRSpatialReference maskSrs;
    OGRSpatialReference wgs84Srs;
    const bool projectionsValid = demProjection && maskProjection && demProjection[0] != '\0' &&
        maskProjection[0] != '\0' && demSrs.SetFromUserInput(demProjection) == OGRERR_NONE &&
        maskSrs.SetFromUserInput(maskProjection) == OGRERR_NONE &&
        wgs84Srs.importFromEPSG(4326) == OGRERR_NONE && demSrs.IsSame(&maskSrs) &&
        demSrs.IsSame(&wgs84Srs);
    const double transformTolerance = 1e-12;
    const bool coreGridCompatible = std::abs(demTransform[2]) <= transformTolerance &&
        std::abs(demTransform[4]) <= transformTolerance &&
        demTransform[1] > 0.0 && demTransform[5] < 0.0;
    compatible = compatible && projectionsValid && coreGridCompatible;

    // The source DEM is intentionally padded beyond the scene. Audit the scene
    // footprint plus one DEM pixel needed by interpolation, not the padded DEM
    // rectangle or its conservatively eroded outer edge.
    const double longitudeMargin = std::abs(demTransform[1]) + std::abs(demTransform[2]);
    const double latitudeMargin = std::abs(demTransform[4]) + std::abs(demTransform[5]);
    const double auditMinLon = requiredMinLon - longitudeMargin;
    const double auditMaxLon = requiredMaxLon + longitudeMargin;
    const double auditMinLat = requiredMinLat - latitudeMargin;
    const double auditMaxLat = requiredMaxLat + latitudeMargin;

    double rasterMinLon = std::numeric_limits<double>::infinity();
    double rasterMaxLon = -std::numeric_limits<double>::infinity();
    double rasterMinLat = std::numeric_limits<double>::infinity();
    double rasterMaxLat = -std::numeric_limits<double>::infinity();
    const double cornerColumns[4] = {0.0, static_cast<double>(columns), 0.0, static_cast<double>(columns)};
    const double cornerRows[4] = {0.0, 0.0, static_cast<double>(rows), static_cast<double>(rows)};
    for (int index = 0; index < 4; ++index) {
        const double longitude = demTransform[0] + cornerColumns[index] * demTransform[1] +
            cornerRows[index] * demTransform[2];
        const double latitude = demTransform[3] + cornerColumns[index] * demTransform[4] +
            cornerRows[index] * demTransform[5];
        rasterMinLon = std::min(rasterMinLon, longitude);
        rasterMaxLon = std::max(rasterMaxLon, longitude);
        rasterMinLat = std::min(rasterMinLat, latitude);
        rasterMaxLat = std::max(rasterMaxLat, latitude);
    }
    const bool auditBoundsCovered = rasterMinLon <= auditMinLon && rasterMaxLon >= auditMaxLon &&
        rasterMinLat <= auditMinLat && rasterMaxLat >= auditMaxLat;

    if (!compatible) {
        GDALClose(maskDataset);
        GDALClose(demDataset);
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "辅助 DEM/mask 必须尺寸与网格一致，并采用 Core 支持的无旋转 WGS 84 等经纬度网格。");
        }
        return false;
    }
    if (!auditBoundsCovered) {
        GDALClose(maskDataset);
        GDALClose(demDataset);
        if (errorMessage) *errorMessage = QStringLiteral("辅助 DEM 未完整覆盖输入影像范围及一像元插值保护区。");
        return false;
    }

    cv::Mat mask(rows, columns, CV_8UC1);
    GDALRasterBand* maskBand = maskDataset->GetRasterBand(1);
    const CPLErr readResult = maskBand->RasterIO(
        GF_Read, 0, 0, columns, rows, mask.data, columns, rows, GDT_Byte, 0, 0);
    GDALClose(maskDataset);
    if (readResult != CE_None) {
        GDALClose(demDataset);
        if (errorMessage) *errorMessage = QStringLiteral("读取辅助 DEM 有效性 mask 失败。");
        return false;
    }

    GDALRasterBand* demBand = demDataset->GetRasterBand(1);
    int hasDemNoData = 0;
    const double demNoData = demBand->GetNoDataValue(&hasDemNoData);
    cv::Mat demRow(1, columns, CV_32FC1);
    qint64 required = 0;
    qint64 valid = 0;
    bool binary = true;
    bool demReadable = true;
    bool demValuesConsistent = true;
    for (int row = 0; row < rows && binary && demValuesConsistent; ++row) {
        if (demBand->RasterIO(GF_Read, 0, row, columns, 1,
                              demRow.data, columns, 1, GDT_Float32, 0, 0) != CE_None) {
            demReadable = false;
            break;
        }
        const uchar* values = mask.ptr<uchar>(row);
        const float* elevations = demRow.ptr<float>(0);
        for (int column = 0; column < columns; ++column) {
            if (values[column] > 1) {
                binary = false;
                break;
            }
            const double centerColumn = column + 0.5;
            const double centerRow = row + 0.5;
            const double longitude = demTransform[0] + centerColumn * demTransform[1] +
                centerRow * demTransform[2];
            const double latitude = demTransform[3] + centerColumn * demTransform[4] +
                centerRow * demTransform[5];
            if (longitude >= auditMinLon && longitude <= auditMaxLon &&
                latitude >= auditMinLat && latitude <= auditMaxLat) {
                ++required;
                if (values[column] == 1) {
                    const double elevation = elevations[column];
                    const bool matchesNoData = hasDemNoData &&
                        ((std::isnan(demNoData) && std::isnan(elevation)) ||
                         (!std::isnan(demNoData) &&
                          std::abs(elevation - demNoData) < 1e-3));
                    if (!std::isfinite(elevation) || matchesNoData) {
                        demValuesConsistent = false;
                        break;
                    }
                    ++valid;
                }
            }
        }
    }
    GDALClose(demDataset);
    if (totalPixelCount) *totalPixelCount = required;
    if (validPixelCount) *validPixelCount = valid;
    if (!demReadable) {
        if (errorMessage) *errorMessage = QStringLiteral("读取辅助 DEM 高程数据失败。");
        return false;
    }
    if (!binary) {
        if (errorMessage) *errorMessage = QStringLiteral("辅助 DEM 有效性 mask 不是 0/1 二值栅格。");
        return false;
    }
    if (!demValuesConsistent) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("辅助 DEM 有效性 mask 将 NaN 或 NoData 高程标记为有效，覆盖契约不一致。");
        }
        return false;
    }
    if (required <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM 网格与输入影像范围没有可审核的重叠像元。");
        return false;
    }
    if (valid != required) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "辅助 DEM 缺少完整源支持：%1 / %2 像元有效（%3% 无效）。当前没有权威陆海 mask，不能把缺失区推断为海洋。")
                .arg(valid).arg(required)
                .arg(QString::number(100.0 * static_cast<double>(required - valid) /
                                     static_cast<double>(required), 'f', 2));
        }
        return false;
    }
    return true;
}

} // namespace NodeUtils
