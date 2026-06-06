#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QStandardItemModel>
#include <QStringList>

class SLCDerampWorker : public QObject
{
    Q_OBJECT

public:
    explicit SLCDerampWorker(QObject* parent = nullptr);
    ~SLCDerampWorker();

public slots:
    void SLC_deramp(
        int masterIndex,
        QString project_name,
        QString src_node,
        QString dst_node,
        QStandardItemModel* model
    );

signals:
    void updateProcess(int value, QString information);
    void endProcess();
    void errorProcess(const QString& errorMsg);
    void sendModel(QStandardItemModel* model);
    void sendResults(const QString& dstNode, const QStringList& h5Paths, const QStringList& originNames);
};
