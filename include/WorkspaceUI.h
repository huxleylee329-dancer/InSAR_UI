#ifndef WORKSPACEUI_H
#define WORKSPACEUI_H

#include "IApplicationInterface.h"
#include "ui_MainWindow.h"
#include <QWidget>
#include <QList>
#include <QToolBar>

class TreeView;
class QSplitter;
class QTabWidget;
class ColorBar;
class QProgressDialog;
class XMLFile;
class QStandardItemModel;

/**
 * @brief 传统工作区界面
 *
 * 封装原MainWindow的传统工作区内容，实现IApplicationInterface接口
 */
class WorkspaceUI : public QWidget, public IApplicationInterface
{
    Q_OBJECT

public:
    explicit WorkspaceUI(QWidget *parent = nullptr);
    ~WorkspaceUI() override;

    // IApplicationInterface interface
    QWidget* centralWidget() override;
    QList<QToolBar*> toolBars() override;
    void activate() override;
    void deactivate() override;
    QString id() const override;
    QString displayName() const override;

    //// Getters for components that MainWindow still needs access to
    TreeView* treeView() const;
    TreeView* toolTree() const;
    QTabWidget* tabWidget() const;
    QSplitter* mainSplitter() const;

    // Project context management
    void setProjectContext(QStandardItemModel* model, const QString& path, const QString& name);
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;

    // Theme management
    void initTheme();
    void setTheme(const QString &theme);

    // Component access for MainWindow compatibility
    QProgressDialog* processDialog() { return Process; }
    QList<ColorBar*> colors() { return mColors; }
    void addColor(ColorBar* color) { mColors.append(color); }
    void removeColor(int index) { if (index >= 0 && index < mColors.size()) mColors.removeAt(index); }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();

    // All the original components from MainWindow
    QSplitter *m_splitter = nullptr;
    QSplitter *m_splitter2 = nullptr;
    TreeView *m_treeView = nullptr;
    TreeView *m_toolTree = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    QProgressDialog *Process = nullptr;
    QList<ColorBar*> mColors;
    QList<bool> mExist_Color;
    int ColorBar_Before = -1;
    int TabCount_Before = -1;

    // Theme property
    QString m_currentTheme;

    // Project context
    QStandardItemModel* m_projectModel;
    QString m_projectPath;
    QString m_projectName;

    QString mData_path;
    QString mType;
    QString bmp_path;
    QString bmp_name;
    QTimer *t = nullptr;
    XMLFile* project = nullptr;
    QStandardItemModel* model = nullptr;
    QString double_click_open_project_file;
    bool b_open_throug_dbclk = false;
};

#endif // WORKSPACEUI_H
