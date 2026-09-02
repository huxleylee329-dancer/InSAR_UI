#include "S1TopsBackGeocodingWorker.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include <Utils.h>
#include <FormatConversion.h>
#include <Hdf5IO.h>
#include <Registration.h>
#include "NodeUtils.h"
#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QFileInfo>
#include <QFile>
#include <QStringList>
#include <QSet>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Registration_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Registration.lib")
#endif

namespace {

QString nativeText(const char* value)
{
    return value ? QString::fromUtf8(value) : QString();
}

int copySentinelRegistrationMetadata(FormatConversion& conversion,
                                     const std::string& sourcePath,
                                     const std::string& outputPath)
{
    int sameFile = 0;
    if (Hdf5IO::areSameExistingFile(sourcePath.c_str(), outputPath.c_str(), &sameFile) != 0 ||
        sameFile != 0) {
        return -1;
    }

    // Keep this list aligned with FormatConversion::Copy_para_from_h5_2_h5.
    // Sentinel imports may legitimately contain only source_1, whereas that
    // generic helper treats an unpaired source path as a failed copy.
    static const char* const stringDatasets[] = {
        "file_type", "sensor", "polarization", "imaging_mode", "lookside", "orbit_dir", "swath",
        "acquisition_start_time", "acquisition_stop_time", "source_1", "source_2",
        "source_path_encoding", "source_path_format_version", "state_vec_time_scale",
        "fine_state_vec_time_scale", "acquisition_time_gps_scale", "h5_time_reference_version",
        "sentinel_geometry_coefficient_contract"
    };
    static const char* const arrayDatasets[] = {
        "orbit_altitude", "carrier_frequency", "heading", "prf", "inc_center", "gcps",
        "azimuth_resolution", "range_resolution", "azimuth_spacing", "range_spacing", "state_vec",
		"acquisition_start_time_gps", "acquisition_stop_time_gps",
		"fine_state_vec", "doppler_centroid", "doppler_coefficient_a", "doppler_coefficient_b",
		"burstAzimuthTime", "azimuthFmRateList", "dcEstimateList", "firstValidSample", "lastValidSample",
		"firstValidLine", "lastValidLine", "burstCount", "linesPerBurst", "azimuthSteeringRate",
		"lon_coefficient", "lat_coefficient", "row_coefficient", "col_coefficient", "inc_coefficient",
        "inc_coefficient_r", "inc_center", "row_coefficient", "slant_range_first_pixel", "topLeftLon",
        "topLeftLat", "topRightLon", "topRightLat", "bottomLeftLon", "bottomLeftLat", "bottomRightLon",
        "bottomRightLat", "TR_mode"
    };

    int result = Hdf5IO::copyDatasetsIfPresent(sourcePath.c_str(), outputPath.c_str(),
        stringDatasets, static_cast<int>(sizeof(stringDatasets) / sizeof(stringDatasets[0])), false);
    if (result != 0) {
        return result;
    }
    result = Hdf5IO::copyDatasetsIfPresent(sourcePath.c_str(), outputPath.c_str(),
        arrayDatasets, static_cast<int>(sizeof(arrayDatasets) / sizeof(arrayDatasets[0])), true);
    if (result != 0) {
        return result;
    }

    // A legacy Sentinel-1 input may already declare the v2/GPS orbit-time
    // contract but lack explicit acquisition GPS scalars. Materialize those
    // scalars only while producing the registered H5; v5 consumers continue
    // to reject UTC-derived time at their read boundary.
    int timeReferenceExists = 0;
    int stateVectorScaleExists = 0;
    if (Hdf5IO::datasetExists(outputPath.c_str(), "h5_time_reference_version", &timeReferenceExists) != 0 ||
        Hdf5IO::datasetExists(outputPath.c_str(), "state_vec_time_scale", &stateVectorScaleExists) != 0) {
        return -1;
    }
    if (timeReferenceExists != 0 && stateVectorScaleExists != 0) {
        std::string timeReferenceVersion;
        std::string stateVectorScale;
        if (Hdf5IO::readString(outputPath.c_str(), "h5_time_reference_version", timeReferenceVersion) != 0 ||
            Hdf5IO::readString(outputPath.c_str(), "state_vec_time_scale", stateVectorScale) != 0) {
            return -1;
        }
        if (timeReferenceVersion == "2" && stateVectorScale == "GPS") {
            int startGpsExists = 0;
            int stopGpsExists = 0;
            int gpsScaleExists = 0;
            if (Hdf5IO::datasetExists(outputPath.c_str(), "acquisition_start_time_gps", &startGpsExists) != 0 ||
                Hdf5IO::datasetExists(outputPath.c_str(), "acquisition_stop_time_gps", &stopGpsExists) != 0 ||
                Hdf5IO::datasetExists(outputPath.c_str(), "acquisition_time_gps_scale", &gpsScaleExists) != 0) {
                return -1;
            }
            if (startGpsExists == 0 && stopGpsExists == 0 && gpsScaleExists == 0) {
                std::string startUtc;
                std::string stopUtc;
                double startGps = 0.0;
                double stopGps = 0.0;
                if (Hdf5IO::readString(outputPath.c_str(), "acquisition_start_time", startUtc) != 0 ||
                    Hdf5IO::readString(outputPath.c_str(), "acquisition_stop_time", stopUtc) != 0 ||
                    conversion.utc2gps(startUtc.c_str(), &startGps) != 0 ||
                    conversion.utc2gps(stopUtc.c_str(), &stopGps) != 0 ||
                    !std::isfinite(startGps) || !std::isfinite(stopGps) || !(stopGps > startGps)) {
                    return -1;
                }
                const cv::Mat startGpsMat(1, 1, CV_64F, cv::Scalar(startGps));
                const cv::Mat stopGpsMat(1, 1, CV_64F, cv::Scalar(stopGps));
                if (Hdf5IO::writeArray(outputPath.c_str(), "acquisition_start_time_gps", startGpsMat) != 0 ||
                    Hdf5IO::writeArray(outputPath.c_str(), "acquisition_stop_time_gps", stopGpsMat) != 0 ||
                    Hdf5IO::writeString(outputPath.c_str(), "acquisition_time_gps_scale", "GPS") != 0) {
                    return -1;
                }
            } else if (startGpsExists == 0 || stopGpsExists == 0 || gpsScaleExists == 0) {
                return -1;
            }
        }
    }
    return 0;
}

struct Sentinel1ProductIdentity
{
    QString swath;
    QString polarization;
};

struct ComplexOutputCoverage
{
    bool available = false;
    int rows = 0;
    int columns = 0;
    qint64 nonZeroSamples = 0;
    qint64 totalSamples = 0;
    QString errorMessage;
};

ComplexOutputCoverage measureComplexOutputCoverage(const QString& h5Path)
{
    ComplexOutputCoverage coverage;
    FormatConversion conversion;
    const std::string nativePath = h5Path.toStdString();
    NodeUtils::Hdf5Locker locker(nativePath);
    if (!locker.isLocked()) {
        coverage.errorMessage = QStringLiteral("Unable to lock output H5.");
        return coverage;
    }

    int realRows = 0;
    int realColumns = 0;
    int imaginaryRows = 0;
    int imaginaryColumns = 0;
    if (conversion.get_dataset_dims(nativePath.c_str(), "s_re", &realRows, &realColumns) != 0 ||
        conversion.get_dataset_dims(nativePath.c_str(), "s_im", &imaginaryRows, &imaginaryColumns) != 0 ||
        realRows <= 0 || realColumns <= 0 || realRows != imaginaryRows || realColumns != imaginaryColumns) {
        coverage.errorMessage = QStringLiteral("Unable to read matching s_re/s_im dimensions.");
        return coverage;
    }

    coverage.rows = realRows;
    coverage.columns = realColumns;
    coverage.totalSamples = static_cast<qint64>(realRows) * realColumns;
    constexpr int kRowsPerBlock = 64;
    for (int row = 0; row < realRows; row += kRowsPerBlock) {
        const int rowsToRead = qMin(kRowsPerBlock, realRows - row);
        cv::Mat realBlock;
        cv::Mat imaginaryBlock;
        if (conversion.read_subarray_from_h5(nativePath.c_str(), "s_re", row, 0, rowsToRead, realColumns, realBlock) != 0 ||
            conversion.read_subarray_from_h5(nativePath.c_str(), "s_im", row, 0, rowsToRead, realColumns, imaginaryBlock) != 0 ||
            realBlock.empty() || imaginaryBlock.empty()) {
            coverage.errorMessage = QStringLiteral("Unable to read complex output samples.");
            return coverage;
        }

        cv::Mat realNonZero;
        cv::Mat imaginaryNonZero;
        cv::Mat complexNonZero;
        cv::compare(realBlock, cv::Scalar(0), realNonZero, cv::CMP_NE);
        cv::compare(imaginaryBlock, cv::Scalar(0), imaginaryNonZero, cv::CMP_NE);
        cv::bitwise_or(realNonZero, imaginaryNonZero, complexNonZero);
        coverage.nonZeroSamples += cv::countNonZero(complexNonZero);
    }

    coverage.available = true;
    return coverage;
}

QString describeH5ForMetadataCopy(FormatConversion& conversion, const QString& h5Path)
{
    const QFileInfo fileInfo(h5Path);
    QStringList details;
    details << QStringLiteral("exists=%1").arg(fileInfo.isFile() ? QStringLiteral("true") : QStringLiteral("false"));
    details << QStringLiteral("size=%1").arg(fileInfo.exists() ? QString::number(fileInfo.size()) : QStringLiteral("n/a"));
    details << QStringLiteral("modified=%1").arg(fileInfo.exists()
        ? fileInfo.lastModified().toString(Qt::ISODateWithMs) : QStringLiteral("n/a"));

    static const char* const datasets[] = {
        "s_re", "s_im", "swath", "polarization", "state_vec", "fine_state_vec",
        "lat_coefficient", "lon_coefficient", "inc_coefficient", "prf",
        "carrier_frequency", "range_spacing", "slant_range_first_pixel",
        "azimuth_len", "range_len", "offset_row", "offset_col",
        "s1_tops_back_geocoding_complete"
    };
    const std::string nativePath = h5Path.toStdString();
    for (const char* dataset : datasets) {
        int rows = 0;
        int columns = 0;
        const int result = conversion.get_dataset_dims(nativePath.c_str(), dataset, &rows, &columns);
        details << (result == 0
            ? QStringLiteral("%1=%2x%3").arg(QString::fromLatin1(dataset)).arg(rows).arg(columns)
            : QStringLiteral("%1=unavailable(rc=%2)").arg(QString::fromLatin1(dataset)).arg(result));
    }

    static const char* const stringDatasets[] = {
        "source_1", "source_2", "source_path_encoding", "source_path_format_version"
    };
    bool source1Present = false;
    bool source2Present = false;
    for (const char* dataset : stringDatasets) {
        std::string value;
        QString readError;
        const bool present = NodeUtils::readStringFromH5(h5Path, QString::fromLatin1(dataset), value, &readError);
        if (qstrcmp(dataset, "source_1") == 0) source1Present = present;
        if (qstrcmp(dataset, "source_2") == 0) source2Present = present;
        details << (present
            ? QStringLiteral("%1=present(value=%2)").arg(QString::fromLatin1(dataset), QString::fromStdString(value))
            : QStringLiteral("%1=absent(error=%2)").arg(QString::fromLatin1(dataset), readError));
    }
    QString sourcePairState;
    if (source1Present && source2Present) {
        sourcePairState = QStringLiteral("both_present");
    } else if (source1Present) {
        sourcePairState = QStringLiteral("only_source_1");
    } else if (source2Present) {
        sourcePairState = QStringLiteral("only_source_2");
    } else {
        sourcePairState = QStringLiteral("both_absent");
    }
    details << QStringLiteral("sourcePair=%1").arg(sourcePairState);
    return details.join(QStringLiteral(", "));
}

QString describeH5MetadataCopySummary(FormatConversion& conversion, const QString& h5Path)
{
    const QFileInfo fileInfo(h5Path);
    const std::string nativePath = h5Path.toStdString();
    const auto dimensions = [&conversion, &nativePath](const char* dataset) {
        int rows = 0;
        int columns = 0;
        const int result = conversion.get_dataset_dims(nativePath.c_str(), dataset, &rows, &columns);
        return result == 0
            ? QStringLiteral("%1x%2").arg(rows).arg(columns)
            : QStringLiteral("unavailable(rc=%1)").arg(result);
    };

    return QStringLiteral("exists=%1, size=%2, s_re=%3, s_im=%4")
        .arg(fileInfo.isFile() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(fileInfo.exists() ? QString::number(fileInfo.size()) : QStringLiteral("n/a"))
        .arg(dimensions("s_re"), dimensions("s_im"));
}

bool readSentinel1ProductIdentity(const QString& h5Path, Sentinel1ProductIdentity& identity,
                                  QString& errorMessage)
{
    std::string swath;
    std::string polarization;
    QString readError;
    if (!NodeUtils::readStringFromH5(h5Path, "swath", swath, &readError) ||
        !NodeUtils::readStringFromH5(h5Path, "polarization", polarization, &readError)) {
        errorMessage = QStringLiteral("Missing Sentinel-1 swath/polarization metadata in %1: %2")
            .arg(h5Path, readError);
        return false;
    }

    identity.swath = QString::fromStdString(swath).trimmed().toUpper();
    identity.polarization = QString::fromStdString(polarization).trimmed().toUpper();
    if (identity.swath.isEmpty() || identity.polarization.isEmpty()) {
        errorMessage = QStringLiteral("Empty Sentinel-1 swath/polarization metadata in %1").arg(h5Path);
        return false;
    }
    return true;
}

QString zeroDopplerReasonText(int reason)
{
    switch (reason) {
    case SENTINEL_ZERO_DOPPLER_INVALID_INPUT:
        return QStringLiteral("invalid input");
    case SENTINEL_ZERO_DOPPLER_NO_BRACKET:
        return QStringLiteral("no orbit-time bracket");
    case SENTINEL_ZERO_DOPPLER_NONFINITE_RESULT:
        return QStringLiteral("non-finite result");
    default:
        return QStringLiteral("unknown reason");
    }
}

QString zeroDopplerCallPathText(int callPath)
{
    switch (callPath) {
    case SENTINEL_ZERO_DOPPLER_CALL_MASTER_RG_AZ:
        return QStringLiteral("master getRgAzPosition");
    case SENTINEL_ZERO_DOPPLER_CALL_SLAVE_RG_AZ:
        return QStringLiteral("slave getRgAzPosition");
    default:
        return QStringLiteral("unknown call path");
    }
}

QString zeroDopplerStatisticText(const SentinelZeroDopplerFailureStatistic& statistic)
{
    return QStringLiteral("Zero-Doppler failures: image %1, burst %2, %3, %4, return=%5, count=%6.")
        .arg(statistic.imageIndex)
        .arg(statistic.burstIndex)
        .arg(zeroDopplerReasonText(statistic.reason))
        .arg(zeroDopplerCallPathText(statistic.callPath))
        .arg(statistic.returnCode)
        .arg(statistic.count);
}

} // namespace

struct BackGeocodingOutputCleanupGuard {
	BackGeocodingOutputCleanupGuard(const QStringList& outputPaths, const QSet<QString>& preExistingOutputs,
		const QString& savePath, const QString& dstNode, bool outputDirExisted)
		: outputPaths(outputPaths), preExistingOutputs(preExistingOutputs), savePath(savePath), dstNode(dstNode), outputDirExisted(outputDirExisted) {}

