#include "Sentinel1ImportWorker.h"
#include <FormatConversion.h>
#include <Hdf5IO.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <QDir>
#include <QFileInfo>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

QString normalizeSentinel1MetadataValue(const QString& value)
{
    return value.trimmed().toUpper();
}

bool resolveCoreCompatibleOrbitPath(const QString& orbitPath, QString& corePath, QString& errorMessage)
{
    constexpr int kCoreLegacyMaxPath = 260;
    corePath.clear();
    const QString nativePath = QDir::toNativeSeparators(orbitPath);
    if (!QFileInfo(nativePath).isFile()) {
        errorMessage = QStringLiteral("EOF 文件不存在：%1").arg(orbitPath);
        return false;
    }

    const auto isAsciiPath = [](const QString& path) {
        for (const QChar character : path) {
            if (character.unicode() > 0x7f) {
                return false;
            }
        }
        return true;
    };
    if (isAsciiPath(nativePath) && nativePath.size() < kCoreLegacyMaxPath) {
        corePath = nativePath;
        return true;
    }

#ifdef _WIN32
    const std::wstring widePath = nativePath.toStdWString();
    std::vector<wchar_t> shortPathBuffer(32768, L'\0');
    const DWORD shortPathLength = GetShortPathNameW(
        widePath.c_str(), shortPathBuffer.data(), static_cast<DWORD>(shortPathBuffer.size()));
    if (shortPathLength == 0 || shortPathLength >= shortPathBuffer.size()) {
        errorMessage = QStringLiteral("无法为 EOF 路径取得 Windows ASCII 短路径：%1").arg(orbitPath);
        return false;
    }
    const QString shortPath = QString::fromWCharArray(shortPathBuffer.data(), static_cast<int>(shortPathLength));
    if (!isAsciiPath(shortPath) || shortPath.size() >= kCoreLegacyMaxPath) {
        errorMessage = QStringLiteral("Windows 未提供可供 Core 使用的短 ASCII EOF 路径：%1").arg(orbitPath);
        return false;
    }
    corePath = QDir::toNativeSeparators(shortPath);
    return true;
#else
    errorMessage = QStringLiteral("当前平台的 Core EOF 接口不支持 Unicode 路径：%1").arg(orbitPath);
    return false;
#endif
}

class Hdf5BatchLocker
{
public:
    Hdf5BatchLocker()
        : m_lock(Hdf5IO::acquireBatchLock())
        , m_status(m_lock ? Hdf5IO::getBatchLockStatus(m_lock) : -1)
    {
    }

    ~Hdf5BatchLocker()
    {
        if (m_lock) {
            Hdf5IO::releaseBatchLock(m_lock);
        }
    }

    bool isLocked() const { return m_status == 0; }

private:
    Hdf5IO::BatchLock* m_lock;
    int m_status;
};

bool readOptionalSentinel1Metadata(const QString& h5Path, const char* name, bool& exists,
                                   std::string& value, QString& errorMessage)
{
    Hdf5IO::Hdf5ReadDiagnostic diagnostic = {};
    const int status = Hdf5IO::readStringDiagnosed(h5Path.toStdString().c_str(), name, value, &diagnostic);
    if (status == 0) {
        exists = true;
        return true;
    }

    if (diagnostic.stage == Hdf5IO::HDF5_READ_STAGE_OPEN_DATASET && diagnostic.hdf5Status == 0) {
        exists = false;
        value.clear();
        return true;
    }

    errorMessage = QStringLiteral("Unable to read Sentinel-1 metadata field %1 in %2: stage=%3, status=%4, detail=%5")
        .arg(QString::fromLatin1(name), h5Path)
        .arg(diagnostic.stage)
        .arg(diagnostic.hdf5Status)
        .arg(QString::fromLocal8Bit(diagnostic.errorStack));
    return false;
}

