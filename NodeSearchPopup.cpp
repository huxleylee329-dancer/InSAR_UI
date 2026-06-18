#include "NodeSearchPopup.h"

#include <QFocusEvent>
#include <QShowEvent>
#include <QApplication>
#include <QScreen>
#include <QScrollBar>

#include "QtNodes/internal/BasicGraphicsScene.hpp"
#include "QtNodes/internal/UndoCommands.hpp"

NodeSearchPopup::NodeSearchPopup(const QStringList &modelNames,
                                 QtNodes::BasicGraphicsScene *scene,
                                 QPointF scenePos,
                                 QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_scene(scene)
    , m_scenePos(scenePos)
    , m_allModelNames(modelNames)
{
    // 布局：搜索框 + 列表
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setPlaceholderText(QStringLiteral("搜索节点..."));
    m_lineEdit->setClearButtonEnabled(true);
    layout->addWidget(m_lineEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    layout->addWidget(m_listWidget);

    // 初始化列表：填充全部节点名
    for (const QString &name : m_allModelNames) {
        m_listWidget->addItem(name);
    }
    // 默认选中第一行
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }

    // 信号连接
    connect(m_lineEdit, &QLineEdit::textChanged,
            this, &NodeSearchPopup::onTextChanged);
    connect(m_listWidget, &QListWidget::itemClicked,
            this, &NodeSearchPopup::onItemClicked);

    // 在 QLineEdit 和 QListWidget 上安装事件过滤器，拦截上下键和回车
    m_lineEdit->installEventFilter(this);
    m_listWidget->installEventFilter(this);

    // 固定弹窗尺寸
    setFixedSize(260, 300);

    // 自动聚焦到搜索框
    m_lineEdit->setFocus();
}

void NodeSearchPopup::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 显示后立即进行屏幕边界检查，调整位置防止被遮挡
    updatePopupGeometry();
}

void NodeSearchPopup::focusOutEvent(QFocusEvent *event)
{
    // 当焦点转移到弹窗外部时，延迟销毁
    // 使用 QueuedConnection 确保不在事件处理中途删除自身
    if (event->reason() != Qt::ActiveWindowFocusReason) {
        // 如果新获焦的控件不在本弹窗内，则销毁
        QWidget *focusWidget = QApplication::focusWidget();
        if (!focusWidget || !this->isAncestorOf(focusWidget)) {
            deleteLater();
        }
    }
    QWidget::focusOutEvent(event);
}

bool NodeSearchPopup::eventFilter(QObject *obj, QEvent *event)
{
    // 同时拦截 m_lineEdit 和 m_listWidget 的按键事件
    if ((obj == m_lineEdit || obj == m_listWidget) && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        switch (keyEvent->key()) {
        case Qt::Key_Up: {
            int row = m_listWidget->currentRow();
            if (row > 0) {
                m_listWidget->setCurrentRow(row - 1);
            }
            return true;
        }
        case Qt::Key_Down: {
            int row = m_listWidget->currentRow();
            if (row < m_listWidget->count() - 1) {
                m_listWidget->setCurrentRow(row + 1);
            }
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            createSelectedNode();
            return true;
        }
        case Qt::Key_Escape: {
            deleteLater();
            return true;
        }
        default:
            // 其他按键：如果当前焦点在 listWidget，将事件转发给 lineEdit 以便继续输入
            if (obj == m_listWidget && m_lineEdit) {
                m_lineEdit->setFocus();
                // 将按键事件重新发送给 lineEdit
                QKeyEvent newEvent(keyEvent->type(), keyEvent->key(), keyEvent->modifiers(), keyEvent->text());
                QApplication::sendEvent(m_lineEdit, &newEvent);
                return true;
            }
            break;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void NodeSearchPopup::onTextChanged(const QString &text)
{
    filterList(text);
}

void NodeSearchPopup::onItemClicked(QListWidgetItem *item)
{
    if (item) {
        createSelectedNode();
    }
}

void NodeSearchPopup::createSelectedNode()
{
    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item || !m_scene) {
        deleteLater();
        return;
    }

    QString modelName = item->text();

    // 通过撤销栈创建节点，确保 Ctrl+Z 可撤销
    m_scene->undoStack().push(
        new QtNodes::CreateCommand(m_scene, modelName, m_scenePos));

    deleteLater();
}

void NodeSearchPopup::filterList(const QString &text)
{
    const QString lowerText = text.toLower();

    m_listWidget->clear();
    for (const QString &name : m_allModelNames) {
        if (lowerText.isEmpty() || name.toLower().contains(lowerText)) {
            m_listWidget->addItem(name);
        }
    }

    // 过滤后默认选中第一行
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

void NodeSearchPopup::updatePopupGeometry()
{
    // 边界检查：确保弹窗不超出屏幕可视区域
    QScreen *screen = QApplication::screenAt(pos());
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    QRect screenRect = screen->availableGeometry();

    int x = pos().x();
    int y = pos().y();

    // 右侧溢出
    if (x + width() > screenRect.right()) {
        x = screenRect.right() - width();
    }
    // 底部溢出
    if (y + height() > screenRect.bottom()) {
        y = screenRect.bottom() - height();
    }
    // 左侧溢出
    if (x < screenRect.left()) {
        x = screenRect.left();
    }
    // 顶部溢出
    if (y < screenRect.top()) {
        y = screenRect.top();
    }

    move(x, y);
}
