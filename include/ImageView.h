#pragma once
#include<QGraphicsView>
#include <QGraphicsRectItem>

class ImageView : public QGraphicsView
{
	Q_OBJECT
public:
	explicit ImageView(QWidget* parent = Q_NULLPTR);
	~ImageView();
	
	void setRoiSelectionEnabled(bool enabled);
	bool isRoiSelectionEnabled() const { return m_roiSelectionEnabled; }
	void clearRoi();
	void setRoiRect(const QRectF& rect);
	void loadImage(const QString& path);

signals:
	void roiSelected(const QRectF& rect);

protected:
	void wheelEvent(QWheelEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	
private:
	QPointF sceneMousePos;//scene鼠标滑轮滚动时的中心坐标，用于鼠标中心缩放
	QPointF posAnchor;//view鼠标坐标，用于拖拽，由于拖拽过程中有抖动，故不转化为scene坐标
	bool isMousePressed;
	
	bool m_roiSelectionEnabled;
	bool m_isDrawingRoi;
	QGraphicsRectItem* m_roiRectItem;
	QPointF m_roiStartPos;
	QRectF m_storedRoi;
};