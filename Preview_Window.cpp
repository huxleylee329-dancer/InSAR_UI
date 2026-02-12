#include<Preview_Window.h>
#include<icon_source.h>
Preview_Window::Preview_Window(QWidget* parent)
    : QWidget(parent)
{
    installEventFilter(this);
    MainLayout = new QVBoxLayout();
    SubLayout1 = new QHBoxLayout();
    SubLayout2 = new QHBoxLayout();
    ButtonBox = new QDialogButtonBox(Qt::Horizontal);
    View = new Preview();
    MoveButton = new QPushButton();
    MoveButton->setIcon(QIcon(HAND_ICON));
    MoveButton->setFixedSize(QSize(30, 30));
    MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
    
    

    CutButton = new QPushButton();
    CutButton->setIcon(QIcon(CUT_ICON));
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
    connect(ButtonBox, &QDialogButtonBox::accepted, View, &Preview::GetOffset);
    connect(ButtonBox, &QDialogButtonBox::rejected, this, &Preview_Window::Close);
    connect(View, &Preview::Parent_Close, this, &Preview_Window::Close);
    
    SubLayout1->addWidget(MoveButton);
    SubLayout1->addWidget(ScaledButton);
    SubLayout1->addWidget(CutButton);
    SubLayout1->addStretch();
    SubLayout1->setStretch(0, 1);
    SubLayout1->setStretch(1, 1);
    SubLayout1->setStretch(2, 1);
    SubLayout1->setStretch(3, 7);
    
    SubLayout2->addStretch();
    SubLayout2->addWidget(ButtonBox);
    SubLayout2->setStretch(0, 4);
    SubLayout2->setStretch(1, 1);

    MainLayout->addLayout(SubLayout1);
    MainLayout->addWidget(View);
    MainLayout->addLayout(SubLayout2);
    MainLayout->setStretch(0, 1);
    MainLayout->setStretch(0, 8);
    MainLayout->setStretch(1, 1);
    //View->setMinimumWidth(ButtonBox->width());
    //View->setMinimumHeight(ButtonBox->width());
    //View->setCursor(Qt::CrossCursor);
    this->setLayout(MainLayout);
    //connect(MoveButton, &QPushButton::pressed, View, &Preview::GetOffset);
}
Preview_Window::~Preview_Window()
{

}

void Preview_Window::resizeEvent(QResizeEvent* event)
{
        update();
}



void Preview_Window::Close()
{
    this->close();
}

bool Preview_Window::eventFilter(QObject* obj, QEvent* e)
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
            MoveButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            ScaledButton->setStyleSheet("QPushButton {background-color: rgb(240,240,240)}");
            CutButton->setStyleSheet("QPushButton {background-color: rgb(128,128,128)}");
            View->SetState(Cut_State);
            View->viewport()->setCursor(QCursor(Qt::CrossCursor));
        }
    }
    
    return false;
}

Preview::Preview(QWidget* parent)
    :QGraphicsView(parent)
{
    mScene = new QGraphicsScene();
    mRect = new QGraphicsRectItem();
    mScene->addItem(mRect);
    this->setInteractive(true);
    this->setDragMode(QGraphicsView::RubberBandDrag);
    this->setRubberBandSelectionMode(Qt::ContainsItemShape);
    this->setScene(mScene);
    Cursor_up.load(CURSOR_UP_ICON);
    Cursor_down.load(CURSOR_DOWN_ICON);
    isMousePressed = false;
    mState = UnChecked;
}

Preview::~Preview()
{
    delete(this->scene());
}

void Preview::setPixmap(const QString& path)
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

void Preview::SetState(State s)
{
    mState = s;
}

