#ifndef WELCOMESCREENUI_H
#define WELCOMESCREENUI_H

#include "IApplicationInterface.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QList>
#include <QToolBar>
#include <QFrame>

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

    // Project context (欢迎界面不管理项目，提供空实现)
    void setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml = nullptr) override;
    QStandardItemModel* projectModel() const override;
    QString projectPath() const override;
    QString projectName() const override;
    XMLFile* projectXml() const override;

    // Theme management
    void initTheme() override;
    void setTheme(const QString& theme) override;

    // Refresh project tree after import (欢迎界面无需操作)
    void refreshProjectTree() override;

    // Clear interface (欢迎界面无需清空操作)
    void clear() override;

    // Refresh recent projects list
    void refreshRecentProjects();

signals:
    void newProjectRequested();
    void openProjectRequested();
    void fetchDataRequested();
    void recentProjectRequested(const QString &filePath);

private:
    void setupUi();
    void loadRecentProjects();
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void updateThemeStyles();
    QWidget* createQuickStartButton(const QString &iconName, const QString &title, const QString &description);
    QWidget* createRecentProjectItem(const QString &name, const QString &path, const QString &time);

    QString m_currentTheme = "light";
    bool m_isDarkTheme = true;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QPushButton *m_newProjectBtn = nullptr;
    QPushButton *m_openProjectBtn = nullptr;
    QPushButton *m_fetchDataBtn = nullptr;
    QWidget *m_recentProjectsContainer = nullptr;
    QFrame *m_tipFrame = nullptr;
    QLabel *m_tipTitleLabel = nullptr;
    QLabel *m_tipTextLabel = nullptr;
};

#endif // WELCOMESCREENUI_H
