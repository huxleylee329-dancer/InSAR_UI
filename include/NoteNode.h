#ifndef NOTENODE_H
#define NOTENODE_H

#include <QtNodes/NodeData>
#include <QtNodes/NodeDelegateModel>

#include <QTextEdit>
#include <QSize>

/**
 * @brief NoteNodeData - 注释节点数据类型
 *
 * 存储注释文本内容
 */
class NoteNodeData : public QtNodes::NodeData
{
public:
    NoteNodeData() = default;
    NoteNodeData(const QString &text) : m_text(text) {}

    QtNodes::NodeDataType type() const override
    {
        return QtNodes::NodeDataType{"Note", "Note"};
    }

    QString text() const { return m_text; }
    void setText(const QString &text) { m_text = text; }

    QString getSummary() const override
    {
        if (m_text.isEmpty()) return "Empty Note";
        QString summary = m_text.left(30);
        if (m_text.length() > 30) summary += "...";
        return summary;
    }

    QVector<QtNodes::DataField> getFields() const override
    {
        QVector<QtNodes::DataField> fields;
        if (!m_text.isEmpty()) {
            QString truncated = m_text;
            if (truncated.length() > 200) truncated = truncated.left(200) + "...";
            fields.append({"Content", truncated, QtNodes::FieldEditType::None});
            fields.append({"Length", QString::number(m_text.length()) + " chars", QtNodes::FieldEditType::None});
        }
        return fields;
    }

    bool setField(const QString& key, const QString& value) override
    {
        Q_UNUSED(key);
        Q_UNUSED(value);
        return false;
    }

private:
    QString m_text;
};

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
