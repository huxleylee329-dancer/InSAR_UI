#include "DenoiseWorker.h"

#include <Filter.h>
#include <FormatConversion.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <cmath>
#include <limits>

#ifdef _DEBUG
#pragma comment(lib, "Filter_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#else
#pragma comment(lib, "Filter.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#endif

using namespace cv;
using namespace std;

namespace {

thread_local DenoiseWorker* currentWorker = nullptr;
thread_local int totalImages = 1;
thread_local int currentImageIndex = 0;
thread_local int lastLoggedMappedProgress = -1;

bool __stdcall denoiseProgressCallback(int progress, const char* message)
{
    thread_local QElapsedTimer callbackTimer;
    thread_local bool timerStarted = false;
    if (!timerStarted) {
        callbackTimer.start();
        timerStarted = true;
    }
    if (progress != 0 && progress != 100 && callbackTimer.elapsed() < 100) {
        return true;
    }
    callbackTimer.restart();

    if (!currentWorker) {
        return true;
    }
    if (currentWorker->isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        return false;
    }

    const int progressRange = 80 / qMax(totalImages, 1);
    const int mappedProgress = 10 + currentImageIndex * progressRange + progress * progressRange / 100;
    QString progressMessage = QString("Filtering image %1/%2").arg(currentImageIndex + 1).arg(totalImages);
    if (message && message[0] != '\0') {
        progressMessage += QString(" (%1)").arg(QString::fromUtf8(message));
    }
    emit currentWorker->updateProcess(mappedProgress, progressMessage);
if (progress == 0 || progress == 100 || lastLoggedMappedProgress < 0 ||
        mappedProgress >= lastLoggedMappedProgress + 10) {
        InSARLogManager::LogDebug("DenoiseWorker",
            QStringLiteral("Filtering progress: image=%1/%2, innerProgress=%3%, mappedProgress=%4%")
                .arg(currentImageIndex + 1).arg(totalImages).arg(progress).arg(mappedProgress),
            "denoise.progress");
        lastLoggedMappedProgress = mappedProgress;
    }
    return true;
}

class WorkerResetGuard
{
public:
    ~WorkerResetGuard()
    {
        currentWorker = nullptr;
        totalImages = 1;
        currentImageIndex = 0;
        lastLoggedMappedProgress = -1;
    }
};

bool copyCompatibleCoherence(const QString& inputPath, const QString& outputPath,
                             const Mat& filteredPhase, QString& error)
{
    Mat coherence;
    if (!NodeUtils::readMatFromH5(inputPath, "coherence", coherence)) {
        return true;
    }

    if (coherence.empty() || coherence.rows != filteredPhase.rows ||
        coherence.cols != filteredPhase.cols || coherence.channels() != 1 ||
        (coherence.type() != CV_32FC1 && coherence.type() != CV_64FC1)) {
        InSARLogManager::LogWarning("DenoiseWorker",
                                    QStringLiteral("Skipping incompatible coherence metadata: %1")
                                        .arg(inputPath));
        return true;
    }

    for (int row = 0; row < coherence.rows; ++row) {
        for (int column = 0; column < coherence.cols; ++column) {
            const double value = coherence.type() == CV_32FC1
                ? static_cast<double>(coherence.ptr<float>(row)[column])
                : coherence.ptr<double>(row)[column];
            if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
                InSARLogManager::LogWarning("DenoiseWorker",
                                            QStringLiteral("Skipping invalid coherence metadata: %1")
                                                .arg(inputPath));
                return true;
            }
        }
    }

    if (!NodeUtils::writeMatToH5(outputPath, "coherence", coherence)) {
        error = QStringLiteral("Failed to preserve coherence metadata.");
        return false;
    }
    // 随 coherence 一并传播语义标签：该数据集为原样复制，语义不变。
    // 输入无标签时保持"未知"，不隐式升级。
    QString semanticsError;
    if (!NodeUtils::copyCoherenceSemantics(inputPath, outputPath, &semanticsError)) {
        error = QStringLiteral("Failed to preserve coherence semantics: %1").arg(semanticsError);
        return false;
    }
    return true;
}

