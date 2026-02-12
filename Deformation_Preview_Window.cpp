#include<Deformation_Preview_Window.h>

#include<icon_source.h>


Deformation_Preview_Window::Deformation_Preview_Window(QWidget* parent)
    : QWidget(parent)
{
    installEventFilter(this);
    MainLayout = new QVBoxLayout();
    SubLayout1 = new QHBoxLayout();
    SubLayout2 = new QHBoxLayout();
    SubLayout3 = new QHBoxLayout();
    InsertLayout = new QHBoxLayout();
    ButtonBox = new QDialogButtonBox(Qt::Horizontal);
    View = new Deformation_Preview();
    setWindowIcon(QIcon(APP_ICON));
    setWindowTitle(QString::fromLocal8Bit("SBAS形变时间序列查看"));
    Methods = new QButtonGroup;
    AbsoluteButton = new QRadioButton(QString::fromLocal8Bit("绝对形变序列"));
    AbsoluteButton->setMinimumHeight(50);
    AbsoluteButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
    AbsoluteButton->setChecked(true);

    RelativeButton = new QRadioButton(QString::fromLocal8Bit("相对形变序列"));
    RelativeButton->setMinimumHeight(50);
    RelativeButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");

    AbsoluteButton->installEventFilter(this);
    RelativeButton->installEventFilter(this);

    Methods->addButton(AbsoluteButton);
    Methods->addButton(RelativeButton);

    ChoosePoint = new QButtonGroup;
    FirstPointButton = new QRadioButton(QString::fromLocal8Bit("选择观测点"));
    FirstPointButton->setMinimumHeight(50);
    FirstPointButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
    FirstPointButton->setChecked(true);
    FirstPointButton->hide();

    SecondPointButton = new QRadioButton(QString::fromLocal8Bit("选择参考点"));
    SecondPointButton->setMinimumHeight(50);
    SecondPointButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
    SecondPointButton->hide();

    FirstPointButton->installEventFilter(this);
    SecondPointButton->installEventFilter(this);

    ChoosePoint->addButton(FirstPointButton);
    ChoosePoint->addButton(SecondPointButton);

    MoveButton = new QPushButton();
    MoveButton->setIcon(QIcon(HAND_ICON));
    MoveButton->setFixedSize(QSize(30, 30));
    MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");


    CutButton = new QPushButton();
    CutButton->setIcon(QIcon(MARK_ICON));
    CutButton->setFixedSize(QSize(30, 30));
    CutButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");

    ScaledButton = new QPushButton();
    ScaledButton->setIcon(QIcon(ZOOM_ICON));
    ScaledButton->setFixedSize(QSize(30, 30));
    ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");

    MoveButton->installEventFilter(this);
    ScaledButton->installEventFilter(this);
    CutButton->installEventFilter(this);
    /*页面初始化布局*/
    ButtonBox->addButton(QString::fromLocal8Bit("确定"), QDialogButtonBox::AcceptRole);
    ButtonBox->addButton(QString::fromLocal8Bit("取消"), QDialogButtonBox::RejectRole);
    connect(ButtonBox, &QDialogButtonBox::accepted, View, &Deformation_Preview::GetOffset);
    connect(ButtonBox, &QDialogButtonBox::rejected, this, &Deformation_Preview_Window::Close);

    SubLayout1->addWidget(AbsoluteButton);
    SubLayout1->addWidget(RelativeButton);
    SubLayout1->addStretch();
    SubLayout1->setStretch(0, 1);
    SubLayout1->setStretch(1, 1);
    SubLayout1->setStretch(2, 8);

    SubLayout2->addWidget(MoveButton);
    SubLayout2->addWidget(ScaledButton);
    SubLayout2->addWidget(CutButton);
    SubLayout2->addStretch();
    SubLayout2->setStretch(0, 1);
    SubLayout2->setStretch(1, 1);
    SubLayout2->setStretch(2, 1);
    SubLayout2->setStretch(3, 7);

    InsertLayout->addWidget(FirstPointButton);
    InsertLayout->addWidget(SecondPointButton);
    InsertLayout->addStretch();
    InsertLayout->setStretch(0, 1);
    InsertLayout->setStretch(1, 1);
    InsertLayout->setStretch(2, 8);

    SubLayout3->addStretch();
    SubLayout3->addWidget(ButtonBox);
    SubLayout3->setStretch(0, 4);
    SubLayout3->setStretch(1, 1);

    MainLayout->addLayout(SubLayout1);
    MainLayout->addLayout(SubLayout2);
    MainLayout->addLayout(InsertLayout);
    MainLayout->addWidget(View);
    MainLayout->addLayout(SubLayout3);
    MainLayout->setStretch(0, 1);
    MainLayout->setStretch(0, 1);
    MainLayout->setStretch(0, 1);
    MainLayout->setStretch(0, 6);
    MainLayout->setStretch(1, 1);
    //View->setMinimumWidth(ButtonBox->width());
    //View->setMinimumHeight(ButtonBox->width());
    //View->setCursor(Qt::CrossCursor);
    this->setLayout(MainLayout);
    //connect(MoveButton, &QPushButton::pressed, View, &Deformation_Preview::GetOffset);
}
Deformation_Preview_Window::~Deformation_Preview_Window()
{

}

