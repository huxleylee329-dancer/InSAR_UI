#include "S1FrameMergeWorker.h"
#include <Utils.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <atomic>

namespace {
std::atomic<S1FrameMergeWorker*> g_frameMergeWorker{ nullptr };

bool __stdcall frameMergeProgressCallback(int progress, const char* message)
{
    S1FrameMergeWorker* worker = g_frameMergeWorker.load(std::memory_order_acquire);
    if (!worker || worker->isStopRequested()) {
        return false;
    }

    const QString text = message ? QString::fromLocal8Bit(message) : QString();
    emit worker->updateProcess(30 + progress * 60 / 100,
        QStringLiteral("Merging frames: %1% %2").arg(progress).arg(text));
    return true;
}
}

S1FrameMergeWorker::S1FrameMergeWorker(QObject* parent)
    : BaseWorker(parent)
{
}

S1FrameMergeWorker::~S1FrameMergeWorker()
{
}

void S1FrameMergeWorker::S1_frame_merge(
    QString projectName,
    QString savePath,
    QString dstNode,
    QString firstH5Path,
    QString secondH5Path)
{
    if (QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    InSARLogManager::LogInfo("S1FrameMergeWorker",
        QString("S1_frame_merge started. Project: %1").arg(projectName));
    if (savePath.isEmpty() || firstH5Path.isEmpty() || secondH5Path.isEmpty()) {
        emit errorProcess(QStringLiteral("Input image file was not found."));
        return;
    }
    if (!QFileInfo::exists(firstH5Path) || !QFileInfo::exists(secondH5Path)) {
        emit errorProcess(QStringLiteral("Input image file does not exist."));
        return;
    }

    QDir dir(savePath);
    if (!dir.exists(dstNode) && !dir.mkdir(dstNode)) {
        emit errorProcess(QStringLiteral("Unable to create output directory."));
        return;
    }

    InSARLogManager::LogInfo("S1FrameMergeWorker",
        QString("Selected images for merge: IW1=%1, IW2=%2").arg(firstH5Path).arg(secondH5Path));
    const QFileInfo fileinfo1(firstH5Path);
    const QFileInfo fileinfo2(secondH5Path);
    const QString filename = fileinfo1.baseName() + "_" + fileinfo2.baseName();
    emit updateProcess(30, QStringLiteral("Merging frames..."));
    InSARLogManager::LogInfo("S1FrameMergeWorker",
        QString("Merging frames into: %1").arg(filename + ".h5"));

    Utils util;
    const QString mergedH5Path = savePath + "/" + dstNode + "/" + filename + ".h5";
    const QString previewPath = savePath + "/" + dstNode + "/" + filename + ".jpg";
    const auto finishCancelled = [this, &mergedH5Path, &previewPath]() {
        QFile::remove(mergedH5Path);
        QFile::remove(previewPath);
        emit cancelled();
    };

    int ret = 0;
    {
        NodeUtils::Hdf5Locker locker;
        g_frameMergeWorker.store(this, std::memory_order_release);
        ret = util.S1_frame_merge(firstH5Path.toStdString().c_str(), secondH5Path.toStdString().c_str(),
            mergedH5Path.toStdString().c_str(), frameMergeProgressCallback);
        g_frameMergeWorker.store(nullptr, std::memory_order_release);
    }
    if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        finishCancelled();
        return;
    }
    if (ret < 0) {
        InSARLogManager::LogError("S1FrameMergeWorker", "Frame merge input is invalid.");
        emit errorProcess(QStringLiteral("Frame merge input is invalid."));
        return;
    }

    InSARLogManager::LogInfo("S1FrameMergeWorker", "Frames merged successfully. Generating preview image...");
    {
        NodeUtils::Hdf5Locker locker;
        NodeUtils::generateJpgPreviewFromH5(mergedH5Path, previewPath, "complex");
    }
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        finishCancelled();
        return;
    }

    emit updateProcess(90, QStringLiteral("Finalizing frame merge..."));
    emit sendResult(dstNode, filename, mergedH5Path, savePath, projectName);
    emit updateProcess(100, QStringLiteral("Completed."));
    InSARLogManager::LogInfo("S1FrameMergeWorker", "S1_frame_merge completed successfully.");
    emit endProcess();
}