bool persistSentinel1ProductIdentity(const QString& h5Path, const QString& subswath,
                                     const QString& polarization, QString& errorMessage)
{
    const QString normalizedSwath = normalizeSentinel1MetadataValue(subswath);
    const QString normalizedPolarization = normalizeSentinel1MetadataValue(polarization);
    if (normalizedSwath.isEmpty() || normalizedPolarization.isEmpty()) {
        errorMessage = QStringLiteral("Sentinel-1 import did not provide a valid swath and polarization.");
        return false;
    }

    NodeUtils::Hdf5Locker locker(h5Path);
    if (!locker.isLocked()) {
        errorMessage = QStringLiteral("Unable to lock imported Sentinel-1 H5 for metadata write: %1").arg(h5Path);
        return false;
    }

    Hdf5BatchLocker hdf5BatchLocker;
    if (!hdf5BatchLocker.isLocked()) {
        errorMessage = QStringLiteral("Unable to acquire HDF5 batch lock for Sentinel-1 metadata write: %1").arg(h5Path);
        return false;
    }

    bool hasStoredSwath = false;
    bool hasStoredPolarization = false;
    std::string storedSwath;
    std::string storedPolarization;
    if (!readOptionalSentinel1Metadata(h5Path, "swath", hasStoredSwath, storedSwath, errorMessage) ||
        !readOptionalSentinel1Metadata(h5Path, "polarization", hasStoredPolarization, storedPolarization, errorMessage)) {
        return false;
    }

    if (hasStoredSwath &&
        normalizeSentinel1MetadataValue(QString::fromStdString(storedSwath)) != normalizedSwath) {
        errorMessage = QStringLiteral("Sentinel-1 swath metadata mismatch in %1: expected %2, found %3")
            .arg(h5Path, normalizedSwath,
                 normalizeSentinel1MetadataValue(QString::fromStdString(storedSwath)));
        return false;
    }
    if (hasStoredPolarization &&
        normalizeSentinel1MetadataValue(QString::fromStdString(storedPolarization)) != normalizedPolarization) {
        errorMessage = QStringLiteral("Sentinel-1 polarization metadata mismatch in %1: expected %2, found %3")
            .arg(h5Path, normalizedPolarization,
                 normalizeSentinel1MetadataValue(QString::fromStdString(storedPolarization)));
        return false;
    }

    const std::string nativePath = h5Path.toStdString();
    bool writeFailed = false;
    QStringList failedFields;
    if (!hasStoredSwath && Hdf5IO::createString(nativePath.c_str(), "swath",
                                                 normalizedSwath.toStdString().c_str()) != 0) {
        writeFailed = true;
        failedFields.append(QStringLiteral("swath"));
    }
    if (!hasStoredPolarization && Hdf5IO::createString(nativePath.c_str(), "polarization",
                                                        normalizedPolarization.toStdString().c_str()) != 0) {
        writeFailed = true;
        failedFields.append(QStringLiteral("polarization"));
    }
    if (writeFailed) {
        QStringList rollbackFailures;
        if (!hasStoredSwath && Hdf5IO::removeDatasetIfPresent(nativePath.c_str(), "swath") < 0) {
            rollbackFailures.append(QStringLiteral("swath"));
        }
        if (!hasStoredPolarization && Hdf5IO::removeDatasetIfPresent(nativePath.c_str(), "polarization") < 0) {
            rollbackFailures.append(QStringLiteral("polarization"));
        }

        errorMessage = QStringLiteral("Unable to write missing Sentinel-1 metadata field(s) %1 to %2")
            .arg(failedFields.join(QStringLiteral(", ")), h5Path);
        if (!rollbackFailures.isEmpty()) {
            errorMessage += QStringLiteral("; metadata rollback failed for field(s): %1")
                .arg(rollbackFailures.join(QStringLiteral("; ")));
        }
        return false;
    }

    if (!hasStoredSwath && !readOptionalSentinel1Metadata(h5Path, "swath", hasStoredSwath, storedSwath, errorMessage)) {
        return false;
    }
    if (!hasStoredPolarization && !readOptionalSentinel1Metadata(h5Path, "polarization", hasStoredPolarization, storedPolarization, errorMessage)) {
        return false;
    }

    if (!hasStoredSwath || !hasStoredPolarization) {
        errorMessage = QStringLiteral("Sentinel-1 swath/polarization metadata was not created in %1").arg(h5Path);
        return false;
    }

    if (normalizeSentinel1MetadataValue(QString::fromStdString(storedSwath)) != normalizedSwath ||
        normalizeSentinel1MetadataValue(QString::fromStdString(storedPolarization)) != normalizedPolarization) {
        errorMessage = QStringLiteral("Sentinel-1 swath/polarization metadata verification mismatch in %1").arg(h5Path);
        return false;
    }

    InSARLogManager::LogInfo("Sentinel1ImportWorker",
        QStringLiteral("Persisted Sentinel-1 product identity: file=%1, swath=%2, polarization=%3")
            .arg(h5Path, normalizedSwath, normalizedPolarization));
    return true;
}

