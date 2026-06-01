#pragma once
#include <QObject>
#include <QString>
#include <QStandardItemModel>

class S1DeburstWorker : public QObject
{
    Q_OBJECT
public:
    explicit S1DeburstWorker(QObject* parent = nullptr);
    ~S1DeburstWorker();

public slots:
    void S1_Deburst(
        QString savePath, 
        QString dstProject, 
        QString srcNode, 
        QString dstNode, 
        QStandardItemModel* model
    );

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);
};
