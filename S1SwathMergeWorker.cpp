#include "S1SwathMergeWorker.h"
#include <Utils.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <atomic>

namespace {
std::atomic<S1SwathMergeWorker*> g_swathMergeWorker{ nullptr };

bool __stdcall swathMergeProgressCallback(int progress, const char* message)
{
    S1SwathMergeWorker* worker = g_swathMergeWorker.load(std::memory_order_acquire);
    if (!worker || worker->isStopRequested()) {
        return false;
    }

    const QString text = message ? QString::fromLocal8Bit(message) : QString();
    emit worker->updateProcess(30 + progress * 60 / 100,
        QStringLiteral("Merging swaths: %1% %2").arg(progress).arg(text));
    return true;
}
}

S1SwathMergeWorker::S1SwathMergeWorker(QObject* parent)
    : BaseWorker(parent)
{
}

S1SwathMergeWorker::~S1SwathMergeWorker()
{
}

void S1SwathMergeWorker::S1_swath_merge(
    QString projectName,
    QString savePath,
    QString dstNode,
    QString firstH5Path,
    QString secondH5Path,
    QString thirdH5Path)
{
    if (QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    InSARLogManager::LogInfo("S1SwathMergeWorker",
        QString("S1_swath_merge started. Project: %1").arg(projectName));
    if (savePath.isEmpty() || firstH5Path.isEmpty() || secondH5Path.isEmpty() || thirdH5Path.isEmpty()) {
        emit errorProcess(QStringLiteral("Input image file was not found."));
        return;
    }
    if (!QFileInfo::exists(firstH5Path) || !QFileInfo::exists(secondH5Path) ||
        !QFileInfo::exists(thirdH5Path)) {
        emit errorProcess(QStringLiteral("Input image file does not exist."));
        return;
    }

    QDir dir(savePath);
    if (!dir.exists(dstNode) && !dir.mkdir(dstNode)) {
        emit errorProcess(QStringLiteral("Unable to create output directory."));
        return;
    }

    const QString filename = QStringLiteral("merged_phase");
    const QString mergedH5Path = savePath + "/" + dstNode + "/" + filename + ".h5";
    const QString previewPath = savePath + "/" + dstNode + "/" + filename + ".jpg";
    const auto finishCancelled = [this, &mergedH5Path, &previewPath]() {
        QFile::remove(mergedH5Path);
        QFile::remove(previewPath);
        emit cancelled();
    };

    InSARLogManager::LogInfo("S1SwathMergeWorker",
        QString("Selected images for merge: IW1=%1, IW2=%2, IW3=%3")
            .arg(firstH5Path).arg(secondH5Path).arg(thirdH5Path));
    emit updateProcess(30, QStringLiteral("Merging swaths..."));

    Utils util;
    int ret = 0;
    {
        NodeUtils::Hdf5Locker locker;
        g_swathMergeWorker.store(this, std::memory_order_release);
        ret = util.S1_subswath_merge(firstH5Path.toStdString().c_str(), secondH5Path.toStdString().c_str(),
            thirdH5Path.toStdString().c_str(), mergedH5Path.toStdString().c_str(), swathMergeProgressCallback);
        g_swathMergeWorker.store(nullptr, std::memory_order_release);
    }
    if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        finishCancelled();
        return;
    }
    if (ret < 0) {
        InSARLogManager::LogError("S1SwathMergeWorker", "Swath merge input is invalid.");
        emit errorProcess(QStringLiteral("Swath merge input is invalid."));
        return;
    }

    InSARLogManager::LogInfo("S1SwathMergeWorker", "Swaths merged successfully. Generating preview image...");
    {
        NodeUtils::Hdf5Locker locker;
        NodeUtils::generateJpgPreviewFromH5(mergedH5Path, previewPath, "phase");
    }
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        finishCancelled();
        return;
    }

    emit updateProcess(90, QStringLiteral("Finalizing swath merge..."));
    emit sendResult(dstNode, filename, mergedH5Path, savePath, projectName);
    emit updateProcess(100, QStringLiteral("Completed."));
    InSARLogManager::LogInfo("S1SwathMergeWorker", "S1_swath_merge completed successfully.");
    emit endProcess();
}
