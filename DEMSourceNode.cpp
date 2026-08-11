#include "DEMSourceNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include "EarthdataLoginDialog.h"
#include <QSettings>
#include <gdal_priv.h>
#include "Utils.h"
#include "QtNodes/internal/NodeDetailWindow.hpp"


#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QFileDialog>
#include <QInputDialog>
#include <QDirIterator>
#include <QtConcurrent/QtConcurrent>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <Hdf5IO.h>
#include <cmath>

namespace QtNodes {

namespace {
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

QByteArray demCanonicalMetadataHash(const QString& tifPath, const QString& h5Path)
{
    double minLon = 0.0, maxLon = 0.0, minLat = 0.0, maxLat = 0.0;
    if (!NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_min_lon"), minLon) ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_max_lon"), maxLon) ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_min_lat"), minLat) ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_max_lat"), maxLat) ||
        !(maxLon > minLon && maxLat > minLat)) return QByteArray();
    GDALAllRegister();
    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(
        QDir::toNativeSeparators(tifPath).toLocal8Bit().constData(), GA_ReadOnly));
    if (!dataset) return QByteArray();
    double transform[6] = {};
    const char* projection = dataset->GetProjectionRef();
    const bool valid = dataset->GetGeoTransform(transform) == CE_None && projection && *projection &&
        transform[1] != 0.0 && transform[5] != 0.0;
    QJsonObject metadata;
    if (valid) {
        metadata.insert(QStringLiteral("schemaVersion"), 1);
        metadata.insert(QStringLiteral("minLon"), minLon);
        metadata.insert(QStringLiteral("maxLon"), maxLon);
        metadata.insert(QStringLiteral("minLat"), minLat);
        metadata.insert(QStringLiteral("maxLat"), maxLat);
        metadata.insert(QStringLiteral("verticalDatum"), QStringLiteral("EGM96"));
        metadata.insert(QStringLiteral("resolutionX"), std::abs(transform[1]));
        metadata.insert(QStringLiteral("resolutionY"), std::abs(transform[5]));
        metadata.insert(QStringLiteral("resolutionUnit"), QStringLiteral("degree"));
        metadata.insert(QStringLiteral("resolutionCoordinateSemantic"), QStringLiteral("geographic_lon_lat"));
        metadata.insert(QStringLiteral("crsWkt"), QString::fromUtf8(projection));
    }
    GDALClose(dataset);
    return valid ? QCryptographicHash::hash(QJsonDocument(metadata).toJson(QJsonDocument::Compact),
                                            QCryptographicHash::Sha256).toHex() : QByteArray();
}

bool buildDemResourceMetadata(const QString& tifPath, const QString& h5Path, QJsonObject& metadata)
{
    double minLon = 0.0, maxLon = 0.0, minLat = 0.0, maxLat = 0.0;
    if (!NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_min_lon"), minLon) ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_max_lon"), maxLon) ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_min_lat"), minLat) ||
        !NodeUtils::readScalarFromH5(h5Path, QStringLiteral("dem_max_lat"), maxLat) ||
        !(maxLon > minLon && maxLat > minLat)) return false;
    GDALAllRegister();
    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(
        QDir::toNativeSeparators(tifPath).toLocal8Bit().constData(), GA_ReadOnly));
    if (!dataset) return false;
    double transform[6] = {};
    const char* projection = dataset->GetProjectionRef();
    const int width = dataset->GetRasterXSize();
    const int height = dataset->GetRasterYSize();
    const bool valid = dataset->GetGeoTransform(transform) == CE_None && projection && *projection &&
        width > 0 && height > 0 && transform[1] != 0.0 && transform[5] != 0.0;
    if (valid) {
        const double tifMinLon = std::min(transform[0], transform[0] + width * transform[1]);
        const double tifMaxLon = std::max(transform[0], transform[0] + width * transform[1]);
        const double tifMinLat = std::min(transform[3], transform[3] + height * transform[5]);
        const double tifMaxLat = std::max(transform[3], transform[3] + height * transform[5]);
        // The paired H5 declares the same raster extent.  Two pixels cover
        // normal GeoTIFF edge-versus-centre conventions; anything larger is
        // a mismatched identity file, not a harmless rounding difference.
        const double lonTolerance = std::abs(transform[1]) * 2.1;
        const double latTolerance = std::abs(transform[5]) * 2.1;
        if (std::abs(tifMinLon - minLon) > lonTolerance || std::abs(tifMaxLon - maxLon) > lonTolerance ||
            std::abs(tifMinLat - minLat) > latTolerance || std::abs(tifMaxLat - maxLat) > latTolerance) {
            GDALClose(dataset);
            return false;
        }
        metadata = QJsonObject();
        metadata.insert(QStringLiteral("schemaVersion"), 1);
        metadata.insert(QStringLiteral("minLon"), minLon);
        metadata.insert(QStringLiteral("maxLon"), maxLon);
        metadata.insert(QStringLiteral("minLat"), minLat);
        metadata.insert(QStringLiteral("maxLat"), maxLat);
        metadata.insert(QStringLiteral("verticalDatum"), QStringLiteral("EGM96"));
        metadata.insert(QStringLiteral("resolutionX"), std::abs(transform[1]));
        metadata.insert(QStringLiteral("resolutionY"), std::abs(transform[5]));
        metadata.insert(QStringLiteral("resolutionUnit"), QStringLiteral("degree"));
        metadata.insert(QStringLiteral("resolutionCoordinateSemantic"), QStringLiteral("geographic_lon_lat"));
        metadata.insert(QStringLiteral("crsWkt"), QString::fromUtf8(projection));
    }
    GDALClose(dataset);
    return valid;
}

QString demResourceId(const QString& tifPath, const QString& h5Path)
{
    const QByteArray rasterHash = sha256File(tifPath);
    const QByteArray identityHash = sha256File(h5Path);
    const QByteArray metadataHash = demCanonicalMetadataHash(tifPath, h5Path);
    if (rasterHash.isEmpty() || identityHash.isEmpty() || metadataHash.isEmpty()) return QString();
    return QStringLiteral("auxiliary_terrain_dem-%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(rasterHash + QByteArrayLiteral("|") + identityHash +
                                 QByteArrayLiteral("|") + metadataHash, QCryptographicHash::Sha256).toHex()));
}

bool ensureManagedDemResource(const QString& projectRoot,
                              const QString& projectXmlPath,
                              const QString& tifPath,
                              const QString& h5Path,
                              const QString& resourceId,
                              const QString& provenanceId,
                              QString* errorMessage)
{
    if (projectRoot.trimmed().isEmpty() || projectXmlPath.trimmed().isEmpty() ||
        resourceId.isEmpty() || provenanceId.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM 恢复缺少工程、资源或 provenance 标识。");
        return false;
    }
    if (provenanceId.contains(QLatin1Char('/')) || provenanceId.contains(QLatin1Char('\\')) ||
        provenanceId == QStringLiteral(".") || provenanceId == QStringLiteral("..")) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM 恢复 provenance 标识不安全。");
        return false;
    }
    QMap<QString, NodeUtils::AuxiliaryDemRegistryEntry> entries;
    QString registryError;
    const bool registryLoaded = NodeUtils::loadAuxiliaryDemRegistry(projectRoot, entries, &registryError);
    const QString registryPath = QDir(projectRoot).absoluteFilePath(QStringLiteral(".dem_resource_registry.json"));
    if (!registryLoaded && QFileInfo::exists(registryPath)) {
        if (errorMessage) {
            *errorMessage = registryError.isEmpty()
                ? QStringLiteral("DEM resource registry is unreadable; recovery was rejected.")
                : registryError;
        }
        return false;
    }
    const QDir root(projectRoot);
    const QDir resourceDir(root.absoluteFilePath(QStringLiteral(".dem_resources/%1").arg(resourceId)));
    const QString managedTif = resourceDir.absoluteFilePath(QStringLiteral("dem.tif"));
    const QString managedH5 = resourceDir.absoluteFilePath(QStringLiteral("identity.h5"));
    const QString manifestPath = resourceDir.absoluteFilePath(QStringLiteral("provenance_%1.json").arg(provenanceId));
    const QByteArray rasterHash = sha256File(tifPath);
    const QByteArray identityHash = sha256File(h5Path);
    QJsonObject metadata;
    if (rasterHash.isEmpty() || identityHash.isEmpty() || !buildDemResourceMetadata(tifPath, h5Path, metadata)) {
        if (errorMessage) *errorMessage = QStringLiteral("DEM 恢复无法验证受管文件或元数据。");
        return false;
    }
    const QString metadataHash = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(metadata).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    QJsonArray existingProvenanceHistory;
    bool existingResourceFilesVerified = false;
    bool registryHistoryMatches = false;
    const auto verifyExisting = [&]() {
        if (!QFileInfo(managedTif).isFile() || !QFileInfo(managedH5).isFile() ||
            sha256File(managedTif) != rasterHash || sha256File(managedH5) != identityHash) return false;

        const QFileInfoList manifestFiles = resourceDir.entryInfoList(
            QStringList() << QStringLiteral("provenance_*.json"), QDir::Files, QDir::Name);
        if (manifestFiles.isEmpty()) return false;

        const QString prefix = QStringLiteral("provenance_");
        const QString suffix = QStringLiteral(".json");
        QSet<QString> knownPins;
        bool currentProvenanceFound = false;
        QJsonArray verifiedHistory;
        for (const QFileInfo& manifestInfo : manifestFiles) {
            const QString fileName = manifestInfo.fileName();
            if (!fileName.startsWith(prefix) || !fileName.endsWith(suffix)) return false;
            const QString filePin = fileName.mid(prefix.size(),
                                                  fileName.size() - prefix.size() - suffix.size());
            if (filePin.isEmpty() || filePin.contains(QLatin1Char('/')) ||
                filePin.contains(QLatin1Char('\\'))) return false;

            QFile manifestFile(manifestInfo.absoluteFilePath());
            if (!manifestFile.open(QIODevice::ReadOnly)) return false;
            QJsonParseError parseError;
            const QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !manifestDocument.isObject()) return false;
            const QJsonObject manifest = manifestDocument.object();
            const QJsonObject raster = manifest.value(QStringLiteral("dem.tif")).toObject();
            const QJsonObject identity = manifest.value(QStringLiteral("identity.h5")).toObject();
            const QString pinnedId = manifest.value(QStringLiteral("pinnedProvenanceId")).toString();
            if (manifest.value(QStringLiteral("role")).toString() != QStringLiteral("auxiliary_terrain_dem") ||
                manifest.value(QStringLiteral("resourceId")).toString() != resourceId ||
                pinnedId != filePin || knownPins.contains(pinnedId) ||
                manifest.value(QStringLiteral("runId")).toString().isEmpty() ||
                manifest.value(QStringLiteral("runId")).toString() != pinnedId ||
                raster.value(QStringLiteral("path")).toString() != QStringLiteral("dem.tif") ||
                identity.value(QStringLiteral("path")).toString() != QStringLiteral("identity.h5") ||
                raster.value(QStringLiteral("sha256")).toString() != QString::fromLatin1(rasterHash) ||
                identity.value(QStringLiteral("sha256")).toString() != QString::fromLatin1(identityHash) ||
                manifest.value(QStringLiteral("canonicalMetadataHash")).toString() != metadataHash) return false;
            knownPins.insert(pinnedId);
            if (pinnedId == provenanceId) currentProvenanceFound = true;
            verifiedHistory.append(manifest);
        }
        if (!currentProvenanceFound || !QFileInfo::exists(manifestPath)) return false;
        existingProvenanceHistory = verifiedHistory;
        return true;
    };
    existingResourceFilesVerified = verifyExisting();
    if (existingResourceFilesVerified && registryLoaded && entries.contains(resourceId)) {
        const NodeUtils::AuxiliaryDemRegistryEntry entry = entries.value(resourceId);
        if (entry.tombstone) {
            if (errorMessage) *errorMessage = QStringLiteral("DEM resource is tombstoned; recovery was rejected.");
            return false;
        }
        QMap<QString, QJsonObject> registryHistoryByPin;
        for (const QJsonValue& value : entry.provenanceHistory) {
            const QJsonObject provenance = value.toObject();
            const QString pin = provenance.value(QStringLiteral("pinnedProvenanceId")).toString();
            if (pin.isEmpty() || registryHistoryByPin.contains(pin)) {
                registryHistoryByPin.clear();
                break;
            }
            registryHistoryByPin.insert(pin, provenance);
        }
        QMap<QString, QJsonObject> diskHistoryByPin;
        for (const QJsonValue& value : existingProvenanceHistory) {
            const QJsonObject provenance = value.toObject();
            diskHistoryByPin.insert(provenance.value(QStringLiteral("pinnedProvenanceId")).toString(), provenance);
        }
        registryHistoryMatches = entry.resourceId == resourceId &&
            entry.role == QStringLiteral("auxiliary_terrain_dem") &&
            entry.rasterHash == QString::fromLatin1(rasterHash) &&
            entry.identityH5Hash == QString::fromLatin1(identityHash) &&
            entry.canonicalMetadataHash == metadataHash &&
            registryHistoryByPin.size() == diskHistoryByPin.size();
        if (registryHistoryMatches) {
            for (auto it = diskHistoryByPin.constBegin(); it != diskHistoryByPin.constEnd(); ++it) {
                if (!registryHistoryByPin.contains(it.key()) ||
                    QJsonDocument(registryHistoryByPin.value(it.key())).toJson(QJsonDocument::Compact) !=
                    QJsonDocument(it.value()).toJson(QJsonDocument::Compact)) {
                    registryHistoryMatches = false;
                    break;
                }
            }
        }
    }
    if (existingResourceFilesVerified && registryHistoryMatches) return true;
    if (!existingResourceFilesVerified && QFileInfo::exists(resourceDir.absolutePath())) {
        if (errorMessage) *errorMessage = QStringLiteral("既有 DEM 资源目录未通过身份或 provenance 校验，拒绝恢复覆盖。");
        return false;
    }
    QJsonObject provenance;
    provenance.insert(QStringLiteral("role"), QStringLiteral("auxiliary_terrain_dem"));
    provenance.insert(QStringLiteral("resourceId"), resourceId);
    provenance.insert(QStringLiteral("pinnedProvenanceId"), provenanceId);
    provenance.insert(QStringLiteral("runId"), provenanceId);
    provenance.insert(QStringLiteral("canonicalMetadataHash"), metadataHash);
    QJsonObject rasterManifest;
    rasterManifest.insert(QStringLiteral("path"), QStringLiteral("dem.tif"));
    rasterManifest.insert(QStringLiteral("sha256"), QString::fromLatin1(rasterHash));
    provenance.insert(QStringLiteral("dem.tif"), rasterManifest);
    QJsonObject identityManifest;
    identityManifest.insert(QStringLiteral("path"), QStringLiteral("identity.h5"));
    identityManifest.insert(QStringLiteral("sha256"), QString::fromLatin1(identityHash));
    provenance.insert(QStringLiteral("identity.h5"), identityManifest);
    NodeUtils::AuxiliaryDemRegistryEntry entry;
    entry.resourceId = resourceId;
    entry.role = QStringLiteral("auxiliary_terrain_dem");
    entry.rasterHash = QString::fromLatin1(rasterHash);
    entry.identityH5Hash = QString::fromLatin1(identityHash);
    entry.canonicalMetadataHash = metadataHash;
    entry.managedRasterPath = QStringLiteral(".dem_resources/%1/dem.tif").arg(resourceId);
    entry.managedIdentityH5Path = QStringLiteral(".dem_resources/%1/identity.h5").arg(resourceId);
    entry.metadata = metadata;
    if (existingResourceFilesVerified) {
        entry.provenanceHistory = existingProvenanceHistory;
    } else {
        entry.provenanceHistory.append(provenance);
    }

    const QString transactionName = QStringLiteral("dem-resource-restore-%1").arg(
        QString::fromLatin1(QCryptographicHash::hash(
            (resourceId + QStringLiteral("|") + provenanceId).toUtf8(), QCryptographicHash::Sha256).toHex().left(24)));
    const QString markerPath = root.absoluteFilePath(transactionName + QStringLiteral("/resource-install.marker"));
    NodeUtils::OutputTransaction transaction;
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(projectRoot, transactionName,
                                           QStringList() << markerPath,
                                           QStringList() << tifPath << h5Path,
                                           transaction, &transactionError, nullptr,
                                           projectXmlPath)) {
        if (errorMessage) *errorMessage = transactionError;
        return false;
    }
    const auto abandon = [&](const QString& reason) {
        NodeUtils::abandonOutputTransaction(transaction, reason);
    };

    transaction.resourceAction = existingResourceFilesVerified ? QStringLiteral("reused")
                                                           : QStringLiteral("installed");
    transaction.provenanceOnlyUpdate = false;
    transaction.installedPath = existingResourceFilesVerified ? QString() : resourceDir.absolutePath();
    const QString installPath = root.absoluteFilePath(QStringLiteral(".dem_resources/.%1.restore-%2")
        .arg(resourceId, transaction.transactionId));
    transaction.resourceStagingPath = existingResourceFilesVerified ? QString() : installPath;
    transaction.provenanceManifestPath = existingResourceFilesVerified ? QString() :
        QDir(installPath).absoluteFilePath(QStringLiteral("provenance_%1.json").arg(provenanceId));
    transaction.provenanceDelta = provenance;
    transaction.resourceRegistryMutationPrepared = true;
    transaction.resourceRegistryBackupPath = registryPath + QStringLiteral(".backup-") + transaction.transactionId;
    if (QFileInfo::exists(registryPath)) {
        QFile oldRegistry(registryPath);
        if (!oldRegistry.open(QIODevice::ReadOnly) ||
            !QFile::copy(registryPath, transaction.resourceRegistryBackupPath)) {
            abandon(QStringLiteral("cannot back up DEM resource registry"));
            if (errorMessage) *errorMessage = QStringLiteral("无法准备 DEM resource registry 恢复备份。");
            return false;
        }
        transaction.resourceRegistryBackupHash = QString::fromLatin1(
            QCryptographicHash::hash(oldRegistry.readAll(), QCryptographicHash::Sha256).toHex());
        transaction.baseRegistryHash = transaction.resourceRegistryBackupHash;
    }
    if (!NodeUtils::persistOutputTransactionState(transaction, &transactionError)) {
        abandon(QStringLiteral("cannot persist DEM resource recovery transaction"));
        if (errorMessage) *errorMessage = transactionError;
        return false;
    }

    if (!existingResourceFilesVerified) {
        if (!QDir().mkpath(QDir(installPath).absolutePath()) ||
            !QFile::copy(tifPath, QDir(installPath).absoluteFilePath(QStringLiteral("dem.tif"))) ||
            !QFile::copy(h5Path, QDir(installPath).absoluteFilePath(QStringLiteral("identity.h5")))) {
            abandon(QStringLiteral("cannot stage DEM resource recovery installation"));
            if (errorMessage) *errorMessage = QStringLiteral("无法安装 DEM 恢复资源。");
            return false;
        }
        QSaveFile manifest(transaction.provenanceManifestPath);
        if (!manifest.open(QIODevice::WriteOnly) ||
            manifest.write(QJsonDocument(provenance).toJson(QJsonDocument::Compact)) < 0 ||
            !manifest.commit() ||
            QFileInfo::exists(resourceDir.absolutePath()) ||
            !QDir().rename(installPath, resourceDir.absolutePath())) {
            abandon(QStringLiteral("cannot promote DEM resource recovery installation"));
            if (errorMessage) *errorMessage = QStringLiteral("无法提交 DEM 恢复资源目录。");
            return false;
        }
        transaction.resourceStagingPath.clear();
        transaction.provenanceManifestPath = resourceDir.absoluteFilePath(
            QStringLiteral("provenance_%1.json").arg(provenanceId));
        if (!NodeUtils::persistOutputTransactionState(transaction, &transactionError)) {
            abandon(QStringLiteral("cannot persist promoted DEM resource recovery installation"));
            if (errorMessage) *errorMessage = transactionError;
            return false;
        }
    }

    if (existingResourceFilesVerified && !registryHistoryMatches && registryLoaded && entries.contains(resourceId) &&
        !NodeUtils::removeAuxiliaryDemRegistryEntry(projectRoot, resourceId, &transactionError)) {
        abandon(QStringLiteral("cannot rebuild inconsistent DEM resource registry"));
        if (errorMessage) *errorMessage = transactionError;
        return false;
    }
    if (!NodeUtils::mergeAuxiliaryDemRegistryEntry(projectRoot, entry, &transactionError)) {
        abandon(QStringLiteral("cannot merge DEM resource recovery registry"));
        if (errorMessage) *errorMessage = transactionError;
        return false;
    }
    transaction.resourceRegistryCommitted = true;
    transaction.resourceRegistryCommittedHash = QString::fromLatin1(sha256File(registryPath));
    transaction.newRegistryGeneration = fileGeneration(registryPath);
    if (transaction.resourceRegistryCommittedHash.isEmpty() ||
        transaction.newRegistryGeneration.isEmpty() ||
        !NodeUtils::persistOutputTransactionState(transaction, &transactionError) ||
        !NodeUtils::completeAuxiliaryDemResourceTransaction(transaction, &transactionError)) {
        abandon(QStringLiteral("cannot complete DEM resource recovery transaction"));
        if (errorMessage) *errorMessage = transactionError.isEmpty()
            ? QStringLiteral("无法完成 DEM 恢复资源事务。") : transactionError;
        return false;
    }
    return true;
}

