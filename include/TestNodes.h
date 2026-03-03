#ifndef TESTNODES_H
#define TESTNODES_H

#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeData>
#include <QLabel>

namespace QtNodes {

// ============================================================================
// Simple Data Type for Test Nodes
// ============================================================================
class SimpleData : public NodeData
{
public:
    SimpleData() = default;
    explicit SimpleData(const QString& value) : _value(value) {}

    NodeDataType type() const override
    {
        return NodeDataType{"simple", "Simple Data"};
    }

    QString value() const { return _value; }
    void setValue(const QString& value) { _value = value; }

    bool sameType(NodeData const &nodeData) const override
    {
        auto d = dynamic_cast<SimpleData const *>(&nodeData);
        return d != nullptr;
    }

private:
    QString _value;
};

// ============================================================================
// Simple Source Node - Outputs a fixed value
// ============================================================================
class SimpleSourceNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    SimpleSourceNode();
    ~SimpleSourceNode() = default;

    QString caption() const override { return QStringLiteral("Source"); }
    QString name() const override { return QStringLiteral("SimpleSource"); }
    unsigned int nPorts(PortType portType) const override;

    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    QWidget *embeddedWidget() override;

private:
    QLabel *_label;
    QString _value;
};

// ============================================================================
// Simple Math Node - Concatenates input values
// ============================================================================
class SimpleMathNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    SimpleMathNode();
    ~SimpleMathNode() = default;

    QString caption() const override { return QStringLiteral("Math (Concat)"); }
    QString name() const override { return QStringLiteral("SimpleMath"); }
    unsigned int nPorts(PortType portType) const override;

    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    QWidget *embeddedWidget() override;

private:
    std::shared_ptr<SimpleData> _input1;
    std::shared_ptr<SimpleData> _input2;
    std::shared_ptr<SimpleData> _output;
    QLabel *_label;
};

// ============================================================================
// Simple Display Node - Shows the input value
// ============================================================================
class SimpleDisplayNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    SimpleDisplayNode();
    ~SimpleDisplayNode() = default;

    QString caption() const override { return QStringLiteral("Display"); }
    QString name() const override { return QStringLiteral("SimpleDisplay"); }
    unsigned int nPorts(PortType portType) const override;

    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    QWidget *embeddedWidget() override;

private:
    std::shared_ptr<SimpleData> _input;
    QLabel *_label;
};

} // namespace QtNodes

#endif // TESTNODES_H
