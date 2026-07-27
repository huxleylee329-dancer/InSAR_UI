#pragma once

#include "BaseWorker.h"
#include <QStringList>
#include <QtNetwork/QNetworkAccessManager>

/**
 * @brief GACOS 在线服务 Worker
 * 通过 HTTP API 向 GACOS 服务器提交大气延迟校正请求，
 * 轮询状态后下载结果（GeoTIFF 或二进制 .ztd），转换为 H5 格式。
 */
class GacosOnlineServiceWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit GacosOnlineServiceWorker(QObject* parent = nullptr);
    ~GacosOnlineServiceWorker();

public slots:
    /**
     * @brief 执行 GACOS 大气校正请求
     * @param apiKey         GACOS API Key
     * @param email          注册邮箱
     * @param dataFormat     数据格式（0=GeoTIFF, 1=Binary Grid）
     * @param save_path      工程根目录
     * @param project_name   工程名称
     * @param node_name      输入数据节点名
     * @param file_name      输出数据节点名
     * @param model          项目树模型
     */
    void doGacosRequest(QString apiKey, QString email, int dataFormat,
                        QString save_path, QString project_name,
                        QString file_name, QStringList inputPaths);

signals:
    void cancelled();
    void outputsGenerated(const QString& dstNode, const QStringList& outputNames,
                          const QStringList& outputPaths, const QString& savePath,
                          const QString& projectName);
};
