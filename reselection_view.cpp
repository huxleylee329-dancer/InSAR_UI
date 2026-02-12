#include"reselection_view.h"
#include"FormatConversion.h"
#include<icon_source.h>
#ifdef DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#endif // DEBUG


reselection_view_Window::reselection_view_Window(QWidget* parent)
    : QWidget(parent)
{
    installEventFilter(this);
    MainLayout = new QVBoxLayout();
    SubLayout2 = new QHBoxLayout();
    SubLayout3 = new QHBoxLayout();
    ButtonBox = new QDialogButtonBox(Qt::Horizontal);
    View = new reselection_view();
    setWindowIcon(QIcon(APP_ICON));
    setWindowTitle(QString::fromLocal8Bit("SBAS参考点重选"));

    MoveButton = new QPushButton();
    MoveButton->setIcon(QIcon(HAND_ICON));
    MoveButton->setFixedSize(QSize(30, 30));
    MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");


    SelectButton = new QPushButton();
    SelectButton->setIcon(QIcon(MARK_ICON));
    SelectButton->setFixedSize(QSize(30, 30));
    SelectButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");

    ScaledButton = new QPushButton();
    ScaledButton->setIcon(QIcon(ZOOM_ICON));
    ScaledButton->setFixedSize(QSize(30, 30));
    ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");

    GCPsButton = new QPushButton();
    GCPsButton->setIcon(QIcon(GCP_ICON));
    GCPsButton->setFixedSize(QSize(30, 30));
    GCPsButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");

    GCPsDeleteButton = new QPushButton();
    GCPsDeleteButton->setIcon(QIcon(DELETE_ICON));
    GCPsDeleteButton->setFixedSize(QSize(30, 30));
    GCPsDeleteButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");

    status_label = new QLabel();
    status_label->setText(QString::fromLocal8Bit("             ...未选中任何操作"));

    MoveButton->installEventFilter(this);
    ScaledButton->installEventFilter(this);
    SelectButton->installEventFilter(this);
    GCPsButton->installEventFilter(this);
    GCPsDeleteButton->installEventFilter(this);
    /*页面初始化布局*/
    ButtonBox->addButton(QString::fromLocal8Bit("确定"), QDialogButtonBox::AcceptRole);
    ButtonBox->addButton(QString::fromLocal8Bit("取消"), QDialogButtonBox::RejectRole);
    connect(ButtonBox, &QDialogButtonBox::accepted, View, &reselection_view::GetOffset);
    connect(ButtonBox, &QDialogButtonBox::rejected, this, &reselection_view_Window::Close);
    connect(View, &reselection_view::send_coordinate, this, &reselection_view_Window::receive_coordinate);

    SubLayout2->addWidget(MoveButton);
    SubLayout2->addWidget(ScaledButton);
    SubLayout2->addWidget(SelectButton);
    SubLayout2->addWidget(GCPsButton);
    SubLayout2->addWidget(GCPsDeleteButton);
    SubLayout2->addWidget(status_label);
    SubLayout2->addStretch();
    SubLayout2->setStretch(0, 1);
    SubLayout2->setStretch(1, 1);
    SubLayout2->setStretch(2, 1);
    SubLayout2->setStretch(3, 1);
    SubLayout2->setStretch(4, 1);
    SubLayout2->setStretch(5, 5);

    SubLayout3->addStretch();
    SubLayout3->addWidget(ButtonBox);
    SubLayout3->setStretch(0, 4);
    SubLayout3->setStretch(1, 1);

    MainLayout->addLayout(SubLayout2);
    MainLayout->addWidget(View);
    MainLayout->addLayout(SubLayout3);
    MainLayout->setStretch(0, 1);
    MainLayout->setStretch(1, 6);
    MainLayout->setStretch(2, 1);
    this->setLayout(MainLayout);
}
reselection_view_Window::~reselection_view_Window()
{

}

void reselection_view_Window::resizeEvent(QResizeEvent* event)
{
    update();
}



void reselection_view_Window::Close()
{
    this->close();
}

void reselection_view_Window::receive_coordinate(int ref_row, int ref_col, QList<QPoint> plist)
{
    send_coordinate(ref_row, ref_col, plist);
    this->close();
}

