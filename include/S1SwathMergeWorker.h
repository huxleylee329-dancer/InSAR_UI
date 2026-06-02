#ifndef S1SWATHMERGEWORKER_H
#define S1SWATHMERGEWORKER_H

#include <QObject>
#include <QString>
#include <QStandardItemModel>

class S1SwathMergeWorker : public QObject
{
    Q_OBJECT
public:
    explicit S1SwathMergeWorker(QObject* parent = nullptr);
    ~S1SwathMergeWorker();

public slots:
    void S1_swath_merge(
        int index1, 
        int index2, 
        int index3, 
        QString project_name, 
        QString srcNode1, 
        QString srcNode2, 
        QString srcNode3, 
        QString dstNode, 
        QStandardItemModel* model
    );

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);
    // 回传计算结果，由调用方完成 XML 写入以满足 SOP 规范 9
    void sendResult(QString dstNode, QString filename, QString savePath, QString projectName);
};

#endif // S1SWATHMERGEWORKER_H