	~BackGeocodingOutputCleanupGuard() {
		cleanup();
	}

	QStringList cleanup() {
		if (dismissed || cleanupPerformed) return cleanupFailures;
		cleanupPerformed = true;
		for (const QString& path : outputPaths) {
			if (!preExistingOutputs.contains(path)) {
				if (QFile::exists(path) && !QFile::remove(path)) {
					cleanupFailures.append(path);
				}
				QString jpgPath = QFileInfo(path).path() + "/" + QFileInfo(path).completeBaseName() + ".jpg";
				if (QFile::exists(jpgPath) && !QFile::remove(jpgPath)) {
					cleanupFailures.append(jpgPath);
				}
			}
			// Full-burst files are worker-owned scratch artifacts even if the final
			// output path existed before this legacy non-staging run.
			const QString fullBurstPath = path + QStringLiteral(".fullburst");
			if (QFile::exists(fullBurstPath) && !QFile::remove(fullBurstPath)) {
				cleanupFailures.append(fullBurstPath);
			}
		}
		QDir outputDir(savePath + "/" + dstNode);
		if (!outputDirExisted && outputDir.exists() &&
			outputDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
			if (!QDir(savePath).rmdir(dstNode)) {
				cleanupFailures.append(outputDir.absolutePath());
			}
		}
		return cleanupFailures;
	}

	void dismiss() {
		dismissed = true;
	}

	void resetForFreshOutput() {
		preExistingOutputs.clear();
		outputDirExisted = false;
	}

	QStringList outputPaths;
	QSet<QString> preExistingOutputs;
	QString savePath;
	QString dstNode;
	bool outputDirExisted;
	bool dismissed = false;
	bool cleanupPerformed = false;
	QStringList cleanupFailures;
};

S1TopsBackGeocodingWorker::S1TopsBackGeocodingWorker(QObject* parent)
    : BaseWorker(parent)
{
}

S1TopsBackGeocodingWorker::~S1TopsBackGeocodingWorker()
{
}

void __stdcall S1TopsBackGeocodingWorker::onNativeDiagnostic(const InSARDiagnosticEvent* event, void* userData) noexcept
{
    try {
        if (event && userData) {
            static_cast<S1TopsBackGeocodingWorker*>(userData)->appendNativeDiagnostic(event);
        }
    } catch (...) {
        // Native callbacks must never propagate exceptions across the DLL boundary.
    }
}

void S1TopsBackGeocodingWorker::appendNativeDiagnostic(const InSARDiagnosticEvent* event) noexcept
{
    const QString phase = nativeText(event->phase);
    if (phase == QStringLiteral("progress.update") && event->statusCode >= 0 && event->statusCode <= 100) {
        Q_EMIT updateProcess(event->statusCode, nativeText(event->message));
        return;
    }

    InSARLogManager::LogLevel level = InSARLogManager::LevelDebug;
    if (event->severity == INSAR_DIAGNOSTIC_WARNING) level = InSARLogManager::LevelWarning;
    else if (event->severity == INSAR_DIAGNOSTIC_ERROR) level = InSARLogManager::LevelError;
    else if (event->severity == INSAR_DIAGNOSTIC_INFO) level = InSARLogManager::LevelInfo;

    if (level == InSARLogManager::LevelDebug &&
        (phase == QStringLiteral("sinc.start") || phase == QStringLiteral("amplitude_matching.sample"))) {
        return;
    }

    QString message = nativeText(event->message);
    const QString detail = nativeText(event->detail);
    const QString h5File = nativeText(event->h5File);
    const QString dataset = nativeText(event->dataset);
    if (!detail.isEmpty()) message += QStringLiteral("; %1").arg(detail);
    if (!h5File.isEmpty()) message += QStringLiteral(" [h5=%1]").arg(h5File);
    if (!dataset.isEmpty()) message += QStringLiteral(" [dataset=%1]").arg(dataset);

    if (event->severity == INSAR_DIAGNOSTIC_ERROR &&
        (phase == QStringLiteral("burst_mapping.preflight") ||
         phase == QStringLiteral("burst_alignment.preflight") ||
         phase == QStringLiteral("refinement.burst_offset_fallback"))) {
        m_lastNativeErrorMessage = message;
    }
    if (event->severity == INSAR_DIAGNOSTIC_WARNING &&
        (phase == QStringLiteral("burst_mapping.partial_coverage") ||
         phase == QStringLiteral("range.partial_coverage_skipped") ||
         phase == QStringLiteral("esd.partial_coverage_skipped"))) {
        m_nativeQualityWarnings.append(message);
    }

    LogTargets targets = LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile;
    if (level == InSARLogManager::LevelError || level == InSARLogManager::LevelWarning) {
        targets |= LogTarget::UserProjectLog;
    }
    InSARLogManager::LogTaskEvent(m_taskLogContext, level, "Sentinel1BackGeocoding", message,
                                  targets, phase, QString(), event->elapsedMs);
}

void S1TopsBackGeocodingWorker::prepareForStart()
{
	m_stopRequested.store(false, std::memory_order_release);
	m_lastNativeErrorMessage.clear();
	m_nativeQualityWarnings.clear();
	std::lock_guard<std::mutex> locker(m_backGeocodingMutex);
	m_backGeocoding.reset();
}

std::shared_ptr<Sentinel1BackGeocoding> S1TopsBackGeocodingWorker::activeBackGeocoding() const
{
	std::lock_guard<std::mutex> locker(m_backGeocodingMutex);
	return m_backGeocoding;
}

void S1TopsBackGeocodingWorker::requestCancel() noexcept
{
	m_stopRequested.store(true, std::memory_order_release);
	std::shared_ptr<Sentinel1BackGeocoding> backGeocoding = activeBackGeocoding();
	if (backGeocoding) {
		backGeocoding->requestCancel();
	}
}

void S1TopsBackGeocodingWorker::S1_TOPS_BackGeocoding(
	int masterIndex,
	QString savePath,
	QString dstProject,
	QString dstNode,
	QStringList inputPaths,
	bool b_ESD
)
{
	ScopedTaskLogContext taskLogContextGuard(m_taskLogContext);
	const int images_number = inputPaths.size();
	if (images_number < 2 ||
		masterIndex < 1 ||
		masterIndex > images_number ||
		savePath.isEmpty() ||
		dstNode.isEmpty() ||
		inputPaths.isEmpty()
		)
	{
		emit errorProcess("Invalid parameters for BackGeocoding.");
		return;
	}
	int ret;
	std::vector<std::string> SAR_images;
	std::vector<std::string> SAR_images_regis;
	QList<QString> origin;
	QString demPath = m_demPath;
	for (const QString& inputPath : inputPaths) {
		QFileInfo fileInfo(inputPath);
		if (!fileInfo.exists() || fileInfo.baseName().isEmpty()) {
			emit errorProcess(QStringLiteral("Invalid Sentinel-1 input path: %1").arg(inputPath));
			return;
		}
		const QString originName = fileInfo.baseName();
		origin.append(originName);
		SAR_images.push_back(fileInfo.absoluteFilePath().toStdString());
		SAR_images_regis.push_back(QDir(savePath).filePath(dstNode + "/" + originName + "_regis.h5").toStdString());
	}

    Sentinel1ProductIdentity referenceIdentity;
    for (int index = 0; index < inputPaths.size(); ++index) {
        Sentinel1ProductIdentity identity;
        QString identityError;
        if (!readSentinel1ProductIdentity(inputPaths.at(index), identity, identityError)) {
            emit errorProcess(identityError);
            return;
        }
        QString geometryContractError;
        if (!NodeUtils::validateSentinelGeometryContract(inputPaths.at(index), &geometryContractError)) {
            emit errorProcess(geometryContractError);
            return;
        }
        if (index == 0) {
            referenceIdentity = identity;
        } else if (identity.swath != referenceIdentity.swath ||
                   identity.polarization != referenceIdentity.polarization) {
            emit errorProcess(QStringLiteral("Sentinel-1 inputs must use the same swath and polarization: %1 is %2/%3, expected %4/%5.")
                .arg(inputPaths.at(index), identity.swath, identity.polarization,
                     referenceIdentity.swath, referenceIdentity.polarization));
            return;
        }
        InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
            QStringLiteral("Validated Sentinel-1 input identity: image=%1, swath=%2, polarization=%3")
                .arg(index + 1).arg(identity.swath, identity.polarization),
            "coregistration.input_identity");
    }

