#include "TestNodes.h"
#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QVBoxLayout>
#include <QThread>
#include <QTimer>
#include <QtWidgets/QProgressBar>

namespace QtNodes {

// ============================================================================
// Simple Source Node Implementation (Executable Version)
// ============================================================================
SimpleSourceNode::SimpleSourceNode()
    : ExecutableNodeDelegateModel()
    , _edit(new QLineEdit("Hello World"))
    , _value("Hello World")
    , _data(std::make_shared<SimpleData>(_value))
    , _stopRequested(false)
{
    _edit->setPlaceholderText("输入文本...");
    _edit->setObjectName("sourceLineEdit");

    // 设置默认为自动模式
    setExecutionMode(ExecutionMode::Automatic);
}

QJsonObject SimpleSourceNode::save() const
{
    QJsonObject modelJson;
    modelJson["text"] = QJsonValue::fromVariant(_value);
    return modelJson;
}

void SimpleSourceNode::load(QJsonObject const &p)
{
    _value = p["text"].toString("Hello World");
    _edit->setText(_value);
}

unsigned int SimpleSourceNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 0;
    else
        return 1;
}

NodeDataType SimpleSourceNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    // Output port shows "Out Data"
    return NodeDataType{"simple", "Out Data"};
}

std::shared_ptr<NodeData> SimpleSourceNode::outData(PortIndex port)
{
    // Try base class first (checks _outputData)
    auto data = ExecutableNodeDelegateModel::outData(port);
    if (data) {
        return data;
    }
    // Fall back to _data for backwards compatibility
    return _data;
}

void SimpleSourceNode::onTextChanged(const QString &text)
{
    _value = text;
    auto data = std::make_shared<SimpleData>(_value);
    _data = data;
    setOutputData(0, data);
    _dataModified = true;

    if (executionMode() == ExecutionMode::Automatic) {
        Q_EMIT dataUpdated(0);
        completeAutomaticExecution();
        _dataModified = false;
    } else {
        // Manual mode: data changed, invalidate previous execution result
        invalidateExecution();
    }
}

QWidget *SimpleSourceNode::embeddedWidget()
{
    if (_widget != nullptr) {
        return _widget;
    }

    QWidget *mainWidget = new QWidget();
    auto layout = new QVBoxLayout(mainWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    connect(_edit, &QLineEdit::textChanged, this, &SimpleSourceNode::onTextChanged);

    layout->addWidget(_edit);

    mainWidget->setLayout(layout);
    mainWidget->adjustSize();
    mainWidget->setMaximumWidth(100 + 0);

    _widget = mainWidget;
    return _widget;
}

void SimpleSourceNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = _mode;
    ExecutableNodeDelegateModel::setExecutionMode(mode);

    // If switching from Manual → Automatic and data was modified, trigger automatic execution immediately
    if (oldMode == ExecutionMode::Manual && mode == ExecutionMode::Automatic && _dataModified) {
        processAutomatically();
        _dataModified = false;
    }
}

QVector<ParameterInfo> SimpleSourceNode::getParameters() const
{
    QVector<ParameterInfo> params;

    ParameterInfo param;
    param.name = "Source Text";
    param.dataType = "Simple Data";
    param.value = _edit->text();
    param.editType = FieldEditType::Text;
    params.append(param);

    return params;
}

void SimpleSourceNode::setParameter(const QString& paramName, const QString& value)
{
    if (paramName == "Source Text") {
        _edit->setText(value);
        // The onTextChanged slot will be triggered, updating _value and data
    }
}

// 执行实现
void SimpleSourceNode::execute()
{
    _stopRequested = false;
    _currentProgress = 0;

    // Simulate work with a timer
    QTimer::singleShot(100, this, &SimpleSourceNode::simulateWorkStep);
}

void SimpleSourceNode::stopExecution()
{
    _stopRequested = true;
}

void SimpleSourceNode::simulateWorkStep()
{
    if (_stopRequested) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }

    _currentProgress += 10;
    setProgress(_currentProgress);

    if (_currentProgress >= 100) {
        _data = std::make_shared<SimpleData>(_value);
        setOutputData(0, _data);
        finishExecution();
    } else {
        QTimer::singleShot(100, this, &SimpleSourceNode::simulateWorkStep);
    }
}

void SimpleSourceNode::processAutomatically()
{
    // 自动模式：立即设置输出数据并完成
    auto data = std::make_shared<SimpleData>(_value);
    _data = data;
    setOutputData(0, data);
    Q_EMIT dataUpdated(0);
    completeAutomaticExecution();
    _dataModified = false;
}

