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
        QString projectName,
        QString savePath,
        QString dstNode,
        QString firstH5Path,
        QString secondH5Path
    );

signals:
    // 特有信号：回传计算结果，由调用方完成 XML 写入
    void cancelled();
    void sendResult(QString dstNode, QString filename, QString mergedH5Path,
                    QString savePath, QString projectName);
};
