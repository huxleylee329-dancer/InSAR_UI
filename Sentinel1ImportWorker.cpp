#include "Sentinel1ImportWorker.h"
#include <QMetaType>
#include <QFile>
#include <QDebug>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include "Sentinel1ImportHelper.h"

using namespace std;

Sentinel1ImportWorker::Sentinel1ImportWorker(QObject *parent)
{
	qRegisterMetaType<QList<double>>("QList<double>");
	stop_flag = true;
}

Sentinel1ImportWorker::~Sentinel1ImportWorker()
{
}

void Sentinel1ImportWorker::import_sentinel(
	QString PODFile,
	QString manifest_file,
	QString subswath,
	QString polarization,
	QString project_path,
	QString folder,
	QString filename,
	QString project_name,
	QStandardItemModel* model
)
{
	Sentinel1ImportHelper::importSentinel(
		this,
		PODFile,
		manifest_file,
		subswath,
		polarization,
		project_path,
		folder,
		filename,
		project_name,
		model
	);
}

void Sentinel1ImportWorker::import_sentinel_patch(
	vector<QString> original_filelist, 
	vector<QString> import_namelist, 
	QString subswath, 
	QString polarization,
	QString savepath, 
	QString dst_node,
	QString dst_project,
	QStandardItemModel* model
)
{
	Sentinel1ImportHelper::importSentinelPatch(
		this,
		original_filelist,
		import_namelist,
		subswath,
		polarization,
		savepath,
		dst_node,
		dst_project,
		model
	);
}


void Sentinel1ImportWorker::StopProcess()
{
	QMutexLocker locker(&lock);
	this->stop_flag = false;
}

bool Sentinel1ImportWorker::isStopRequested()
{
	QMutexLocker locker(&lock);
	return !stop_flag;
}
