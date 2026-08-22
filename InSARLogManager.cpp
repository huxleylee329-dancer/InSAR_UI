#include "InSARLogManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QTextStream>
#include <QThread>

#include <windows.h>

#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

namespace {

thread_local QList<TaskLogContext> g_taskContexts;
thread_local bool g_qtMessageHandlerActive = false;

LogTargets userTarget()
{
    return LogTargets(LogTarget::UserProjectLog);
}

LogTargets diagnosticTargets()
{
    return LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile;
}

LogTargets legacyTargets(InSARLogManager::LogLevel level)
{
    if (level == InSARLogManager::LevelInfo || level == InSARLogManager::LevelDebug) {
        return diagnosticTargets();
    }
    return userTarget() | diagnosticTargets();
}

QString levelName(int level)
{
    switch (level) {
    case InSARLogManager::LevelDebug: return QStringLiteral("DEBUG");
    case InSARLogManager::LevelWarning: return QStringLiteral("WARNING");
    case InSARLogManager::LevelError: return QStringLiteral("ERROR");
    case InSARLogManager::LevelInfo:
    default: return QStringLiteral("INFO");
    }
}

bool isTransformedGlyphMetricsWarning(QtMsgType type, const QString& message)
{
    static const QString prefix = QStringLiteral(
        "QWinFontEngine: unable to query transformed glyph metrics "
        "(GetGlyphOutline() failed, error 1003)");
    return type == QtCriticalMsg && message.startsWith(prefix);
}

bool shouldLogTransformedGlyphMetricsWarning()
{
    static std::mutex rateLimitMutex;
    static std::chrono::steady_clock::time_point lastLogged;

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(rateLimitMutex);
    if (lastLogged.time_since_epoch().count() != 0
        && now - lastLogged < std::chrono::seconds(5)) {
        return false;
    }

    lastLogged = now;
    return true;
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (g_qtMessageHandlerActive) {
        const QByteArray fallback = QStringLiteral("[Qt recursive message] %1\n").arg(message).toLocal8Bit();
        std::fputs(fallback.constData(), stderr);
        OutputDebugStringW(reinterpret_cast<LPCWSTR>(QString::fromLocal8Bit(fallback).utf16()));
        return;
    }

    g_qtMessageHandlerActive = true;
    const bool transformedGlyphMetricsWarning = isTransformedGlyphMetricsWarning(type, message);
    if (transformedGlyphMetricsWarning && !shouldLogTransformedGlyphMetricsWarning()) {
        g_qtMessageHandlerActive = false;
        return;
    }

    InSARLogManager::LogLevel level = InSARLogManager::LevelDebug;
    switch (type) {
    case QtDebugMsg: level = InSARLogManager::LevelDebug; break;
    case QtInfoMsg: level = InSARLogManager::LevelInfo; break;
    case QtWarningMsg: level = InSARLogManager::LevelWarning; break;
    case QtCriticalMsg:
    case QtFatalMsg: level = InSARLogManager::LevelError; break;
    }

    QString location;
    if (context.file && context.line > 0) {
        location = QStringLiteral("%1:%2").arg(QString::fromLocal8Bit(context.file)).arg(context.line);
    }
    if (context.function) {
        location += location.isEmpty() ? QString::fromLocal8Bit(context.function)
                                       : QStringLiteral(" (%1)").arg(QString::fromLocal8Bit(context.function));
    }

    QString source = QStringLiteral("Qt");
    if (!location.isEmpty()) {
        source += QStringLiteral("/") + location;
    }

    LogTargets targets = diagnosticTargets();
    if (type == QtCriticalMsg || type == QtFatalMsg) {
        targets |= LogTarget::UserProjectLog;
    }
    QString category = context.category
        ? QString::fromLocal8Bit(context.category) : QStringLiteral("qt");
    if (transformedGlyphMetricsWarning) {
        level = InSARLogManager::LevelWarning;
        targets = diagnosticTargets();
        category = QStringLiteral("qt.font");
    }
    InSARLogManager::LogDiagnostic(level, source, message, targets, category);
    if (type == QtFatalMsg) {
        InSARLogManager::flushAll(1000);
    }
    g_qtMessageHandlerActive = false;
}

} // namespace

class InSARLogManager::DiagnosticDispatcher
{
public:
    explicit DiagnosticDispatcher(InSARLogManager* owner)
        : m_owner(owner)
        , m_thread(&DiagnosticDispatcher::run, this)
    {
    }

