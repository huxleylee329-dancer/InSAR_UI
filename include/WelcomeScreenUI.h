#ifndef WELCOMESCREENUI_H
#define WELCOMESCREENUI_H

#include "IApplicationInterface.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QList>
#include <QToolBar>

/**
 * @brief 欢迎界面（作为 IApplicationInterface 实现）
 *
 * 显示在 MainWindow 中心，提供新建项目、打开项目和最近项目列表功能
 * 类似 VS Code 的欢迎屏幕体验
 */
class WelcomeScreenUI : public QWidget, public IApplicationInterface
{
    Q_OBJECT

public:
    explicit WelcomeScreenUI(QWidget *parent = nullptr);
    ~WelcomeScreenUI() override;

    // IApplicationInterface interface
    QWidget* centralWidget() override;
    QList<QToolBar*> toolBars() override;
    void activate() override;
    void deactivate() override;
    QString id() const override;
    QString displayName() const override;

    // Refresh recent projects list
    void refreshRecentProjects();

signals:
    void newProjectRequested();
    void openProjectRequested();
    void recentProjectRequested(const QString &filePath);

private:
    void setupUi();
    void loadRecentProjects();
    void paintEvent(QPaintEvent *event) override;

    QLabel *m_titleLabel = nullptr;
    QPushButton *m_newProjectBtn = nullptr;
    QPushButton *m_openProjectBtn = nullptr;
    QListWidget *m_recentList = nullptr;
    QLabel *m_recentLabel = nullptr;
};

#endif // WELCOMESCREENUI_H
