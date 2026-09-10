
#include "ExecutableNodeDelegateModel.hpp"
#include "NodeGraphicsObject.hpp"
#include "BasicGraphicsScene.hpp"
#include <QMessageBox>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include "DataFlowGraphModel.hpp"

#include <memory>
#include <unordered_map>

namespace QtNodes {

// A graph owns one project workflow.  Keep commit serialization inside that
// project so independent open projects cannot block one another.
static std::mutex g_commitLeaseRegistryMutex;
static std::unordered_map<BasicGraphicsScene const*, std::shared_ptr<std::mutex>> g_projectCommitLeaseMutexes;
static std::shared_ptr<std::mutex> g_unboundCommitLeaseMutex = std::make_shared<std::mutex>();

static std::shared_ptr<std::mutex> projectCommitLeaseMutex(BasicGraphicsScene const* scene)
{
    if (scene == nullptr) return g_unboundCommitLeaseMutex;
    std::lock_guard<std::mutex> guard(g_commitLeaseRegistryMutex);
    auto& mutex = g_projectCommitLeaseMutexes[scene];
    if (!mutex) mutex = std::make_shared<std::mutex>();
    return mutex;
}

ExecutableNodeDelegateModel::ExecutableNodeDelegateModel()
    : _mode(ExecutionMode::Automatic)
    , _state(ExecutionState::Idle)
    , _widget(nullptr)
    , _progress(0)
    , _nodeId(NodeId())
    , _scene(nullptr)
    , _lastErrorMessage("")
    , _lastWarningMessage("")
{
}

ExecutableNodeDelegateModel::OutputCommitLease::OutputCommitLease(
    ExecutableNodeDelegateModel* owner, std::unique_lock<std::mutex>&& lock)
    : _owner(owner)
    , _lock(std::move(lock))
{
}

ExecutableNodeDelegateModel::OutputCommitLease::OutputCommitLease(OutputCommitLease&& other) noexcept
    : _owner(other._owner)
    , _lock(std::move(other._lock))
{
    other._owner = nullptr;
}

ExecutableNodeDelegateModel::OutputCommitLease&
ExecutableNodeDelegateModel::OutputCommitLease::operator=(OutputCommitLease&& other) noexcept
{
    if (this != &other) {
        release();
        _owner = other._owner;
        _lock = std::move(other._lock);
        other._owner = nullptr;
    }
    return *this;
}

ExecutableNodeDelegateModel::OutputCommitLease::~OutputCommitLease()
{
    release();
}

void ExecutableNodeDelegateModel::OutputCommitLease::release()
{
    if (_owner) {
        _owner->releaseOutputCommitLease(_lock);
        _owner = nullptr;
    }
}

ExecutableNodeDelegateModel::OutputCommitLease
ExecutableNodeDelegateModel::acquireOutputCommitLease(std::uint64_t revision)
{
    std::unique_lock<std::mutex> lock(*projectCommitLeaseMutex(_scene));
    if (revision == 0 || revision != _executionRevision.load() ||
        _commitInvalidationRequested.load()) {
        return OutputCommitLease();
    }
    _commitLeaseActive.store(true);
    return OutputCommitLease(this, std::move(lock));
}

void ExecutableNodeDelegateModel::releaseOutputCommitLease(std::unique_lock<std::mutex>& lock)
{
    _commitLeaseActive.store(false);
    const bool invalidateAfterCommit = _commitInvalidationRequested.exchange(false);
    if (lock.owns_lock()) {
        lock.unlock();
    }
    if (invalidateAfterCommit) {
        QTimer::singleShot(0, this, [this]() { invalidateExecution(); });
    }
}

void ExecutableNodeDelegateModel::setNodeContext(NodeId nodeId, BasicGraphicsScene *scene)
{
    _nodeId = nodeId;
    _scene = scene;
}

QString ExecutableNodeDelegateModel::outputNodeNameForPaste(QJsonObject const& json) const
{
    const QString key = outputNodeNameJsonKey();
    return key.isEmpty() ? QString() : json.value(key).toString().trimmed();
}

QString ExecutableNodeDelegateModel::uniquePastedOutputNodeName(const QString& sourceName,
                                                                 PasteContext& context) const
{
    QString baseName = sourceName.trimmed();
    const auto isDirectProjectChildName = [](const QString& value) {
        return !value.isEmpty() && value != QStringLiteral(".") && value != QStringLiteral("..") &&
            !value.contains(QLatin1Char('/')) && !value.contains(QLatin1Char('\\'));
    };
    if (!isDirectProjectChildName(baseName)) {
        baseName = name();
    }

    const auto conflicts = [&context](const QString& candidate) {
        if (context.reservedOutputNodeNames.contains(candidate.toCaseFolded())) {
            return true;
        }
        if (context.projectDirectory.isEmpty()) {
            return false;
        }

        const QDir root(context.projectDirectory);
        return root.exists(candidate) ||
            QFileInfo::exists(root.absoluteFilePath(
                QStringLiteral(".node_transactions/%1.json").arg(candidate)));
    };

    QString candidate = baseName + QStringLiteral("_copy");
    int suffix = 2;
    while (conflicts(candidate)) {
        candidate = baseName + QStringLiteral("_copy_%1").arg(suffix++);
    }

    context.reservedOutputNodeNames.insert(candidate.toCaseFolded());
    return candidate;
}

void ExecutableNodeDelegateModel::prepareForPaste(QJsonObject& json, PasteContext& context) const
{
    const QString outputNameKey = outputNodeNameJsonKey();
    if (!outputNameKey.isEmpty() && json.contains(outputNameKey)) {
        QString sourceName = json.value(outputNameKey).toString().trimmed();
        if (sourceName.isEmpty()) sourceName = name();
        json.insert(outputNameKey, uniquePastedOutputNodeName(sourceName, context));
    }

    // Persisted terminal state is an artifact reference, not clone
    // configuration.  A pasted Disabled node keeps its mode but cannot restore
    // the source artifact because the saved state is now Idle.
    json.insert(QStringLiteral("execution-state"), static_cast<int>(ExecutionState::Idle));

    // These keys conventionally represent a prior execution result rather
    // than editable configuration.  Node-specific subclasses may clear
    // additional snapshots in their prepareForPaste override.
    const QStringList runtimeSnapshotKeys = {
        QStringLiteral("last-warning-message"),
        QStringLiteral("last-error-message"),
        QStringLiteral("execution-progress"),
        QStringLiteral("outputPaths"),
        QStringLiteral("outputFiles"),
        QStringLiteral("masterOutputPath"),
        QStringLiteral("processingWarning"),
        QStringLiteral("processingQualityWarnings"),
        QStringLiteral("registrationOffsets")
    };
    for (const QString& key : runtimeSnapshotKeys) {
        json.remove(key);
    }
}

QSet<QString> ExecutableNodeDelegateModel::workflowDeclaredDemLabels() const
{
    QSet<QString> declared;
    if (_scene == nullptr) return declared;
    auto* graph = dynamic_cast<DataFlowGraphModel*>(&_scene->graphModel());
    if (graph == nullptr) return declared;
    for (const NodeId nodeId : graph->allNodeIds()) {
        auto* model = graph->delegateModel<ExecutableNodeDelegateModel>(nodeId);
        if (!model || model->name() != QStringLiteral("DEMSource")) continue;
        const QJsonObject modelJson = model->save();
        const QString label = modelJson.value(QStringLiteral("workflowDemLabel")).toString().trimmed();
        if (!label.isEmpty()) declared.insert(label.toCaseFolded());
    }
    return declared;
}

QHash<QString, QString> ExecutableNodeDelegateModel::workflowDemProducerNodeIdMap() const
{
    QHash<QString, QString> producerNodeIds;
    if (_scene == nullptr) return producerNodeIds;
    auto* graph = dynamic_cast<DataFlowGraphModel*>(&_scene->graphModel());
    if (graph == nullptr) return producerNodeIds;
    for (const NodeId nodeId : graph->allNodeIds()) {
        auto* model = graph->delegateModel<ExecutableNodeDelegateModel>(nodeId);
        if (!model || model->name() != QStringLiteral("DEMSource")) continue;
        const QJsonObject modelJson = model->save();
        const QString identity = modelJson.value(QStringLiteral("workflowDemProducerIdentity")).toString().trimmed();
        if (!identity.isEmpty()) {
            producerNodeIds.insert(identity, QString::number(static_cast<quint32>(nodeId)));
        }
    }
    return producerNodeIds;
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
    if (_mode == mode) {
        return;
    }

    const bool wasDisabled = _mode == ExecutionMode::Disabled;
    _mode = mode;
    auto* graph = _scene == nullptr
        ? nullptr : dynamic_cast<DataFlowGraphModel*>(&_scene->graphModel());
    if (mode == ExecutionMode::Disabled) {
        ++_executionRevision;
        _restartAfterInputChange = false;
        _restartScheduled = false;
        if (_state == ExecutionState::Running) {
            stopExecution();
        }
        setState(ExecutionState::Disabled);
        if (graph != nullptr) {
            QString ignored;
            graph->setNodeConnectionsDormant(_nodeId, true, &ignored);
        }
    } else if (wasDisabled) {
        QString restoreFailure;
        const bool restored = graph == nullptr ||
            graph->setNodeConnectionsDormant(_nodeId, false, &restoreFailure);
        if (!restored) {
            setLastErrorMessage(restoreFailure.isEmpty()
                ? QStringLiteral("One or more disabled workflow edges could not be restored.")
                : restoreFailure);
            setState(ExecutionState::Error);
        } else if (mode == ExecutionMode::Automatic && nPorts(PortType::In) == 0) {
            setState(ExecutionState::Idle);
            triggerAutoExecution();
        } else {
            setState(mode == ExecutionMode::Automatic && allRequiredPortsConnected()
                ? ExecutionState::Pending : ExecutionState::Idle);
        }
    }
    Q_EMIT modeChanged(mode);
    triggerVisualUpdate();
}

void ExecutableNodeDelegateModel::setInData(std::shared_ptr<NodeData> nodeData, PortIndex const portIndex)
{
    const std::uint64_t incomingRevision = inputRevisionFromGraph(portIndex);
    // Check if data actually changed
    auto it = _inputData.find(portIndex);
    auto revisionIt = _inputRevisions.find(portIndex);
    if (it == _inputData.end() || it->second != nodeData ||
        revisionIt == _inputRevisions.end() || revisionIt->second != incomingRevision) {
        // Data changed
        _inputData[portIndex] = nodeData;
        _inputRevisions[portIndex] = incomingRevision;

        // Skip state changes and auto-execution during restoration
        if (_isRestoring) {
            return;
        }

        if (_mode == ExecutionMode::Manual) {
            // In Manual mode, any input change invalidates previous result
            invalidateExecution();
        }

        if (_mode == ExecutionMode::Automatic) {
            const bool inputChangedWhileRunning = supportsAutomaticRestartAfterInputChange()
                && (_state == ExecutionState::Running);
            if (inputChangedWhileRunning) {
                ++_executionRevision;
                _restartAfterInputChange = true;
                stopExecution();
            }

            // If the incoming data is null, an upstream node was invalidated.
            // 根据所有必需端口是否已连齐，决定将其状态设为 Pending 还是 Idle。
            // 这样当上游重置或仍在运行时，连齐的节点会进入 Pending 状态等待。
            if (nodeData == nullptr) {
                if (allRequiredPortsConnected()) {
                    setState(ExecutionState::Pending);
                } else {
                    setState(ExecutionState::Idle);
                }
                return;
            }

            // A multi-input node must not start until every required input has data.
            // Optional inputs are deliberately excluded from this readiness gate.
            bool allRequiredInputsReady = true;
            unsigned int inPortCount = nPorts(PortType::In);
            for (PortIndex index = 0; index < inPortCount; ++index)
            {
                if (portIsOptional(PortType::In, index))
                {
                    continue;
                }

                auto inputIt = _inputData.find(index);
                if (!isInputBindingValid(index) || inputIt == _inputData.end() || inputIt->second == nullptr)
                {
                    allRequiredInputsReady = false;
                    break;
                }
            }

            if (!allRequiredInputsReady)
            {
                // 如果必需端口已经全部连接，则将其设为 Pending（等待数据）状态；
                // 如果未连齐，则保持 Idle 状态，避免在连线不完整时误导用户为等待中。
                if (allRequiredPortsConnected()) {
                    setState(ExecutionState::Pending);
                } else {
                    setState(ExecutionState::Idle);
                }
                return;
            }

            // An active asynchronous task must acknowledge cancellation before
            // it is allowed to restart with the updated input snapshot.
            if (inputChangedWhileRunning) {
                setState(ExecutionState::Pending);
                return;
            }

            ++_executionRevision;
            // For automatic mode: set running state and zero progress before execution
            setState(ExecutionState::Running);
            _progress = 0;
            Q_EMIT executionStarted();

            // Let subclass do the automatic processing (sets output data if inputs are complete)
            // Mark as auto-triggered so executeProcessing() can skip overwrite popups
            _startFailureMessage.clear();
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
                if (_state != ExecutionState::Pending && !_startFailureMessage.isEmpty()) {
                    Q_EMIT executionStartRejected(_startFailureMessage);
                }
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
    if (_mode == ExecutionMode::Disabled) {
        Q_EMIT executionStartRejected(QStringLiteral("Node is disabled."));
        return;
    }
    if (_state == ExecutionState::Running) {
        return;
    }

    _startFailureMessage.clear();
    if (!prepareToStart()) {
        if (!_startFailureMessage.isEmpty()) {
            Q_EMIT executionStartRejected(_startFailureMessage);
        }
        // start() is an explicit user action regardless of the configured
        // execution mode. It must surface a terminal preparation failure;
        // automatic orchestration uses processAutomatically() instead.
        if (!_startFailureMessage.isEmpty()) setLastErrorMessage(_startFailureMessage);
        setState(ExecutionState::Error);
        return;
    }

    ++_executionRevision;
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

    ++_executionRevision;
    _stopRequested.store(true);
    _targetProgress = _progress;
    _currentShownProgress = _progress;
    if (_progressTimer) {
        _progressTimer->stop();
    }
    stopExecution();
    if (stopExecutionIsAsynchronous()) {
        Q_EMIT progressUpdated(_progress);
        triggerVisualUpdate();
        return;
    }

    _state = ExecutionState::Stopped;
    Q_EMIT executionStopped();
    Q_EMIT executionStateChanged();
    Q_EMIT computingFinished();
    triggerVisualUpdate();
}

void ExecutableNodeDelegateModel::setProgress(int percent)
{
    if (_stopRequested.load() && percent != 0) {
        return;
    }

    if (!_progressTimer) {
        _progressTimer = new QTimer(this);
        connect(_progressTimer, &QTimer::timeout, this, &ExecutableNodeDelegateModel::updateSmoothProgress);
    }

    // Set target progress, ensuring it is monotonic
    if (percent > _targetProgress) {
        _targetProgress = percent;
    }
    
    if (percent == 0) {
        _targetProgress = 0.0;
        _currentShownProgress = 0.0;
        _progress = 0;
        _progressTimer->stop();
        Q_EMIT progressUpdated(0);
        triggerVisualUpdate();
        return;
    }

    if (!_progressTimer->isActive() && _state == ExecutionState::Running && _progress < 100) {
        _progressTimer->start(30); // 30ms interval
    }
}

void ExecutableNodeDelegateModel::updateSmoothProgress()
{
    if (_state != ExecutionState::Running || _progress >= 100) {
        if (_progressTimer) {
            _progressTimer->stop();
        }
        return;
    }

    double diff = _targetProgress - _currentShownProgress;
    if (diff > 0.01) {
        double k = 0.05; // smoothing factor
        _currentShownProgress += diff * k;

        double min_step = 0.05;
        if (diff * k < min_step) {
            _currentShownProgress += min_step;
        }

        if (_currentShownProgress > _targetProgress) {
            _currentShownProgress = _targetProgress;
        }

        int newProg = qRound(_currentShownProgress);
        if (newProg != _progress) {
            _progress = newProg;
            Q_EMIT progressUpdated(_progress);
            triggerVisualUpdate();
        }
    } else {
        if (_currentShownProgress >= _targetProgress && _progressTimer) {
            _progressTimer->stop();
        }
    }
}

void ExecutableNodeDelegateModel::triggerAutoExecution()
{
    if (_mode != ExecutionMode::Automatic || _state == ExecutionState::Disabled) {
        return;
    }

    // For nodes with no input ports (source nodes), trigger automatic processing
    unsigned int inPortCount = nPorts(PortType::In);
    if (inPortCount == 0) {
        processAutomatically();
    }
}

void ExecutableNodeDelegateModel::retryAutomaticExecution()
{
    if (_mode != ExecutionMode::Automatic || _state == ExecutionState::Disabled ||
        _state == ExecutionState::Running) {
        return;
    }

    if (!allRequiredPortsConnected()) {
        setState(ExecutionState::Idle);
        return;
    }

    for (PortIndex index = 0; index < nPorts(PortType::In); ++index) {
        if (portIsOptional(PortType::In, index)) {
            continue;
        }
        auto inputIt = _inputData.find(index);
        if (!isInputBindingValid(index) || inputIt == _inputData.end() || inputIt->second == nullptr) {
            setState(ExecutionState::Pending);
            return;
        }
    }

    ++_executionRevision;
    setState(ExecutionState::Running);
    _progress = 0;
    Q_EMIT executionStarted();
    _startFailureMessage.clear();
    _isAutoTriggered = true;
    _deferAutomaticCompletion = false;
    processAutomatically();
    _isAutoTriggered = false;

    if (_deferAutomaticCompletion) {
        _deferAutomaticCompletion = false;
        return;
    }
    if (_state != ExecutionState::Running) {
        if (_state != ExecutionState::Pending && !_startFailureMessage.isEmpty()) {
            Q_EMIT executionStartRejected(_startFailureMessage);
        }
        Q_EMIT executionStateChanged();
        triggerVisualUpdate();
        return;
    }

    const unsigned int outPortCount = nPorts(PortType::Out);
    bool shouldComplete = outPortCount == 0;
    if (!shouldComplete) {
        for (auto const& pair : _outputData) {
            if (pair.second != nullptr) {
                shouldComplete = true;
                break;
            }
        }
    }

    _progress = shouldComplete ? 100 : 0;
    _state = shouldComplete ? ExecutionState::Completed : ExecutionState::Idle;
    Q_EMIT progressUpdated(_progress);
    Q_EMIT executionStateChanged();
    Q_EMIT computingFinished();
    triggerVisualUpdate();
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

    const auto publishOutputs = [this]() {
        for (auto const &pair : _outputData) {
            Q_EMIT dataUpdated(pair.first);
        }
    };

    // Output finalization holds a per-project commit lease.  Propagating
    // synchronously can start an automatic downstream node which attempts to
    // acquire that same non-recursive lease on this thread.
    if (_commitLeaseActive.load()) {
        QTimer::singleShot(0, this, publishOutputs);
    } else {
        publishOutputs();
    }
}

void ExecutableNodeDelegateModel::finishExecutionWithWarning()
{
    if (_state != ExecutionState::Running) {
        return;
    }

    _progress = 100;
    Q_EMIT progressUpdated(_progress);
    _state = ExecutionState::Warning;
    Q_EMIT executionFinished();
    Q_EMIT executionStateChanged();
    Q_EMIT computingFinished();
    triggerVisualUpdate();

    const auto publishOutputs = [this]() {
        for (auto const &pair : _outputData) {
            Q_EMIT dataUpdated(pair.first);
        }
    };

    if (_commitLeaseActive.load()) {
        QTimer::singleShot(0, this, publishOutputs);
    } else {
        publishOutputs();
    }
}

void ExecutableNodeDelegateModel::deferAutomaticCompletion()
{
    _deferAutomaticCompletion = true;
}

bool ExecutableNodeDelegateModel::discardObsoleteAutomaticExecution()
{
    if (!_restartAfterInputChange) {
        return false;
    }

    if (!_restartScheduled) {
        _restartScheduled = true;
        QTimer::singleShot(0, this, [this]() {
            restartAutomaticExecutionAfterInputChange();
        });
    }
    return true;
}

void ExecutableNodeDelegateModel::restartAutomaticExecutionAfterInputChange()
{
    _restartScheduled = false;
    if (!_restartAfterInputChange || _mode != ExecutionMode::Automatic) {
        return;
    }

    _restartAfterInputChange = false;
    if (!allRequiredPortsConnected()) {
        setState(ExecutionState::Idle);
        return;
    }

    for (PortIndex index = 0; index < nPorts(PortType::In); ++index) {
        if (portIsOptional(PortType::In, index)) {
            continue;
        }
        auto inputIt = _inputData.find(index);
        if (!isInputBindingValid(index) || inputIt == _inputData.end() || inputIt->second == nullptr) {
            setState(ExecutionState::Pending);
            return;
        }
    }

    ++_executionRevision;
    setState(ExecutionState::Running);
    _progress = 0;
    Q_EMIT executionStarted();
    _isAutoTriggered = true;
    _deferAutomaticCompletion = false;
    processAutomatically();
    _isAutoTriggered = false;

    if (_deferAutomaticCompletion || _state != ExecutionState::Running) {
        _deferAutomaticCompletion = false;
        return;
    }

    bool hasOutput = false;
    for (auto const &pair : _outputData) {
        if (pair.second != nullptr) {
            hasOutput = true;
            break;
        }
    }
    setState(hasOutput || nPorts(PortType::Out) == 0
        ? ExecutionState::Completed
        : ExecutionState::Idle);
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
    Q_EMIT executionStarted();
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
    // Do not block cancellation or invalidation behind output finalization.
    std::unique_lock<std::mutex> leaseLock(*projectCommitLeaseMutex(_scene), std::try_to_lock);
    if (!leaseLock.owns_lock()) {
        _commitInvalidationRequested.store(true);
        return;
    }
    ++_executionRevision;

    // Connected automatic nodes wait for re-execution; all other invalid nodes are idle.
    const bool shouldBePending = _mode == ExecutionMode::Automatic
        && nPorts(PortType::In) > 0
        && allRequiredPortsConnected();
    const ExecutionState targetState = shouldBePending
        ? ExecutionState::Pending
        : ExecutionState::Idle;
    const bool progressIsReset = _progress == 0
        && _targetProgress == 0.0
        && _currentShownProgress == 0.0
        && (!_progressTimer || !_progressTimer->isActive());

    if (_state == targetState && progressIsReset) {
        // Already invalid with no stale progress state.
        return;
    }

    setProgress(0);
    setState(targetState);

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
    if (data) {
        const ProductOutputContract contract = productOutputContract(portIndex);
        if (contract.semanticId.isEmpty() || contract.publishedProductTypes.size() != 1) {
            // A node without an explicit, unique output contract cannot publish.
            data.reset();
        } else if (!data->productDescriptor()) {
            QMap<QString, QString> provenance;
            provenance.insert(QStringLiteral("producer"), name());
            provenance.insert(QStringLiteral("output_port"), contract.semanticId);
            data->setProductDescriptor(ProductDescriptor::create(
                contract.publishedProductTypes.first(), contract.schemaId,
                contract.schemaVersion, contract.publishedState, name(), provenance));
        }
        if (data && !validatePublishedDescriptor(contract, data->productDescriptor()).accepted) {
            data.reset();
        }
    }
    _outputData[portIndex] = data;
    auto it = _lastRevisionedOutputData.find(portIndex);
    if (it == _lastRevisionedOutputData.end() || it->second != data) {
        _lastRevisionedOutputData[portIndex] = data;
        ++_outputRevisions[portIndex];
    }
}

void ExecutableNodeDelegateModel::setInputBindingValid(PortIndex portIndex,
                                                        bool valid,
                                                        const QString& reason)
{
    const auto it = _inputBindingValidity.find(portIndex);
    if (it != _inputBindingValidity.end() && it->second == valid) {
        return;
    }
    _inputBindingValidity[portIndex] = valid;
    if (valid || _isRestoring) {
        return;
    }

    _lastErrorMessage = reason;
    setState(allRequiredPortsConnected() ? ExecutionState::Pending : ExecutionState::Idle);
}

bool ExecutableNodeDelegateModel::isInputBindingValid(PortIndex portIndex) const
{
    const auto it = _inputBindingValidity.find(portIndex);
    return it == _inputBindingValidity.end() || it->second;
}

std::uint64_t ExecutableNodeDelegateModel::outputRevision(PortIndex portIndex) const
{
    auto it = _outputRevisions.find(portIndex);
    return it == _outputRevisions.end() ? 0 : it->second;
}

void ExecutableNodeDelegateModel::synchronizeOutputRevision(PortIndex portIndex)
{
    std::shared_ptr<NodeData> data = outData(portIndex);
    auto it = _lastRevisionedOutputData.find(portIndex);
    if (it == _lastRevisionedOutputData.end() || it->second != data) {
        _lastRevisionedOutputData[portIndex] = data;
        ++_outputRevisions[portIndex];
    }
}

void ExecutableNodeDelegateModel::markOutputArtifactChanged(PortIndex portIndex)
{
    _lastRevisionedOutputData[portIndex] = outData(portIndex);
    ++_outputRevisions[portIndex];
}

std::shared_ptr<NodeData> ExecutableNodeDelegateModel::getOutputData(PortIndex portIndex)
{
    auto data = outData(portIndex);
    if (data) {
        return data;
    }
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
    if (_state == ExecutionState::Warning) {
        modelJson["last-warning-message"] = _lastWarningMessage;
    }

    return modelJson;
}

void ExecutableNodeDelegateModel::load(QJsonObject const &json)
{
    NodeDelegateModel::load(json);

    QJsonValue v = json["execution-mode"];
    if (!v.isUndefined()) {
        _mode = static_cast<ExecutionMode>(v.toInt());
    }

    // 恢复状态：只有当保存的是 Completed 或 Warning 状态且输出验证通过才恢复。
    // Warning 表示结果已生成但存在需要保留的提示，因此与 Completed 一样恢复输出。
    QJsonValue stateValue = json["execution-state"];
    if (!stateValue.isUndefined()) {
        ExecutionState savedState = static_cast<ExecutionState>(stateValue.toInt());
        if (_mode == ExecutionMode::Disabled || savedState == ExecutionState::Disabled) {
            _mode = ExecutionMode::Disabled;
            _state = ExecutionState::Disabled;
            _progress = 0;
            Q_EMIT progressUpdated(_progress);
            Q_EMIT executionStateChanged();
            triggerVisualUpdate();
        } else if (savedState == ExecutionState::Completed || savedState == ExecutionState::Warning) {
            // 调用子类验证输出数据
            if (validateAndRestoreOutput()) {
                if (isRestoringAsync()) {
                    // 异步恢复中：暂存工程记录状态，置为 Running 状态等待子类异步收口
                    _pendingSavedState = savedState;
                    _state = ExecutionState::Running;
                    _progress = 0;
                    if (savedState == ExecutionState::Warning) {
                        QString savedWarningMessage = json["last-warning-message"].toString();
                        if (!savedWarningMessage.isEmpty()) {
                            _lastWarningMessage = savedWarningMessage;
                        }
                    }
                    Q_EMIT progressUpdated(_progress);
                    Q_EMIT executionStateChanged();
                    triggerVisualUpdate();
                } else {
                    // 同步恢复成功：状态立即收口并门控广播
                    _pendingSavedState = ExecutionState::Idle;
                    _state = savedState;
                    _progress = 100;
                    if (savedState == ExecutionState::Warning) {
                        QString savedWarningMessage = json["last-warning-message"].toString();
                        if (!savedWarningMessage.isEmpty()) {
                            _lastWarningMessage = savedWarningMessage;
                        }
                    }
                    // 发送信号通知UI更新状态显示
                    Q_EMIT progressUpdated(_progress);
                    Q_EMIT executionStateChanged();
                    triggerVisualUpdate();
                    // 门控单次发布恢复的输出数据至下游
                    publishRestoredOutputs();
                }
            }
            // 否则保持Idle状态
        }
        // 其他状态（Running/Error/Stopped）都重置为Idle
        // 因为重新打开工程时，这些瞬态没有意义
    }
}

void ExecutableNodeDelegateModel::refreshStateAfterRestoration()
{
    // Restoring suppresses input and connection callbacks, so automatic nodes
    // need one explicit readiness check after the complete graph is available.
    if (_mode != ExecutionMode::Automatic || _state != ExecutionState::Idle ||
        nPorts(PortType::In) == 0) {
        return;
    }

    if (!allRequiredPortsConnected()) {
        return;
    }

    for (PortIndex index = 0; index < nPorts(PortType::In); ++index) {
        if (portIsOptional(PortType::In, index)) {
            continue;
        }
        if (!isInputBindingValid(index)) {
            _lastErrorMessage = QStringLiteral("Required input descriptor was rejected during restoration.");
            setState(ExecutionState::Error);
            return;
        }
        const auto input = _inputData.find(index);
        if (input == _inputData.end() || input->second == nullptr) {
            setState(ExecutionState::Pending);
            return;
        }
    }
    setState(ExecutionState::Pending);
}

void ExecutableNodeDelegateModel::setState(ExecutionState state)
{
    // A cancelled worker may report a terminal state after the user disabled
    // its node. Disabled remains authoritative until the mode is changed.
    if (_mode == ExecutionMode::Disabled && state != ExecutionState::Disabled) {
        return;
    }
    if (_state == state) {
        return;
    }
    
    _state = state;
    if (state != ExecutionState::Error) {
        _lastErrorMessage = "";
    }
    if (state != ExecutionState::Warning) {
        _lastWarningMessage = "";
    }
    if (state == ExecutionState::Running) {
        _stopRequested.store(false);
        _progress = 0;
        _targetProgress = 0.0;
        _currentShownProgress = 0.0;
        if (_progressTimer) {
            _progressTimer->stop();
        }
        Q_EMIT progressUpdated(0);
    } else if (state == ExecutionState::Idle) {
        _progress = 0;
        _targetProgress = 0.0;
        _currentShownProgress = 0.0;
        if (_progressTimer) {
            _progressTimer->stop();
        }
        Q_EMIT progressUpdated(0);
    } else if (state == ExecutionState::Completed || state == ExecutionState::Stopped ||
               state == ExecutionState::Error || state == ExecutionState::Disabled) {
        if (_progressTimer) {
            _progressTimer->stop();
        }
    }
    Q_EMIT executionStateChanged();
    triggerVisualUpdate();

    // Dirty propagation: if this node becomes non-Completed (Idle, Running, Error, etc.),
    // all downstream nodes should also become Idle, and old output data should be cleared
    if (!_isRestoring && state != ExecutionState::Completed && state != ExecutionState::Warning && _scene != nullptr) {
        // Clear own output data and propagate nullptr downstream to break old data chains
        unsigned int outCount = nPorts(PortType::Out);
        for (PortIndex idx = 0; idx < outCount; ++idx) {
            invalidateOutputArtifact(idx);
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

std::uint64_t ExecutableNodeDelegateModel::inputRevisionFromGraph(PortIndex portIndex) const
{
    if (_scene == nullptr) {
        return 0;
    }

    auto *graph = dynamic_cast<DataFlowGraphModel*>(&_scene->graphModel());
    if (graph == nullptr) {
        return 0;
    }

    const auto connections = graph->activeConnections(_nodeId, PortType::In, portIndex);
    if (connections.size() != 1) {
        return 0;
    }

    ConnectionId const &connection = *connections.begin();
    auto *source = graph->delegateModel<ExecutableNodeDelegateModel>(connection.outNodeId);
    return source == nullptr ? 0 : source->outputRevision(connection.outPortIndex);
}

void ExecutableNodeDelegateModel::invalidateOutputArtifact(PortIndex portIndex)
{
    _lastRevisionedOutputData[portIndex].reset();
    ++_outputRevisions[portIndex];
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

    auto* graphModel = dynamic_cast<DataFlowGraphModel*>(&_scene->graphModel());
    if (graphModel == nullptr) {
        return false;
    }

    for (PortIndex index = 0; index < inPortCount; ++index) {
        // Check if this port has any connection
        auto connections = graphModel->activeConnections(_nodeId, PortType::In, index);
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

bool ExecutableNodeDelegateModel::allRequiredPortsConnected() const
{
    unsigned int inPortCount = nPorts(PortType::In);
    if (inPortCount == 0) {
        return true;
    }

    if (_scene == nullptr) {
        return false;
    }

    auto* graphModel = dynamic_cast<DataFlowGraphModel*>(&_scene->graphModel());
    if (graphModel == nullptr) {
        return false;
    }
    for (PortIndex index = 0; index < inPortCount; ++index) {
        if (portIsOptional(PortType::In, index)) {
            continue;
        }

        auto connections = graphModel->activeConnections(_nodeId, PortType::In, index);
        if (connections.empty()) {
            return false;
        }
    }
    for (const QList<PortIndex>& group : alternativeInputGroups()) {
        bool connected = false;
        for (const PortIndex index : group) {
            if (!graphModel->activeConnections(_nodeId, PortType::In, index).empty()) {
                connected = true;
                break;
            }
        }
        if (!connected) return false;
    }
    return true;
}

bool ExecutableNodeDelegateModel::hasActiveInputConnection(PortIndex portIndex) const
{
    if (_scene == nullptr) {
        return false;
    }
    auto* graphModel = dynamic_cast<DataFlowGraphModel*>(&_scene->graphModel());
    return graphModel != nullptr &&
        !graphModel->activeConnections(_nodeId, PortType::In, portIndex).empty();
}

void ExecutableNodeDelegateModel::inputConnectionCreated(ConnectionId const &connectionId)
{
    Q_UNUSED(connectionId);
    if (_isRestoring) return;

    if (_mode == ExecutionMode::Automatic) {
        // 当连接建立时，如果所有必需的端口已接齐，则节点应该更新为 Pending 状态以排队等待数据
        if (_state == ExecutionState::Idle || _state == ExecutionState::Pending) {
            if (allRequiredPortsConnected()) {
                setState(ExecutionState::Pending);
            } else {
                setState(ExecutionState::Idle);
            }
        }
    }
}

void ExecutableNodeDelegateModel::inputConnectionDeleted(ConnectionId const &connectionId)
{
    if (_isRestoring) return;

    if (_mode == ExecutionMode::Automatic && supportsAutomaticRestartAfterInputChange()
        && _state == ExecutionState::Running) {
        _restartAfterInputChange = true;
        stopExecution();
    }

    // 当输入连接断开时，我们需要清除缓存的数据
    PortIndex portIndex = connectionId.inPortIndex;
    _inputData[portIndex] = nullptr;
    _inputBindingValidity[portIndex] = false;
    // Optional inputs may affect the produced artifact. A topology change is
    // therefore always an invalidation, never merely a readiness recalculation.
    invalidateExecution();
}

} // namespace QtNodes

