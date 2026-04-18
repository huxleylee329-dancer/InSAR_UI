#include "WorkspaceUI.h"
#include "treeview.h"
#include <QSplitter>
#include <QTreeView>
#include <QTabWidget>
#include <QVBoxLayout>
#include "ColorBar.h"
#include "MyThread.h"

WorkspaceUI::WorkspaceUI(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
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
