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
        QGraphicsScene* sc = scene();
        QList<QGraphicsView*> views = sc->views();
        for (QGraphicsView* view : views) {
            GCPAnnotationWidget* widget = qobject_cast<GCPAnnotationWidget*>(view);
            if (widget) {
                widget->notifyMarkerMoved(m_gcpId, scenePos());
                break;
            }
        }
    }
    return QGraphicsItemGroup::itemChange(change, value);
}


// ===== GCPAnnotationWidget 实现 =====

GCPAnnotationWidget::GCPAnnotationWidget(QWidget* parent)
    : ImageView(parent)
    , m_annotationMode(false)
    , m_selectedGcpId(-1)
{
}

GCPAnnotationWidget::~GCPAnnotationWidget()
{
}

void GCPAnnotationWidget::displayGCPs(const std::vector<GCPPoint>& gcps)
{
    clearGCPMarkers();

    for (const auto& gcp : gcps) {
        if (gcp.isAnnotated()) {
            addOrUpdateMarker(gcp);
        }
    }
}

void GCPAnnotationWidget::clearGCPMarkers()
{
    if (!scene()) return;

    for (auto* marker : m_markerMap.values()) {
        scene()->removeItem(marker);
        delete marker;
    }
    m_markerMap.clear();
}

void GCPAnnotationWidget::setAnnotationMode(bool enabled)
{
    m_annotationMode = enabled;
    for (auto* marker : m_markerMap.values()) {
        marker->setFlag(QGraphicsItem::ItemIsMovable, enabled);
    }
}

void GCPAnnotationWidget::setSelectedGCP(int gcpId)
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

void GCPAnnotationWidget::mousePressEvent(QMouseEvent* event)
{
    if (!scene()) {
        ImageView::mousePressEvent(event);
        return;
    }

    QPointF scenePos = mapToScene(event->pos());

    if (m_annotationMode && event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        if (m_selectedGcpId >= 0) {
            emit gcpAddedOrUpdated(m_selectedGcpId, scenePos.y(), scenePos.x());
            return;
        }
    }

    QGraphicsItem* clickedItem = scene()->itemAt(scenePos, transform());
    if (clickedItem) {
        QGraphicsItem* parent = clickedItem->parentItem();
        while (parent && !qgraphicsitem_cast<GCPMarkerItem*>(parent)) {
            parent = parent->parentItem();
        }
        
        GCPMarkerItem* marker = qgraphicsitem_cast<GCPMarkerItem*>(parent);
        if (marker) {
            int gcpId = marker->gcpId();
            setSelectedGCP(gcpId);
            emit gcpSelected(gcpId);
            ImageView::mousePressEvent(event);
            return;
        }
    }

    ImageView::mousePressEvent(event);
}

void GCPAnnotationWidget::mouseReleaseEvent(QMouseEvent* event)
{
    ImageView::mouseReleaseEvent(event);
}

void GCPAnnotationWidget::addOrUpdateMarker(const GCPPoint& gcp)
{
    if (!scene()) return;

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
        
        scene()->addItem(marker);
        m_markerMap[gcp.id] = marker;
    }
}

void GCPAnnotationWidget::notifyMarkerMoved(int gcpId, const QPointF& newScenePos)
{
    emit gcpAddedOrUpdated(gcpId, newScenePos.y(), newScenePos.x());
}


// ===== GCPAnnotationDialog 实现 =====

GCPAnnotationDialog::GCPAnnotationDialog(const QString& h5Path, GCPDatabase* db, QWidget* parent)
    : QDialog(parent)
    , m_h5Path(h5Path)
    , m_db(db)
    , m_isUpdatingTable(false)
{
    setWindowTitle(QStringLiteral("GCP 控制点标注与管理"));
    resize(1200, 750);
    setModal(true);

    createLayout();
    setupConnections();
    
    // 异步或同步准备预览图
    prepareImagePreview();
    
    // 加载数据列表
    loadGCPList();
}

GCPAnnotationDialog::~GCPAnnotationDialog()
{
}

