#ifndef MYTHREAD_H
#define MYTHREAD_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QList>
#include <vector>

class QStandardItemModel;

class MyThread : public QObject
{
    Q_OBJECT

public:
    MyThread(QObject* parent = nullptr);
    ~MyThread();

public slots:
    void import_sentinel(QString PODFile, QString manifest_file, QString subswath, QString polarization, QString project_path, QString folder, QString filename, QString project_name, QStandardItemModel* model);
    void import_sentinel_patch(std::vector<QString> original_namelist, std::vector<QString> import_namelist, QString subswath, QString polarization, QString savepath, QString dst_node, QString dst_project, QStandardItemModel* model);
    void StopProcess();
    bool isStopRequested();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);

private:
    QMutex lock;
    bool stop_flag;
};

#endif // MYTHREAD_H
