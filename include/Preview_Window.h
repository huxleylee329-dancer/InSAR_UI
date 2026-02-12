#pragma once
#include <QtGui> 
#include <QDebug> 
#include <QtWidgets>
#include <QVector>
#include <QPainterPath>
#include <qgraphicssceneevent.h>
class QPixmap;
class QPaintEvent;
/*用于判断操作状态的枚举量*/
enum State
{
    UnChecked,
    Move_State,
    Scaled_State,
    Cut_State,
    Check_State,
    GCPs_state,
    GCPs_delete
};


/*预览图显示区域*/
class Preview :public QGraphicsView
{
    Q_OBJECT
public:
    explicit Preview(QWidget* parent = Q_NULLPTR);
    ~Preview();
    /** @brief 设置预览图
    * @param path                       图片路径
    */
    void setPixmap(const QString& path);
    /** @brief 设置目前状态（移动、缩放、裁剪）
    * @param s                          状态
    */
    void SetState(State s);

    QGraphicsScene* mScene;             //子场景
    QGraphicsRectItem* mRect;           //裁剪框
    QGraphicsPixmapItem* mPixmap;       //预览图Item
    QPixmap Cursor_up, Cursor_down;
public slots:
    /** @brief 获取裁剪框四边比例
    */
    void GetOffset();
signals:
    void SendPos(double left, double right, double top, double bottom);
    void Parent_Close();
protected:
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void wheelEvent(QWheelEvent* event);
private:

    QPointF sceneMousePos;//scene鼠标滑轮滚动时的中心坐标，用于鼠标中心缩放
    QPointF posAnchor;//view鼠标坐标，用于拖拽，由于拖拽过程中有抖动，故不转化为scene坐标
    bool isMousePressed;                //用于判断移动时鼠标是否按下
    State mState;                       //操作状态
    QPointF mStart, mEnd;               //记录起始点与终止点                
};

/*整体窗口*/
class Preview_Window : public QWidget
{
    Q_OBJECT
public:
    explicit Preview_Window(QWidget* parent = 0);
    ~Preview_Window();
    void resizeEvent(QResizeEvent* event);
    QDialogButtonBox* ButtonBox;        //确定、取消按钮
    Preview* View;                      //用于显示预览图的自定义QGraphicsView
public slots:
    void Close();
protected:
    bool eventFilter(QObject*, QEvent*);
private:
    QVBoxLayout* MainLayout;
    QHBoxLayout* SubLayout1;
    QHBoxLayout* SubLayout2;
    QPushButton* MoveButton;
    QPushButton* ScaledButton;
    QPushButton* CutButton;
    QPixmap pixmap;
private slots:
};

