#include "TestNodes.h"
#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/ExecutableNodeGeometry.hpp>
#include <QVBoxLayout>
#include <QThread>
#include <QTimer>
#include <QtWidgets/QProgressBar>

namespace QtNodes {

// ============================================================================
// Card-based Simple Source Node Implementation (New Card Layout)
// ============================================================================
CardSimpleSourceNode::CardSimpleSourceNode()
    : CardExecutableNodeDelegateModel()
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

QJsonObject CardSimpleSourceNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["text"] = QJsonValue::fromVariant(_value);
    return modelJson;
}

void CardSimpleSourceNode::load(QJsonObject const &p)
{
    ExecutableNodeDelegateModel::load(p);
    _value = p["text"].toString("Hello World");
    _edit->setText(_value);
}

unsigned int CardSimpleSourceNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 0;
    else
        return 1;
}

NodeDataType CardSimpleSourceNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return NodeDataType{"simple", "Out Data"};
}

std::shared_ptr<NodeData> CardSimpleSourceNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    auto data = ExecutableNodeDelegateModel::outData(port);
    if (data) {
        return data;
    }
    return _data;
}

void CardSimpleSourceNode::onTextChanged(const QString &text)
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
        invalidateExecution();
    }
}

QWidget *CardSimpleSourceNode::embeddedWidget()
{
    if (_widget != nullptr) {
        return _widget;
    }

    QWidget *mainWidget = new QWidget();
    auto layout = new QVBoxLayout(mainWidget);
    layout->setContentsMargins(CARD_MARGIN, CARD_MARGIN, CARD_MARGIN, CARD_MARGIN);
    layout->setSpacing(2);

    connect(_edit, &QLineEdit::textChanged, this, &CardSimpleSourceNode::onTextChanged);

    layout->addWidget(_edit);

    mainWidget->setLayout(layout);
    mainWidget->adjustSize();

    _widget = mainWidget;
    return _widget;
}

void CardSimpleSourceNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = _mode;
    CardExecutableNodeDelegateModel::setExecutionMode(mode);

    if (oldMode == ExecutionMode::Manual && mode == ExecutionMode::Automatic && _dataModified) {
        processAutomatically();
        _dataModified = false;
    }
}

QVector<ParameterInfo> CardSimpleSourceNode::getParameters() const
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

void CardSimpleSourceNode::setParameter(const QString& paramName, const QString& value)
{
    if (paramName == "Source Text") {
        _edit->setText(value);
    }
}

void CardSimpleSourceNode::execute()
{
    _stopRequested = false;
    _progress = 0;

    QTimer::singleShot(100, this, &CardSimpleSourceNode::simulateWorkStep);
}

void CardSimpleSourceNode::stopExecution()
{
    _stopRequested = true;
}

void CardSimpleSourceNode::simulateWorkStep()
{
    if (_stopRequested) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }

    _progress += 10;
    setProgress(_progress);

    if (_progress >= 100) {
        _data = std::make_shared<SimpleData>(_value);
        setOutputData(0, _data);
        finishExecution();
    } else {
        QTimer::singleShot(100, this, &CardSimpleSourceNode::simulateWorkStep);
    }
}

void CardSimpleSourceNode::processAutomatically()
{
    auto data = std::make_shared<SimpleData>(_value);
    _data = data;
    setOutputData(0, data);
    Q_EMIT dataUpdated(0);
    completeAutomaticExecution();
    _dataModified = false;
}

// ============================================================================
// Card-based Simple Math Node Implementation (New Card Layout)
// ============================================================================
CardSimpleMathNode::CardSimpleMathNode()
    : CardExecutableNodeDelegateModel()
    , _output(std::make_shared<SimpleData>())
    , _label(new QLabel("Waiting for input..."))
    , _stopRequested(false)
{
    _label->setAlignment(Qt::AlignCenter);
    _label->setMinimumWidth(100);
    _label->setObjectName("mathLabel");

    setExecutionMode(ExecutionMode::Automatic);
}

QJsonObject CardSimpleMathNode::save() const
{
    return QJsonObject();
}

void CardSimpleMathNode::load(QJsonObject const &p)
{
    Q_UNUSED(p);
}

unsigned int CardSimpleMathNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 1;
}

NodeDataType CardSimpleMathNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In) {
        return NodeDataType{"simple", "In Data"};
    } else {
        return NodeDataType{"simple", "Out Data"};
    }
}

std::shared_ptr<NodeData> CardSimpleMathNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    return _output;
}

void CardSimpleMathNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    auto input1Prev = _input1.lock();
    auto input2Prev = _input2.lock();
    bool changed = false;

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

    CardExecutableNodeDelegateModel::setInData(data, port);
}

