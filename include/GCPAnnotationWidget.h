// include/GCPAnnotationWidget.h
#pragma once
#include "ImageView.h"
#include "GCPPoint.h"
#include <QGraphicsItemGroup>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QMap>
#include <QTableWidget>
#include <QPushButton>
#include <QDockWidget>
#include <QLabel>
#include <opencv2/opencv.hpp>

// 预声明 GCPDatabase，防止循环包含
class GCPDatabase;
class GCPAnnotationDockWidget;

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

// GCP 场景辅助器，非侵入式劫持事件与绘制 GCP
class GCPSceneHelper : public QObject
{
    Q_OBJECT
public:
    GCPSceneHelper(ImageView* view, GCPAnnotationDockWidget* dock, QObject* parent = nullptr);
    ~GCPSceneHelper();

    void displayGCPs(const std::vector<GCPPoint>& gcps);
    void clearMarkers();
    void setSelectedGCP(int gcpId);
    void setAnnotationMode(bool enabled);
    ImageView* imageView() const { return m_view; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ImageView* m_view;
    GCPAnnotationDockWidget* m_dock;
    QMap<int, GCPMarkerItem*> m_markerMap;
    int m_selectedGcpId;
    bool m_annotationMode;

    void addOrUpdateMarker(const GCPPoint& gcp);
};

// GCP 交互标注与管理停靠窗口
class GCPAnnotationDockWidget : public QDockWidget
{
    Q_OBJECT
public:
    explicit GCPAnnotationDockWidget(QWidget* parent = nullptr);
    ~GCPAnnotationDockWidget();

    void loadDataset(const QString& h5Path);
    void calculateRealtimeResiduals();
    GCPDatabase* getDatabase() const { return m_db; }

    void bindImageView(ImageView* view);
    ImageView* findActiveImageView();
    ImageView* boundImageView() const { return m_sceneHelper ? m_sceneHelper->imageView() : nullptr; }

signals:
    void gcpDataSaved(const QString& h5Path);

public slots:
    void loadGCPList();
    void onTableSelectionChanged();
    void onTableItemChanged(QTableWidgetItem* item);
    
    // 来自标注视口的关联信号
    void onGcpSelectedFromImage(int gcpId);
    void onGcpAddedOrUpdatedFromImage(int gcpId, double row, double col);
    void onMouseMovedOverImage(const QPointF& scenePos);

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
    QPushButton* m_btnAdd;
    QPushButton* m_btnDelete;
    QPushButton* m_btnImport;
    QPushButton* m_btnExport;
    QPushButton* m_btnClose;
    QLabel* m_magnifierLabel;   // 像素放大镜 QLabel
    QLabel* m_rmseStatusLabel;  // RMSE 状态 QLabel

    bool m_isUpdatingTable; // 防循环更新标志

    GCPSceneHelper* m_sceneHelper; // 场景打点辅助器

    // 缓存参数以便前台进行残差实时评估
    cv::Mat m_rowCoef;
    cv::Mat m_colCoef;
    int m_sceneWidth;
    int m_sceneHeight;
    int m_offsetRow;
    int m_offsetCol;
    double m_rangeSpacing;
    double m_azimuthSpacing;
    QImage m_previewImage;      // 缓存JPG图像，用于局部裁剪生成放大镜
};
