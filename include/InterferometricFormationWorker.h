#pragma once

#include "BaseWorker.h"
#include <QMetaType>
#include <QStringList>

struct InterferogramFileResult {
    QString phaseName;
    QString cohName;
    QString h5Path;
    QString relativePath;
    QString masterName;
    int offsetRow = 0;
    int offsetCol = 0;
    bool isDeflat = false;
    bool isTopoRemoval = false;
    bool isCoherence = false;
    int winWidth = 0;
    int winHeight = 0;
    int multilookRg = 0;
    int multilookAz = 0;
};
Q_DECLARE_METATYPE(InterferogramFileResult)

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
                         QString file_name, QStringList input_paths);

    void InterferometricWithDem(bool isdeflat, bool istopo_removal, bool iscoherence,
                                int master_index, int win_width, int win_height,
                                int multilook_rg, int multilook_az, QString save_path,
                                QString file_name, QStringList input_paths,
                                QString dem_path);

signals:
    void interferogramGenerated(const InterferogramFileResult& result);
    void cancelled();
};
