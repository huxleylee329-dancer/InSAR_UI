#include "OrbitSourceWorker.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include <QAuthenticator>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QSettings>
#include <QRegularExpression>
#include <iostream>

OrbitSourceWorker::OrbitSourceWorker(QObject* parent)
    : BaseWorker(parent)
{
}

OrbitSourceWorker::~OrbitSourceWorker()
{
}

QString OrbitSourceWorker::getOriginalGranuleName(const QString& h5FilePath, const QString& projectDir)
{
    QString granuleName;
    std::string sourceFileStr;
    bool readOk = false;

    // 优先读取 source_1 元数据
    {
        NodeUtils::Hdf5Locker locker(h5FilePath);
        readOk = NodeUtils::readStringFromH5(h5FilePath, "source_1", sourceFileStr);
    }

    if (readOk && !sourceFileStr.empty())
    {
        QString rawPath = QString::fromStdString(sourceFileStr);
        QFileInfo fi(rawPath);
        granuleName = fi.completeBaseName(); // 得到 S1A_IW_SLC...
    }

    // 兜底读取 comment 属性
    if (granuleName.isEmpty())
    {
        std::string commentStr;
        bool commentOk = false;
        {
            NodeUtils::Hdf5Locker locker(h5FilePath);
            commentOk = NodeUtils::readStringFromH5(h5FilePath, "comment", commentStr);
        }
        if (commentOk && !commentStr.empty())
        {
            QString rawComment = QString::fromStdString(commentStr);
            QRegularExpression re("(S1[AB]_IW_SLC__[^\\s]+)");
            QRegularExpressionMatch match = re.match(rawComment);
            if (match.hasMatch())
            {
                granuleName = match.captured(1);
            }
        }
    }

    // 如果仍然为空，使用 H5 文件名本身（例如 20251216_iw1vv_regis -> 提取时间段做猜测）
    if (granuleName.isEmpty())
    {
        QFileInfo fi(h5FilePath);
        granuleName = fi.completeBaseName();
    }

    return granuleName;
}

int OrbitSourceWorker::downloadFile(const QString& url, const QString& savePath)
{
    QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(url)));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    // 从配置读取 Earthdata 凭据，NASA ASF 下载需要认证
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString encryptedUser = settings.value("DEM/EarthdataUser", "").toString();
    QString encryptedPass = settings.value("DEM/EarthdataPassword", "").toString();

    QString username = QString::fromUtf8(QByteArray::fromBase64(encryptedUser.toUtf8()));
    QString password = QString::fromUtf8(QByteArray::fromBase64(encryptedPass.toUtf8()));

    if (!username.isEmpty() && !password.isEmpty())
    {
        QByteArray authHeader = "Basic " + QByteArray(QString("%1:%2").arg(username).arg(password).toUtf8()).toBase64();
        request.setRawHeader("Authorization", authHeader);

        connect(&manager, &QNetworkAccessManager::authenticationRequired,
                this, [username, password](QNetworkReply*, QAuthenticator* authenticator) {
                    authenticator->setUser(username);
                    authenticator->setPassword(password);
                });
    }

    QString tempPath = savePath + ".part";
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly))
    {
        InSARLogManager::LogError("OrbitSourceWorker", QString("无法打开临时文件写入：%1").arg(tempPath));
        return -1;
    }

    QNetworkReply* reply = manager.get(request);

    connect(reply, &QNetworkReply::readyRead, this, [&]() {
        tempFile.write(reply->readAll());
    });

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(); // 阻塞等待下载完毕

    tempFile.close();

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError err = reply->error();

    if (err == QNetworkReply::NoError && (statusCode == 200 || statusCode == 302))
    {
        if (QFile::exists(savePath))
        {
            QFile::remove(savePath);
        }
        tempFile.rename(savePath);
        reply->deleteLater();
        return 1;
    }
    else
    {
        tempFile.remove();
        reply->deleteLater();
        if (statusCode == 404 || err == QNetworkReply::ContentNotFoundError)
        {
            return 0;
        }
        return -1;
    }
}