    ~DiagnosticDispatcher()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_condition.notify_all();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void enqueue(const LogEntry& entry)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool droppable = entry.level == LevelDebug;
        if (droppable && m_queue.size() >= kMaximumQueuedEntries) {
            ++m_droppedDebugEntries;
            return;
        }

        m_queue.push_back(entry);
        if (!entry.runId.isEmpty()) {
            ++m_pendingByRun[entry.runId];
        }
        m_condition.notify_one();
    }

    bool flushRun(const QString& runId, int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_drained.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this, &runId]() {
            return m_pendingByRun.value(runId, 0) == 0;
        });
    }

    bool flushAll(int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_drained.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
            return m_queue.empty() && !m_writing;
        });
    }

private:
    void run()
    {
        for (;;) {
            LogEntry entry;
            quint64 dropped = 0;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this]() { return m_stopping || !m_queue.empty(); });
                if (m_stopping && m_queue.empty()) {
                    return;
                }

                entry = m_queue.front();
                m_queue.pop_front();
                m_writing = true;
            }

            m_owner->writeEntry(entry);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_writing = false;
                if (!entry.runId.isEmpty()) {
                    const int remaining = m_pendingByRun.value(entry.runId) - 1;
                    if (remaining <= 0) {
                        m_pendingByRun.remove(entry.runId);
                    } else {
                        m_pendingByRun.insert(entry.runId, remaining);
                    }
                }
                if (m_droppedDebugEntries > 0 && m_queue.size() < kMaximumQueuedEntries / 2) {
                    dropped = m_droppedDebugEntries;
                    m_droppedDebugEntries = 0;
                }
                m_drained.notify_all();
            }

            if (dropped > 0) {
                m_owner->writeDroppedDiagnosticSummary(dropped);
            }
        }
    }

    static const size_t kMaximumQueuedEntries = 4096;
    InSARLogManager* m_owner;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::condition_variable m_drained;
    std::deque<LogEntry> m_queue;
    QHash<QString, int> m_pendingByRun;
    std::thread m_thread;
    bool m_stopping = false;
    bool m_writing = false;
    quint64 m_droppedDebugEntries = 0;
};

ScopedTaskLogContext::ScopedTaskLogContext(const TaskLogContext& context)
{
    g_taskContexts.append(context);
}

ScopedTaskLogContext::~ScopedTaskLogContext()
{
    if (!g_taskContexts.isEmpty()) {
        g_taskContexts.removeLast();
    }
}

InSARLogManager& InSARLogManager::instance()
{
    static InSARLogManager inst;
    return inst;
}

InSARLogManager::InSARLogManager()
{
    qRegisterMetaType<LogEntry>("LogEntry");
    const QString applicationDir = QCoreApplication::applicationDirPath();
    m_logFilePath = QDir(applicationDir).absoluteFilePath(QStringLiteral("SatExplorer.log"));
    m_diagnosticLogFilePath = QDir(applicationDir).absoluteFilePath(QStringLiteral("diagnostic.log"));
    m_dispatcher = new DiagnosticDispatcher(this);
}

InSARLogManager::~InSARLogManager()
{
    if (m_dispatcher) {
        m_dispatcher->flushAll(2000);
    }
    delete m_dispatcher;
    m_dispatcher = nullptr;
}

void InSARLogManager::setProjectDirectory(const QString& projectDir)
{
    QMutexLocker locker(&m_configurationMutex);
    if (projectDir.isEmpty()) {
        const QDir applicationDir(QCoreApplication::applicationDirPath());
        m_logFilePath = applicationDir.absoluteFilePath(QStringLiteral("SatExplorer.log"));
        m_diagnosticLogFilePath = applicationDir.absoluteFilePath(QStringLiteral("diagnostic.log"));
    } else {
        const QDir project(projectDir);
        m_logFilePath = project.absoluteFilePath(QStringLiteral("project.log"));
        m_diagnosticLogFilePath = project.absoluteFilePath(QStringLiteral("diagnostic.log"));
    }
}

void InSARLogManager::configureDiagnosticSinks(bool debugConsoleEnabled, bool diagnosticFileEnabled)
{
    QMutexLocker locker(&m_configurationMutex);
    m_debugConsoleEnabled = debugConsoleEnabled;
    m_diagnosticFileEnabled = diagnosticFileEnabled;
}

