#pragma once

#include "BaseWorker.h"
#include <QStringList>

class SLCDerampWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit SLCDerampWorker(QObject* parent = nullptr);
    ~SLCDerampWorker();

public slots:
    void SLC_deramp(
        int masterIndex,
        QString project_name,
        QString src_node,
        QString dst_node,
        QStandardItemModel* model
    );

signals:
    // 特有信号：回传 SLC 去斜坡结果
    void sendResults(const QString& dstNode, const QStringList& h5Paths, const QStringList& originNames);
};
