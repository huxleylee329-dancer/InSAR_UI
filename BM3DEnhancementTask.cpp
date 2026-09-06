#include "InSARLogManager.h"
#include <memory>
#include "BM3DEnhancementTask.h"
#include "icon_source.h"
#include "SARProcessor.h"
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <opencv2/opencv.hpp>

namespace {
bool __stdcall isCancellationRequested(void* context)
{
    return static_cast<std::atomic_bool*>(context)->load(std::memory_order_relaxed);
}
}

BM3DEnhancementTask::BM3DEnhancementTask(
    EnhancementType type,
    QStringList inputPaths,
    QStringList outputPaths,
    bool allowSkipOnError,
    ClutterSuppressionParameters clutterParameters
)
    : m_type(type)
    , m_inputPaths(inputPaths)
    , m_outputPaths(outputPaths)
    , m_allowSkipOnError(allowSkipOnError)
    , m_stopFlag(false)
    , m_clutterParameters(clutterParameters)
{
    // QObject lifetime is managed on its affinity (UI) thread.  QRunnable's
    // default auto-delete would destroy this object on a pool thread and leave
    // node-side task pointers dangling until queued terminal signals run.
    setAutoDelete(false);
}

void BM3DEnhancementTask::stop()
{
    m_stopFlag.store(true, std::memory_order_relaxed);
    QMutexLocker locker(&m_decisionMutex);
    m_decisionReady.wakeAll();
}

void BM3DEnhancementTask::resolveErrorDecision(quint64 requestId, bool skip)
{
    QMutexLocker locker(&m_decisionMutex);
    if (requestId != m_pendingRequestId || m_hasDecision) {
        return;
    }
    m_skipCurrentFile = skip;
    m_hasDecision = true;
    m_decisionReady.wakeAll();
}

bool BM3DEnhancementTask::waitForErrorDecision(const QString& errorMessage, bool& skip)
{
    quint64 requestId = 0;
    {
        QMutexLocker locker(&m_decisionMutex);
        if (isStopped()) {
            return false;
        }
        requestId = ++m_nextRequestId;
        m_pendingRequestId = requestId;
        m_hasDecision = false;
        m_skipCurrentFile = false;
    }

    emit askUserError(requestId, errorMessage);

    QMutexLocker locker(&m_decisionMutex);
    while (!m_hasDecision && !isStopped()) {
        m_decisionReady.wait(&m_decisionMutex);
    }
    if (isStopped()) {
        m_pendingRequestId = 0;
        return false;
    }
    skip = m_skipCurrentFile;
    m_pendingRequestId = 0;
    return true;
}

void BM3DEnhancementTask::run()
{
    if (m_inputPaths.isEmpty() || m_inputPaths.size() != m_outputPaths.size()) {
        emit errorProcess(QStringLiteral("BM3D input and output paths are inconsistent."));
        return;
    }

    QStringList generatedOutputPaths;
    for (int i = 0; i < m_inputPaths.size(); ++i) {
        if (isStopped()) {
            emit cancelled();
            return;
        }

        int baseProgress = i * 100 / m_inputPaths.size();
        int progressStep = 100 / m_inputPaths.size();
        QString outError;
        bool ok = processBM3DEnhancement(
            m_inputPaths[i], m_outputPaths[i], outError, baseProgress, progressStep
        );

        if (isStopped()) {
            emit cancelled();
            return;
        }

        if (!ok) {
            const QString failedFile = QFileInfo(m_inputPaths[i]).fileName();
            if (!m_allowSkipOnError) {
                const QString error = QStringLiteral("批处理在 %1 处停止: %2").arg(failedFile, outError);
                InSARLogManager::LogError("BM3DEnhancementTask", error);
                emit errorProcess(error);
                return;
            }

            QString msg = QStringLiteral("处理 %1 时发生错误: %2\n是否跳过并继续处理其余文件？").arg(failedFile, outError);
            bool skip = false;
            if (!waitForErrorDecision(msg, skip)) {
                emit cancelled();
                return;
            }
            if (!skip) {
                InSARLogManager::LogError("BM3DEnhancementTask", QStringLiteral("批处理在 %1 处停止").arg(failedFile));
                emit errorProcess(QStringLiteral("批处理在 %1 处停止").arg(failedFile));
                return;
            }
        } else {
            generatedOutputPaths.append(m_outputPaths[i]);
        }

        int overallProgress = (i + 1) * 100 / m_inputPaths.size();
        emit updateProcess(overallProgress, QStringLiteral("批处理进度: %1/%2").arg(i + 1).arg(m_inputPaths.size()));
    }

    if (isStopped()) {
        emit cancelled();
        return;
    }

    emit outputsGenerated(generatedOutputPaths);
    emit endProcess();
}