		// 如果外部未传入DEM路径，在 GUI 线程安全地查询项目全局默认高程数据路径
	QStringList outputPaths;
	QSet<QString> preExistingOutputs;
	for (const std::string& outputPath : SAR_images_regis) {
		const QString path = QString::fromStdString(outputPath);
		outputPaths.append(path);
		if (QFileInfo::exists(path)) {
			preExistingOutputs.insert(path);
		}
	}
	QDir dir(savePath);
	const bool outputDirExisted = dir.exists(dstNode);
	BackGeocodingOutputCleanupGuard cleanupGuard(outputPaths, preExistingOutputs, savePath, dstNode, outputDirExisted);
	if (!outputDirExisted && !dir.mkdir(dstNode)) {
		emit errorProcess("Failed to create directory: " + dstNode);
		return;
	}
	std::shared_ptr<Sentinel1BackGeocoding> backGeocodingPtr = std::make_shared<Sentinel1BackGeocoding>();
	{
		std::lock_guard<std::mutex> locker(m_backGeocodingMutex);
		m_backGeocoding = backGeocodingPtr;
	}
	backGeocodingPtr->clearCancelRequest();
	if (m_stopRequested.load(std::memory_order_acquire)) {
		backGeocodingPtr->requestCancel();
	}
	Sentinel1BackGeocoding& backgeocoding = *backGeocodingPtr;
	backgeocoding.setDiagnosticCallback(&S1TopsBackGeocodingWorker::onNativeDiagnostic, this);
	struct DiagnosticCallbackResetGuard {
		Sentinel1BackGeocoding& instance;
		~DiagnosticCallbackResetGuard() { instance.setDiagnosticCallback(nullptr, nullptr); }
	} diagnosticCallbackResetGuard{ backgeocoding };
	const auto cancellationRequested = [this, &backgeocoding]() {
		return m_stopRequested.load(std::memory_order_acquire) || backgeocoding.isCancelRequested();
	};
	const auto finishCancelled = [this, &cleanupGuard]() {
		QStringList cleanupFailures = cleanupGuard.cleanup();
		if (!cleanupFailures.isEmpty()) {
			InSARLogManager::LogWarning("S1TopsBackGeocodingWorker",
				QStringLiteral("任务已取消，但部分临时输出未能清理：%1").arg(cleanupFailures.join(", ")));
		}
		Q_EMIT cancelled(cleanupFailures);
	};
	if (cancellationRequested()) {
		finishCancelled();
		return;
	}
    emit updateProcess(10, QStringLiteral("Starting Sentinel-1 back-geocoding..."));
	InSARLogManager::LogDebug("S1TopsBackGeocodingWorker", QStringLiteral("Worker execution started: images=%1, master=%2, esd=%3, rangeRefinement=%4")
		.arg(images_number).arg(masterIndex).arg(b_ESD ? QStringLiteral("启用") : QStringLiteral("关闭"))
		.arg(m_bRangeRefine ? QStringLiteral("启用") : QStringLiteral("关闭")), "worker.started");
	for (size_t i = 0; i < SAR_images.size(); ++i) {
		InSARLogManager::LogDebug("S1TopsBackGeocodingWorker", QString("Input image=%1, source=%2, output=%3")
			.arg(i + 1).arg(QString::fromStdString(SAR_images[i])).arg(QString::fromStdString(SAR_images_regis[i])),
			"coregistration.input");
	}

	// Keep environment details out of project.log while retaining them for diagnosis.
	cv::Mat test_orbit;
	bool hasPreciseOrbit = false;
	{
		NodeUtils::Hdf5Locker locker(SAR_images[0]);
		FormatConversion temp_conv;
		hasPreciseOrbit = (temp_conv.read_array_from_h5(SAR_images[0].c_str(), "fine_state_vec", test_orbit) == 0);
	}
	InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
		QString("Coregistration environment: preciseOrbit=%1, esd=%2, rangeRefinement=%3, interpolator=sinc(kernelRadius=4)")
			.arg(hasPreciseOrbit ? "loaded" : "unavailable")
			.arg(b_ESD ? "enabled" : "disabled")
			.arg(m_bRangeRefine ? "enabled" : "disabled"),
		"coregistration.environment");

	if (demPath.isEmpty() || !QFileInfo(demPath).isFile()) {
		emit errorProcess(QStringLiteral("Back-geocoding requires a resolved Auxiliary DEM file."));
		return;
	}
	//后向地理编码配准
	std::string tmpDem = demPath.toStdString();
	std::replace(tmpDem.begin(), tmpDem.end(), '/', '\\');
	std::vector<SentinelRefinementImageResult> refinementImages(images_number - 1);
	SentinelRefinementOptions refinementOptions = {};
	refinementOptions.version = SENTINEL_REFINEMENT_OPTIONS_VERSION;
	refinementOptions.structSize = sizeof(refinementOptions);
	refinementOptions.enableEsd = b_ESD ? 1 : 0;
	refinementOptions.esdRangeMultilook = 16;
	refinementOptions.esdAzimuthMultilook = 4;
	refinementOptions.esdCoherenceThreshold = 0.4;
	refinementOptions.esdHistogramBinSize = 0.1;
	refinementOptions.esdDopplerRateHz = 4500.0;
	refinementOptions.esdAzimuthBandwidthHz = 486.0;
	refinementOptions.rangeSamplePointCount = 5;
	refinementOptions.rangeTemplateSize = 200;
	refinementOptions.rangeSearchSize = 206;
	refinementOptions.rangeOffsetMode = m_bRangeRefine ? SENTINEL_RANGE_OFFSET_ESTIMATE : SENTINEL_RANGE_OFFSET_NONE;
	SentinelRefinementResult refinementResult = {};
	refinementResult.version = SENTINEL_REFINEMENT_RESULT_VERSION;
	refinementResult.structSize = sizeof(refinementResult);
	refinementResult.images = refinementImages.data();
	refinementResult.imageCapacity = static_cast<int>(refinementImages.size());
	ret = backgeocoding.init(SAR_images, SAR_images_regis, tmpDem.c_str(), masterIndex,
		&S1TopsBackGeocodingWorker::onNativeDiagnostic, this);
	if (ret != 0) {
		SentinelRefinementTransactionStatus transactionStatus = {};
		transactionStatus.version = SENTINEL_REFINEMENT_TRANSACTION_STATUS_VERSION;
		transactionStatus.structSize = sizeof(transactionStatus);
		if (backgeocoding.getPostRegistrationRefinementTransactionStatus(transactionStatus) == 0 &&
			(transactionStatus.state == SENTINEL_REFINEMENT_TRANSACTION_IN_PROGRESS ||
				transactionStatus.state == SENTINEL_REFINEMENT_TRANSACTION_FAILED)) {
			InSARLogManager::LogWarning("S1TopsBackGeocodingWorker",
				QString("Refinement transaction remains isolated in staging: state=%1, outputs=%2, verified=%3.")
					.arg(transactionStatus.state).arg(transactionStatus.outputCount).arg(transactionStatus.verifiedOutputCount));
			emit errorProcess(QStringLiteral("Sentinel-1 refinement transaction did not terminate in staging."));
			return;
		}
	}
	if (ret != 0) { emit errorProcess(QStringLiteral("Failed to initialize Sentinel-1 registration: %1").arg(ret)); return; }
	ret = backgeocoding.backGeoCodingCoregistration(&refinementOptions, &refinementResult,
		&S1TopsBackGeocodingWorker::onNativeDiagnostic, this);
	if (ret == -2 || cancellationRequested()) { finishCancelled(); return; }
	if (ret != 0) {
		if (ret == SENTINEL_BACK_GEOCODING_BURST_MAPPING_ERROR ||
			ret == SENTINEL_BACK_GEOCODING_BURST_OFFSET_FALLBACK_ERROR) {
			const QString diagnostic = m_lastNativeErrorMessage.trimmed();
			const QString failureKind = ret == SENTINEL_BACK_GEOCODING_BURST_MAPPING_ERROR
				? QStringLiteral("burst mapping preflight")
				: QStringLiteral("burst alignment preflight");
			emit errorProcess(diagnostic.isEmpty()
				? QStringLiteral("Sentinel-1 %1 failed (error %2): burst alignment must be estimated before DEM projection.").arg(failureKind).arg(ret)
				: QStringLiteral("Sentinel-1 %1 failed: %2").arg(failureKind, diagnostic));
		} else {
			emit errorProcess(QStringLiteral("Sentinel-1 registration transaction failed: %1").arg(ret));
		}
		return;
	}

	const bool hasRangeOffsets = refinementOptions.rangeOffsets != nullptr;
	const bool expectsCoreOnlyBaseline = refinementOptions.enableEsd == 0 &&
		refinementOptions.rangeOffsetMode == SENTINEL_RANGE_OFFSET_NONE &&
		!hasRangeOffsets && refinementOptions.rangeOffsetCount == 0;
	QString executionPathName;
	switch (refinementResult.executionPath) {
	case SENTINEL_EXECUTION_PATH_CORE_ONLY_BASELINE:
		executionPathName = QStringLiteral("core_only_baseline");
		break;
	case SENTINEL_EXECUTION_PATH_POST_REGISTRATION_REFINEMENT:
		executionPathName = QStringLiteral("post_registration_refinement");
		break;
	default:
		executionPathName = QStringLiteral("unspecified_or_unknown");
		break;
	}
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker",
		QString("Refinement execution: path=%1 (%2), enableEsd=%3, rangeOffsetMode=%4, hasRangeOffsets=%5, rangeOffsetCount=%6.")
			.arg(executionPathName).arg(refinementResult.executionPath).arg(refinementOptions.enableEsd)
			.arg(refinementOptions.rangeOffsetMode).arg(hasRangeOffsets ? "true" : "false").arg(refinementOptions.rangeOffsetCount));
	const int expectedExecutionPath = expectsCoreOnlyBaseline ?
		SENTINEL_EXECUTION_PATH_CORE_ONLY_BASELINE : SENTINEL_EXECUTION_PATH_POST_REGISTRATION_REFINEMENT;
	if (refinementResult.executionPath != expectedExecutionPath) {
		emit errorProcess(QStringLiteral("Sentinel-1 refinement returned an unexpected execution path: %1 (expected %2).")
			.arg(refinementResult.executionPath).arg(expectedExecutionPath));
		return;
	}

	bool hasRefinementWarning = false;
	QStringList refinementWarnings;
	for (int i = 0; i < refinementResult.imageCount; ++i) {
		const SentinelRefinementImageResult& image = refinementResult.images[i];
		InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
			QString("Refinement result: image=%1, offset_a=%2, offset_r=%3, esdQuality=%4, rangeQuality=%5, warningFlags=%6")
				.arg(image.imageIndex).arg(image.esdAzimuthOffset, 0, 'g', 12).arg(image.rangeOffset, 0, 'g', 12)
				.arg(image.esdQualityCode).arg(image.rangeQualityCode).arg(image.warningFlags),
			"refinement.result");

		const bool esdWarning = b_ESD && image.esdQualityCode >= 3;
		const bool rangeWarning = m_bRangeRefine && image.rangeQualityCode >= 3;
		if (esdWarning || rangeWarning) {
			hasRefinementWarning = true;
            refinementWarnings.append(QStringLiteral("Slave image %1 refinement reported a quality warning.").arg(image.imageIndex));
		}
	}

