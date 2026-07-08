// GCPAnnotationWidget.cpp
#include "include/GCPAnnotationWidget.h"
#include "include/GCPDatabase.h"
#include "include/GCPImportExport.h"
#include "include/NodeUtils.h"
#include <QMouseEvent>
#include <QGraphicsScene>
#include <QBrush>
#include <QPen>
#include <QDebug>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <cmath>
#include <algorithm>

// ===== GCPMarkerItem 实现 =====

GCPMarkerItem::GCPMarkerItem(int gcpId, double x, double y, const QString& label, int quality, QGraphicsItem* parent)
    : QGraphicsItemGroup(parent)
    , m_gcpId(gcpId)
{
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);

    QColor color;
    if (quality == 2) color = Qt::green;
    else if (quality == 1) color = QColor(255, 165, 0); // 橙色
    else color = Qt::red;

    QPen pen(color, 2);
    QBrush brush(Qt::transparent);

    m_circle = new QGraphicsEllipseItem(-5, -5, 10, 10, this);
    m_circle->setPen(pen);
    m_circle->setBrush(brush);

    m_hLine = new QGraphicsLineItem(-10, 0, 10, 0, this);
    m_hLine->setPen(pen);

    m_vLine = new QGraphicsLineItem(0, -10, 0, 10, this);
    m_vLine->setPen(pen);

    m_text = new QGraphicsTextItem(label, this);
    m_text->setDefaultTextColor(color);
    m_text->setPos(8, -12);

    addToGroup(m_circle);
    addToGroup(m_hLine);
    addToGroup(m_vLine);
    addToGroup(m_text);

    setPos(x, y);
}

void GCPMarkerItem::updatePosition(double x, double y)
{
    setPos(x, y);
}

QVariant GCPMarkerItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged && scene()) {
        QVariant prop = scene()->property("gcpDockWidget");
        if (prop.isValid()) {
            GCPAnnotationDockWidget* dock = (GCPAnnotationDockWidget*)prop.toLongLong();
            if (dock) {
                dock->onGcpAddedOrUpdatedFromImage(m_gcpId, scenePos().y(), scenePos().x());
            }
        }
    }
    return QGraphicsItemGroup::itemChange(change, value);
}


// ===== GCPSceneHelper 实现 =====

GCPSceneHelper::GCPSceneHelper(ImageView* view, GCPAnnotationDockWidget* dock, QObject* parent)
    : QObject(parent)
    , m_view(view)
    , m_dock(dock)
    , m_selectedGcpId(-1)
    , m_annotationMode(false)
{
    if (m_view && m_view->viewport()) {
        m_view->viewport()->installEventFilter(this);
        m_view->setMouseTracking(true);
        m_view->viewport()->setMouseTracking(true);
        
        // 设置 Scene 属性，方便 Marker 寻回
        if (m_view->scene()) {
            m_view->scene()->setProperty("gcpDockWidget", (qlonglong)m_dock);
        }
    }
}

GCPSceneHelper::~GCPSceneHelper()
{
    clearMarkers();
    if (m_view && m_view->viewport()) {
        m_view->viewport()->removeEventFilter(this);
        if (m_view->scene()) {
            m_view->scene()->setProperty("gcpDockWidget", QVariant());
        }
    }
}

void GCPSceneHelper::displayGCPs(const std::vector<GCPPoint>& gcps)
{
    clearMarkers();

    for (const auto& gcp : gcps) {
        if (gcp.isAnnotated()) {
            addOrUpdateMarker(gcp);
        }
    }
}

void GCPSceneHelper::clearMarkers()
{
    if (!m_view || !m_view->scene()) return;

    QGraphicsScene* sc = m_view->scene();
    for (auto* marker : m_markerMap.values()) {
        sc->removeItem(marker);
        delete marker;
    }
    m_markerMap.clear();
}

void GCPSceneHelper::setSelectedGCP(int gcpId)
{
    m_selectedGcpId = gcpId;
    for (auto it = m_markerMap.begin(); it != m_markerMap.end(); ++it) {
        GCPMarkerItem* marker = it.value();
        QGraphicsEllipseItem* circle = marker->circleItem();
        if (circle) {
            QPen pen = circle->pen();
            if (it.key() == gcpId) {
                pen.setWidth(4);
            } else {
                pen.setWidth(2);
            }
            circle->setPen(pen);
        }
    }
}

