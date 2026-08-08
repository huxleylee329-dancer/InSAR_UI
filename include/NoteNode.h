#ifndef NOTENODE_H
#define NOTENODE_H

#include <QtNodes/NodeData>
#include <QtNodes/NodeDelegateModel>

#include <QTextEdit>
#include <QSize>

/**
 * @brief NoteNode - 注释节点类
 *
 * 允许用户在工作流中添加文本注释
 * 使用 QTextEdit 作为嵌入控件
 * 支持基本的文本编辑功能
 */
class NoteNode : public QtNodes::NodeDelegateModel
{
    Q_OBJECT

public:
    NoteNode();
    virtual ~NoteNode();

    QString caption() const override
    {
        return "Note";
    }

    QString name() const override
    {
        return "NoteNode";
    }

    unsigned int nPorts(QtNodes::PortType portType) const override
    {
        Q_UNUSED(portType);
        return 0;  // Note nodes have no input or output ports
    }

    QtNodes::NodeDataType dataType(QtNodes::PortType, QtNodes::PortIndex) const override
    {
        return QtNodes::NodeDataType{"", ""};
    }

    QtNodes::ProductInputContract productInputContract(QtNodes::PortIndex) const override
    {
        return QtNodes::ProductInputContract();
    }

    QtNodes::ProductOutputContract productOutputContract(QtNodes::PortIndex) const override
    {
        return QtNodes::ProductOutputContract();
    }

    void setInData(std::shared_ptr<QtNodes::NodeData> nodeData, QtNodes::PortIndex port) override
    {
        Q_UNUSED(nodeData);
        Q_UNUSED(port);
    }

    std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex port) override
    {
        Q_UNUSED(port);
        return nullptr;
    }

    QWidget *embeddedWidget() override;

    QJsonObject save() const override;

    void load(QJsonObject const &json) override;

    bool resizable() const override { return true; }

    QSize minimumSize() const
    {
        return QSize(200, 100);
    }

    QSize sizeHint() const
    {
        return QSize(250, 150);
    }

private slots:
    void onTextChanged();

private:
    QTextEdit *m_textEdit;
};

#endif // NOTENODE_H