void Deformation_Preview_Window::resizeEvent(QResizeEvent* event)
{
    update();
}



void Deformation_Preview_Window::Close()
{
    this->close();
}

bool Deformation_Preview_Window::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == MoveButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            MoveButton->setStyleSheet("QPushButton {background-color: rgb(128,128,128)}");
            ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            CutButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
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
            CutButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            View->SetState(Scaled_State);
            View->viewport()->setCursor(QCursor(View->Cursor_up));
        }
    }
    if (obj == CutButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            if (View->GetMethod() == AbsoluteResult)
            {
                FirstPointButton->hide();
                SecondPointButton->hide();
            }
            else if (View->GetMethod() == RelativeResult)
            {
                FirstPointButton->show();
                SecondPointButton->show();
            }
            MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            CutButton->setStyleSheet("QPushButton {background-color: rgb(128,128,128)}");
            View->SetState(Check_State);
            View->viewport()->setCursor(QCursor(Qt::CrossCursor));
        }
    }
    if (obj == AbsoluteButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            View->SetMethod(AbsoluteResult);
            if (View->GetState() == Check_State)
            {
                FirstPointButton->hide();
                SecondPointButton->hide();
            }
            View->mSecondIsChecked = false;
            View->mScene->removeItem(View->mSecondRect);
            View ->mSecondRect->setRect(0, 0, 0, 0);
            View->mScene->addItem(View->mSecondRect);
            View->viewport()->update();
        }
    }
    if (obj == RelativeButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
        {
            View->SetMethod(RelativeResult);
            if (View->GetState() == Check_State)
            {
                FirstPointButton->show();
                SecondPointButton->show();
            }
        }
    }
    if (obj == FirstPointButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
            View->SetPointFlag(true);
    }
    if (obj == SecondPointButton)
    {
        if (e->type() == QEvent::MouseButtonPress)
            View->SetPointFlag(false);
    }

    return false;
}

Deformation_Preview::Deformation_Preview(QWidget* parent)
    :QGraphicsView(parent)
{
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
    mMethod = AbsoluteResult;
    mState = UnChecked;
    mPointFlag = true;
    mPath.clear();
    mFirst = QPointF(0, 0);
    mSecond = QPointF(0, 0);
    mFirstIsChecked = false;
    mSecondIsChecked = false;
}

Deformation_Preview::~Deformation_Preview()
{
    delete(this->scene());
}

void Deformation_Preview::setPixmap(const QString& path)
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

void Deformation_Preview::SetMethod(Method m)
{
    mMethod = m;
}

void Deformation_Preview::SetState(State s)
{
    mState = s;
}

void Deformation_Preview::SetPointFlag(bool flag)
{
    mPointFlag = flag;
}

void Deformation_Preview::SetH5Path(QString& path)
{
    mPath = path;
}

State Deformation_Preview::GetState()
{
    return mState;
}

Method Deformation_Preview::GetMethod()
{
    return mMethod;
}

