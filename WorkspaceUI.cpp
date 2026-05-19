#include "WorkspaceUI.h"
#include "treeview.h"
#include <QSplitter>
#include <QTreeView>
#include <QTabWidget>
#include <QVBoxLayout>
#include "ColorBar.h"
#include "MyThread.h"
#include "icon_source.h"
#include "icon_utils.h"

// Icons now use SVG currentColor - automatically follows widget color property
// No manual tinting needed - theme colors are set via stylesheet

WorkspaceUI::WorkspaceUI(QWidget *parent)
    : QWidget(parent)
    , m_projectModel(nullptr)
    , m_projectPath()
    , m_projectName()
{
    setAutoFillBackground(true);
    setupUi();
    initTheme();
}

WorkspaceUI::~WorkspaceUI()
{
    // Components are children of this widget, Qt will automatically delete them
}

void WorkspaceUI::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create the same layout as in MainWindow.ui
    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_treeView = new TreeView(m_splitter);
    m_treeView->init_tree();
    m_treeView->setColumnHidden(1, true);
    m_treeView->setMinimumSize(300, 300);
    m_treeView->setMaximumSize(QWIDGETSIZE_MAX, 16777215);

    m_toolTree = new TreeView(m_splitter);
    m_toolTree->init_mould();
    m_toolTree->setMinimumSize(300, 300);
    m_toolTree->setMaximumSize(QWIDGETSIZE_MAX, 16777215);

    m_splitter->addWidget(m_treeView);
    m_splitter->addWidget(m_toolTree);

    m_splitter2 = new QSplitter(Qt::Horizontal, this);
    m_splitter2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_splitter2->addWidget(m_splitter);

    m_tabWidget = new QTabWidget(m_splitter2);
    m_tabWidget->setTabsClosable(true);
    m_splitter2->addWidget(m_tabWidget);

    layout->addWidget(m_splitter2);
    setLayout(layout);

    // Set initial splitter sizes: left panel at minimum 300px, right panel gets remaining space
    m_splitter2->setSizes({300, 1000});

    Process = nullptr;
}

QWidget* WorkspaceUI::centralWidget()
{
    return this;
}

QList<QToolBar*> WorkspaceUI::toolBars()
{
    // Workspace doesn't have its own toolbar - uses main window menu only
    return QList<QToolBar*>();
}

void WorkspaceUI::activate()
{
    show();
}

void WorkspaceUI::deactivate()
{
    hide();
}

QString WorkspaceUI::id() const
{
    return "workspace";
}

QString WorkspaceUI::displayName() const
{
    return "工作区";
}

TreeView* WorkspaceUI::treeView() const
{
    return m_treeView;
}

TreeView* WorkspaceUI::toolTree() const
{
    return m_toolTree;
}

QTabWidget* WorkspaceUI::tabWidget() const
{
    return m_tabWidget;
}

QSplitter* WorkspaceUI::mainSplitter() const
{
    return m_splitter2;
}

void WorkspaceUI::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Handle ColorBar resizing when window size changes
    if (m_tabWidget && m_tabWidget->count() > 0)
    {
        int index = m_tabWidget->currentIndex();
        if (index >= 0 && index < mExist_Color.size() && mExist_Color.at(index))
        {
            QWidget* currentWidget = m_tabWidget->currentWidget();
            if (currentWidget && mColors.at(index))
            {
                mColors.at(index)->resize(currentWidget->width() / 10, currentWidget->height() / 5);
                mColors.at(index)->move(currentWidget->mapToGlobal(QPoint(0, 0)));
            }
        }
    }
}

void WorkspaceUI::setProjectContext(QStandardItemModel* model, const QString& path, const QString& name)
{
    m_projectModel = model;
    m_projectPath = path;
    m_projectName = name;

    // Note: Don't set model to m_treeView here
    // because MainWindow already manages ui.treeView's model
    // and WorkspaceUI's m_treeView is a separate instance
}

QStandardItemModel* WorkspaceUI::projectModel() const
{
    return m_projectModel;
}

