#include "BaseImportWorker.h"
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

BaseImportWorker::BaseImportWorker(const QString& satelliteName, QObject* parent)
    : BaseWorker(parent)
    , m_satelliteName(satelliteName)
{
}

BaseImportWorker::~BaseImportWorker()
{
}

void BaseImportWorker::updateImportProgress(int percent, const QString& message)
{
    emit updateProcess(percent, message.isEmpty() ? QStringLiteral("Importing...") : message);
}

void BaseImportWorker::handleError(const QString& errorMsg, const QString& h5Path, const QString& dirPath)
{
    InSARLogManager::LogError(m_satelliteName + "ImportWorker", QString("Task failed: ") + errorMsg);
    QFile::remove(h5Path);
    QDir(dirPath).removeRecursively();
    emit errorProcess(errorMsg);
}

void BaseImportWorker::import_patch(
    const QString& savepath,
    const std::vector<ImportTask>& tasks,
    const QString& dstNode)
{
    if (savepath.isEmpty() || dstNode.isEmpty() || tasks.empty()) {
        InSARLogManager::LogError(m_satelliteName + "ImportWorker", "Invalid parameter arguments.");
        emit endProcess();
        return;
    }

    QDir dir(savepath);
    if (!dir.exists(dstNode) && !dir.mkpath(dstNode)) {
        emit errorProcess(QStringLiteral("Failed to create the output directory."));
        return;
    }

    const int imageCount = static_cast<int>(tasks.size());
    QStringList outputNames;
    QStringList outputPaths;
    InSARLogManager::LogInfo(m_satelliteName + "ImportWorker",
        QString("Task started: import_patch. Target Node: %1, Total Images: %2")
            .arg(dstNode).arg(imageCount));
    emit updateProcess(2, QStringLiteral("Starting import..."));

    for (int i = 0; i < imageCount; ++i) {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            break;

        const ImportTask& task = tasks[i];
        const QString h5Path = savepath + "/" + dstNode + "/" + task.filename + ".h5";
        const int startRange = static_cast<int>(double(i) / imageCount * 90.0);
        const int endRange = static_cast<int>(double(i + 1) / imageCount * 90.0);
        const int progressMin = startRange;
        const int progressMax = startRange + static_cast<int>((endRange - startRange) * 0.8);
        QString errorMessage;
        bool converted = false;

        {
            NodeUtils::Hdf5Locker hdf5Locker;
            converted = convertToH5(task.arguments, h5Path, progressMin, progressMax, errorMessage);
        }

        if (!converted || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            handleError(errorMessage.isEmpty() ? QStringLiteral("Conversion to H5 failed or interrupted.") : errorMessage,
                h5Path, savepath + "/" + dstNode);
            return;
        }

        const QFileInfo h5Info(h5Path);
        const QString jpgPath = h5Info.absolutePath() + "/" + h5Info.baseName() + ".jpg";
        {
            NodeUtils::Hdf5Locker hdf5Locker;
            NodeUtils::generateJpgPreviewFromH5WithProgress(h5Path, jpgPath, previewDataType(),
                [this, progressMax, endRange](int current, int total) {
                    const int progress = total > 0
                        ? progressMax + static_cast<int>((endRange - progressMax) * double(current) / total)
                        : progressMax;
                    emit updateProcess(progress, QStringLiteral("Generating preview..."));
                });
        }

        outputNames.append(task.filename);
        outputPaths.append(h5Path);
        emit updateProcess(endRange, QStringLiteral("Importing..."));
    }

    emit updateProcess(100, QStringLiteral("Import completed"));
    emit outputsGenerated(dstNode, outputNames, outputPaths, previewDataType(), satelliteFormatTag());
    InSARLogManager::LogInfo(m_satelliteName + "ImportWorker", "Task completed: import_patch");
    emit endProcess();
}
