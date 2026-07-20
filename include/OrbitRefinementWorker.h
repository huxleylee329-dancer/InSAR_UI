#pragma once
#include "BaseWorker.h"
#include <QStringList>

class OrbitRefinementWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit OrbitRefinementWorker(QObject* parent = nullptr);
    ~OrbitRefinementWorker();

signals:
    void sendResults(const QString& dstNode, const QStringList& h5Paths, const QStringList& originNames);
    void cancelled();

public slots:
    void refine_orbit(
        QString projectPath,
        QString projectName,
        QString dstNode,
        QStringList filePaths,      // 输入 SLC H5 文件列表
        QString dbPath,             // SQLite 数据库路径
        int masterIndex,            // 参考影像索引
        int polyDegree              // 多项式阶数（1 或 2）
    );
};