void GCPSceneHelper::setAnnotationMode(bool enabled)
{
    m_annotationMode = enabled;
    for (auto* marker : m_markerMap.values()) {
        marker->setFlag(QGraphicsItem::ItemIsMovable, enabled);
    }
}

bool GCPSceneHelper::eventFilter(QObject* watched, QEvent* event)
{
    if (m_view && watched == m_view->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPointF scenePos = m_view->mapToScene(mouseEvent->pos());
            
            // Ctrl + 左键点击：打点或修改控制点坐标
            if (m_annotationMode && mouseEvent->button() == Qt::LeftButton && (mouseEvent->modifiers() & Qt::ControlModifier)) {
                if (m_selectedGcpId >= 0) {
                    m_dock->onGcpAddedOrUpdatedFromImage(m_selectedGcpId, scenePos.y(), scenePos.x());
                    return true; // 拦截事件，防止主视口进行默认的拖拽/框选
                }
            }
            
            // 点击现有的点：选中控制点
            QGraphicsItem* clickedItem = m_view->scene()->itemAt(scenePos, m_view->transform());
            if (clickedItem) {
                QGraphicsItem* parent = clickedItem->parentItem();
                while (parent && !qgraphicsitem_cast<GCPMarkerItem*>(parent)) {
                    parent = parent->parentItem();
                }
                GCPMarkerItem* marker = qgraphicsitem_cast<GCPMarkerItem*>(parent);
                if (marker) {
                    int gcpId = marker->gcpId();
                    setSelectedGCP(gcpId);
                    m_dock->onGcpSelectedFromImage(gcpId);
                    return false; // 不拦截，以便让拖拽机制本身工作
                }
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPointF scenePos = m_view->mapToScene(mouseEvent->pos());
            m_dock->onMouseMovedOverImage(scenePos);
        }
    }
    return QObject::eventFilter(watched, event);
}

void GCPSceneHelper::addOrUpdateMarker(const GCPPoint& gcp)
{
    if (!m_view || !m_view->scene()) return;

    double x = gcp.col;
    double y = gcp.row;

    if (m_markerMap.contains(gcp.id)) {
        m_markerMap[gcp.id]->updatePosition(x, y);
    } else {
        QString label = QString("GCP_%1").arg(gcp.id);
        if (!gcp.description.empty()) {
            label += QString(" (%1)").fromStdString(gcp.description);
        }
        
        GCPMarkerItem* marker = new GCPMarkerItem(gcp.id, x, y, label, gcp.quality);
        marker->setFlag(QGraphicsItem::ItemIsMovable, m_annotationMode);
        
        m_view->scene()->addItem(marker);
        m_markerMap[gcp.id] = marker;
    }
}


// ===== GCPAnnotationDockWidget 实现 =====
#include "include/MainWindow.h"
#include "include/InterfaceManager.h"
#include "include/WorkspaceUI.h"
#include "GCPManager.h"
#include <QPainter>

