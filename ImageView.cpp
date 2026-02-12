#include"ImageView.h"
#include<qcursor.h>
#include<QWheelEvent>
#include<QMouseEvent>
#include<qmath.h>
#include<qdebug.h>
ImageView::ImageView(QWidget* parent) :
	QGraphicsView(parent)
{
    isMousePressed = false;
}
ImageView::~ImageView()
{
    delete(this->scene());
}
void ImageView::wheelEvent(QWheelEvent* event)
{
	if (event->orientation() == Qt::Vertical)
	{
		double angleDeltaY = event->angleDelta().y();
		double zoomFactor = qPow(1.0015, angleDeltaY);
		scale(zoomFactor, zoomFactor);
		if (angleDeltaY > 0)
		{
			this->centerOn(sceneMousePos);
			sceneMousePos = this->mapToScene(event->pos());
		}
		this->viewport()->update();
		event->accept();
	}
	else
		event->ignore();
}



void ImageView::mousePressEvent(QMouseEvent* event)
{
    QGraphicsView::mousePressEvent(event);
    if (this->scene() == nullptr)
    {
        qDebug() << "The scene is null";
        return;
    }
    // 记录鼠标按下时的中心点坐标
    sceneMousePos = this->mapToScene(event->pos());
    // 记录当前鼠标在view中的位置，用来在mouseMove事件中计算偏移
    // 此处不将view坐标转换成scene坐标的原因是优化性能，在move的过程中会产生抖动
    posAnchor = event->pos();
    isMousePressed = true;
}

void ImageView::mouseMoveEvent(QMouseEvent* event)
{
    QGraphicsView::mouseMoveEvent(event);
    QPointF offsetPos = event->pos() - posAnchor;
    if (isMousePressed) {
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        centerOn(sceneMousePos - offsetPos);
    }
}

void ImageView::mouseReleaseEvent(QMouseEvent* event)
{
    QGraphicsView::mouseReleaseEvent(event);
    isMousePressed = false;
}