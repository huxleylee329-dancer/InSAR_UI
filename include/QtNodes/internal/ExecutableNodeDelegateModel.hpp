#pragma once

#include "NodeDelegateModel.hpp"
#include "Export.hpp"
#include "BasicGraphicsScene.hpp"
#include "NodeData.hpp"
#include <QTimer>
#include <QList>

#include <unordered_map>
#include <memory>
#include <cstdint>
#include <atomic>
#include <mutex>

namespace QtNodes {

// Forward declarations
class ExecutableNodeDelegateModel;

enum class ExecutionMode
{
    Automatic,
    Manual,
    Disabled
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
    class OutputCommitLease
    {
    public:
        OutputCommitLease() = default;
        OutputCommitLease(OutputCommitLease const&) = delete;
        OutputCommitLease& operator=(OutputCommitLease const&) = delete;
        OutputCommitLease(OutputCommitLease&& other) noexcept;
        OutputCommitLease& operator=(OutputCommitLease&& other) noexcept;
        ~OutputCommitLease();

        explicit operator bool() const { return _owner != nullptr; }

    private:
        friend class ExecutableNodeDelegateModel;
        OutputCommitLease(ExecutableNodeDelegateModel* owner, std::unique_lock<std::mutex>&& lock);
        void release();

        ExecutableNodeDelegateModel* _owner = nullptr;
        std::unique_lock<std::mutex> _lock;
    };

    ExecutionMode executionMode() const { return _mode; }
    virtual void setExecutionMode(ExecutionMode mode);

    // Configuration-only nodes may reuse executable persistence and propagation
    // without exposing execution controls in the workflow UI.
    virtual bool hasExecutionControls() const { return true; }

    ExecutionState executionState() const { return _state; }

    int progress() const { return _progress; }

    std::uint64_t executionRevision() const { return _executionRevision.load(); }

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

    // Data validation interface for detail view
    virtual bool supportsValidation() const { return false; }
    virtual ::QWidget* createValidationWidget(::QWidget* parent) { return nullptr; }

    // 干涉测量分析选项卡接口（第3个选项卡）
    // 用于评估两个影像是否适合进行 InSAR 干涉处理
    virtual bool supportsInterferometry() const { return false; }
    virtual ::QWidget* createInterferometryWidget(::QWidget* parent) { return nullptr; }

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
    void setInputBindingValid(PortIndex portIndex, bool valid, const QString& reason) override;
    bool isInputBindingValid(PortIndex portIndex) const override;

    /// Runtime-only artifact generation for a given output port.
    std::uint64_t outputRevision(PortIndex portIndex) const;

    /// Detect direct output assignments made by legacy nodes before propagation.
    void synchronizeOutputRevision(PortIndex portIndex);

    /// Use when an existing output object is changed in place.
    void markOutputArtifactChanged(PortIndex portIndex);

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

    /// Recalculate the waiting state after all project connections are restored.
    void refreshStateAfterRestoration();

    /// Check if the node was automatically triggered by upstream propagation
    bool isAutoTriggered() const { return _isAutoTriggered; }

    /// Get/Set the last execution error message
    QString lastErrorMessage() const { return _lastErrorMessage; }
    void setLastErrorMessage(QString const& errorMsg) { _lastErrorMessage = errorMsg; }

    /// Get/Set the reason for a successful execution with warnings.
    QString lastWarningMessage() const { return _lastWarningMessage; }
    void setLastWarningMessage(QString const& warningMsg) { _lastWarningMessage = warningMsg; }

public Q_SLOTS:
    void start();

    void stop();

    void setProgress(int percent);

    /// Trigger automatic execution for this node
    /// Used when a source node is connected in auto mode
    void triggerAutoExecution();

    /// Retry an automatic node after a non-port dependency becomes ready.
    /// This follows the normal automatic execution path, including auto-trigger semantics.
    void retryAutomaticExecution();

    void inputConnectionCreated(ConnectionId const &connectionId) override;

    void inputConnectionDeleted(ConnectionId const &connectionId) override;

private Q_SLOTS:
    void updateSmoothProgress();

Q_SIGNALS:
    void executionStarted();

    void executionFinished();

    void executionStopped();

    void executionError(QString const &error);

    void executionStartRejected(QString const &reason);

