#ifndef SENTINEL1IMPORTHELPER_H
#define SENTINEL1IMPORTHELPER_H

#include <QString>
#include <QStandardItemModel>
#include <vector>

class MyThread;

namespace Sentinel1ImportHelper {

void importSentinel(
    MyThread* worker,
    QString PODFile,
    QString manifest_file,
    QString subswath,
    QString polarization,
    QString project_path,
    QString folder,
    QString filename,
    QString project_name,
    QStandardItemModel* model
);

void importSentinelPatch(
    MyThread* worker,
    std::vector<QString> original_filelist,
    std::vector<QString> import_namelist,
    QString subswath,
    QString polarization,
    QString savepath,
    QString dst_node,
    QString dst_project,
    QStandardItemModel* model
);

} // namespace Sentinel1ImportHelper

#endif // SENTINEL1IMPORTHELPER_H