bool BM3DEnhancementTask::processBM3DEnhancement(
    QString inputPath,
    QString outputPath,
    QString& outError,
    int baseProgress,
    int progressStep
)
{
    // 根据增强类型动态配置
    QString processMsg;

    if (m_type == EnhancementType::SpeckleDenoise) {
        processMsg = QStringLiteral("执行BM3D去噪 (可能耗时较长)...");
    } else { // ClutterSuppression
        processMsg = QStringLiteral("Executing %1 clutter suppression...")
            .arg(QString::fromLatin1(
                ClutterSuppressionAlgorithms::methodName(
                    m_clutterParameters.method)));
    }

    emit updateProcess(baseProgress + progressStep * 0.0, QStringLiteral("加载图像..."));

    cv::Mat inputGray = cv::imread(inputPath.toStdString(), cv::IMREAD_GRAYSCALE);
    if (inputGray.empty()) {
        outError = QStringLiteral("无法读取输入图像");
        return false;
    }
    if (isStopped()) {
        return false;
    }

    emit updateProcess(baseProgress + progressStep * 0.4, processMsg);

    cv::Mat output8U;
    cv::Mat targetMask;

    if (m_type == EnhancementType::ClutterSuppression &&
        m_clutterParameters.method != ClutterSuppressionMethod::BM3D)
    {
        if (isStopped()) {
            return false;
        }

        const ClutterSuppressionResult result =
            ClutterSuppressionAlgorithms::process(
                inputGray, m_clutterParameters);

        if (isStopped()) {
            return false;
        }

        output8U = result.suppressedImage;
        targetMask = result.targetMask;

        if (output8U.empty()) {
            outError = QStringLiteral(
                "Clutter suppression returned an empty image");
            return false;
        }
    }
    else
    {
        const int ret = SARProcessor::DenoiseGray(
            inputGray,
            0.0,
            output8U,
            &isCancellationRequested,
            &m_stopFlag,
            nullptr,
            nullptr);

        if (ret == -2 || isStopped()) {
            return false;
        }

        if (ret != 0 || output8U.empty()) {
            outError = QStringLiteral("BM3D processing failed");
            return false;
        }
    }

    emit updateProcess(baseProgress + progressStep * 0.8, QStringLiteral("后处理及保存..."));
    if (isStopped()) {
        return false;
    }

    if (!cv::imwrite(outputPath.toStdString(), output8U)) {
        outError = QStringLiteral("Failed to save processed image");
        return false;
    }

    QString maskPath;

    if (!targetMask.empty()) {
        const QFileInfo outputInfo(outputPath);
        maskPath = outputInfo.absolutePath() + "/" +
                outputInfo.completeBaseName() +
                "_mask.png";

        if (!cv::imwrite(maskPath.toStdString(), targetMask)) {
            QFile::remove(outputPath);
            outError = QStringLiteral("Failed to save CFAR target mask");
            return false;
        }
    }

    if (isStopped()) {
        QFile::remove(outputPath);

        if (!maskPath.isEmpty()) {
            QFile::remove(maskPath);
        }

        return false;
    }

    emit updateProcess(100, QStringLiteral("处理完成"));
    return true;
}
