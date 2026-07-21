#ifndef INSARLOGMANAGER_H
#define INSARLOGMANAGER_H

#include <QObject>
#include <QDateTime>
#include <QFlags>
#include <QList>
#include <QMetaType>
#include <QMutex>
#include <QString>

enum class LogTarget : quint8 {
    None = 0,
    UserProjectLog = 1,
    DebugConsole = 2,
    DiagnosticFile = 4
};
Q_DECLARE_FLAGS(LogTargets, LogTarget)
Q_DECLARE_OPERATORS_FOR_FLAGS(LogTargets)

struct TaskLogContext {
    QString runId;
    QString nodeId;
    QString displayName;
    QString scope = QStringLiteral("application");
};

struct LogEntry {
    int level = 0;
    LogTargets targets;
    QString source;
    QString category;
    QString message;
    QString timestamp;
    QString runId;
    QString nodeId;
    QString displayName;
    QString scope = QStringLiteral("application");
    QString phase;
    QString status;
    qint64 elapsedMs = -1;
    quint64 sequence = 0;
};
Q_DECLARE_METATYPE(LogEntry)

class ScopedTaskLogContext
{
public:
    explicit ScopedTaskLogContext(const TaskLogContext& context);
    ~ScopedTaskLogContext();

    ScopedTaskLogContext(const ScopedTaskLogContext&) = delete;
    ScopedTaskLogContext& operator=(const ScopedTaskLogContext&) = delete;
};

class InSARLogManager : public QObject
{
    Q_OBJECT

public:
    enum LogLevel {
        LevelInfo = 0,
        LevelWarning,
        LevelError,
        LevelDebug
    };

    static InSARLogManager& instance();

    void setProjectDirectory(const QString& projectDir);
    void configureDiagnosticSinks(bool debugConsoleEnabled, bool diagnosticFileEnabled);
    void setActiveWorkflowRunId(const QString& runId);

    static void LogInfo(const QString& source, const QString& message);
    static void LogWarning(const QString& source, const QString& message);
    static void LogError(const QString& source, const QString& message);
    static void LogDebug(const QString& source, const QString& message,
                         const QString& category = QString());
    static void LogDiagnostic(LogLevel level, const QString& source, const QString& message,
                              LogTargets targets, const QString& category = QString(),
                              const QString& phase = QString(), const QString& status = QString(),
                              qint64 elapsedMs = -1);
    static void LogTaskEvent(const TaskLogContext& context, LogLevel level,
                             const QString& source, const QString& message,
                             LogTargets targets, const QString& phase = QString(),
                             const QString& status = QString(), qint64 elapsedMs = -1);
    static void Log(LogLevel level, const QString& source, const QString& message);

    static TaskLogContext currentTaskContext();
    static void installQtMessageHandler();
    static bool flushRun(const QString& runId, int timeoutMs = 5000);
    static bool flushAll(int timeoutMs = 5000);
    static QString formatEntry(const LogEntry& entry);

    QString getCurrentLogFilePath() const;
    QString getDiagnosticLogFilePath() const;

signals:
    void logAppended(const LogEntry& entry);

private:
    class DiagnosticDispatcher;

    InSARLogManager();
    ~InSARLogManager();
    Q_DISABLE_COPY(InSARLogManager)

    void appendEntry(LogEntry entry);
    void publishUserEntry(const LogEntry& entry);
    void writeEntry(const LogEntry& entry);
    void writeDroppedDiagnosticSummary(quint64 count);
    void writeEmergency(const QString& line) const;
    bool appendLine(const QString& path, const QString& line, bool rotate) const;

    mutable QMutex m_configurationMutex;
    QString m_logFilePath;
    QString m_diagnosticLogFilePath;
    bool m_debugConsoleEnabled = false;
    bool m_diagnosticFileEnabled = false;
    QString m_activeWorkflowRunId;
    quint64 m_nextSequence = 0;
    DiagnosticDispatcher* m_dispatcher = nullptr;
};

#endif // INSARLOGMANAGER_H
