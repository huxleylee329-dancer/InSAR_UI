#pragma once
#include "BaseWorker.h"
#include <vector>
#include "ImportTask.h"

// 导入 Worker 基类，提供批量导入的标准骨架流程
// 子类只需实现 convertToH5() 和 satelliteFormatTag() 即可完成卫星特定的导入逻辑
class BaseImportWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit BaseImportWorker(const QString& satelliteName, QObject* parent = nullptr);
    virtual ~BaseImportWorker();

public slots:
    // 核心骨架方法：执行批量导入的主循环，子类无需重写
    void import_patch(
        const QString& savepath,
        const std::vector<ImportTask>& tasks,
        const QString& dst_node,
        const QString& dst_project,
        QStandardItemModel* model,
        void* contextPtr = nullptr
    );

    // 辅助函数：更新导入进度（子类可在 convertToH5 内部调用，也可在静态回调中使用）
    void updateImportProgress(int percent, const QString& message = QString());

protected:
    // 子类必须实现的数据转换逻辑
    // arguments: 传入的源文件路径列表或参数串
    // outputPath: 输出 H5 文件的完整路径
    // progressMin: 当前任务的进度下界 (0-100)
    // progressMax: 当前任务的进度上界 (0-100)
    // 返回值: true 成功，false 失败
    virtual bool convertToH5(const QStringList& arguments, const QString& outputPath,
                             int progressMin, int progressMax, QString& outErrorMsg) = 0;

    // 子类必须实现的 XML 标签标识（如 "CSG-2", "TSX", "sentinel" 等）
    virtual QString satelliteFormatTag() const = 0;

    // 子类可覆写：指定预览图渲染的数据类型（默认 "complex"）
    // LiDAR 等非复数数据应覆写为 "dem" 等类型
    virtual QString previewDataType() const { return "complex"; }

    // 辅助函数：清理资源并报告错误
    void handleError(const QString& error_msg, const QString& h5_path, const QString& dir_path);

protected:
    QString m_satelliteName;
};
