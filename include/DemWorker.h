#pragma once

#include "BaseWorker.h"

class DemWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit DemWorker(QObject* parent = nullptr);
    ~DemWorker();

public slots:
    void Dem(int method, int times, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);

signals:
    void cancelled();
};
