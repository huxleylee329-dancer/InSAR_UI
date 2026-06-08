#ifndef TESTNODES_H
#define TESTNODES_H

#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeData>
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include "CardExecutableNodeDelegateModel.hpp"
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

    QString getSummary() const override
    {
        return _value;
    }

    QVector<DataField> getFields() const override
    {
        QVector<DataField> fields;
        // 可编辑数值字段
        DataField valueField;
        valueField.key = "Value";
        valueField.value = _value;
        valueField.editType = FieldEditType::Text;
        fields.append(valueField);
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        if (key == "Value") {
            _value = value;
            return true;
        }
        return false;
    }

private:
    QString _value;
};


// ============================================================================
// Card-based Simple Source Node Implementation (New Card Layout)
// ============================================================================
class CardSimpleSourceNode : public CardExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    CardSimpleSourceNode();
    ~CardSimpleSourceNode() override = default;

    QString caption() const override { return QStringLiteral("Card Source"); }
    QString name() const override { return QStringLiteral("Card Simple Source"); }

    QJsonObject save() const override;
    void load(QJsonObject const &p) override;

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void onTextChanged(const QString &text);
    QWidget *embeddedWidget() override;

    void setExecutionMode(ExecutionMode mode) override;
    QVector<ParameterInfo> getParameters() const override;
    void setParameter(const QString& paramName, const QString& value) override;

    // Execution implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

private:
    void simulateWorkStep();

private:
    QLineEdit *_edit;
    QString _value;
    std::shared_ptr<SimpleData> _data;
    bool _stopRequested = false;
    bool _dataModified = false;
};

// ============================================================================
// Card-based Simple Math Node Implementation (New Card Layout)
// ============================================================================
class CardSimpleMathNode : public CardExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    CardSimpleMathNode();
    ~CardSimpleMathNode() override = default;

    QString caption() const override { return QStringLiteral("Card Math (Concat)"); }
    QString name() const override { return QStringLiteral("Card Simple Math"); }

    QJsonObject save() const override;
    void load(QJsonObject const &p) override;

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    QWidget *embeddedWidget() override;

    // Execution implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    void setExecutionMode(ExecutionMode mode) override;

private:
    void simulateWorkStep();

private:
    std::shared_ptr<SimpleData> _output;
    QLabel *_label;
    std::weak_ptr<SimpleData> _input1;
    std::weak_ptr<SimpleData> _input2;
    bool _stopRequested = false;
    bool _dataModified = false;
    std::shared_ptr<SimpleData> _cachedData;
};

// ============================================================================
// Card-based Simple Display Node Implementation (New Card Layout)
// ============================================================================
class CardSimpleDisplayNode : public CardExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    CardSimpleDisplayNode();
    ~CardSimpleDisplayNode() override = default;

    QString caption() const override { return QStringLiteral("Card Display"); }
    QString name() const override { return QStringLiteral("Card Simple Display"); }

    QJsonObject save() const override;
    void load(QJsonObject const &p) override;

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    QWidget *embeddedWidget() override;

    // Execution implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    void setExecutionMode(ExecutionMode mode) override;

private:
    void simulateWorkStep();

private:
    QLabel *_label;
    std::weak_ptr<SimpleData> _input;
    bool _stopRequested = false;
    bool _dataModified = false;
    std::shared_ptr<SimpleData> _cachedData;
};

} // namespace QtNodes

#endif // TESTNODES_H
