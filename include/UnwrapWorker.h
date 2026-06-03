#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QStandardItemModel>

class UnwrapWorker : public QObject
{
    Q_OBJECT

public:
    explicit UnwrapWorker(QObject* parent = nullptr);
    ~UnwrapWorker();

public slots:
    void Unwrap(int method, double coherence_threshold, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);

signals:
    void updateProcess(int value, QString information);
    void endProcess();
    void errorProcess(const QString& errorMsg);
    void sendModel(QStandardItemModel* model);
};
