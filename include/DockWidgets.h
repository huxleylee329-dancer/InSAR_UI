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
#include <QToolButton>
#include <QProgressBar>
#include <QVector>
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

// Forward declarations for QtNodes types
namespace QtNodes {
    enum class NodeRole;
    enum class ExecutionMode;
    enum class ExecutionState;
    class ExecutableNodeDelegateModel;
    struct ParameterInfo;
}

/**
 * @brief PortDataInfo - 端口数据信息
 */
struct PortDataInfo {
    QtNodes::PortIndex index;
    QtNodes::PortType portType;
    QString name;
    QString dataType;
    QString summary;
    QVector<QtNodes::DataField> fields;
    bool isConnected;
    bool showIndex;  // 是否需要显示序号后缀
};

/**
 * @brief CollapsibleSection - 可折叠部分结构
 */
struct CollapsibleSection {
    QWidget* container = nullptr;
    QWidget* header = nullptr;
    QToolButton* toggleButton = nullptr;
    QLabel* titleLabel = nullptr;
    QWidget* contentWidget = nullptr;
    bool isExpanded = true;
};

/**
 * @brief PropertyEditor - 属性编辑器
 * 显示节点基本信息、输入/处理/输出数据部分
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

    void refreshCurrentNode();  // Refresh properties of currently selected node

    QtNodes::NodeId currentNodeId() const { return m_currentNodeId; }

signals:
    void propertyChanged(QtNodes::NodeId nodeId, const QString &property, const QVariant &value);
    void portDataChanged(QtNodes::NodeId nodeId, QtNodes::PortType portType, int portIndex, const QString& fieldKey, const QString& newValue);

private slots:
    void onPropertyValueChanged();

private:
    // UI Setup
    void setupUi();
    void createCollapsibleSection(CollapsibleSection& section, const QString& title);
    void toggleSection(CollapsibleSection& section);

    // Data Capture
    void captureNodeData(QtNodes::NodeId nodeId);
    void clearProperties();
    void clearBasicInfoFromLayout();

    // Property Generation
    void generateProperties();
    void generateBasicInfoSection();
    void generateInputSection();
    void generateProcessingSection();
    void generateOutputSection();
    void addPortCard(QVBoxLayout* layout, const PortDataInfo& info, bool isEditable);
    void addParameterCard(QVBoxLayout* layout, const QtNodes::ParameterInfo& param);

    // Helpers
    QString executionStateToString(QtNodes::ExecutionState state) const;
    QString executionModeToString(QtNodes::ExecutionMode mode) const;
    bool isDarkTheme() const;

    // Basic Info UI
    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QVBoxLayout *m_mainLayout;

    // 固定顶部区域（Node ID + 基本信息）
    QWidget *m_fixedTopWidget;
    QVBoxLayout *m_fixedTopLayout;

    QLabel *m_noSelectionLabel;
    QLabel *m_nodeIdLabel;
    QDoubleSpinBox *m_xSpinBox;
    QDoubleSpinBox *m_ySpinBox;
    QLabel *m_executionStateLabel;
    QProgressBar *m_progressBar;
    QLabel *m_modeLabel;

    // Collapsible Sections
    CollapsibleSection m_inputSection;
    CollapsibleSection m_processingSection;
    CollapsibleSection m_outputSection;

    // Data
    QtNodes::DataFlowGraphModel *m_graphModel;
    QtNodes::NodeId m_currentNodeId;
    bool m_updatingProperties;  // Flag to prevent recursive updates
    bool m_isExecutable;        // Whether current node is an ExecutableNode

    // Captured Data
    struct NodeData {
        QString caption;
        QPointF position;
        QtNodes::ExecutionState executionState;
        QtNodes::ExecutionMode executionMode;
        int progress;
        QVector<PortDataInfo> inputPorts;
        QVector<PortDataInfo> outputPorts;
        QVector<QtNodes::ParameterInfo> parameters;  // 控件参数（可编辑）
        QVector<QString> processingInfo;
    };
    NodeData m_nodeData;
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
