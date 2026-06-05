#include "WorkspaceUI.h"
#include "treeview.h"
#include <QSplitter>
#include <QTreeView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QLayout>
#include "ColorBar.h"
#include "MyThread.h"
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
    static bool actionsConnected = false;
    if (!actionsConnected && m_toolbar) {
        QWidget* p = this;
        while (p && !qobject_cast<QMainWindow*>(p)) {
            p = p->parentWidget();
        }
        QMainWindow* mainWindow = qobject_cast<QMainWindow*>(p);
        if (mainWindow) {
            QList<QToolButton*> buttons = m_toolbar->findChildren<QToolButton*>();
            for (QToolButton* btn : buttons) {
                if (btn->text().trimmed() == "New") {
                    QAction* action = mainWindow->findChild<QAction*>("actionNew");
                    if (action) connect(btn, &QToolButton::clicked, action, &QAction::trigger);
                } else if (btn->text().trimmed() == "Open") {
                    QAction* action = mainWindow->findChild<QAction*>("actionOpen");
                    if (action) connect(btn, &QToolButton::clicked, action, &QAction::trigger);
                } else if (btn->text().trimmed() == "Save") {
                    QAction* action = mainWindow->findChild<QAction*>("actionSave");
                    if (action) connect(btn, &QToolButton::clicked, action, &QAction::trigger);
                } else if (btn->text().trimmed() == "Workflow") {
                    connect(btn, &QToolButton::clicked, mainWindow, [mainWindow]() {
                        QMetaObject::invokeMethod(mainWindow, "switchToWorkflow");
                    });
                }
            }
            actionsConnected = true;
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

    // Handle ColorBar resizing when window size changes
    if (m_tabWidget && m_tabWidget->count() > 0)
    {
        int index = m_tabWidget->currentIndex();
        if (index >= 0 && index < mExist_Color.size() && mExist_Color.at(index))
        {
            QWidget* currentWidget = m_tabWidget->currentWidget();
            if (currentWidget && mColors.at(index))
            {
                mColors.at(index)->resize(currentWidget->width() / 8, currentWidget->height() / 3);
                mColors.at(index)->move(0, 0);
            }
        }
    }
}

void WorkspaceUI::setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml)
{
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
        QString toolbarStyle;
        QColor separatorColor;
        QColor iconColor = themeIconColor(theme == "dark");
        QColor textColor = (theme == "dark") ? QColor("#c1c7cf") : QColor("#595F66");
        
        if (theme == "dark") {
            toolbarStyle = R"(
                QToolBar {
                    background-color: #1a1c1c;
                    border-bottom: 1px solid rgba(135, 141, 152, 0.3);
                }
            )";
            separatorColor = QColor(135, 141, 152, 77);
        } else if (theme == "light") {
            toolbarStyle = R"(
                QToolBar {
                    background-color: #f3f3f3;
                    border-bottom: 1px solid rgba(192, 199, 212, 0.3);
                }
            )";
            separatorColor = QColor(192, 199, 212, 77);
        } else { // fusion
            toolbarStyle = R"(
                QToolBar {
                    background-color: #f0f0f0;
                    border-bottom: 1px solid rgba(74, 154, 207, 0.3);
                }
            )";
            separatorColor = QColor(74, 154, 207, 77);
        }
        
        m_toolbar->setStyleSheet(toolbarStyle);
        // 必须在 setStyleSheet 之后重新设置，否则会被样式表重置
        m_toolbar->setContentsMargins(0, 0, 0, 0);
        
        // Update separator widgets
        for (QObject *obj : m_toolbar->children()) {
            QWidget *widget = qobject_cast<QWidget*>(obj);
            if (widget && widget->metaObject()->className() == QString("QWidget")) {
                widget->setStyleSheet(QString("background-color: %1; margin: 2px 0px;").arg(separatorColor.name(QColor::HexArgb)));
            }
        }
        
        // Update toolbar buttons
        for (QObject *obj : m_toolbar->children()) {
            QToolButton *btn = qobject_cast<QToolButton*>(obj);
            if (btn) {
                QString textColorStr = textColor.name();
                QString hoverBg = (theme == "dark") ? "#2f3131" : "#E0E0E0";
                QString pressedBg = (theme == "dark") ? "#3f4141" : "#D0D0D0";
                btn->setStyleSheet(
                    QString("QToolButton { "
                    "  border: none; "
                    "  border-radius: 4px; "
                    "  background-color: transparent; "
                    "  color: %1; "
                    "  font-size: 11px; "
                    "  font-weight: bold; "
                    "  text-transform: uppercase; "
                    "  letter-spacing: 0.5px; "
                    "  padding: 0px 6px; "
                    "  margin: 0px; "
                    "  min-height: 20px; "
                    "  max-height: 20px; "
                    "}"
                    "QToolButton:hover { "
                    "  background-color: %2; "
                    "}"
                    "QToolButton:pressed { "
                    "  background-color: %3; "
                    "}").arg(textColorStr).arg(hoverBg).arg(pressedBg)
                );
                
                QString iconPath = btn->property("iconPath").toString();
                if (!iconPath.isEmpty()) {
                    QColor c = iconColor;
                    bool isDark = (theme == "dark");
                    QString btnText = btn->text().trimmed();
                    if (btnText == "New" || btnText == "Open" || btnText == "Save") {
                        c = isDark ? QColor("#82CFFF") : QColor("#005FAC");
                    } else if (btnText == "Workflow") {
                        c = isDark ? QColor("#D0BCFF") : QColor("#6750A4");
                    } else if (btnText == "Zoom In" || btnText == "Zoom Out" || btnText == "Fit") {
                        c = isDark ? QColor("#FFB95B") : QColor("#A85C00");
                    }
                    btn->setIcon(createColoredIcon(iconPath, c));
                }
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

// Helper to create custom toolbar button
static QToolButton* createToolbarButton(const QString &iconPath, const QString &text, const QColor &iconColor = QColor("#414752"), const QColor &textColor = QColor("#595F66"), QWidget *parent = nullptr)
{
    QToolButton *btn = new QToolButton(parent);
    
    QIcon coloredIcon = createColoredIcon(iconPath, iconColor);
    btn->setIcon(coloredIcon);
    btn->setIconSize(QSize(24, 24));
    btn->setText(" " + text); // 前置空格拉开图标和文字的间距
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setProperty("iconPath", iconPath);
    
    QString textColorHex = textColor.name();
    btn->setStyleSheet(
        QString("QToolButton { "
        "  border: none; "
        "  border-radius: 4px; "
        "  background-color: transparent; "
        "  color: %1; "
        "  font-size: 11px; "
        "  font-weight: bold; "
        "  text-transform: uppercase; "
        "  letter-spacing: 0.5px; "
        "  padding: 0px 6px; "
        "  margin: 0px; "
        "  min-height: 20px; "
        "  max-height: 20px; "
        "}"
        "QToolButton:hover { "
        "  background-color: #E0E0E0; "
        "}"
        "QToolButton:pressed { "
        "  background-color: #D0D0D0; "
        "}").arg(textColorHex)
    );
    return btn;
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
    m_toolbar->addWidget(btnWorkflow);

    // Vertical separator
    QWidget* sepMode = new QWidget();
    sepMode->setFixedWidth(1);
    sepMode->setStyleSheet("background-color: rgba(192, 199, 212, 0.3); margin: 2px 0px;");
    m_toolbar->addWidget(sepMode);

    // Group 2: Project management
    QToolButton* btnNew = createToolbarButton(":/SatExplorer/svg/new_project.svg", "New", COLOR_PRIMARY, COLOR_TEXT, this);
    m_toolbar->addWidget(btnNew);

    QToolButton* btnOpen = createToolbarButton(":/SatExplorer/svg/open_project.svg", "Open", COLOR_PRIMARY, COLOR_TEXT, this);
    m_toolbar->addWidget(btnOpen);

    QToolButton* btnSave = createToolbarButton(":/SatExplorer/svg/save.svg", "Save", COLOR_PRIMARY, COLOR_TEXT, this);
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
