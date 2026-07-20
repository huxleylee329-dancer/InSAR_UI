#pragma once
#include<QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QVector>

class ImageView : public QGraphicsView
{
	Q_OBJECT
public:
	explicit ImageView(QWidget* parent = Q_NULLPTR);
	~ImageView();
	
	enum class RoiSelectionMode {
		None,
		Single,
		Target,
		Clutter
	};

	void setRoiSelectionMode(RoiSelectionMode mode);
	RoiSelectionMode roiSelectionMode() const { return m_roiSelectionMode; }
	
	// Backward compatibility
	void setRoiSelectionEnabled(bool enabled) { setRoiSelectionMode(enabled ? RoiSelectionMode::Single : RoiSelectionMode::None); }
	bool isRoiSelectionEnabled() const { return m_roiSelectionMode == RoiSelectionMode::Single; }
	
	void clearRoi();
	void clearTargetRoi();
	void clearClutterRoi();
	
	void setRoiRect(const QRectF& rect);
	void setTargetRoiRect(const QRectF& rect);
	void setClutterRoiRect(const QRectF& rect);
	void setOverlayRects(const QVector<QRectF>& rects, int highlightedIndex = -1);
	void clearOverlayRects();
	
	void loadImage(const QString& path);
	void setImage(const QImage& image);
	void fitImage();
	void resetZoom();
	void zoomIn();
	void zoomOut();
	void zoomBy(double factor);
	void setMoveMode();

signals:
	void roiSelected(const QRectF& rect);
	void targetRoiSelected(const QRectF& rect);
	void clutterRoiSelected(const QRectF& rect);

protected:
	void wheelEvent(QWheelEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	
private:
	QPointF sceneMousePos;//scene鼠标滑轮滚动时的中心坐标，用于鼠标中心缩放
	QPointF posAnchor;//view鼠标坐标，用于拖拽，由于拖拽过程中有抖动，故不转化为scene坐标
	bool isMousePressed;
	
	RoiSelectionMode m_roiSelectionMode;
	bool m_isDrawingRoi;
	
	QGraphicsRectItem* m_roiRectItem;
	QGraphicsRectItem* m_targetRectItem;
	QGraphicsRectItem* m_clutterRectItem;
	QVector<QGraphicsRectItem*> m_overlayRectItems;
	QVector<QGraphicsSimpleTextItem*> m_overlayLabelItems;
	
	QPointF m_roiStartPos;
	QRectF m_storedRoi;
	QRectF m_storedTargetRoi;
	QRectF m_storedClutterRoi;
	QVector<QRectF> m_storedOverlayRects;
	int m_highlightedOverlayIndex = -1;
	bool m_needsFit;

	void updateTransformationMode();
	void redrawOverlayRects();
	void clearOverlayGraphics();
};