#if 0
	FormatConversion conversion;
	ComplexMat slaveSLC, tmp;
	Utils util;

	{
		NodeUtils::Hdf5Locker locker;
		ret = backgeocoding.loadData(SAR_images);
	}
	if (ret < 0) {
		emit errorProcess("Failed to load Sentinel-1 images metadata.");
		return;
	}
    emit updateProcess(11, QStringLiteral("Sentinel-1 metadata loaded..."));
	ret = backgeocoding.setDEMPath(tmpDem.c_str());
	if (ret < 0) {
		emit errorProcess("Failed to set DEM path.");
		return;
	}
	ret = backgeocoding.loadOutFiles(SAR_images_regis);
	if (ret < 0) {
		emit errorProcess("Failed to load registration output paths.");
		return;
	}
	ret = backgeocoding.setMasterIndex(masterIndex);
	if (ret < 0) {
		emit errorProcess("Failed to set master image index.");
		return;
	}
	if (backgeocoding.numOfImages < 2) {
		emit errorProcess("Number of loaded images is less than 2.");
		return;
	}
	{
		NodeUtils::Hdf5Locker locker(backgeocoding.su[masterIndex - 1]->h5File);
		ret = conversion.read_slc_from_h5(backgeocoding.su[masterIndex - 1]->h5File.c_str(), tmp);
	}
	if (cancellationRequested()) {
		finishCancelled();
		return;
	}
	if (ret < 0) {
		emit errorProcess("Failed to read master SLC from H5.");
		return;
	}
    emit updateProcess(12, QStringLiteral("Master SLC loaded; initializing registration workspace..."));
	tmp.convertTo(tmp, CV_32F);
	{
		NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[masterIndex - 1]);
		ret = conversion.creat_new_h5(backgeocoding.outFiles[masterIndex - 1].c_str());
		if (ret >= 0) {
			ret = conversion.write_int_to_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s1_tops_back_geocoding_complete", 0);
		}
		if (ret >= 0) {
			ret = conversion.write_slc_to_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), tmp);
		}
	}
	if (cancellationRequested()) {
		finishCancelled();
		return;
	}
	if (ret < 0) {
		emit errorProcess("Failed to create or write master registration H5 file.");
		return;
	}
    emit updateProcess(14, QStringLiteral("Master registration H5 initialized..."));
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Successfully loaded Master SLC data and set DEM path.");

	// 从影像不再预写入整个�?0 的大矩阵，改为调�?create_empty_dataset 延迟分配物理磁盘空间
	for (int i = 0; i < backgeocoding.numOfImages; i++)
	{
		if (i == masterIndex - 1) continue;
        emit updateProcess(14 + i, QStringLiteral("Initializing slave image %1/%2 H5 workspace...").arg(i + 1).arg(backgeocoding.numOfImages));
		{
			NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[i]);
			ret = conversion.creat_new_h5(backgeocoding.outFiles[i].c_str());
			if (ret >= 0) {
				ret = conversion.write_int_to_h5(backgeocoding.outFiles[i].c_str(), "s1_tops_back_geocoding_complete", 0);
			}
			if (ret >= 0) {
				ret = conversion.create_empty_dataset(backgeocoding.outFiles[i].c_str(), "s_re", tmp.GetRows(), tmp.GetCols(), CV_32F);
			}
			if (ret >= 0) {
				ret = conversion.create_empty_dataset(backgeocoding.outFiles[i].c_str(), "s_im", tmp.GetRows(), tmp.GetCols(), CV_32F);
			}
		}
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
		if (ret < 0) {
			emit errorProcess("Failed to create empty datasets in slave registration H5 file: " + QString::fromStdString(backgeocoding.outFiles[i]));
			return;
		}
	}
    emit updateProcess(18, QStringLiteral("Slave H5 workspaces initialized; preparing back projection..."));

	cv::Mat start(backgeocoding.su[masterIndex - 1]->burstCount, 1, CV_32S), end(backgeocoding.su[masterIndex - 1]->burstCount, 1, CV_32S);
	start.at<int>(0, 0) = 1;
	end.at<int>(0, 0) = backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(0, 0);
	double lastValidTime = backgeocoding.su[masterIndex - 1]->burstAzimuthTime.at<double>(0, 0) +
		(backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(0, 0) - 1) * backgeocoding.su[masterIndex - 1]->azimuthTimeInterval;
	double firstValidTime;
	int overlap;
	cv::Mat overlapMat = cv::Mat::zeros(backgeocoding.su[masterIndex - 1]->burstCount - 1, 1, CV_32S);
	for (int i = 1; i < backgeocoding.su[masterIndex - 1]->burstCount; i++)
	{
		firstValidTime = backgeocoding.su[masterIndex - 1]->burstAzimuthTime.at<double>(i, 0) + (backgeocoding.su[masterIndex - 1]->firstValidLine.at<int>(i, 0) - 1) *
			backgeocoding.su[masterIndex - 1]->azimuthTimeInterval;

		overlap = round((lastValidTime - firstValidTime) / backgeocoding.su[masterIndex - 1]->azimuthTimeInterval + 1);
		overlapMat.at<int>(i - 1, 0) = overlap;
		end.at<int>(i - 1, 0) = end.at<int>(i - 1, 0) - int(overlap / 2);

		start.at<int>(i, 0) = backgeocoding.su[masterIndex - 1]->linesPerBurst * i + backgeocoding.su[masterIndex - 1]->firstValidLine.at<int>(i, 0) + overlap - int(overlap / 2);

		end.at<int>(i, 0) = backgeocoding.su[masterIndex - 1]->linesPerBurst * i + backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(i, 0);

		lastValidTime = backgeocoding.su[masterIndex - 1]->burstAzimuthTime.at<double>(i, 0) +
			(backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(i, 0) - 1) * backgeocoding.su[masterIndex - 1]->azimuthTimeInterval;
	}
	end.at<int>(backgeocoding.su[masterIndex - 1]->burstCount - 1, 0) = backgeocoding.su[masterIndex - 1]->linesPerBurst * backgeocoding.su[masterIndex - 1]->burstCount;
	start -= 1;
	start.copyTo(backgeocoding.start);
	end.copyTo(backgeocoding.end);
	backgeocoding.isdeBurstConfig = true;

	int linesPerBurst = backgeocoding.su[masterIndex - 1]->linesPerBurst;
	int samplesPerBurst = backgeocoding.su[masterIndex - 1]->samplesPerBurst;
	int offset_row = 0;
	double lonMin, lonMax, latMin, latMax;
	int burstCount = backgeocoding.su[masterIndex - 1]->burstCount;
	for (int i = 0; i < burstCount; i++)
	{
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
		InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Registration: processing burst %1/%2...").arg(i + 1).arg(burstCount));
		ret = backgeocoding.su[masterIndex - 1]->computeImageGeoBoundry(&lonMin, &lonMax, &latMin, &latMax, i + 1);
		if (ret < 0) {
			emit errorProcess("Failed to compute master image geo boundary.");
			return;
		}
		ret = backgeocoding.loadDEM(backgeocoding.DEMPath.c_str(), lonMin, lonMax, latMin, latMax);
		if (ret < 0) {
			emit errorProcess("Failed to load DEM.");
			return;
		}
		
		for (int j = 0; j < backgeocoding.numOfImages; j++)
		{
			if (cancellationRequested()) {
				finishCancelled();
				return;
			}
			if (j == masterIndex - 1) continue;
			if (!backgeocoding.burstOffsetComputed)
			{
				ret = backgeocoding.computeBurstOffset();
				if (ret == -2 || cancellationRequested()) {
					finishCancelled();
					return;
				}
				if (ret < 0) {
					emit errorProcess("Failed to compute burst offset.");
					return;
				}
			}
			int mBurstIndex = i + 1; int slaveImageIndex = j + 1;
			int sBurstIndex = mBurstIndex + backgeocoding.su[slaveImageIndex - 1]->burstOffset;
			if (sBurstIndex < 1 || sBurstIndex > backgeocoding.su[slaveImageIndex - 1]->burstCount) {
				continue;
			}
			double a0Rg = 0.0, a1Rg = 0.0, a2Rg = 0.0, a0Az = 0.0, a1Az = 0.0, a2Az = 0.0;
			cv::Mat coef(1, 6, CV_64F);
			ret = backgeocoding.su[slaveImageIndex - 1]->getBurst(sBurstIndex, slaveSLC);
			if (ret < 0) {
				emit errorProcess(QStringLiteral("Failed to read slave burst %1 for image %2.").arg(sBurstIndex).arg(slaveImageIndex));
				return;
			}
			if (slaveSLC.type() != CV_64F) slaveSLC.convertTo(slaveSLC, CV_64F);
			cv::Mat derampDemodPhase;
			ret = backgeocoding.su[slaveImageIndex - 1]->computeDerampDemodPhase(sBurstIndex, derampDemodPhase);
			if (ret < 0) {
				emit errorProcess(QStringLiteral("Failed to compute deramp phase for slave burst %1, image %2.").arg(sBurstIndex).arg(slaveImageIndex));
				return;
			}
			ret = backgeocoding.performDerampDemod(derampDemodPhase, slaveSLC);
			if (ret < 0) {
				emit errorProcess(QStringLiteral("Failed to perform deramp demodulation for slave burst %1, image %2.").arg(sBurstIndex).arg(slaveImageIndex));
				return;
			}
			ret = backgeocoding.computeSlavePosition(slaveImageIndex, mBurstIndex);
			if (ret == -2 || cancellationRequested()) {
				finishCancelled();
				return;
			}
			if (ret < 0) {
				emit errorProcess(QStringLiteral("Failed to compute slave position for master burst %1, image %2.").arg(mBurstIndex).arg(slaveImageIndex));
				return;
			}
			cv::Mat slaveAzimuthOffset, slaveRangeOffset;
			ret = backgeocoding.computeSlaveOffset(slaveAzimuthOffset, slaveRangeOffset);
			if (ret == -2 || cancellationRequested()) {
				finishCancelled();
				return;
			}
			if (ret < 0) {
				emit errorProcess(QStringLiteral("Failed to compute slave offset for master burst %1, image %2.").arg(mBurstIndex).arg(slaveImageIndex));
				return;
			}
			
			int fitRetAz = backgeocoding.fitSlaveOffset(slaveAzimuthOffset, &a0Az, &a1Az, &a2Az);
			if (fitRetAz == -2 || cancellationRequested()) {
				finishCancelled();
				return;
			}
			if (fitRetAz < 0) {
				InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", QString("Failed to fit slave azimuth offset for burst %1, slave %2. Using default 0.0.").arg(mBurstIndex).arg(slaveImageIndex));
				a0Az = 0.0; a1Az = 0.0; a2Az = 0.0;
			}
			int fitRetRg = backgeocoding.fitSlaveOffset(slaveRangeOffset, &a0Rg, &a1Rg, &a2Rg);
			if (fitRetRg == -2 || cancellationRequested()) {
				finishCancelled();
				return;
			}
			if (fitRetRg < 0) {
				InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", QString("Failed to fit slave range offset for burst %1, slave %2. Using default 0.0.").arg(mBurstIndex).arg(slaveImageIndex));
				a0Rg = 0.0; a1Rg = 0.0; a2Rg = 0.0;
			}

			coef.at<double>(0) = a0Rg;
			coef.at<double>(1) = a1Rg;
			coef.at<double>(2) = a2Rg;
			coef.at<double>(3) = a0Az;
			coef.at<double>(4) = a1Az;
			coef.at<double>(5) = a2Az;
			ret = backgeocoding.performSincResampling(slaveSLC, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
				a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
			if (ret == -2 || cancellationRequested()) {
				finishCancelled();
				return;
			}
			if (ret < 0) {
				emit errorProcess(QStringLiteral("Failed to resample slave burst %1 for image %2.").arg(sBurstIndex).arg(slaveImageIndex));
				return;
			}
			tmp.SetRe(derampDemodPhase); tmp.SetIm(derampDemodPhase);
			ret = backgeocoding.performSincResampling(tmp, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
				a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
			if (ret == -2 || cancellationRequested()) {
				finishCancelled();
				return;
			}
			if (ret < 0) {
				emit errorProcess(QStringLiteral("Failed to resample deramp phase for slave burst %1, image %2.").arg(sBurstIndex).arg(slaveImageIndex));
				return;
			}
			tmp.re.copyTo(derampDemodPhase);
			util.phase2cos(derampDemodPhase, tmp.re, tmp.im);
			slaveSLC.Mul(tmp, slaveSLC, true);//reramp
			slaveSLC.convertTo(slaveSLC, CV_32F);
			char str[256];
			sprintf(str, "burst_%d_coef", i + 1);
			{
				NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
				conversion.write_array_to_h5(backgeocoding.outFiles[j].c_str(), str, coef);
				ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_re", slaveSLC.re,
					offset_row, 0, linesPerBurst, samplesPerBurst);
				if (ret >= 0) {
					ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_im", slaveSLC.im,
						offset_row, 0, linesPerBurst, samplesPerBurst);
				}
			}
			if (cancellationRequested()) {
				finishCancelled();
				return;
			}
			if (ret < 0) {
				emit errorProcess("Failed to write slave SLC real or imag part to H5.");
				return;
			}
		}
		offset_row += linesPerBurst;
		backgeocoding.isMasterRgAzComputed = false;
		if (cancellationRequested())
		{
			finishCancelled();
			return;
		}
        emit updateProcess(10.0 + 50.0 / burstCount * (i + 1), QStringLiteral("Back-geocoding registration..."));
	}

	if (b_ESD || m_bRangeRefine)
	{
		std::vector<SentinelRefinementImageResult> refinementImages(backgeocoding.numOfImages - 1);
		SentinelRefinementOptions options = {};
		options.version = SENTINEL_REFINEMENT_OPTIONS_VERSION;
		options.structSize = sizeof(options);
		options.enableEsd = b_ESD ? 1 : 0;
		options.esdRangeMultilook = 16;
		options.esdAzimuthMultilook = 4;
		options.esdCoherenceThreshold = 0.4;
		options.esdHistogramBinSize = 0.1;
		options.esdDopplerRateHz = 4500.0;
		options.esdAzimuthBandwidthHz = 486.0;
		options.rangeSamplePointCount = 5;
		options.rangeTemplateSize = 200;
		options.rangeSearchSize = 206;
		options.rangeOffsetMode = m_bRangeRefine ? SENTINEL_RANGE_OFFSET_ESTIMATE : SENTINEL_RANGE_OFFSET_NONE;
		options.rangeOffsets = nullptr;
		options.rangeOffsetCount = 0;
		options.transactionDirectory = nullptr;

		SentinelRefinementResult refinementResult = {};
		refinementResult.version = SENTINEL_REFINEMENT_RESULT_VERSION;
		refinementResult.structSize = sizeof(refinementResult);
		refinementResult.images = refinementImages.data();
		refinementResult.imageCapacity = static_cast<int>(refinementImages.size());
		ret = backgeocoding.applyPostRegistrationRefinement(options, refinementResult,
			&S1TopsBackGeocodingWorker::onNativeDiagnostic, this);
		if (ret == -2 || cancellationRequested()) { finishCancelled(); return; }
		if (ret != 0) {
			emit errorProcess(QStringLiteral("Post-registration refinement transaction failed: %1").arg(ret));
			return;
		}
		for (int i = 0; i < refinementResult.imageCount; ++i) {
			const SentinelRefinementImageResult& image = refinementResult.images[i];
			InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
				QString("Refinement image=%1, offset_a=%2, offset_r=%3, esdQuality=%4, rangeQuality=%5, warnings=%6")
					.arg(image.imageIndex).arg(image.esdAzimuthOffset).arg(image.rangeOffset)
					.arg(image.esdQualityCode).arg(image.rangeQualityCode).arg(image.warningFlags), "refinement.result");
		}
	}

	// Legacy UI-side refinement retained only in source history; the active
	// implementation is the DLL transaction above.
