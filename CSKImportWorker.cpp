#include "CSKImportWorker.h"
#include <FormatConversion.h>

CSKImportWorker::CSKImportWorker(QObject* parent)
    : BaseImportWorker("CSK", parent)
{
}

CSKImportWorker::~CSKImportWorker()
{
}

bool CSKImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                 int progressMin, int progressMax, QString& outErrorMsg)
{
    if (arguments.isEmpty()) return false;

    QString csk_filepath = arguments[0];

    CSK_reader csk_reader(csk_filepath.toStdString().c_str());
    int ret = csk_reader.init();
    if (ret >= 0)
    {
        ret = csk_reader.write_to_h5(outputPath.toStdString().c_str());
    }

    return ret >= 0;
}
