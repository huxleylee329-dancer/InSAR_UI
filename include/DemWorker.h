#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QStandardItemModel>

class DemWorker : public QObject
{
    Q_OBJECT

public:
    explicit DemWorker(QObject* parent = nullptr);
    ~DemWorker();

public slots:
    void Dem(int method, int times, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);

signals:
    void updateProcess(int value, QString information);
    void endProcess();
    void errorProcess(const QString& errorMsg);
    void sendModel(QStandardItemModel* model);
};
