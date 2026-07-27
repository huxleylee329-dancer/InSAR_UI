#ifndef GENERICSARIMPORTTASK_H
#define GENERICSARIMPORTTASK_H

#include <QObject>
#include <QRunnable>
#include <QString>
#include <QStringList>
#include <vector>

// Task for Single Import
class GenericSARImportTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    GenericSARImportTask(
        QString xml_filename,
        QString project_path,
        QString folder,
        QString filename
    );
    ~GenericSARImportTask() override;

    void run() override;
    void stop();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error);
    void outputsGenerated(const QString& dstNode,
                          const QStringList& outputNames,
                          const QStringList& outputPaths,
                          const QString& dataType,
                          const QString& satelliteFormat);

private:
    QString m_xmlFilename;
    QString m_projectPath;
    QString m_folder;
    QString m_filename;
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
        QString dst_node
    );
    ~GenericSARBatchImportTask() override;

    void run() override;
    void stop();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error);
    void outputsGenerated(const QString& dstNode,
                          const QStringList& outputNames,
                          const QStringList& outputPaths,
                          const QString& dataType,
                          const QString& satelliteFormat);

private:
    QString m_savepath;
    std::vector<QString> m_originalFileList;
    std::vector<QString> m_importNamelist;
    QString m_dstNode;
    bool m_stopFlag = false;
};

#endif // GENERICSARIMPORTTASK_H