ProductDescriptor::Ptr auxiliaryDemReferenceDescriptor(const QString& resourceId,
                                                        const QString& pinnedProvenanceId)
{
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), QStringLiteral("DEMSource"));
    provenance.insert(QStringLiteral("output_port"),
                      QStringLiteral("dem_source.output.auxiliary_terrain_dem.reference"));
    provenance.insert(QStringLiteral("resourceId"), resourceId);
    provenance.insert(QStringLiteral("pinnedProvenanceId"), pinnedProvenanceId);
    return ProductDescriptor::create(QStringLiteral("auxiliary_terrain_dem"),
                                     QStringLiteral("sat-explorer-product"), 1,
                                     ProductState::Committed, resourceId, provenance);
}

ProductDescriptor::Ptr auxiliaryDemEntityDescriptor(const QString& resourceId,
                                                     const QString& pinnedProvenanceId)
{
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), QStringLiteral("DEMSource"));
    provenance.insert(QStringLiteral("output_port"),
                      QStringLiteral("dem_source.output.auxiliary_terrain_dem.entity"));
    provenance.insert(QStringLiteral("resourceId"), resourceId);
    provenance.insert(QStringLiteral("pinnedProvenanceId"), pinnedProvenanceId);
    return ProductDescriptor::create(QStringLiteral("auxiliary_terrain_dem"),
                                     QStringLiteral("sat-explorer-product"), 1,
                                     ProductState::Committed, resourceId, provenance);
}

bool isLegacyAuxiliaryDemEntityDescriptor(const ProductDescriptor::Ptr& descriptor)
{
    if (!descriptor || descriptor->productType() != QStringLiteral("auxiliary_terrain_dem") ||
        descriptor->schemaId() != QStringLiteral("sat-explorer-product") ||
        descriptor->schemaVersion() != 1 || descriptor->state() != ProductState::Committed) {
        return false;
    }

    const QMap<QString, QString> provenance = descriptor->provenance();
    return provenance.value(QStringLiteral("producer")) == QStringLiteral("DEMSource") &&
        provenance.value(QStringLiteral("output_port")) ==
            QStringLiteral("dem_source.output.auxiliary_terrain_dem");
}

bool canUseCommittedDemResource(const QString& projectRoot, const QString& sourceTif,
                                const QString& sourceH5, const QString& resourceId,
                                const QString& provenanceId)
{
    if (resourceId.isEmpty() || provenanceId.isEmpty()) return false;

    const QFileInfo sourceTifInfo(sourceTif);
    const QFileInfo sourceH5Info(sourceH5);
    const QDir resourceDir(QDir(projectRoot).absoluteFilePath(
        QStringLiteral(".dem_resources/%1").arg(resourceId)));
    const QFileInfo managedTif(resourceDir.absoluteFilePath(QStringLiteral("dem.tif")));
    const QFileInfo managedH5(resourceDir.absoluteFilePath(QStringLiteral("identity.h5")));
    const QFileInfo provenanceFile(resourceDir.absoluteFilePath(
        QStringLiteral("provenance_%1.json").arg(provenanceId)));
    if (!sourceTifInfo.isFile() || !sourceH5Info.isFile() || !managedTif.isFile() ||
        !managedH5.isFile() || !provenanceFile.isFile() ||
        sourceTifInfo.size() != managedTif.size() || sourceH5Info.size() != managedH5.size()) {
        return false;
    }

    QMap<QString, NodeUtils::AuxiliaryDemRegistryEntry> entries;
    if (!NodeUtils::loadAuxiliaryDemRegistry(projectRoot, entries, nullptr) ||
        !entries.contains(resourceId)) {
        return false;
    }
    const NodeUtils::AuxiliaryDemRegistryEntry entry = entries.value(resourceId);
    if (entry.tombstone || entry.role != QStringLiteral("auxiliary_terrain_dem") ||
        entry.rasterHash.isEmpty() || entry.identityH5Hash.isEmpty() ||
        entry.canonicalMetadataHash.isEmpty() ||
        QDir::cleanPath(QDir(projectRoot).absoluteFilePath(entry.managedRasterPath)) !=
            QDir::cleanPath(managedTif.absoluteFilePath()) ||
        QDir::cleanPath(QDir(projectRoot).absoluteFilePath(entry.managedIdentityH5Path)) !=
            QDir::cleanPath(managedH5.absoluteFilePath())) {
        return false;
    }

    QFile manifest(provenanceFile.absoluteFilePath());
    if (!manifest.open(QIODevice::ReadOnly)) return false;
    const QJsonObject provenance = QJsonDocument::fromJson(manifest.readAll()).object();
    if (provenance.value(QStringLiteral("resourceId")).toString() != resourceId ||
        provenance.value(QStringLiteral("pinnedProvenanceId")).toString() != provenanceId ||
        provenance.value(QStringLiteral("runId")).toString() != provenanceId ||
        provenance.value(QStringLiteral("dem.tif")).toObject().value(QStringLiteral("sha256")).toString() != entry.rasterHash ||
        provenance.value(QStringLiteral("identity.h5")).toObject().value(QStringLiteral("sha256")).toString() != entry.identityH5Hash ||
        provenance.value(QStringLiteral("canonicalMetadataHash")).toString() != entry.canonicalMetadataHash) {
        return false;
    }

    NodeUtils::Hdf5Locker locker(managedH5.absoluteFilePath(), 50);
    std::string descriptorJson;
    if (!locker.isLocked() || Hdf5IO::readString(
            QDir::toNativeSeparators(managedH5.absoluteFilePath()).toLocal8Bit().constData(),
            "semantic_product_descriptor", descriptorJson) != 0) {
        return false;
    }
    const ProductDescriptor::Ptr descriptor = ProductDescriptor::fromJson(
        QJsonDocument::fromJson(QByteArray::fromStdString(descriptorJson)).object());
    return descriptor && descriptor->productType() == QStringLiteral("auxiliary_terrain_dem");
}

bool restoreManagedDemResource(const QString& projectRoot, const QString& projectXmlPath,
                               const QString& sourceTif, const QString& sourceH5,
                               const QString& savedResourceId, const QString& savedProvenanceId,
                               QString& resourceId, QString& provenanceId, QString* errorMessage)
{
    resourceId = savedResourceId;
    provenanceId = savedProvenanceId;
    if (canUseCommittedDemResource(projectRoot, sourceTif, sourceH5, resourceId, provenanceId)) {
        return true;
    }

    resourceId = demResourceId(sourceTif, sourceH5);
    if (resourceId.isEmpty()) return false;
    if (provenanceId.isEmpty()) {
        NodeUtils::loadCommittedOutputManifestRunId(projectRoot, QFileInfo(sourceH5).absoluteDir().dirName(), provenanceId);
    }
    return !provenanceId.isEmpty() && ensureManagedDemResource(projectRoot, projectXmlPath,
                                                                 sourceTif, sourceH5,
                                                                 resourceId, provenanceId, errorMessage);
}

QByteArray stagedOutputHash(const NodeUtils::OutputTransaction& transaction, const QString& fileName)
{
    const QString manifestPath = QDir(transaction.projectRoot).absoluteFilePath(
        transaction.stagingName + QStringLiteral("/.node_output_manifest.json"));
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) return QByteArray();
    const QJsonArray outputs = QJsonDocument::fromJson(manifest.readAll()).object()
        .value(QStringLiteral("outputs")).toArray();
    for (const QJsonValue& value : outputs) {
        const QJsonObject output = value.toObject();
        if (output.value(QStringLiteral("name")).toString() == fileName) {
            return output.value(QStringLiteral("sha256")).toString().toLatin1();
        }
    }
    return QByteArray();
}
}

struct DemFinalizationPreparation
{
    bool success = false;
    QString errorMessage;
    NodeUtils::OutputTransaction transaction;
    QString stagedH5Path;
    QString stagedTifPath;
    QString managedH5Path;
    QString managedTifPath;
    QString resourceDirectory;
    QString resourceId;
    QString provenanceId;
    QByteArray rasterHash;
    QByteArray identityHash;
    QByteArray canonicalMetadataHash;
    QJsonObject resourceMetadata;
    QJsonObject provenance;
    bool resourceAlreadyCommitted = false;
};