void GCPAnnotationDialog::createLayout()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    // ================= 左侧控制区 =================
    QWidget* leftWidget = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // 按钮工具栏
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnAdd = new QPushButton(QStringLiteral("添加点"), this);
    m_btnDelete = new QPushButton(QStringLiteral("删除点"), this);
    m_btnImport = new QPushButton(QStringLiteral("导入..."), this);
    m_btnExport = new QPushButton(QStringLiteral("导出..."), this);

    btnLayout->addWidget(m_btnAdd);
    btnLayout->addWidget(m_btnDelete);
    btnLayout->addWidget(m_btnImport);
    btnLayout->addWidget(m_btnExport);
    leftLayout->addLayout(btnLayout);

    // 表格视图
    m_table = new QTableWidget(this);
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
    
    leftLayout->addWidget(m_table);

    // 关闭按钮
    m_btnClose = new QPushButton(QStringLiteral("保存并关闭"), this);
    m_btnClose->setMinimumHeight(35);
    leftLayout->addWidget(m_btnClose);

    leftWidget->setFixedWidth(500);
    mainLayout->addWidget(leftWidget);

    // ================= 右侧影像区 =================
    m_view = new GCPAnnotationWidget(this);
    m_view->setAnnotationMode(true); // 始终在弹窗里启用标注模式
    mainLayout->addWidget(m_view, 1);
}

void GCPAnnotationDialog::setupConnections()
{
    connect(m_btnAdd, &QPushButton::clicked, this, &GCPAnnotationDialog::onAddGcp);
    connect(m_btnDelete, &QPushButton::clicked, this, &GCPAnnotationDialog::onDeleteGcp);
    connect(m_btnImport, &QPushButton::clicked, this, &GCPAnnotationDialog::onImportGcp);
    connect(m_btnExport, &QPushButton::clicked, this, &GCPAnnotationDialog::onExportGcp);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &GCPAnnotationDialog::onTableSelectionChanged);
    connect(m_table, &QTableWidget::itemChanged, this, &GCPAnnotationDialog::onTableItemChanged);

    // 绑定影像标注的事件信号
    connect(m_view, &GCPAnnotationWidget::gcpSelected, this, &GCPAnnotationDialog::onGcpSelectedFromImage);
    connect(m_view, &GCPAnnotationWidget::gcpAddedOrUpdated, this, &GCPAnnotationDialog::onGcpAddedOrUpdatedFromImage);
}

void GCPAnnotationDialog::prepareImagePreview()
{
    if (m_h5Path.isEmpty() || !QFile::exists(m_h5Path)) {
        m_view->setMoveMode();
        return;
    }

    QFileInfo fi(m_h5Path);
    // 临时预览图存放路径，命名为同级同名的 jpg
    m_jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";

    if (QFile::exists(m_jpgPath)) {
        m_view->loadImage(m_jpgPath);
        m_view->fitImage();
        return;
    }

    // 若本地无预览图，启用 QProgressDialog 异步生成，防止 UI 线程阻塞
    QProgressDialog* progressDlg = new QProgressDialog(
        QStringLiteral("正在从 H5 数据中提取生成影像预览图，请稍候..."), 
        QString(), 0, 0, this
    );
    progressDlg->setWindowTitle(QStringLiteral("影像提取"));
    progressDlg->setWindowModality(Qt::WindowModal);
    progressDlg->show();

    // 跨线程池启动生成任务
    QFutureWatcher<bool>* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, progressDlg, watcher]() {
        progressDlg->close();
        progressDlg->deleteLater();
        if (watcher->result() && QFile::exists(m_jpgPath)) {
            m_view->loadImage(m_jpgPath);
            m_view->fitImage();
            
            // 影像加载完后，重新渲染控制点，确保标记在最新的图像视口上呈现
            loadGCPList();
        } else {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("影像预览图生成失败。"));
        }
        watcher->deleteLater();
    });

    QFuture<bool> future = QtConcurrent::run(NodeUtils::generateJpgPreviewFromH5, m_h5Path, m_jpgPath, QString("complex"));
    watcher->setFuture(future);
}

