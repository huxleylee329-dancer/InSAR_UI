#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
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
    // 回传生成的 H5 路径列表和 origin 名称列表，由 Node 端用原生 TinyXML 写入 XML（SOP 避坑经验 #9）
    void sendResults(QString dstNode, QStringList deburstH5Paths, QStringList originNames);
};

