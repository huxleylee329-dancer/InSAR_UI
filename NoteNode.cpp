#include "include/NoteNode.h"

#include <QTextEdit>
#include <QSize>
#include <QFont>
#include <QPalette>
#include <QVBoxLayout>

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

void NoteNode::onTextChanged()
{
    // Emit the model's dataUpdated signal to indicate that the node has been modified
    Q_EMIT dataUpdated(0);
}