GCPAnnotationDockWidget::GCPAnnotationDockWidget(QWidget* parent)
    : QDockWidget(parent)
    , m_db(nullptr)
    , m_isUpdatingTable(false)
    , m_sceneHelper(nullptr)
    , m_sceneWidth(0)
    , m_sceneHeight(0)
    , m_offsetRow(0)
    , m_offsetCol(0)
    , m_rangeSpacing(0.0)
    , m_azimuthSpacing(0.0)
{
    setWindowTitle(QStringLiteral("GCP 控制点标注与管理"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    QWidget* contentWidget = new QWidget(this);
    setWidget(contentWidget);

    createLayout();
    setupConnections();

    m_db = new GCPDatabase(this);
}

GCPAnnotationDockWidget::~GCPAnnotationDockWidget()
{
}

void GCPAnnotationDockWidget::createLayout()
{
    QWidget* contentWidget = widget();
    QHBoxLayout* mainLayout = new QHBoxLayout(contentWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);

    // 左侧控制子面板
    QWidget* leftPanel = new QWidget(contentWidget);
    leftPanel->setFixedWidth(300);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(5);

    // 放大镜与状态指示面板
    QHBoxLayout* infoLayout = new QHBoxLayout();
    m_magnifierLabel = new QLabel(leftPanel);
    m_magnifierLabel->setFixedSize(100, 100);
    m_magnifierLabel->setStyleSheet("border: 1px solid rgba(192, 199, 212, 0.5); background-color: black;");
    m_magnifierLabel->setAlignment(Qt::AlignCenter);

    m_rmseStatusLabel = new QLabel(leftPanel);
    m_rmseStatusLabel->setText(QStringLiteral("请双击底图影像以开始标注...\n评估点数: 0"));
    m_rmseStatusLabel->setStyleSheet("font-size: 11px; color: #1e88e5; line-height: 1.3;");
    m_rmseStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    infoLayout->addWidget(m_magnifierLabel);
    infoLayout->addWidget(m_rmseStatusLabel, 1);
    leftLayout->addLayout(infoLayout);

    // 功能按钮栏
    QGridLayout* btnLayout = new QGridLayout();
    m_btnAdd = new QPushButton(QStringLiteral("添加点"), leftPanel);
    m_btnDelete = new QPushButton(QStringLiteral("删除点"), leftPanel);
    m_btnImport = new QPushButton(QStringLiteral("导入..."), leftPanel);
    m_btnExport = new QPushButton(QStringLiteral("导出..."), leftPanel);
    m_btnClose = new QPushButton(QStringLiteral("保存并隐藏"), leftPanel);
    m_btnClose->setMinimumHeight(28);

    btnLayout->addWidget(m_btnAdd, 0, 0);
    btnLayout->addWidget(m_btnDelete, 0, 1);
    btnLayout->addWidget(m_btnImport, 1, 0);
    btnLayout->addWidget(m_btnExport, 1, 1);
    btnLayout->addWidget(m_btnClose, 2, 0, 1, 2);
    leftLayout->addLayout(btnLayout);

    mainLayout->addWidget(leftPanel);

    // 右侧表格视图
    m_table = new QTableWidget(contentWidget);
    m_table->setColumnCount(10);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    
    QStringList headers;
    headers << "ID" << QStringLiteral("经度 (Lon)") << QStringLiteral("纬度 (Lat)") 
            << QStringLiteral("高程 (Hgt)") << QStringLiteral("行号 (Row)") << QStringLiteral("列号 (Col)") 
            << QStringLiteral("距离残差") << QStringLiteral("方位残差") << QStringLiteral("质量") << QStringLiteral("描述");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    
    mainLayout->addWidget(m_table, 1);
    contentWidget->setFixedHeight(200); // 底部栏固定高度，节省主视口高度空间
}

void GCPAnnotationDockWidget::setupConnections()
{
    connect(m_btnAdd, &QPushButton::clicked, this, &GCPAnnotationDockWidget::onAddGcp);
    connect(m_btnDelete, &QPushButton::clicked, this, &GCPAnnotationDockWidget::onDeleteGcp);
    connect(m_btnImport, &QPushButton::clicked, this, &GCPAnnotationDockWidget::onImportGcp);
    connect(m_btnExport, &QPushButton::clicked, this, &GCPAnnotationDockWidget::onExportGcp);
    
    connect(m_btnClose, &QPushButton::clicked, this, [this]() {
        emit gcpDataSaved(m_h5Path);
        this->hide();
    });

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &GCPAnnotationDockWidget::onTableSelectionChanged);
    connect(m_table, &QTableWidget::itemChanged, this, &GCPAnnotationDockWidget::onTableItemChanged);
}

ImageView* GCPAnnotationDockWidget::findActiveImageView()
{
    MainWindow* mainWin = qobject_cast<MainWindow*>(window());
    if (!mainWin) return nullptr;

    // 1. 如果在 Workspace 模式，从活动 Tab 中找 ImageView
    if (mainWin->workspaceUI() && mainWin->workspaceUI()->isVisible()) {
        return mainWin->workspaceUI()->activeImageView();
    }

    // 2. 如果在 Workflow 模式，从当前的整个主窗口子树中找可见 of ImageView
    QList<ImageView*> views = mainWin->findChildren<ImageView*>();
    for (ImageView* view : views) {
        if (view->isVisible()) {
            return view;
        }
    }

    return nullptr;
}

void GCPAnnotationDockWidget::bindImageView(ImageView* view)
{
    if (m_sceneHelper) {
        delete m_sceneHelper;
        m_sceneHelper = nullptr;
    }
    
    if (view) {
        m_sceneHelper = new GCPSceneHelper(view, this, this);
        m_sceneHelper->setAnnotationMode(true);
        
        // 绑定后，刷新渲染当前影像的所有控制点
        if (m_db && m_db->isOpen()) {
            std::vector<GCPPoint> gcps = m_db->getGCPs();
            m_sceneHelper->displayGCPs(gcps);
        }
    }
}

void GCPAnnotationDockWidget::loadDataset(const QString& h5Path)
{
    m_h5Path = h5Path;
    
    MainWindow* mainWin = qobject_cast<MainWindow*>(window());
    if (mainWin && mainWin->interfaceManager()) {
        QString projPath = mainWin->interfaceManager()->projectPath();
        QString projBase = QFileInfo(projPath).baseName();
        QString projDir = QFileInfo(projPath).absolutePath();
        QString dbPath = projDir + "/" + projBase + "_gcp.db";
        
        if (m_db) {
            m_db->close();
            m_db->open(dbPath);
        }
    }
    
    // 2. 从底图 H5 中提取并缓存定位参数
    m_rowCoef = cv::Mat();
    m_colCoef = cv::Mat();
    m_sceneWidth = 0;
    m_sceneHeight = 0;
    m_offsetRow = 0;
    m_offsetCol = 0;
    m_rangeSpacing = 0.0;
    m_azimuthSpacing = 0.0;
    
    if (QFile::exists(m_h5Path)) {
        NodeUtils::Hdf5Locker locker;
        NodeUtils::readScalarFromH5(m_h5Path, "range_len", m_sceneWidth);
        NodeUtils::readScalarFromH5(m_h5Path, "azimuth_len", m_sceneHeight);
        NodeUtils::readScalarFromH5(m_h5Path, "offset_row", m_offsetRow);
        NodeUtils::readScalarFromH5(m_h5Path, "offset_col", m_offsetCol);
        NodeUtils::readMatFromH5(m_h5Path, "row_coefficient", m_rowCoef, CV_64F);
        NodeUtils::readMatFromH5(m_h5Path, "col_coefficient", m_colCoef, CV_64F);
        NodeUtils::readScalarFromH5(m_h5Path, "range_spacing", m_rangeSpacing);
        if (!NodeUtils::readScalarFromH5(m_h5Path, "azimuth_spacing", m_azimuthSpacing)) {
            m_azimuthSpacing = m_rangeSpacing * 2.0;
        }
    }
    
    // 3. 寻找当前活动的大影像视口并挂载 GCP 覆盖层
    ImageView* activeView = findActiveImageView();
    bindImageView(activeView);
    
    // 4. 载入本地 JPG 预览图用于左侧放大镜（因为影像已被加载，JPG必已存在）
    QFileInfo fi(m_h5Path);
    m_jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
    if (QFile::exists(m_jpgPath)) {
        m_previewImage.load(m_jpgPath);
    } else {
        m_previewImage = QImage();
    }
    
    // 5. 刷新控制点列表与残差估计
    loadGCPList();
}

void GCPAnnotationDockWidget::prepareImagePreview()
{
    // 本方案复用主视口，由 loadDataset 自动处理
}

void GCPAnnotationDockWidget::loadGCPList()
{
    if (!m_db || !m_db->isOpen()) return;

    m_isUpdatingTable = true;
    m_table->setRowCount(0);

    std::vector<GCPPoint> gcps = m_db->getGCPs();
    
    for (size_t i = 0; i < gcps.size(); ++i) {
        const auto& gcp = gcps[i];
        m_table->insertRow(i);

        QTableWidgetItem* idItem = new QTableWidgetItem(QString::number(gcp.id));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 0, idItem);

        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(gcp.lon, 'f', 8)));
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(gcp.lat, 'f', 8)));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(gcp.height, 'f', 3)));

        QString rowStr = std::isnan(gcp.row) ? "" : QString::number(gcp.row, 'f', 3);
        QString colStr = std::isnan(gcp.col) ? "" : QString::number(gcp.col, 'f', 3);
        QTableWidgetItem* rowItem = new QTableWidgetItem(rowStr);
        QTableWidgetItem* colItem = new QTableWidgetItem(colStr);
        rowItem->setFlags(rowItem->flags() & ~Qt::ItemIsEditable);
        colItem->setFlags(colItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 4, rowItem);
        m_table->setItem(i, 5, colItem);

        QString resRStr = std::isnan(gcp.residual_range) ? "" : QString::number(gcp.residual_range, 'f', 3);
        QString resAStr = std::isnan(gcp.residual_azimuth) ? "" : QString::number(gcp.residual_azimuth, 'f', 3);
        QTableWidgetItem* resRItem = new QTableWidgetItem(resRStr);
        QTableWidgetItem* resAItem = new QTableWidgetItem(resAStr);
        resRItem->setFlags(resRItem->flags() & ~Qt::ItemIsEditable);
        resAItem->setFlags(resAItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 6, resRItem);
        m_table->setItem(i, 7, resAItem);

        m_table->setItem(i, 8, new QTableWidgetItem(QString::number(gcp.quality)));
        m_table->setItem(i, 9, new QTableWidgetItem(QString::fromStdString(gcp.description)));
    }

    if (m_sceneHelper) {
        m_sceneHelper->displayGCPs(gcps);
    }
    
    m_isUpdatingTable = false;

    calculateRealtimeResiduals();
}

