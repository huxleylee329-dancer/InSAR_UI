#ifndef TARGETDETECTIONTASK_H
#define TARGETDETECTIONTASK_H

#include <QObject>
#include <QRunnable>
#include <QStringList>
#include <atomic>

class TargetDetectionTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    TargetDetectionTask(QStringList imagePaths, QString modelPath, float thresholdValue);
    
    // Stop the running task
    void stop();
    
    // The main execution method
    void run() override;

signals:
    void cancelled();
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendTargetDetectionResult(int imageIndex, bool success, float shipProb, QString resultText, QString errorMsg);
    void askUserError(QString error_msg, bool* skip);

private:
    QStringList m_imagePaths;
    QString m_modelPath;
    float m_thresholdValue;
    
    std::atomic_bool m_stopFlag{false};
};

#endif // TARGETDETECTIONTASK_H
