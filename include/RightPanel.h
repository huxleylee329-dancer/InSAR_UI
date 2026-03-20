#ifndef RIGHTPANEL_H
#define RIGHTPANEL_H

#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QScrollArea>
#include <QGroupBox>
#include <QFormLayout>
#include <QDir>
#include <memory>

// QtNodes headers
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeGraphicsObject>

// Forward declarations
class PropertyEditor;
class QueueManager;

/**
 * @brief RightPanel - 右侧面板组件
 *
 * 包含两个标签页：
 * 1. 节点属性 (Node Properties) - 查看和编辑选中节点的详细参数
 * 2. 队列管理 (Queue Manager) - 占位符，用于未来的任务队列管理
 */
class RightPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RightPanel(QWidget *parent = nullptr);
    ~RightPanel();

    // 设置图模型（用于获取节点数据）
    void setGraphModel(QtNodes::DataFlowGraphModel *model);
    QtNodes::DataFlowGraphModel* graphModel() const { return m_graphModel; }

    // 更新属性编辑器显示的节点
    void setSelectedNode(QtNodes::NodeId nodeId);

    // 清除选择
    void clearSelection();

    // 折叠/展开功能
    void toggleCollapse();
    bool isCollapsed() const { return m_collapsed; }

signals:
    void propertyChanged(QtNodes::NodeId nodeId, const QString &property, const QVariant &value);

private slots:
    void onTabChanged(int index);

private:
    void setupUi();
    void setupPropertiesTab();
    void setupQueueTab();

    // UI components
    QTabWidget *m_tabWidget;
    PropertyEditor *m_propertyEditor;
    QueueManager *m_queueManager;

    // Data
    QtNodes::DataFlowGraphModel *m_graphModel;
    QtNodes::NodeId m_selectedNodeId;
    bool m_collapsed;
    int m_normalWidth;
};

/**
 * @brief PropertyEditor - 属性编辑器组件
 *
 * 动态生成属性控件，基于选中节点的嵌入控件参数
 */
class PropertyEditor : public QWidget
{
    Q_OBJECT

public:
    explicit PropertyEditor(QWidget *parent = nullptr);
    ~PropertyEditor();

    // 设置图模型
    void setGraphModel(QtNodes::DataFlowGraphModel *model);

    // 更新显示的节点
    void setSelectedNode(QtNodes::NodeId nodeId);

    // 清除选择
    void clearSelection();

signals:
    void propertyChanged(QtNodes::NodeId nodeId, const QString &property, const QVariant &value);

private slots:
    void onPropertyValueChanged();

private:
    void setupUi();
    void clearProperties();
    void generateProperties(QtNodes::NodeId nodeId);

    // 辅助函数：从嵌入控件提取参数
    void extractPropertiesFromWidget(QWidget *widget, QFormLayout *layout, QtNodes::NodeId nodeId);
    QString getLabelForWidget(QWidget *widget);  // 从布局中提取控件的标签文本

    // UI components
    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QFormLayout *m_formLayout;
    QLabel *m_noSelectionLabel;
    QLabel *m_nodeIdLabel;
    QLineEdit *m_captionEdit;
    QDoubleSpinBox *m_xSpinBox;
    QDoubleSpinBox *m_ySpinBox;

    // Data
    QtNodes::DataFlowGraphModel *m_graphModel;
    QtNodes::NodeId m_currentNodeId;
    bool m_updatingProperties;  // Flag to prevent recursive updates
};

/**
 * @brief QueueManager - 队列管理器组件（占位符）
 *
 * 用于未来的任务队列管理功能
 */
class QueueManager : public QWidget
{
    Q_OBJECT

public:
    explicit QueueManager(QWidget *parent = nullptr);
    ~QueueManager();

private:
    void setupUi();
};

#endif // RIGHTPANEL_H
