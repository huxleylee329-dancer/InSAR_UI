#pragma once

#include "BaseWorker.h"
#include <QStringList>

class DeformationRateFieldWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit DeformationRateFieldWorker(QObject* parent = nullptr);
    ~DeformationRateFieldWorker();

public slots:
    void analyze_rate_field(
        QString  projectPath,           // 工程路径（可能为 .insar 文件路径，需取目录）
        QString  projectName,
        QString  dstNode,               // 输出目录名
        QStringList filePaths,          // 输入 SBAS H5 文件列表
        int      modelType,             // 1=线性（仅评估不确定性）, 2=二次多项式
        double   confidenceLevel,       // 置信水平（默认 0.95）
        double   coherenceThresholdHigh,
        double   coherenceThresholdMid,
        double   uncertaintyThresholdHigh,
        double   uncertaintyThresholdMid,
        int      colorMap,              // 0=蓝-白-红, 1=热力图, 2=彩虹
        bool     showContour,
        int      contourInterval,
        bool     showArrow,
        int      arrowSpacing,
        bool     outputDirectoryIsStaging = false
    );

signals:
    void cancelled();
    void outputsGenerated(const QString& dstNode, const QString& outputH5Path);
};
