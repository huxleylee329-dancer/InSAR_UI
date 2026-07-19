#pragma once

#include "BaseWorker.h"

class UnwrapWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit UnwrapWorker(QObject* parent = nullptr);
    ~UnwrapWorker();

public slots:
    void Unwrap(int method, double coherence_threshold, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);

signals:
    void cancelled();
};