    void progressUpdated(int percent);

    void modeChanged(ExecutionMode newMode);

    void executionStateChanged();

public:
    /// Trigger update on the NodeGraphicsObject when progress/state changes
    void triggerVisualUpdate();

    virtual void collapseDetailedList() {}

protected:
    virtual void execute() = 0;

    virtual void stopExecution() = 0;

    virtual void processAutomatically() = 0;

    virtual bool prepareToStart() { return true; }

    /// 返回 true 表示 stopExecution() 仅请求异步停止，实际停止状态由子类完成信号更新。
    virtual bool stopExecutionIsAsynchronous() const { return false; }

    /// Opt in only after terminal callbacks discard obsolete results.
    virtual bool supportsAutomaticRestartAfterInputChange() const { return false; }

    /// 设置手动启动校验失败时向用户展示的原因。
    void setStartFailureMessage(QString const& message) { _startFailureMessage = message; }

    void finishExecution();

    /// Complete a successful task that produced usable outputs with warnings.
    /// Output ports are propagated just like a normal completion.
    void finishExecutionWithWarning();

    /// Complete automatic execution for source nodes with no inputs
    /// Call this after you've set output data in automatic mode
    void completeAutomaticExecution();

    void deferAutomaticCompletion();

    /// Return true when an asynchronous callback belongs to an obsolete input.
    /// Call this after releasing the worker/thread and before publishing results.
    bool discardObsoleteAutomaticExecution();

    bool isAutomaticExecutionObsolete() const { return _restartAfterInputChange; }
    bool executionStopRequested() const { return _stopRequested.load(); }

    OutputCommitLease acquireOutputCommitLease(std::uint64_t revision);

    /// Call this when input or source data changes in Manual mode
    void invalidateExecution();

    /// Check if parameter change should proceed. Prompts user if node already has data.
    bool confirmParameterChange();

    /// Check if node is in Pending state (ports connected but no data, or multi-port not all have data)
    /// Port with no connection is considered Idle, not Pending
    bool isPending() const;

    /// 检查所有必需（非可选）的输入端口是否都已经连线
    bool allRequiredPortsConnected() const;

    /// Check whether a port has an active graph connection, independent of
    /// the current cached input data.
    bool hasActiveInputConnection(PortIndex portIndex) const;

    /// Optional input alternatives (for example entity DEM vs resource
    /// reference). Every group requires at least one connected member.
    virtual QList<QList<PortIndex>> alternativeInputGroups() const { return {}; }

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
    std::unordered_map<PortIndex, std::uint64_t> _inputRevisions;
    std::unordered_map<PortIndex, bool> _inputBindingValidity;
    std::unordered_map<PortIndex, std::uint64_t> _outputRevisions;
    std::unordered_map<PortIndex, std::shared_ptr<NodeData>> _lastRevisionedOutputData;
    std::atomic<std::uint64_t> _executionRevision{1};
    std::atomic<bool> _commitLeaseActive{false};
    std::atomic<bool> _commitInvalidationRequested{false};
    std::atomic<bool> _stopRequested{false};

    // Widget managed by NodeDelegateModel base class (ownership handled by base)
    ::QWidget *_widget;

    int _progress;
    NodeId _nodeId;

    // Scene pointer for visual updates (non-owning reference)
    // Lifetime is managed externally, this class only stores the reference
    BasicGraphicsScene *_scene = nullptr;
    bool _isRestoring = false;
    // True when this node's execution was triggered automatically by upstream data propagation
    // (vs. manually by the user clicking Start). Allows skipping overwrite popups in auto mode.
    bool _isAutoTriggered = false;
    bool _deferAutomaticCompletion = false;
    bool _restartAfterInputChange = false;
    bool _restartScheduled = false;

    double _targetProgress = 0.0;
    double _currentShownProgress = 0.0;
    QTimer* _progressTimer = nullptr;
    QString _lastErrorMessage;
    QString _lastWarningMessage;
    QString _startFailureMessage;

private:
    std::uint64_t inputRevisionFromGraph(PortIndex portIndex) const;
    void invalidateOutputArtifact(PortIndex portIndex);
    void restartAutomaticExecutionAfterInputChange();
    void releaseOutputCommitLease(std::unique_lock<std::mutex>& lock);
};

} // namespace QtNodes
