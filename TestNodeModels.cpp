#include "TestNodeModels.h"

namespace QtNodes {

// ========== NumberSourceDataModel implementation ==========
NumberSourceDataModel::NumberSourceDataModel()
    : NodeDelegateModel()
{
}

QString NumberSourceDataModel::caption() const
{
    return QStringLiteral("Number Source");
}

unsigned int NumberSourceDataModel::nPorts(PortType portType) const
{
    return (portType == PortType::Out) ? 1 : 0;
}

NodeDataType NumberSourceDataModel::dataType(PortType, PortIndex) const
{
    return NodeDataType{"decimal", "Number"};
}

std::shared_ptr<NodeData> NumberSourceDataModel::outData(PortIndex)
{
    return std::make_shared<DecimalData>(_number);
}

QWidget *NumberSourceDataModel::embeddedWidget()
{
    if (!_widget)
    {
        _widget = new QLineEdit();
        _widget->setMaximumSize(100, 20);
        _widget->setText(QString::number(_number));

        QObject::connect(_widget, &QLineEdit::textChanged,
                this, [this](const QString &text) {
            bool ok = false;
            double d = text.toDouble(&ok);
            if (ok)
            {
                _number = d;
                emit dataUpdated(0);
            }
        });
    }
    return _widget;
}

// ========== NumberDisplayDataModel implementation ==========
NumberDisplayDataModel::NumberDisplayDataModel()
    : NodeDelegateModel()
{
}

QString NumberDisplayDataModel::caption() const
{
    return QStringLiteral("Display");
}

unsigned int NumberDisplayDataModel::nPorts(PortType portType) const
{
    return (portType == PortType::In) ? 1 : 0;
}

NodeDataType NumberDisplayDataModel::dataType(PortType, PortIndex) const
{
    return NodeDataType{"decimal", "Number"};
}

void NumberDisplayDataModel::setInData(std::shared_ptr<NodeData> data, PortIndex)
{
    _numberData = std::dynamic_pointer_cast<DecimalData>(data);

    if (_numberData)
    {
        _label->setText(QString::number(_numberData->number()));
    }
    else
    {
        _label->clear();
    }
}

QWidget *NumberDisplayDataModel::embeddedWidget()
{
    if (!_label)
    {
        _label = new QLabel();
        _label->setAlignment(Qt::AlignCenter);
        _label->setMaximumSize(100, 20);
    }
    return _label;
}

// ========== AdditionModel implementation ==========
AdditionModel::AdditionModel()
    : NodeDelegateModel()
{
}

QString AdditionModel::caption() const
{
    return QStringLiteral("Addition");
}

unsigned int AdditionModel::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 2;
    if (portType == PortType::Out) return 1;
    return 0;
}

NodeDataType AdditionModel::dataType(PortType, PortIndex) const
{
    return NodeDataType{"decimal", "Number"};
}

void AdditionModel::setInData(std::shared_ptr<NodeData> data, PortIndex portIndex)
{
    auto numberData = std::dynamic_pointer_cast<DecimalData>(data);

    if (portIndex == 0)
    {
        _number1 = numberData;
    }
    else if (portIndex == 1)
    {
        _number2 = numberData;
    }

    computeResult();
}

std::shared_ptr<NodeData> AdditionModel::outData(PortIndex)
{
    return std::make_shared<DecimalData>(_result);
}

void AdditionModel::computeResult()
{
    double n1 = _number1 ? _number1->number() : 0.0;
    double n2 = _number2 ? _number2->number() : 0.0;
    _result = n1 + n2;

    emit dataUpdated(0);
}

} // namespace QtNodes
