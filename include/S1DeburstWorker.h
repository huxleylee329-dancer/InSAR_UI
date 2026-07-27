#pragma once
#include "BaseWorker.h"
#include <QStringList>

class S1DeburstWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit S1DeburstWorker(QObject* parent = nullptr);
    ~S1DeburstWorker();

public slots:
    void S1_Deburst(
        QString savePath,
        QString dstProject,
        QString dstNode,
        QStringList inputPaths
    );

signals:
    // 特有信号：回传生成的 H5 路径列表和 origin 名称列表
    void cancelled();
    void sendResults(QString dstNode, QStringList deburstH5Paths, QStringList originNames);
};

