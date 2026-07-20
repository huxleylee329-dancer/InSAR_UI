#pragma once

#include "BaseWorker.h"
#include <QStandardItemModel>

/**
 * @brief ERA5 对流层校正 Worker
 * 使用 GDAL 读取 ERA5 NetCDF (.nc) 气象场数据，
 * 通过 3D 大气折射率剖面数值积分计算天顶对流层延迟。
 */
class TroposphericCorrectionWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit TroposphericCorrectionWorker(QObject* parent = nullptr);
    ~TroposphericCorrectionWorker();

public slots:
    void doCorrection(QString era5Dir, QString save_path, QString project_name,
                      QString node_name, QString file_name,
                      QStandardItemModel* model);

signals:
    void cancelled();
};
