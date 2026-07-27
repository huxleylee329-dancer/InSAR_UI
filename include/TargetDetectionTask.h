#ifndef TARGETDETECTIONTASK_H
#define TARGETDETECTIONTASK_H

#include <QObject>
#include <QRunnable>
#include <QStringList>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>

class TargetDetectionTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    TargetDetectionTask(QStringList imagePaths, QString modelPath, float thresholdValue);
    
    // Stop the running task
    void stop();

    void resolveErrorDecision(quint64 requestId, bool skip);
    
    // The main execution method
    void run() override;

signals:
    void cancelled();
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendTargetDetectionResult(int imageIndex, bool success, float shipProb, QString resultText, QString errorMsg);
    void askUserError(quint64 requestId, QString error_msg);

private:
    bool waitForErrorDecision(const QString& errorMessage, bool& skip);

    QStringList m_imagePaths;
    QString m_modelPath;
    float m_thresholdValue;
    
    std::atomic_bool m_stopFlag{false};
    QMutex m_decisionMutex;
    QWaitCondition m_decisionReady;
    quint64 m_nextRequestId = 0;
    quint64 m_pendingRequestId = 0;
    bool m_hasDecision = false;
    bool m_skipCurrentFile = false;
};

#endif // TARGETDETECTIONTASK_H
