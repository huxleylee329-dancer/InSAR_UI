#pragma once
#ifndef ORBIT_SOURCE_WORKER_H
#define ORBIT_SOURCE_WORKER_H

#include "BaseWorker.h"
#include <QStringList>
#include <QStandardItemModel>

class OrbitSourceWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit OrbitSourceWorker(QObject* parent = nullptr);
    ~OrbitSourceWorker();

public slots:
    /**
     * @brief 执行精密轨道文件的联网自动下载与匹配
     * @param projectPath   工程路径（.insar 文件路径）
     * @param projectName   工程名称
     * @param filePaths     参考的 SLC H5 绝对文件路径列表
     * @param orbitSource   精轨数据源索引 (0: NASA ASF, 1: ESA CDSE)
     * @param cacheDir      EOF 缓存目录绝对路径
     */
    void fetch_orbits(
        QString projectPath,
        QString projectName,
        QStringList filePaths,
        int orbitSource,
        QString cacheDir,
        QStandardItemModel* model
    );

private:
    /**
     * @brief 下载单个轨道文件
     * @param url       下载的 HTTP 链接
     * @param savePath  保存路径
     * @return 成功返回 1，404返回 0，失败返回 -1
     */
    int downloadFile(const QString& url, const QString& savePath);

    /**
     * @brief 从本地 H5 文件提取原始影像的文件名 (如 S1A_IW_SLC...)
     */
    QString getOriginalGranuleName(const QString& h5FilePath, const QString& projectDir);
};

#endif // ORBIT_SOURCE_WORKER_H