void Deformation_Preview::mousePressEvent(QMouseEvent* event)
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
        if (mMethod == AbsoluteResult)
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
                    p.setColor(Qt::black);
                    mFirstRect->setPen(p);
                    mFirstRect->setRect(mFirst.x() - 4, mFirst.y() - 4, 10, 10);
                    //mRect->setFlag(QGraphicsItem::ItemIgnoresTransformations);
                    mScene->addItem(mFirstRect);
                    mFirstIsChecked = true;
                    this->viewport()->update();     //时刻更新，否则会有残影
                }

                
            }
        }
        else if (mMethod == RelativeResult)
        {
            if (event->button() == Qt::LeftButton)
            {
                //QPoint MousePos = et->pos();
                if (mPointFlag)
                {
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
                        p.setColor(Qt::black);
                        mFirstRect->setPen(p);
                        mFirstRect->setRect(mFirst.x() - 4, mFirst.y() - 4, 10, 10);
                        //mRect->setFlag(QGraphicsItem::ItemIgnoresTransformations);
                        mScene->addItem(mFirstRect);
                        mFirstIsChecked = true;
                        this->viewport()->update();     //时刻更新，否则会有残影
                    }
                    
                }
                else
                {
                    mSecond = mapToScene(event->pos());
                    int pen_width = 4;  //线宽，建议宽一些，否则缩小容易看不见
                    /*保证裁剪框在图像内*/
                    if (mSecond.x() > mScene->width() || mSecond.x() < 0 || mSecond.y() > mScene->height() || mSecond.y() < 0)
                    {
                        mSecondIsChecked = false;
                        mScene->removeItem(mSecondRect);
                        mSecondRect->setRect(0, 0, 0, 0);
                        mScene->addItem(mSecondRect);
                        this->viewport()->update();     //时刻更新，否则会有残影
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
                        mScene->removeItem(mSecondRect);
                        QPen p;
                        p.setWidth(pen_width);
                        p.setColor(QColor(128, 0, 128));
                        mSecondRect->setPen(p);
                        mSecondRect->setRect(mSecond.x() - 4, mSecond.y() - 4, 10, 10);
                        //mRect->setFlag(QGraphicsItem::ItemIgnoresTransformations);
                        mScene->addItem(mSecondRect);
                        mSecondIsChecked = true;
                        this->viewport()->update();     //时刻更新，否则会有残影
                    }
                    
                }
               
            }
            else if (event->button() == Qt::RightButton)
            {
                
            }
        }
    }
}

void Deformation_Preview::mouseMoveEvent(QMouseEvent* event)
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

void Deformation_Preview::mouseReleaseEvent(QMouseEvent* event)
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

void Deformation_Preview::wheelEvent(QWheelEvent* event)
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



