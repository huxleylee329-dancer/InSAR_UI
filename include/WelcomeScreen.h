#ifndef WELCOMESCREEN_H
#define WELCOMESCREEN_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QMouseEvent>

/**
 * @brief 启动欢迎界面
 *
 * 类似VS Code的启动界面，显示新建项目、打开项目按钮和最近项目列表
 */
class WelcomeScreen : public QDialog
{
    Q_OBJECT

public:
    explicit WelcomeScreen(QWidget *parent = nullptr);
    ~WelcomeScreen() override;

signals:
    /**
     * @brief 用户请求新建项目
     */
    void newProjectRequested();

    /**
     * @brief 用户请求打开项目
     */
    void openProjectRequested();

    /**
     * @brief 用户点击最近项目
     */
    void recentProjectRequested(const QString &filePath);

private slots:
    void onNewProjectClicked();
    void onOpenProjectClicked();
    void onRecentItemDoubleClicked(QListWidgetItem *item);
    void refreshRecentProjects();

private:
    void setupUi();
    void loadRecentProjects();
    void paintEvent(QPaintEvent *event) override;

    QLabel *m_titleLabel;
    QPushButton *m_newProjectBtn;
    QPushButton *m_openProjectBtn;
    QListWidget *m_recentList;
    QLabel *m_recentLabel;
};

#endif // WELCOMESCREEN_H
