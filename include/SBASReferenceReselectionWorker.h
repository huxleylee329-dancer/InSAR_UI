#ifndef SBASREFERENCERESELECTIONWORKER_H
#define SBASREFERENCERESELECTIONWORKER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QPoint>

class SBASReferenceReselectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit SBASReferenceReselectionWorker(QObject* parent = nullptr);
    ~SBASReferenceReselectionWorker();

public slots:
    void SBAS_reference_reselection(QString save_path, QString srcNode, QString times_series_h5,
                                    int ref_row, int ref_col, QList<QPoint> GCPs);

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
};

#endif // SBASREFERENCERESELECTIONWORKER_H