void OrbitSourceWorker::fetch_orbits(
    QString projectPath,
    QString projectName,
    QStringList filePaths,
    int orbitSource,
    QString cacheDir,
    QStandardItemModel* model
)
{
    stop_flag = false;
    emit updateProcess(0, QStringLiteral("开始检索精密轨道数据……"));

    QString projectDir = projectPath;
    if (projectPath.endsWith(".insar", Qt::CaseInsensitive))
    {
        projectDir = QFileInfo(projectPath).absolutePath();
    }

    QDir().mkpath(cacheDir);

    int total = filePaths.size();
    int successCount = 0;

    for (int i = 0; i < total; ++i)
    {
        if (isStopRequested())
        {
            emit errorProcess(QStringLiteral("用户中止了下载。"));
            return;
        }

        QString h5File = filePaths.at(i);
        QString granule = getOriginalGranuleName(h5File, projectDir);

        emit updateProcess(10 + i * 80 / total, QStringLiteral("正在解析影像 [%1] 的成像时间……").arg(QFileInfo(h5File).fileName()));

        // 解析卫星平台 (S1A 或 S1B) 与成像日期
        QString platform = "S1A";
        QString dateStr; // YYYYMMDD
        
        if (granule.contains("S1A", Qt::CaseInsensitive)) platform = "S1A";
        else if (granule.contains("S1B", Qt::CaseInsensitive)) platform = "S1B";

        // 从 granule 名字正则匹配日期：如 S1A_IW_..._20251204T015841_
        QRegularExpression dateRe("(\\d{8})T\\d{6}");
        QRegularExpressionMatch dateMatch = dateRe.match(granule);
        if (dateMatch.hasMatch())
        {
            dateStr = dateMatch.captured(1);
        }
        else
        {
            // 如果无法从 granule 提取，尝试从 H5 的数据集中读出成像日期（比如 utc_time 矩阵或者 orbital 元数据）
            // 这里我们采用保守的猜测，或者继续匹配 H5 文件名中的数字
            QRegularExpression dateFallbackRe("(\\d{8})");
            QRegularExpressionMatch dateFallbackMatch = dateFallbackRe.match(QFileInfo(h5File).fileName());
            if (dateFallbackMatch.hasMatch())
            {
                dateStr = dateFallbackMatch.captured(1);
            }
        }

        if (dateStr.isEmpty())
        {
            InSARLogManager::LogWarning("OrbitSourceWorker", QString("无法确定影像 [%1] 的成像日期，跳过此文件。").arg(h5File));
            continue;
        }

        // 计算精轨生效时间区间：前一天 到 后一天
        QDate centerDate = QDate::fromString(dateStr, "yyyyMMdd");
        QDate prevDate = centerDate.addDays(-1);
        QDate nextDate = centerDate.addDays(1);

        QString prevDateStr = prevDate.toString("yyyy-MM-dd");
        QString nextDateStr = nextDate.toString("yyyy-MM-dd");

        QString searchUrl;
        if (orbitSource == 0) // NASA ASF
        {
            // 通过 ASF API 查找该天的 AUX_POEORB 辅助数据
            searchUrl = QString("https://api.daac.asf.alaska.edu/services/search/param?platform=S1&processingLevel=AUX_POEORB&start=%1T20:00:00Z&end=%2T04:00:00Z&output=json")
                            .arg(prevDateStr)
                            .arg(nextDateStr);
        }
        else // ESA CDSE
        {
            // 通过 CDSE OData 查找
            searchUrl = QString("https://catalogue.dataspace.copernicus.eu/odata/v1/Products?$filter=contains(Name,'AUX_POEORB') and contains(Name,'V%1') and contains(Name,'%2')&$format=json")
                            .arg(prevDate.toString("yyyyMMdd"))
                            .arg(platform);
        }

        // 发起 API 查询
        QNetworkAccessManager queryManager;
        QNetworkRequest queryRequest((QUrl(searchUrl)));
        QNetworkReply* queryReply = queryManager.get(queryRequest);

        QEventLoop loop;
        connect(queryReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        bool foundEof = false;
        QString downloadUrl;
        QString eofFileName;

        if (queryReply->error() == QNetworkReply::NoError)
        {
            QByteArray responseData = queryReply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            
            if (orbitSource == 0) // ASF JSON 结构
            {
                QJsonArray results = doc.array();
                if (results.isEmpty() && doc.isObject()) {
                    results = doc.object().value("results").toArray();
                }
                
                // 遍历搜索出来的所有 POEORB 文件，进行精确匹配
                for (int r = 0; r < results.size(); ++r)
                {
                    QJsonObject item = results.at(r).toObject();
                    QString fileName = item.value("fileName").toString();
                    
                    // 必须满足：同一个平台(S1A/S1B)，且覆盖了该影像的成像时间范围 (前一天 21:59 到 后一天 00:01)
                    QString targetValidity = QString("V%1T215942_%2T000142")
                                                .arg(prevDate.toString("yyyyMMdd"))
                                                .arg(nextDate.toString("yyyyMMdd"));
                                                
                    if (fileName.contains(platform, Qt::CaseInsensitive) && fileName.contains(targetValidity, Qt::CaseInsensitive))
                    {
                        downloadUrl = item.value("downloadUrl").toString();
                        eofFileName = fileName;
                        foundEof = true;
                        break;
                    }
                }
            }
            else // CDSE OData JSON 结构
            {
                QJsonObject obj = doc.object();
                QJsonArray valueArr = obj.value("value").toArray();
                for (int r = 0; r < valueArr.size(); ++r)
                {
                    QJsonObject item = valueArr.at(r).toObject();
                    QString name = item.value("Name").toString();
                    QString id = item.value("Id").toString();
                    
                    QString targetValidity = QString("V%1T215942_%2T000142")
                                                .arg(prevDate.toString("yyyyMMdd"))
                                                .arg(nextDate.toString("yyyyMMdd"));
                                                
                    if (name.contains(platform, Qt::CaseInsensitive) && name.contains(targetValidity, Qt::CaseInsensitive))
                    {
                        downloadUrl = QString("https://zipper.dataspace.copernicus.eu/odata/v1/Products(%1)/$value").arg(id);
                        eofFileName = name;
                        foundEof = true;
                        break;
                    }
                }
            }
        }
        queryReply->deleteLater();

        // 如果没有找到 POEORB (精密轨道)，尝试寻找 RESORB (重构轨道，发布速度更快，精度在10cm内)
        if (!foundEof)
        {
            InSARLogManager::LogInfo("OrbitSourceWorker", QString("未检索到 %1 在 %2 日期的精密轨道(POEORB)，尝试搜索重构轨道(RESORB)...").arg(platform).arg(dateStr));
            
            QString resorbUrl;
            if (orbitSource == 0)
            {
                resorbUrl = QString("https://api.daac.asf.alaska.edu/services/search/param?platform=S1&processingLevel=AUX_RESORB&start=%1T00:00:00Z&end=%2T23:59:59Z&output=json")
                                .arg(centerDate.toString("yyyy-MM-dd"))
                                .arg(centerDate.toString("yyyy-MM-dd"));
            }
            else
            {
                resorbUrl = QString("https://catalogue.dataspace.copernicus.eu/odata/v1/Products?$filter=contains(Name,'AUX_RESORB') and contains(Name,'V%1') and contains(Name,'%2')&$format=json")
                                .arg(dateStr)
                                .arg(platform);
            }

            QNetworkReply* resReply = queryManager.get(QNetworkRequest(QUrl(resorbUrl)));
            QEventLoop resLoop;
            connect(resReply, &QNetworkReply::finished, &resLoop, &QEventLoop::quit);
            resLoop.exec();

            if (resReply->error() == QNetworkReply::NoError)
            {
                QByteArray resData = resReply->readAll();
                QJsonDocument resDoc = QJsonDocument::fromJson(resData);
                if (orbitSource == 0)
                {
                    QJsonArray resResults = resDoc.array();
                    if (resResults.isEmpty() && resDoc.isObject()) {
                        resResults = resDoc.object().value("results").toArray();
                    }
                    
                    // 重构轨道可能有多个覆盖不同时刻的文件，挑选包含成像时间的那个
                    for (int r = 0; r < resResults.size(); ++r)
                    {
                        QJsonObject item = resResults.at(r).toObject();
                        QString fileName = item.value("fileName").toString();
                        
                        // 提取重构轨道的 V 覆盖区间，看是否包含成像时分秒
                        // 示例：S1A_OPER_AUX_RESORB_OPOD_20251204T032011_V20251204T015841_20251204T053341.EOF
                        QRegularExpression validityRe("V(\\d{8}T\\d{6})_(\\d{8}T\\d{6})");
                        QRegularExpressionMatch valMatch = validityRe.match(fileName);
                        if (valMatch.hasMatch() && fileName.contains(platform, Qt::CaseInsensitive))
                        {
                            QDateTime startVal = QDateTime::fromString(valMatch.captured(1), "yyyyMMddTHHmmss");
                            QDateTime endVal = QDateTime::fromString(valMatch.captured(2), "yyyyMMddTHHmmss");
                            
                            // 从 granule 中解析成像时刻
                            QRegularExpression timeRe("\\d{8}T(\\d{6})");
                            QRegularExpressionMatch timeMatch = timeRe.match(granule);
                            if (timeMatch.hasMatch())
                            {
                                QDateTime imgTime = QDateTime::fromString(dateStr + "T" + timeMatch.captured(1), "yyyyMMddTHHmmss");
                                if (imgTime >= startVal && imgTime <= endVal)
                                {
                                    downloadUrl = item.value("downloadUrl").toString();
                                    eofFileName = fileName;
                                    foundEof = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            resReply->deleteLater();
        }

        if (foundEof && !downloadUrl.isEmpty())
        {
            emit updateProcess(10 + i * 80 / total, QStringLiteral("正在下载轨道文件 %1...").arg(eofFileName));
            QString finalSavePath = cacheDir + "/" + eofFileName;

            int dlResult = downloadFile(downloadUrl, finalSavePath);
            if (dlResult == 1)
            {
                successCount++;
                InSARLogManager::LogInfo("OrbitSourceWorker", QString("成功下载并缓存轨道文件：%1").arg(eofFileName));
            }
            else
            {
                InSARLogManager::LogError("OrbitSourceWorker", QString("轨道文件下载失败：%1").arg(eofFileName));
            }
        }
        else
        {
            InSARLogManager::LogWarning("OrbitSourceWorker", QString("未在服务器上检索到影像 [%1] 的任何有效精轨/重构轨数据。").arg(QFileInfo(h5File).fileName()));
        }
    }

    emit updateProcess(100, QStringLiteral("精密轨道数据下载完成！成功匹配数：%1/%2").arg(successCount).arg(total));
    emit endProcess();
}
