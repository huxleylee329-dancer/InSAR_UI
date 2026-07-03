#include "WorkspaceUI.h"
#include "MainWindow.h"
#include "treeview.h"
#include "include/GCPDatabase.h"
#include "include/GCPAnnotationWidget.h"
#include <QMessageBox>
#include <QtSql/QSqlError>
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
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QQueue>
#include <QSet>
#include "tinyxml.h"

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

    // Create treeContainer widget
    QWidget* treeContainer = new QWidget(this);
    QVBoxLayout* treeLayout = new QVBoxLayout(treeContainer);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(0);

    // Create Title Panel
    m_titlePanel = new QFrame(treeContainer);
    m_titlePanel->setObjectName("titlePanel");
    static_cast<QFrame*>(m_titlePanel)->setFrameShape(QFrame::NoFrame);
    m_titlePanel->setFixedHeight(30);
    QHBoxLayout* titleLayout = new QHBoxLayout(m_titlePanel);
    titleLayout->setContentsMargins(10, 4, 10, 6);
    titleLayout->setSpacing(5);

    m_titleLabel = new QLabel(tr("Workspace"), m_titlePanel);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLayout->addWidget(m_titleLabel);

    titleLayout->addStretch();

    // Create ComboBox
    m_sortComboBox = new QComboBox(m_titlePanel);
    m_sortComboBox->addItem(tr("生成顺序"), 0);
    m_sortComboBox->addItem(tr("时间顺序"), 1);
    m_sortComboBox->addItem(tr("工作流拓扑"), 2);
    m_sortComboBox->setFixedHeight(20);
    m_sortComboBox->setFixedWidth(100);
    m_sortComboBox->setCurrentIndex(1); // 默认选择时间顺序 (索引 1)
    titleLayout->addWidget(m_sortComboBox, 0, Qt::AlignVCenter);

    treeLayout->addWidget(m_titlePanel);

    m_treeView = new TreeView(treeContainer);
    m_treeView->init_tree();
    m_treeView->setHeaderHidden(true); // Hide default QTreeView column header
    m_treeView->setColumnHidden(1, true);
    m_treeView->setMinimumSize(300, 300);
    m_treeView->setMaximumSize(QWIDGETSIZE_MAX, 16777215);

    treeLayout->addWidget(m_treeView);

    m_splitter2 = new QSplitter(Qt::Horizontal, this);
    m_splitter2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_splitter2->addWidget(treeContainer); // Add treeContainer instead of treeView directly

    m_tabWidget = new QTabWidget(m_splitter2);
    m_tabWidget->setTabsClosable(true);
    m_splitter2->addWidget(m_tabWidget);

    layout->addWidget(m_splitter2);
    setLayout(layout);

    // Set initial splitter sizes: left panel at minimum 300px, right panel gets remaining space
    m_splitter2->setSizes({300, 1000});

    Process = nullptr;

    connect(m_sortComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WorkspaceUI::onSortMethodChanged);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int) {
        updateGcpButtonState();
    });
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

    if (m_projectModel) {
        initializeOriginalIndices(m_projectModel);
    }

    // Trigger tree refresh to ensure the view reflects the new model
    refreshProjectTree();
    updateGcpButtonState();
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
        sortProjectTree(m_projectModel, m_projectPath);

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
    initializeOriginalIndices(m_projectModel);

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
            if (btnText == "Zoom In" || btnText == "Zoom Out" || btnText == "Fit" || btnText == "Reset Zoom")
                return isDark ? QColor("#FFB95B") : QColor("#A85C00");
            if (btnText.trimmed() == "GCP")
                return isDark ? QColor("#47D8A4") : QColor("#0F7D5C");
            return isDark ? QColor("#CCCCCC") : QColor("#414752");
        });
    }

    // Apply styling to title panel and sort combobox
    if (m_titlePanel && m_titleLabel && m_sortComboBox) {
        if (theme == "dark") {
            m_titlePanel->setStyleSheet("QFrame#titlePanel { background-color: #252526; border-bottom: 1px solid #3C3C3C; }");
            m_titleLabel->setStyleSheet("color: #CCCCCC; font-weight: bold; font-size: 12px; border: none; background: transparent;");
            m_sortComboBox->setStyleSheet(
                "QComboBox { "
                "  border: 1px solid #555555; "
                "  border-radius: 3px; "
                "  padding: 1px 18px 1px 5px; "
                "  background: #333333; "
                "  color: #CCCCCC; "
                "  font-size: 11px; "
                "  min-height: 20px; "
                "  max-height: 20px; "
                "} "
                "QComboBox::drop-down { "
                "  subcontrol-origin: padding; "
                "  subcontrol-position: top right; "
                "  width: 15px; "
                "  border-left-width: 1px; "
                "  border-left-color: #555555; "
                "  border-left-style: solid; "
                "} "
                "QComboBox::down-arrow { "
                "  image: url(:/SatExplorer/svg/chevron_down_dark.svg); "
                "  width: 10px; "
                "  height: 10px; "
                "} "
                "QComboBox QAbstractItemView { "
                "  background: #2D2D2D; "
                "  color: #CCCCCC; "
                "  selection-background-color: #005FAC; "
                "}"
            );
        } else { // light or fusion
            m_titlePanel->setStyleSheet("QFrame#titlePanel { background-color: #F3F3F3; border-bottom: 1px solid rgba(192, 199, 212, 0.3); }");
            m_titleLabel->setStyleSheet("color: #414752; font-weight: bold; font-size: 12px; border: none; background: transparent;");
            m_sortComboBox->setStyleSheet(
                "QComboBox { "
                "  border: 1px solid rgba(192, 199, 212, 0.5); "
                "  border-radius: 3px; "
                "  padding: 1px 18px 1px 5px; "
                "  background: white; "
                "  color: #414752; "
                "  font-size: 11px; "
                "  min-height: 20px; "
                "  max-height: 20px; "
                "} "
                "QComboBox::drop-down { "
                "  subcontrol-origin: padding; "
                "  subcontrol-position: top right; "
                "  width: 15px; "
                "  border-left-width: 1px; "
                "  border-left-color: rgba(192, 199, 212, 0.5); "
                "  border-left-style: solid; "
                "} "
                "QComboBox::down-arrow { "
                "  image: url(:/SatExplorer/svg/chevron_down_light.svg); "
                "  width: 10px; "
                "  height: 10px; "
                "} "
                "QComboBox QAbstractItemView { "
                "  background: white; "
                "  color: #414752; "
                "  selection-background-color: #005fac; "
                "  selection-color: white; "
                "}"
            );
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

    m_projectPath.clear();
    m_projectName.clear();

    // 清空标签页（删除 widget 会自动销毁子 ColorBar）
    while (m_tabWidget && m_tabWidget->count() > 0) {
        QWidget* page = m_tabWidget->widget(0);
        m_tabWidget->removeTab(0);
        delete page;
    }

    updateGcpButtonState();
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

    QToolButton* btnZoomReset = createToolbarButton(":/SatExplorer/svg/zoom_reset.svg", "Reset Zoom", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_toolbar->addWidget(btnZoomReset);
    connect(btnZoomReset, &QToolButton::clicked, this, [this]() {
        ImageView* view = activeImageView();
        if (view) view->resetZoom();
    });

    // 增加 GCP 专用分割线与按钮
    QWidget* sepGcp = new QWidget(this);
    sepGcp->setObjectName("sepGcp");
    sepGcp->setFixedWidth(1);
    sepGcp->setStyleSheet("background-color: rgba(192, 199, 212, 0.3); margin: 2px 0px;");
    m_gcpSepAction = m_toolbar->addWidget(sepGcp);

    m_btnGcp = createToolbarButton(":/SatExplorer/svg/GCPs.svg", "GCP", COLOR_PRIMARY, COLOR_TEXT, this);
    m_btnGcp->setObjectName("m_btnGcp");
    m_btnGcp->setToolTip(QStringLiteral("打开当前影像的地面控制点(GCP)标注与残差评估界面"));
    m_gcpBtnAction = m_toolbar->addWidget(m_btnGcp);

    m_gcpSepAction->setVisible(true);
    m_gcpBtnAction->setVisible(true);

    connect(m_btnGcp, &QToolButton::clicked, this, [this]() {
        MainWindow* mainWin = qobject_cast<MainWindow*>(window());
        if (mainWin) {
            mainWin->on_actionGCP_Manager_triggered();
        }
    });
}

ImageView* WorkspaceUI::activeImageView() const
{
    if (!m_tabWidget) return nullptr;
    QWidget* currentTab = m_tabWidget->currentWidget();
    if (!currentTab) return nullptr;
    return currentTab->findChild<ImageView*>();
}

void WorkspaceUI::sortProjectTree(QStandardItemModel* model, const QString& projectPath)
{
    if (!model || projectPath.isEmpty()) return;

    for (int pIdx = 0; pIdx < model->rowCount(); ++pIdx) {
        QStandardItem* projectItem = model->item(pIdx, 0);
        if (!projectItem) continue;

        int catCount = projectItem->rowCount();

        // 1. 先对每个二级目录（Data Node）底下的三级文件（Level 3 Files）进行排序
        for (int cIdx = 0; cIdx < catCount; ++cIdx) {
            QStandardItem* categoryItem = projectItem->child(cIdx, 0);
            if (!categoryItem) continue;

            int fileCount = categoryItem->rowCount();
            QList<QList<QStandardItem*>> fileRows;
            fileRows.reserve(fileCount);
            for (int i = fileCount - 1; i >= 0; --i) {
                fileRows.prepend(categoryItem->takeRow(i));
            }

            if (m_sortMethod == 1) {
                // 时间顺序：按修改时间升序排列
                std::stable_sort(fileRows.begin(), fileRows.end(), [](const QList<QStandardItem*>& a, const QList<QStandardItem*>& b) {
                    QString pathA = (a.size() > 1 && a[1]) ? a[1]->text() : "";
                    QString pathB = (b.size() > 1 && b[1]) ? b[1]->text() : "";
                    QDateTime timeA = QFileInfo(pathA).exists() ? QFileInfo(pathA).lastModified() : QDateTime();
                    QDateTime timeB = QFileInfo(pathB).exists() ? QFileInfo(pathB).lastModified() : QDateTime();
                    return timeA < timeB;
                });
            } else {
                // 生成顺序 / 拓扑顺序下：文件按原始生成索引升序排列
                std::stable_sort(fileRows.begin(), fileRows.end(), [](const QList<QStandardItem*>& a, const QList<QStandardItem*>& b) {
                    int idxA = (a.size() > 0 && a[0]) ? a[0]->data(Qt::UserRole + 1).toInt() : 9999;
                    int idxB = (b.size() > 0 && b[0]) ? b[0]->data(Qt::UserRole + 1).toInt() : 9999;
                    return idxA < idxB;
                });
            }

            for (const auto& row : fileRows) {
                categoryItem->appendRow(row);
            }
        }

        // 2. 对所有二级目录（Data Nodes）进行提取和排序
        QList<QList<QStandardItem*>> catRows;
        catRows.reserve(catCount);
        for (int i = catCount - 1; i >= 0; --i) {
            catRows.prepend(projectItem->takeRow(i));
        }

        if (m_sortMethod == 0) {
            // ① 按生成顺序排序
            std::stable_sort(catRows.begin(), catRows.end(), [](const QList<QStandardItem*>& a, const QList<QStandardItem*>& b) {
                int idxA = (a.size() > 0 && a[0]) ? a[0]->data(Qt::UserRole + 1).toInt() : 9999;
                int idxB = (b.size() > 0 && b[0]) ? b[0]->data(Qt::UserRole + 1).toInt() : 9999;
                return idxA < idxB;
            });
        }
        else if (m_sortMethod == 1) {
            // ② 按时间顺序排序
            auto getCategoryLatestTime = [&](const QList<QStandardItem*>& row) -> QDateTime {
                if (row.isEmpty() || !row[0]) return QDateTime::fromMSecsSinceEpoch(0);
                QStandardItem* categoryItem = row[0];

                QDateTime latestTime;

                // 获取目录自身的修改时间
                QString dirName = categoryItem->text();
                QFileInfo projectFileInfo(projectPath);
                QString projectDir = projectFileInfo.absolutePath();
                QString dirPath = projectDir + "/" + dirName;
                QFileInfo dirInfo(dirPath);
                if (dirInfo.exists() && dirInfo.isDir()) {
                    latestTime = dirInfo.lastModified();
                }

                // 获取目录底下所有有效三级文件的最新修改时间
                int fileCount = categoryItem->rowCount();
                for (int i = 0; i < fileCount; ++i) {
                    QStandardItem* pathItem = categoryItem->child(i, 1);
                    if (pathItem) {
                        QString filePath = pathItem->text();
                        if (filePath.endsWith(".jpg", Qt::CaseInsensitive) || filePath.endsWith(".jpeg", Qt::CaseInsensitive)) {
                            continue;
                        }
                        QFileInfo fileInfo(filePath);
                        if (fileInfo.exists()) {
                            QDateTime fileTime = fileInfo.lastModified();
                            if (!latestTime.isValid() || fileTime > latestTime) {
                                latestTime = fileTime;
                            }
                        }
                    }
                }

                if (!latestTime.isValid()) {
                    latestTime = QDateTime::fromMSecsSinceEpoch(0);
                }
                return latestTime;
            };

            std::stable_sort(catRows.begin(), catRows.end(), [&](const QList<QStandardItem*>& a, const QList<QStandardItem*>& b) {
                return getCategoryLatestTime(a) < getCategoryLatestTime(b);
            });
        }
        else if (m_sortMethod == 2) {
            // ③ 按工作流拓扑顺序排序
            QList<QString> topoOrder = getWorkflowTopologicalOrder(projectPath);
            std::stable_sort(catRows.begin(), catRows.end(), [&topoOrder](const QList<QStandardItem*>& a, const QList<QStandardItem*>& b) {
                QString nameA = (a.size() > 0 && a[0]) ? a[0]->text() : "";
                QString nameB = (b.size() > 0 && b[0]) ? b[0]->text() : "";

                int idxA = topoOrder.indexOf(nameA);
                int idxB = topoOrder.indexOf(nameB);

                // 孤立节点放在最下面
                if (idxA < 0) idxA = 9999;
                if (idxB < 0) idxB = 9999;

                // 若都是孤立节点或索引相同，则按原始生成顺序排列
                if (idxA == idxB) {
                    int genA = (a.size() > 0 && a[0]) ? a[0]->data(Qt::UserRole + 1).toInt() : 9999;
                    int genB = (b.size() > 0 && b[0]) ? b[0]->data(Qt::UserRole + 1).toInt() : 9999;
                    return genA < genB;
                }

                return idxA < idxB;
            });
        }

        // 重新添加二级节点
        for (const auto& row : catRows) {
            projectItem->appendRow(row);
        }
    }
}

void WorkspaceUI::initializeOriginalIndices(QStandardItemModel* model)
{
    if (!model) return;

    // 检查是否已经初始化过，防止重复打标覆盖原始顺序
    bool alreadySet = false;
    for (int pIdx = 0; pIdx < model->rowCount(); ++pIdx) {
        QStandardItem* projectItem = model->item(pIdx, 0);
        if (projectItem && projectItem->rowCount() > 0) {
            QStandardItem* child = projectItem->child(0, 0);
            if (child && child->data(Qt::UserRole + 1).isValid()) {
                alreadySet = true;
                break;
            }
        }
    }

    if (alreadySet) {
        // 对动态新增的数据补充原始顺序标记
        int currentMaxIdx = -1;
        for (int pIdx = 0; pIdx < model->rowCount(); ++pIdx) {
            QStandardItem* projectItem = model->item(pIdx, 0);
            if (!projectItem) continue;
            for (int cIdx = 0; cIdx < projectItem->rowCount(); ++cIdx) {
                QStandardItem* categoryItem = projectItem->child(cIdx, 0);
                if (!categoryItem) continue;
                QVariant val = categoryItem->data(Qt::UserRole + 1);
                if (val.isValid()) {
                    currentMaxIdx = std::max(currentMaxIdx, val.toInt());
                }
            }
        }

        for (int pIdx = 0; pIdx < model->rowCount(); ++pIdx) {
            QStandardItem* projectItem = model->item(pIdx, 0);
            if (!projectItem) continue;
            for (int cIdx = 0; cIdx < projectItem->rowCount(); ++cIdx) {
                QStandardItem* categoryItem = projectItem->child(cIdx, 0);
                if (!categoryItem) continue;
                if (!categoryItem->data(Qt::UserRole + 1).isValid()) {
                    currentMaxIdx++;
                    categoryItem->setData(currentMaxIdx, Qt::UserRole + 1);
                }

                int currentFileMaxIdx = -1;
                for (int fIdx = 0; fIdx < categoryItem->rowCount(); ++fIdx) {
                    QStandardItem* fileItem = categoryItem->child(fIdx, 0);
                    if (fileItem) {
                        QVariant fileVal = fileItem->data(Qt::UserRole + 1);
                        if (fileVal.isValid()) {
                            currentFileMaxIdx = std::max(currentFileMaxIdx, fileVal.toInt());
                        }
                    }
                }
                for (int fIdx = 0; fIdx < categoryItem->rowCount(); ++fIdx) {
                    QStandardItem* fileItem = categoryItem->child(fIdx, 0);
                    if (fileItem && !fileItem->data(Qt::UserRole + 1).isValid()) {
                        currentFileMaxIdx++;
                        fileItem->setData(currentFileMaxIdx, Qt::UserRole + 1);
                    }
                }
            }
        }
        return;
    }

    // 初次加载，设定连续顺序索引
    for (int pIdx = 0; pIdx < model->rowCount(); ++pIdx) {
        QStandardItem* projectItem = model->item(pIdx, 0);
        if (!projectItem) continue;

        for (int cIdx = 0; cIdx < projectItem->rowCount(); ++cIdx) {
            QStandardItem* categoryItem = projectItem->child(cIdx, 0);
            if (!categoryItem) continue;

            categoryItem->setData(cIdx, Qt::UserRole + 1);

            for (int fIdx = 0; fIdx < categoryItem->rowCount(); ++fIdx) {
                QStandardItem* fileItem = categoryItem->child(fIdx, 0);
                if (fileItem) {
                    fileItem->setData(fIdx, Qt::UserRole + 1);
                }
            }
        }
    }
}

QList<QString> WorkspaceUI::getWorkflowTopologicalOrder(const QString& projectPath)
{
    QList<QString> order;
    if (projectPath.isEmpty()) return order;

    TiXmlDocument doc(projectPath.toLocal8Bit().constData());
    if (!doc.LoadFile()) return order;

    TiXmlElement* root = doc.RootElement();
    if (!root) return order;

    TiXmlElement* workflowNode = nullptr;
    for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
        if (strcmp(p->Value(), "workflow") == 0) {
            workflowNode = p;
            break;
        }
    }

    if (!workflowNode || !workflowNode->GetText()) return order;

    QByteArray workflowData = QByteArray(workflowNode->GetText());
    QJsonDocument jsonDoc = QJsonDocument::fromJson(workflowData);
    if (!jsonDoc.isObject()) return order;

    QJsonObject json = jsonDoc.object();
    QJsonArray nodes = json["nodes"].toArray();
    QJsonArray connections = json["connections"].toArray();

    // 建立 ID 与 outputNodeName 映射
    QMap<int, QString> nodeNames;
    for (int i = 0; i < nodes.size(); ++i) {
        QJsonObject nodeObj = nodes[i].toObject();
        int nodeId = nodeObj["id"].toInt();
        QJsonObject internalData = nodeObj["internal-data"].toObject();
        QString outputNodeName = internalData["outputNodeName"].toString().trimmed();
        if (!outputNodeName.isEmpty()) {
            nodeNames[nodeId] = outputNodeName;
        }
    }

    // 构建有向无环图结构以进行拓扑排序
    QMap<int, QList<int>> adjList;
    QMap<int, int> inDegree;

    for (int nodeId : nodeNames.keys()) {
        inDegree[nodeId] = 0;
    }

    for (int i = 0; i < connections.size(); ++i) {
        QJsonObject conn = connections[i].toObject();
        int outNode = conn["outNodeId"].toInt();
        int inNode = conn["intNodeId"].toInt();

        if (nodeNames.contains(outNode) && nodeNames.contains(inNode)) {
            adjList[outNode].append(inNode);
            inDegree[inNode]++;
        }
    }

    // Kahn 拓扑排序算法
    QQueue<int> queue;
    for (int nodeId : inDegree.keys()) {
        if (inDegree[nodeId] == 0) {
            queue.enqueue(nodeId);
        }
    }

    QList<int> sortedNodeIds;
    while (!queue.isEmpty()) {
        int u = queue.dequeue();
        sortedNodeIds.append(u);

        for (int v : adjList[u]) {
            inDegree[v]--;
            if (inDegree[v] == 0) {
                queue.enqueue(v);
            }
        }
    }

    // 转换成排好序的目录名称列表
    for (int nodeId : sortedNodeIds) {
        if (nodeNames.contains(nodeId)) {
            order.append(nodeNames[nodeId]);
        }
    }

    return order;
}

void WorkspaceUI::onSortMethodChanged(int index)
{
    m_sortMethod = index;
    refreshProjectTree();
}

void WorkspaceUI::updateGcpButtonState()
{
    if (!m_gcpBtnAction) return;

    // GCP 按钮在工具区一直保持可见，不再根据当前页是否为 h5 动态隐藏
    m_gcpBtnAction->setVisible(true);
    if (m_gcpSepAction) {
        m_gcpSepAction->setVisible(true);
    }
}
