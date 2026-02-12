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
#include<FormatConversion.h>
QT_CHARTS_USE_NAMESPACE
#ifdef DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "Qt5Chartsd.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "Qt5Charts.lib")
#endif // DEBUG

using namespace cv;

/*用于判断操作状态的枚举量*/

enum Method
{
    AbsoluteResult,
    RelativeResult
};

/*预览图显示区域*/
class Deformation_Preview :public QGraphicsView
{
    Q_OBJECT
public:
    explicit Deformation_Preview(QWidget* parent = Q_NULLPTR);
    ~Deformation_Preview();
    /** @brief 设置预览图
    * @param path                       图片路径
    */
    void setPixmap(const QString& path);
    /** @brief 设置平均方法
    * @param m                          方法
    */
    void SetMethod(Method m);
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

    Method GetMethod();
    QGraphicsScene* mScene;             //子场景
    QGraphicsRectItem* mFirstRect;      //首选点框
    QGraphicsRectItem* mSecondRect;     //次选点框
    QGraphicsPixmapItem* mPixmap;       //预览图Item
    QPixmap Cursor_up, Cursor_down;
    QList<QPoint> MatPos;
    bool mFirstIsChecked, mSecondIsChecked;  //检测是否选点
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
    Method mMethod;                     //平均方法
    State mState;                       //操作状态
    bool mPointFlag;
    QPointF mFirst, mSecond;            //记录观测点和参考点  
    
};

/*整体窗口*/
class Deformation_Preview_Window : public QWidget
{
    Q_OBJECT
public:
    explicit Deformation_Preview_Window(QWidget* parent = 0);
    ~Deformation_Preview_Window();
    void resizeEvent(QResizeEvent* event);
    QDialogButtonBox* ButtonBox;        //确定、取消按钮
    Deformation_Preview* View;                      //用于显示预览图的自定义QGraphicsView
public slots:
    void Close();
protected:
    bool eventFilter(QObject*, QEvent*);
private:
    QVBoxLayout* MainLayout;
    QHBoxLayout* SubLayout1;
    QHBoxLayout* SubLayout2;
    QHBoxLayout* SubLayout3;
    QHBoxLayout* InsertLayout;
    QButtonGroup* Methods;
    QRadioButton* AbsoluteButton;
    QRadioButton* RelativeButton;
    QButtonGroup* ChoosePoint;
    QRadioButton* FirstPointButton;
    QRadioButton* SecondPointButton;
    QPushButton* MoveButton;
    QPushButton* ScaledButton;
    QPushButton* CutButton;
    QPixmap pixmap;
};

class Deformation_Chart : public QWidget
{
    Q_OBJECT
public:
    Deformation_Chart(QWidget* parent = Q_NULLPTR);
    Deformation_Chart(QString Title);
    ~Deformation_Chart();
    void resizeEvent(QResizeEvent* event);
    void SetData(Mat Times_series, Mat Deformation);
    void SetMinMax(double Min, double Max);
    void Paint(QList<double> temporal_baseline, QList<double> spatial_baseline);
protected:

private:
    double MinDefomation;
    double MaxDeformation;
    QChart* mChart;
    QChartView* mChartView;
    QLabel* mLabel;
    QValueAxis* mAxisX;
    QValueAxis* mAxisY;
private slots:
    void ShowData(const QPointF& point, bool state);
};