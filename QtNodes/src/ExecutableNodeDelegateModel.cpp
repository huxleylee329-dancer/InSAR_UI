#include "ExecutableNodeDelegateModel.hpp"
#include "NodeGraphicsObject.hpp"
#include "BasicGraphicsScene.hpp"

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

        if (_mode == ExecutionMode::Manual) {
            // In Manual mode, any input change invalidates previous result
            invalidateExecution();
        }
    }

    if (_mode == ExecutionMode::Automatic) {
        // For automatic mode: set running state and zero progress before execution
        _state = ExecutionState::Running;
        _progress = 0;
        Q_EMIT executionStateChanged();
        triggerVisualUpdate();

        // Let subclass do the automatic processing (sets output data if inputs are complete)
        processAutomatically();

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

    _state = ExecutionState::Running;
    _progress = 0;
    Q_EMIT executionStarted();
    Q_EMIT executionStateChanged();
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

void ExecutableNodeDelegateModel::finishExecution()
{
    if (_state != ExecutionState::Running) {
        return;
    }

    _state = ExecutionState::Completed;
    Q_EMIT executionFinished();
    Q_EMIT executionStateChanged();
    Q_EMIT computingFinished();
    triggerVisualUpdate();

    for (auto const &pair : _outputData) {
        Q_EMIT dataUpdated(pair.first);
    }
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
    _state = ExecutionState::Idle;
    Q_EMIT progressUpdated(_progress);
    Q_EMIT executionStateChanged();
    triggerVisualUpdate();
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

void ExecutableNodeDelegateModel::setState(ExecutionState state)
{
    _state = state;
    Q_EMIT executionStateChanged();
    triggerVisualUpdate();
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

} // namespace QtNodes
