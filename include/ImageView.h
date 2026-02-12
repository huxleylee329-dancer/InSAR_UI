#pragma once
#include<QGraphicsView>

class ImageView : public QGraphicsView
{
	Q_OBJECT;
public:
	explicit ImageView(QWidget* parent = Q_NULLPTR);
	~ImageView();
protected:
	void wheelEvent(QWheelEvent*);
	void mouseMoveEvent(QMouseEvent*);
	void mousePressEvent(QMouseEvent*);
	void mouseReleaseEvent(QMouseEvent*);
private:
	QPointF sceneMousePos;//scene鼠标滑轮滚动时的中心坐标，用于鼠标中心缩放
	QPointF posAnchor;//view鼠标坐标，用于拖拽，由于拖拽过程中有抖动，故不转化为scene坐标
	bool isMousePressed;
};