bool persistSentinel1OrbitIdentityAudit(const QString& h5Path, const QString& subswath,
                                        const QString& polarization, QString& errorMessage)
{
    const QString normalizedSwath = normalizeSentinel1MetadataValue(subswath);
    const QString normalizedPolarization = normalizeSentinel1MetadataValue(polarization);
    NodeUtils::Hdf5Locker locker(h5Path);
    if (!locker.isLocked()) {
        errorMessage = QStringLiteral("Unable to lock Sentinel-1 H5 for orbit identity audit: %1").arg(h5Path);
        return false;
    }
    Hdf5BatchLocker hdf5BatchLocker;
    if (!hdf5BatchLocker.isLocked()) {
        errorMessage = QStringLiteral("Unable to acquire HDF5 batch lock for orbit identity audit: %1").arg(h5Path);
        return false;
    }

    struct AuditField {
        const char* name;
        QString expected;
        bool exists = false;
        std::string value;
    } fields[] = {
        {"orbit_input_swath", normalizedSwath},
        {"orbit_input_polarization", normalizedPolarization},
        {"orbit_identity_consistency", QStringLiteral("verified")}
    };

    for (AuditField& field : fields) {
        if (!readOptionalSentinel1Metadata(h5Path, field.name, field.exists, field.value, errorMessage)) {
            return false;
        }
        if (field.exists && normalizeSentinel1MetadataValue(QString::fromStdString(field.value)) !=
                normalizeSentinel1MetadataValue(field.expected)) {
            errorMessage = QStringLiteral("Sentinel-1 orbit identity audit field mismatch in %1: %2")
                .arg(h5Path, QString::fromLatin1(field.name));
            return false;
        }
    }

    const std::string nativePath = h5Path.toStdString();
    QStringList createdFields;
    for (const AuditField& field : fields) {
        if (!field.exists && Hdf5IO::createString(nativePath.c_str(), field.name,
                                                   field.expected.toStdString().c_str()) != 0) {
            createdFields.append(QString::fromLatin1(field.name));
        }
    }
    if (!createdFields.isEmpty()) {
        for (const AuditField& field : fields) {
            if (!field.exists) {
                Hdf5IO::removeDatasetIfPresent(nativePath.c_str(), field.name);
            }
        }
        errorMessage = QStringLiteral("Unable to write Sentinel-1 orbit identity audit field(s) %1 to %2")
            .arg(createdFields.join(QStringLiteral(", ")), h5Path);
        return false;
    }

    for (AuditField& field : fields) {
        bool exists = false;
        std::string value;
        if (!readOptionalSentinel1Metadata(h5Path, field.name, exists, value, errorMessage) ||
            !exists || normalizeSentinel1MetadataValue(QString::fromStdString(value)) !=
                normalizeSentinel1MetadataValue(field.expected)) {
            errorMessage = QStringLiteral("Unable to verify Sentinel-1 orbit identity audit field %1 in %2")
                .arg(QString::fromLatin1(field.name), h5Path);
            return false;
        }
    }
    InSARLogManager::LogInfo("Sentinel1ImportWorker",
        QStringLiteral("Persisted Sentinel-1 orbit identity audit: file=%1, swath=%2, polarization=%3")
            .arg(h5Path, normalizedSwath, normalizedPolarization));
    return true;
}