namespace {

DemFinalizationPreparation prepareDemFinalization(NodeUtils::OutputTransaction transaction,
                                                  const QString& workerOutputH5Path)
{
    DemFinalizationPreparation result;
    result.transaction = transaction;
    QString error;
    if (!NodeUtils::validateStagedOutputTransaction(result.transaction, &error)) {
        result.errorMessage = error;
        return result;
    }

    {
        NodeUtils::Hdf5Locker locker;
        if (!NodeUtils::validateStagedH5Datasets(result.transaction,
                                                  QStringList() << QStringLiteral("dem")
                                                                << QStringLiteral("dem_x")
                                                                << QStringLiteral("dem_y")
                                                                << QStringLiteral("dem_z")
                                                                << QStringLiteral("lon")
                                                                << QStringLiteral("lat"),
                                                  &error)) {
            result.errorMessage = error;
            return result;
        }
    }
    if (!NodeUtils::workerOutputsMatchManifest(QStringList() << transaction.expectedFileNames.value(0),
                                               QStringList() << workerOutputH5Path, &error)) {
        result.errorMessage = error;
        return result;
    }

    const QDir stagedOutput(QDir(transaction.projectRoot).absoluteFilePath(transaction.stagingName));
    for (const QString& fileName : transaction.expectedFileNames) {
        const QString path = stagedOutput.absoluteFilePath(fileName);
        if (fileName.endsWith(QStringLiteral(".h5"), Qt::CaseInsensitive)) result.stagedH5Path = path;
        if (fileName.endsWith(QStringLiteral(".tif"), Qt::CaseInsensitive)) result.stagedTifPath = path;
    }
    if (result.stagedH5Path.isEmpty() || result.stagedTifPath.isEmpty()) {
        result.errorMessage = QStringLiteral("Promoted DEM transaction is missing its H5 or TIFF output.");
        return result;
    }

    result.rasterHash = stagedOutputHash(result.transaction, QFileInfo(result.stagedTifPath).fileName());
    result.identityHash = stagedOutputHash(result.transaction, QFileInfo(result.stagedH5Path).fileName());
    if (result.rasterHash.isEmpty() || result.identityHash.isEmpty() ||
        !buildDemResourceMetadata(result.stagedTifPath, result.stagedH5Path, result.resourceMetadata)) {
        result.errorMessage = QStringLiteral("DEM resource validation failed while preparing the managed resource.");
        return result;
    }

    result.canonicalMetadataHash = QCryptographicHash::hash(
        QJsonDocument(result.resourceMetadata).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    result.resourceId = QStringLiteral("auxiliary_terrain_dem-%1").arg(
        QString::fromLatin1(QCryptographicHash::hash(
            result.rasterHash + QByteArrayLiteral("|") + result.identityHash + QByteArrayLiteral("|") +
            result.canonicalMetadataHash, QCryptographicHash::Sha256).toHex()));
    result.provenanceId = result.transaction.runId;

    const QDir resourcesRoot(QDir(transaction.projectRoot).absoluteFilePath(QStringLiteral(".dem_resources")));
    const QDir resourceDir(resourcesRoot.absoluteFilePath(result.resourceId));
    result.resourceDirectory = resourceDir.absolutePath();
    result.managedTifPath = resourceDir.absoluteFilePath(QStringLiteral("dem.tif"));
    result.managedH5Path = resourceDir.absoluteFilePath(QStringLiteral("identity.h5"));
    const bool hasManagedTif = QFileInfo::exists(result.managedTifPath);
    const bool hasManagedH5 = QFileInfo::exists(result.managedH5Path);
    if (QFileInfo::exists(resourceDir.absolutePath()) && !(hasManagedTif && hasManagedH5)) {
        result.errorMessage = QStringLiteral("已存在不完整 DEM 资源目录，拒绝覆盖或复用。");
        return result;
    }
    result.resourceAlreadyCommitted = hasManagedTif && hasManagedH5;
    if (result.resourceAlreadyCommitted &&
        (sha256File(result.managedTifPath) != result.rasterHash ||
         sha256File(result.managedH5Path) != result.identityHash)) {
        result.errorMessage = QStringLiteral("已存在 DEM 资源目录的内容 hash 不匹配，拒绝复用。");
        return result;
    }

    result.provenance.insert(QStringLiteral("role"), QStringLiteral("auxiliary_terrain_dem"));
    result.provenance.insert(QStringLiteral("resourceId"), result.resourceId);
    result.provenance.insert(QStringLiteral("pinnedProvenanceId"), result.provenanceId);
    result.provenance.insert(QStringLiteral("runId"), result.provenanceId);
    result.provenance.insert(QStringLiteral("canonicalMetadataHash"), QString::fromLatin1(result.canonicalMetadataHash));
    QJsonObject rasterManifest;
    rasterManifest.insert(QStringLiteral("path"), QStringLiteral("dem.tif"));
    rasterManifest.insert(QStringLiteral("sha256"), QString::fromLatin1(result.rasterHash));
    result.provenance.insert(QStringLiteral("dem.tif"), rasterManifest);
    QJsonObject identityManifest;
    identityManifest.insert(QStringLiteral("path"), QStringLiteral("identity.h5"));
    identityManifest.insert(QStringLiteral("sha256"), QString::fromLatin1(result.identityHash));
    result.provenance.insert(QStringLiteral("identity.h5"), identityManifest);

    if (!result.resourceAlreadyCommitted) {
        const QString installPath = result.transaction.resourceStagingPath;
        if (installPath.isEmpty() || QDir(installPath).exists() ||
            !QDir().mkpath(installPath) ||
            !QFile::copy(result.stagedTifPath, QDir(installPath).absoluteFilePath(QStringLiteral("dem.tif"))) ||
            !QFile::copy(result.stagedH5Path, QDir(installPath).absoluteFilePath(QStringLiteral("identity.h5")))) {
            QDir(installPath).removeRecursively();
            result.errorMessage = QStringLiteral("无法准备 DEM 资源隔离安装目录。");
            return result;
        }
        QSaveFile provenanceFile(QDir(installPath).absoluteFilePath(
            QStringLiteral("provenance_%1.json").arg(result.provenanceId)));
        if (!provenanceFile.open(QIODevice::WriteOnly) ||
            provenanceFile.write(QJsonDocument(result.provenance).toJson(QJsonDocument::Compact)) < 0 ||
            !provenanceFile.commit()) {
            QDir(installPath).removeRecursively();
            result.errorMessage = QStringLiteral("无法提交 DEM provenance manifest。");
            return result;
        }
    }

    result.success = true;
    return result;
}

} // namespace

DEMSourceNode::DEMSourceNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_demSourceCombo(nullptr)
    , m_resolutionCombo(nullptr)
    , m_customResEdit(nullptr)
    , m_cacheDirEdit(nullptr)
    , m_browseCacheBtn(nullptr)
    , m_clearCacheBtn(nullptr)
    , m_cacheSizeLabel(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_demSource(2)
    , m_resMode(0)
    , m_customResolution(30.0)
    , m_cacheDir("")
    , m_outputNodeName("")
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    m_workflowProducerIdentity = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setExecutionMode(ExecutionMode::Automatic);
    
    // 初始化默认缓存目录，优先使用项目的全局默认高程数据路径
    auto* iface = NodeUtils::getProjectContext(nullptr);
    if (iface) {
        m_cacheDir = NodeUtils::getGlobalDemPath(iface);
    }
}

DEMSourceNode::~DEMSourceNode()
{
    ++m_executionGeneration;
    ++m_remedyGeneration;
    if (m_finalizationWatcher.isRunning()) {
        m_finalizationWatcher.cancel();
    }
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    if (m_workerThread) {
        m_workerThread->StopProcess();
    }
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("node destroyed"), projectXml());
}

unsigned int DEMSourceNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType DEMSourceNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "Imported File"};
    else
    {
        if (portIndex == 0)
            return NodeDataType{"auxiliary_dem", "Auxiliary DEM"};
        else if (portIndex == 1)
            return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

ProductInputContract DEMSourceNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("dem_source.input.complex_sar_coverage");
    contract.allowedProductTypes = QStringList()
        << QStringLiteral("complex_sar")
        << QStringLiteral("cropped_complex_sar")
        << QStringLiteral("coregistered_complex_sar")
        << QStringLiteral("debursted_complex_sar")
        << QStringLiteral("deramped_complex_sar")
        << QStringLiteral("merged_complex_sar")
        << QStringLiteral("back_geocoded_complex_sar")
        << QStringLiteral("sentinel1_burst_sar");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract DEMSourceNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    if (portIndex == 0) {
        contract.semanticId = QStringLiteral("dem_source.output.auxiliary_terrain_dem.entity");
        contract.publishedProductTypes = QStringList() << QStringLiteral("auxiliary_terrain_dem");
    } else if (portIndex == 1) {
        contract.semanticId = QStringLiteral("dem_source.output.preview");
        contract.publishedProductTypes = QStringList() << QStringLiteral("preview");
    }
    return contract;
}

bool DEMSourceNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString DEMSourceNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入影像");
    } else {
        if (portIndex == 0)
            return QStringLiteral("实体 DEM");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool DEMSourceNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out && portIndex == 1;
}

void DEMSourceNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_referenceData.reset();
        m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
    }
}

void DEMSourceNode::clearPublishedOutputs()
{
    ++m_remedyGeneration;
    m_remedyH5Path.clear();
    m_remedyJpgPath.clear();
    m_outputData.reset();
    m_referenceData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
}

std::shared_ptr<NodeData> DEMSourceNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed && executionState() != ExecutionState::Warning)
        return nullptr;

    std::shared_ptr<NodeData> data;
    if (port == 0) {
        data = m_outputData;
    } else if (port == 1) {
        data = m_imageInfoData;
    }
    return data;
}

QWidget* DEMSourceNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject DEMSourceNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["demSource"] = m_demSource;
    modelJson["resMode"] = m_resMode;
    modelJson["customResolution"] = m_customResolution;
    modelJson["cacheDir"] = m_cacheDirEdit ? m_cacheDirEdit->text() : m_cacheDir;
    modelJson["acquisitionMode"] = m_isLocalImport ? QStringLiteral("local_import") : QStringLiteral("download");
    modelJson["localImportResourceId"] = m_localImportResourceId;
    modelJson["localImportPinnedProvenanceId"] = m_localImportProvenanceId;
    modelJson["legacyReferenceResourceId"] = m_referenceData ? m_referenceData->resourceId() : QString();
    modelJson["legacyReferencePinnedProvenanceId"] = m_referenceData ? m_referenceData->pinnedProvenanceId() : QString();
    modelJson["workflowDemLabel"] = m_workflowLabel;
    modelJson["workflowDemProducerIdentity"] = m_workflowProducerIdentity;

    return modelJson;
}

void DEMSourceNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vSource = json["demSource"];
    if (!vSource.isUndefined()) m_demSource = vSource.toInt();

    QJsonValue vResMode = json["resMode"];
    if (!vResMode.isUndefined()) m_resMode = vResMode.toInt();

    QJsonValue vCustomRes = json["customResolution"];
    if (!vCustomRes.isUndefined()) m_customResolution = vCustomRes.toDouble();

    QJsonValue vCache = json["cacheDir"];
    if (!vCache.isUndefined()) m_cacheDir = vCache.toString();
    m_isLocalImport = json.value("acquisitionMode").toString() == QStringLiteral("local_import");
    m_localImportResourceId = json.value("localImportResourceId").toString().trimmed();
    m_localImportProvenanceId = json.value("localImportPinnedProvenanceId").toString().trimmed();
    m_savedResourceId = json.value("legacyReferenceResourceId").toString().trimmed();
    m_savedResourceProvenanceId = json.value("legacyReferencePinnedProvenanceId").toString().trimmed();
    m_workflowLabel = json.value("workflowDemLabel").toString().trimmed();
    m_workflowProducerIdentity = json.value("workflowDemProducerIdentity").toString().trimmed();
    const bool claimLegacyWorkflowProducer = m_workflowProducerIdentity.isEmpty();
    if (m_workflowProducerIdentity.isEmpty()) {
        m_workflowProducerIdentity = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // 先加载基础节点，然后再刷新 UI
    ExecutableNodeDelegateModel::load(json);

    if (!m_workflowLabel.isEmpty()) {
        QString labelError;
        if (!NodeUtils::restoreWorkflowAuxiliaryDemLabel(projectPath(), m_workflowLabel,
                                                          m_workflowProducerIdentity,
                                                          claimLegacyWorkflowProducer, &labelError)) {
            setLastErrorMessage(labelError.isEmpty()
                ? QStringLiteral("流程 DEM 标签无法恢复。") : labelError);
            setState(ExecutionState::Error);
            return;
        }
    }

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_demSourceCombo) m_demSourceCombo->setCurrentIndex(m_demSourceCombo->findData(m_demSource));
    if (m_resolutionCombo) m_resolutionCombo->setCurrentIndex(m_resMode);
    if (m_customResEdit) m_customResEdit->setText(QString::number(m_customResolution));
    if (m_cacheDirEdit) m_cacheDirEdit->setText(m_cacheDir);

    onResolutionModeChanged(m_resMode);
    updateCacheSizeLabel();
    if (m_isLocalImport) {
        AuxiliaryDemReferenceData reference(m_localImportResourceId, m_localImportProvenanceId, 1);
        NodeUtils::AuxiliaryDemBinding binding;
        QString error;
        if (m_localImportResourceId.isEmpty() || m_localImportProvenanceId.isEmpty() ||
            !NodeUtils::resolveAuxiliaryDemBinding(projectPath(), reference, binding, &error)) {
            setLastErrorMessage(error.isEmpty() ? QStringLiteral("本地导入 DEM 资源无法恢复。") : error);
            setState(ExecutionState::Error);
            return;
        }
        m_outputData = std::make_shared<AuxiliaryDemData>(binding.rasterPath, binding.identityH5Path,
                                                           binding.resourceId, binding.pinnedProvenanceId,
                                                           m_outputNodeName);
        m_outputData->setProductDescriptor(auxiliaryDemEntityDescriptor(binding.resourceId, binding.pinnedProvenanceId));
        setOutputData(0, m_outputData);
        Q_EMIT dataUpdated(0);
        setProgress(100);
        setState(ExecutionState::Completed);
    }
}

void DEMSourceNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void DEMSourceNode::createWidget()
{
    // 动态确定默认缓存目录，避免使用运行目录下的 dem，统一使用工程路径下的 .dem_cache
    if (m_cacheDir.isEmpty())
    {
        QString projPath = projectPath();
        if (!projPath.isEmpty())
        {
            m_cacheDir = QDir::toNativeSeparators(projPath + "/.dem_cache");
        }
    }

    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_referenceData) m_referenceData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene);
        }
    };

    const int labelWidth = 90;

    // 1. DEM 数据源
    auto* sourceLayout = new QHBoxLayout();
    QLabel* sourceLabel = new QLabel(QStringLiteral("DEM 数据源"));
    sourceLabel->setFixedWidth(labelWidth);
    sourceLayout->addWidget(sourceLabel);
    m_demSourceCombo = new QComboBox();
    m_demSourceCombo->addItem("Copernicus DEM (30m)", 2);
    m_demSourceCombo->addItem("SRTM 1\" (~30m)", 0);
    m_demSourceCombo->addItem("SRTM 3\" (~90m)", 1);
    m_demSourceCombo->addItem("ASTER GDEM v3 (30m)", 3);
    m_demSourceCombo->setCurrentIndex(m_demSourceCombo->findData(m_demSource));
    connect(m_demSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        const int demSource = m_demSourceCombo->itemData(index).toInt();
        if (m_demSource != demSource) {
            if (!confirmParameterChange()) {
                m_demSourceCombo->blockSignals(true);
                m_demSourceCombo->setCurrentIndex(m_demSourceCombo->findData(m_demSource));
                m_demSourceCombo->blockSignals(false);
                return;
            }
            m_demSource = demSource;
            invalidateNodeData();
        }
    });
    sourceLayout->addWidget(m_demSourceCombo);
    layout->addLayout(sourceLayout);

    // 账户状态与登录注销按钮
    auto* loginLayout = new QHBoxLayout();
    QLabel* loginLabel = new QLabel(QStringLiteral("账户状态"));
    loginLabel->setFixedWidth(labelWidth);
    loginLayout->addWidget(loginLabel);

    m_loginStatusLabel = new QLabel();
    m_loginBtn = new QPushButton(QStringLiteral("登录"));
    m_loginBtn->setFixedWidth(50);
    m_logoutBtn = new QPushButton(QStringLiteral("注销"));
    m_logoutBtn->setFixedWidth(50);

    loginLayout->addWidget(m_loginStatusLabel);
    loginLayout->addWidget(m_loginBtn);
    loginLayout->addWidget(m_logoutBtn);
    layout->addLayout(loginLayout);

    connect(m_demSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DEMSourceNode::updateLoginStatus);
    connect(m_loginBtn, &QPushButton::clicked, this, [this]() {
        EarthdataLoginDialog dlg(nullptr);
        if (dlg.exec() == QDialog::Accepted) {
            updateLoginStatus();
        }
    });
    connect(m_logoutBtn, &QPushButton::clicked, this, [this]() {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        settings.remove("DEM/EarthdataUser");
        settings.remove("DEM/EarthdataPassword");
        updateLoginStatus();
    });

    // 2. 目标分辨率模式
    auto* resLayout = new QHBoxLayout();
    QLabel* resLabel = new QLabel(QStringLiteral("目标分辨率"));
    resLabel->setFixedWidth(labelWidth);
    resLayout->addWidget(resLabel);
    m_resolutionCombo = new QComboBox();
    m_resolutionCombo->addItem(QStringLiteral("原始分辨率"));
    m_resolutionCombo->addItem("30 米");
    m_resolutionCombo->addItem("90 米");
    m_resolutionCombo->addItem(QStringLiteral("自定义"));
    m_resolutionCombo->setCurrentIndex(m_resMode);
    connect(m_resolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        if (m_resMode != index) {
            if (!confirmParameterChange()) {
                m_resolutionCombo->blockSignals(true);
                m_resolutionCombo->setCurrentIndex(m_resMode);
                m_resolutionCombo->blockSignals(false);
                return;
            }
            m_resMode = index;
            onResolutionModeChanged(index);
            invalidateNodeData();
        }
    });
    resLayout->addWidget(m_resolutionCombo);
    layout->addLayout(resLayout);

    // 3. 自定义分辨率输入
    auto* customLayout = new QHBoxLayout();
    QLabel* customLabel = new QLabel(QStringLiteral("分辨率(米)"));
    customLabel->setFixedWidth(labelWidth);
    customLayout->addWidget(customLabel);
    m_customResEdit = new QLineEdit();
    m_customResEdit->setText(QString::number(m_customResolution));
    connect(m_customResEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        bool ok = false;
        double val = m_customResEdit->text().toDouble(&ok);
        if (ok && val > 0.0 && m_customResolution != val) {
            if (!confirmParameterChange()) {
                m_customResEdit->setText(QString::number(m_customResolution));
                return;
            }
            m_customResolution = val;
            invalidateNodeData();
        } else if (!ok || val <= 0.0) {
            QMessageBox::warning(nullptr, "Warning", QStringLiteral("分辨率必须为正数！"));
            m_customResEdit->setText(QString::number(m_customResolution));
        }
    });
    customLayout->addWidget(m_customResEdit);
    layout->addLayout(customLayout);

    // 4. 缓存目录及浏览
    auto* cacheLayout = new QHBoxLayout();
    QLabel* cacheLabel = new QLabel(QStringLiteral("缓存目录"));
    cacheLabel->setFixedWidth(labelWidth);
    cacheLayout->addWidget(cacheLabel);
    
    m_cacheDirEdit = new QLineEdit();
    m_cacheDirEdit->setObjectName("demPathEdit");
    m_cacheDirEdit->setText(m_cacheDir);
    m_cacheDirEdit->setToolTip(QStringLiteral("默认指向软件全局共享 dem 目录。可修改为工程局部目录以便打包工程。"));
    connect(m_cacheDirEdit, &QLineEdit::editingFinished, this, [this]() {
        QString dir = m_cacheDirEdit->text().trimmed();
        if (m_cacheDir != dir) {
            m_cacheDir = dir;
            updateCacheSizeLabel();
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_cacheDir, true);
            }
        }
    });
    cacheLayout->addWidget(m_cacheDirEdit);

    m_browseCacheBtn = new QPushButton("...");
    m_browseCacheBtn->setFixedWidth(30);
    connect(m_browseCacheBtn, &QPushButton::clicked, this, [this]() {
        QString selectedDir = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("选择缓存目录"), m_cacheDirEdit->text());
        if (!selectedDir.isEmpty()) {
            m_cacheDir = QDir::toNativeSeparators(selectedDir);
            m_cacheDirEdit->setText(m_cacheDir);
            updateCacheSizeLabel();
            auto* iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                NodeUtils::setGlobalDemPath(iface, m_cacheDir, true);
            }
        }
    });
    cacheLayout->addWidget(m_browseCacheBtn);
    layout->addLayout(cacheLayout);

    // 5. 缓存大小与清理
    auto* clearLayout = new QHBoxLayout();
    m_cacheSizeLabel = new QLabel("0.00 MB");
    clearLayout->addWidget(m_cacheSizeLabel);
    
    m_clearCacheBtn = new QPushButton(QStringLiteral("清理缓存"));
    connect(m_clearCacheBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(nullptr, QStringLiteral("确认"), QStringLiteral("确定要清空该目录下的所有 DEM 缓存文件吗？")) == QMessageBox::Yes) {
            QDir dir(m_cacheDir);
            if (dir.exists()) {
                dir.removeRecursively();
                dir.mkpath(m_cacheDir);
                updateCacheSizeLabel();
            }
        }
    });
    clearLayout->addWidget(m_clearCacheBtn);
    layout->addLayout(clearLayout);

    // 6. 目标节点名
    auto* outputLayout = new QHBoxLayout();
    QLabel* outputLabel = new QLabel(QStringLiteral("目标节点"));
    outputLabel->setFixedWidth(labelWidth);
    outputLayout->addWidget(outputLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString name = m_outputNodeNameEdit->text().trimmed();
        if (!name.isEmpty() && m_outputNodeName != name) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            m_outputNodeName = name;
            invalidateNodeData();
        }
    });
    outputLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(outputLayout);

    m_createLabelBtn = new QPushButton(QStringLiteral("声明流程 DEM 标签..."));
    connect(m_createLabelBtn, &QPushButton::clicked, this, [this]() {
        bool accepted = false;
        const QString label = QInputDialog::getText(nullptr, QStringLiteral("声明流程 DEM 标签"),
                                                     QStringLiteral("标签名"), QLineEdit::Normal,
                                                     m_workflowLabel.isEmpty() ? QStringLiteral("dem-main") : m_workflowLabel,
                                                     &accepted).trimmed();
        if (!accepted || label.isEmpty()) return;
        QString error;
        if (NodeUtils::activateWorkflowAuxiliaryDemLabel(projectPath(), m_workflowLabel, label,
                                                          m_workflowProducerIdentity, false, &error)) {
            m_workflowLabel = label;
            return;
        }
        if (error != QStringLiteral("DEM label already exists; explicit conversion/rebind is required.") ||
            QMessageBox::question(nullptr, QStringLiteral("DEM 标签重名"),
                                  QStringLiteral("标签已绑定到其他资源或流程输出。转换会使所有引用该标签的节点重新准备，是否继续？")) != QMessageBox::Yes) {
            QMessageBox::warning(nullptr, QStringLiteral("DEM 标签"), error);
            return;
        }
        if (!NodeUtils::activateWorkflowAuxiliaryDemLabel(projectPath(), m_workflowLabel, label,
                                                          m_workflowProducerIdentity, true, &error)) {
            QMessageBox::warning(nullptr, QStringLiteral("DEM 标签"), error);
        } else {
            m_workflowLabel = label;
        }
    });
    layout->addWidget(m_createLabelBtn);

    m_importLocalDemBtn = new QPushButton(QStringLiteral("导入本地 DEM..."));
    m_importLocalDemBtn->setToolTip(QStringLiteral("导入 TIFF 与其配套 identity H5；下载缓存目录不会参与此操作。"));
    connect(m_importLocalDemBtn, &QPushButton::clicked, this, [this]() {
        const QString tifPath = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择本地 DEM TIFF"),
                                                               QString(), QStringLiteral("GeoTIFF (*.tif *.tiff)"));
        if (tifPath.isEmpty()) return;
        const QString h5Path = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择配套 identity H5"),
                                                              QFileInfo(tifPath).absolutePath(), QStringLiteral("HDF5 (*.h5)"));
        if (h5Path.isEmpty()) return;
        if (!QFileInfo(tifPath).isFile() || !QFileInfo(h5Path).isFile()) {
            QMessageBox::warning(nullptr, QStringLiteral("导入本地 DEM"), QStringLiteral("TIFF 或 identity H5 不存在。"));
            return;
        }
        std::string descriptorJson;
        QString descriptorError;
        int descriptorRead = -1;
        {
            NodeUtils::Hdf5Locker h5Lock(h5Path, 50);
            if (!h5Lock.isLocked()) {
                QMessageBox::warning(nullptr, QStringLiteral("导入本地 DEM"),
                                     QStringLiteral("identity H5 正在被使用，无法读取。"));
                return;
            }
            descriptorRead = Hdf5IO::readString(
                QDir::toNativeSeparators(h5Path).toLocal8Bit().constData(),
                "semantic_product_descriptor", descriptorJson);
        }
        const ProductDescriptor::Ptr descriptor = descriptorRead == 0
            ? ProductDescriptor::fromJson(QJsonDocument::fromJson(QByteArray::fromStdString(descriptorJson)).object(), &descriptorError)
            : nullptr;
        const QMap<QString, QString> provenance = descriptor ? descriptor->provenance() : QMap<QString, QString>();
        if (!descriptor || descriptor->productType() != QStringLiteral("auxiliary_terrain_dem") ||
            provenance.value(QStringLiteral("producer")).isEmpty() ||
            provenance.value(QStringLiteral("output_port")).isEmpty()) {
            QMessageBox::warning(nullptr, QStringLiteral("导入本地 DEM"),
                                 descriptorError.isEmpty() ? QStringLiteral("identity H5 缺少有效的 auxiliary_terrain_dem 语义身份。") : descriptorError);
            return;
        }
        const QString resourceId = demResourceId(tifPath, h5Path);
        const QString provenanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString error;
        if (resourceId.isEmpty() || !ensureManagedDemResource(projectPath(), NodeUtils::getProjectFilePath(_widget),
                                                               tifPath, h5Path, resourceId, provenanceId, &error)) {
            QMessageBox::warning(nullptr, QStringLiteral("导入本地 DEM"), error.isEmpty() ? QStringLiteral("本地 DEM 身份或受管资源事务验证失败。") : error);
            return;
        }
        const QDir resourceDir(QDir(projectPath()).absoluteFilePath(QStringLiteral(".dem_resources/%1").arg(resourceId)));
        m_outputData = std::make_shared<AuxiliaryDemData>(resourceDir.absoluteFilePath(QStringLiteral("dem.tif")),
                                                           resourceDir.absoluteFilePath(QStringLiteral("identity.h5")),
                                                           resourceId, provenanceId, QFileInfo(tifPath).completeBaseName());
        m_outputData->setProductDescriptor(auxiliaryDemEntityDescriptor(resourceId, provenanceId));
        m_isLocalImport = true;
        m_localImportResourceId = resourceId;
        m_localImportProvenanceId = provenanceId;
        if (!m_workflowLabel.isEmpty() &&
            !NodeUtils::resolveWorkflowAuxiliaryDemLabel(projectPath(), m_workflowLabel,
                                                          m_workflowProducerIdentity,
                                                          resourceId, provenanceId, &error)) {
            QMessageBox::warning(nullptr, QStringLiteral("导入本地 DEM"), error);
            return;
        }
        m_imageInfoData.reset();
        setState(ExecutionState::Completed);
        setOutputData(0, m_outputData);
        setOutputData(1, nullptr);
        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
    });
    layout->addWidget(m_importLocalDemBtn);

    // 初始化控件状态
    onResolutionModeChanged(m_resMode);
    updateCacheSizeLabel();
    updateLoginStatus();
}

void DEMSourceNode::onResolutionModeChanged(int index)
{
    if (m_customResEdit) {
        m_customResEdit->setEnabled(index == 3);
    }
}

void DEMSourceNode::updateCacheSizeLabel()
{
    if (!m_cacheSizeLabel) return;
    
    double sizeMB = 0;
    QDir dir(m_cacheDir);
    if (dir.exists()) {
        QDirIterator it(m_cacheDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            sizeMB += it.fileInfo().size();
        }
    }
    sizeMB /= (1024.0 * 1024.0);
    m_cacheSizeLabel->setText(QString("%1 MB").arg(sizeMB, 0, 'f', 2));
}

QString DEMSourceNode::generateDefaultOutputName() const
{
    if (m_inputData && !m_inputData->nodeName().isEmpty()) {
        return m_inputData->nodeName() + "_ExternalDEM";
    }
    return "ExternalDEM";
}

bool DEMSourceNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;
    
    QString cache = m_cacheDirEdit ? m_cacheDirEdit->text().trimmed() : m_cacheDir.trimmed();
    if (cache.isEmpty())
        return false;

    QString dst = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName.trimmed();
    if (dst.isEmpty())
        return false;

    return true;
}