void Deformation_Preview::GetOffset()
{

    if (mPath == NULL)
    {
        QMessageBox::warning(NULL, QString::fromLocal8Bit("警告"), QString::fromLocal8Bit("无形变文件"));
        return;
    }
    if (mMethod == AbsoluteResult)
    {
        if (!mFirstIsChecked)
        {
            QMessageBox::warning(NULL, QString::fromLocal8Bit("警告"), QString::fromLocal8Bit("请选择想要估计的点"));
            return;
        }
        else
        {
            FormatConversion FC;
            Mat Mask, Mask_Index, Times_Series;
            double Min_def, Max_def;
            FC.read_double_from_h5(mPath.toStdString().c_str(), "max_deformation", &Max_def);
            FC.read_double_from_h5(mPath.toStdString().c_str(), "min_deformation", &Min_def);
            FC.read_array_from_h5(mPath.toStdString().c_str(), "mask", Mask);
            FC.read_array_from_h5(mPath.toStdString().c_str(), "mask_count_map", Mask_Index);
            FC.read_array_from_h5(mPath.toStdString().c_str(), "temporal_baseline", Times_Series);
            int rows = Mask.rows;
            int cols = Mask.cols;
            int times_num = Times_Series.cols;
            int center_row = mFirst.y() / mScene->height() * rows;
            int center_col = mFirst.x() / mScene->width() * cols;
            if (center_row < 4)
                center_row = 4;
            if (center_col < 4)
                center_col = 4;
            Mat Result = Mat::zeros(1, times_num, CV_64FC1);
            int count = 0;
//# pragma omp parallel for schedule(guided)
            for (int i = center_row-4; i < center_row+5; i++)
                for (int j = center_col-4; j < center_col+5; j++)
                {
                    if (Mask.at<int>(i, j))
                    {
                        count++;
                        int index = Mask_Index.at<int>(i, j);
                        Mat V;
                        FC.read_subarray_from_h5(mPath.toStdString().c_str(), "deformation_time_series", index, 0, 1, times_num, V);
                        for (int k = 0; k < times_num; k++)
                        {
                            Result.at<double>(k) += V.at<double>(k);
                        }

                    }
                }
            Result /= count;
            //Result = -Result;
            Deformation_Chart* map = new Deformation_Chart(QString::fromLocal8Bit("绝对形变时间序列"));
            map->SetMinMax(Min_def, Max_def);
            map->SetData(Times_Series, Result);
            map->show();
            map->setAttribute(Qt::WA_DeleteOnClose, true);
        }
    }
    else if (mMethod == RelativeResult)
    {
        if (mFirstIsChecked == false || mSecondIsChecked == false)
        {
            QMessageBox::warning(NULL, QString::fromLocal8Bit("警告"), QString::fromLocal8Bit("请选择参考点与观测点"));
            return;
        }
        else
        {
            FormatConversion FC;
            Mat Mask, Mask_Index, Times_Series;
            double Min_def, Max_def;
            FC.read_double_from_h5(mPath.toStdString().c_str(), "max_deformation", &Max_def);
            FC.read_double_from_h5(mPath.toStdString().c_str(), "min_deformation", &Min_def);
            FC.read_array_from_h5(mPath.toStdString().c_str(), "mask", Mask);
            FC.read_array_from_h5(mPath.toStdString().c_str(), "mask_count_map", Mask_Index);
            FC.read_array_from_h5(mPath.toStdString().c_str(), "temporal_baseline", Times_Series);
            int rows = Mask.rows;
            int cols = Mask.cols;
            int times_num = Times_Series.cols;
            int center_row = mFirst.y() / mScene->height() * rows;
            int center_col = mFirst.x() / mScene->width() * cols;
            int center_row2 = mSecond.y() / mScene->height() * rows;
            int center_col2 = mSecond.x() / mScene->width() * cols;
            if (center_row < 4)
                center_row = 4;
            if (center_col < 4)
                center_col = 4;
            if (center_row2 < 4)
                center_row2 = 4;
            if (center_col2 < 4)
                center_col2 = 4;
            Mat Result = Mat::zeros(1, times_num, CV_64FC1);
            Mat Result2 = Mat::zeros(1, times_num, CV_64FC1);
            int count = 0;
            int count2 = 0;
//# pragma omp parallel for schedule(guided)
            for (int i = center_row - 4; i < center_row + 5; i++)
                for (int j = center_col - 4; j < center_col + 5; j++)
                {
                    if (Mask.at<int>(i, j))
                    {
                        count++;
                        int index = Mask_Index.at<int>(i, j);
                        Mat V;
                        FC.read_subarray_from_h5(mPath.toStdString().c_str(), "deformation_time_series", index, 0, 1, times_num, V);
                        for (int k = 0; k < times_num; k++)
                        {
                            Result.at<double>(k) += V.at<double>(k);
                        }

                    }
                }
//# pragma omp parallel for schedule(guided)
            for (int i = center_row2 - 4; i < center_row2+ 5; i++)
                for (int j = center_col2 - 4; j < center_col2 + 5; j++)
                {
                    if (Mask.at<int>(i, j))
                    {
                        count2++;
                        int index2 = Mask_Index.at<int>(i, j);
                        Mat V2;
                        FC.read_subarray_from_h5(mPath.toStdString().c_str(), "deformation_time_series", index2, 0, 1, times_num, V2);
                        for (int k = 0; k < times_num; k++)
                        {
                            Result2.at<double>(k) += V2.at<double>(k);
                        }

                    }
                }
            Result /= count;
            Result2 /= count2;
            Result = Result - Result2;
            Deformation_Chart* map = new Deformation_Chart(QString::fromLocal8Bit("相对形变时间序列"));
            map->SetMinMax(Min_def, Max_def);
            map->SetData(Times_Series, Result);
            map->show();
            map->setAttribute(Qt::WA_DeleteOnClose, true);
        }
        
    }
   
}


Deformation_Chart::Deformation_Chart(QWidget* parent) :
    QWidget(parent)
{
    this->resize(QSize(400, 400));
    this->setWindowTitle(QString::fromLocal8Bit(""));
    mChart = new QChart();
    mChart->setTitle(QString::fromLocal8Bit("时空基线"));
    mChartView = new QChartView(mChart, this);
    mChart = mChartView->chart();   //关联
    mAxisX = new QValueAxis();
    mAxisY = new QValueAxis();
    mChart->setAxisX(mAxisX);
    mChart->setAxisY(mAxisY);
    mChartView->setRenderHint(QPainter::Antialiasing); //抗锯齿
    MinDefomation = -0.5;
    MaxDeformation = 0.5;
    mLabel = new QLabel(mChartView);
}
Deformation_Chart::Deformation_Chart(QString Title)
{
    this->resize(QSize(400, 400));
    this->setWindowTitle(QString::fromLocal8Bit("形变预览"));
    this->setWindowIcon(QIcon(APP_ICON));
    mChart = new QChart();
    mChart->legend()->hide();
    mChart->setTitle(Title);
    mChartView = new QChartView(mChart, this);
    mChart = mChartView->chart();   //关联
    mAxisX = new QValueAxis();
    mAxisY = new QValueAxis();
    mChart->setAxisX(mAxisX);
    mChart->setAxisY(mAxisY);
    mChartView->setRenderHint(QPainter::Antialiasing); //抗锯齿
    MinDefomation = -0.5;
    MaxDeformation = 0.5;
    mLabel = new QLabel(mChartView);
}