bool reselection_view_Window::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == MoveButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            MoveButton->setStyleSheet("QPushButton {background-color: rgb(128,128,128)}");
            ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            SelectButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            GCPsButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            GCPsDeleteButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            status_label->setText(QString::fromLocal8Bit("             ...平移"));
            View->SetState(Move_State);
            View->viewport()->setCursor(QCursor(Qt::PointingHandCursor));
        }
    }
    if (obj == ScaledButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            ScaledButton->setStyleSheet("QPushButton {background-color: rgb(128,128,128)}");
            SelectButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            GCPsButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            GCPsDeleteButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            status_label->setText(QString::fromLocal8Bit("             ...缩放"));
            View->SetState(Scaled_State);
            View->viewport()->setCursor(QCursor(View->Cursor_up));
        }
    }
    if (obj == SelectButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            SelectButton->setStyleSheet("QPushButton {background-color: rgb(128,128,128)}");
            GCPsButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            GCPsDeleteButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            status_label->setText(QString::fromLocal8Bit("             ...参考点选择"));
            View->SetState(Check_State);
            View->viewport()->setCursor(QCursor(Qt::CrossCursor));
        }
    }
    if (obj == GCPsButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            SelectButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            GCPsButton->setStyleSheet("QPushButton {background-color: rgb(128,128,128)}");
            GCPsDeleteButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            status_label->setText(QString::fromLocal8Bit("             ...添加重去平和轨道精炼控制点"));
            View->SetState(GCPs_state);
            View->viewport()->setCursor(QCursor(Qt::PointingHandCursor));
        }
    }
    if (obj == GCPsDeleteButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            SelectButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            GCPsButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            GCPsDeleteButton->setStyleSheet("QPushButton {background-color: rgb(128,128,128)}");
            status_label->setText(QString::fromLocal8Bit("             ...剔除重去平和轨道精炼控制点"));
            View->SetState(GCPs_delete);
            View->viewport()->setCursor(QCursor(Qt::ArrowCursor));
        }
    }
    return false;
}

reselection_view::reselection_view(QWidget* parent)
    :QGraphicsView(parent)
{
    GCPs_rect.clear();
    GCPs_point.clear();
    mScene = new QGraphicsScene();
    mFirstRect = new QGraphicsRectItem();
    mScene->addItem(mFirstRect);
    mSecondRect = new QGraphicsRectItem();
    mScene->addItem(mSecondRect);
    this->setInteractive(true);
    this->setDragMode(QGraphicsView::RubberBandDrag);
    this->setRubberBandSelectionMode(Qt::ContainsItemShape);
    this->setScene(mScene);
    Cursor_up.load(CURSOR_UP_ICON);
    Cursor_down.load(CURSOR_DOWN_ICON);
    isMousePressed = false;
    mState = UnChecked;
    mPointFlag = true;
    mPath.clear();
    mFirst = QPointF(0, 0);
    mSecond = QPointF(0, 0);
    mFirstIsChecked = false;
    mSecondIsChecked = false;
}

reselection_view::~reselection_view()
{
    for (int i = 0; i < GCPs_rect.size(); i++)
    {
        delete GCPs_rect[i];
    }
    delete(this->scene());
}

void reselection_view::setPixmap(const QString& path)
{
    if (path.isEmpty())
        return;
    QPixmap Img;
    if (Img.load(path))
    {
        mPixmap = new QGraphicsPixmapItem(Img);
        mScene->addItem(mPixmap);
    }
}

void reselection_view::SetState(State s)
{
    mState = s;
}

void reselection_view::SetPointFlag(bool flag)
{
    mPointFlag = flag;
}

void reselection_view::SetH5Path(QString& path)
{
    mPath = path;
}

State reselection_view::GetState()
{
    return mState;
}