#if 0
	if (b_ESD || m_bRangeRefine)
	{
		if (b_ESD)
		{
			InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Starting Enhanced Spectral Diversity (ESD) correction...");
			cv::Mat overlap_phase(cv::sum(overlapMat)[0], samplesPerBurst, CV_64F);
			cv::Mat phase; int count_sum = 0;
			ComplexMat overlap_master_up, overlap_slave_up, overlap_master_down, overlap_slave_down;
			for (int j = 0; j < backgeocoding.numOfImages; j++)
			{
				if (cancellationRequested()) {
					finishCancelled();
					return;
				}
				if (j == masterIndex - 1) continue;
				for (int i = 1; i < burstCount; i++)
				{
					if (cancellationRequested()) {
						finishCancelled();
						return;
					}
					int offset_col = 0;
					offset_row = (i - 1) * linesPerBurst + backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(i - 1, 0) - overlapMat.at<int>(i - 1, 0);
					{
						NodeUtils::Hdf5Locker locker_master(backgeocoding.outFiles[masterIndex - 1]);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_master_up.re);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_master_up.im);
					}

					{
						NodeUtils::Hdf5Locker locker_slave(backgeocoding.outFiles[j]);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_slave_up.re);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_slave_up.im);
					}

					offset_row = linesPerBurst * i + backgeocoding.su[masterIndex - 1]->firstValidLine.at<int>(i, 0) - 1;

					{
						NodeUtils::Hdf5Locker locker_master(backgeocoding.outFiles[masterIndex - 1]);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_master_down.re);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_master_down.im);
					}

					{
						NodeUtils::Hdf5Locker locker_slave(backgeocoding.outFiles[j]);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_slave_down.re);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_slave_down.im);
					}

					overlap_master_up.convertTo(overlap_master_up, CV_64F);
					overlap_master_down.convertTo(overlap_master_down, CV_64F);
					overlap_slave_up.convertTo(overlap_slave_up, CV_64F);
					overlap_slave_down.convertTo(overlap_slave_down, CV_64F);

					overlap_master_up.Mul(overlap_slave_up, overlap_master_up, true);
					overlap_master_down.Mul(overlap_slave_down, overlap_master_down, true);

					overlap_master_up.Mul(overlap_master_down, overlap_master_up, true);

					phase = overlap_master_up.GetPhase();
					overlap = overlapMat.at<int>(i - 1);
					phase.copyTo(overlap_phase(cv::Range(count_sum, count_sum + overlap), cv::Range(0, samplesPerBurst)));
					count_sum += overlap;
				}
				count_sum = 0;
				cv::Mat phase0, coh;
				util.multilook(overlap_phase, phase0, 16, 4);

				util.phase_axial_concentration(phase0, coh);
				for (int mm = 0; mm < coh.rows; mm++)
				{
					for (int nn = 0; nn < coh.cols; nn++)
					{
						if (coh.at<double>(mm, nn) < 0.4) phase0.at<double>(mm, nn) = 0.0;
					}
				}
				
				cv::Point p;
				double t1, t2;
				cv::Mat output, out_x;
				phase0 = phase0.reshape(0, 1);
				util.hist(phase0, -PI, PI, 0.1, out_x, output);
				if (output.type() != CV_64F) output.convertTo(output, CV_64F);
				if (out_x.type() != CV_64F) out_x.convertTo(out_x, CV_64F);

				//拉格朗日插�?
				double x0, x1, x2, x3, y0, y1, y2, y3, x; x = 31;
				x0 = 29; x1 = 30; x2 = 32; x3 = 33;
				if (output.total() > 33) {
					y0 = output.at<double>(29); y1 = output.at<double>(30); y2 = output.at<double>(32); y3 = output.at<double>(33);
					output.at<double>(31) = (x - x1) * (x - x2) * (x - x3) / ((x0 - x1) * (x0 - x2) * (x0 - x3)) * y0 +
						(x - x0) * (x - x2) * (x - x3) / ((x1 - x0) * (x1 - x2) * (x1 - x3)) * y1 +
						(x - x0) * (x - x1) * (x - x3) / ((x2 - x0) * (x2 - x1) * (x2 - x3)) * y2 +
						(x - x0) * (x - x1) * (x - x2) / ((x3 - x0) * (x3 - x1) * (x3 - x2)) * y3;
				}
				cv::minMaxLoc(output, &t1, &t2, NULL, &p);
				double offset = 0.0;
				if (p.x >= 0 && p.x < out_x.total()) {
					offset = out_x.at<double>(p.x);
				}
				double offset_a = offset / (2 * 3.1415926535 * 4500) * 486;
				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					conversion.write_double_to_h5(backgeocoding.outFiles[j].c_str(), "offset_a", offset_a);
				}
				InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Slave image %1 (index %2) ESD azimuth offset calculated: %3")
					.arg(origin[j]).arg(j + 1).arg(offset_a));
			}
		}

		if (m_bRangeRefine)
		{
			InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Starting Range Amplitude Refinement offset estimation...");
			for (int j = 0; j < backgeocoding.numOfImages; j++)
			{
				if (cancellationRequested()) {
					finishCancelled();
					return;
				}
				if (j == masterIndex - 1) continue;

				std::string masterPath = backgeocoding.outFiles[masterIndex - 1];
				std::string slavePath = backgeocoding.outFiles[j];

				Point2D pts[5];
				int detectRet = DetectAdaptiveSamplingPoints(masterPath.c_str(), pts, 5);
				double offset_r = 0.0;
				if (detectRet >= 0)
				{
					AlignmentResult res[5]{};
					for (int k = 0; k < 5; ++k) {
						res[k].structSize = sizeof(AlignmentResult);
						res[k].heatmap_rgb = nullptr;
						res[k].overlay_rgb = nullptr;
					}
					int calcRet = CalculateOffsetAndCoherenceWithDiagnostics(
						masterPath.c_str(),
						slavePath.c_str(),
						pts, 5, 200, 206, res,
						&S1TopsBackGeocodingWorker::onNativeDiagnostic, this
					);
					if (calcRet == 0)
					{
						double sumOffsetRg = 0.0;
						int validCount = 0;
						std::vector<int> validOffsets;
						for (int k = 0; k < 5; ++k)
						{
							// 1. 物理残差合理性约束：相干系数 >= 0.15 且距离向像素级偏差在 [-1, 1] 之间
							if (res[k].maxCorrelation >= 0.15 && std::abs(res[k].offsetX) <= 1)
							{
								validOffsets.push_back(res[k].offsetX);
								sumOffsetRg += res[k].offsetX;
								validCount++;
							}
						}

						// 2. 多点一致性投票判�?
						if (validCount >= 2)
						{
							// 检查所有有效偏差的最大最小值差值是�?<= 1
							int min_val = *std::min_element(validOffsets.begin(), validOffsets.end());
							int max_val = *std::max_element(validOffsets.begin(), validOffsets.end());
							if (max_val - min_val <= 1)
							{
								offset_r = sumOffsetRg / validCount;
							}
							else
							{
								// 偏差不一致，判定为含噪伪匹配，放弃纠偏，安全回退�?0
								offset_r = 0.0;
								InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", "Range offsets are inconsistent, skipping Range correction.");
							}
						}
						else if (validCount == 1)
						{
							// 单个有效点，需要其相关系数更高（如 >= 0.20）以确保置信�?
							int idx = -1;
							for (int k = 0; k < 5; ++k) {
								if (res[k].maxCorrelation >= 0.15 && std::abs(res[k].offsetX) <= 1) {
									idx = k;
									break;
								}
							}
							if (idx != -1 && res[idx].maxCorrelation >= 0.20)
							{
								offset_r = res[idx].offsetX;
							}
							else
							{
								offset_r = 0.0;
							}
						}
						else
						{
							offset_r = 0.0;
						}
					}
					FreeAlignmentResults(res, 5);
				}

				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					conversion.write_double_to_h5(backgeocoding.outFiles[j].c_str(), "offset_r", offset_r);
				}
				InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Slave image %1 (index %2) Range matching offset calculated: %3")
					.arg(origin[j]).arg(j + 1).arg(offset_r));
				InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
					QString("Range refinement result: slaveImage=%1, offset=%2")
						.arg(j + 1).arg(offset_r, 0, 'g', 12), "range.result");
			}
		}

		offset_row = 0;
		for (int i = 0; i < burstCount; i++)
		{
			if (cancellationRequested()) {
				finishCancelled();
				return;
			}
			for (int j = 0; j < backgeocoding.numOfImages; j++)
			{
				if (cancellationRequested()) {
					finishCancelled();
					return;
				}
				if (j == masterIndex - 1) continue;
				double offset_a = 0.0;
				double offset_r = 0.0;
				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					if (b_ESD)
					{
						conversion.read_double_from_h5(backgeocoding.outFiles[j].c_str(), "offset_a", &offset_a);
					}
					if (m_bRangeRefine)
					{
						conversion.read_double_from_h5(backgeocoding.outFiles[j].c_str(), "offset_r", &offset_r);
					}
				}
				if (fabs(offset_a) < 0.0001 && fabs(offset_r) < 0.01) continue;
				if (!backgeocoding.burstOffsetComputed)
				{
					ret = backgeocoding.computeBurstOffset();
				}
				int mBurstIndex = i + 1; int slaveImageIndex = j + 1;
				int sBurstIndex = mBurstIndex + backgeocoding.su[slaveImageIndex - 1]->burstOffset;
				if (sBurstIndex < 1 || sBurstIndex > backgeocoding.su[slaveImageIndex - 1]->burstCount) {
					continue;
				}
				double a0Rg = 0.0, a1Rg = 0.0, a2Rg = 0.0, a0Az = 0.0, a1Az = 0.0, a2Az = 0.0;
				cv::Mat coef(1, 6, CV_64F);
				ret = backgeocoding.su[slaveImageIndex - 1]->getBurst(sBurstIndex, slaveSLC);
				if (slaveSLC.type() != CV_64F) slaveSLC.convertTo(slaveSLC, CV_64F);
				cv::Mat derampDemodPhase;
				ret = backgeocoding.su[slaveImageIndex - 1]->computeDerampDemodPhase(sBurstIndex, derampDemodPhase);
				ret = backgeocoding.performDerampDemod(derampDemodPhase, slaveSLC);
				char str[256];
				sprintf(str, "burst_%d_coef", i + 1);
				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					conversion.read_array_from_h5(backgeocoding.outFiles[j].c_str(), str, coef);
				}
				
				if (coef.rows == 1 && coef.cols == 6)
				{
					a0Rg = coef.at<double>(0) + offset_r;
					a1Rg = coef.at<double>(1);
					a2Rg = coef.at<double>(2);
					a0Az = coef.at<double>(3) + offset_a;
					a1Az = coef.at<double>(4);
					a2Az = coef.at<double>(5);
				}
				else
				{
					InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", QString("Failed to read valid registration coefficients from H5 for burst %1, slave %2. Using default 0.0.").arg(i + 1).arg(j + 1));
					a0Rg = offset_r;
					a1Rg = 0.0;
					a2Rg = 0.0;
					a0Az = offset_a;
					a1Az = 0.0;
					a2Az = 0.0;
				}
				ret = backgeocoding.performSincResampling(slaveSLC, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
					a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
				if (ret == -2 || cancellationRequested()) {
					finishCancelled();
					return;
				}
				tmp.SetRe(derampDemodPhase); tmp.SetIm(derampDemodPhase);
				ret = backgeocoding.performSincResampling(tmp, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
					a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
				if (ret == -2 || cancellationRequested()) {
					finishCancelled();
					return;
				}
				tmp.re.copyTo(derampDemodPhase);
				util.phase2cos(derampDemodPhase, tmp.re, tmp.im);
				slaveSLC.Mul(tmp, slaveSLC, true);//reramp
				slaveSLC.convertTo(slaveSLC, CV_32F);

				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_re", slaveSLC.re,
						offset_row, 0, linesPerBurst, samplesPerBurst);
					if (ret >= 0) {
						ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_im", slaveSLC.im,
							offset_row, 0, linesPerBurst, samplesPerBurst);
					}
				}
				if (cancellationRequested()) {
					finishCancelled();
					return;
				}
				if (ret < 0) {
					emit errorProcess("Failed to write ESD compensated SLC real or imag part to H5.");
					return;
				}
			}
			offset_row += linesPerBurst;
			backgeocoding.isMasterRgAzComputed = false;

            emit updateProcess(60 + 30 / burstCount * (i + 1), QStringLiteral("Enhanced spectral diversity correction..."));
		}
	}

