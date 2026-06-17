#pragma once

#include "BaseWorker.h"

class S1FrameMergeWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit S1FrameMergeWorker(QObject* parent = nullptr);
    ~S1FrameMergeWorker();

public slots:
    void S1_frame_merge(
        int index1,
        int index2,
        QString project_name,
        QString srcNode1,
        QString srcNode2,
        QString dstNode,
        QStandardItemModel* model
    );

signals:
    // 特有信号：回传计算结果，由调用方完成 XML 写入
    void sendResult(QString dstNode, QString filename, QString savePath, QString projectName);
};