bool DEMSourceNode::prepareToStart()
{
    m_isLocalImport = false;
    m_localImportResourceId.clear();
    m_localImportProvenanceId.clear();
    if (!validateInputs()) {
        return false;
    }

    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    
    m_preparedDstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName.trimmed();
    m_preparedSource = m_demSourceCombo ? m_demSourceCombo->currentData().toInt() : m_demSource;
    m_preparedCacheDir = m_cacheDirEdit ? m_cacheDirEdit->text().trimmed() : m_cacheDir;
    m_preparedInputPaths = m_inputData->filePaths();
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_preparedInputPaths,
                                         m_inputData->physicalProductDescriptor(), &identityError)) {
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }

    // 检查 NASA Earthdata 登录状态（如果选择的源非 Copernicus 且未登录）
    if (m_preparedSource != 2)
    {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
        QString encryptedPass = settings.value("DEM/EarthdataPassword", "").toString();
        if (encryptedUser.isEmpty() || encryptedPass.isEmpty())
        {
            if (_isAutoTriggered)
            {
                InSARLogManager::LogError("DEMSourceNode", "NASA Earthdata login required but credentials are missing. Skip execution.");
                return false;
            }
            else
            {
                QMessageBox::warning(nullptr, "Warning", QStringLiteral("所选 DEM 数据源需要 NASA Earthdata 账户登录，请先登录！"));
                EarthdataLoginDialog dlg(nullptr);
                if (dlg.exec() != QDialog::Accepted)
                {
                    return false;
                }
                updateLoginStatus();
            }
        }
    }

    int resIdx = m_resolutionCombo ? m_resolutionCombo->currentIndex() : m_resMode;
    if (resIdx == 0) m_preparedResolution = 0.0;
    else if (resIdx == 1) m_preparedResolution = 30.0;
    else if (resIdx == 2) m_preparedResolution = 90.0;
    else m_preparedResolution = m_customResEdit ? m_customResEdit->text().toDouble() : m_customResolution;

    const QDir outputDirectory(m_preparedSavePath + "/" + m_preparedDstNode);
    m_preparedOutputPaths = QStringList()
        << outputDirectory.absoluteFilePath(m_preparedDstNode + "_dem.h5")
        << outputDirectory.absoluteFilePath(m_preparedDstNode + "_dem.tif");
    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);
    }
    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void DEMSourceNode::execute()
{
    executeProcessing();
}

void DEMSourceNode::stopExecution()
{
    InSARLogManager::LogInfo("DEMSourceNode", "DEM cancellation requested.");
    ++m_remedyGeneration;
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
    }
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
    if (m_thread) {
        m_thread->requestInterruption();
    }
}

bool DEMSourceNode::stopExecutionIsAsynchronous() const
{
    return m_workerThread != nullptr || (m_thread != nullptr && m_thread->isRunning()) ||
        m_finalizationWatcher.isRunning();
}

void DEMSourceNode::processAutomatically()
{
    if (m_workerThread || m_thread || m_finalizationWatcher.isRunning()) {
        deferAutomaticCompletion();
        return;
    }
    if (prepareToStart()) {
        executeProcessing();
        deferAutomaticCompletion();
    } else {
        setState(ExecutionState::Pending);
    }
}

void DEMSourceNode::executeProcessing()
{
    if (m_workerThread || m_thread || m_finalizationWatcher.isRunning()) {
        return;
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        invalidateExecution();
        return;
    }

    ++m_remedyGeneration;
    QString labelError;
    if (!m_workflowLabel.isEmpty() &&
        !NodeUtils::invalidateWorkflowAuxiliaryDemLabel(projectPath(), m_workflowLabel,
                                                         m_workflowProducerIdentity, &labelError)) {
        onError(labelError.isEmpty() ? QStringLiteral("无法使流程 DEM 标签进入待生成状态。") : labelError);
        return;
    }
    clearPublishedOutputs();
    const quint64 executionGeneration = ++m_executionGeneration;
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode,
                                           m_preparedOutputPaths, m_preparedInputPaths,
                                           m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> descriptorProvenance;
    descriptorProvenance.insert(QStringLiteral("producer"), name());
    descriptorProvenance.insert(QStringLiteral("output_port"),
                                QStringLiteral("dem_source.output.auxiliary_terrain_dem.entity"));
    if (!NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction, ProductDescriptor::create(
                QStringLiteral("auxiliary_terrain_dem"), QStringLiteral("sat-explorer-product"), 1,
                ProductState::Committed, name(), descriptorProvenance), &transactionError)) {
        onError(transactionError);
        return;
    }

    m_workerThread = new DEMSourceWorker();
    m_thread = new QThread();
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    connect(m_workerThread, &DEMSourceWorker::updateProcess, this,
            [this, executionGeneration](int progress, const QString& message) {
        if (executionGeneration == m_executionGeneration) onProgressUpdate(progress, message);
    });
    connect(m_workerThread, &DEMSourceWorker::errorProcess, this,
            [this, executionGeneration](const QString& error) {
        if (executionGeneration == m_executionGeneration) onError(error);
    });
    connect(m_workerThread, &DEMSourceWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &DEMSourceWorker::cancelled, this,
            [this, executionGeneration]() {
        if (executionGeneration == m_executionGeneration) onCancelled();
    });
    connect(m_workerThread, &DEMSourceWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &DEMSourceWorker::demFetchFinished, this,
            [this, executionGeneration](const QString& outputH5Path, const QString& stagingNode,
                                        const QString& projectName, int demSource, double targetResolution,
                                        const QStringList& availableTiles, const QStringList& serverNotFoundTiles,
                                        int requestedTileCount, bool outputValidated) {
        if (executionGeneration == m_executionGeneration) {
            onProcessingFinished(outputH5Path, stagingNode, projectName, demSource, targetResolution,
                                 availableTiles, serverNotFoundTiles, requestedTileCount, outputValidated);
        }
    });
    connect(m_workerThread, &DEMSourceWorker::demFetchFinished, m_thread, &QThread::quit);

    DEMSourceWorker* const worker = m_workerThread;
    const QString projectRoot = m_preparedSavePath;
    const QString projectName = m_preparedProjectName;
    const QString stagingNode = m_outputTransaction.stagingName;
    const QString outputNodeName = m_preparedDstNode;
    const QStringList inputPaths = m_preparedInputPaths;
    const int source = m_preparedSource;
    const double resolution = m_preparedResolution;
    const QString cacheDirectory = m_preparedCacheDir;
    connect(m_thread, &QThread::started, m_workerThread,
            [worker, projectRoot, projectName, stagingNode, outputNodeName, inputPaths,
             source, resolution, cacheDirectory]() {
        worker->fetch_dem(projectRoot, projectName, stagingNode, outputNodeName, inputPaths,
                          source, resolution, cacheDirectory);
    });
    m_thread->start();
    setState(ExecutionState::Running);
    deferAutomaticCompletion();
}

void DEMSourceNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (executionStopRequested() || isAutomaticExecutionObsolete()) return;
    setProgress(progress);
}

void DEMSourceNode::onError(const QString& error)
{
    if (executionStopRequested()) {
        onCancelled();
        return;
    }

    ++m_remedyGeneration;
    clearPublishedOutputs();
    m_workerThread = nullptr;
    m_thread = nullptr;

    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    setLastErrorMessage(error);
    setState(ExecutionState::Error);
    InSARLogManager::LogError("DEMSourceNode", "Execution failed: " + error);
    Q_EMIT executionError(error);
}

