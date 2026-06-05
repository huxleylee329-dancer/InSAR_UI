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
class ImageView;

/**
 * @brief 传统工作区界面
 *
 * 封装原MainWindow的传统工作区内容，实现IApplicationInterface接口
 */
class WorkspaceUI : public QWidget, public IApplicationInterface
{
    Q_OBJECT

signals:
    void projectTreeRefreshed();

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

    // Project context management
    void setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml = nullptr) override;
    QStandardItemModel* projectModel() const override;
    QString projectPath() const override;
    QString projectName() const override;
    XMLFile* projectXml() const override;

    // Theme management
    void initTheme() override;
    void setTheme(const QString &theme) override;

    // Clear interface
    void clear() override;

public slots:
    // Refresh project tree after import
    void refreshProjectTree() override;

    /**
     * @brief Update the project model and refresh the tree view
     * @param model New data model
     */
    void updateProjectModel(QStandardItemModel* model);

    //// Getters for components that MainWindow still needs access to
    TreeView* treeView() const;
    QTabWidget* tabWidget() const;
    QSplitter* mainSplitter() const;

    // Component access for MainWindow compatibility
    QProgressDialog* processDialog() { return Process; }
    QList<ColorBar*> colors() { return mColors; }
    void addColor(ColorBar* color) { mColors.append(color); }
    void removeColor(int index) { if (index >= 0 && index < mColors.size()) mColors.removeAt(index); }
    void addExistColor(bool exist) { mExist_Color.append(exist); }
    void removeExistColor(int index) { if (index >= 0 && index < mExist_Color.size()) mExist_Color.removeAt(index); }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();
    void setupToolbar();
    ImageView* activeImageView() const;

    QToolBar *m_toolbar = nullptr;

    // All the original components from MainWindow
    QSplitter *m_splitter2 = nullptr;
    TreeView *m_treeView = nullptr;
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
    XMLFile* m_projectXml = nullptr;

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
