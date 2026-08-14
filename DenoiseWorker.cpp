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
    return true;
}

bool writeDenoisedPhase(FormatConversion& conversion,
                        const QString& inputPath,
                        const QString& outputPath,
                        const QString& projectPath,
                        const Mat& filteredPhase,
                        int method,
                        int slopePrefilterWindow,
                        int slopeWindow,
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
    if (!NodeUtils::writeScalarToH5(outputPath, "denoise_method", method) ||
        (method == 1 && (!NodeUtils::writeScalarToH5(outputPath, "denoise_slope_pre_win", slopePrefilterWindow) ||
                         !NodeUtils::writeScalarToH5(outputPath, "denoise_slope_win", slopeWindow)))) {
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
    if (!copyCompatibleCoherence(inputPath, outputPath, filteredPhase, error)) {
        return false;
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

        emit updateProcess(10 + i * 80 / imageCount,
                            QString("Filtering image %1/%2").arg(i + 1).arg(imageCount));

        Mat phase;
        if (!NodeUtils::readMatFromH5(inputPath, "phase", phase, CV_64F)) {
            emit errorProcess(QStringLiteral("Failed to read input phase data: ") + inputPath);
            return;
        }

        Mat filteredPhase;
        int result = -1;
        if (method == 1) {
            result = filter.slope_adaptive_filter(phase, filteredPhase, para.at(1), para.at(0), denoiseProgressCallback);
        } else if (method == 2) {
            result = filter.Goldstein_filter(phase, filteredPhase, alpha, para.at(2), para.at(3), denoiseProgressCallback);
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

        int offsetRow = 0;
        int offsetCol = 0;
        QString writeError;
        if (!writeDenoisedPhase(conversion, inputPath, outputPath, savePath, filteredPhase,
                                method, para.at(0), para.at(1), offsetRow, offsetCol, writeError)) {
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
