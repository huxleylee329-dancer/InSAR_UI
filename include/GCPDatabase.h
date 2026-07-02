// include/GCPDatabase.h
#pragma once
#include <QObject>
#include <QtSql/QSqlDatabase>
#include <QList>
#include <QString>
#include "GCPPoint.h"

class GCPDatabase : public QObject
{
    Q_OBJECT
public:
    explicit GCPDatabase(QObject* parent = nullptr);
    ~GCPDatabase();

    // 数据库连接管理
    bool open(const QString& dbPath);
    void close();
    bool isOpen() const;
    QString databasePath() const;
    QString connectionName() const { return m_connectionName; } // 公开连接名接口，便于批量事务控制

    // GCP 点增删改查
    int addGCP(const GCPPoint& gcp);
    bool updateGCP(const GCPPoint& gcp);
    bool deleteGCP(int id);
    bool clearGCPs();
    
    // 批量更新残差与属性 (支持数据库事务)
    bool updateGCPs(const std::vector<GCPPoint>& gcps);
    bool addGCPs(const std::vector<GCPPoint>& gcps); // 批量插入控制点

    // 获取数据
    GCPPoint getGCP(int id);
    std::vector<GCPPoint> getGCPs();
    std::vector<GCPPoint> getAnnotatedGCPs();

    // 统计指标
    int getGCPCount();
    int getAnnotatedCount();

private:
    bool createTables();
    QString m_dbPath;
    QString m_connectionName;
    bool m_isOpen;
};