bool validateAcquisitionTimeRange(const QString& h5Path, QString& errorMessage)
{
    std::string startText;
    std::string stopText;
    QString readError;
    if (!NodeUtils::readStringFromH5(h5Path, "acquisition_start_time", startText, &readError) ||
        !NodeUtils::readStringFromH5(h5Path, "acquisition_stop_time", stopText, &readError)) {
        errorMessage = QStringLiteral("Unable to read Sentinel-1 acquisition time metadata from %1: %2")
            .arg(h5Path, readError);
        return false;
    }

    FormatConversion conversion;
    double startGps = 0.0;
    double stopGps = 0.0;
    if (conversion.utc2gps(startText.c_str(), &startGps) != 0 ||
        conversion.utc2gps(stopText.c_str(), &stopGps) != 0 ||
        stopGps <= startGps) {
        errorMessage = QStringLiteral("Invalid Sentinel-1 acquisition time range in %1: start=%2, stop=%3")
            .arg(h5Path)
            .arg(QString::fromStdString(startText))
            .arg(QString::fromStdString(stopText));
        return false;
    }

    InSARLogManager::LogInfo("Sentinel1ImportWorker",
        QStringLiteral("Validated Sentinel-1 acquisition time metadata: file=%1, start=%2 (%3), stop=%4 (%5)")
            .arg(h5Path)
            .arg(QString::fromStdString(startText))
            .arg(startGps, 0, 'f', 3)
            .arg(QString::fromStdString(stopText))
            .arg(stopGps, 0, 'f', 3));
    return true;
}

} // namespace

// 进度回调上下文
struct S1ProgressContext
{
    Sentinel1ImportWorker* worker;
    int progressMin;
    int progressMax;
};

// DLL进度回调：将DLL内部0-100映射到当前任务的进度区间
static bool onS1DllProgress(int percent, const char* message, void* userData)
{
    S1ProgressContext* ctx = static_cast<S1ProgressContext*>(userData);
    if (!ctx || !ctx->worker)
        return false;

    if (ctx->worker->isStopRequested())
    {
        return false;
    }

    int mapped = ctx->progressMin + (ctx->progressMax - ctx->progressMin) * percent / 100;
    QString msg = (message && message[0]) ? QString::fromUtf8(message) : QStringLiteral("正在导入...");
    ctx->worker->updateImportProgress(mapped, msg);
    return true;
}

Sentinel1ImportWorker::Sentinel1ImportWorker(QObject* parent)
    : BaseImportWorker("Sentinel1", parent)
{
}

Sentinel1ImportWorker::~Sentinel1ImportWorker()
{
}