void GCPAnnotationDockWidget::onTableSelectionChanged()
{
    if (m_isUpdatingTable) return;

    int curRow = m_table->currentRow();
    if (curRow < 0) return;

    int gcpId = m_table->item(curRow, 0)->text().toInt();
    if (m_sceneHelper) {
        m_sceneHelper->setSelectedGCP(gcpId);
    }
}

void GCPAnnotationDockWidget::onTableItemChanged(QTableWidgetItem* item)
{
    if (m_isUpdatingTable || !m_db) return;

    int row = item->row();
    int gcpId = m_table->item(row, 0)->text().toInt();

    GCPPoint gcp = m_db->getGCP(gcpId);
    int col = item->column();
    bool ok = false;
    double val = item->text().toDouble(&ok);

    if (col == 1 && ok) {
        gcp.lon = val;
        gcp.residual_range = std::numeric_limits<double>::quiet_NaN();
        gcp.residual_azimuth = std::numeric_limits<double>::quiet_NaN();
    } 
    else if (col == 2 && ok) {
        gcp.lat = val;
        gcp.residual_range = std::numeric_limits<double>::quiet_NaN();
        gcp.residual_azimuth = std::numeric_limits<double>::quiet_NaN();
    } 
    else if (col == 3 && ok) {
        gcp.height = val;
        gcp.residual_range = std::numeric_limits<double>::quiet_NaN();
        gcp.residual_azimuth = std::numeric_limits<double>::quiet_NaN();
    } 
    else if (col == 8) {
        int qual = item->text().toInt(&ok);
        if (ok && (qual >= 0 && qual <= 2)) {
            gcp.quality = qual;
        }
    } 
    else if (col == 9) {
        gcp.description = item->text().trimmed().toStdString();
    }

    m_db->updateGCP(gcp);
    
    QMetaObject::invokeMethod(this, "loadGCPList", Qt::QueuedConnection);
}

