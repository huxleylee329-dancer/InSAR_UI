#ifndef DOCKWIDGETS_H
#define DOCKWIDGETS_H

#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDrag>
#include <QMimeData>
#include <QEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <QDir>
#include <QScrollArea>
#include <QGroupBox>
#include <QFormLayout>
#include <QTextEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <memory>

// QtNodes headers
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/DataFlowGraphModel>

// Forward declarations
class NodeEditorWindow;
class NodeTreeWidget;

#include "PaletteOrder.h"

// ============================================================================
// Left Side Panel Components
// ============================================================================

/**
 * @brief WorkflowBrowser - 工作流浏览器
 * 用于显示和加载本地工作流文件
 */
class WorkflowBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit WorkflowBrowser(QWidget *parent = nullptr);

    void setWorkflowPath(const QString &path) { m_workflowPath = path; refresh(); }
    void refresh();

signals:
    void loadWorkflow(const QString &filePath);

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    QLineEdit *m_searchBox;
    QTreeWidget *m_workflowList;
    QString m_workflowPath;
};

/**
 * @brief NodeLibraryWidget - 节点库
 * 显示所有可用节点，支持拖拽到画布
 */
class NodeLibraryWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NodeLibraryWidget(QWidget *parent = nullptr);

    void setRegistry(std::shared_ptr<QtNodes::NodeDelegateModelRegistry> registry);
    void setPaletteOrder(const PaletteOrder& order);  // Implemented in .cpp

signals:
    void nodeDoubleClicked(const QString &modelName);
    void nodeSearchTextChanged(const QString &text);
    void nodeItemClicked(const QString &modelName);

private slots:
    void onNodeSearchTextChanged(const QString &text);
    void onNodeItemDoubleClicked(const QString &modelName);
    void onNodeItemClicked(QTreeWidgetItem *item, int column);

private:
    void setupUi();
    void populateNodeTree();

    QLineEdit *m_searchBox;
    NodeTreeWidget *m_nodeTree;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    PaletteOrder m_paletteOrder;  // Palette order configuration
};

// ============================================================================
// Right Side Panel Components
// ============================================================================

/**
 * @brief PropertyEditor - 属性编辑器
 * 动态生成属性控件，基于选中节点的嵌入控件参数
 */
class PropertyEditor : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyEditor(QWidget *parent = nullptr);
    ~PropertyEditor();

    void setGraphModel(QtNodes::DataFlowGraphModel *model);

    void setSelectedNode(QtNodes::NodeId nodeId);

    void clearSelection();

signals:
    void propertyChanged(QtNodes::NodeId nodeId, const QString &property, const QVariant &value);

private slots:
    void onPropertyValueChanged();

private:
    void setupUi();
    void clearProperties();
    void generateProperties(QtNodes::NodeId nodeId);
    void extractPropertiesFromWidget(QWidget *widget, QFormLayout *layout, QtNodes::NodeId nodeId);

    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QFormLayout *m_formLayout;
    QLabel *m_noSelectionLabel;
    QLabel *m_nodeIdLabel;
    QLineEdit *m_captionEdit;
    QDoubleSpinBox *m_xSpinBox;
    QDoubleSpinBox *m_ySpinBox;

    QtNodes::DataFlowGraphModel *m_graphModel;
    QtNodes::NodeId m_currentNodeId;
    bool m_updatingProperties;  // Flag to prevent recursive updates
};

/**
 * @brief QueueManager - 队列管理器
 * 用于未来的任务队列管理功能
 */
class QueueManagerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QueueManagerWidget(QWidget *parent = nullptr);

private:
    void setupUi();
};

#endif // DOCKWIDGETS_H
