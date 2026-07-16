#pragma once
#include "BaseWorker.h"

class S1TopsBackGeocodingWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit S1TopsBackGeocodingWorker(QObject* parent = nullptr);
    ~S1TopsBackGeocodingWorker();

    void setDemPath(const QString& path) { m_demPath = path; }
    void setRangeRefine(bool enable) { m_bRangeRefine = enable; }

public slots:
    void S1_TOPS_BackGeocoding(
        int images_number,
        int masterIndex,
        QString savePath,
        QString dstProject,
        QString srcNode,
        QString dstNode,
        QStandardItemModel* model,
        bool b_ESD = true
    );

signals:
    void registrationFinished(
        const QStringList& regisH5Paths,
        const QString& dstNode,
        const QString& dstProject,
        const QString& savePath,
        int masterIndex
    );

private:
    QString m_demPath;
    bool m_bRangeRefine = false;
};
