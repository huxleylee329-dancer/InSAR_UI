#ifndef BM3DENHANCEMENTTASK_H
#define BM3DENHANCEMENTTASK_H

#include <QObject>
#include <QRunnable>
#include <QStringList>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include "ClutterSuppressionAlgorithms.h"

// BM3D增强类型枚举
enum class EnhancementType {
    SpeckleDenoise,      // 斑点去噪
    ClutterSuppression   // 杂波抑制
};

class BM3DEnhancementTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    BM3DEnhancementTask(
        EnhancementType type,
        QStringList inputPaths,
        QStringList outputPaths,
        bool allowSkipOnError,
        ClutterSuppressionParameters clutterParameters =
            ClutterSuppressionParameters());

    void stop();
    void resolveErrorDecision(quint64 requestId, bool skip);
    void run() override;
    bool isStopped() const noexcept { return m_stopFlag.load(std::memory_order_relaxed); }

signals:
    void cancelled();
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void askUserError(quint64 requestId, QString error_msg);
    void outputsGenerated(QStringList outputPaths);

private:
    bool waitForErrorDecision(const QString& errorMessage, bool& skip);

    bool processBM3DEnhancement(
        QString inputPath,
        QString outputPath,
        QString& outError,
        int baseProgress,
        int progressStep
    );

    EnhancementType m_type;
    QStringList m_inputPaths;
    QStringList m_outputPaths;
    bool m_allowSkipOnError = false;

    std::atomic_bool m_stopFlag{false};
    QMutex m_decisionMutex;
    QWaitCondition m_decisionReady;
    quint64 m_nextRequestId = 0;
    quint64 m_pendingRequestId = 0;
    bool m_hasDecision = false;
    bool m_skipCurrentFile = false;
    ClutterSuppressionParameters m_clutterParameters;
};

#endif // BM3DENHANCEMENTTASK_H