Deformation_Chart::~Deformation_Chart()
{
   
}

void Deformation_Chart::resizeEvent(QResizeEvent* event)
{
    /*窗口变化时重载图片并更改尺寸*/
    mChartView->resize(this->size());
}

void Deformation_Chart::SetData(Mat Times_series, Mat Deformation)
{
    QList<double> temporal_baseline;
    QList<double> spatial_baseline;
    for (int i = 0; i < Times_series.cols; i++)
    {
        temporal_baseline.append(Times_series.at<double>(i));
        spatial_baseline.append(Deformation.at<double>(i));
    }
    Paint(temporal_baseline, spatial_baseline);
}

void Deformation_Chart::SetMinMax(double Min, double Max)
{
    this->MinDefomation = Min - 0.1;
    this->MaxDeformation = Max + 0.1;
}

void Deformation_Chart::Paint(QList<double> temporal_baseline, QList<double> spatial_baseline)
{
    double Min_time = 0, Max_time = 0, Min_space = 0, Max_space = 0;
    for (int i = 0; i < temporal_baseline.size(); i++)
    {
        if (Min_time > temporal_baseline.at(i))
            Min_time = temporal_baseline.at(i);
        if (Max_time < temporal_baseline.at(i))
            Max_time = temporal_baseline.at(i);
        if (Min_space > spatial_baseline.at(i))
            Min_space = spatial_baseline.at(i);
        if (Max_space < spatial_baseline.at(i))
            Max_space = spatial_baseline.at(i);

    }
    double pad_time = (Max_time - Min_time) / 10;
    double pad_space = (Max_space - Min_space) / 10;
    mAxisX->setRange(Min_time - pad_time, Max_time + pad_time);
    mAxisX->setLabelFormat("%.2f");
    mAxisY->setRange(MinDefomation, MaxDeformation);
    mAxisY->setLabelFormat("%.2f");
    mAxisX->setTitleText(QString::fromLocal8Bit("时间基线（天）"));
    mAxisY->setTitleText(QString::fromLocal8Bit("形变（米）"));
    /*for (int i = 0; i < temporal_baseline.size(); i++)
    {
        QLineSeries* mLine = new QLineSeries();
        mLine->append(QPointF(temporal_baseline.at(i), spatial_baseline.at(i)));
        mChart->addSeries(mLine);
        mChart->setAxisX(mAxisX, mLine);
        mChart->setAxisY(mAxisY, mLine);
    }*/
    for (int i = 0; i < temporal_baseline.size(); i++)
    {
        QScatterSeries* mPoint_edge = new QScatterSeries();
        QScatterSeries* mPoint_central = new QScatterSeries();
        mPoint_edge->setBorderColor(QColor(0, 0, 0));
        mPoint_edge->setBrush(QColor(0, 0, 0));
        mPoint_edge->setMarkerSize(15);
        mPoint_central->setMarkerSize(12);
        mPoint_edge->setMarkerShape(QScatterSeries::MarkerShapeCircle);
        mPoint_central->setMarkerShape(QScatterSeries::MarkerShapeCircle);
        mPoint_central->setBorderColor(Qt::white);
        mPoint_central->setBrush(Qt::white);
        mPoint_edge->append(QPointF(temporal_baseline.at(i), spatial_baseline.at(i)));
        mPoint_central->append(QPointF(temporal_baseline.at(i), spatial_baseline.at(i)));
        mChart->addSeries(mPoint_edge);
        mChart->addSeries(mPoint_central);
        mChart->setAxisX(mAxisX, mPoint_edge);
        mChart->setAxisY(mAxisY, mPoint_edge);
        mChart->setAxisX(mAxisX, mPoint_central);
        mChart->setAxisY(mAxisY, mPoint_central);
        connect(mPoint_central, &QScatterSeries::hovered, this, &Deformation_Chart::ShowData);
    }


}

void Deformation_Chart::ShowData(const QPointF& point, bool state)
{
    if (state)
    {
        mLabel->setText(QString("(%1,%2)").arg(QString::number(point.x(), 'f', 1)).arg(QString::number(point.y(), 'f', 3)));
        mLabel->setStyleSheet("QLabel { background-color : rgb(129, 199, 212); color : rgb(0, 92, 175); border-radius:3px;font:20pt }");
        QPoint curPos = mapFromGlobal(QCursor::pos());
        mLabel->move(curPos.x() - mLabel->width() / 2, curPos.y() - mLabel->height() * 1.5);//移动数值 m_valueLabel->show();//显示出来
        mLabel->show();
    }
    else
        mLabel->hide();
}