bool Sentinel1ImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                       int progressMin, int progressMax, QString& outErrorMsg)
{
    if (arguments.size() < 3) return false;

    QString manifest_file = arguments[0];
    QString subswath      = arguments[1];
    QString polarization  = arguments[2];

    // 智能识别 arguments[3]：EOF 文件路径 vs burst 起始编号
    // EOF 路径包含 ".EOF" 扩展名或路径分隔符；burst 编号为纯数字字符串
    QString pod_file;
    int burstArgStart = 3;
    if (arguments.size() > 3 && arguments[3].contains(".EOF", Qt::CaseInsensitive)) {
        pod_file = arguments[3];
        burstArgStart = 4;
    }

    int start_burst = -1;
    int end_burst = -1;
    if (arguments.size() > burstArgStart + 1) {
        start_burst = arguments[burstArgStart].toInt();
        end_burst = arguments[burstArgStart + 1].toInt();
    }

    QString corePodFile;
    std::string corePodFileUtf8;
    if (!pod_file.isEmpty()) {
        if (!resolveCoreCompatibleOrbitPath(pod_file, corePodFile, outErrorMsg)) {
            InSARLogManager::LogError("Sentinel1ImportWorker", outErrorMsg);
            return false;
        }
        corePodFileUtf8 = corePodFile.toStdString();
    }

    InSARLogManager::LogInfo("Sentinel1ImportWorker",
        pod_file.isEmpty()
            ? QStringLiteral("未提供 EOF，导入将使用 SAFE 中的轨道：%1").arg(manifest_file)
            : QStringLiteral("导入将应用 EOF 轨道：%1").arg(pod_file));

    FormatConversion conversion;
    S1ProgressContext context;
    context.worker = this;
    context.progressMin = progressMin;
    context.progressMax = progressMax;

    int ret = conversion.import_sentinel(
        manifest_file.toStdString().c_str(),
        subswath.toStdString().c_str(),
        polarization.toStdString().c_str(),
        outputPath.toStdString().c_str(),
        pod_file.isEmpty() ? nullptr : corePodFileUtf8.c_str(),
        onS1DllProgress,
        &context,
        start_burst,
        end_burst
    );

    if (ret < 0) {
        outErrorMsg = QString("底转换 DLL 执行失败，错误码：%1。请核对爆块(Burst)范围或原始数据文件是否损坏。").arg(ret);
    }

    if (ret < 0) {
        return false;
    }

    if (!persistSentinel1ProductIdentity(outputPath, subswath, polarization, outErrorMsg) ||
        !validateAcquisitionTimeRange(outputPath, outErrorMsg)) {
        return false;
    }

    if (!pod_file.isEmpty()) {
        if (!persistSentinel1OrbitIdentityAudit(outputPath, subswath, polarization, outErrorMsg)) {
            return false;
        }
        int fineRows = 0;
        int fineColumns = 0;
        bool hasUsableFineOrbit = false;
        const bool isPreciseOrbit = pod_file.contains(QStringLiteral("POEORB"), Qt::CaseInsensitive);
        const QString orbitType = isPreciseOrbit
            ? QStringLiteral("Precise (POE)")
            : QStringLiteral("Reconstructed (RES)");
        {
            NodeUtils::Hdf5Locker locker(outputPath);
            cv::Mat fineOrbit;
            if (conversion.read_array_from_h5(outputPath.toLocal8Bit().constData(),
                                              "fine_state_vec", fineOrbit) == 0) {
                fineRows = fineOrbit.rows;
                fineColumns = fineOrbit.cols;
                hasUsableFineOrbit = fineRows >= 7 && fineColumns == 7 && fineOrbit.type() == CV_64F;
            }
            if (hasUsableFineOrbit) {
                const std::string orbitTypeUtf8 = orbitType.toStdString();
                if (conversion.write_str_to_h5(outputPath.toLocal8Bit().constData(),
                                               "orbit_type", orbitTypeUtf8.c_str()) != 0) {
                    outErrorMsg = QStringLiteral("精密轨道已生成但无法写入 orbit_type：file=%1，type=%2")
                        .arg(outputPath, orbitType);
                    InSARLogManager::LogError("Sentinel1ImportWorker", outErrorMsg);
                    return false;
                }
                const std::string orbitSourceFile = QFileInfo(pod_file).fileName().toStdString();
                if (conversion.write_str_to_h5(outputPath.toLocal8Bit().constData(),
                                               "orbit_source_file", orbitSourceFile.c_str()) != 0) {
                    outErrorMsg = QStringLiteral("精密轨道已生成但无法写入 orbit_source_file：file=%1，EOF=%2")
                        .arg(outputPath, pod_file);
                    InSARLogManager::LogError("Sentinel1ImportWorker", outErrorMsg);
                    return false;
                }
            }
        }
        if (hasUsableFineOrbit) {
            InSARLogManager::LogInfo("Sentinel1ImportWorker",
                QStringLiteral("%1已应用并写入 H5：file=%2，fine_state_vec=%3 行，EOF=%4")
                    .arg(isPreciseOrbit ? QStringLiteral("精密轨道") : QStringLiteral("重建轨道"))
                    .arg(outputPath).arg(fineRows).arg(pod_file));
        } else {
            outErrorMsg = QStringLiteral("外部 EOF 已传入但未生成可用于基线计算的 fine_state_vec（需要 Float64、至少 7 行、7 列）：file=%1，rows=%2，columns=%3，EOF=%4")
                .arg(outputPath).arg(fineRows).arg(fineColumns).arg(pod_file);
            InSARLogManager::LogError("Sentinel1ImportWorker", outErrorMsg);
            return false;
        }
    } else {
        InSARLogManager::LogInfo("Sentinel1ImportWorker",
            QStringLiteral("本次导入未应用外部精密轨道：file=%1").arg(outputPath));
    }
    return true;
}