bool readSnapCompatibleInputs(const QString& inputPath,
                              Mat& interferogramReal,
                              Mat& interferogramImaginary,
                              Mat& complexGamma,
                              Mat& gammaValidMask,
                              QString& error)
{
	QString phaseContractError;
	int phaseSchemaVersion = 0;
	if (!NodeUtils::validatePhaseValidityContract(inputPath, false, &phaseContractError) ||
		!NodeUtils::readScalarFromH5(inputPath, "phase_processing_schema_version", phaseSchemaVersion) ||
		phaseSchemaVersion != 2) {
		error = phaseContractError.isEmpty()
			? QStringLiteral("GoldsteinSnapCompatibleV1 requires phase_processing_schema_version=2: %1").arg(inputPath)
			: phaseContractError;
		return false;
	}
    Mat gammaValidSampleCount;
    if (!NodeUtils::readMatFromH5(inputPath, "interferogram_i", interferogramReal, CV_32F) ||
        !NodeUtils::readMatFromH5(inputPath, "interferogram_q", interferogramImaginary, CV_32F) ||
        !NodeUtils::readMatFromH5(inputPath, "complex_gamma", complexGamma, CV_64F) ||
        !NodeUtils::readMatFromH5(inputPath, "complex_gamma_valid_mask", gammaValidMask, CV_8U) ||
        !NodeUtils::readMatFromH5(inputPath, "complex_gamma_valid_sample_count", gammaValidSampleCount, CV_32S)) {
        error = QStringLiteral("GoldsteinSnapCompatibleV1 requires interferogram_i/q and true complex_gamma datasets: %1")
                    .arg(inputPath);
        return false;
    }
    if (interferogramReal.empty() || interferogramImaginary.empty() || complexGamma.empty() || gammaValidMask.empty() ||
        interferogramReal.size() != interferogramImaginary.size() || interferogramReal.size() != complexGamma.size() ||
        interferogramReal.size() != gammaValidMask.size() || interferogramReal.size() != gammaValidSampleCount.size()) {
        error = QStringLiteral("GoldsteinSnapCompatibleV1 input datasets have inconsistent grids: %1").arg(inputPath);
        return false;
    }
    int gammaWindowRange = 0;
    int gammaWindowAzimuth = 0;
    std::string gammaSemantics;
    std::string gammaAlgorithm;
    if (!NodeUtils::readStringFromH5(inputPath, "complex_gamma_semantics", gammaSemantics) ||
        QString::fromStdString(gammaSemantics) != QString::fromLatin1(NodeUtils::CoherenceSemantics::kComplexGamma) ||
        !NodeUtils::readStringFromH5(inputPath, "complex_gamma_algorithm", gammaAlgorithm) ||
        gammaAlgorithm != "corrected_multilooked_source_row_aware_v1" ||
        !NodeUtils::readScalarFromH5(inputPath, "complex_gamma_window_range", gammaWindowRange) ||
        !NodeUtils::readScalarFromH5(inputPath, "complex_gamma_window_azimuth", gammaWindowAzimuth) ||
        gammaWindowRange < 3 || gammaWindowAzimuth < 3 ||
        gammaWindowRange % 2 == 0 || gammaWindowAzimuth % 2 == 0) {
        error = QStringLiteral("GoldsteinSnapCompatibleV1 requires complex_gamma semantics, not phase concentration: %1")
                    .arg(inputPath);
        return false;
    }
    for (int row = 0; row < complexGamma.rows; ++row) {
        const double* gammaRow = complexGamma.ptr<double>(row);
        const uchar* maskRow = gammaValidMask.ptr<uchar>(row);
        const int* countRow = gammaValidSampleCount.ptr<int>(row);
        for (int column = 0; column < complexGamma.cols; ++column) {
            if ((maskRow[column] != 0 && maskRow[column] != 1) || countRow[column] < 0 ||
                (maskRow[column] != 0 && (countRow[column] <= 0 || !std::isfinite(gammaRow[column]) ||
                                           gammaRow[column] < 0.0 || gammaRow[column] > 1.0)) ||
                (maskRow[column] == 0 && countRow[column] != 0)) {
                error = QStringLiteral("GoldsteinSnapCompatibleV1 complex_gamma values or support mask are invalid: %1").arg(inputPath);
                return false;
            }
        }
    }
    return true;
}