void reselection_view::mousePressEvent(QMouseEvent* event)
{
    QGraphicsView::mousePressEvent(event);
    if (mState == Move_State)
    {
        viewport()->setCursor(Qt::ClosedHandCursor);
        if (this->scene() == nullptr)
        {
            qDebug() << "The scene is null";
            return;
        }
        // 记录鼠标按下时的中心点坐标
        sceneMousePos = mapToScene(event->pos()) - event->pos() + QPointF(width() / 2, height() / 2);

        // 此处不将view坐标转换成scene坐标的原因是优化性能，在move的过程中会产生抖动
        posAnchor = event->pos();
        isMousePressed = true;
    }
    else if (mState == Scaled_State)
    {

        sceneMousePos = mapToScene(event->pos());
        if (event->button() == Qt::LeftButton)
        {
            viewport()->setCursor(QCursor(Cursor_up));
            double zoomFactor = 1.2;
            scale(zoomFactor, zoomFactor);
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            this->centerOn(sceneMousePos);
            this->viewport()->update();
            sceneMousePos = mapToScene(event->pos());
            event->accept();
        }
        else if (event->button() == Qt::RightButton)
        {
            viewport()->setCursor(QCursor(Cursor_down));
            double zoomFactor = 0.8;
            scale(zoomFactor, zoomFactor);
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            this->centerOn(sceneMousePos);
            this->viewport()->update();
            sceneMousePos = mapToScene(event->pos());
            event->accept();
        }
    }
    else if (mState == Check_State)
    {
        if (event->button() == Qt::LeftButton)
        {
            //QPoint MousePos = et->pos();
            mFirst = mapToScene(event->pos());
            int pen_width = 4;  //线宽，建议宽一些，否则缩小容易看不见
            /*保证裁剪框在图像内*/

            if (mFirst.x() > mScene->width() || mFirst.x() < 0 || mFirst.y() > mScene->height() || mFirst.y() < 0)
            {
                mFirstIsChecked = false;
                mScene->removeItem(mFirstRect);
                mFirstRect->setRect(0, 0, 0, 0);
                mScene->addItem(mFirstRect);
                this->viewport()->update();     //时刻更新，否则会有残影
            }
            else
            {
                if (mFirst.x() >= mScene->width() - 5 - pen_width)
                    mFirst.setX(mScene->width() - 5 - pen_width); //减去线宽，否则会使scene迭代加长
                else if (mFirst.x() < 4)
                    mFirst.setX(4);

                if (mFirst.y() >= mScene->height() - 5 - pen_width)
                    mFirst.setY(mScene->height() - 5 - pen_width);
                else if (mFirst.y() < 4)
                    mFirst.setY(4);
                mScene->removeItem(mFirstRect);
                QPen p;
                p.setWidth(pen_width);
                p.setColor(Qt::darkBlue);
                mFirstRect->setPen(p);
                mFirstRect->setRect(mFirst.x() - 4, mFirst.y() - 4, 10, 10);
                //mRect->setFlag(QGraphicsItem::ItemIgnoresTransformations);
                mScene->addItem(mFirstRect);
                mFirstIsChecked = true;
                this->viewport()->update();     //时刻更新，否则会有残影
            }
        }
    }
    else if (mState == GCPs_state)
    {
        if (event->button() == Qt::LeftButton)
        {
            //QPoint MousePos = et->pos();
            mSecond = mapToScene(event->pos());
            int pen_width = 4;  //线宽，建议宽一些，否则缩小容易看不见
            /*保证裁剪框在图像内*/

            if (mSecond.x() > mScene->width() || mSecond.x() < 0 || mSecond.y() > mScene->height() || mSecond.y() < 0)
            {

            }
            else
            {
                if (mSecond.x() >= mScene->width() - 5 - pen_width)
                    mSecond.setX(mScene->width() - 5 - pen_width); //减去线宽，否则会使scene迭代加长
                else if (mSecond.x() < 4)
                    mSecond.setX(4);

                if (mSecond.y() >= mScene->height() - 5 - pen_width)
                    mSecond.setY(mScene->height() - 5 - pen_width);
                else if (mSecond.y() < 4)
                    mSecond.setY(4);
                QPen p;
                p.setWidth(pen_width);
                p.setColor(Qt::black);
                QGraphicsRectItem* mRect = new QGraphicsRectItem();
                mRect->setPen(p);
                mRect->setRect(mSecond.x() - 4, mSecond.y() - 4, 10, 10);
                mScene->addItem(mRect);
                this->viewport()->update();     //时刻更新，否则会有残影
                GCPs_point.push_back(mSecond);
                GCPs_rect.push_back(mRect);
            }
        }
    }
    else if (mState == GCPs_delete)
    {
        if (event->button() == Qt::LeftButton)
        {
            if (GCPs_point.size() > 0 && GCPs_rect.size() > 0)
            {
                GCPs_point.removeLast();
                mScene->removeItem(GCPs_rect.last());
                delete GCPs_rect.last();
                GCPs_rect.removeLast();
                this->viewport()->update();
            }
        }
    }
}