void DEMSourceNode::onProcessingFinished(
    const QString& outputH5Path,
    const QString& stagingNode,
    const QString& projectName,
    int demSource,
    double targetResolution,
    const QStringList& availableTiles,
    const QStringList& serverNotFoundTiles,
    int requestedTileCount,
    bool outputValidated
)
{
    m_workerThread = nullptr;
    m_thread = nullptr;

    if (executionStopRequested()) {
        InSARLogManager::LogInfo("DEMSourceNode", "Discarded DEM completion after cancellation request.");
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
        clearPublishedOutputs();
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }

    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete automatic execution"), projectXml());
        clearPublishedOutputs();
        return;
    }

    if (stagingNode != m_outputTransaction.stagingName || !outputValidated) {
        onError(QStringLiteral("DEM worker returned an incomplete or inconsistent staging output."));
        return;
    }

    if (!m_finalizationPreparation) {
        if (m_finalizationWatcher.isRunning()) {
            return;
        }

        const QString resourceStagingPath = QDir(m_outputTransaction.projectRoot).absoluteFilePath(
            QStringLiteral(".dem_resources/.dem-install-%1").arg(m_outputTransaction.transactionId));
        m_outputTransaction.resourceStagingPath = resourceStagingPath;
        QString preparationError;
        if (!NodeUtils::persistOutputTransactionState(m_outputTransaction, &preparationError)) {
            onError(preparationError);
            return;
        }

        const quint64 executionGeneration = m_executionGeneration;
        const quint64 revision = executionRevision();
        const NodeUtils::OutputTransaction transaction = m_outputTransaction;
        const auto preparation = std::make_shared<DemFinalizationPreparation>();
        m_finalizationWatcher.disconnect(this);
        connect(&m_finalizationWatcher, &QFutureWatcher<void>::finished, this,
                [this, preparation, executionGeneration, revision, outputH5Path, stagingNode,
                 projectName, demSource, targetResolution, availableTiles, serverNotFoundTiles,
                 requestedTileCount, outputValidated]() {
            if (executionGeneration != m_executionGeneration || revision != executionRevision()) {
                QDir(preparation->transaction.resourceStagingPath).removeRecursively();
                return;
            }
            m_finalizationPreparation = preparation;
            onProcessingFinished(outputH5Path, stagingNode, projectName, demSource, targetResolution,
                                 availableTiles, serverNotFoundTiles, requestedTileCount, outputValidated);
        });
        setProgress(96);
        m_finalizationWatcher.setFuture(QtConcurrent::run([preparation, transaction, outputH5Path]() {
            *preparation = prepareDemFinalization(transaction, outputH5Path);
        }));
        return;
    }

    const std::shared_ptr<DemFinalizationPreparation> preparation = m_finalizationPreparation;
    m_finalizationPreparation.reset();
    if (!preparation->success) {
        onError(preparation->errorMessage.isEmpty()
            ? QStringLiteral("DEM staging output did not pass transaction validation.")
            : preparation->errorMessage);
        return;
    }
    m_outputTransaction = preparation->transaction;

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete execution revision"), projectXml());
        clearPublishedOutputs();
        const QString error = QStringLiteral("DEM execution became obsolete before its output transaction could be committed.");
        setLastErrorMessage(error);
        if (executionMode() == ExecutionMode::Automatic) {
            setState(ExecutionState::Pending);
        } else {
            setState(ExecutionState::Error);
            Q_EMIT executionError(error);
        }
        return;
    }

    TaskLogContext logContext;
    logContext.displayName = caption();
    const double tileAvailability = requestedTileCount > 0
        ? 100.0 * availableTiles.size() / requestedTileCount : 0.0;
    QString transactionError;
    XMLFile* xml = projectXml();
    const QString xmlPath = NodeUtils::getProjectFilePath(_widget);
    if (!xml || xmlPath.isEmpty()) {
        onError(transactionError.isEmpty()
            ? QStringLiteral("DEM staging output did not pass transaction validation.") : transactionError);
        return;
    }

    // Install the auxiliary resource while the output is still staged.  The
    // transaction journal records every resource mutation so a later promote
    // or metadata failure can restore the previous committed state.
    const QDir stagedOutput(QDir(m_outputTransaction.projectRoot).absoluteFilePath(m_outputTransaction.stagingName));
    QString h5Path = stagedOutput.absoluteFilePath(QFileInfo(m_preparedOutputPaths.value(0)).fileName());
    QString tifPath = stagedOutput.absoluteFilePath(QFileInfo(m_preparedOutputPaths.value(1)).fileName());
    if (h5Path.isEmpty() || tifPath.isEmpty()) {
        onError(QStringLiteral("Promoted DEM transaction is missing its H5 or TIFF output."));
        return;
    }

    // validateStagedOutputTransaction has just hashed these artifacts while
    // creating the staged manifest. Reuse those values instead of reading the
    // same large H5 a second time on the completion path.
    const QByteArray rasterHash = preparation->rasterHash;
    const QByteArray identityHash = preparation->identityHash;
    if (rasterHash.isEmpty() || identityHash.isEmpty()) {
        onError(QStringLiteral("无法计算 DEM 受管文件 hash。"));
        return;
    }

    const QJsonObject resourceMetadata = preparation->resourceMetadata;
    const QByteArray canonicalMetadataHash = preparation->canonicalMetadataHash;
    const QString resourceId = preparation->resourceId;
    const QString provenanceId = preparation->provenanceId;
    const QDir resourcesRoot(QDir(projectPath()).absoluteFilePath(QStringLiteral(".dem_resources")));
    if (!QDir().mkpath(resourcesRoot.absolutePath())) {
        onError(QStringLiteral("无法创建 DEM 资源根目录。"));
        return;
    }
    const QDir resourceDir(preparation->resourceDirectory);
    const QString managedTif = resourceDir.absoluteFilePath(QStringLiteral("dem.tif"));
    const QString managedH5 = resourceDir.absoluteFilePath(QStringLiteral("identity.h5"));
    const bool hasPartialResource = QFile::exists(resourceDir.absolutePath()) &&
        !(QFile::exists(managedTif) && QFile::exists(managedH5));
    if (hasPartialResource) {
        onError(QStringLiteral("已存在不完整 DEM 资源目录，拒绝覆盖或复用。"));
        return;
    }
    const bool resourceAlreadyCommitted = preparation->resourceAlreadyCommitted;
    m_outputTransaction.resourceAction = resourceAlreadyCommitted
        ? QStringLiteral("reused") : QStringLiteral("installed");
    m_outputTransaction.installedPath = resourceDir.absolutePath();
    m_outputTransaction.provenanceOnlyUpdate = resourceAlreadyCommitted;
    const QDir installDir(m_outputTransaction.resourceStagingPath);
    m_outputTransaction.resourceStagingPath = resourceAlreadyCommitted ? QString() : installDir.absolutePath();
    if (!NodeUtils::persistOutputTransactionState(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    QJsonObject provenance;
    provenance.insert(QStringLiteral("role"), QStringLiteral("auxiliary_terrain_dem"));
    provenance.insert(QStringLiteral("resourceId"), resourceId);
    provenance.insert(QStringLiteral("pinnedProvenanceId"), provenanceId);
    provenance.insert(QStringLiteral("runId"), provenanceId);
    provenance.insert(QStringLiteral("canonicalMetadataHash"), QString::fromLatin1(canonicalMetadataHash));
    QJsonObject rasterManifest;
    rasterManifest.insert(QStringLiteral("path"), QStringLiteral("dem.tif"));
    rasterManifest.insert(QStringLiteral("sha256"), QString::fromLatin1(rasterHash));
    provenance.insert(QStringLiteral("dem.tif"), rasterManifest);
    QJsonObject identityManifest;
    identityManifest.insert(QStringLiteral("path"), QStringLiteral("identity.h5"));
    identityManifest.insert(QStringLiteral("sha256"), QString::fromLatin1(identityHash));
    provenance.insert(QStringLiteral("identity.h5"), identityManifest);
    const QString manifestRoot = resourceAlreadyCommitted ? resourceDir.absolutePath() : installDir.absolutePath();
    m_outputTransaction.provenanceManifestPath = QDir(manifestRoot).absoluteFilePath(
        QStringLiteral("provenance_%1.json").arg(provenanceId));
    QSaveFile provenanceFile(m_outputTransaction.provenanceManifestPath);
    if (!provenanceFile.open(QIODevice::WriteOnly) ||
        provenanceFile.write(QJsonDocument(provenance).toJson(QJsonDocument::Compact)) < 0 ||
        !provenanceFile.commit()) {
        onError(QStringLiteral("无法提交 DEM provenance manifest。"));
        return;
    }
    if (!resourceAlreadyCommitted &&
        !QDir().rename(installDir.absolutePath(), resourceDir.absolutePath())) {
        QDir(installDir.absolutePath()).removeRecursively();
        onError(QStringLiteral("无法原子安装 DEM 资源目录。"));
        return;
    }
    if (!resourceAlreadyCommitted) {
        m_outputTransaction.provenanceManifestPath = resourceDir.absoluteFilePath(
            QStringLiteral("provenance_%1.json").arg(provenanceId));
    }
    NodeUtils::AuxiliaryDemRegistryEntry registryEntry;
    registryEntry.resourceId = resourceId;
    registryEntry.role = QStringLiteral("auxiliary_terrain_dem");
    registryEntry.rasterHash = QString::fromLatin1(rasterHash);
    registryEntry.identityH5Hash = QString::fromLatin1(identityHash);
    registryEntry.canonicalMetadataHash = QString::fromLatin1(canonicalMetadataHash);
    registryEntry.managedRasterPath = QStringLiteral(".dem_resources/%1/dem.tif").arg(resourceId);
    registryEntry.managedIdentityH5Path = QStringLiteral(".dem_resources/%1/identity.h5").arg(resourceId);
    registryEntry.metadata = resourceMetadata;
    registryEntry.provenanceHistory.append(provenance);
    m_outputTransaction.provenanceDelta = provenance;
    m_outputTransaction.resourceRegistryMutationPrepared = true;
    const QString registryPath = QDir(projectPath()).absoluteFilePath(QStringLiteral(".dem_resource_registry.json"));
    m_outputTransaction.resourceRegistryBackupPath = registryPath + QStringLiteral(".backup-") + m_outputTransaction.transactionId;
    if (QFileInfo::exists(registryPath)) {
        QFile oldRegistry(registryPath);
        if (!oldRegistry.open(QIODevice::ReadOnly) ||
            !QFile::copy(registryPath, m_outputTransaction.resourceRegistryBackupPath)) {
            onError(QStringLiteral("无法准备 DEM resource registry 备份。"));
            return;
        }
        m_outputTransaction.resourceRegistryBackupHash =
            QString::fromLatin1(QCryptographicHash::hash(oldRegistry.readAll(), QCryptographicHash::Sha256).toHex());
        m_outputTransaction.baseRegistryHash = m_outputTransaction.resourceRegistryBackupHash;
    }
    if (!NodeUtils::persistOutputTransactionState(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    if (!NodeUtils::mergeAuxiliaryDemRegistryEntry(projectPath(), registryEntry, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.resourceRegistryCommitted = true;
    m_outputTransaction.resourceRegistryCommittedHash =
        QString::fromLatin1(sha256File(registryPath));
    m_outputTransaction.newRegistryGeneration = fileGeneration(registryPath);
    if (m_outputTransaction.resourceRegistryCommittedHash.isEmpty() ||
        m_outputTransaction.newRegistryGeneration.isEmpty()) {
        onError(QStringLiteral("无法捕获已提交 DEM resource registry 的完整性基线。"));
        return;
    }
    if (!NodeUtils::persistOutputTransactionState(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }

    QStringList finalPaths;
    if (!NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, xml, xmlPath, &transactionError)) {
        onError(transactionError);
        return;
    }
    for (const QString& finalPath : finalPaths) {
        if (finalPath.endsWith(QStringLiteral(".h5"), Qt::CaseInsensitive)) h5Path = finalPath;
        if (finalPath.endsWith(QStringLiteral(".tif"), Qt::CaseInsensitive)) tifPath = finalPath;
    }

    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    NodeUtils::removeDataNodeFromProject(iface, m_preparedDstNode, false, false);
    std::string srcName = "SRTM1";
    if (demSource == 1) srcName = "SRTM3";
    else if (demSource == 2) srcName = "Copernicus";
    else if (demSource == 3) srcName = "ASTER";
    xml->XMLFile_add_dem(m_preparedDstNode.toStdString().c_str(),
                          (m_preparedDstNode + "_dem").toStdString().c_str(),
                          ("/" + m_preparedDstNode + "/" + QFileInfo(h5Path).fileName()).toStdString().c_str(),
                          0, 0, srcName.c_str(), targetResolution);
    if (!NodeUtils::saveProjectXmlAtomically(xml, xmlPath, &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("Unable to commit DEM project metadata.") : transactionError);
        return;
    }
    if (!m_workflowLabel.isEmpty() &&
        !NodeUtils::resolveWorkflowAuxiliaryDemLabel(projectPath(), m_workflowLabel,
                                                      m_workflowProducerIdentity,
                                                      resourceId, provenanceId, &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("无法解析流程 DEM 标签。") : transactionError);
        return;
    }
    if (!m_outputTransaction.resourceRegistryBackupPath.isEmpty()) {
        QFile::remove(m_outputTransaction.resourceRegistryBackupPath);
    }

    const QString summary = QStringLiteral("DEM 获取完成：可用瓦片 %1/%2，请求瓦片可用率 %3%，服务器 404 瓦片：%4，输出：%5，输出数据集校验：通过。")
        .arg(availableTiles.size()).arg(requestedTileCount).arg(tileAvailability, 0, 'f', 1)
        .arg(serverNotFoundTiles.isEmpty() ? QStringLiteral("无") : serverNotFoundTiles.join(QStringLiteral(", ")))
        .arg(h5Path);
    InSARLogManager::LogTaskEvent(logContext, InSARLogManager::LevelInfo, "DEMSourceNode", summary,
                                  LogTargets(LogTarget::UserProjectLog),
                                  QStringLiteral("artifact_validated"), QStringLiteral("completed"));

    const QString dstNode = m_preparedDstNode;
    const QString jpgPath = h5Path.left(h5Path.lastIndexOf('.')) + ".jpg";
    m_outputData = std::make_shared<AuxiliaryDemData>(managedTif, managedH5, resourceId, provenanceId, dstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
    m_referenceData = std::make_shared<AuxiliaryDemReferenceData>(resourceId, provenanceId, 1);
    m_referenceData->setProductDescriptor(auxiliaryDemReferenceDescriptor(resourceId, provenanceId));
    m_imageInfoData.reset();
    setOutputData(0, m_outputData);
    setOutputData(1, nullptr);
    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);

    // 主线程挂载项目树 UI
    QStandardItemModel* model = projectModel();
    if (model)
    {
        if (iface) NodeUtils::removeDataNodeFromProjectTree(iface, dstNode);
        QStandardItem* project = nullptr;
        QList<QStandardItem*> foundProjects = model->findItems(projectName);
        if (!foundProjects.isEmpty())
        {
            project = foundProjects.first();
        }

        if (project)
        {
            QStandardItem* demNode = nullptr;
            for (int i = 0; i < project->rowCount(); ++i)
            {
                if (project->child(i, 0)->text() == dstNode)
                {
                    demNode = project->child(i, 0);
                    break;
                }
            }

            if (!demNode)
            {
                demNode = new QStandardItem(dstNode);
                demNode->setToolTip(projectName);
                demNode->setIcon(QIcon(FOLDER_ICON));
                int insertIndex = 0;
                for (; insertIndex < project->rowCount(); ++insertIndex)
                {
                    QString t = project->child(insertIndex, 1)->text();
                    if (t == "complex-0.0" || t == "complex-1.0" || t == "complex-2.0" || 
                        t == "phase-1.0" || t == "phase-2.0" || t == "phase-3.0" || t == "dem-1.0")
                    {
                        continue;
                    }
                    break;
                }
                project->insertRow(insertIndex, demNode);
                project->setChild(insertIndex, 1, new QStandardItem("dem-1.0"));
            }

            QStandardItem* itemImg = nullptr;
            QString imgName = dstNode + "_dem";
            for (int j = 0; j < demNode->rowCount(); ++j)
            {
                if (demNode->child(j, 0)->text() == imgName)
                {
                    itemImg = demNode->child(j, 0);
                    break;
                }
            }

            if (!itemImg)
            {
                QStandardItem* image = new QStandardItem(imgName);
                image->setToolTip("dem");
                image->setIcon(QIcon(IMAGEDATA_ICON));
                demNode->appendRow(image);
                demNode->setChild(demNode->rowCount() - 1, 1, new QStandardItem(h5Path));
            }
            else
            {
                demNode->setChild(itemImg->row(), 1, new QStandardItem(h5Path));
            }
        }
    }
    startPreviewGeneration(h5Path, jpgPath);
}

void DEMSourceNode::startPreviewGeneration(const QString& h5Path, const QString& jpgPath)
{
    const quint64 generation = ++m_remedyGeneration;
    const quint64 executionGeneration = m_executionGeneration;
    const quint64 revision = executionRevision();
    m_remedyH5Path = QFileInfo(h5Path).absoluteFilePath();
    m_remedyJpgPath = QFileInfo(jpgPath).absoluteFilePath();
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.disconnect(this);
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Path, jpgPath, generation]() {
            if (m_remedyGeneration != generation) return;
            m_remedyWatcher.disconnect(this);
            startPreviewGeneration(h5Path, jpgPath);
        });
        return;
    }
    m_remedyWatcher.disconnect(this);
    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, h5Path, jpgPath, generation, executionGeneration, revision]() {
        if (m_remedyGeneration != generation ||
            m_executionGeneration != executionGeneration ||
            executionRevision() != revision ||
            m_remedyH5Path != QFileInfo(h5Path).absoluteFilePath() ||
            m_remedyJpgPath != QFileInfo(jpgPath).absoluteFilePath()) {
            return;
        }
        bool jpgExists = NodeUtils::isJpgPreviewCurrent(h5Path, jpgPath);
        if (jpgExists) {
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPath);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
        } else {
            m_imageInfoData.reset();
            setOutputData(1, nullptr);
            Q_EMIT dataUpdated(1);
            InSARLogManager::LogWarning("DEMSourceNode", "DEM preview JPG failed to generate: " + jpgPath);
        }

        if (!jpgExists) {
            setLastWarningMessage(QStringLiteral("DEM data was generated, but its preview image could not be generated."));
            setState(ExecutionState::Running);
            finishExecutionWithWarning();
        } else {
            setState(ExecutionState::Running);
            finishExecution();
        }
        // outData() intentionally hides products while Running.  Re-publish
        // the DEM ports after the terminal transition so downstream nodes do
        // not retain the empty propagation emitted before preview generation.
        if (m_outputData) {
            setOutputData(0, m_outputData);
            Q_EMIT dataUpdated(0);
        }
        if (m_imageInfoData) {
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
        }
        updateCacheSizeLabel();

        if (auto* iface = NodeUtils::getProjectContext(_widget)) {
            iface->refreshProjectTree();
        }
    });
    m_remedyWatcher.setFuture(QtConcurrent::run([h5Path, jpgPath]() {
        if (NodeUtils::isJpgPreviewCurrent(h5Path, jpgPath)) {
            return;
        }
        NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "dem");
    }));
}

void DEMSourceNode::onCancelled()
{
    ++m_remedyGeneration;
    clearPublishedOutputs();
    InSARLogManager::LogInfo("DEMSourceNode", "DEM fetch cancellation cleanup completed.");
    m_workerThread = nullptr;
    m_thread = nullptr;

    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());

    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