void InSARLogManager::setActiveWorkflowRunId(const QString& runId)
{
    QMutexLocker locker(&m_configurationMutex);
    m_activeWorkflowRunId = runId;
}

void InSARLogManager::LogInfo(const QString& source, const QString& message)
{
    Log(LevelInfo, source, message);
}

void InSARLogManager::LogWarning(const QString& source, const QString& message)
{
    Log(LevelWarning, source, message);
}

void InSARLogManager::LogError(const QString& source, const QString& message)
{
    Log(LevelError, source, message);
}

void InSARLogManager::LogDebug(const QString& source, const QString& message, const QString& category)
{
    LogDiagnostic(LevelDebug, source, message, diagnosticTargets(), category);
}

void InSARLogManager::LogDiagnostic(LogLevel level, const QString& source, const QString& message,
                                    LogTargets targets, const QString& category,
                                    const QString& phase, const QString& status, qint64 elapsedMs)
{
    LogEntry entry;
    const TaskLogContext context = currentTaskContext();
    entry.level = level;
    entry.targets = targets;
    entry.source = source;
    entry.category = category;
    entry.message = message;
    entry.timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    entry.runId = context.runId;
    entry.nodeId = context.nodeId;
    entry.displayName = context.displayName;
    entry.scope = context.scope;
    entry.phase = phase;
    entry.status = status;
    entry.elapsedMs = elapsedMs;
    instance().appendEntry(entry);
}

void InSARLogManager::LogTaskEvent(const TaskLogContext& context, LogLevel level,
                                   const QString& source, const QString& message,
                                   LogTargets targets, const QString& phase,
                                   const QString& status, qint64 elapsedMs)
{
    const TaskLogContext fallback = currentTaskContext();
    LogEntry entry;
    entry.level = level;
    entry.targets = targets;
    entry.source = source;
    entry.message = message;
    entry.timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    entry.runId = context.runId.isEmpty() ? fallback.runId : context.runId;
    entry.nodeId = context.nodeId.isEmpty() ? fallback.nodeId : context.nodeId;
    entry.displayName = context.displayName.isEmpty() ? fallback.displayName : context.displayName;
    entry.scope = context.scope == QStringLiteral("application") && !fallback.scope.isEmpty()
        ? fallback.scope : context.scope;
    entry.phase = phase;
    entry.status = status;
    entry.elapsedMs = elapsedMs;
    instance().appendEntry(entry);
}

void InSARLogManager::Log(LogLevel level, const QString& source, const QString& message)
{
    LogDiagnostic(level, source, message, legacyTargets(level));
}

TaskLogContext InSARLogManager::currentTaskContext()
{
    if (!g_taskContexts.isEmpty()) return g_taskContexts.constLast();
    TaskLogContext context;
    InSARLogManager& manager = instance();
    QMutexLocker locker(&manager.m_configurationMutex);
    context.runId = manager.m_activeWorkflowRunId;
    return context;
}

void InSARLogManager::installQtMessageHandler()
{
    qInstallMessageHandler(qtMessageHandler);
}

bool InSARLogManager::flushRun(const QString& runId, int timeoutMs)
{
    if (runId.isEmpty()) {
        return flushAll(timeoutMs);
    }
    return instance().m_dispatcher->flushRun(runId, timeoutMs);
}

bool InSARLogManager::flushAll(int timeoutMs)
{
    return instance().m_dispatcher->flushAll(timeoutMs);
}

QString InSARLogManager::formatEntry(const LogEntry& entry)
{
    QString source = entry.displayName.isEmpty() ? entry.source : entry.displayName;
    if (source.isEmpty()) {
        source = QStringLiteral("application");
    }

    QStringList details;
    if (!entry.status.isEmpty()) details.append(QStringLiteral("status=%1").arg(entry.status));
    if (!entry.phase.isEmpty()) details.append(QStringLiteral("phase=%1").arg(entry.phase));
    if (entry.elapsedMs >= 0) details.append(QStringLiteral("elapsed_ms=%1").arg(entry.elapsedMs));
    if (!entry.runId.isEmpty()) details.append(QStringLiteral("run=%1").arg(entry.runId));
    if (!entry.nodeId.isEmpty()) details.append(QStringLiteral("node=%1").arg(entry.nodeId));
    if (entry.scope != QStringLiteral("application")) details.append(QStringLiteral("scope=%1").arg(entry.scope));
    if (!entry.category.isEmpty()) details.append(QStringLiteral("category=%1").arg(entry.category));

    QString message = entry.message;
    message.replace(QStringLiteral("\r\n"), QStringLiteral("\\n"));
    message.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    message.replace(QLatin1Char('\r'), QStringLiteral("\\r"));

    const QString suffix = details.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(details.join(QStringLiteral(", ")));
    return QStringLiteral("[%1] [%2] [%3] %4%5")
        .arg(entry.timestamp, levelName(entry.level), source, message, suffix);
}

