#ifndef EXPORTKMLWORKER_H
#define EXPORTKMLWORKER_H

#include <QObject>
#include <QString>

class ExportKMLWorker : public QObject
{
    Q_OBJECT

public:
    explicit ExportKMLWorker(QObject* parent = nullptr);
    ~ExportKMLWorker();

public slots:
    void exportKML(QString h5Path, QString outFolder, QString fileName);

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);

private:
    void paintColorbar(double mMin, double mMax, QString save_path);
};

#endif // EXPORTKMLWORKER_H
