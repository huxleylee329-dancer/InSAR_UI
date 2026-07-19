#pragma once

#include "BaseWorker.h"

class DenoiseWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit DenoiseWorker(QObject* parent = nullptr);
    ~DenoiseWorker();

public slots:
    void Denoise(QList<int> para, double alpha, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);

signals:
    void cancelled();
};
