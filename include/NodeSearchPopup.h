#ifndef NODESEARCHPOPUP_H
#define NODESEARCHPOPUP_H

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QStringList>

namespace QtNodes {
class BasicGraphicsScene;
}

/**
 * @brief 轻量级悬浮节点搜索弹窗
 *
 * 无边框弹出窗口，支持模糊搜索和键盘盲操。
 * 失去焦点或选择节点后自动销毁。
 */
class NodeSearchPopup : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief 构造搜索弹窗
     * @param registry 已注册的节点模型注册表
     * @param scene    当前画布场景（用于创建节点）
     * @param scenePos 节点创建的场景坐标
     * @param parent   父窗口
     */
    explicit NodeSearchPopup(const QStringList &modelNames,
                             QtNodes::BasicGraphicsScene *scene,
                             QPointF scenePos,
                             QWidget *parent = nullptr);

protected:
    // 失去焦点时自动关闭销毁
    void focusOutEvent(QFocusEvent *event) override;
    // 显示时自动进行屏幕边界检查
    void showEvent(QShowEvent *event) override;
    // 按键事件拦截（Esc关闭，上下键导航，回车创建）
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onTextChanged(const QString &text);
    void onItemClicked(QListWidgetItem *item);

private:
    void createSelectedNode();
    void filterList(const QString &text);
    void updatePopupGeometry();

    QLineEdit *m_lineEdit;
    QListWidget *m_listWidget;
    QtNodes::BasicGraphicsScene *m_scene;
    QPointF m_scenePos;
    QStringList m_allModelNames;
};

#endif // NODESEARCHPOPUP_H