void GCPAnnotationDockWidget::onGcpSelectedFromImage(int gcpId)
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (m_table->item(i, 0)->text().toInt() == gcpId) {
            m_table->selectRow(i);
            break;
        }
    }
}

void GCPAnnotationDockWidget::onGcpAddedOrUpdatedFromImage(int gcpId, double row, double col)
{
    if (!m_db) return;

    GCPPoint gcp = m_db->getGCP(gcpId);
    gcp.row = row;
    gcp.col = col;
    
    gcp.residual_range = std::numeric_limits<double>::quiet_NaN();
    gcp.residual_azimuth = std::numeric_limits<double>::quiet_NaN();

    m_db->updateGCP(gcp);
    
    loadGCPList();
    
    if (m_sceneHelper) {
        m_sceneHelper->setSelectedGCP(gcpId);
    }
}

void GCPAnnotationDockWidget::onMouseMovedOverImage(const QPointF& scenePos)
{
    if (m_previewImage.isNull()) {
        m_magnifierLabel->setText(QStringLiteral("无底图缓存"));
        return;
    }

    int px = static_cast<int>(std::round(scenePos.x()));
    int py = static_cast<int>(std::round(scenePos.y()));

    int w = m_previewImage.width();
    int h = m_previewImage.height();

    if (px < 0 || px >= w || py < 0 || py >= h) {
        m_magnifierLabel->setText(QStringLiteral("越界"));
        return;
    }

    int size = 11;
    int half = size / 2;
    int srcX = px - half;
    int srcY = py - half;

    int clampX = std::max(0, std::min(srcX, w - size));
    int clampY = std::max(0, std::min(srcY, h - size));

    QImage subImg = m_previewImage.copy(clampX, clampY, size, size);
    QPixmap pix = QPixmap::fromImage(subImg.scaled(120, 120, Qt::KeepAspectRatio, Qt::FastTransformation));

    QPainter painter(&pix);
    painter.setPen(QPen(Qt::red, 1));
    painter.drawLine(60, 0, 60, 120);
    painter.drawLine(0, 60, 120, 60);
    painter.end();

    m_magnifierLabel->setPixmap(pix);
}

