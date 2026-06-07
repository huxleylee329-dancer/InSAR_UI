
#include "ExecutableNodeDelegateModel.hpp"
#include "NodeGraphicsObject.hpp"
#include "BasicGraphicsScene.hpp"
#include <QMessageBox>
#include "DataFlowGraphModel.hpp"

namespace QtNodes {

ExecutableNodeDelegateModel::ExecutableNodeDelegateModel()
    : _mode(ExecutionMode::Automatic)
    , _state(ExecutionState::Idle)
    , _widget(nullptr)
    , _progress(0)
    , _nodeId(NodeId())
    , _scene(nullptr)
{
}

void ExecutableNodeDelegateModel::setNodeContext(NodeId nodeId, BasicGraphicsScene *scene)
{
    _nodeId = nodeId;
    _scene = scene;
}

void ExecutableNodeDelegateModel::triggerVisualUpdate()
{
    if (_scene != nullptr) {
        NodeGraphicsObject *ngo = _scene->nodeGraphicsObject(_nodeId);
        if (ngo != nullptr) {
            ngo->update();
        }
        // Emit signal to notify that node properties have changed
        Q_EMIT _scene->nodePropertyChanged(_nodeId);
    }
}

void ExecutableNodeDelegateModel::setExecutionMode(ExecutionMode mode)
{
    if (_mode != mode) {
        _mode = mode;
        Q_EMIT modeChanged(mode);
        triggerVisualUpdate();
    }
}

void ExecutableNodeDelegateModel::setInData(std::shared_ptr<NodeData> nodeData, PortIndex const portIndex)
{
    // Check if data actually changed
    auto it = _inputData.find(portIndex);
    if (it == _inputData.end() || it->second != nodeData) {
        // Data changed
        _inputData[portIndex] = nodeData;

        // Skip state changes and auto-execution during restoration
        if (_isRestoring) {
            return;
        }

        if (_mode == ExecutionMode::Manual) {
            // In Manual mode, any input change invalidates previous result
            invalidateExecution();
        }

        if (_mode == ExecutionMode::Automatic) {
            // If the incoming data is null, an upstream node was invalidated.
            // Use setState(Idle) to: (1) update our visual, (2) clear our own stale output data,
            // and (3) cascade the invalidation to our downstream nodes via dataUpdated(nullptr).
            if (nodeData == nullptr) {
                setState(ExecutionState::Idle);
                return;
            }

            // For automatic mode: set running state and zero progress before execution
            setState(ExecutionState::Running);
            _progress = 0;

            // Let subclass do the automatic processing (sets output data if inputs are complete)
            // Mark as auto-triggered so executeProcessing() can skip overwrite popups
            _isAutoTriggered = true;
            _deferAutomaticCompletion = false;
            processAutomatically();
            _isAutoTriggered = false;

            if (_deferAutomaticCompletion) {
                _deferAutomaticCompletion = false;
                return;
            }

            // CRITICAL FIX: If the subclass explicitly changed its state (e.g. to Idle, Error)
            // or if it launched an asynchronous thread and is still Running,
            // we MUST NOT override its state with default completion logic!
            if (_state != ExecutionState::Running) {
                Q_EMIT executionStateChanged();
                triggerVisualUpdate();
                return;
            }

            // Check if we have any non-empty output data after processing
            // Special case: node with zero output ports (pure display) always completes after processing
            unsigned int outPortCount = nPorts(PortType::Out);
            bool shouldComplete = false;

            if (outPortCount == 0) {
                // No output ports - this is a display node, complete after processing
                shouldComplete = true;
            } else {
                // Has output ports - check if we have at least one non-empty output
                bool hasOutput = false;
                for (auto const &pair : _outputData) {
                    if (pair.second != nullptr) {
                        hasOutput = true;
                        break;
                    }
                }
                shouldComplete = hasOutput;
            }

            if (shouldComplete) {
                _progress = 100;
                _state = ExecutionState::Completed;
            } else {
                _progress = 0;
                _state = ExecutionState::Idle;
            }

            Q_EMIT progressUpdated(_progress);
            Q_EMIT executionStateChanged();
            Q_EMIT computingFinished();
            triggerVisualUpdate();
        }
    }
}

std::shared_ptr<NodeData> ExecutableNodeDelegateModel::outData(PortIndex const port)
{
    auto it = _outputData.find(port);
    if (it != _outputData.end()) {
        return it->second;
    }
    return nullptr;
}

void ExecutableNodeDelegateModel::start()
{
    if (_state == ExecutionState::Running) {
        return;
    }

    if (!prepareToStart()) {
        return;
    }

    // Use setState() to trigger downstream invalidation (clear stale outputs & propagate nullptr)
    setState(ExecutionState::Running);
    _progress = 0;
    Q_EMIT executionStarted();
    Q_EMIT computingStarted();
    triggerVisualUpdate();

    execute();
}

void ExecutableNodeDelegateModel::stop()
{
    if (_state != ExecutionState::Running) {
        return;
    }

    stopExecution();
    _state = ExecutionState::Stopped;
    Q_EMIT executionStopped();
    Q_EMIT executionStateChanged();
    Q_EMIT computingFinished();
    triggerVisualUpdate();
}

void ExecutableNodeDelegateModel::setProgress(int percent)
{
    _progress = percent;
    Q_EMIT progressUpdated(percent);
    triggerVisualUpdate();
}

void ExecutableNodeDelegateModel::triggerAutoExecution()
{
    if (_mode != ExecutionMode::Automatic) {
        return;
    }

    // For nodes with no input ports (source nodes), trigger automatic processing
    unsigned int inPortCount = nPorts(PortType::In);
    if (inPortCount == 0) {
        processAutomatically();
    }
}

void ExecutableNodeDelegateModel::finishExecution()
{
    if (_state != ExecutionState::Running) {
        return;
    }

    _progress = 100;
    Q_EMIT progressUpdated(_progress);
    _state = ExecutionState::Completed;
    Q_EMIT executionFinished();
    Q_EMIT executionStateChanged();
    Q_EMIT computingFinished();
    triggerVisualUpdate();

    for (auto const &pair : _outputData) {
        Q_EMIT dataUpdated(pair.first);
    }
}

void ExecutableNodeDelegateModel::deferAutomaticCompletion()
{
    _deferAutomaticCompletion = true;
}

void ExecutableNodeDelegateModel::completeAutomaticExecution()
{
    // For automatic mode source nodes with no inputs:
    // set running state, then after subclass outputs data, complete with 100% progress
    if (_mode != ExecutionMode::Automatic) {
        return;
    }

    _state = ExecutionState::Running;
    _progress = 0;
    Q_EMIT executionStateChanged();
    triggerVisualUpdate();

    // Subclass has already set output data in automatic mode

    _progress = 100;
    _state = ExecutionState::Completed;
    Q_EMIT progressUpdated(_progress);
    Q_EMIT executionStateChanged();
    Q_EMIT computingFinished();
    triggerVisualUpdate();
}

void ExecutableNodeDelegateModel::invalidateExecution()
{
    // Reset to idle state when data changes in Manual mode
    // This indicates the previous result is outdated and needs re-execution
    if (_state == ExecutionState::Idle && _progress == 0) {
        // Already invalid, no change needed
        return;
    }

    _progress = 0;
    Q_EMIT progressUpdated(_progress);
    setState(ExecutionState::Idle);

    if (_scene) {
        Q_EMIT _scene->modified(_scene);
    }
}

bool ExecutableNodeDelegateModel::confirmParameterChange()
{
    bool hasData = false;
    for (auto const &pair : _outputData) {
        if (pair.second != nullptr) {
            hasData = true;
            break;
        }
    }

    if (_state == ExecutionState::Completed || hasData) {
        auto reply = QMessageBox::question(nullptr, tr("确认修改"),
            tr("修改该参数将导致本节点及所有下游节点重置。\n\n"
               "说明：\n"
               "- 未修改名称的节点，其旧文件将被覆盖。\n"
               "- 修改名称的节点将生成新文件夹，但旧文件夹会成为孤立文件，可通过【清除孤立文件】功能移除。\n\n"
               "是否确认修改？"),
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::No) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<NodeData> ExecutableNodeDelegateModel::getInputData(PortIndex portIndex)
{
    auto it = _inputData.find(portIndex);
    if (it != _inputData.end()) {
        return it->second;
    }
    return nullptr;
}

void ExecutableNodeDelegateModel::setOutputData(PortIndex portIndex, std::shared_ptr<NodeData> data)
{
    _outputData[portIndex] = data;
}

std::shared_ptr<NodeData> ExecutableNodeDelegateModel::getOutputData(PortIndex portIndex)
{
    auto it = _outputData.find(portIndex);
    if (it != _outputData.end()) {
        return it->second;
    }
    return nullptr;
}

QJsonObject ExecutableNodeDelegateModel::save() const
{
    QJsonObject modelJson = NodeDelegateModel::save();

    modelJson["execution-mode"] = static_cast<int>(_mode);
    modelJson["execution-state"] = static_cast<int>(_state);

    return modelJson;
}

void ExecutableNodeDelegateModel::load(QJsonObject const &json)
{
    NodeDelegateModel::load(json);

    QJsonValue v = json["execution-mode"];
    if (!v.isUndefined()) {
        _mode = static_cast<ExecutionMode>(v.toInt());
    }

    // 恢复状态：只有当保存的是Completed状态且验证通过才恢复
    QJsonValue stateValue = json["execution-state"];
    if (!stateValue.isUndefined()) {
        ExecutionState savedState = static_cast<ExecutionState>(stateValue.toInt());
        if (savedState == ExecutionState::Completed) {
            // 调用子类验证输出数据
            if (validateAndRestoreOutput()) {
                _state = ExecutionState::Completed;
                _progress = 100;
                // 发送信号通知UI更新状态显示
                Q_EMIT progressUpdated(_progress);
                Q_EMIT executionStateChanged();
                triggerVisualUpdate();
            }
            // 否则保持Idle状态
        }
        // 其他状态（Running/Error/Stopped）都重置为Idle
        // 因为重新打开工程时，这些瞬态没有意义
    }
}

void ExecutableNodeDelegateModel::setState(ExecutionState state)
{
    if (_state == state) {
        return;
    }
    
    _state = state;
    Q_EMIT executionStateChanged();
    triggerVisualUpdate();

    // Dirty propagation: if this node becomes non-Completed (Idle, Running, Error, etc.),
    // all downstream nodes should also become Idle, and old output data should be cleared
    if (!_isRestoring && state != ExecutionState::Completed && _scene != nullptr) {
        // Clear own output data and propagate nullptr downstream to break old data chains
        unsigned int outCount = nPorts(PortType::Out);
        for (PortIndex idx = 0; idx < outCount; ++idx) {
            auto it = _outputData.find(idx);
            if (it != _outputData.end()) {
                if (it->second != nullptr) {
                    it->second = nullptr;
                }
            }
            // Always emit dataUpdated(idx) to force downstream nodes to clear their inputs and transition to Idle
            Q_EMIT dataUpdated(idx);
        }
        // Note: dataUpdated(nullptr) signals above will be caught by ExecutableDataFlowGraphModel::onOutPortDataUpdated,
        // which calls setPortData(nullptr) -> setInData(nullptr) on downstream nodes.
        // The setInData(nullptr) early-exit in Automatic mode will then set those nodes to Idle.
    }
}

bool ExecutableNodeDelegateModel::isPending() const
{
    // Get number of input ports
    unsigned int inPortCount = nPorts(PortType::In);

    // No input ports - cannot be pending
    if (inPortCount == 0) {
        return false;
    }

    // Check each input port
    bool hasConnectedPort = false;
    bool allConnectedPortsHaveData = true;

    if (_scene == nullptr) {
        return false;
    }

    auto &graphModel = _scene->graphModel();

    for (PortIndex index = 0; index < inPortCount; ++index) {
        // Check if this port has any connection
        auto connections = graphModel.connections(_nodeId, PortType::In, index);
        bool isConnected = !connections.empty();

        if (isConnected) {
            hasConnectedPort = true;

            // Check if there's data for this port
            auto it = _inputData.find(index);
            bool hasData = (it != _inputData.end()) && (it->second != nullptr);

            if (!hasData) {
                allConnectedPortsHaveData = false;
            }
        }
    }

    // Port with no connection is Idle, not Pending
    if (!hasConnectedPort) {
        return false;
    }

    // Pending if: connected but no data, or multi-port but not all have data
    return !allConnectedPortsHaveData;
}

QVector<ParameterInfo> ExecutableNodeDelegateModel::getParameters() const
{
    // Default implementation: no parameters
    return {};
}

void ExecutableNodeDelegateModel::setParameter(const QString& paramName, const QString& value)
{
    // Default implementation: do nothing (parameters are read-only)
    Q_UNUSED(paramName);
    Q_UNUSED(value);
}

} // namespace QtNodes
