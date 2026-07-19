#pragma once

#include "BaseWorker.h"

class InterferometricFormationWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit InterferometricFormationWorker(QObject* parent = nullptr);
    ~InterferometricFormationWorker();

public slots:
    void Interferometric(bool isdeflat, bool istopo_removal, bool iscoherence,
                         int master_index, int win_width, int win_height,
                         int multilook_rg, int multilook_az, QString save_path,
                         QString project_name, QString node_name, QString file_name,
                         QStandardItemModel* model);

    void InterferometricWithDem(bool isdeflat, bool istopo_removal, bool iscoherence,
                                int master_index, int win_width, int win_height,
                                int multilook_rg, int multilook_az, QString save_path,
                                QString project_name, QString node_name, QString file_name,
                         QStandardItemModel* model, QString dem_path);

signals:
    void cancelled();
};
