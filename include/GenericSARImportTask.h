#ifndef GENERICSARIMPORTTASK_H
#define GENERICSARIMPORTTASK_H

#include <QObject>
#include <QRunnable>
#include <QString>
#include <QStringList>
#include <QStandardItemModel>
#include <vector>
#include "FormatConversion.h"

// Task for Single Import
class GenericSARImportTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    GenericSARImportTask(
        QString xml_filename,
        QString project_path,
        QString folder,
        QString filename,
        QString project_name,
        QStandardItemModel* model
    );
    ~GenericSARImportTask() override;

    void run() override;
    void stop();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error);
    void sendModel(QStandardItemModel* model);

private:
    QString m_xmlFilename;
    QString m_projectPath;
    QString m_folder;
    QString m_filename;
    QString m_projectName;
    QStandardItemModel* m_model;
    
    bool m_stopFlag = false;
};

// Task for Batch Import
class GenericSARBatchImportTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    GenericSARBatchImportTask(
        QString savepath,
        std::vector<QString> original_file_list,
        std::vector<QString> import_namelist,
        QString dst_node,
        QString dst_project,
        QStandardItemModel* model
    );
    ~GenericSARBatchImportTask() override;

    void run() override;
    void stop();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error);
    void sendModel(QStandardItemModel* model);

private:
    QString m_savepath;
    std::vector<QString> m_originalFileList;
    std::vector<QString> m_importNamelist;
    QString m_dstNode;
    QString m_dstProject;
    QStandardItemModel* m_model;
    
    bool m_stopFlag = false;
};

#endif // GENERICSARIMPORTTASK_H
