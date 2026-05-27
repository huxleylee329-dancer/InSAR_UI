#include "InSARLogManager.h"
#include "include/NoteNode.h"

#include <QTextEdit>
#include <QSize>
#include <QFont>
#include <QPalette>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QJsonValue>

NoteNode::NoteNode()
    : m_textEdit(nullptr)
{
}

NoteNode::~NoteNode()
{
    // m_textEdit is owned by QtNodes via QGraphicsProxyWidget, don't delete
}

QWidget *NoteNode::embeddedWidget()
{
    if (!m_textEdit)
    {
        m_textEdit = new QTextEdit();
        m_textEdit->setPlaceholderText("Add a note...");
        m_textEdit->setPlainText("New note");
        m_textEdit->setMinimumWidth(180);
        m_textEdit->setMinimumHeight(80);

        // Connect to text changes to notify the node that data has changed
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
            // If widget not created yet, we have a problem because embeddedWidget() creates it.
            // Usually load() is called after the model is registered and possibly instantiated.
            // However, it's safer to ensure widget is created if needed, or store the text temporarily.
            embeddedWidget(); 
            if (m_textEdit) m_textEdit->setPlainText(text);
        }
    }
}

void NoteNode::onTextChanged()
{
    // Emit the model's dataUpdated signal to indicate that the node has been modified
    Q_EMIT dataUpdated(0);
}
