#ifndef S1FRAMEMERGEWORKER_H
#define S1FRAMEMERGEWORKER_H

#include <QObject>
#include <QString>
#include <QStandardItemModel>

class S1FrameMergeWorker : public QObject
{
    Q_OBJECT
public:
    explicit S1FrameMergeWorker(QObject* parent = nullptr);
    ~S1FrameMergeWorker();

public slots:
    void S1_frame_merge(
        int index1, 
        int index2, 
        QString project_name, 
        QString srcNode1, 
        QString srcNode2, 
        QString dstNode, 
        QStandardItemModel* model
    );

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);
    // 回传计算结果，由调用方完成 XML 写入（SOP 避坑经验 #9）
    // savePath+projectName 供旧 Workspace Dialog 使用；Node 侧通过 validateAndRestoreOutput 自行获取路径
    void sendResult(QString dstNode, QString filename, QString savePath, QString projectName);
};

#endif // S1FRAMEMERGEWORKER_H
