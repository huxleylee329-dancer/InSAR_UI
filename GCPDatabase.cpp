// GCPDatabase.cpp
#include "include/GCPDatabase.h"
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QVariant>
#include <QDateTime>
#include <QDebug>
#include <cmath>

GCPDatabase::GCPDatabase(QObject* parent)
    : QObject(parent)
    , m_isOpen(false)
{
    // 为当前实例生成唯一的数据库连接名称，防止连接名冲突
    m_connectionName = QString("GcpConnection_%1").arg(QString::number((quintptr)this, 16));
}

GCPDatabase::~GCPDatabase()
{
    close();
}

bool GCPDatabase::open(const QString& dbPath)
{
    if (m_isOpen) {
        if (m_dbPath == dbPath) {
            return true;
        }
        close();
    }

    m_dbPath = dbPath;
    
    // 创建 SQLite 数据库连接
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qDebug() << "GCPDatabase Error: Failed to open database at" << dbPath 
                 << "-" << db.lastError().text();
        m_isOpen = false;
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    m_isOpen = true;
    
    // 初始化表结构
    if (!createTables()) {
        close();
        return false;
    }

    return true;
}

void GCPDatabase::close()
{
    if (m_isOpen) {
        // 必须先让连接离开作用域（显式关闭），然后再移出数据库，防止 Qt 警告连接仍在使用中
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
        m_isOpen = false;
    }
}

bool GCPDatabase::isOpen() const
{
    return m_isOpen;
}

QString GCPDatabase::databasePath() const
{
    return m_dbPath;
}

bool GCPDatabase::createTables()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    // 创建 GCP 控制点表
    QString sqlCreateGcp = 
        "CREATE TABLE IF NOT EXISTS gcp_points ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  lon REAL NOT NULL,"
        "  lat REAL NOT NULL,"
        "  height REAL DEFAULT 0.0,"
        "  row REAL DEFAULT NULL,"
        "  col REAL DEFAULT NULL,"
        "  residual_range REAL DEFAULT NULL,"
        "  residual_azimuth REAL DEFAULT NULL,"
        "  residual_height REAL DEFAULT NULL,"
        "  coherence REAL DEFAULT NULL,"
        "  description TEXT,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  source VARCHAR(50) DEFAULT 'manual',"
        "  quality INTEGER DEFAULT 1"
        ");";

    if (!query.exec(sqlCreateGcp)) {
        qDebug() << "GCPDatabase Error: Failed to create table 'gcp_points'-" << query.lastError().text();
        return false;
    }

    // 创建控制点分类标签表
    QString sqlCreateTags =
        "CREATE TABLE IF NOT EXISTS gcp_tags ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  gcp_id INTEGER NOT NULL,"
        "  tag VARCHAR(100) NOT NULL,"
        "  FOREIGN KEY (gcp_id) REFERENCES gcp_points(id) ON DELETE CASCADE,"
        "  UNIQUE(gcp_id, tag)"
        ");";

    if (!query.exec(sqlCreateTags)) {
        qDebug() << "GCPDatabase Error: Failed to create table 'gcp_tags'-" << query.lastError().text();
        return false;
    }

    // 为 gcp_id 创建索引以加快关联查询
    QString sqlCreateIndex = "CREATE INDEX IF NOT EXISTS idx_gcp_tags_gcp_id ON gcp_tags(gcp_id);";
    query.exec(sqlCreateIndex);

    return true;
}

