#pragma once

#include "BaseWorker.h"
#include "InSARLogManager.h"
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

    void setTaskLogContext(const TaskLogContext& context) { m_taskLogContext = context; }
    void setDemValidMaskPath(const QString& path) { m_demValidMaskPath = path; }
    void setDemRequiredBounds(double minLon, double maxLon, double minLat, double maxLat)
    {
        m_demRequiredMinLon = minLon;
        m_demRequiredMaxLon = maxLon;
        m_demRequiredMinLat = minLat;
        m_demRequiredMaxLat = maxLat;
        m_hasDemRequiredBounds = true;
    }

public slots:
    void Interferometric(bool isdeflat, bool istopo_removal, bool iscoherence,
                         int master_index, int win_width, int win_height,
                         int multilook_rg, int multilook_az, QString save_path,
                         QString file_name, QStringList input_paths,
                         bool outputDirectoryIsStaging = false);

    void InterferometricWithDem(bool isdeflat, bool istopo_removal, bool iscoherence,
                                int master_index, int win_width, int win_height,
                                int multilook_rg, int multilook_az, QString save_path,
                                QString file_name, QStringList input_paths,
                                QString dem_path,
                                bool outputDirectoryIsStaging = false);

signals:
    void interferogramGenerated(const InterferogramFileResult& result);
    void cancelled();

private:
    TaskLogContext m_taskLogContext;
    QString m_demValidMaskPath;
    double m_demRequiredMinLon = 0.0;
    double m_demRequiredMaxLon = 0.0;
    double m_demRequiredMinLat = 0.0;
    double m_demRequiredMaxLat = 0.0;
    bool m_hasDemRequiredBounds = false;
};
