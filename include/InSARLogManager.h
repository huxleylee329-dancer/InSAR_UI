#ifndef INSARLOGMANAGER_H
#define INSARLOGMANAGER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMetaType>

struct LogEntry {
    int level; // 0: Info, 1: Warning, 2: Error
    QString source;
    QString message;
    QString timestamp;
    QString rawLine;
};
Q_DECLARE_METATYPE(LogEntry)

class InSARLogManager : public QObject
{
    Q_OBJECT

public:
    enum LogLevel {
        LevelInfo = 0,
        LevelWarning,
        LevelError
    };

    static InSARLogManager& instance();

    void setProjectDirectory(const QString& projectDir);

    static void LogInfo(const QString& source, const QString& message);
    static void LogWarning(const QString& source, const QString& message);
    static void LogError(const QString& source, const QString& message);
    static void Log(LogLevel level, const QString& source, const QString& message);

    QString getCurrentLogFilePath() const;

signals:
    void logAppended(const LogEntry& entry);

private:
    InSARLogManager();
    ~InSARLogManager();
    Q_DISABLE_COPY(InSARLogManager)

    QString m_logFilePath;
    QMutex m_mutex;
};

#endif // INSARLOGMANAGER_H
