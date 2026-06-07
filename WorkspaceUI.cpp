#include "WorkspaceUI.h"
#include "MainWindow.h"
#include "treeview.h"
#include <QSplitter>
#include <QTreeView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QLayout>
#include "ColorBar.h"
#include "icon_source.h"
#include "icon_utils.h"
#include "ImageView.h"
#include <QToolButton>
#include <QMainWindow>
#include <QAction>

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
    setupToolbar();
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

    m_treeView = new TreeView(this);
    m_treeView->init_tree();
    m_treeView->setColumnHidden(1, true);
    m_treeView->setMinimumSize(300, 300);
    m_treeView->setMaximumSize(QWIDGETSIZE_MAX, 16777215);

    m_splitter2 = new QSplitter(Qt::Horizontal, this);
    m_splitter2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_splitter2->addWidget(m_treeView);

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
    QList<QToolBar*> list;
    if (m_toolbar) {
        list.append(m_toolbar);
    }
    return list;
}

void WorkspaceUI::activate()
{
    show();

    // 动态将工具栏的动作连接至主窗口
    if (!m_actionsConnected && m_toolbar) {
        QWidget* p = this;
        while (p && !qobject_cast<QMainWindow*>(p)) {
            p = p->parentWidget();
        }
        QMainWindow* mainWindow = qobject_cast<QMainWindow*>(p);
        if (mainWindow) {
            auto connectBtn = [&](const QString& objName, const QString& actionName) {
                QToolButton* btn = m_toolbar->findChild<QToolButton*>(objName);
                if (!btn) return;
                QAction* action = mainWindow->findChild<QAction*>(actionName);
                if (action) connect(btn, &QToolButton::clicked, action, &QAction::trigger);
            };
            connectBtn("btnNew", "actionNew");
            connectBtn("btnOpen", "actionOpen");
            connectBtn("btnSave", "actionSave");

            QToolButton* btnWorkflow = m_toolbar->findChild<QToolButton*>("btnWorkflow");
            if (btnWorkflow) {
                connect(btnWorkflow, &QToolButton::clicked, mainWindow, [mainWindow]() {
                    QMetaObject::invokeMethod(mainWindow, "switchToWorkflow");
                });
            }
            m_actionsConnected = true;
        }
    }
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

    // Handle ColorBar resizing when window size changes (reads from MainWindow's lists)
    if (m_mainWindow && m_tabWidget && m_tabWidget->count() > 0)
    {
        int index = m_tabWidget->currentIndex();
        QList<bool> existColors = m_mainWindow->existColors();
        QList<ColorBar*> colors = m_mainWindow->colors();
        if (index >= 0 && index < existColors.size() && existColors.at(index))
        {
            QWidget* currentWidget = m_tabWidget->currentWidget();
            if (currentWidget && index < colors.size() && colors.at(index))
            {
                colors.at(index)->resize(currentWidget->width() / 8, currentWidget->height() / 3);
                colors.at(index)->move(0, 0);
            }
        }
    }
}

