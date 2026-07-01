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
class MainWindow;
class QComboBox;
class QLabel;

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

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSortMethodChanged(int index);

private:
    void setupUi();
    void setupToolbar();
    ImageView* activeImageView() const;
    void initializeOriginalIndices(QStandardItemModel* model);
    void sortProjectTree(QStandardItemModel* model, const QString& projectPath);
    QList<QString> getWorkflowTopologicalOrder(const QString& projectPath);

    QToolBar *m_toolbar = nullptr;

    // All the original components from MainWindow
    QSplitter *m_splitter2 = nullptr;
    TreeView *m_treeView = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    QProgressDialog *Process = nullptr;

    // UI elements for custom title panel
    QWidget *m_titlePanel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QComboBox *m_sortComboBox = nullptr;
    int m_sortMethod = 1; // 0: Generation, 1: Time, 2: Topology

    // Theme property
    QString m_currentTheme;

    // Project context
    QStandardItemModel* m_projectModel;
    QString m_projectPath;
    QString m_projectName;
    XMLFile* m_projectXml = nullptr;
    MainWindow* m_mainWindow = nullptr;

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
