#include"ImageView.h"
#include<qcursor.h>
#include<QWheelEvent>
#include<QMouseEvent>
#include<qmath.h>
#include<qdebug.h>
#include<QScrollBar>
#include<QGraphicsRectItem>
#include<QGraphicsPixmapItem>

ImageView::ImageView(QWidget* parent) :
	QGraphicsView(parent),
	isMousePressed(false),
	m_roiSelectionMode(RoiSelectionMode::None),
	m_isDrawingRoi(false),
	m_roiRectItem(nullptr),
	m_targetRectItem(nullptr),
	m_clutterRectItem(nullptr),
	m_needsFit(false)
{
	setDragMode(QGraphicsView::ScrollHandDrag);
	this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}
ImageView::~ImageView()
{
    delete(this->scene());
}

void ImageView::setRoiSelectionMode(RoiSelectionMode mode)
{
	m_roiSelectionMode = mode;
	if (mode != RoiSelectionMode::None) {
		setDragMode(QGraphicsView::NoDrag);
	} else {
		setDragMode(QGraphicsView::ScrollHandDrag);
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
	if (m_roiSelectionMode == RoiSelectionMode::Single) m_isDrawingRoi = false;
}

void ImageView::clearTargetRoi()
{
	m_storedTargetRoi = QRectF();
	if (m_targetRectItem && scene()) {
		scene()->removeItem(m_targetRectItem);
		delete m_targetRectItem;
		m_targetRectItem = nullptr;
	}
	if (m_roiSelectionMode == RoiSelectionMode::Target) m_isDrawingRoi = false;
}

void ImageView::clearClutterRoi()
{
	m_storedClutterRoi = QRectF();
	if (m_clutterRectItem && scene()) {
		scene()->removeItem(m_clutterRectItem);
		delete m_clutterRectItem;
		m_clutterRectItem = nullptr;
	}
	if (m_roiSelectionMode == RoiSelectionMode::Clutter) m_isDrawingRoi = false;
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

void ImageView::setTargetRoiRect(const QRectF& rect)
{
	m_storedTargetRoi = rect;
	if (rect.isNull() || !scene()) return;
	
	if (!m_targetRectItem) {
		m_targetRectItem = new QGraphicsRectItem();
		QPen pen(Qt::red);
		pen.setWidth(2);
		pen.setCosmetic(true);
		m_targetRectItem->setPen(pen);
		scene()->addItem(m_targetRectItem);
	}
	m_targetRectItem->setRect(rect);
}

void ImageView::setClutterRoiRect(const QRectF& rect)
{
	m_storedClutterRoi = rect;
	if (rect.isNull() || !scene()) return;
	
	if (!m_clutterRectItem) {
		m_clutterRectItem = new QGraphicsRectItem();
		QPen pen(Qt::green);
		pen.setWidth(2);
		pen.setCosmetic(true);
		m_clutterRectItem->setPen(pen);
		scene()->addItem(m_clutterRectItem);
	}
	m_clutterRectItem->setRect(rect);
}

void ImageView::loadImage(const QString& path)
{
	if (!scene()) {
		setScene(new QGraphicsScene(this));
	}
	scene()->clear();
	m_roiRectItem = nullptr;
	m_targetRectItem = nullptr;
	m_clutterRectItem = nullptr;
	
	QPixmap pixmap(path);
	scene()->addPixmap(pixmap);
	
	if (!m_storedRoi.isNull()) setRoiRect(m_storedRoi);
	if (!m_storedTargetRoi.isNull()) setTargetRoiRect(m_storedTargetRoi);
	if (!m_storedClutterRoi.isNull()) setClutterRoiRect(m_storedClutterRoi);
	
	scene()->setSceneRect(pixmap.rect());
	m_needsFit = true;
	viewport()->update();
}

void ImageView::fitImage()
{
	if (scene() && !scene()->sceneRect().isEmpty()) {
		fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
		m_needsFit = false;
		updateTransformationMode();
	}
}

void ImageView::resizeEvent(QResizeEvent* event)
{
	QGraphicsView::resizeEvent(event);
	if (m_needsFit) {
		fitImage();
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
		updateTransformationMode();
		this->viewport()->update();
		event->accept();
	}
	else
		event->ignore();
}

void ImageView::mousePressEvent(QMouseEvent* event)
{
	if (m_roiSelectionMode != RoiSelectionMode::None && event->button() == Qt::LeftButton) {
		if (scene()) {
			m_roiStartPos = mapToScene(event->pos());
			m_isDrawingRoi = true;
			
			QGraphicsRectItem** activeRectItem = &m_roiRectItem;
			QColor rectColor = Qt::red;
			
			if (m_roiSelectionMode == RoiSelectionMode::Target) {
				activeRectItem = &m_targetRectItem;
			} else if (m_roiSelectionMode == RoiSelectionMode::Clutter) {
				activeRectItem = &m_clutterRectItem;
				rectColor = Qt::green;
			}
			
			if (!*activeRectItem) {
				*activeRectItem = new QGraphicsRectItem();
				QPen pen(rectColor);
				pen.setWidth(2);
				pen.setCosmetic(true); // Don't scale the line width
				(*activeRectItem)->setPen(pen);
				scene()->addItem(*activeRectItem);
			}
			(*activeRectItem)->setRect(QRectF(m_roiStartPos, m_roiStartPos));
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
	if (m_roiSelectionMode != RoiSelectionMode::None && m_isDrawingRoi) {
		QPointF currentPos = mapToScene(event->pos());
		QRectF rect(qMin(m_roiStartPos.x(), currentPos.x()),
					qMin(m_roiStartPos.y(), currentPos.y()),
					qAbs(currentPos.x() - m_roiStartPos.x()),
					qAbs(currentPos.y() - m_roiStartPos.y()));
					
		QGraphicsRectItem* activeRectItem = nullptr;
		if (m_roiSelectionMode == RoiSelectionMode::Single) activeRectItem = m_roiRectItem;
		else if (m_roiSelectionMode == RoiSelectionMode::Target) activeRectItem = m_targetRectItem;
		else if (m_roiSelectionMode == RoiSelectionMode::Clutter) activeRectItem = m_clutterRectItem;
		
		if (activeRectItem) {
			activeRectItem->setRect(rect);
		}
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
	if (m_roiSelectionMode != RoiSelectionMode::None && event->button() == Qt::LeftButton && m_isDrawingRoi) {
		m_isDrawingRoi = false;
		
		if (m_roiSelectionMode == RoiSelectionMode::Single && m_roiRectItem) {
			m_storedRoi = m_roiRectItem->rect();
			emit roiSelected(m_storedRoi);
		} else if (m_roiSelectionMode == RoiSelectionMode::Target && m_targetRectItem) {
			m_storedTargetRoi = m_targetRectItem->rect();
			emit targetRoiSelected(m_storedTargetRoi);
		} else if (m_roiSelectionMode == RoiSelectionMode::Clutter && m_clutterRectItem) {
			m_storedClutterRoi = m_clutterRectItem->rect();
			emit clutterRoiSelected(m_storedClutterRoi);
		}
		return;
	}

	QGraphicsView::mouseReleaseEvent(event);
	if (event->button() == Qt::LeftButton)
	{
		isMousePressed = false;
	}
}

void ImageView::updateTransformationMode()
{
	if (!scene()) return;

	double currentScale = transform().m11();
	Qt::TransformationMode mode = (currentScale < 0.95) ? Qt::SmoothTransformation : Qt::FastTransformation;

	QList<QGraphicsItem*> allItems = scene()->items();
	for (QGraphicsItem* item : allItems) {
		QGraphicsPixmapItem* pixmapItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(item);
		if (pixmapItem) {
			pixmapItem->setTransformationMode(mode);
		}
	}
}

