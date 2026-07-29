#pragma once

#include "BaseWorker.h"

class CutWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit CutWorker(QObject* parent = nullptr);
    ~CutWorker();

public slots:
    /**
     * @brief Coordinate-based AOI crop
     */
    void Cut(QList<double> para,
             QString save_path,
             QString project_name,
             QString src_node,
             QString dst_node,
             QStringList inputPaths,
             QString src_data_rank,
             bool outputDirectoryIsStaging = false);

    /**
     * @brief Normalized ratio-based AOI crop (used by box-selection and auto-center)
     */
    void Cut2(double h5_left,
              double h5_right,
              double h5_top,
              double h5_bottom,
              QString save_path,
              QString project_name,
              QString src_node,
              QString dst_node,
              QStringList inputPaths,
              QString src_data_rank,
              int master_index,
              bool outputDirectoryIsStaging = false);

signals:
    void cancelled();
    void fileCropped(QString cutName, QString fullPath, int offsetRow, int offsetCol, int masterIndex, QString srcDataRank, QList<double> para);
    void outputsGenerated(const QStringList& outputPaths);
};