int GCPDatabase::addGCP(const GCPPoint& gcp)
{
    if (!m_isOpen) return -1;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    query.prepare(
        "INSERT INTO gcp_points (lon, lat, height, row, col, residual_range, residual_azimuth, "
        "residual_height, coherence, description, timestamp, source, quality) "
        "VALUES (:lon, :lat, :height, :row, :col, :res_r, :res_a, :res_h, :coh, :desc, :time, :src, :qual)"
    );

    query.bindValue(":lon", gcp.lon);
    query.bindValue(":lat", gcp.lat);
    query.bindValue(":height", gcp.height);

    // C++ NaN 映射为 SQLite NULL
    query.bindValue(":row", std::isnan(gcp.row) ? QVariant(QVariant::Double) : gcp.row);
    query.bindValue(":col", std::isnan(gcp.col) ? QVariant(QVariant::Double) : gcp.col);
    query.bindValue(":res_r", std::isnan(gcp.residual_range) ? QVariant(QVariant::Double) : gcp.residual_range);
    query.bindValue(":res_a", std::isnan(gcp.residual_azimuth) ? QVariant(QVariant::Double) : gcp.residual_azimuth);
    query.bindValue(":res_h", std::isnan(gcp.residual_height) ? QVariant(QVariant::Double) : gcp.residual_height);
    query.bindValue(":coh", std::isnan(gcp.coherence) ? QVariant(QVariant::Double) : gcp.coherence);
    
    query.bindValue(":desc", gcp.description.empty() ? QVariant(QVariant::String) : QString::fromStdString(gcp.description));
    query.bindValue(":time", gcp.timestamp.empty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") : QString::fromStdString(gcp.timestamp));
    query.bindValue(":src", QString::fromStdString(gcp.source));
    query.bindValue(":qual", gcp.quality);

    if (!query.exec()) {
        qDebug() << "GCPDatabase Error: Failed to add GCP -" << query.lastError().text();
        return -1;
    }

    return query.lastInsertId().toInt();
}

bool GCPDatabase::updateGCP(const GCPPoint& gcp)
{
    if (!m_isOpen || gcp.id < 0) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    query.prepare(
        "UPDATE gcp_points SET "
        "  lon = :lon, lat = :lat, height = :height, "
        "  row = :row, col = :col, "
        "  residual_range = :res_r, residual_azimuth = :res_a, residual_height = :res_h, "
        "  coherence = :coh, description = :desc, source = :src, quality = :qual "
        "WHERE id = :id"
    );

    query.bindValue(":id", gcp.id);
    query.bindValue(":lon", gcp.lon);
    query.bindValue(":lat", gcp.lat);
    query.bindValue(":height", gcp.height);
    query.bindValue(":row", std::isnan(gcp.row) ? QVariant(QVariant::Double) : gcp.row);
    query.bindValue(":col", std::isnan(gcp.col) ? QVariant(QVariant::Double) : gcp.col);
    query.bindValue(":res_r", std::isnan(gcp.residual_range) ? QVariant(QVariant::Double) : gcp.residual_range);
    query.bindValue(":res_a", std::isnan(gcp.residual_azimuth) ? QVariant(QVariant::Double) : gcp.residual_azimuth);
    query.bindValue(":res_h", std::isnan(gcp.residual_height) ? QVariant(QVariant::Double) : gcp.residual_height);
    query.bindValue(":coh", std::isnan(gcp.coherence) ? QVariant(QVariant::Double) : gcp.coherence);
    query.bindValue(":desc", gcp.description.empty() ? QVariant(QVariant::String) : QString::fromStdString(gcp.description));
    query.bindValue(":src", QString::fromStdString(gcp.source));
    query.bindValue(":qual", gcp.quality);

    if (!query.exec()) {
        qDebug() << "GCPDatabase Error: Failed to update GCP -" << query.lastError().text();
        return false;
    }

    return true;
}

bool GCPDatabase::deleteGCP(int id)
{
    if (!m_isOpen) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    query.prepare("DELETE FROM gcp_points WHERE id = :id");
    query.bindValue(":id", id);

    if (!query.exec()) {
        qDebug() << "GCPDatabase Error: Failed to delete GCP -" << query.lastError().text();
        return false;
    }

    return true;
}

bool GCPDatabase::clearGCPs()
{
    if (!m_isOpen) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    // 会触发级联删除 gcp_tags 里的数据
    if (!query.exec("DELETE FROM gcp_points")) {
        qDebug() << "GCPDatabase Error: Failed to clear GCPs -" << query.lastError().text();
        return false;
    }

    return true;
}

bool GCPDatabase::updateGCPs(const std::vector<GCPPoint>& gcps)
{
    if (!m_isOpen) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    
    // 使用 SQLite 事务保护，防止频繁磁盘 I/O 引起主线程卡顿
    db.transaction();

    QSqlQuery query(db);
    query.prepare(
        "UPDATE gcp_points SET "
        "  lon = :lon, lat = :lat, height = :height, "
        "  row = :row, col = :col, "
        "  residual_range = :res_r, residual_azimuth = :res_a, residual_height = :res_h, "
        "  coherence = :coh, description = :desc, source = :src, quality = :qual "
        "WHERE id = :id"
    );

    for (const auto& gcp : gcps) {
        if (gcp.id < 0) continue;

        query.bindValue(":id", gcp.id);
        query.bindValue(":lon", gcp.lon);
        query.bindValue(":lat", gcp.lat);
        query.bindValue(":height", gcp.height);
        query.bindValue(":row", std::isnan(gcp.row) ? QVariant(QVariant::Double) : gcp.row);
        query.bindValue(":col", std::isnan(gcp.col) ? QVariant(QVariant::Double) : gcp.col);
        query.bindValue(":res_r", std::isnan(gcp.residual_range) ? QVariant(QVariant::Double) : gcp.residual_range);
        query.bindValue(":res_a", std::isnan(gcp.residual_azimuth) ? QVariant(QVariant::Double) : gcp.residual_azimuth);
        query.bindValue(":res_h", std::isnan(gcp.residual_height) ? QVariant(QVariant::Double) : gcp.residual_height);
        query.bindValue(":coh", std::isnan(gcp.coherence) ? QVariant(QVariant::Double) : gcp.coherence);
        query.bindValue(":desc", gcp.description.empty() ? QVariant(QVariant::String) : QString::fromStdString(gcp.description));
        query.bindValue(":src", QString::fromStdString(gcp.source));
        query.bindValue(":qual", gcp.quality);

        if (!query.exec()) {
            qDebug() << "GCPDatabase Error: Batch update failed on ID" << gcp.id << "-" << query.lastError().text();
            db.rollback();
            return false;
        }
    }

    db.commit();
    return true;
}

bool GCPDatabase::addGCPs(const std::vector<GCPPoint>& gcps)
{
    if (!m_isOpen) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    db.transaction();

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO gcp_points (lon, lat, height, row, col, residual_range, residual_azimuth, "
        "residual_height, coherence, description, timestamp, source, quality) "
        "VALUES (:lon, :lat, :height, :row, :col, :res_r, :res_a, :res_h, :coh, :desc, :time, :src, :qual)"
    );

    for (const auto& gcp : gcps) {
        query.bindValue(":lon", gcp.lon);
        query.bindValue(":lat", gcp.lat);
        query.bindValue(":height", gcp.height);
        query.bindValue(":row", std::isnan(gcp.row) ? QVariant(QVariant::Double) : gcp.row);
        query.bindValue(":col", std::isnan(gcp.col) ? QVariant(QVariant::Double) : gcp.col);
        query.bindValue(":res_r", std::isnan(gcp.residual_range) ? QVariant(QVariant::Double) : gcp.residual_range);
        query.bindValue(":res_a", std::isnan(gcp.residual_azimuth) ? QVariant(QVariant::Double) : gcp.residual_azimuth);
        query.bindValue(":res_h", std::isnan(gcp.residual_height) ? QVariant(QVariant::Double) : gcp.residual_height);
        query.bindValue(":coh", std::isnan(gcp.coherence) ? QVariant(QVariant::Double) : gcp.coherence);
        query.bindValue(":desc", gcp.description.empty() ? QVariant(QVariant::String) : QString::fromStdString(gcp.description));
        query.bindValue(":time", gcp.timestamp.empty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") : QString::fromStdString(gcp.timestamp));
        query.bindValue(":src", QString::fromStdString(gcp.source));
        query.bindValue(":qual", gcp.quality);

        if (!query.exec()) {
            qDebug() << "GCPDatabase Error: Batch insert failed -" << query.lastError().text();
            db.rollback();
            return false;
        }
    }

    db.commit();
    return true;
}

GCPPoint GCPDatabase::getGCP(int id)
{
    GCPPoint gcp;
    if (!m_isOpen) return gcp;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    query.prepare("SELECT * FROM gcp_points WHERE id = :id");
    query.bindValue(":id", id);

    if (query.exec() && query.next()) {
        gcp.id = query.value("id").toInt();
        gcp.lon = query.value("lon").toDouble();
        gcp.lat = query.value("lat").toDouble();
        gcp.height = query.value("height").toDouble();

        // SQLite NULL 转换为 C++ NaN
        gcp.row = query.value("row").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("row").toDouble();
        gcp.col = query.value("col").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("col").toDouble();
        gcp.residual_range = query.value("residual_range").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_range").toDouble();
        gcp.residual_azimuth = query.value("residual_azimuth").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_azimuth").toDouble();
        gcp.residual_height = query.value("residual_height").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_height").toDouble();
        gcp.coherence = query.value("coherence").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("coherence").toDouble();

        gcp.description = query.value("description").toString().toStdString();
        gcp.timestamp = query.value("timestamp").toString().toStdString();
        gcp.source = query.value("source").toString().toStdString();
        gcp.quality = query.value("quality").toInt();
    }

    return gcp;
}

std::vector<GCPPoint> GCPDatabase::getGCPs()
{
    std::vector<GCPPoint> list;
    if (!m_isOpen) return list;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    if (query.exec("SELECT * FROM gcp_points ORDER BY id ASC")) {
        while (query.next()) {
            GCPPoint gcp;
            gcp.id = query.value("id").toInt();
            gcp.lon = query.value("lon").toDouble();
            gcp.lat = query.value("lat").toDouble();
            gcp.height = query.value("height").toDouble();

            gcp.row = query.value("row").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("row").toDouble();
            gcp.col = query.value("col").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("col").toDouble();
            gcp.residual_range = query.value("residual_range").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_range").toDouble();
            gcp.residual_azimuth = query.value("residual_azimuth").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_azimuth").toDouble();
            gcp.residual_height = query.value("residual_height").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_height").toDouble();
            gcp.coherence = query.value("coherence").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("coherence").toDouble();

            gcp.description = query.value("description").toString().toStdString();
            gcp.timestamp = query.value("timestamp").toString().toStdString();
            gcp.source = query.value("source").toString().toStdString();
            gcp.quality = query.value("quality").toInt();

            list.push_back(gcp);
        }
    }

    return list;
}

std::vector<GCPPoint> GCPDatabase::getAnnotatedGCPs()
{
    std::vector<GCPPoint> list;
    if (!m_isOpen) return list;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    // 筛选 row 和 col 都不为空且有效的已标注点
    if (query.exec("SELECT * FROM gcp_points WHERE row IS NOT NULL AND col IS NOT NULL ORDER BY id ASC")) {
        while (query.next()) {
            GCPPoint gcp;
            gcp.id = query.value("id").toInt();
            gcp.lon = query.value("lon").toDouble();
            gcp.lat = query.value("lat").toDouble();
            gcp.height = query.value("height").toDouble();

            gcp.row = query.value("row").toDouble();
            gcp.col = query.value("col").toDouble();
            gcp.residual_range = query.value("residual_range").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_range").toDouble();
            gcp.residual_azimuth = query.value("residual_azimuth").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_azimuth").toDouble();
            gcp.residual_height = query.value("residual_height").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("residual_height").toDouble();
            gcp.coherence = query.value("coherence").isNull() ? std::numeric_limits<double>::quiet_NaN() : query.value("coherence").toDouble();

            gcp.description = query.value("description").toString().toStdString();
            gcp.timestamp = query.value("timestamp").toString().toStdString();
            gcp.source = query.value("source").toString().toStdString();
            gcp.quality = query.value("quality").toInt();

            list.push_back(gcp);
        }
    }

    return list;
}

int GCPDatabase::getGCPCount()
{
    if (!m_isOpen) return 0;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    if (query.exec("SELECT COUNT(*) FROM gcp_points") && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

int GCPDatabase::getAnnotatedCount()
{
    if (!m_isOpen) return 0;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    if (query.exec("SELECT COUNT(*) FROM gcp_points WHERE row IS NOT NULL AND col IS NOT NULL") && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}