void WorkspaceUI::setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml)
{
    // Cache MainWindow pointer for accessing shared color bar lists
    if (!m_mainWindow) {
        m_mainWindow = qobject_cast<MainWindow*>(window());
    }

    m_projectModel = model;
    m_projectPath = path;
    m_projectName = name;
    m_projectXml = projectXml;

    // Trigger tree refresh to ensure the view reflects the new model
    refreshProjectTree();
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

XMLFile* WorkspaceUI::projectXml() const
{
    return m_projectXml;
}

void WorkspaceUI::refreshProjectTree()
{
    // Ensure project tree components are visible if they were hidden
    // (Replicates logic from legacy MainWindow::RenewTree)
    if (m_treeView && m_treeView->isHidden())
    {
        m_treeView->show();
        if (m_tabWidget) m_tabWidget->show();
    }

    if (m_projectModel && m_treeView) {
        m_treeView->setModel(m_projectModel);
        m_treeView->model = m_projectModel; // Synchronize TreeView's internal pointer
        m_treeView->setColumnHidden(1, true);
        m_treeView->updateTreeIcons(m_currentTheme);
    }
    Q_EMIT projectTreeRefreshed();
}

void WorkspaceUI::updateProjectModel(QStandardItemModel* model)
{
    if (!model) return;

    m_projectModel = model;

    // Refresh the view
    refreshProjectTree();
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
    if (m_splitter2) {
        m_splitter2->setProperty("theme-background", bgColor);
    }
    if (m_treeView) {
        m_treeView->setProperty("theme-background", bgColor);
    }
    if (m_tabWidget) {
        m_tabWidget->setProperty("theme-background", bgColor);
    }

    // Refresh tree view icons for theme
    if (m_treeView) {
        m_treeView->updateTreeIcons(theme);
    }

    // Apply toolbar theme styles
    if (m_toolbar) {
        applyToolbarTheme(m_toolbar, theme, [](const QString& btnText, bool isDark) -> QColor {
            if (btnText == "New" || btnText == "Open" || btnText == "Save")
                return isDark ? QColor("#82CFFF") : QColor("#005FAC");
            if (btnText == "Workflow")
                return isDark ? QColor("#D0BCFF") : QColor("#6750A4");
            if (btnText == "Zoom In" || btnText == "Zoom Out" || btnText == "Fit")
                return isDark ? QColor("#FFB95B") : QColor("#A85C00");
            return isDark ? QColor("#CCCCCC") : QColor("#414752");
        });
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

    // 清空标签页（删除 widget 会自动销毁子 ColorBar）
    while (m_tabWidget && m_tabWidget->count() > 0) {
        QWidget* page = m_tabWidget->widget(0);
        m_tabWidget->removeTab(0);
        delete page;
    }
}

void WorkspaceUI::setupToolbar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setStyleSheet(
        "QToolBar { "
        "  background-color: #F3F3F3; "
        "  border-bottom: 1px solid rgba(192, 199, 212, 0.3); "
        "}"
    );
    // 必须在 setStyleSheet 之后调用，否则会被样式表重置
    m_toolbar->setContentsMargins(0, 0, 0, 0);
    m_toolbar->setFixedHeight(30);

    const QColor COLOR_PRIMARY("#005fac");          // Blue
    const QColor COLOR_ON_SURFACE_VARIANT("#414752"); // Gray
    const QColor COLOR_TEXT("#595F66");            // Text color

    // Group 1: Mode toggle (移至第一个位置)
    QToolButton* btnWorkflow = createToolbarButton(":/SatExplorer/svg/flow_editor.svg", "Workflow", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    btnWorkflow->setObjectName("btnWorkflow");
    m_toolbar->addWidget(btnWorkflow);

    // Vertical separator
    QWidget* sepMode = new QWidget();
    sepMode->setFixedWidth(1);
    sepMode->setStyleSheet("background-color: rgba(192, 199, 212, 0.3); margin: 2px 0px;");
    m_toolbar->addWidget(sepMode);

    // Group 2: Project management
    QToolButton* btnNew = createToolbarButton(":/SatExplorer/svg/new_project.svg", "New", COLOR_PRIMARY, COLOR_TEXT, this);
    btnNew->setObjectName("btnNew");
    m_toolbar->addWidget(btnNew);

    QToolButton* btnOpen = createToolbarButton(":/SatExplorer/svg/open_project.svg", "Open", COLOR_PRIMARY, COLOR_TEXT, this);
    btnOpen->setObjectName("btnOpen");
    m_toolbar->addWidget(btnOpen);

    QToolButton* btnSave = createToolbarButton(":/SatExplorer/svg/save.svg", "Save", COLOR_PRIMARY, COLOR_TEXT, this);
    btnSave->setObjectName("btnSave");
    m_toolbar->addWidget(btnSave);

    // Vertical separator
    QWidget* sep1 = new QWidget();
    sep1->setFixedWidth(1);
    sep1->setStyleSheet("background-color: rgba(192, 199, 212, 0.3); margin: 2px 0px;");
    m_toolbar->addWidget(sep1);

    // Group 3: View operations (Zoom In, Zoom Out, Fit Image)
    QToolButton* btnZoomIn = createToolbarButton(":/SatExplorer/svg/zoom.svg", "Zoom In", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_toolbar->addWidget(btnZoomIn);
    connect(btnZoomIn, &QToolButton::clicked, this, [this]() {
        ImageView* view = activeImageView();
        if (view) view->zoomIn();
    });

    QToolButton* btnZoomOut = createToolbarButton(":/SatExplorer/svg/zoom_out.svg", "Zoom Out", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_toolbar->addWidget(btnZoomOut);
    connect(btnZoomOut, &QToolButton::clicked, this, [this]() {
        ImageView* view = activeImageView();
        if (view) view->zoomOut();
    });

    QToolButton* btnZoomFit = createToolbarButton(":/SatExplorer/svg/zoom_fit.svg", "Fit", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_toolbar->addWidget(btnZoomFit);
    connect(btnZoomFit, &QToolButton::clicked, this, [this]() {
        ImageView* view = activeImageView();
        if (view) view->fitImage();
    });
}

ImageView* WorkspaceUI::activeImageView() const
{
    if (!m_tabWidget) return nullptr;
    QWidget* currentTab = m_tabWidget->currentWidget();
    if (!currentTab) return nullptr;
    return currentTab->findChild<ImageView*>();
}
