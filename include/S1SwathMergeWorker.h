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
        QString projectName,
        QString savePath,
        QString dstNode,
        QString firstH5Path,
        QString secondH5Path,
        QString thirdH5Path
    );

signals:
    // 特有信号：回传计算结果，由调用方完成 XML 写入
    void cancelled();
    void sendResult(QString dstNode, QString filename, QString mergedH5Path,
                    QString savePath, QString projectName);
};