void GCPAnnotationDockWidget::calculateRealtimeResiduals()
{
    if (!m_db || !m_db->isOpen()) return;

    std::vector<GCPPoint> gcps = m_db->getGCPs();
    if (gcps.empty()) {
        m_rmseStatusLabel->setText(QStringLiteral("无控制点数据\n评估点数: 0"));
        return;
    }

    std::vector<GCPPoint> activeGcps;
    for (const auto& gcp : gcps) {
        if (gcp.isAnnotated()) {
            activeGcps.push_back(gcp);
        }
    }

    if (activeGcps.empty()) {
        m_rmseStatusLabel->setText(QStringLiteral("无有效标注点\n评估点数: 0"));
        return;
    }

    if (m_rowCoef.empty() || m_colCoef.empty()) {
        m_rmseStatusLabel->setText(QStringLiteral("未加载定位多项式参数\n评估点数: %1").arg(activeGcps.size()));
        return;
    }

    GCPManager manager;
    GCPEvaluationResult evalResult;

    manager.evaluate_coregistration_accuracy(
        activeGcps,
        m_rowCoef, m_colCoef,
        m_sceneHeight, m_sceneWidth,
        m_offsetRow, m_offsetCol,
        m_rangeSpacing, m_azimuthSpacing,
        evalResult
    );

    m_db->updateGCPs(activeGcps);

    m_isUpdatingTable = true;
    
    for (int i = 0; i < m_table->rowCount(); ++i) {
        int id = m_table->item(i, 0)->text().toInt();
        for (const auto& gcp : activeGcps) {
            if (gcp.id == id) {
                QString resRStr = std::isnan(gcp.residual_range) ? "" : QString::number(gcp.residual_range, 'f', 3);
                QString resAStr = std::isnan(gcp.residual_azimuth) ? "" : QString::number(gcp.residual_azimuth, 'f', 3);
                
                m_table->item(i, 6)->setText(resRStr);
                m_table->item(i, 7)->setText(resAStr);

                QColor rowBgColor = Qt::transparent;
                if (!std::isnan(gcp.residual_range) && !std::isnan(gcp.residual_azimuth)) {
                    double pixelR = gcp.residual_range / m_rangeSpacing;
                    double pixelA = gcp.residual_azimuth / m_azimuthSpacing;
                    double pixelErr = std::sqrt(pixelR * pixelR + pixelA * pixelA);
                    
                    if (pixelErr > 2.0) {
                        rowBgColor = QColor(255, 230, 230);
                    }
                }
                for (int col = 0; col < m_table->columnCount(); ++col) {
                    m_table->item(i, col)->setBackground(QBrush(rowBgColor));
                }
                break;
            }
        }
    }

    m_isUpdatingTable = false;

    QString statusText = QStringLiteral("评估点数: %1\n距离向 RMS: %2 米\n方位向 RMS: %3 米\n二维综合 RMS: %4 米")
        .arg(evalResult.num_gcp_used)
        .arg(evalResult.mean_residual_range, 0, 'f', 3)
        .arg(evalResult.mean_residual_azimuth, 0, 'f', 3)
        .arg(evalResult.rms_residual_2d, 0, 'f', 3);
        
    if (!m_sceneHelper) {
        statusText += QStringLiteral("\n\n⚠️ 提示：未在主界面中找到活动的影像视口。\n请先双击或打开该底图影像以启用图形标注。");
    }
    
    m_rmseStatusLabel->setText(statusText);
    
    emit gcpDataSaved(m_h5Path);
}

