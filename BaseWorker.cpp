#include "BaseWorker.h"
#include <QMutexLocker>

BaseWorker::BaseWorker(QObject* parent)
    : QObject(parent)
    , stop_flag(true)
{
}

BaseWorker::~BaseWorker()
{
}

void BaseWorker::StopProcess()
{
    QMutexLocker locker(&lock);
    this->stop_flag = false;
}

bool BaseWorker::isStopRequested()
{
    QMutexLocker locker(&lock);
    return !stop_flag;
}
