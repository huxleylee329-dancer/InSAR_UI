#include "TestNodes.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>

namespace QtNodes {

// ============================================================================
// Simple Source Node Implementation
// ============================================================================
SimpleSourceNode::SimpleSourceNode()
    : _edit(new QLineEdit("Hello World"))
    , _value("Hello World")
{
    _edit->setPlaceholderText("输入文本...");
    _edit->setStyleSheet("QLineEdit { background-color: #4a9acf; border-radius: 5px; padding: 5px; color: white; }");

    connect(_edit, &QLineEdit::textChanged, this, &SimpleSourceNode::onTextChanged);
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
    return SimpleData().type();
}

std::shared_ptr<NodeData> SimpleSourceNode::outData(PortIndex port)
{
    return std::make_shared<SimpleData>(_value);
}

void SimpleSourceNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(data);
    Q_UNUSED(port);
}

void SimpleSourceNode::onTextChanged(const QString &text)
{
    _value = text;
    Q_EMIT dataUpdated(0);
}

QWidget *SimpleSourceNode::embeddedWidget()
{
    return _edit;
}

// ============================================================================
// Simple Math Node Implementation
// ============================================================================
SimpleMathNode::SimpleMathNode()
    : _output(std::make_shared<SimpleData>())
    , _label(new QLabel("Waiting for input..."))
{
    _label->setAlignment(Qt::AlignCenter);
    _label->setStyleSheet("QLabel { background-color: #3a7aaf; border-radius: 5px; padding: 5px; }");
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
    return SimpleData().type();
}

std::shared_ptr<NodeData> SimpleMathNode::outData(PortIndex port)
{
    return _output;
}

void SimpleMathNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0)
        _input1 = std::dynamic_pointer_cast<SimpleData>(data);
    else if (port == 1)
        _input2 = std::dynamic_pointer_cast<SimpleData>(data);

    // Update output
    QString result = "";
    if (_input1)
        result += _input1->value();
    if (_input1 && _input2)
        result += " + ";
    if (_input2)
        result += _input2->value();

    if (_input1 || _input2)
        _output->setValue(result);

    // Update label
    if (_input1 || _input2)
        _label->setText(result);
    else
        _label->setText("Waiting for input...");

    Q_EMIT dataUpdated(0);
}

QWidget *SimpleMathNode::embeddedWidget()
{
    return _label;
}

// ============================================================================
// Simple Display Node Implementation
// ============================================================================
SimpleDisplayNode::SimpleDisplayNode()
    : _label(new QLabel("No input"))
{
    _label->setAlignment(Qt::AlignCenter);
    _label->setStyleSheet("QLabel { background-color: #2a5a8f; border-radius: 5px; padding: 5px; }");
    _label->setMinimumWidth(100);
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
    return SimpleData().type();
}

std::shared_ptr<NodeData> SimpleDisplayNode::outData(PortIndex port)
{
    return nullptr;
}

void SimpleDisplayNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    _input = std::dynamic_pointer_cast<SimpleData>(data);

    if (_input)
        _label->setText(_input->value());
    else
        _label->setText("No input");
}

QWidget *SimpleDisplayNode::embeddedWidget()
{
    return _label;
}

} // namespace QtNodes
