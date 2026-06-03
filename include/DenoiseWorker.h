#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QStandardItemModel>

class DenoiseWorker : public QObject
{
    Q_OBJECT

public:
    explicit DenoiseWorker(QObject* parent = nullptr);
    ~DenoiseWorker();

public slots:
    void Denoise(QList<int> para, double alpha, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);

signals:
    void updateProcess(int value, QString information);
    void endProcess();
    void errorProcess(const QString& errorMsg);
    void sendModel(QStandardItemModel* model);
};
