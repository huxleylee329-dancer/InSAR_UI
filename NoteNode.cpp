#include "InSARLogManager.h"
#include "include/NoteNode.h"

#include <QTextEdit>
#include <QSize>
#include <QFont>
#include <QPalette>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QtNodes/internal/BasicGraphicsScene.hpp>

NoteNode::NoteNode()
    : m_textEdit(nullptr)
{
}

NoteNode::~NoteNode()
{
    // m_textEdit 由 QtNodes 在销毁 QGraphicsProxyWidget 时接管，无需手动 delete
}

QWidget *NoteNode::embeddedWidget()
{
    if (!m_textEdit)
    {
        m_textEdit = new QTextEdit();
        // 开启输入法支持
        m_textEdit->setAttribute(Qt::WA_InputMethodEnabled, true);
        m_textEdit->setPlaceholderText("Add a note...");
        m_textEdit->setPlainText("New note");
        m_textEdit->setMinimumWidth(180);
        m_textEdit->setMinimumHeight(80);

        // 监听文本修改以通知场景数据已更新并触发保存
        connect(m_textEdit, &QTextEdit::textChanged, this, &NoteNode::onTextChanged);
    }
    return m_textEdit;
}

QJsonObject NoteNode::save() const
{
    QJsonObject modelJson = NodeDelegateModel::save();

    if (m_textEdit)
        modelJson["noteText"] = m_textEdit->toPlainText();
    else
        modelJson["noteText"] = QString("New note");

    return modelJson;
}

void NoteNode::load(QJsonObject const &json)
{
    QJsonValue v = json["noteText"];

    if (!v.isUndefined())
    {
        QString text = v.toString();
        if (m_textEdit)
            m_textEdit->setPlainText(text);
        else
        {
            embeddedWidget(); 
            if (m_textEdit) m_textEdit->setPlainText(text);
        }
    }
}

void NoteNode::onTextChanged()
{
    // 发送数据更新信号以通知节点工程已修改，触发保存
    Q_EMIT dataUpdated(0);

    // 获取所在的场景并手动触发 modified 信号以将项目标记为 dirty 状态，防止直接退出而不保存
    if (!m_textEdit)
        return;

    auto *proxy = m_textEdit->graphicsProxyWidget();
    if (!proxy)
        return;

    auto *qscene = proxy->scene();
    if (!qscene)
        return;

    if (auto *bscene = dynamic_cast<QtNodes::BasicGraphicsScene*>(qscene))
    {
        Q_EMIT bscene->modified(bscene);
    }
}

