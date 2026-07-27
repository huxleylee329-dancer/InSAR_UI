#include "DemWorker.h"

#include <Dem.h>
#include <FormatConversion.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Dem_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Dem.lib")
#endif

using namespace cv;
using namespace std;

namespace {

thread_local DemWorker* activeWorker = nullptr;
thread_local int activeImageIndex = 0;
thread_local int totalImageCount = 1;
thread_local int lastLoggedProgress = -10;

bool __stdcall demProgressCallback(int progress, const char* message)
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

    if (!activeWorker) {
        return true;
    }
    if (activeWorker->isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        return false;
    }

    const int startProgress = 10 + activeImageIndex * 80 / totalImageCount;
    const int endProgress = 10 + (activeImageIndex + 1) * 80 / totalImageCount;
    const int mappedProgress = startProgress + progress * (endProgress - startProgress) / 100;
    const QString messageText = message ? QString::fromLocal8Bit(message) : QString();
    emit activeWorker->updateProcess(mappedProgress,
        QString("Generating DEM %1/%2 (%3%) %4")
            .arg(activeImageIndex + 1).arg(totalImageCount).arg(progress).arg(messageText));

    if (progress == 0 || progress == 100 || progress - lastLoggedProgress >= 10) {
        InSARLogManager::LogInfo("DemWorker", QString("DEM progress: %1% (total %2%)")
            .arg(progress).arg(mappedProgress));
        lastLoggedProgress = progress;
    }
    return true;
}

class WorkerResetGuard
{
public:
    WorkerResetGuard(DemWorker* worker, int imageCount)
    {
        activeWorker = worker;
        activeImageIndex = 0;
        totalImageCount = imageCount;
        lastLoggedProgress = -10;
    }

    ~WorkerResetGuard()
    {
        activeWorker = nullptr;
        activeImageIndex = 0;
        totalImageCount = 1;
        lastLoggedProgress = -10;
    }
};

} // namespace

DemWorker::DemWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<DemFileResult>("DemFileResult");
}

DemWorker::~DemWorker()
{
}

void DemWorker::Dem(int method,
                    int times,
                    QString savePath,
                    QString outputNode,
                    QStringList phaseNames,
                    QStringList phasePaths)
{
    const auto finishCancelled = [this]() { emit cancelled(); };
    if (savePath.isEmpty() || outputNode.isEmpty() || phaseNames.isEmpty() ||
        phaseNames.size() != phasePaths.size()) {
        emit errorProcess(QStringLiteral("Invalid DEM parameters or input paths."));
        return;
    }
    if (method != 1) {
        emit errorProcess(QStringLiteral("Unsupported DEM generation method."));
        return;
    }

    InSARLogManager::LogInfo("DemWorker", QString("DEM generation started: %1 images.").arg(phasePaths.size()));
    const QString outputDirectory = savePath + "/" + outputNode;
    QDir targetDirectory(outputDirectory);
    if (targetDirectory.exists()) {
        targetDirectory.removeRecursively();
    }
    if (!QDir(savePath).mkdir(outputNode)) {
        emit errorProcess(QStringLiteral("Failed to create DEM output directory."));
        return;
    }

    WorkerResetGuard resetGuard(this, phasePaths.size());
    ::Dem dem;
    FormatConversion conversion;

    for (int i = 0; i < phasePaths.size(); ++i) {
        activeImageIndex = i;
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            finishCancelled();
            return;
        }

        QString inputH5 = phasePaths.at(i);
        if (QDir::isRelativePath(inputH5)) {
            inputH5 = savePath + "/" + inputH5;
        }
        const QString demName = phaseNames.at(i) + "_dem";
        const QString outputH5 = outputDirectory + "/" + demName + ".h5";
        emit updateProcess(10 + i * 80 / phasePaths.size(),
                            QString("Generating DEM %1/%2").arg(i + 1).arg(phasePaths.size()));

        int offsetRow = 0;
        int offsetCol = 0;
        {
            NodeUtils::Hdf5Locker locker;
            Mat phase;
            if (!NodeUtils::readMatFromH5(inputH5, "phase", phase)) {
                emit errorProcess(QStringLiteral("Failed to read input phase data: ") + inputH5);
                return;
            }

            Mat flatPhaseCoefficient;
            if (!NodeUtils::readMatFromH5(inputH5, "flat_phase_coefficient", flatPhaseCoefficient)) {
                emit errorProcess(QStringLiteral("Input phase file has no flat_phase_coefficient: ") + inputH5);
                return;
            }

            Mat phaseDem;
            const int result = dem.dem_newton_iter(inputH5.toStdString().c_str(), phaseDem,
                savePath.toStdString().c_str(), times, 1, demProgressCallback);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (result < 0) {
                emit errorProcess(QStringLiteral("DEM Newton iteration failed."));
                return;
            }

            if (conversion.creat_new_h5(outputH5.toStdString().c_str()) < 0 ||
                !NodeUtils::writeMatToH5(outputH5, "dem", phaseDem)) {
                emit errorProcess(QStringLiteral("Failed to create DEM output: ") + outputH5);
                return;
            }

            string sourcePath;
            Mat value;
            NodeUtils::readStringFromH5(inputH5, "source_1", sourcePath);
            conversion.write_str_to_h5(outputH5.toStdString().c_str(), "source_1", sourcePath.c_str());
            const QString masterPath = QDir::toNativeSeparators(savePath) + QString::fromStdString(sourcePath);
            NodeUtils::readStringFromH5(inputH5, "source_2", sourcePath);
            conversion.write_str_to_h5(outputH5.toStdString().c_str(), "source_2", sourcePath.c_str());

            if (NodeUtils::readMatFromH5(inputH5, "flat_phase_coefficient", value) ||
                NodeUtils::readMatFromH5(inputH5, "flat_phase_coefficientficient", value)) {
                NodeUtils::writeMatToH5(outputH5, "flat_phase_coefficient", value);
            }
            const char* const copiedDatasets[] = {
                "range_len", "azimuth_len", "multilook_rg", "multilook_az"
            };
            for (const char* dataset : copiedDatasets) {
                NodeUtils::readMatFromH5(inputH5, dataset, value);
                NodeUtils::writeMatToH5(outputH5, dataset, value);
            }

            Mat offsetValue = Mat::zeros(1, 1, CV_32SC1);
            NodeUtils::readMatFromH5(masterPath, "offset_row", offsetValue);
            offsetRow = offsetValue.at<int>(0, 0);
            NodeUtils::readMatFromH5(masterPath, "offset_col", offsetValue);
            offsetCol = offsetValue.at<int>(0, 0);
        }

        DemFileResult fileResult;
        fileResult.demName = demName;
        fileResult.relativeDemPath = "/" + outputNode + "/" + demName + ".h5";
        fileResult.absoluteDemPath = outputH5;
        fileResult.offsetRow = offsetRow;
        fileResult.offsetCol = offsetCol;
        emit demFileGenerated(fileResult);
    }

    InSARLogManager::LogInfo("DemWorker", "DEM generation completed.");
    emit endProcess();
}