void reselection_view::mouseMoveEvent(QMouseEvent* event)
{
    //QGraphicsView::mouseMoveEvent(event);
    if (mState == Move_State)
    {
        QPointF offsetPos = event->pos() - posAnchor;
        if (isMousePressed) {
            //setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            viewport()->setCursor(Qt::ClosedHandCursor);
            centerOn(sceneMousePos - offsetPos);    //一定要保证scene比view大，不然函数无效
        }
        else
        {
            viewport()->setCursor(Qt::OpenHandCursor);
        }
    }
    else if (mState == Scaled_State)
    {
        viewport()->setCursor(QCursor(Cursor_up));
    }
}

void reselection_view::mouseReleaseEvent(QMouseEvent* event)
{
    QGraphicsView::mouseReleaseEvent(event);
    if (mState == Move_State)
    {
        viewport()->setCursor(Qt::OpenHandCursor);
        isMousePressed = false;
    }
    else if (mState == Scaled_State)
    {
        if (event->button() == Qt::LeftButton)
        {
            viewport()->setCursor(QCursor(Cursor_up));
        }
        else
        {
            viewport()->setCursor(QCursor(Cursor_down));
        }

    }
}

void reselection_view::wheelEvent(QWheelEvent* event)
{
    //QGraphicsView::wheelEvent(event);
    if (mState == Scaled_State)
    {
        if (event->orientation() == Qt::Vertical)
        {
            double angleDeltaY = event->angleDelta().y();
            double zoomFactor = qPow(1.0015, angleDeltaY);
            scale(zoomFactor, zoomFactor);
            if (angleDeltaY > 0)
            {
                viewport()->setCursor(QCursor(Cursor_up));
                setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
                this->centerOn(sceneMousePos);
                this->viewport()->update();
                sceneMousePos = mapToScene(event->pos());
            }
            else
                viewport()->setCursor(QCursor(Cursor_down));
            event->accept();
        }
        else
            event->ignore();
    }
}



void reselection_view::GetOffset()
{

    if (mPath == NULL)
    {
        QMessageBox::warning(NULL, QString::fromLocal8Bit("警告"), QString::fromLocal8Bit("无形变文件"));
        return;
    }
    FormatConversion FC;
    Mat Mask;
    FC.read_array_from_h5(mPath.toStdString().c_str(), "mask", Mask);
    int rows = Mask.rows;
    int cols = Mask.cols;
    int center_row = mFirst.y() / mScene->height() * rows;
    int center_col = mFirst.x() / mScene->width() * cols;
    center_row = center_row > rows - 2 ? rows - 2 : center_row;
    center_col = center_col > cols - 2 ? cols - 2 : center_col;
    center_row = center_row < 1 ? 1 : center_row;
    center_col = center_col < 1 ? 1 : center_col;
    int ref_rows = 0, ref_cols = 0;
    bool b_break = false;
    for (int i = center_row - 1; i < center_row + 1; i++)
    {
        for (int j = center_col - 1; j < center_col + 1; j++)
        {
            if (Mask.at<int>(i, j))
            {
                ref_rows = i;
                ref_cols = j;
                b_break = true;
                break;
            }
        }
        if (b_break) break;
    }
    if (!b_break)
    {
        QMessageBox::warning(NULL, QString::fromLocal8Bit("警告"), QString::fromLocal8Bit("未选中参考点，请重试！"));
        return;
    }
    QList<QPoint> plist;
    for (int i = 0; i < GCPs_point.size(); i++)
    {
        QPoint p;
        int center_row = GCPs_point[i].y() / mScene->height() * rows;
        int center_col = GCPs_point[i].x() / mScene->width() * cols;
        center_row = center_row > rows - 2 ? rows - 2 : center_row;
        center_col = center_col > cols - 2 ? cols - 2 : center_col;
        center_row = center_row < 1 ? 1 : center_row;
        center_col = center_col < 1 ? 1 : center_col;
        int ref_rows = 0, ref_cols = 0;
        bool b_break = false;
        for (int i = center_row - 1; i < center_row + 1; i++)
        {
            for (int j = center_col - 1; j < center_col + 1; j++)
            {
                if (Mask.at<int>(i, j))
                {
                    ref_rows = i;
                    ref_cols = j;
                    b_break = true;
                    break;
                }
            }
            if (b_break) break;
        }
        if (b_break)
        {
            p.setX(ref_rows);
            p.setY(ref_cols);
            plist.push_back(p);
        }
    }
    emit send_coordinate(ref_rows, ref_cols, plist);
    this->close();
}