#endif

	//deburst
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Starting Deburst processing for %1 images...").arg(backgeocoding.numOfImages));
	ComplexMat slc;
	for (int i = 0; i < backgeocoding.numOfImages; i++)
	{
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
		InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Debursting image %1/%2: %3")
			.arg(i + 1).arg(backgeocoding.numOfImages).arg(origin[i]));
		{
			NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[i]);
			conversion.read_slc_from_h5(backgeocoding.outFiles[i].c_str(), slaveSLC);
		}
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
		int expectedRows = backgeocoding.su[masterIndex - 1]->linesPerBurst * backgeocoding.su[masterIndex - 1]->burstCount;
		if (slaveSLC.isEmpty() || slaveSLC.GetRows() < expectedRows)
		{
			emit errorProcess(QString("Slave SLC image %1 has invalid dimensions (rows: %2, expected: %3). Deburst failed. Please check if DEM covers the full image or if registration succeeded.")
				.arg(origin[i]).arg(slaveSLC.GetRows()).arg(expectedRows));
			return;
		}
		{
			NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[i]);
			conversion.creat_new_h5(backgeocoding.outFiles[i].c_str());
		}
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
		slc = slaveSLC(cv::Range(backgeocoding.start.at<int>(0, 0), backgeocoding.end.at<int>(0, 0)),
			cv::Range(0, backgeocoding.su[masterIndex - 1]->samplesPerBurst));
		for (int j = 1; j < backgeocoding.su[masterIndex - 1]->burstCount; j++)
		{
			if (cancellationRequested()) {
				finishCancelled();
				return;
			}
			tmp = slaveSLC(cv::Range(backgeocoding.start.at<int>(j, 0), backgeocoding.end.at<int>(j, 0)),
				cv::Range(0, backgeocoding.su[masterIndex - 1]->samplesPerBurst));
			// 使用临时变量存储拼接结果，避�?OpenCV vconcat 目标矩阵与输入矩阵相同导致的内存重叠/重分配异�?
			cv::Mat concat_re, concat_im;
			cv::vconcat(slc.re, tmp.re, concat_re);
			cv::vconcat(slc.im, tmp.im, concat_im);
			slc.re = concat_re;
			slc.im = concat_im;
		}
		{
			NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[i]);
			conversion.write_slc_to_h5(backgeocoding.outFiles[i].c_str(), slc);
		}
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
        emit updateProcess(90 + 10 / burstCount * (i + 1), QStringLiteral("Debursting..."));
	}

