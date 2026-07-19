#pragma once
#include "BaseWorker.h"
#include <QStringList>
#include <atomic>

class PSNetworkWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit PSNetworkWorker(QObject* parent = nullptr);
    ~PSNetworkWorker();
    void StopProcess() override;
    bool cancellationRequested() const noexcept;

public slots:
    void build_network(
        double max_edge_length,
        int ref_row,
        int ref_col,
        QString projectPath,
        QString projectName,
        QString dstNode,
        QString candidatesH5,
        QStringList slcFilePaths
    );

signals:
    void cancelled();

private:
    std::atomic_bool m_cancelRequested{false};
};
