#ifndef SPECKLEDENOISETASK_H
#define SPECKLEDENOISETASK_H

#include <QObject>
#include <QRunnable>
#include <QStringList>
#include <QMutex>
#include <QStandardItemModel>
#include "FormatConversion.h"

class SpeckleDenoiseTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    SpeckleDenoiseTask(
        QStringList inputPaths,
        QStringList outputPaths,
        QString nodeName,
        QStringList fileNames,
        QString projectPath,
        QString projectName,
        QStandardItemModel* model,
        bool saveToProject,
        XMLFile* projectXml
    );
    
    void stop();
    void run() override;

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);
    void askUserError(QString error_msg, bool* skip);
    void saveImageToProjectRequested(
        QString projectName,
        QString nodeName,
        QString displayName,
        QString finalPath,
        QString tag,
        QString finalFileName
    );

private:
    bool processBM3DEnhancement(
        QString tag,
        QString inputPath,
        QString outputPath,
        QString nodeName,
        QString fileName,
        QString projectPath,
        QString projectName,
        QStandardItemModel* model,
        bool saveToProject,
        XMLFile* projectXml,
        QString& outError,
        int baseProgress,
        int progressStep
    );

    QStringList m_inputPaths;
    QStringList m_outputPaths;
    QString m_nodeName;
    QStringList m_fileNames;
    QString m_projectPath;
    QString m_projectName;
    QStandardItemModel* m_model;
    bool m_saveToProject;
    XMLFile* m_projectXml;
    
    QMutex m_lock;
    bool m_stopFlag;
};

#endif // SPECKLEDENOISETASK_H
