#include "InSARLogManager.h"
#include <QDir>
#include <QDebug>
#include <QCoreApplication>

InSARLogManager& InSARLogManager::instance()
{
    static InSARLogManager inst;
    return inst;
}

InSARLogManager::InSARLogManager()
{
    qRegisterMetaType<LogEntry>("LogEntry");
    // Default log file location
    m_logFilePath = QCoreApplication::applicationDirPath() + "/SatExplorer.log";

    qRegisterMetaType<LogEntry>("LogEntry");
}

InSARLogManager::~InSARLogManager()
{
}

void InSARLogManager::setProjectDirectory(const QString& projectDir)
{
    QMutexLocker locker(&m_mutex);
    if (projectDir.isEmpty()) {
        m_logFilePath = QCoreApplication::applicationDirPath() + "/SatExplorer.log";
    } else {
        m_logFilePath = QDir(projectDir).absoluteFilePath("project.log");
    }
}

QString InSARLogManager::getCurrentLogFilePath() const
{
    return m_logFilePath;
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

void InSARLogManager::Log(LogLevel level, const QString& source, const QString& message)
{
    InSARLogManager& mgr = instance();
    QMutexLocker locker(&mgr.m_mutex);

    QString levelStr;
    switch (level) {
        case LevelInfo: levelStr = "INFO"; break;
        case LevelWarning: levelStr = "WARNING"; break;
        case LevelError: levelStr = "ERROR"; break;
        default: levelStr = "INFO"; break;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString rawLine = QString("[%1] [%2] [%3] %4").arg(timestamp).arg(levelStr).arg(source).arg(message);

    QFile file(mgr.m_logFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << rawLine << "\n";
        file.close();
    } else {
        qWarning() << "InSARLogManager: Failed to open log file" << mgr.m_logFilePath;
    }

    LogEntry entry;
    entry.level = level;
    entry.source = source;
    entry.message = message;
    entry.timestamp = timestamp;
    entry.rawLine = rawLine;

    emit mgr.logAppended(entry);
}
