#pragma once

#include "BaseWorker.h"

class S1SwathMergeWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit S1SwathMergeWorker(QObject* parent = nullptr);
    ~S1SwathMergeWorker();

public slots:
    void S1_swath_merge(
        int index1,
        int index2,
        int index3,
        QString project_name,
        QString srcNode1,
        QString srcNode2,
        QString srcNode3,
        QString dstNode,
        QStandardItemModel* model
    );

signals:
    // 特有信号：回传计算结果，由调用方完成 XML 写入
    void sendResult(QString dstNode, QString filename, QString savePath, QString projectName);
};
