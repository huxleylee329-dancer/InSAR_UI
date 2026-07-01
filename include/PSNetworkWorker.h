#pragma once
#include "BaseWorker.h"
#include <QStringList>

class PSNetworkWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit PSNetworkWorker(QObject* parent = nullptr);
    ~PSNetworkWorker();

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
};