bool writeDenoisedPhase(FormatConversion& conversion,
                        const QString& inputPath,
                        const QString& outputPath,
                        const QString& projectPath,
                        const Mat& filteredPhase,
						const Mat* filterSupportMask,
                        int method,
                        int slopePrefilterWindow,
                        int slopeWindow,
                        int goldsteinWindow,
                        int nPad,
                        double alpha,
						const Mat* filteredInterferogramReal,
						const Mat* filteredInterferogramImaginary,
                        int& offsetRow,
                        int& offsetCol,
                        QString& error)
{
    NodeUtils::Hdf5Locker locker;
    if (conversion.creat_new_h5(outputPath.toStdString().c_str()) < 0) {
        error = QStringLiteral("Failed to create denoised output.");
        return false;
    }

    if (!NodeUtils::writeMatToH5(outputPath, "phase", filteredPhase)) {
        error = QStringLiteral("Failed to write denoised phase data.");
        return false;
    }

    bool metadataOk = NodeUtils::writeScalarToH5(outputPath, "denoise_method", method);
    if (method == 1) {
        metadataOk = metadataOk &&
            NodeUtils::writeScalarToH5(outputPath, "denoise_slope_pre_win", slopePrefilterWindow) &&
            NodeUtils::writeScalarToH5(outputPath, "denoise_slope_win", slopeWindow);
    } else if (method == 2) {
        metadataOk = metadataOk &&
            NodeUtils::writeScalarToH5(outputPath, "denoise_goldstein_win", goldsteinWindow) &&
            NodeUtils::writeScalarToH5(outputPath, "denoise_goldstein_npad", nPad) &&
            NodeUtils::writeScalarToH5(outputPath, "denoise_goldstein_alpha", alpha);
    } else if (method == 3) {
        metadataOk = metadataOk &&
            NodeUtils::writeScalarToH5(outputPath, "denoise_dl", 1);
	} else if (method == 4) {
		metadataOk = metadataOk &&
			NodeUtils::writeStringToH5(outputPath, "denoise_goldstein_profile", "GoldsteinSnapCompatibleV1") &&
			NodeUtils::writeScalarToH5(outputPath, "denoise_goldstein_win", 64) &&
			NodeUtils::writeScalarToH5(outputPath, "denoise_goldstein_npad", 0) &&
			NodeUtils::writeStringToH5(outputPath, "denoise_goldstein_alpha_semantics",
				"clamp_1_minus_mean_complex_gamma_0.2_1.0_v1") &&
			NodeUtils::writeStringToH5(outputPath, "denoise_goldstein_spectral_smoothing",
				"mean_3x3_skip_zero_power_v1") &&
			NodeUtils::writeStringToH5(outputPath, "denoise_goldstein_overlap_window",
				"separable_triangular_v1");
    }
    if (!metadataOk) {
        error = QStringLiteral("Failed to write denoise processing metadata.");
        return false;
    }

    string sourcePath;
    Mat value;
    NodeUtils::readStringFromH5(inputPath, "source_1", sourcePath);
    conversion.write_str_to_h5(outputPath.toStdString().c_str(), "source_1", sourcePath.c_str());
    const QString masterPath = QDir::toNativeSeparators(projectPath) + QString::fromStdString(sourcePath);

    NodeUtils::readStringFromH5(inputPath, "source_2", sourcePath);
    conversion.write_str_to_h5(outputPath.toStdString().c_str(), "source_2", sourcePath.c_str());
    if (!NodeUtils::copySourcePathMetadata(inputPath, outputPath, &error)) {
        return false;
    }

    const char* const copiedDatasets[] = {
        "range_len", "azimuth_len", "multilook_rg", "multilook_az"
    };
    for (const char* dataset : copiedDatasets) {
        NodeUtils::readMatFromH5(inputPath, dataset, value);
        NodeUtils::writeMatToH5(outputPath, dataset, value);
    }
	if (!NodeUtils::copyPhaseProcessingMetadata(inputPath, outputPath, &error)) {
        return false;
    }
	if ((method == 2 || method == 4) && filterSupportMask != nullptr) {
		const int supportCount = countNonZero(*filterSupportMask);
		if (!NodeUtils::writeMatToH5(outputPath, QStringLiteral("denoise_filter_support_mask"), *filterSupportMask) ||
			!NodeUtils::writeScalarToH5(outputPath, QStringLiteral("denoise_mask_contract_version"), method == 4 ? 2 : 1) ||
			!NodeUtils::writeScalarToH5(outputPath, QStringLiteral("denoise_filter_support_count"), supportCount) ||
			!NodeUtils::writeStringToH5(outputPath, QStringLiteral("denoise_filter_support_semantics"),
				method == 4 ? "original_valid_pixel_with_at_least_one_processed_fft_window_v2"
					: "fully_valid_fft_window_coverage_v1") ||
			!NodeUtils::writeStringToH5(outputPath, QStringLiteral("denoise_filter_fallback_semantics"),
				method == 4 ? "input_phase_passthrough_when_unsupported_v2"
					: "input_phase_passthrough_when_unsupported_v1")) {
			error = QStringLiteral("Failed to write Goldstein filter support contract.");
			return false;
		}
	}
    if (!copyCompatibleCoherence(inputPath, outputPath, filteredPhase, error)) {
        return false;
    }
	if (method == 4) {
		Mat value;
		std::string text;
		if (filteredInterferogramReal == nullptr || filteredInterferogramImaginary == nullptr ||
			!NodeUtils::writeMatToH5(outputPath, "interferogram_i", *filteredInterferogramReal) ||
			!NodeUtils::writeMatToH5(outputPath, "interferogram_q", *filteredInterferogramImaginary) ||
			!NodeUtils::readMatFromH5(inputPath, "complex_gamma", value, CV_64F) ||
			!NodeUtils::writeMatToH5(outputPath, "complex_gamma", value) ||
			!NodeUtils::readMatFromH5(inputPath, "complex_gamma_valid_mask", value, CV_8U) ||
			!NodeUtils::writeMatToH5(outputPath, "complex_gamma_valid_mask", value) ||
			!NodeUtils::readMatFromH5(inputPath, "complex_gamma_valid_sample_count", value, CV_32S) ||
			!NodeUtils::writeMatToH5(outputPath, "complex_gamma_valid_sample_count", value) ||
			!NodeUtils::readStringFromH5(inputPath, "complex_gamma_semantics", text) ||
			!NodeUtils::writeStringToH5(outputPath, "complex_gamma_semantics", text) ||
			!NodeUtils::readStringFromH5(inputPath, "complex_gamma_algorithm", text) ||
			!NodeUtils::writeStringToH5(outputPath, "complex_gamma_algorithm", text)) {
			error = QStringLiteral("Failed to preserve GoldsteinSnapCompatibleV1 I/Q or complex-gamma provenance.");
			return false;
		}
		int window = 0;
		if (!NodeUtils::readScalarFromH5(inputPath, "complex_gamma_window_range", window) ||
			!NodeUtils::writeScalarToH5(outputPath, "complex_gamma_window_range", window) ||
			!NodeUtils::readScalarFromH5(inputPath, "complex_gamma_window_azimuth", window) ||
			!NodeUtils::writeScalarToH5(outputPath, "complex_gamma_window_azimuth", window)) {
			error = QStringLiteral("Failed to preserve GoldsteinSnapCompatibleV1 complex-gamma window provenance.");
			return false;
		}
	}
    if (NodeUtils::readMatFromH5(inputPath, "mapped_lon", value)) {
        NodeUtils::writeMatToH5(outputPath, "mapped_lon", value);
    }
    if (NodeUtils::readMatFromH5(inputPath, "mapped_lat", value)) {
        NodeUtils::writeMatToH5(outputPath, "mapped_lat", value);
    }

    Mat offsetValue = Mat::zeros(1, 1, CV_32SC1);
    NodeUtils::readMatFromH5(masterPath, "offset_row", offsetValue);
    offsetRow = offsetValue.at<int>(0, 0);
    NodeUtils::readMatFromH5(masterPath, "offset_col", offsetValue);
    offsetCol = offsetValue.at<int>(0, 0);
    return true;
}

} // namespace

