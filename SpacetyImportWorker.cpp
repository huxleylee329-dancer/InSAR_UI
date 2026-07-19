#include "SpacetyImportWorker.h"
#include <FormatConversion.h>

SpacetyImportWorker::SpacetyImportWorker(QObject* parent)
    : BaseImportWorker("Spacety", parent)
{
}

SpacetyImportWorker::~SpacetyImportWorker()
{
}

bool SpacetyImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                     int progressMin, int progressMax, QString& outErrorMsg)
{
    if (arguments.size() < 3) return false;

    QString data_file = arguments[0];
    QString xml_file = arguments[1];
    bool spotlight_mode = (arguments[2].toInt() != 0);

    Spacety_reader spacety_reader(data_file.toStdString().c_str(), xml_file.toStdString().c_str());
    int ret;
    if (spotlight_mode) {
        ret = spacety_reader.init_test();
    } else {
        ret = spacety_reader.init();
    }

    if (ret >= 0)
    {
        ret = spacety_reader.write_to_h5(outputPath.toStdString().c_str());
    }

    return ret >= 0;
}
