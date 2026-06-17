#pragma once
#include <QObject>
#include <QMutex>
#include <QStandardItemModel>

/**
 * @brief 所有计算/导入 Worker 的顶层轻量级基类
 * 统一管理进度信号、并为支持手动中断的 Worker 提供标准化中断接口
 */
class BaseWorker : public QObject
{
    Q_OBJECT

public:
    explicit BaseWorker(QObject* parent = nullptr);
    virtual ~BaseWorker();

signals:
    // 统一的标准进度信号
    void updateProcess(int progress, const QString& message);
    void endProcess();
    void errorProcess(const QString& errorMsg);
    void sendModel(QStandardItemModel* model);

public slots:
    // 统一下沉的中断接口
    virtual void StopProcess();
    virtual bool isStopRequested();

protected:
    QMutex lock;
    bool stop_flag; // 初始为 true（运行中），StopProcess() 触发后设为 false
};