#endif
	FormatConversion FC;
	/*获取主星参数*/
	cv::Mat outArray;
	const std::string& masterOutputPath = SAR_images_regis.at(masterIndex - 1);
	int readRet = 0;
	{
		NodeUtils::Hdf5Locker locker(masterOutputPath);
		readRet = FC.read_array_from_h5(masterOutputPath.c_str(), "s_re", outArray);
	}
	if (cancellationRequested()) {
		finishCancelled();
		return;
	}
	if (readRet != 0 || outArray.empty())
	{
		emit errorProcess("Failed to read valid s_re dataset from master output H5.");
		return;
	}
	int rows = outArray.rows;
	int cols = outArray.cols;
	int offset_col = 0;
	const QString geometryReferenceFile = QFileInfo(QString::fromStdString(masterOutputPath)).fileName();
	const QByteArray geometryReferenceFileUtf8 = geometryReferenceFile.toUtf8();

	/*写入辅助参数到h5*/
	for (int i = 0; i < images_number; i++)
	{
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
		{
			NodeUtils::Hdf5Locker locker_src(SAR_images.at(i));
			NodeUtils::Hdf5Locker locker_dst(SAR_images_regis.at(i));
			const std::string& sourcePath = SAR_images.at(i);
			const std::string& outputPath = SAR_images_regis.at(i);
			const QString sourceSummary = describeH5MetadataCopySummary(FC, QString::fromStdString(sourcePath));
			const QString outputSummary = describeH5MetadataCopySummary(FC, QString::fromStdString(outputPath));
			InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
				QStringLiteral("Registration metadata-copy preflight: image=%1, sourceLock=%2, outputLock=%3; source={%4}; output={%5}.")
					.arg(i + 1).arg(locker_src.isLocked() ? QStringLiteral("locked") : QStringLiteral("unlocked"))
					.arg(locker_dst.isLocked() ? QStringLiteral("locked") : QStringLiteral("unlocked"))
					.arg(sourceSummary, outputSummary), "output.metadata_copy.preflight");
			const int copyResult = copySentinelRegistrationMetadata(FC, sourcePath, outputPath);
			if (copyResult != 0) {
				const QString sourceSnapshot = describeH5ForMetadataCopy(FC, QString::fromStdString(sourcePath));
				const QString outputSnapshot = describeH5ForMetadataCopy(FC, QString::fromStdString(outputPath));
				const QString failedOutputSnapshot = describeH5ForMetadataCopy(FC, QString::fromStdString(outputPath));
				InSARLogManager::LogError("S1TopsBackGeocodingWorker",
					QStringLiteral("Registration metadata copy failed: image=%1, rc=%2; source=%3; output=%4; "
						"sourceSnapshot={%5}; outputBefore={%6}; outputAfterFailure={%7}.")
						.arg(i + 1).arg(copyResult).arg(QString::fromStdString(sourcePath), QString::fromStdString(outputPath))
						.arg(sourceSnapshot).arg(outputSnapshot).arg(failedOutputSnapshot));
				emit errorProcess(QStringLiteral("Failed to copy registration metadata to %1 (rc=%2).")
					.arg(QString::fromStdString(outputPath)).arg(copyResult));
				return;
			}
			const auto writeString = [&](const char* dataset, const char* value) {
				return FC.write_str_to_h5(outputPath.c_str(), dataset, value);
			};
			const auto writeInt = [&](const char* dataset, int value) {
				return FC.write_int_to_h5(outputPath.c_str(), dataset, value);
			};
			struct StringWrite {
				const char* dataset;
				const char* value;
			};
			const StringWrite stringWrites[] = {
				{"process_state", "coregistration"},
				{"comment", "complex-2.0"},
				{"s1_tops_geometry_reference_file", geometryReferenceFileUtf8.constData()}
			};
			for (const StringWrite& write : stringWrites) {
				const int writeResult = writeString(write.dataset, write.value);
				if (writeResult != 0) {
					emit errorProcess(QStringLiteral("Failed to write registration metadata '%1' to %2 (rc=%3).")
						.arg(QString::fromLatin1(write.dataset), QString::fromStdString(outputPath)).arg(writeResult));
					return;
				}
			}
			struct IntWrite {
				const char* dataset;
				int value;
			};
			int outputSourceRowOrigin = 0;
			if (FC.read_int_from_h5(outputPath.c_str(), "s1_tops_output_source_row_origin", &outputSourceRowOrigin) != 0 ||
				outputSourceRowOrigin < 0) {
				emit errorProcess(QStringLiteral("Missing Core common-coverage source-row provenance in %1.")
					.arg(QString::fromStdString(outputPath)));
				return;
			}
			const IntWrite intWrites[] = {
				{"offset_row", outputSourceRowOrigin},
				{"offset_col", 0},
				{"azimuth_len", rows},
				{"range_len", cols}
			};
			for (const IntWrite& write : intWrites) {
				const int writeResult = writeInt(write.dataset, write.value);
				if (writeResult != 0) {
					emit errorProcess(QStringLiteral("Failed to write registration metadata '%1' to %2 (rc=%3).")
						.arg(QString::fromLatin1(write.dataset), QString::fromStdString(outputPath)).arg(writeResult));
					return;
				}
			}
		}
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
	}
	InSARLogManager::LogDebug("S1TopsBackGeocodingWorker", "Copied registration metadata and completion parameters to output H5 files.", "output.metadata");

    for (int imageIndex = 0; imageIndex < images_number; ++imageIndex) {
        const QString outputPath = QString::fromStdString(SAR_images_regis.at(imageIndex));
        const ComplexOutputCoverage coverage = measureComplexOutputCoverage(outputPath);
        if (cancellationRequested()) {
            finishCancelled();
            return;
        }
        if (!coverage.available) {
            InSARLogManager::LogWarning("S1TopsBackGeocodingWorker",
                QStringLiteral("Output complex-sample coverage is unavailable for image %1: %2")
                    .arg(imageIndex + 1).arg(coverage.errorMessage));
            continue;
        }

        const double nonZeroCoveragePercent = coverage.totalSamples > 0
            ? 100.0 * static_cast<double>(coverage.nonZeroSamples) / coverage.totalSamples : 0.0;
        InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
            QStringLiteral("Output complex-sample coverage: image=%1, dimensions=%2x%3, nonZero=%4/%5 (%6%). "
                           "This is an exact diagnostic of non-zero complex samples, not a geometric validity mask.")
                .arg(imageIndex + 1).arg(coverage.rows).arg(coverage.columns)
                .arg(coverage.nonZeroSamples).arg(coverage.totalSamples)
                .arg(nonZeroCoveragePercent, 0, 'f', 3),
            "quality.output_coverage");

        NodeUtils::Hdf5Locker locker(SAR_images_regis.at(imageIndex));
        const int writeResult = FC.write_double_to_h5(SAR_images_regis.at(imageIndex).c_str(),
            "s1_tops_complex_nonzero_samples", static_cast<double>(coverage.nonZeroSamples)) |
            FC.write_double_to_h5(SAR_images_regis.at(imageIndex).c_str(),
                "s1_tops_complex_total_samples", static_cast<double>(coverage.totalSamples)) |
            FC.write_double_to_h5(SAR_images_regis.at(imageIndex).c_str(),
                "s1_tops_complex_nonzero_coverage_percent", nonZeroCoveragePercent);
        if (writeResult != 0) {
            InSARLogManager::LogWarning("S1TopsBackGeocodingWorker",
                QStringLiteral("Unable to persist output complex-sample coverage metrics for image %1.").arg(imageIndex + 1));
        }
    }
	std::vector<SentinelBurstQualityStatus> burstQualityStatus;
	ret = backgeocoding.getBurstQualityStatus(burstQualityStatus);
	if (ret < 0) {
		emit errorProcess("Failed to retrieve Back-Geocoding quality status.");
		return;
	}
	InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
		QString("Back-Geocoding quality status: return=%1, entries=%2.")
			.arg(ret).arg(static_cast<qulonglong>(burstQualityStatus.size())), "quality.summary");

	for (const SentinelBurstQualityStatus& status : burstQualityStatus)
	{
        const double demGridProjectionCoverage = status.attemptedPoints > 0
            ? 100.0 * static_cast<double>(status.validPoints) / status.attemptedPoints : 0.0;
        const double rangeOrBurstRejectionRatio = status.attemptedPoints > 0
            ? 100.0 * static_cast<double>(status.rangeOrBurstFailures) / status.attemptedPoints : 0.0;
        InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
            QString("Burst projection context: image=%1, burst=%2, demGridValid=%3/%4 (%5%), "
                    "rangeOrBurstRejections=%6 (%7%), zeroDopplerFailures=%8. "
                    "These ratios use the full DEM grid as their denominator and are not SAR footprint coverage.")
                .arg(status.imageIndex).arg(status.burstIndex)
                .arg(status.validPoints).arg(status.attemptedPoints)
                .arg(demGridProjectionCoverage, 0, 'f', 1)
                .arg(status.rangeOrBurstFailures).arg(rangeOrBurstRejectionRatio, 0, 'f', 1)
                .arg(status.zeroDopplerFailures), "quality.projection_context");
		InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
			QString("Burst quality: image=%1, burst=%2, code=%3, valid=%4/%5, zeroDopplerFailures=%6, rangeOrBurstFailures=%7, fitPoints=%8, fitRms=%9.")
				.arg(status.imageIndex).arg(status.burstIndex).arg(status.qualityCode)
				.arg(status.validPoints).arg(status.attemptedPoints)
				.arg(status.zeroDopplerFailures).arg(status.rangeOrBurstFailures)
				.arg(status.fitPointCount).arg(status.fitRms, 0, 'g', 8), "quality.burst");
	}

	std::vector<SentinelZeroDopplerFailureStatistic> zeroDopplerStatistics;
	const int zeroDopplerStatisticsRet = backgeocoding.getZeroDopplerFailureStatistics(zeroDopplerStatistics);
	if (zeroDopplerStatisticsRet < 0) {
		InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
			"Zero-Doppler failure statistics are unavailable for this task.", "quality.zero_doppler");
	} else {
		InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
			QString("Zero-Doppler failure statistics: return=%1, groups=%2.")
				.arg(zeroDopplerStatisticsRet).arg(static_cast<qulonglong>(zeroDopplerStatistics.size())), "quality.zero_doppler");
	}

	bool hasQualityWarning = hasRefinementWarning || !m_nativeQualityWarnings.isEmpty();
	QStringList qualityWarnings = refinementWarnings;
	qualityWarnings.append(m_nativeQualityWarnings);
	QSet<QString> zeroDopplerAffectedBurstPairs;
	for (const SentinelZeroDopplerFailureStatistic& statistic : zeroDopplerStatistics)
	{
		if (statistic.count <= 0) {
			continue;
		}

		hasQualityWarning = true;
		zeroDopplerAffectedBurstPairs.insert(QStringLiteral("%1:%2")
			.arg(statistic.imageIndex).arg(statistic.burstIndex));
		InSARLogManager::LogDebug("S1TopsBackGeocodingWorker", zeroDopplerStatisticText(statistic), "quality.zero_doppler");
	}
	if (!zeroDopplerAffectedBurstPairs.isEmpty()) {
		qualityWarnings.append(QStringLiteral("Zero-Doppler projection rejections affected %1 retained burst pairs; burst-attributed counts are available in diagnostic logs and were handled by the quality policy.")
			.arg(zeroDopplerAffectedBurstPairs.size()));
	}

	if (!zeroDopplerStatistics.empty()) {
		SentinelZeroDopplerDiagnostic lastDiagnostic;
		const int lastDiagnosticRet = backgeocoding.getLastZeroDopplerDiagnostic(lastDiagnostic);
		if (lastDiagnosticRet == 0) {
			InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
				QString("Last zero-Doppler diagnostic: image=%1, burst=%2, line=%3, sample=%4, %5, %6, return=%7, stateVectors=%8.")
					.arg(lastDiagnostic.imageIndex).arg(lastDiagnostic.burstIndex)
					.arg(lastDiagnostic.line).arg(lastDiagnostic.sample)
					.arg(zeroDopplerReasonText(lastDiagnostic.reason))
					.arg(zeroDopplerCallPathText(lastDiagnostic.callPath))
					.arg(lastDiagnostic.returnCode).arg(lastDiagnostic.stateVectorCount), "quality.zero_doppler");
		} else {
			InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
				"No last zero-Doppler diagnostic is available.", "quality.zero_doppler");
		}
	}

	int partialInvalidBurstCount = 0;
	int zeroOffsetFallbackCount = 0;
	double minValidRatio = 1.0;
	double maxValidRatio = 0.0;
	for (const SentinelBurstQualityStatus& status : burstQualityStatus)
	{
		if (status.qualityCode == SENTINEL_BURST_FAILED)
		{
			emit errorProcess(QStringLiteral("Back-Geocoding quality check failed: slave image %1, master burst %2.")
				.arg(status.imageIndex).arg(status.burstIndex));
			return;
		}

		if (status.qualityCode == SENTINEL_BURST_WARNING_PARTIAL_INVALID ||
			status.qualityCode == SENTINEL_BURST_WARNING_ZERO_OFFSET_FALLBACK)
		{
			hasQualityWarning = true;
			if (status.qualityCode == SENTINEL_BURST_WARNING_PARTIAL_INVALID) {
				++partialInvalidBurstCount;
				const double validRatio = status.attemptedPoints > 0
					? static_cast<double>(status.validPoints) / status.attemptedPoints : 0.0;
				minValidRatio = qMin(minValidRatio, validRatio);
				maxValidRatio = qMax(maxValidRatio, validRatio);
			}
			else {
				++zeroOffsetFallbackCount;
			}
		}
	}
	if (partialInvalidBurstCount > 0) {
		qualityWarnings.append(QStringLiteral("%1 retained burst pairs had geometric projection rejections; joint-valid points occupy %2%-%3% of the full DEM grid, not SAR footprint or final output coverage.")
			.arg(partialInvalidBurstCount)
			.arg(minValidRatio * 100.0, 0, 'f', 1)
			.arg(maxValidRatio * 100.0, 0, 'f', 1));
	}
	if (zeroOffsetFallbackCount > 0) {
        qualityWarnings.append(QStringLiteral("%1 bursts fell back to zero burst offset.").arg(zeroOffsetFallbackCount));
	}

	if (cancellationRequested()) {
		finishCancelled();
		return;
	}

	for (int i = 0; i < images_number; i++)
	{
		{
			NodeUtils::Hdf5Locker locker(SAR_images_regis.at(i));
			ret = FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "s1_tops_back_geocoding_complete", 1);
		}
		if (cancellationRequested()) {
			finishCancelled();
			return;
		}
		if (ret != 0) {
			emit errorProcess(QStringLiteral("Failed to finalize registration output: %1.").arg(QString::fromStdString(SAR_images_regis.at(i))));
			return;
		}
	}

	SentinelRefinementTransactionStatus finalTransactionStatus = {};
	finalTransactionStatus.version = SENTINEL_REFINEMENT_TRANSACTION_STATUS_VERSION;
	finalTransactionStatus.structSize = sizeof(finalTransactionStatus);
	const int transactionStatusRet = backgeocoding.getPostRegistrationRefinementTransactionStatus(finalTransactionStatus);
	const bool refinementTransactionTerminated =
		finalTransactionStatus.state == SENTINEL_REFINEMENT_TRANSACTION_NONE ||
		finalTransactionStatus.state == SENTINEL_REFINEMENT_TRANSACTION_COMPLETE;
	if (transactionStatusRet != 0 || !refinementTransactionTerminated) {
		emit errorProcess(QStringLiteral("Sentinel-1 refinement transaction is not in a committable terminal state before staging validation: "
			"query=%1, state=%2, outputs=%3, verified=%4.")
			.arg(transactionStatusRet)
			.arg(finalTransactionStatus.state)
			.arg(finalTransactionStatus.outputCount)
			.arg(finalTransactionStatus.verifiedOutputCount));
		return;
	}
	InSARLogManager::LogDebug("S1TopsBackGeocodingWorker",
		QString("DLL refinement transaction is in a committable terminal state: state=%1, outputs=%2, verified=%3; staging cleanup may proceed.")
			.arg(finalTransactionStatus.state)
			.arg(finalTransactionStatus.outputCount)
			.arg(finalTransactionStatus.verifiedOutputCount), "refinement.isolation_verified");
	const QString refinementManifestPath = QDir(savePath).filePath(
		dstNode + QStringLiteral("/refinement_transaction.json"));
	if (QFileInfo::exists(refinementManifestPath) && !QFile::remove(refinementManifestPath)) {
		emit errorProcess(QStringLiteral("Cannot remove staging refinement transaction manifest: %1").arg(refinementManifestPath));
		return;
	}
	for (const std::string& outputPath : SAR_images_regis) {
		const QString fullBurstPath = QString::fromStdString(outputPath) + QStringLiteral(".fullburst");
		if (QFile::exists(fullBurstPath) && !QFile::remove(fullBurstPath)) {
			emit errorProcess(QStringLiteral("Cannot remove completed full-burst scratch output: %1").arg(fullBurstPath));
			return;
		}
	}

	cleanupGuard.dismiss();
	backgeocoding.setDiagnosticCallback(nullptr, nullptr);
	QStringList regisH5Paths;
	for (const auto& pathStr : SAR_images_regis)
	{
		regisH5Paths.append(QString::fromStdString(pathStr));
	}

	emit registrationFinished(regisH5Paths, dstNode, dstProject, savePath, masterIndex, hasQualityWarning, qualityWarnings);
	InSARLogManager::LogDebug("S1TopsBackGeocodingWorker", QString("Worker finished: outputs=%1, qualityWarnings=%2")
		.arg(regisH5Paths.join("; ")).arg(qualityWarnings.size()), "worker.finished");
	emit endProcess();
}
