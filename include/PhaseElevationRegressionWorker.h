#pragma once

#include "BaseWorker.h"
#include <QStringList>
#include <QList>

/**
 * @brief 经验性相位-高程回归校正 Worker
 * 使用 Eigen 最小二乘拟合相位与高程/空间坐标的线性/二次关系，
 * 从干涉图中减去估计的大气延迟分量。
 */
class PhaseElevationRegressionWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit PhaseElevationRegressionWorker(QObject* parent = nullptr);
    ~PhaseElevationRegressionWorker();

public slots:
    /**
     * @brief 执行经验性相位-高程回归校正
     * @param polyOrder        多项式阶数（1=线性, 2=二次）
     * @param windowSize       滑动窗口大小（0=全局回归）
     * @param coherenceThresh  相干性阈值，仅高于此值的像素参与回归
     * @param outputDirectory  节点准备的 staging 输出目录
     */
    void doRegression(int polyOrder, int windowSize, double coherenceThresh,
                      QString outputDirectory,
                      QStringList phaseNames, QStringList phasePaths);

signals:
    void cancelled();
    void outputsGenerated(const QStringList& outputNames, const QStringList& outputPaths,
                          const QList<int>& offsetRows, const QList<int>& offsetCols);
};
