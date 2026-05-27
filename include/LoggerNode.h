#ifndef LOGGERNODE_H
#define LOGGERNODE_H

#include <QtNodes/NodeDelegateModel>
#include <QWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QComboBox>
#include <QVBoxLayout>
#include <QPushButton>
#include "InSARLogManager.h"

namespace QtNodes {

class LoggerNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    LoggerNode();
    ~LoggerNode() override;

    QString caption() const override { return QStringLiteral("Logger"); }
    QString name() const override { return QStringLiteral("LoggerNode"); }

    bool captionVisible() const override { return true; }
    unsigned int nPorts(PortType) const override { return 0; }
    NodeDataType dataType(PortType, PortIndex) const override { return NodeDataType(); }

    std::shared_ptr<NodeData> outData(PortIndex) override { return nullptr; }
    void setInData(std::shared_ptr<NodeData>, PortIndex) override {}

    QWidget *embeddedWidget() override { return m_widget; }
    bool resizable() const override { return true; }

    QJsonObject save() const override;
    void load(QJsonObject const &p) override;

    bool hasLoadedLogs() const { return m_hasLoadedLogs; }

private slots:
    void onLogAppended(const LogEntry& entry);
    void onFilterChanged();
    void onClearClicked();

private:
    void appendLogToView(const LogEntry& entry);
    void loadExistingLogs();
    QString formatLogEntry(const LogEntry& entry) const;

    QWidget* m_widget;
    QTextBrowser* m_textBrowser;
    QLineEdit* m_searchEdit;
    QComboBox* m_levelFilter;
    QPushButton* m_clearBtn;

    QList<LogEntry> m_allLogs;
    bool m_hasLoadedLogs = false;
};

} // namespace QtNodes

#endif // LOGGERNODE_H
