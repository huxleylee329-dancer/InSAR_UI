#ifndef GENERICSARIMPORTTASK_H
#define GENERICSARIMPORTTASK_H

#include <QObject>
#include <QRunnable>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <vector>

bool isSupportedGenericSarRasterPath(const QString& path);

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
        std::shared_ptr<std::atomic_bool> cancellationToken
    );
    ~GenericSARImportTask() override;

    void run() override;
    void stop();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error);
    void cancelled();
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
    std::shared_ptr<std::atomic_bool> m_cancellationToken;
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
        std::shared_ptr<std::atomic_bool> cancellationToken
    );
    ~GenericSARBatchImportTask() override;

    void run() override;
    void stop();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error);
    void cancelled();
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
    std::shared_ptr<std::atomic_bool> m_cancellationToken;
};

#endif // GENERICSARIMPORTTASK_H
