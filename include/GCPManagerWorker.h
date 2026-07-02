// include/GCPManagerWorker.h
#pragma once
#include "BaseWorker.h"
#include "GCPPoint.h"
#include <vector>
#include <QString>

class GCPManagerWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit GCPManagerWorker(QObject* parent = nullptr);
    ~GCPManagerWorker();

signals:
    // 计算完成后的通知信号，传回计算后的控制点列表和报告文本，安全解耦 SQLite
    void evaluationFinished(const std::vector<GCPPoint>& updatedGcps, const GCPEvaluationResult& result, const QString& reportText);

public slots:
    // 执行异步评估
    void evaluate_gcps(
        const QString& projectPath,
        const QString& projectName,
        const QString& inputH5Path,     // 输入影像/干涉图 H5 绝对路径
        const QString& outputH5Path,    // 节点专用输出 H5 绝对路径
        const std::vector<GCPPoint>& gcps,
        double thresholdSigma,
        int minQuality
    );
};
