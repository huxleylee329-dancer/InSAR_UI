#include "InSARLogManager.h"
#include "TargetDetectionTask.h"
#include "TargetDetection.h"
#include "SARProcessor.h"
#include <QFileInfo>
#include <QDebug>

namespace {
bool __stdcall isCancellationRequested(void* context)
{
    return static_cast<std::atomic_bool*>(context)->load(std::memory_order_relaxed);
}
}

TargetDetectionTask::TargetDetectionTask(QStringList imagePaths, QString modelPath, float thresholdValue)
    : m_imagePaths(imagePaths)
    , m_modelPath(modelPath)
    , m_thresholdValue(thresholdValue)
    , m_stopFlag(false)
{
    // QRunnable objects delete themselves by default when autoDelete() is true.
    // It is true by default.
}

void TargetDetectionTask::stop()
{
    m_stopFlag.store(true, std::memory_order_relaxed);
}

void TargetDetectionTask::run()
{
    for (int i = 0; i < m_imagePaths.size(); ++i) {
        if (m_stopFlag.load(std::memory_order_relaxed)) {
            emit cancelled();
            return;
        }

        QString msg = QStringLiteral("正在运行推理 %1/%2...").arg(i + 1).arg(m_imagePaths.size());
        int overallProgress = i * 100 / m_imagePaths.size();
        emit updateProcess(overallProgress, msg);

        float shipProb = 0.0f;
        QString resultText;
        char resultBuf[256] = {0};

        const int ret = SARProcessor::DetectShip(
            m_imagePaths[i].toLocal8Bit().constData(),
            m_modelPath.toLocal8Bit().constData(),
            m_thresholdValue,
            shipProb,
            resultBuf,
            sizeof(resultBuf),
            &isCancellationRequested,
            &m_stopFlag,
            nullptr,
            nullptr
        );

        if (ret == -2 || m_stopFlag.load(std::memory_order_relaxed)) {
            emit cancelled();
            return;
        }

        if (ret != 0) {
            QString errorMsg = QString::fromLocal8Bit(resultBuf);
            bool skip = false;
            QString errMsg = QStringLiteral("处理 %1 时发生错误: %2\n是否跳过并继续处理其余文件？").arg(QFileInfo(m_imagePaths[i]).fileName(), errorMsg);
            
            // Blocking signal emit expects user interaction
            emit askUserError(errMsg, &skip);
            
            if (!skip) {
                InSARLogManager::LogError("MyThread", QStringLiteral("批处理在 %1 处停止").arg(QFileInfo(m_imagePaths[i]).fileName()));
                emit errorProcess(QStringLiteral("批处理在 %1 处停止").arg(QFileInfo(m_imagePaths[i]).fileName()));
                emit sendTargetDetectionResult(i, false, 0.0f, "", errorMsg);
                return;
            }
            emit sendTargetDetectionResult(i, false, 0.0f, "", errorMsg);
            continue;
        }

        resultText = QString::fromLocal8Bit(resultBuf);
        emit sendTargetDetectionResult(i, true, shipProb, resultText, "");
    }

    emit updateProcess(100, QStringLiteral("目标检测完成"));
    emit endProcess();
}
