#include "InSARLogManager.h"
#include "LoggerNode.h"
#include <QHBoxLayout>
#include <QScrollBar>

namespace QtNodes {

LoggerNode::LoggerNode()
{
    m_widget = new QWidget();
    m_widget->setMinimumSize(400, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(m_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);

    QHBoxLayout* topLayout = new QHBoxLayout();
    
    m_levelFilter = new QComboBox();
    m_levelFilter->addItem(QStringLiteral("All Levels"), -1);
    m_levelFilter->addItem(QStringLiteral("Info"), InSARLogManager::LevelInfo);
    m_levelFilter->addItem(QStringLiteral("Warning"), InSARLogManager::LevelWarning);
    m_levelFilter->addItem(QStringLiteral("Error"), InSARLogManager::LevelError);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("Search..."));

    m_clearBtn = new QPushButton(QStringLiteral("Clear"));

    topLayout->addWidget(m_levelFilter);
    topLayout->addWidget(m_searchEdit);
    topLayout->addWidget(m_clearBtn);

    m_textBrowser = new QTextBrowser();
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setLineWrapMode(QTextEdit::NoWrap);

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_textBrowser);

    connect(m_levelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoggerNode::onFilterChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &LoggerNode::onFilterChanged);
    connect(m_clearBtn, &QPushButton::clicked, this, &LoggerNode::onClearClicked);

    connect(&InSARLogManager::instance(), &InSARLogManager::logAppended, this, &LoggerNode::onLogAppended);

    loadExistingLogs();
}

LoggerNode::~LoggerNode()
{
}

QJsonObject LoggerNode::save() const
{
    QJsonObject modelJson = NodeDelegateModel::save();
    return modelJson;
}

void LoggerNode::load(QJsonObject const &p)
{
    // No specific persistent data needed
}

void LoggerNode::loadExistingLogs()
{
    QString logPath = InSARLogManager::instance().getCurrentLogFilePath();
    QFile file(logPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.setCodec("UTF-8");
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.isEmpty()) continue;

            LogEntry entry;
            entry.rawLine = line;
            if (line.contains("[INFO]")) entry.level = InSARLogManager::LevelInfo;
            else if (line.contains("[WARNING]")) entry.level = InSARLogManager::LevelWarning;
            else if (line.contains("[ERROR]")) entry.level = InSARLogManager::LevelError;
            else entry.level = InSARLogManager::LevelInfo;
            
            m_allLogs.append(entry);
        }
        file.close();
        m_hasLoadedLogs = true;
        onFilterChanged(); // Refresh view
        Q_EMIT embeddedWidgetSizeUpdated();
    } else {
        m_hasLoadedLogs = false;
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

void LoggerNode::onLogAppended(const LogEntry& entry)
{
    m_allLogs.append(entry);

    int filterLevel = m_levelFilter->currentData().toInt();
    QString searchText = m_searchEdit->text();

    bool matchLevel = (filterLevel == -1 || entry.level == filterLevel);
    bool matchSearch = (searchText.isEmpty() || entry.rawLine.contains(searchText, Qt::CaseInsensitive));

    if (matchLevel && matchSearch) {
        appendLogToView(entry);
    }
    
    if (!m_hasLoadedLogs) {
        m_hasLoadedLogs = true;
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

void LoggerNode::onFilterChanged()
{
    m_textBrowser->clear();
    int filterLevel = m_levelFilter->currentData().toInt();
    QString searchText = m_searchEdit->text();

    for (const auto& entry : m_allLogs) {
        bool matchLevel = (filterLevel == -1 || entry.level == filterLevel);
        bool matchSearch = (searchText.isEmpty() || entry.rawLine.contains(searchText, Qt::CaseInsensitive));

        if (matchLevel && matchSearch) {
            appendLogToView(entry);
        }
    }
}

void LoggerNode::onClearClicked()
{
    m_allLogs.clear();
    m_textBrowser->clear();
}

void LoggerNode::appendLogToView(const LogEntry& entry)
{
    m_textBrowser->append(formatLogEntry(entry));
    // Auto-scroll to bottom
    QScrollBar *sb = m_textBrowser->verticalScrollBar();
    sb->setValue(sb->maximum());
}

QString LoggerNode::formatLogEntry(const LogEntry& entry) const
{
    QString color;
    switch (entry.level) {
        case InSARLogManager::LevelInfo: color = ""; break; // 空字符串代表使用系统/主题默认文本颜色
        case InSARLogManager::LevelWarning: color = "orange"; break;
        case InSARLogManager::LevelError: color = "red"; break;
        default: color = ""; break;
    }

    // Escape HTML to prevent injection if logs contain < or >
    QString safeLine = entry.rawLine.toHtmlEscaped();
    
    if (color.isEmpty()) {
        return safeLine;
    } else {
        return QString("<span style=\"color:%1;\">%2</span>").arg(color, safeLine);
    }
}

} // namespace QtNodes