void GCPAnnotationDialog::loadGCPList()
{
    if (!m_db || !m_db->isOpen()) return;

    m_isUpdatingTable = true;
    m_table->setRowCount(0);

    std::vector<GCPPoint> gcps = m_db->getGCPs();
    
    for (size_t i = 0; i < gcps.size(); ++i) {
        const auto& gcp = gcps[i];
        m_table->insertRow(i);

        // ID
        QTableWidgetItem* idItem = new QTableWidgetItem(QString::number(gcp.id));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable); // ID 不可编辑
        m_table->setItem(i, 0, idItem);

        // Lon, Lat, Height (可编辑)
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(gcp.lon, 'f', 8)));
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(gcp.lat, 'f', 8)));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(gcp.height, 'f', 3)));

        // Row, Col (行列号在影像中标注，表格中不可直接编辑)
        QString rowStr = std::isnan(gcp.row) ? "" : QString::number(gcp.row, 'f', 3);
        QString colStr = std::isnan(gcp.col) ? "" : QString::number(gcp.col, 'f', 3);
        QTableWidgetItem* rowItem = new QTableWidgetItem(rowStr);
        QTableWidgetItem* colItem = new QTableWidgetItem(colStr);
        rowItem->setFlags(rowItem->flags() & ~Qt::ItemIsEditable);
        colItem->setFlags(colItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 4, rowItem);
        m_table->setItem(i, 5, colItem);

        // Residuals (不可编辑)
        QString resRStr = std::isnan(gcp.residual_range) ? "" : QString::number(gcp.residual_range, 'f', 3);
        QString resAStr = std::isnan(gcp.residual_azimuth) ? "" : QString::number(gcp.residual_azimuth, 'f', 3);
        QTableWidgetItem* resRItem = new QTableWidgetItem(resRStr);
        QTableWidgetItem* resAItem = new QTableWidgetItem(resAStr);
        resRItem->setFlags(resRItem->flags() & ~Qt::ItemIsEditable);
        resAItem->setFlags(resAItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 6, resRItem);
        m_table->setItem(i, 7, resAItem);

        // Quality (可编辑，0,1,2)
        m_table->setItem(i, 8, new QTableWidgetItem(QString::number(gcp.quality)));

        // Description (可编辑)
        m_table->setItem(i, 9, new QTableWidgetItem(QString::fromStdString(gcp.description)));
    }

    m_view->displayGCPs(gcps);
    m_isUpdatingTable = false;
}

void GCPAnnotationDialog::onTableSelectionChanged()
{
    if (m_isUpdatingTable) return;

    int curRow = m_table->currentRow();
    if (curRow < 0) return;

    int gcpId = m_table->item(curRow, 0)->text().toInt();
    m_view->setSelectedGCP(gcpId);
}

void GCPAnnotationDialog::onTableItemChanged(QTableWidgetItem* item)
{
    if (m_isUpdatingTable || !m_db) return;

    int row = item->row();
    int gcpId = m_table->item(row, 0)->text().toInt();

    // 加载当前最新点位
    GCPPoint gcp = m_db->getGCP(gcpId);

    // 根据修改列进行更新
    int col = item->column();
    bool ok = false;
    double val = item->text().toDouble(&ok);

    if (col == 1 && ok) { // Lon
        gcp.lon = val;
        // 关键安全规范：地理坐标改变，重置原有残差数据
        gcp.residual_range = std::numeric_limits<double>::quiet_NaN();
        gcp.residual_azimuth = std::numeric_limits<double>::quiet_NaN();
    } 
    else if (col == 2 && ok) { // Lat
        gcp.lat = val;
        gcp.residual_range = std::numeric_limits<double>::quiet_NaN();
        gcp.residual_azimuth = std::numeric_limits<double>::quiet_NaN();
    } 
    else if (col == 3 && ok) { // Height
        gcp.height = val;
        gcp.residual_range = std::numeric_limits<double>::quiet_NaN();
        gcp.residual_azimuth = std::numeric_limits<double>::quiet_NaN();
    } 
    else if (col == 8) { // Quality
        int qual = item->text().toInt(&ok);
        if (ok && (qual >= 0 && qual <= 2)) {
            gcp.quality = qual;
        }
    } 
    else if (col == 9) { // Description
        gcp.description = item->text().trimmed().toStdString();
    }

    m_db->updateGCP(gcp);
    
    // 异步更新显示
    QMetaObject::invokeMethod(this, "loadGCPList", Qt::QueuedConnection);
}

void GCPAnnotationDialog::onGcpSelectedFromImage(int gcpId)
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (m_table->item(i, 0)->text().toInt() == gcpId) {
            m_table->selectRow(i);
            break;
        }
    }
}

void GCPAnnotationDialog::onGcpAddedOrUpdatedFromImage(int gcpId, double row, double col)
{
    if (!m_db) return;

    GCPPoint gcp = m_db->getGCP(gcpId);
    gcp.row = row;
    gcp.col = col;
    
    // 标注微调后重置以前的残差
    gcp.residual_range = std::numeric_limits<double>::quiet_NaN();
    gcp.residual_azimuth = std::numeric_limits<double>::quiet_NaN();

    m_db->updateGCP(gcp);
    
    // 刷新显示并保持高亮
    loadGCPList();
    m_view->setSelectedGCP(gcpId);
}

void GCPAnnotationDialog::onAddGcp()
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
        // 选中新行，方便编辑
        for (int i = 0; i < m_table->rowCount(); ++i) {
            if (m_table->item(i, 0)->text().toInt() == newId) {
                m_table->setCurrentCell(i, 1);
                break;
            }
        }
    }
}

void GCPAnnotationDialog::onDeleteGcp()
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

void GCPAnnotationDialog::onImportGcp()
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

void GCPAnnotationDialog::onExportGcp()
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