bool DEMSourceNode::validateAndRestoreOutput()
{
    QString savePath = projectPath();
    QString name = m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName;
    const auto clearRestoredOutputs = [this]() {
        clearPublishedOutputs();
    };
    QStringList committedPaths;
    if (!NodeUtils::loadCommittedOutputManifest(savePath, name, committedPaths)) {
        clearRestoredOutputs();
        setState(ExecutionState::Pending);
        return false;
    }
    QString targetH5;
    QString targetTif;
    for (const QString& path : committedPaths) {
        if (path.endsWith(QStringLiteral(".h5"), Qt::CaseInsensitive)) targetH5 = path;
        if (path.endsWith(QStringLiteral(".tif"), Qt::CaseInsensitive)) targetTif = path;
    }
    if (targetH5.isEmpty() || targetTif.isEmpty()) {
        clearRestoredOutputs();
        setState(ExecutionState::Pending);
        return false;
    }
    ProductDescriptor::Ptr descriptor;
    QString identityError;
    bool legacyDescriptor = false;
    if (!NodeUtils::loadCommittedOutputProductDescriptor(savePath, name, descriptor, &identityError) ||
        (!(validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
           (legacyDescriptor = isLegacyAuxiliaryDemEntityDescriptor(descriptor)))) ||
        !NodeUtils::validateH5Identity(targetH5, descriptor, nullptr, &identityError)) {
        clearRestoredOutputs();
        setLastErrorMessage(identityError.isEmpty() ? QStringLiteral("DEM committed output identity is invalid.") : identityError);
        setState(ExecutionState::Error);
        return false;
    }
    QString targetJpg = QFileInfo(targetH5).absolutePath() + "/" + QFileInfo(targetH5).baseName() + ".jpg";



    if (QFile::exists(targetH5)) {
        bool needsTif = !QFile::exists(targetTif);
        bool needsJpg = !NodeUtils::isJpgPreviewCurrent(targetH5, targetJpg);

        if (needsTif || needsJpg) {
            const quint64 remedyGeneration = ++m_remedyGeneration;
            const quint64 executionGeneration = m_executionGeneration;
            const quint64 revision = executionRevision();
            m_remedyH5Path = QFileInfo(targetH5).absoluteFilePath();
            m_remedyJpgPath = QFileInfo(targetJpg).absoluteFilePath();
            m_remedyWatcher.disconnect(this);
            if (m_remedyWatcher.isRunning()) {
                m_remedyWatcher.cancel();
                connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                        [this, remedyGeneration]() {
                    if (m_remedyGeneration != remedyGeneration) return;
                    m_remedyWatcher.disconnect(this);
                    QTimer::singleShot(0, this, [this]() { validateAndRestoreOutput(); });
                });
                return true;
            }

            auto writeTifSuccess = std::make_shared<bool>(true);
            connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                    [this, targetH5, targetTif, targetJpg, name, descriptor,
                     legacyDescriptor, writeTifSuccess, remedyGeneration, executionGeneration, revision]() {
                if (m_remedyGeneration != remedyGeneration ||
                    m_executionGeneration != executionGeneration ||
                    executionRevision() != revision ||
                    m_remedyH5Path != QFileInfo(targetH5).absoluteFilePath() ||
                    m_remedyJpgPath != QFileInfo(targetJpg).absoluteFilePath()) {
                    return;
                }
                bool tifExists = QFile::exists(targetTif) && *writeTifSuccess;
                bool jpgExists = NodeUtils::isJpgPreviewCurrent(targetH5, targetJpg);
                QString restoreError;

                if (tifExists) {
                    QString resourceId;
                    QString provenanceId;
                    OutputCommitLease restoreLease = acquireOutputCommitLease(revision);
                    if (!restoreLease || !restoreManagedDemResource(
                            projectPath(), NodeUtils::getProjectFilePath(_widget), targetTif, targetH5,
                            m_savedResourceId, m_savedResourceProvenanceId,
                            resourceId, provenanceId, &restoreError)) {
                        clearPublishedOutputs();
                        setLastErrorMessage(restoreError.isEmpty()
                            ? QStringLiteral("DEM 资源恢复在提交租约或事务校验时失败。") : restoreError);
                        setState(ExecutionState::Error);
                        return;
                    }
                    const QString managedRoot = QDir(projectPath()).absoluteFilePath(QStringLiteral(".dem_resources/%1").arg(resourceId));
                    m_outputData = std::make_shared<AuxiliaryDemData>(
                        QDir(managedRoot).absoluteFilePath(QStringLiteral("dem.tif")),
                        QDir(managedRoot).absoluteFilePath(QStringLiteral("identity.h5")), resourceId, provenanceId, name);
                    m_outputData->setProductDescriptor(legacyDescriptor
                        ? auxiliaryDemEntityDescriptor(resourceId, provenanceId) : descriptor);
                    m_referenceData = std::make_shared<AuxiliaryDemReferenceData>(resourceId, provenanceId, 1);
                    m_referenceData->setProductDescriptor(auxiliaryDemReferenceDescriptor(resourceId, provenanceId));
                    setOutputData(0, m_outputData);
                    Q_EMIT dataUpdated(0);
                } else {
                    m_outputData.reset();
                    m_referenceData.reset();
                    setOutputData(0, nullptr);
                    Q_EMIT dataUpdated(0);
                }

                if (jpgExists) {
                    m_imageInfoData = std::make_shared<ImageInfoData>(targetJpg);
                    setOutputData(1, m_imageInfoData);
                    Q_EMIT dataUpdated(1);
                } else {
                    m_imageInfoData.reset();
                    setOutputData(1, nullptr);
                    Q_EMIT dataUpdated(1);
                }

                if (executionState() == ExecutionState::Running ||
                    executionState() == ExecutionState::Pending ||
                    executionState() == ExecutionState::Completed) {
                    if (tifExists) {
                        if (!jpgExists) {
                            setLastWarningMessage(QStringLiteral("DEM data was restored, but its preview image could not be generated."));
                            setState(ExecutionState::Warning);
                            InSARLogManager::LogWarning("DEMSourceNode", "DEM recovery finished with warning: JPG preview generation failed.");
                        } else {
                            setState(ExecutionState::Completed);
                        }
                        // outData() remains hidden until the terminal state.
                        // Re-publish after that transition for graph consumers.
                        setOutputData(0, m_outputData);
                        Q_EMIT dataUpdated(0);
                        setOutputData(1, m_imageInfoData);
                        Q_EMIT dataUpdated(1);
                        setProgress(100);
                        Q_EMIT computingFinished();
                    } else {
                        clearPublishedOutputs();
                        setLastErrorMessage(QStringLiteral("DEM TIFF recovery failed."));
                        setState(ExecutionState::Error);
                        InSARLogManager::LogError("DEMSourceNode", "TIFF generation failed during background recovery. Target path: " + targetTif);
                    }
                }
            });

            QFuture<void> future = QtConcurrent::run([targetH5, targetTif, targetJpg, needsTif, needsJpg, writeTifSuccess]() {
                if (needsTif) {
                    cv::Mat dem;
                    double min_lon = 0, max_lon = 0, min_lat = 0, max_lat = 0;
                    bool read_success = false;
                    {
                        NodeUtils::Hdf5Locker locker;
                        read_success = (NodeUtils::readMatFromH5(targetH5, "dem", dem) &&
                                        NodeUtils::readScalarFromH5(targetH5, "dem_min_lon", min_lon) &&
                                        NodeUtils::readScalarFromH5(targetH5, "dem_max_lon", max_lon) &&
                                        NodeUtils::readScalarFromH5(targetH5, "dem_min_lat", min_lat) &&
                                        NodeUtils::readScalarFromH5(targetH5, "dem_max_lat", max_lat));
                    }
                    if (read_success) {
                        double res_lon = (max_lon - min_lon) / dem.cols;
                        double res_lat = (max_lat - min_lat) / dem.rows;
                        double new_gt[6] = { min_lon, res_lon, 0.0, max_lat, 0.0, -res_lat };
                        const char* wkt_projection = "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]";
                        if (!NodeUtils::writeDemToTif(targetTif, dem, new_gt, wkt_projection)) {
                            *writeTifSuccess = false;
                        }
                    } else {
                        *writeTifSuccess = false;
                    }
                }
                if (needsJpg) {
                    if (NodeUtils::isJpgPreviewCurrent(targetH5, targetJpg)) {
                        return;
                    }
                    NodeUtils::generateJpgPreviewFromH5(targetH5, targetJpg, "dem");
                }
            });
            m_remedyWatcher.setFuture(future);
            if (executionState() != ExecutionState::Running) {
                setState(ExecutionState::Pending);
            }
        } else {
            QString resourceId;
            QString provenanceId;
            const quint64 restoreRevision = executionRevision();
            OutputCommitLease restoreLease = acquireOutputCommitLease(restoreRevision);
            if (!restoreLease || !restoreManagedDemResource(
                    projectPath(), NodeUtils::getProjectFilePath(_widget), targetTif, targetH5,
                    m_savedResourceId, m_savedResourceProvenanceId,
                    resourceId, provenanceId, &identityError)) {
                clearPublishedOutputs();
                setLastErrorMessage(identityError.isEmpty()
                    ? QStringLiteral("DEM 资源恢复在提交租约或事务校验时失败。") : identityError);
                setState(ExecutionState::Error);
                return false;
            }
            const QString managedRoot = QDir(projectPath()).absoluteFilePath(QStringLiteral(".dem_resources/%1").arg(resourceId));
            m_outputData = std::make_shared<AuxiliaryDemData>(
                QDir(managedRoot).absoluteFilePath(QStringLiteral("dem.tif")),
                QDir(managedRoot).absoluteFilePath(QStringLiteral("identity.h5")), resourceId, provenanceId, name);
            m_outputData->setProductDescriptor(legacyDescriptor
                ? auxiliaryDemEntityDescriptor(resourceId, provenanceId) : descriptor);
            m_referenceData = std::make_shared<AuxiliaryDemReferenceData>(resourceId, provenanceId, 1);
            m_referenceData->setProductDescriptor(auxiliaryDemReferenceDescriptor(resourceId, provenanceId));
            m_imageInfoData = std::make_shared<ImageInfoData>(targetJpg);
            setOutputData(0, m_outputData);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
            setState(ExecutionState::Completed);
        }

        updateCacheSizeLabel();
        return true;
    }

    setState(ExecutionState::Idle);
    return false;
}

QStringList DEMSourceNode::previewImagePaths() const
{
    QStringList list;
    const QString name = m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName;
    QStringList committedPaths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), name, committedPaths)) {
        return list;
    }
    for (const QString& outputPath : committedPaths) {
        if (!outputPath.endsWith(QStringLiteral(".h5"), Qt::CaseInsensitive)) continue;
        const QFileInfo h5Info(outputPath);
        const QString jpgPath = h5Info.absolutePath() + "/" + h5Info.baseName() + ".jpg";
        if (NodeUtils::isJpgPreviewCurrent(outputPath, jpgPath)) {
            list.append(jpgPath);
        }
    }
    return list;
}

void DEMSourceNode::updateWidgetSize()
{
    if (_widget) {
        _widget->adjustSize();
    }
}

QStandardItemModel* DEMSourceNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString DEMSourceNode::projectPath() const
{
    return NodeUtils::getProjectDirectory(_widget);
}

QString DEMSourceNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* DEMSourceNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void DEMSourceNode::updateLoginStatus()
{
    if (!m_demSourceCombo || !m_loginStatusLabel || !m_loginBtn || !m_logoutBtn)
        return;

    int demSource = m_demSourceCombo->currentData().toInt();
    if (demSource == 2) // Copernicus DEM 不需要登录
    {
        m_loginStatusLabel->setText(QStringLiteral("无需登录"));
        m_loginStatusLabel->setStyleSheet("color: gray;");
        m_loginBtn->setEnabled(false);
        m_logoutBtn->setEnabled(false);
        m_loginBtn->hide();
        m_logoutBtn->hide();
    }
    else
    {
        m_loginBtn->show();
        m_loginBtn->setEnabled(true);
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
        if (encryptedUser.isEmpty())
        {
            m_loginStatusLabel->setText(QStringLiteral("未登录"));
            m_loginStatusLabel->setStyleSheet("color: red;");
            m_logoutBtn->setEnabled(false);
            m_logoutBtn->hide();
        }
        else
        {
            QString username = QString::fromUtf8(QByteArray::fromBase64(encryptedUser.toUtf8()));
            m_loginStatusLabel->setText(QStringLiteral("已保存(%1)").arg(username));
            m_loginStatusLabel->setStyleSheet("color: green;");
            m_logoutBtn->setEnabled(true);
            m_logoutBtn->show();
            m_loginBtn->hide();
        }
    }
}

QStringList DEMSourceNode::getExpectedOutputFilePaths() const
{
    QString savePath = projectPath();
    QString name = m_outputNodeName.isEmpty() ? generateDefaultOutputName() : m_outputNodeName;
    QString targetH5 = savePath + "/" + name + "/" + name + "_dem.h5";
    QString targetTif = savePath + "/" + name + "/" + name + "_dem.tif";
    return QStringList() << targetH5 << targetTif;
}

