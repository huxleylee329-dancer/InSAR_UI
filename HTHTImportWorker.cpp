#include "HTHTImportWorker.h"
#include <FormatConversion.h>

HTHTImportWorker::HTHTImportWorker(QObject* parent)
    : BaseImportWorker("HTHT", parent)
{
}

HTHTImportWorker::~HTHTImportWorker()
{
}

bool HTHTImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                  int progressMin, int progressMax)
{
    if (arguments.size() < 3) return false;

    QString data_file = arguments[0];
    QString xml_file = arguments[1];
    int mode = arguments[2].toInt();

    HTHT_reader reader(data_file.toStdString().c_str(), xml_file.toStdString().c_str(), mode);
    int ret = reader.init();
    if (ret >= 0)
    {
        ret = reader.write_to_h5(outputPath.toStdString().c_str());
    }

    return ret >= 0;
}
