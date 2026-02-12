#ifndef SIMPLETESTNODE_H
#define SIMPLETESTNODE_H

#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeData>
#include <string>

namespace QtNodes {

// Simple data type without Q_OBJECT
class SimpleData : public NodeData
{
public:
    SimpleData() = default;
    SimpleData(int value) : _value(value) {}

    NodeDataType type() const override
    {
        return NodeDataType{"simple", "Simple"};
    }

    int value() const { return _value; }
    void setValue(int value) { _value = value; }

private:
    int _value = 0;
};

// Simple source node without Q_OBJECT
class SimpleSourceNode : public NodeDelegateModel
{
public:
    SimpleSourceNode() = default;
    virtual ~SimpleSourceNode() = default;

    QString caption() const override { return QStringLiteral("Simple Source"); }
    bool captionVisible() const override { return true; }
    QString name() const override { return QStringLiteral("SimpleSource"); }

    unsigned int nPorts(PortType portType) const override
    {
        return (portType == PortType::Out) ? 1 : 0;
    }

    NodeDataType dataType(PortType, PortIndex) const override
    {
        return NodeDataType{"simple", "Simple"};
    }

    std::shared_ptr<NodeData> outData(PortIndex) override
    {
        return std::make_shared<SimpleData>(_value);
    }

    void setInData(std::shared_ptr<NodeData>, PortIndex) override {}

    QWidget *embeddedWidget() override { return nullptr; }

private:
    int _value = 42;
};

} // namespace QtNodes

#endif // SIMPLETESTNODE_H
