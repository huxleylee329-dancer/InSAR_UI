#include"ImageView.h"
#include<qcursor.h>
#include<QWheelEvent>
#include<QMouseEvent>
#include<qmath.h>
#include<qdebug.h>
#include<QScrollBar>
#include<QGraphicsRectItem>

ImageView::ImageView(QWidget* parent) :
	QGraphicsView(parent),
	isMousePressed(false),
	m_roiSelectionEnabled(false),
	m_isDrawingRoi(false),
	m_roiRectItem(nullptr)
{
	setDragMode(QGraphicsView::ScrollHandDrag);
	this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}
ImageView::~ImageView()
{
    delete(this->scene());
}

void ImageView::setRoiSelectionEnabled(bool enabled)
{
	m_roiSelectionEnabled = enabled;
	if (enabled) {
		setDragMode(QGraphicsView::NoDrag);
	} else {
		setDragMode(QGraphicsView::ScrollHandDrag);
		clearRoi();
	}
}

void ImageView::clearRoi()
{
	m_storedRoi = QRectF();
	if (m_roiRectItem && scene()) {
		scene()->removeItem(m_roiRectItem);
		delete m_roiRectItem;
		m_roiRectItem = nullptr;
	}
	m_isDrawingRoi = false;
}

void ImageView::setRoiRect(const QRectF& rect)
{
	m_storedRoi = rect;
	if (rect.isNull() || !scene()) return;
	
	if (!m_roiRectItem) {
		m_roiRectItem = new QGraphicsRectItem();
		QPen pen(Qt::red);
		pen.setWidth(2);
		pen.setCosmetic(true);
		m_roiRectItem->setPen(pen);
		scene()->addItem(m_roiRectItem);
	}
	m_roiRectItem->setRect(rect);
}

void ImageView::loadImage(const QString& path)
{
	if (!scene()) {
		setScene(new QGraphicsScene(this));
	}
	scene()->clear();
	m_roiRectItem = nullptr; // scene()->clear() deletes all items
	
	QPixmap pixmap(path);
	scene()->addPixmap(pixmap);
	
	if (m_roiSelectionEnabled && !m_storedRoi.isNull()) {
		setRoiRect(m_storedRoi);
	}
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
	if (m_roiSelectionEnabled && event->button() == Qt::LeftButton) {
		if (scene()) {
			m_roiStartPos = mapToScene(event->pos());
			m_isDrawingRoi = true;
			
			if (!m_roiRectItem) {
				m_roiRectItem = new QGraphicsRectItem();
				QPen pen(Qt::red);
				pen.setWidth(2);
				pen.setCosmetic(true); // Don't scale the line width
				m_roiRectItem->setPen(pen);
				scene()->addItem(m_roiRectItem);
			}
			m_roiRectItem->setRect(QRectF(m_roiStartPos, m_roiStartPos));
		}
		return;
	}

	QGraphicsView::mousePressEvent(event);
	if (this->dragMode() == QGraphicsView::ScrollHandDrag)
	{
		if (event->button() == Qt::LeftButton)
		{
			posAnchor = event->pos();
			isMousePressed = true;
		}
	}
}

void ImageView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_roiSelectionEnabled && m_isDrawingRoi && m_roiRectItem) {
		QPointF currentPos = mapToScene(event->pos());
		QRectF rect(qMin(m_roiStartPos.x(), currentPos.x()),
					qMin(m_roiStartPos.y(), currentPos.y()),
					qAbs(currentPos.x() - m_roiStartPos.x()),
					qAbs(currentPos.y() - m_roiStartPos.y()));
		m_roiRectItem->setRect(rect);
		return;
	}

	QGraphicsView::mouseMoveEvent(event);
	if (this->dragMode() == QGraphicsView::ScrollHandDrag)
	{
		if (isMousePressed)
		{
			QPointF dis = event->pos() - posAnchor;
			this->horizontalScrollBar()->setValue(this->horizontalScrollBar()->value() - dis.x());
			this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - dis.y());
			posAnchor = event->pos();
		}
	}
}

void ImageView::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_roiSelectionEnabled && event->button() == Qt::LeftButton && m_isDrawingRoi) {
		m_isDrawingRoi = false;
		if (m_roiRectItem) {
			m_storedRoi = m_roiRectItem->rect();
			emit roiSelected(m_storedRoi);
		}
		return;
	}

	QGraphicsView::mouseReleaseEvent(event);
	if (event->button() == Qt::LeftButton)
	{
		isMousePressed = false;
	}
}