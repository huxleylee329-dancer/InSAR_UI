// include/GCPAnnotationWidget.h
#pragma once
#include "ImageView.h"
#include "GCPPoint.h"
#include <QGraphicsItemGroup>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QMap>
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>

// 自定义控制点图形项，支持鼠标拖拽，并在拖拽结束后通知界面
class GCPMarkerItem : public QGraphicsItemGroup
{
public:
    GCPMarkerItem(int gcpId, double x, double y, const QString& label, int quality, QGraphicsItem* parent = nullptr);
    int gcpId() const { return m_gcpId; }
    void updatePosition(double x, double y);
    QGraphicsEllipseItem* circleItem() const { return m_circle; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    int m_gcpId;
    QGraphicsEllipseItem* m_circle;
    QGraphicsLineItem* m_hLine;
    QGraphicsLineItem* m_vLine;
    QGraphicsTextItem* m_text;
};

class GCPAnnotationWidget : public ImageView
{
    Q_OBJECT
public:
    explicit GCPAnnotationWidget(QWidget* parent = nullptr);
    ~GCPAnnotationWidget();

    // 加载/清除控制点标记
    void displayGCPs(const std::vector<GCPPoint>& gcps);
    void clearGCPMarkers();

    // 标注模式设置
    void setAnnotationMode(bool enabled);
    bool isAnnotationMode() const { return m_annotationMode; }

    // 选择/激活特定 GCP
    void setSelectedGCP(int gcpId);
    int selectedGCP() const { return m_selectedGcpId; }

signals:
    void gcpAddedOrUpdated(int gcpId, double row, double col);
    void gcpSelected(int gcpId);

protected:
    // 重写事件以捕获标注操作
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_annotationMode;
    int m_selectedGcpId;
    
    // gcpId -> MarkerItem 的映射
    QMap<int, GCPMarkerItem*> m_markerMap;
    
    // 根据 GCP 属性渲染单个标记并添加到场景中
    void addOrUpdateMarker(const GCPPoint& gcp);
    
    friend class GCPMarkerItem;
    // 供 GCPMarkerItem 拖动结束时调用，以发出更新信号
    void notifyMarkerMoved(int gcpId, const QPointF& newScenePos);
};

// 预声明 GCPDatabase，防止循环包含
class GCPDatabase;

// GCP 交互标注与管理大窗口
class GCPAnnotationDialog : public QDialog
{
    Q_OBJECT
public:
    GCPAnnotationDialog(const QString& h5Path, GCPDatabase* db, QWidget* parent = nullptr);
    ~GCPAnnotationDialog();

private slots:
    void loadGCPList();
    void onTableSelectionChanged();
    void onTableItemChanged(QTableWidgetItem* item);
    
    // 来自标注视口的关联信号
    void onGcpSelectedFromImage(int gcpId);
    void onGcpAddedOrUpdatedFromImage(int gcpId, double row, double col);

    // 按钮操作
    void onAddGcp();
    void onDeleteGcp();
    void onImportGcp();
    void onExportGcp();

private:
    void createLayout();
    void setupConnections();
    void prepareImagePreview();

    QString m_h5Path;
    QString m_jpgPath;
    GCPDatabase* m_db;
    
    // 界面控件
    QTableWidget* m_table;
    GCPAnnotationWidget* m_view;
    QPushButton* m_btnAdd;
    QPushButton* m_btnDelete;
    QPushButton* m_btnImport;
    QPushButton* m_btnExport;
    QPushButton* m_btnClose;

    bool m_isUpdatingTable; // 防循环更新标志
};
