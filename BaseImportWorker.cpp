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

    // 预检（磁盘）：本批的转换与预览都要落盘。这里【刻意不做落盘量估算】——
    // task.arguments 里可能只有 XML（例如 TSX 真正被消费的栅格是 IMAGEDATA/*.cos，另有其文件），
    // 按它求和会严重低估。所以只做「安全余量」检查：可用空间须高于该卷容量的 5%
    //（Config.ini 的 [Storage] MinFreePercent / MinFreeBytes 可覆盖）。
    // 它挡的是「盘快满了还开跑」：那种情况下会在第 k 景失败并整批回滚，
    // 前 k-1 景已经完成的分钟级转换全部作废。宁可开跑前就拒绝。
    {
        QString diskError;
        if (!NodeUtils::ensureSufficientDiskSpace(savepath, 0, &diskError)) {
            emit errorProcess(diskError);
            return;
        }
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