QString InSARLogManager::getCurrentLogFilePath() const
{
    QMutexLocker locker(&m_configurationMutex);
    return m_logFilePath;
}

QString InSARLogManager::getDiagnosticLogFilePath() const
{
    QMutexLocker locker(&m_configurationMutex);
    return m_diagnosticLogFilePath;
}

void InSARLogManager::appendEntry(LogEntry entry)
{
    // User-facing events are also useful when investigating the same run in a
    // diagnostic sink. Ordinary INFO records stay diagnostic-only.
    if (entry.targets.testFlag(LogTarget::UserProjectLog)) {
        entry.targets |= diagnosticTargets();
    }

    {
        QMutexLocker locker(&m_configurationMutex);
        entry.sequence = ++m_nextSequence;
    }

    if (entry.targets.testFlag(LogTarget::UserProjectLog)) {
        publishUserEntry(entry);
    }
    m_dispatcher->enqueue(entry);
}

void InSARLogManager::publishUserEntry(const LogEntry& entry)
{
    if (QThread::currentThread() == thread()) {
        emit logAppended(entry);
        return;
    }

    QMetaObject::invokeMethod(this, [this, entry]() {
        emit logAppended(entry);
    }, Qt::QueuedConnection);
}

void InSARLogManager::writeEntry(const LogEntry& entry)
{
    const QString line = formatEntry(entry);
    QString userLogPath;
    QString diagnosticLogPath;
    bool consoleEnabled = false;
    bool diagnosticFileEnabled = false;
    {
        QMutexLocker locker(&m_configurationMutex);
        userLogPath = m_logFilePath;
        diagnosticLogPath = m_diagnosticLogFilePath;
        consoleEnabled = m_debugConsoleEnabled;
        diagnosticFileEnabled = m_diagnosticFileEnabled;
    }

    if (entry.targets.testFlag(LogTarget::UserProjectLog) && !appendLine(userLogPath, line, false)) {
        writeEmergency(line);
    }
    if (entry.targets.testFlag(LogTarget::DiagnosticFile) && diagnosticFileEnabled
        && !appendLine(diagnosticLogPath, line, true)) {
        writeEmergency(line);
    }
    if (entry.targets.testFlag(LogTarget::DebugConsole) && consoleEnabled) {
        writeEmergency(line);
    }
}

void InSARLogManager::writeDroppedDiagnosticSummary(quint64 count)
{
    LogEntry entry;
    entry.level = LevelWarning;
    entry.targets = diagnosticTargets();
    entry.source = QStringLiteral("InSARLogManager");
    entry.category = QStringLiteral("logging");
    entry.message = QStringLiteral("Dropped %1 debug events because the diagnostic queue was full.").arg(count);
    entry.timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    writeEntry(entry);
}

void InSARLogManager::writeEmergency(const QString& line) const
{
    const QByteArray bytes = (line + QLatin1Char('\n')).toLocal8Bit();
    std::fputs(bytes.constData(), stderr);
    OutputDebugStringW(reinterpret_cast<LPCWSTR>(QString::fromLocal8Bit(bytes).utf16()));
}

bool InSARLogManager::appendLine(const QString& path, const QString& line, bool rotate) const
{
    if (rotate) {
        QFileInfo fileInfo(path);
        static const qint64 kMaximumDiagnosticLogBytes = 10 * 1024 * 1024;
        if (fileInfo.exists() && fileInfo.size() >= kMaximumDiagnosticLogBytes) {
            const QString backupPath = path + QStringLiteral(".1");
            QFile::remove(backupPath);
            QFile::rename(path, backupPath);
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << line << '\n';
    return out.status() == QTextStream::Ok;
}