QWidget *CardSimpleMathNode::embeddedWidget()
{
    if (_widget != nullptr) {
        return _widget;
    }

    QWidget *mainWidget = new QWidget();
    auto layout = new QVBoxLayout(mainWidget);
    layout->setContentsMargins(CARD_MARGIN, CARD_MARGIN, CARD_MARGIN, CARD_MARGIN);
    layout->setSpacing(4);

    _label->setMargin(3);
    _label->setMinimumWidth(100);
    layout->addWidget(_label);

    mainWidget->setLayout(layout);
    mainWidget->adjustSize();

    _widget = mainWidget;
    return _widget;
}

void CardSimpleMathNode::execute()
{
    _stopRequested = false;
    _progress = 0;

    auto input1Data = getInputData(0);
    auto input2Data = getInputData(1);
    auto input1 = std::dynamic_pointer_cast<SimpleData>(input1Data);
    auto input2 = std::dynamic_pointer_cast<SimpleData>(input2Data);

    _input1 = input1;
    _input2 = input2;

    QTimer::singleShot(100, this, &CardSimpleMathNode::simulateWorkStep);
}

void CardSimpleMathNode::stopExecution()
{
    _stopRequested = true;
}

void CardSimpleMathNode::simulateWorkStep()
{
    if (_stopRequested) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }

    _progress += 10;
    setProgress(_progress);

    if (_progress >= 100) {
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
        QTimer::singleShot(100, this, &CardSimpleMathNode::simulateWorkStep);
    }
}

void CardSimpleMathNode::processAutomatically()
{
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

void CardSimpleMathNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = _mode;
    CardExecutableNodeDelegateModel::setExecutionMode(mode);

    if (oldMode == ExecutionMode::Manual && mode == ExecutionMode::Automatic && _dataModified) {
        processAutomatically();
        _dataModified = false;
    }
}

// ============================================================================
// Card-based Simple Display Node Implementation (New Card Layout)
// ============================================================================
CardSimpleDisplayNode::CardSimpleDisplayNode()
    : CardExecutableNodeDelegateModel()
    , _label(new QLabel("No input"))
    , _stopRequested(false)
{
    _label->setAlignment(Qt::AlignCenter);
    _label->setMinimumWidth(100);
    _label->setObjectName("displayLabel");

    setExecutionMode(ExecutionMode::Automatic);
}

QJsonObject CardSimpleDisplayNode::save() const
{
    return QJsonObject();
}
void CardSimpleDisplayNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);
}

unsigned int CardSimpleDisplayNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 0;
}

NodeDataType CardSimpleDisplayNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return NodeDataType{"simple", "In Data"};
}

std::shared_ptr<NodeData> CardSimpleDisplayNode::outData(PortIndex port)
{
    return nullptr;
}

void CardSimpleDisplayNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    auto prevInput = _input.lock();
    auto newInput = std::dynamic_pointer_cast<SimpleData>(data);
    if (prevInput != newInput) {
        _dataModified = true;
    }

    _input = newInput;

    if (_dataModified && executionMode() == ExecutionMode::Manual) {
        invalidateExecution();
    }

    CardExecutableNodeDelegateModel::setInData(data, port);
}

QWidget *CardSimpleDisplayNode::embeddedWidget()
{
    if (_widget != nullptr) {
        return _widget;
    }

    QWidget *mainWidget = new QWidget();
    auto layout = new QVBoxLayout(mainWidget);
    layout->setContentsMargins(CARD_MARGIN, CARD_MARGIN, CARD_MARGIN, CARD_MARGIN);
    layout->setSpacing(4);

    _label->setMargin(3);
    _label->setMinimumWidth(100);
    layout->addWidget(_label);

    mainWidget->setLayout(layout);
    mainWidget->adjustSize();

    _widget = mainWidget;
    return _widget;
}

void CardSimpleDisplayNode::execute()
{
    _stopRequested = false;
    _progress = 0;

    auto inputData = getInputData(0);
    _cachedData = std::dynamic_pointer_cast<SimpleData>(inputData);
    _input = _cachedData;

    QTimer::singleShot(100, this, &CardSimpleDisplayNode::simulateWorkStep);
}

void CardSimpleDisplayNode::stopExecution()
{
    _stopRequested = true;
}

void CardSimpleDisplayNode::simulateWorkStep()
{
    if (_stopRequested) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }

    _progress += 10;
    setProgress(_progress);

    if (_progress >= 100) {
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
        QTimer::singleShot(100, this, &CardSimpleDisplayNode::simulateWorkStep);
    }
}

void CardSimpleDisplayNode::processAutomatically()
{
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

void CardSimpleDisplayNode::setExecutionMode(ExecutionMode mode)
{
    ExecutionMode oldMode = _mode;
    CardExecutableNodeDelegateModel::setExecutionMode(mode);

    if (oldMode == ExecutionMode::Manual && mode == ExecutionMode::Automatic && _dataModified) {
        processAutomatically();
        _dataModified = false;
    }
}

} // namespace QtNodes