DenoiseWorker::DenoiseWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<DenoiseFileResult>("DenoiseFileResult");
}

DenoiseWorker::~DenoiseWorker()
{
}

void DenoiseWorker::Denoise(QList<int> para,
                            double alpha,
                            QString savePath,
                            QString outputNode,
                            QStringList phaseNames,
                            QStringList phasePaths)
{
    currentWorker = this;
    WorkerResetGuard resetGuard;

    const auto finishCancelled = [this]() {
        InSARLogManager::LogInfo("DenoiseWorker", "Task cancelled by user/interruption.");
        emit cancelled();
    };

    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        finishCancelled();
        return;
    }
    if (savePath.isEmpty() || outputNode.isEmpty() || phaseNames.isEmpty() ||
        phaseNames.size() != phasePaths.size() || para.size() < 5) {
        emit errorProcess(QStringLiteral("Invalid denoise parameters or input paths."));
        return;
    }

    const QString outputDirectory = savePath + "/" + outputNode;
    QDir targetDir(outputDirectory);
    if (!targetDir.exists() && !QDir(savePath).mkdir(outputNode)) {
        emit errorProcess(QStringLiteral("Failed to create denoise output directory."));
        return;
    }

    const int method = para.at(4);
    const int imageCount = phasePaths.size();
    totalImages = imageCount;
    Filter filter;
    FormatConversion conversion;

    for (int i = 0; i < imageCount; ++i) {
        currentImageIndex = i;
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            finishCancelled();
            return;
        }

        QString inputPath = phasePaths.at(i);
        if (QDir::isRelativePath(inputPath)) {
            inputPath = savePath + "/" + inputPath;
        }
        const QString phaseName = phaseNames.at(i);
        const QString filterName = phaseName + "_denoised";
        const QString outputPath = outputDirectory + "/" + filterName + ".h5";

        QString phaseValidityError;
		if (!NodeUtils::validatePhaseValidityContract(inputPath, false, &phaseValidityError)) {
            emit errorProcess(phaseValidityError);
            return;
        }
		if (!NodeUtils::validateDenoiseFilterSupportContract(inputPath, &phaseValidityError)) {
			emit errorProcess(phaseValidityError);
			return;
		}

        emit updateProcess(10 + i * 80 / imageCount,
                            QString("Filtering image %1/%2").arg(i + 1).arg(imageCount));

        Mat phase;
        if (!NodeUtils::readMatFromH5(inputPath, "phase", phase, CV_64F)) {
            emit errorProcess(QStringLiteral("Failed to read input phase data: ") + inputPath);
            return;
        }

		Mat inputValidMask;
		const bool hasV2ValidMask = NodeUtils::readMatFromH5(inputPath, "phase_valid_mask", inputValidMask, CV_8U);
		if (!hasV2ValidMask) {
			inputValidMask = Mat::ones(phase.size(), CV_8U);
		}
		const int inputValidCount = countNonZero(inputValidMask);
		if (method != 2 && method != 4 && inputValidCount != phase.rows * phase.cols) {
			emit errorProcess(QStringLiteral("当前滤波方法不支持掩膜相位；请选择 Goldstein 滤波：%1").arg(inputPath));
			return;
		}

		Mat filteredPhase;
		Mat filterSupportMask;
		Mat inputInterferogramReal;
		Mat inputInterferogramImaginary;
		Mat filteredInterferogramReal;
		Mat filteredInterferogramImaginary;
        int result = -1;
        if (method == 1) {
            result = filter.slope_adaptive_filter(phase, filteredPhase, para.at(1), para.at(0), denoiseProgressCallback);
        } else if (method == 2) {
			result = filter.Goldstein_filter_masked(phase, inputValidMask, filteredPhase, filterSupportMask,
				alpha, para.at(2), para.at(3), denoiseProgressCallback);
		} else if (method == 4) {
			Mat interferogramReal;
			Mat interferogramImaginary;
			Mat complexGamma;
			Mat gammaValidMask;
			QString snapInputError;
			if (!readSnapCompatibleInputs(inputPath, interferogramReal, interferogramImaginary,
				complexGamma, gammaValidMask, snapInputError) ||
				interferogramReal.size() != phase.size()) {
				emit errorProcess(snapInputError.isEmpty()
					? QStringLiteral("GoldsteinSnapCompatibleV1 I/Q grid does not match phase: %1").arg(inputPath)
					: snapInputError);
				return;
			}
			inputInterferogramReal = interferogramReal;
			inputInterferogramImaginary = interferogramImaginary;
			result = filter.Goldstein_filter_snap_compatible(interferogramReal, interferogramImaginary,
				complexGamma, gammaValidMask, inputValidMask, filteredInterferogramReal,
				filteredInterferogramImaginary, filterSupportMask, denoiseProgressCallback);
			if (result == 0) {
				filteredPhase.create(phase.size(), CV_64F);
				for (int row = 0; row < filteredPhase.rows; ++row) {
					const float* realRow = filteredInterferogramReal.ptr<float>(row);
					const float* imaginaryRow = filteredInterferogramImaginary.ptr<float>(row);
					const uchar* supportRow = filterSupportMask.ptr<uchar>(row);
					double* phaseRow = filteredPhase.ptr<double>(row);
					for (int column = 0; column < filteredPhase.cols; ++column) {
						phaseRow[column] = supportRow[column] != 0
							? std::atan2(static_cast<double>(imaginaryRow[column]), static_cast<double>(realRow[column]))
							: std::numeric_limits<double>::quiet_NaN();
					}
				}
			}
        } else if (method == 3) {
            const QString applicationPath = QCoreApplication::applicationDirPath();
            const QString modelPath = applicationPath + "\\other\\net.pt";
            result = filter.filter_dl(applicationPath.toStdString().c_str(),
                                      QDir::toNativeSeparators(outputDirectory).toStdString().c_str(),
                                      modelPath.toStdString().c_str(), phase, filteredPhase);
        } else {
            emit errorProcess(QStringLiteral("Unknown denoise method."));
            return;
        }

        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            QFile::remove(outputPath);
            finishCancelled();
            return;
        }
        if (result < 0 || filteredPhase.empty()) {
            emit errorProcess(QStringLiteral("Denoise processing failed."));
            return;
        }
		Mat publishedPhase;
		Mat publishedInterferogramReal;
		Mat publishedInterferogramImaginary;
		const Mat* supportForMetadata = nullptr;
		if ((method == 2 || method == 4) && hasV2ValidMask) {
			if (filterSupportMask.type() != CV_8U || filterSupportMask.channels() != 1 ||
				filterSupportMask.size() != phase.size()) {
				emit errorProcess(QStringLiteral("Goldstein 滤波支持掩膜类型或网格不一致：%1").arg(inputPath));
				return;
			}
			for (int row = 0; row < phase.rows; ++row) {
				const uchar* valid = inputValidMask.ptr<uchar>(row);
				const uchar* support = filterSupportMask.ptr<uchar>(row);
				const double* filtered = filteredPhase.ptr<double>(row);
				for (int column = 0; column < phase.cols; ++column) {
					if ((support[column] != 0 && support[column] != 1) ||
						(support[column] != 0 && valid[column] == 0) ||
						(support[column] != 0 && !std::isfinite(filtered[column]))) {
						emit errorProcess(QStringLiteral("Goldstein 滤波支持掩膜或结果无效：%1").arg(inputPath));
						return;
					}
				}
			}
			publishedPhase = phase.clone();
			filteredPhase.copyTo(publishedPhase, filterSupportMask);
			if (method == 4) {
				publishedInterferogramReal = inputInterferogramReal.clone();
				publishedInterferogramImaginary = inputInterferogramImaginary.clone();
				filteredInterferogramReal.copyTo(publishedInterferogramReal, filterSupportMask);
				filteredInterferogramImaginary.copyTo(publishedInterferogramImaginary, filterSupportMask);
			}
			supportForMetadata = &filterSupportMask;
			InSARLogManager::LogInfo("DenoiseWorker",
				QStringLiteral("Goldstein filter support: profile=%1, inputValid=%2, fftSupported=%3, total=%4")
					.arg(method == 4 ? QStringLiteral("GoldsteinSnapCompatibleV1") : QStringLiteral("GoldsteinPhaseLegacyV1"))
					.arg(inputValidCount).arg(countNonZero(filterSupportMask)).arg(phase.rows * phase.cols));
		} else {
			publishedPhase = filteredPhase;
		}

        int offsetRow = 0;
        int offsetCol = 0;
        QString writeError;
		if (!writeDenoisedPhase(conversion, inputPath, outputPath, savePath, publishedPhase, supportForMetadata,
								method, para.at(0), para.at(1), para.at(2), para.at(3), alpha,
								method == 4 ? &publishedInterferogramReal : nullptr,
								method == 4 ? &publishedInterferogramImaginary : nullptr,
                                offsetRow, offsetCol, writeError)) {
            QFile::remove(outputPath);
            emit errorProcess(writeError);
            return;
        }

        DenoiseFileResult fileResult;
        fileResult.fileName = outputNode;
        fileResult.filterName = filterName;
        fileResult.filterPath = outputPath;
        fileResult.relativePath = "/" + outputNode + "/" + filterName + ".h5";
        fileResult.offsetRow = offsetRow;
        fileResult.offsetCol = offsetCol;
        emit denoiseGenerated(fileResult);
        emit updateProcess((i + 1) * 100 / imageCount,
                            QString("Filtered image %1/%2").arg(i + 1).arg(imageCount));
    }

    InSARLogManager::LogInfo("DenoiseWorker", "Task completed: Denoise");
    emit endProcess();
}
