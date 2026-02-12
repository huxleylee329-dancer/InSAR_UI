#pragma once
#include<QtCharts/QtCharts>
#include <QtGui> 
#include <QDebug> 
#include <QtWidgets>
#include <QVector>
#include <QPainterPath>
#include <qgraphicssceneevent.h>
#include<Preview_Window.h>
#include<opencv2/highgui.hpp>
QT_CHARTS_USE_NAMESPACE
#ifdef DEBUG
#pragma comment(lib, "Qt5Chartsd.lib")
#else
#pragma comment(lib, "Qt5Charts.lib")
#endif // DEBUG

using namespace cv;


/*预览图显示区域*/
class reselection_view :public QGraphicsView
{
    Q_OBJECT
public:
    explicit reselection_view(QWidget* parent = Q_NULLPTR);
    ~reselection_view();
    /** @brief 设置预览图
    * @param path                       图片路径
    */
    void setPixmap(const QString& path);
    /** @brief 设置目前状态（移动、缩放、裁剪）
    * @param s                          状态
    */
    void SetState(State s);
    /** @brief 设置选点标志（true为观测点，flase为参考点）
    * @param flag                       标志
    */
    void SetPointFlag(bool flag);
    /** @brief 设置形变文件路径
    * @param path                       路径
    */
    void SetH5Path(QString& path);

    State GetState();

    QGraphicsScene* mScene;             //子场景
    QGraphicsRectItem* mFirstRect;      //首选点框
    QGraphicsRectItem* mSecondRect;     //次选点框
    QList<QGraphicsRectItem*> GCPs_rect;
    QList<QPointF> GCPs_point;
    QGraphicsPixmapItem* mPixmap;       //预览图Item
    QPixmap Cursor_up, Cursor_down;
    QList<QPoint> MatPos;
    bool mFirstIsChecked, mSecondIsChecked;  //检测是否选点
signals:
    //发送参考点坐标（行列数从0开始）
    void send_coordinate(int rows, int cols, QList<QPoint>);
public slots:
    /** @brief 获取裁剪框四边比例
    */
    void GetOffset();
protected:
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void wheelEvent(QWheelEvent* event);
private:
    QString mPath;                      //形变文件路径
    QPointF sceneMousePos;//scene鼠标滑轮滚动时的中心坐标，用于鼠标中心缩放
    QPointF posAnchor;//view鼠标坐标，用于拖拽，由于拖拽过程中有抖动，故不转化为scene坐标
    bool isMousePressed;                //用于判断移动时鼠标是否按下
    State mState;                       //操作状态
    bool mPointFlag;
    QPointF mFirst, mSecond;            //记录观测点和参考点  

};

/*整体窗口*/
class reselection_view_Window : public QWidget
{
    Q_OBJECT
public:
    explicit reselection_view_Window(QWidget* parent = 0);
    ~reselection_view_Window();
    void resizeEvent(QResizeEvent* event);
    QDialogButtonBox* ButtonBox;        //确定、取消按钮
    reselection_view* View;                      //用于显示预览图的自定义QGraphicsView
signals:
    void send_coordinate(int, int, QList<QPoint>);
public slots:
    void Close();
    void receive_coordinate(int, int, QList<QPoint>);
protected:
    bool eventFilter(QObject*, QEvent*);
private:
    QVBoxLayout* MainLayout;
    QHBoxLayout* SubLayout2;
    QHBoxLayout* SubLayout3;
    QPushButton* MoveButton;
    QPushButton* ScaledButton;
    QPushButton* SelectButton;
    QPushButton* GCPsButton;
    QPushButton* GCPsDeleteButton;
    QLabel* status_label;
    QPixmap pixmap;
};