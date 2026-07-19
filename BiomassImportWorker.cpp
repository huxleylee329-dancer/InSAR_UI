#include "BiomassImportWorker.h"
#include <FormatConversion.h>

BiomassImportWorker::BiomassImportWorker(QObject* parent)
    : BaseImportWorker("Biomass", parent)
{
}

BiomassImportWorker::~BiomassImportWorker()
{
}

bool BiomassImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                     int progressMin, int progressMax, QString& outErrorMsg)
{
    if (arguments.size() < 5) return false;

    QString amp_file = arguments[0];
    QString phase_file = arguments[1];
    QString xml_file = arguments[2];
    QString orbit_file = arguments[3];
    QString polarization = arguments[4];

    Biomass1A_reader biomass_reader(
        amp_file.toStdString().c_str(),
        phase_file.toStdString().c_str(),
        xml_file.toStdString().c_str(),
        orbit_file.toStdString().c_str(),
        polarization.toStdString().c_str()
    );

    int ret = biomass_reader.init();
    if (ret >= 0)
    {
        ret = biomass_reader.write_to_h5(outputPath.toStdString().c_str());
    }

    return ret >= 0;
}
