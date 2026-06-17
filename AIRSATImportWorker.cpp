#include "AIRSATImportWorker.h"
#include <FormatConversion.h>

AIRSATImportWorker::AIRSATImportWorker(QObject* parent)
    : BaseImportWorker("AIRSAT", parent)
{
}

AIRSATImportWorker::~AIRSATImportWorker()
{
}

bool AIRSATImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                    int progressMin, int progressMax)
{
    if (arguments.size() < 2) return false;

    QString data_file = arguments[0];
    QString xml_file  = arguments[1];

    AIRSAT_reader airsat_reader(data_file.toStdString().c_str(), xml_file.toStdString().c_str());
    int ret = airsat_reader.init();
    if (ret >= 0)
    {
        ret = airsat_reader.write_to_h5(outputPath.toStdString().c_str());
    }

    return ret >= 0;
}
