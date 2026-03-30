#ifndef TESTNODES_H
#define TESTNODES_H

#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeData>
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QString>

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
// Simple Source Node - Outputs a fixed value (Executable Version)
// ============================================================================
class SimpleSourceNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    SimpleSourceNode();
    ~SimpleSourceNode() override = default;

    QString caption() const override { return QStringLiteral("Source"); }
    QString name() const override { return QStringLiteral("ExecutableSource"); }

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override {}
    QWidget *embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &p) override;

    void setExecutionMode(ExecutionMode mode) override;

protected:
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

private slots:
    void onTextChanged(const QString &text);

private:
    void simulateWorkStep();

private:
    QLineEdit *_edit;
    QString _value;
    std::shared_ptr<SimpleData> _data;
    bool _stopRequested = false;
    int _currentProgress = 0;
    bool _dataModified = false;  // Data modified but not executed in Manual mode
};

// ============================================================================
// Simple Math Node - Concatenates input values (Executable Version)
// ============================================================================
class SimpleMathNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    SimpleMathNode();
    ~SimpleMathNode() override = default;

    QString caption() const override { return QStringLiteral("Math (Concat)"); }
    QString name() const override { return QStringLiteral("ExecutableMath"); }

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex portIndex) override;

    QWidget *embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &p) override;

    void setExecutionMode(ExecutionMode mode) override;

protected:
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

private:
    void simulateWorkStep();

private:
    std::weak_ptr<SimpleData> _input1;
    std::weak_ptr<SimpleData> _input2;
    std::shared_ptr<SimpleData> _output;
    QLabel *_label;
    bool _stopRequested = false;
    int _currentProgress = 0;
    bool _dataModified = false;  // Input changed but not executed in Manual mode
};

// ============================================================================
// Simple Display Node - Shows the input value (Executable Version)
// ============================================================================
class SimpleDisplayNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    SimpleDisplayNode();
    ~SimpleDisplayNode() override = default;

    QString caption() const override { return QStringLiteral("Display"); }
    QString name() const override { return QStringLiteral("ExecutableDisplay"); }

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex portIndex) override;

    QWidget *embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &p) override;

    void setExecutionMode(ExecutionMode mode) override;

protected:
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

private:
    void simulateWorkStep();

    std::weak_ptr<SimpleData> _input;
    std::shared_ptr<SimpleData> _cachedData;
    QLabel *_label;
    bool _stopRequested = false;
    int _currentProgress = 0;
    bool _dataModified = false;  // Input changed but not executed in Manual mode
};

} // namespace QtNodes

#endif // TESTNODES_H
