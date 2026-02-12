#ifndef TESTNODEMODELS_H
#define TESTNODEMODELS_H

#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeData>

#include <QLineEdit>
#include <QLabel>

#include <memory>

namespace QtNodes {

// ========== Simple number data type for test ==========
class DecimalData : public NodeData
{
public:
    DecimalData() = default;
    DecimalData(double number) : _number(number) {}

    NodeDataType type() const override
    {
        return NodeDataType{"decimal", "Number"};
    }

    double number() const { return _number; }
    void setNumber(double number) { _number = number; }

private:
    double _number = 0.0;
};

// ========== Simple data source node (for test) ==========
class NumberSourceDataModel : public NodeDelegateModel
{
    Q_OBJECT

public:
    NumberSourceDataModel();
    virtual ~NumberSourceDataModel() = default;

    QString caption() const override;
    bool captionVisible() const override { return true; }
    QString name() const override { return QStringLiteral("NumberSource"); }

    unsigned int nPorts(PortType portType) const override;

    NodeDataType dataType(PortType, PortIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex) override;

    void setInData(std::shared_ptr<NodeData>, PortIndex) override {}

    QWidget *embeddedWidget() override;

signals:
    void dataUpdated(PortIndex);

private:
    double _number = 0.0;
    QLineEdit *_widget = nullptr;
};

// ========== Simple display node (for test) ==========
class NumberDisplayDataModel : public NodeDelegateModel
{
    Q_OBJECT

public:
    NumberDisplayDataModel();
    virtual ~NumberDisplayDataModel() = default;

    QString caption() const override;
    bool captionVisible() const override { return true; }
    QString name() const override { return QStringLiteral("NumberDisplay"); }

    unsigned int nPorts(PortType portType) const override;

    NodeDataType dataType(PortType, PortIndex) const override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex) override;

    std::shared_ptr<NodeData> outData(PortIndex) override { return nullptr; }

    QWidget *embeddedWidget() override;

private:
    std::shared_ptr<DecimalData> _numberData;
    QLabel *_label = nullptr;
};

// ========== Simple compute node (for test) ==========
class AdditionModel : public NodeDelegateModel
{
    Q_OBJECT

public:
    AdditionModel();
    virtual ~AdditionModel() = default;

    QString caption() const override;
    bool captionVisible() const override { return true; }
    QString name() const override { return QStringLiteral("Addition"); }

    unsigned int nPorts(PortType portType) const override;

    NodeDataType dataType(PortType, PortIndex) const override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex portIndex) override;

    std::shared_ptr<NodeData> outData(PortIndex) override;

    QWidget *embeddedWidget() override { return nullptr; }

private:
    void computeResult();

signals:
    void dataUpdated(PortIndex);

private:
    std::shared_ptr<DecimalData> _number1;
    std::shared_ptr<DecimalData> _number2;
    double _result = 0.0;
};

} // namespace QtNodes

#endif // TESTNODEMODELS_H