// 提取输入图像地理边界的辅助函数
static bool getInputImageBounds(const QString& firstInput, const QString& save_path,
                                double& min_lon, double& max_lon, double& min_lat, double& max_lat)
{
    FormatConversion FC;
    cv::Mat mat_lon, mat_lat;
    bool mapped_check = false;
    {
        NodeUtils::Hdf5Locker locker;
        mapped_check = (NodeUtils::readMatFromH5(firstInput, "mapped_lon", mat_lon) &&
                        NodeUtils::readMatFromH5(firstInput, "mapped_lat", mat_lat));
    }
    if (mapped_check)
    {
        double min_lon_val, max_lon_val, min_lat_val, max_lat_val;
        cv::minMaxLoc(mat_lon, &min_lon_val, &max_lon_val);
        cv::minMaxLoc(mat_lat, &min_lat_val, &max_lat_val);
        min_lon = min_lon_val;
        max_lon = max_lon_val;
        min_lat = min_lat_val;
        max_lat = max_lat_val;
        return true;
    }

    std::string source_file;
    QString src_file;
    bool read_src_ok = false;
    {
        NodeUtils::Hdf5Locker locker;
        read_src_ok = NodeUtils::readStringFromH5(firstInput, "source_1", source_file);
    }
    if (read_src_ok)
    {
        src_file = save_path + "/" + QString(source_file.c_str());
        if (!QFile::exists(src_file))
        {
            src_file = firstInput;
        }
    }
    else
    {
        src_file = firstInput;
    }

    if (QFile::exists(src_file))
    {
        int sceneHeight = 0, sceneWidth = 0, offset_row = 0, offset_col = 0;
        cv::Mat lon_coef, lat_coef;
        bool read_para_ok = false;
        {
            NodeUtils::Hdf5Locker locker;
            if (NodeUtils::readScalarFromH5(src_file, "range_len", sceneWidth) &&
                NodeUtils::readScalarFromH5(src_file, "azimuth_len", sceneHeight) &&
                NodeUtils::readMatFromH5(src_file, "lon_coefficient", lon_coef) &&
                NodeUtils::readMatFromH5(src_file, "lat_coefficient", lat_coef))
            {
                read_para_ok = true;
                offset_row = 0;
                offset_col = 0;
                NodeUtils::readScalarFromH5(src_file, "offset_row", offset_row);
                NodeUtils::readScalarFromH5(src_file, "offset_col", offset_col);
            }
        }
        if (read_para_ok)
        {
            double lonMax = 0, lonMin = 0, latMax = 0, latMin = 0;
            if (Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
                &lonMax, &latMax, &lonMin, &latMin) == 0)
            {
                min_lon = lonMin;
                max_lon = lonMax;
                min_lat = latMin;
                max_lat = latMax;
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// DEMSourceValidationWidget - 外部 DEM 数据验证选项卡组件
// ============================================================================
class DEMSourceValidationWidget : public BaseValidationWidget
{
public:
    DEMSourceValidationWidget(DEMSourceNode* node, QWidget* parent)
        : BaseValidationWidget(node, parent)
        , m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }
    ~DEMSourceValidationWidget() override = default;

private:
    void setupUI()
    {
        setupBaseUI(QObject::tr("正在验证数据中..."),
                    QObject::tr("正在读取并核对 DEM 文件的投影、高程范围及几何范围。"),
                    QObject::tr("高程特征与有效性"));

        m_lblElevationRange = createFeatureLabel();
        m_lblValidPixelRate = createFeatureLabel();
        m_lblDemResolution = createFeatureLabel();
        m_lblCrsInfo = createFeatureLabel();
        m_lblBoundsCheck = createFeatureLabel();

        m_featureLayout->addRow(createHeaderLabel(QObject::tr("高程数值范围:")), m_lblElevationRange);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("有效像元比例:")), m_lblValidPixelRate);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("DEM 像元行列数:")), m_lblDemResolution);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("空间参考系(CRS):")), m_lblCrsInfo);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("地理范围包容性:")), m_lblBoundsCheck);
    }

    struct DEMValidationResults {
        bool success = false;
        QString errorMsg;
        
        // 用于比对的期望参数
        QString expSource;
        QString actSource;
        double expRes = 0.0;
        double actRes = 0.0;
        int expResMode = 0;
        
        // 范围包围框
        double expMinLon = 0.0, expMaxLon = 0.0;
        double expMinLat = 0.0, expMaxLat = 0.0;
        double actMinLon = 0.0, actMaxLon = 0.0;
        double actMinLat = 0.0, actMaxLat = 0.0;
        bool hasInputBounds = false;
        
        // 特征值
        int rows = 0;
        int cols = 0;
        QString crsWkt;
        double minElev = 0.0;
        double maxElev = 0.0;
        double validRate = 0.0;
        bool hasData = false;
        
        bool boundsPass = false;
        bool crsPass = false;
    };

    void startAsyncValidation() override
    {
        m_isTimedOut = false;

        if (m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(QObject::tr("验证未通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未检测到获取完成的 DEM 数据。请先运行该节点，成功生成 DEM 数据后再进行验证。"));
            
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);
            
            m_lblElevationRange->setText(QObject::tr("未执行"));
            m_lblValidPixelRate->setText(QObject::tr("未执行"));
            m_lblDemResolution->setText(QObject::tr("未执行"));
            m_lblCrsInfo->setText(QObject::tr("未执行"));
            m_lblBoundsCheck->setText(QObject::tr("未执行"));
            return;
        }

        QStringList expectedOuts = m_node->getExpectedOutputFilePaths();
        if (expectedOuts.size() < 2 || !QFileInfo::exists(expectedOuts[0])) {
            m_statusTitle->setText(QObject::tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("生成的 DEM 成果 H5 文件不存在或路径无效。"));
            return;
        }

        m_loadingOverlay->startLoading(QObject::tr("正在加载 DEM 文件并计算高程特征值..."));

        QString h5Path = expectedOuts[0];
        QString tifPath = expectedOuts[1];
        
        auto inData = m_node->getInputData();
        QString firstInput = (inData && !inData->filePaths().isEmpty()) ? inData->filePaths().first() : "";
        QString save_path = m_node->projectPath();

        // 获取参数用于比对
        QJsonObject saved = m_node->save();
        int expSrcIdx = saved["demSource"].toInt(0);
        int expResM = saved["resMode"].toInt(0);
        double expCustRes = saved["customResolution"].toDouble(0.0);

        QFuture<DEMValidationResults> future = QtConcurrent::run([h5Path, tifPath, expSrcIdx, expResM, expCustRes, firstInput, save_path]() {
            DEMValidationResults res;
            res.expResMode = expResM;
            
            // 期望数据源
            if (expSrcIdx == 0) res.expSource = "SRTM1";
            else if (expSrcIdx == 1) res.expSource = "SRTM3";
            else if (expSrcIdx == 2) res.expSource = "Copernicus";
            else if (expSrcIdx == 3) res.expSource = "ASTER";
            
            // 期望分辨率
            if (expResM == 0) res.expRes = (expSrcIdx == 1) ? 90.0 : 30.0;
            else if (expResM == 1) res.expRes = 30.0;
            else if (expResM == 2) res.expRes = 90.0;
            else res.expRes = expCustRes;

            // 1. 读取输入影像地理边界 (期望覆盖范围)
            double inMinLon = 0, inMaxLon = 0, inMinLat = 0, inMaxLat = 0;
            if (!firstInput.isEmpty()) {
                res.hasInputBounds = getInputImageBounds(firstInput, save_path, inMinLon, inMaxLon, inMinLat, inMaxLat);
                if (res.hasInputBounds) {
                    res.expMinLon = inMinLon;
                    res.expMaxLon = inMaxLon;
                    res.expMinLat = inMinLat;
                    res.expMaxLat = inMaxLat;
                }
            }

            // 2. 读取 H5 成果元数据及 DEM 矩阵
            double demMinLon = 0.0, demMaxLon = 0.0, demMinLat = 0.0, demMaxLat = 0.0;
            std::string dem_source_str;
            cv::Mat dem;
            bool read_h5_ok = false;
            {
                NodeUtils::Hdf5Locker locker;
                read_h5_ok = (NodeUtils::readMatFromH5(h5Path, "dem", dem) &&
                              NodeUtils::readScalarFromH5(h5Path, "dem_min_lon", demMinLon) &&
                              NodeUtils::readScalarFromH5(h5Path, "dem_max_lon", demMaxLon) &&
                              NodeUtils::readScalarFromH5(h5Path, "dem_min_lat", demMinLat) &&
                              NodeUtils::readScalarFromH5(h5Path, "dem_max_lat", demMaxLat) &&
                              NodeUtils::readStringFromH5(h5Path, "dem_source", dem_source_str));
            }

            if (read_h5_ok) {
                res.actSource = QString::fromStdString(dem_source_str);
                // H5 metadata may contain the protocol token "COPERNICUS" while
                // the node stores the same source as the display name "Copernicus".
                if (res.actSource.compare(QStringLiteral("COPERNICUS"), Qt::CaseInsensitive) == 0) {
                    res.actSource = QStringLiteral("Copernicus");
                }
                res.actMinLon = demMinLon;
                res.actMaxLon = demMaxLon;
                res.actMinLat = demMinLat;
                res.actMaxLat = demMaxLat;
                res.cols = dem.cols;
                res.rows = dem.rows;
            }

            // 3. 读取 TIFF 获取 CRS 详细信息 (使用 GDAL)
            if (QFile::exists(tifPath)) {
                GDALAllRegister();
                GDALDataset* poDS = (GDALDataset*)GDALOpen(tifPath.toLocal8Bit().constData(), GA_ReadOnly);
                if (poDS) {
                    if (!read_h5_ok) {
                        res.cols = poDS->GetRasterXSize();
                        res.rows = poDS->GetRasterYSize();
                    }
                    const char* projRef = poDS->GetProjectionRef();
                    if (projRef && strlen(projRef) > 0) {
                        res.crsWkt = QString::fromLocal8Bit(projRef);
                    } else {
                        res.crsWkt = "WGS 84";
                    }
                    
                    double adfGeoTransform[6];
                    if (poDS->GetGeoTransform(adfGeoTransform) == CE_None && !read_h5_ok) {
                        res.actMinLon = adfGeoTransform[0];
                        res.actMaxLat = adfGeoTransform[3];
                        res.actMaxLon = res.actMinLon + adfGeoTransform[1] * res.cols;
                        res.actMinLat = res.actMaxLat + adfGeoTransform[5] * res.rows;
                    }
                    GDALClose(poDS);
                }
            } else {
                res.crsWkt = "WGS 84";
            }

            // 计算实际分辨率
            if (res.cols > 0 && res.rows > 0) {
                double lonResDeg = (res.actMaxLon - res.actMinLon) / res.cols;
                res.actRes = lonResDeg * 111000.0; // 粗略转换为米
            }

            // 4. 统计分析 DEM 矩阵高程值范围和有效像元比例
            if (!dem.empty()) {
                cv::Mat doubleDem;
                if (dem.type() != CV_64F) {
                    dem.convertTo(doubleDem, CV_64F);
                } else {
                    doubleDem = dem;
                }

                double minElev = 99999.0;
                double maxElev = -99999.0;
                qint64 validCount = 0;
                qint64 totalCount = 0;
                
                int dRows = doubleDem.rows;
                int dCols = doubleDem.cols;
                int step = 1;
                // 如果像元数大于 400 万，采用跨步采样提高统计速度
                if (dRows * dCols > 4000000) {
                    step = std::max(1, (dRows * dCols) / 4000000);
                }

                for (int r = 0; r < dRows; r += step) {
                    for (int c = 0; c < dCols; c += step) {
                        double val = doubleDem.at<double>(r, c);
                        totalCount++;
                        // 过滤掉 NoData 填充值 -32767.0 与 -9999.0
                        if (val != -32767.0 && val != -9999.0 && val > -1000.0 && val < 9000.0) {
                            validCount++;
                            if (val < minElev) minElev = val;
                            if (val > maxElev) maxElev = val;
                        }
                    }
                }

                if (validCount > 0) {
                    res.minElev = minElev;
                    res.maxElev = maxElev;
                    res.validRate = (double)validCount / totalCount;
                    res.hasData = true;
                    res.success = true;
                } else {
                    res.errorMsg = QObject::tr("DEM 数据全部为 NoData (无效高程值)。");
                }
            } else {
                res.errorMsg = QObject::tr("无法从 H5 成果文件中加载 DEM 数据集。");
            }

            // 校验坐标系投影
            res.crsPass = res.crsWkt.contains("WGS 84", Qt::CaseInsensitive) || 
                          res.crsWkt.contains("WGS84", Qt::CaseInsensitive);

            // 校验包容性
            if (res.hasInputBounds) {
                bool lonOk = (res.actMinLon <= res.expMinLon + 1e-4) && (res.actMaxLon >= res.expMaxLon - 1e-4);
                bool latOk = (res.actMinLat <= res.expMinLat + 1e-4) && (res.actMaxLat >= res.expMaxLat - 1e-4);
                res.boundsPass = lonOk && latOk;
            } else {
                res.boundsPass = true;
            }

            return res;
        });

        auto* watcher = new QFutureWatcher<DEMValidationResults>(this);
        connect(watcher, &QFutureWatcher<DEMValidationResults>::finished, this, [this, watcher]() {
            if (m_isTimedOut) {
                watcher->deleteLater();
                return;
            }

            DEMValidationResults res = watcher->result();
            m_loadingOverlay->stopLoading();

            if (res.success) {
                m_compTable->clearComparison();
                m_compTable->setEnabled(true);

                // 1. 数据源比对
                m_compTable->addComparison(QObject::tr("DEM 数据源"), res.expSource, res.actSource);

                // 2. 几何范围比对
                QString expLonStr, actLonStr;
                if (res.hasInputBounds) {
                    if (res.boundsPass) {
                        // 如果校验通过，显示为一致的经度范围，使表格展示为“一致”
                        QString rangeStr = QString("[%1°, %2°]").arg(res.actMinLon, 0, 'f', 4).arg(res.actMaxLon, 0, 'f', 4);
                        expLonStr = rangeStr;
                        actLonStr = rangeStr;
                    } else {
                        // 如果不通过，展示要求的扩展包围框与实际包围框，以高亮“不一致”
                        expLonStr = QString("≥ [%1°, %2°]").arg(res.expMinLon - 0.05, 0, 'f', 4).arg(res.expMaxLon + 0.05, 0, 'f', 4);
                        actLonStr = QString("[%1°, %2°]").arg(res.actMinLon, 0, 'f', 4).arg(res.actMaxLon, 0, 'f', 4);
                    }
                } else {
                    expLonStr = QObject::tr("不限");
                    actLonStr = QString("[%1°, %2°]").arg(res.actMinLon, 0, 'f', 4).arg(res.actMaxLon, 0, 'f', 4);
                }
                m_compTable->addComparison(QObject::tr("经度覆盖范围"), expLonStr, actLonStr);

                QString expLatStr, actLatStr;
                if (res.hasInputBounds) {
                    if (res.boundsPass) {
                        QString rangeStr = QString("[%1°, %2°]").arg(res.actMinLat, 0, 'f', 4).arg(res.actMaxLat, 0, 'f', 4);
                        expLatStr = rangeStr;
                        actLatStr = rangeStr;
                    } else {
                        expLatStr = QString("≥ [%1°, %2°]").arg(res.expMinLat - 0.05, 0, 'f', 4).arg(res.expMaxLat + 0.05, 0, 'f', 4);
                        actLatStr = QString("[%1°, %2°]").arg(res.actMinLat, 0, 'f', 4).arg(res.actMaxLat, 0, 'f', 4);
                    }
                } else {
                    expLatStr = QObject::tr("不限");
                    actLatStr = QString("[%1°, %2°]").arg(res.actMinLat, 0, 'f', 4).arg(res.actMaxLat, 0, 'f', 4);
                }
                m_compTable->addComparison(QObject::tr("纬度覆盖范围"), expLatStr, actLatStr);

                // 3. 分辨率比对
                QString expResStr, actResStr;
                double targetRes = res.expRes;
                bool resMatch = (std::abs(res.actRes - targetRes) / targetRes < 0.15);
                if (resMatch) {
                    QString resStr = QString("%1m").arg(targetRes, 0, 'f', 1);
                    expResStr = resStr;
                    actResStr = resStr;
                } else {
                    expResStr = res.expResMode == 0 ? QObject::tr("原始分辨率") : QString("%1m").arg(targetRes, 0, 'f', 1);
                    actResStr = QString("%1m").arg(res.actRes, 0, 'f', 1);
                }
                m_compTable->addComparison(QObject::tr("目标网格分辨率"), expResStr, actResStr);

                // 4. CRS 比对
                QString actCrsName = res.crsPass ? "WGS 84" : QObject::tr("非标准 (或投影像元)");
                m_compTable->addComparison(QObject::tr("坐标系统 (CRS)"), "WGS 84", actCrsName);

                // 刷新界面标签
                m_lblElevationRange->setText(QString("%1m ~ %2m").arg(res.minElev, 0, 'f', 1).arg(res.maxElev, 0, 'f', 1));
                m_lblValidPixelRate->setText(QString("%1%").arg(res.validRate * 100.0, 0, 'f', 2));
                m_lblDemResolution->setText(QString("%1 × %2").arg(res.cols).arg(res.rows));
                
                QString crsDisp = QObject::tr("WGS 84 (EPSG:4326)");
                if (!res.crsPass) {
                    crsDisp = res.crsWkt.left(50) + (res.crsWkt.length() > 50 ? "..." : "");
                }
                m_lblCrsInfo->setText(crsDisp);

                if (res.hasInputBounds) {
                    m_lblBoundsCheck->setText(res.boundsPass ? QObject::tr("完全包含 (有效)") : QObject::tr("未完全包含 (警告)"));
                    m_lblBoundsCheck->setStyleSheet(res.boundsPass ? "color: #10B981; font-weight: bold;" : "color: #F59E0B; font-weight: bold;");
                } else {
                    m_lblBoundsCheck->setText(QObject::tr("未连接输入图像 (仅校验 DEM)"));
                    m_lblBoundsCheck->setStyleSheet("color: #6B7280;");
                }

                // 更新状态说明
                if (!res.boundsPass) {
                    m_statusTitle->setText(QObject::tr("校验范围不匹配"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                    m_statusDesc->setText(QObject::tr("下载 of DEM 地理范围未完全包含输入影像。请检查上游数据或手动扩展下载范围。"));
                } else if (res.validRate < 0.95) {
                    m_statusTitle->setText(QObject::tr("高程有效率偏低"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                    m_statusDesc->setText(QObject::tr("DEM 中包含较多无效像元（NoData，占比 %1%）。如果位于沿海或海洋，这属于正常现象。").arg(QString::number((1.0 - res.validRate) * 100.0, 'f', 1)));
                } else {
                    m_statusTitle->setText(QObject::tr("验证通过"));
                    m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                    m_statusDesc->setText(QObject::tr("DEM 文件的坐标系 (WGS 84)、高程区间与覆盖空间范围均核对一致，像元无位错。"));
                }
            } else {
                m_statusTitle->setText(QObject::tr("验证失败"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                m_statusDesc->setText(res.errorMsg);
            }

            watcher->deleteLater();
        });

        watcher->setFuture(future);
    }

private:
    DEMSourceNode* m_node = nullptr;
    
    QLabel* m_lblElevationRange = nullptr;
    QLabel* m_lblValidPixelRate = nullptr;
    QLabel* m_lblDemResolution = nullptr;
    QLabel* m_lblCrsInfo = nullptr;
    QLabel* m_lblBoundsCheck = nullptr;
};

::QWidget* DEMSourceNode::createValidationWidget(::QWidget* parent)
{
    return new DEMSourceValidationWidget(this, parent);
}

} // namespace QtNodes