void GCPAnnotationDockWidget::onAddGcp()
{
    if (!m_db) return;

    GCPPoint gcp;
    gcp.lon = 0.0;
    gcp.lat = 0.0;
    gcp.height = 0.0;
    gcp.quality = 1;
    gcp.source = "manual";

    int newId = m_db->addGCP(gcp);
    if (newId >= 0) {
        loadGCPList();
        for (int i = 0; i < m_table->rowCount(); ++i) {
            if (m_table->item(i, 0)->text().toInt() == newId) {
                m_table->setCurrentCell(i, 1);
                break;
            }
        }
    }
}

void GCPAnnotationDockWidget::onDeleteGcp()
{
    int curRow = m_table->currentRow();
    if (curRow < 0 || !m_db) return;

    int gcpId = m_table->item(curRow, 0)->text().toInt();
    
    if (QMessageBox::question(this, QStringLiteral("确认删除"), 
        QStringLiteral("确定要删除控制点 GCP_%1 吗？").arg(gcpId)) == QMessageBox::Yes) {
        m_db->deleteGCP(gcpId);
        loadGCPList();
    }
}

void GCPAnnotationDockWidget::onImportGcp()
{
    if (!m_db) return;

    QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入地面控制点"), "", 
        "Supported Files (*.csv *.shp);;CSV Files (*.csv);;Shapefile (*.shp)"
    );

    if (filePath.isEmpty()) return;

    std::vector<GCPPoint> imported;
    bool success = false;

    if (filePath.endsWith(".csv", Qt::CaseInsensitive)) {
        success = GCPImportExport::importCSV(filePath, imported);
    } else if (filePath.endsWith(".shp", Qt::CaseInsensitive)) {
        success = GCPImportExport::importShapefile(filePath, imported);
    }

    if (success && !imported.empty()) {
        m_db->addGCPs(imported);
        loadGCPList();
        QMessageBox::information(this, QStringLiteral("导入成功"), 
            QStringLiteral("已成功导入 %1 个控制点。").arg(imported.size()));
    } else {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("未导入任何有效的控制点，请检查格式。"));
    }
}

void GCPAnnotationDockWidget::onExportGcp()
{
    if (!m_db) return;

    std::vector<GCPPoint> gcps = m_db->getGCPs();
    if (gcps.empty()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("当前数据库中没有控制点可供导出。"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出地面控制点"), "", 
        "CSV Files (*.csv);;Shapefile (*.shp)"
    );

    if (filePath.isEmpty()) return;

    bool success = false;
    if (filePath.endsWith(".csv", Qt::CaseInsensitive)) {
        success = GCPImportExport::exportCSV(filePath, gcps);
    } else if (filePath.endsWith(".shp", Qt::CaseInsensitive)) {
        success = GCPImportExport::exportShapefile(filePath, gcps);
    }

    if (success) {
        QMessageBox::information(this, QStringLiteral("导出成功"), QStringLiteral("控制点已成功保存至:\n") + filePath);
    } else {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("导出控制点失败，请检查文件写入权限。"));
    }
}
