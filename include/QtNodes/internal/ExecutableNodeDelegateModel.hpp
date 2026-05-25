#pragma once

#include "NodeDelegateModel.hpp"
#include "Export.hpp"
#include "BasicGraphicsScene.hpp"
#include "NodeData.hpp"

#include <unordered_map>
#include <memory>

namespace QtNodes {

// Forward declarations
class ExecutableNodeDelegateModel;

enum class ExecutionMode
{
    Automatic,
    Manual
};

enum class ExecutionState
{
    Idle,       // 空闲（端口未连接）
    Pending,    // 等待（端口连接但上游无数据 / 多端口且非全部有数据）
    Running,    // 运行中
    Completed,  // 成功完成
    Stopped,    // 用户手动停止
    Warning,    // 有警告/需要特别注意
    Error,      // 执行出错
    Disabled    // 禁用状态（人工设置进入）
};

/**
 * @brief ParameterInfo - 控件参数信息
 * 用于显示和编辑节点控件中的参数（如 QLineEdit、QSpinBox 等）
 */
struct NODE_EDITOR_PUBLIC ParameterInfo
{
    QString name;                    // 参数名称
    QString dataType;                // 数据类型
    QString value;                   // 当前值
    FieldEditType editType = FieldEditType::None;  // 编辑类型
    double minNumber = -1000000.0;                // 数字最小值
    double maxNumber = 1000000.0;                  // 数字最大值
    int decimals = 2;                              // 小数位数
    QString pathFilter = "All Files (*)";          // 文件过滤器
};

class NODE_EDITOR_PUBLIC ExecutableNodeDelegateModel : public NodeDelegateModel
{
    Q_OBJECT

public:
    ExecutableNodeDelegateModel();

    ~ExecutableNodeDelegateModel() override = default;

public:
    ExecutionMode executionMode() const { return _mode; }
    virtual void setExecutionMode(ExecutionMode mode);

    ExecutionState executionState() const { return _state; }

    int progress() const { return _progress; }

    /// Mark whether this node uses external layout (ears + progress bar painted outside)
    virtual bool useExternalLayout() const { return true; }

    /// Return an image path to be displayed in the detail view preview section
    virtual QStringList previewImagePaths() const { return QStringList(); }

    // ROI Selection Interface (for detail view)
    virtual bool supportsRoiSelection() const { return false; }
    virtual void processRoiSelection(const QRectF& sceneRect, int imageIndex) {}
    virtual void clearRoiSelection() {}
    virtual bool hasCustomRoi() const { return false; }
    virtual QRectF customRoi() const { return QRectF(); }
    
    // Dual ROI Selection Interface (e.g., Target & Clutter)
    virtual bool supportsTwoRois() const { return false; }
    virtual void processTargetRoiSelection(const QRectF& sceneRect, int imageIndex) {}
    virtual void processClutterRoiSelection(const QRectF& sceneRect, int imageIndex) {}
    virtual void clearTargetRoiSelection() {}
    virtual void clearClutterRoiSelection() {}
    virtual bool hasTargetRoi() const { return false; }
    virtual QRectF targetRoi() const { return QRectF(); }
    virtual bool hasClutterRoi() const { return false; }
    virtual QRectF clutterRoi() const { return QRectF(); }
    
    // Detail View UI Customization
    virtual QStringList detailTableHeaders() const { return {}; }
    
    // Data extraction for detail view (e.g., target detection results)
    virtual QList<QStringList> detectionResults() const { return {}; }
    
    // Processing Info extraction for detail view middle column
    virtual std::vector<QString> processingInfo() const { return {}; }

    /// Set the nodeId and scene for visual updates (called when node is created)
    void setNodeContext(NodeId nodeId, BasicGraphicsScene *scene);

public:
    void setInData(std::shared_ptr<NodeData> nodeData, PortIndex const portIndex) override;

    std::shared_ptr<NodeData> outData(PortIndex const port) override;

    ::QWidget *embeddedWidget() override { return _widget; }

    /// Access to input/output data for detail view capture
    std::shared_ptr<NodeData> getInputData(PortIndex portIndex);
    void setOutputData(PortIndex portIndex, std::shared_ptr<NodeData> data);
    std::shared_ptr<NodeData> getOutputData(PortIndex portIndex);

    QJsonObject save() const override;

    void load(QJsonObject const &json) override;

    /// Get widget parameters for display and editing in Properties panel
    /// Returns parameters from node's widgets (e.g., QLineEdit, QSpinBox, etc.)
    /// Default implementation returns empty vector (no parameters)
    virtual QVector<ParameterInfo> getParameters() const;

    /// Set a parameter value from Properties panel
    /// Called when user edits a parameter in Properties panel
    /// Default implementation does nothing (parameters are read-only)
    virtual void setParameter(const QString& paramName, const QString& value);

    /// Set whether the node is currently being restored from a project
    void setRestoring(bool restoring) { _isRestoring = restoring; }

    /// Check if the node is currently being restored from a project
    bool isRestoring() const { return _isRestoring; }

public Q_SLOTS:
    void start();

    void stop();

    void setProgress(int percent);

    /// Trigger automatic execution for this node
    /// Used when a source node is connected in auto mode
    void triggerAutoExecution();

Q_SIGNALS:
    void executionStarted();

    void executionFinished();

    void executionStopped();

    void executionError(QString const &error);

    void progressUpdated(int percent);

    void modeChanged(ExecutionMode newMode);

    void executionStateChanged();

public:
    /// Trigger update on the NodeGraphicsObject when progress/state changes
    void triggerVisualUpdate();

protected:
    virtual void execute() = 0;

    virtual void stopExecution() = 0;

    virtual void processAutomatically() = 0;

    void finishExecution();

    /// Complete automatic execution for source nodes with no inputs
    /// Call this after you've set output data in automatic mode
    void completeAutomaticExecution();

    /// Call this when input or source data changes in Manual mode
    void invalidateExecution();

    /// Check if parameter change should proceed. Prompts user if node already has data.
    bool confirmParameterChange();

    /// Check if node is in Pending state (ports connected but no data, or multi-port not all have data)
    /// Port with no connection is considered Idle, not Pending
    bool isPending() const;

protected:
    void setState(ExecutionState state);

    /// 验证并恢复输出数据
    /// 子类实现：根据保存的参数验证输出文件是否存在
    /// 返回true表示验证成功且已恢复output数据，返回false表示文件不存在或无法恢复
    virtual bool validateAndRestoreOutput() { return false; }

protected:
    ExecutionMode _mode;
    ExecutionState _state;
    std::unordered_map<PortIndex, std::shared_ptr<NodeData>> _inputData;
    std::unordered_map<PortIndex, std::shared_ptr<NodeData>> _outputData;

    // Widget managed by NodeDelegateModel base class (ownership handled by base)
    ::QWidget *_widget;

    int _progress;
    NodeId _nodeId;

    // Scene pointer for visual updates (non-owning reference)
    // Lifetime is managed externally, this class only stores the reference
    BasicGraphicsScene *_scene = nullptr;
    bool _isRestoring = false;
};

} // namespace QtNodes