// ============================================================================
// Simple Math Node Implementation (Executable Version)
// ============================================================================
SimpleMathNode::SimpleMathNode()
    : ExecutableNodeDelegateModel()
    , _output(std::make_shared<SimpleData>())
    , _label(new QLabel("Waiting for input..."))
    , _stopRequested(false)
{
    _label->setAlignment(Qt::AlignCenter);
    _label->setMinimumWidth(100);
    _label->setObjectName("mathLabel");

    // 设置默认为自动模式
    setExecutionMode(ExecutionMode::Automatic);
}

QJsonObject SimpleMathNode::save() const
{
    return QJsonObject();
}

void SimpleMathNode::load(QJsonObject const &p)
{
    Q_UNUSED(p);
}

unsigned int SimpleMathNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 1;
}

NodeDataType SimpleMathNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In) {
        // Input ports show "In Data"
        return NodeDataType{"simple", "In Data"};
    } else {
        // Output port shows "Out Data"
        return NodeDataType{"simple", "Out Data"};
    }
}

std::shared_ptr<NodeData> SimpleMathNode::outData(PortIndex port)
{
    return _output;
}

void SimpleMathNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    // Check if data actually changed
    auto input1Prev = _input1.lock();
    auto input2Prev = _input2.lock();
    bool changed = false;

    // First update our own storage
    if (port == 0) {
        auto newInput1 = std::dynamic_pointer_cast<SimpleData>(data);
        if (input1Prev != newInput1) {
            changed = true;
        }
        _input1 = newInput1;
    }
    else if (port == 1) {
        auto newInput2 = std::dynamic_pointer_cast<SimpleData>(data);
        if (input2Prev != newInput2) {
            changed = true;
        }
        _input2 = newInput2;
    }

    if (changed && executionMode() == ExecutionMode::Manual) {
        _dataModified = true;
    }

    // Then call base class implementation - it stores data in _inputData map
    // and handles automatic mode processing automatically (will call processAutomatically)
    ExecutableNodeDelegateModel::setInData(data, port);
}

QWidget *SimpleMathNode::embeddedWidget()
{
    if (_widget != nullptr) {
        return _widget;
    }

    QWidget *mainWidget = new QWidget();
    auto layout = new QVBoxLayout(mainWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    _label->setMargin(3);
    _label->setMinimumWidth(100);
    layout->addWidget(_label);

    mainWidget->setLayout(layout);
    mainWidget->adjustSize();
    mainWidget->setMaximumWidth(100 + 6);

    _widget = mainWidget;
    return _widget;
}

// 执行实现
void SimpleMathNode::execute()
{
    _stopRequested = false;
    _currentProgress = 0;

    // 检查是否有所需的输入，已经存储在基类 _inputData 中
    auto input1Data = getInputData(0);
    auto input2Data = getInputData(1);
    auto input1 = std::dynamic_pointer_cast<SimpleData>(input1Data);
    auto input2 = std::dynamic_pointer_cast<SimpleData>(input2Data);

    _input1 = input1;
    _input2 = input2;

    // Simulate work with a timer
    QTimer::singleShot(100, this, &SimpleMathNode::simulateWorkStep);
}

void SimpleMathNode::stopExecution()
{
    _stopRequested = true;
}

void SimpleMathNode::simulateWorkStep()
{
    if (_stopRequested) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }

    _currentProgress += 10;
    setProgress(_currentProgress);

    if (_currentProgress >= 100) {
        auto input1 = _input1.lock();
        auto input2 = _input2.lock();

        QString result = "";
        if (input1) {
            result += input1->value();
        }
        if (input1 && input2) {
            result += " + ";
        }
        if (input2) {
            result += input2->value();
        }

        auto data = std::make_shared<SimpleData>(result);
        _output = data;
        setOutputData(0, data);
        if (_label) {
            _label->setText(result);
        }
        finishExecution();
    } else {
        QTimer::singleShot(100, this, &SimpleMathNode::simulateWorkStep);
    }
}

void SimpleMathNode::processAutomatically()
{
    // 自动模式：等待输入数据变化时自动处理
    auto input1 = _input1.lock();
    auto input2 = _input2.lock();

    if (input1 && input2) {
        QString result = input1->value() + " + " + input2->value();
        auto data = std::make_shared<SimpleData>(result);
        _output = data;
        setOutputData(0, data);
        _label->setText(result);
        Q_EMIT dataUpdated(0);
        completeAutomaticExecution();
    } else {
        _label->setText("Waiting for input...");
    }
    _dataModified = false;
}

void SimpleMathNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = _mode;
    ExecutableNodeDelegateModel::setExecutionMode(mode);

    // If switching from Manual → Automatic and data was modified, trigger automatic execution immediately
    if (oldMode == ExecutionMode::Manual && mode == ExecutionMode::Automatic && _dataModified) {
        processAutomatically();
        _dataModified = false;
    }
}

// ============================================================================
// Simple Display Node Implementation (Executable Version)
// ============================================================================
SimpleDisplayNode::SimpleDisplayNode()
    : ExecutableNodeDelegateModel()
    , _label(new QLabel("No input"))
    , _stopRequested(false)
{
    _label->setAlignment(Qt::AlignCenter);
    _label->setMinimumWidth(100);
    _label->setObjectName("displayLabel");

    // 设置默认为自动模式
    setExecutionMode(ExecutionMode::Automatic);
}

QJsonObject SimpleDisplayNode::save() const
{
    return QJsonObject();
}

void SimpleDisplayNode::load(QJsonObject const &p)
{
    Q_UNUSED(p);
}

unsigned int SimpleDisplayNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 0;
}

NodeDataType SimpleDisplayNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    // Input port shows "In Data"
    return NodeDataType{"simple", "In Data"};
}

std::shared_ptr<NodeData> SimpleDisplayNode::outData(PortIndex port)
{
    return nullptr;
}

void SimpleDisplayNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    // Check if data actually changed
    auto prevInput = _input.lock();
    auto newInput = std::dynamic_pointer_cast<SimpleData>(data);
    if (prevInput != newInput) {
        _dataModified = true;
    }

    // First update our own storage
    Q_UNUSED(port);
    _input = newInput;

    if (_dataModified && executionMode() == ExecutionMode::Manual) {
        invalidateExecution();
    }

    // Then call base class implementation - it stores data in _inputData map
    // and handles automatic mode processing automatically (will call processAutomatically)
    ExecutableNodeDelegateModel::setInData(data, port);
}

QWidget *SimpleDisplayNode::embeddedWidget()
{
    if (_widget != nullptr) {
        return _widget;
    }

    QWidget *mainWidget = new QWidget();
    auto layout = new QVBoxLayout(mainWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    _label->setMargin(3);
    _label->setMinimumWidth(100);
    layout->addWidget(_label);

    mainWidget->setLayout(layout);
    mainWidget->adjustSize();
    mainWidget->setMaximumWidth(100 + 6);

    _widget = mainWidget;
    return _widget;
}

// 执行实现
void SimpleDisplayNode::execute()
{
    _stopRequested = false;
    _currentProgress = 0;

    // Get the cached input data from base class
    auto inputData = getInputData(0);
    _cachedData = std::dynamic_pointer_cast<SimpleData>(inputData);
    _input = _cachedData;

    // Simulate work with a timer using singleShot like upstream
    QTimer::singleShot(100, this, &SimpleDisplayNode::simulateWorkStep);
}

void SimpleDisplayNode::simulateWorkStep()
{
    if (_stopRequested) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }

    _currentProgress += 10;
    setProgress(_currentProgress);

    if (_currentProgress >= 100) {
        if (_label) {
            if (_cachedData) {
                _label->setText(_cachedData->value());
            } else {
                _label->setText("No input");
            }
            _label->adjustSize();
        }
        finishExecution();
    } else {
        QTimer::singleShot(100, this, &SimpleDisplayNode::simulateWorkStep);
    }
}

void SimpleDisplayNode::stopExecution()
{
    _stopRequested = true;
}

void SimpleDisplayNode::processAutomatically()
{
    // Update label with current input value in automatic mode
    auto inputData = getInputData(0);
    _cachedData = std::dynamic_pointer_cast<SimpleData>(inputData);

    if (_label) {
        if (_cachedData) {
            _label->setText(_cachedData->value());
        } else {
            _label->setText("No input");
        }
        _label->adjustSize();
    }
    _dataModified = false;
}

void SimpleDisplayNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = _mode;
    ExecutableNodeDelegateModel::setExecutionMode(mode);

    // If switching from Manual → Automatic and data was modified, trigger automatic execution immediately
    if (oldMode == ExecutionMode::Manual && mode == ExecutionMode::Automatic && _dataModified) {
        processAutomatically();
        _dataModified = false;
    }
}

} // namespace QtNodes