void Preview::mousePressEvent(QMouseEvent* event)
{
    QGraphicsView::mousePressEvent(event);
    if (mState == Move_State)
    {
        viewport()->setCursor(Qt::ClosedHandCursor);
        if (this->scene() == nullptr)
        {
            qDebug() << "The scene is null";
            return ;
        }
        // 记录鼠标按下时的中心点坐标
        sceneMousePos = mapToScene(event->pos()) - event->pos() +QPointF(width() / 2, height() / 2);

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
    else if (mState == Cut_State)
    {
        if (event->button() == Qt::LeftButton)
        {
            isMousePressed = true;

            mStart = mapToScene(event->pos());
        }
    }
}

void Preview::mouseMoveEvent(QMouseEvent* event)
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
    else if (mState == Cut_State)
    {
        if (isMousePressed)
        {
            //QPoint MousePos = et->pos();
            mEnd = mapToScene(event->pos());
            int pen_width = 4;  //线宽，建议宽一些，否则缩小容易看不见
            /*保证裁剪框在图像内*/
            if (mStart.x() >= mScene->width())
                mStart.setX(mScene->width()-pen_width); //减去线宽，否则会使scene迭代加长
            else if (mStart.x() < 0)
                mStart.setX(0);

            if (mStart.y() >= mScene->height())
                mStart.setY(mScene->height()- pen_width);
            else if (mStart.y() < 0)
                mStart.setY(0);

            if (mEnd.x() >= mScene->width())
                mEnd.setX(mScene->width()- pen_width);
            else if (mEnd.x() < 0)
                mEnd.setX(0);

            if (mEnd.y() >= mScene->height())
                mEnd.setY(mScene->height()- pen_width);
            else if (mEnd.y() < 0)
                mEnd.setY(0);

            
            double width = mScene->width();
            double x = mPixmap->mapToScene(QPoint(mScene->width(), 0)).x();
            mScene->removeItem(mRect);
            QPen p;
            
            p.setWidth(2);
            p.setColor(QColor(255, 0, 0));
            mRect->setPen(p);
            mRect->setRect(mStart.x(), mStart.y(), mEnd.x() - mStart.x(), mEnd.y() - mStart.y());
            //mRect->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            mScene->addItem(mRect);
            this->viewport()->update();     //时刻更新，否则会有残影
        }
    }
}

void Preview::mouseReleaseEvent(QMouseEvent* event)
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
    else if (mState == Cut_State)
    {
        
        if (event->button() == Qt::LeftButton)
        {
            //mEnd = mapToScene(event->pos());
            isMousePressed = false;
        }
    }
}

void Preview::wheelEvent(QWheelEvent* event)
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



void Preview::GetOffset()
{
    QPoint left_top, Qpoint, right_bottom;
    if (mStart == mEnd)
    {
        QMessageBox::warning(NULL, QString::fromLocal8Bit("警告"), QString::fromLocal8Bit("请按住鼠标左键框选感兴趣区域进行裁剪"));
        return;
    }
    if (mStart.x() > mEnd.x())
    {
        left_top.setX(mEnd.x());
        right_bottom.setX(mStart.x());
    }
    else
    {
        left_top.setX(mStart.x());
        right_bottom.setX(mEnd.x());
    }
    if (mStart.y() > mEnd.y())
    {
        left_top.setY(mEnd.y());
        right_bottom.setY(mStart.y());
    }
    else
    {
        left_top.setY(mStart.y());
        right_bottom.setY(mEnd.y());
    }
    //this->scene()->items().at(0)->mapFromItem()
    QPointF Coord_ItemToScene = mPixmap->mapToScene(QPoint(0, 0));
    double width = mScene->width();
    double height = mScene->height();
    double h5_left = double(left_top.x()- Coord_ItemToScene.x()) / width;
    double h5_right = double(right_bottom.x()- Coord_ItemToScene.x()) / width;
    double h5_top = double(left_top.y()- Coord_ItemToScene.y()) / height;
    double h5_bottom = double(right_bottom.y()- Coord_ItemToScene.y()) / height;
    emit SendPos(h5_left, h5_right, h5_top, h5_bottom);     //传递四边在原图的比例以裁剪
    emit Parent_Close();    //用于关闭父亲窗口
}

