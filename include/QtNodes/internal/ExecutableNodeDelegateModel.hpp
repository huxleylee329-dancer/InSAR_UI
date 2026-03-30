#pragma once

#include "NodeDelegateModel.hpp"
#include "Export.hpp"
#include "BasicGraphicsScene.hpp"

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

    /// Set the nodeId and scene for visual updates (called when node is created)
    void setNodeContext(NodeId nodeId, BasicGraphicsScene *scene);

public:
    void setInData(std::shared_ptr<NodeData> nodeData, PortIndex const portIndex) override;

    std::shared_ptr<NodeData> outData(PortIndex const port) override;

    QWidget *embeddedWidget() override { return _widget; }

    /// Access to input/output data for detail view capture
    std::shared_ptr<NodeData> getInputData(PortIndex portIndex);
    void setOutputData(PortIndex portIndex, std::shared_ptr<NodeData> data);
    std::shared_ptr<NodeData> getOutputData(PortIndex portIndex);

public Q_SLOTS:
    void start();

    void stop();

    void setProgress(int percent);

Q_SIGNALS:
    void executionStarted();

    void executionFinished();

    void executionStopped();

    void executionError(QString const &error);

    void progressUpdated(int percent);

    void modeChanged(ExecutionMode newMode);

    void executionStateChanged();

protected:
    virtual void execute() = 0;

    virtual void stopExecution() = 0;

    virtual void processAutomatically() = 0;

    void finishExecution();

    /// Complete automatic execution for source nodes with no inputs
    /// Call this after you've set output data in automatic mode
    void completeAutomaticExecution();

    /// Invalidate current execution result (reset to Idle, progress 0)
    /// Call this when input or source data changes in Manual mode
    void invalidateExecution();

    /// Check if node is in Pending state (ports connected but no data, or multi-port not all have data)
    /// Port with no connection is considered Idle, not Pending
    bool isPending() const;

protected:
    void setState(ExecutionState state);

    /// Trigger update on the NodeGraphicsObject when progress/state changes
    void triggerVisualUpdate();

protected:
    ExecutionMode _mode;
    ExecutionState _state;
    std::unordered_map<PortIndex, std::shared_ptr<NodeData>> _inputData;
    std::unordered_map<PortIndex, std::shared_ptr<NodeData>> _outputData;

    // Widget managed by NodeDelegateModel base class (ownership handled by base)
    QWidget *_widget;

    int _progress;
    NodeId _nodeId;

    // Scene pointer for visual updates (non-owning reference)
    // Lifetime is managed externally, this class only stores the reference
    BasicGraphicsScene *_scene = nullptr;
};

} // namespace QtNodes