QString WorkspaceUI::projectPath() const
{
    return m_projectPath;
}

QString WorkspaceUI::projectName() const
{
    return m_projectName;
}

void WorkspaceUI::refreshProjectTree()
{
    if (m_projectModel && m_treeView) {
        m_treeView->setModel(m_projectModel);
        m_treeView->setColumnHidden(1, true);
    }
    Q_EMIT projectTreeRefreshed();
}

void WorkspaceUI::initTheme()
{
    // Default to light theme
    setTheme("light");
}

void WorkspaceUI::setTheme(const QString &theme)
{
    m_currentTheme = theme;

    // Set theme-background property for components to detect theme
    QColor bgColor;
    QString bgStyle;

    if (theme == "dark") {
        // Dark theme colors from ui2.md
        bgColor = QColor(26, 28, 28);  // #1A1C1C
        bgStyle = "background-color: #1A1C1C;";
        // Set property with dark theme color
        this->setProperty("theme-background", QColor(26, 28, 28));
    } else if (theme == "light") {
        // Light theme colors from ui2.md
        bgColor = QColor(249, 249, 249);  // #F9F9F9
        bgStyle = "background-color: #F9F9F9;";
        // Set property with light theme color
        this->setProperty("theme-background", QColor(249, 249, 249));
    } else {  // fusion
        // Fusion theme colors - flat light gray
        bgColor = QColor(240, 240, 240);  // #F0F0F0
        bgStyle = "background-color: #F0F0F0;";
        // Set property with fusion theme color
        this->setProperty("theme-background", QColor(240, 240, 240));
    }

    // Apply background color
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, bgColor);
    this->setPalette(palette);
    this->setStyleSheet(bgStyle);

    // Apply colors to children
    if (m_splitter) {
        m_splitter->setProperty("theme-background", bgColor);
    }
    if (m_splitter2) {
        m_splitter2->setProperty("theme-background", bgColor);
    }
    if (m_treeView) {
        m_treeView->setProperty("theme-background", bgColor);
    }
    if (m_toolTree) {
        m_toolTree->setProperty("theme-background", bgColor);
    }
    if (m_tabWidget) {
        m_tabWidget->setProperty("theme-background", bgColor);
    }

    // Refresh tree view icons for theme
    if (m_treeView) {
        m_treeView->updateTreeIcons(theme);
    }
    if (m_toolTree && m_toolTree->model) {
        QColor toolIconColor = themeIconColor(theme == "dark");
        for (int row = 0; row < m_toolTree->model->rowCount(); ++row) {
            QStandardItem *item = m_toolTree->model->item(row, 0);
            if (!item) continue;
            QString text = item->text();
            if (text == "InSAR" || text == "DInSAR") {
                item->setIcon(createColoredIcon(TEMPLATE_FOLDER, toolIconColor));
            } else {
                item->setIcon(createColoredIcon(TEMPLATE_TOOL, toolIconColor));
            }
            for (int c = 0; c < item->rowCount(); ++c) {
                QStandardItem *child = item->child(c, 0);
                if (child) child->setIcon(QIcon(TEMPLATE_TOOL));
            }
        }
    }

    update();
}

void WorkspaceUI::clear()
{
    // 清空树形视图模型
    if (m_treeView && m_treeView->model) {
        m_treeView->model->clear();
        m_treeView->model->setColumnCount(2);
        m_treeView->model->setHeaderData(0, Qt::Horizontal, tr("workspace"));
        m_treeView->model->setHeaderData(1, Qt::Horizontal, tr("Path"));
        m_treeView->setColumnHidden(1, true);
    }

    // 清空标签页
    while (m_tabWidget && m_tabWidget->count() > 0) {
        m_tabWidget->removeTab(0);
    }

    // 清空颜色条
    qDeleteAll(mColors);
    mColors.clear();
    mExist_Color.clear();

    // 重置状态
    ColorBar_Before = -1;
    TabCount_Before = -1;
}
