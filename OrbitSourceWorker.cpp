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
        if (fi.fileName().toLower() == "manifest.safe")
        {
            granuleName = fi.dir().dirName(); // 获取 .SAFE 目录名，如 S1A_IW_SLC__...
        }
        else
        {
            granuleName = fi.completeBaseName();
        }
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

    // 检测 HTML 重定向（鉴权失效时 NASA 返回 URS 登录页）
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    if (!contentType.isEmpty() && contentType.contains("html", Qt::CaseInsensitive))
    {
        qDebug() << "[OrbitWorker] Auth failed: got HTML instead of EOF, clearing credentials";
        tempFile.remove();
        reply->deleteLater();
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        settings.remove("DEM/EarthdataUser");
        settings.remove("DEM/EarthdataPassword");
        return -1;
    }

    if (err == QNetworkReply::NoError && (statusCode == 200 || statusCode == 302))
    {
        qint64 expectedSize = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        qint64 actualSize = tempFile.size();
        qDebug() << "[OrbitWorker] download:" << savePath
                 << "expectedSize:" << expectedSize << "actualSize:" << actualSize
                 << "contentType:" << contentType;
        if (expectedSize > 0 && actualSize != expectedSize)
        {
            qDebug() << "[OrbitWorker] Size mismatch!";
            tempFile.remove();
            reply->deleteLater();
            return -1;
        }

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
    qDebug() << "[OrbitWorker] fetch_orbits() called";
    qDebug() << "[OrbitWorker]   projectPath:" << projectPath;
    qDebug() << "[OrbitWorker]   cacheDir:" << cacheDir;
    qDebug() << "[OrbitWorker]   filePaths:" << filePaths;
    qDebug() << "[OrbitWorker]   orbitSource:" << orbitSource;
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
        qDebug() << "[OrbitWorker] h5File:" << h5File;
        qDebug() << "[OrbitWorker]   granule:" << granule;

        emit updateProcess(10 + i * 80 / total, QStringLiteral("正在解析影像 [%1] 的成像时间……").arg(QFileInfo(h5File).fileName()));

        // 解析卫星平台 (S1A 或 S1B) 与成像日期
        QString platform; // 空字符串 = 不按平台过滤
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
            QRegularExpression dateFallbackRe("(\\d{8})");
            QRegularExpressionMatch dateFallbackMatch = dateFallbackRe.match(QFileInfo(h5File).fileName());
            if (dateFallbackMatch.hasMatch())
            {
                dateStr = dateFallbackMatch.captured(1);
            }
        }
        qDebug() << "[OrbitWorker]   platform:" << platform << "dateStr:" << dateStr;

        // 从 H5 读取精确成像时刻（用于 RESORB 时效窗口校验）
        QDateTime imgTime;
        {
            std::string acqTimeStr;
            NodeUtils::Hdf5Locker locker;
            if (NodeUtils::readStringFromH5(h5File, "acquisition_start_time", acqTimeStr)) {
                imgTime = QDateTime::fromString(QString::fromStdString(acqTimeStr), Qt::ISODate);
            }
        }
        qDebug() << "[OrbitWorker]   imgTime:" << imgTime.toString(Qt::ISODate);

        if (dateStr.isEmpty())
        {
            InSARLogManager::LogWarning("OrbitSourceWorker", QString("无法确定影像 [%1] 的成像日期，跳过此文件。").arg(h5File));
            continue;
        }

        // 计算精轨生效时间区间
        QDate centerDate = QDate::fromString(dateStr, "yyyyMMdd");
        QDate prevDate = centerDate.addDays(-1);
        QDate nextDate = centerDate.addDays(1);

        bool foundEof = false;
        QString downloadUrl;
        QString eofFileName;

        // 直接抓取 ASF S1QC POEORB 目录列表（不依赖 API）
        {
            // S1QC 公开目录：https://s1qc.asf.alaska.edu/aux_poeorb/
            QString listingUrl = "https://s1qc.asf.alaska.edu/aux_poeorb/";
            qDebug() << "[OrbitWorker] fetching listing:" << listingUrl;

            QNetworkAccessManager listingManager;
            QNetworkRequest listingRequest((QUrl(listingUrl)));
            listingRequest.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
            QNetworkReply* listingReply = listingManager.get(listingRequest);

            QEventLoop loop;
            connect(listingReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            if (listingReply->error() == QNetworkReply::NoError)
            {
                QString html = QString::fromUtf8(listingReply->readAll());
                qDebug() << "[OrbitWorker] listing HTML size:" << html.size();

                // 从 HTML 中提取所有 .EOF 文件名的 <a href="..."> 链接
                QRegularExpression hrefRe("<a\\s+href=\"([^\"]+\\.EOF)\"", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatchIterator it = hrefRe.globalMatch(html);

                // 使用正则匹配新旧版本 ESA POEORB 命名规范
                // 旧版：V{prev}T215942_{next}T000142（2020 年前）
                // 新版：V{prev}T225942_{next}T005942（2021 年后，约推后 1 小时）
                QRegularExpression valRe(
                    QString("V%1T\\d{6}_%2T\\d{6}").arg(
                        prevDate.toString("yyyyMMdd"),
                        nextDate.toString("yyyyMMdd")),
                    QRegularExpression::CaseInsensitiveOption);
                qDebug() << "[OrbitWorker] POEORB regex:" << valRe.pattern() << "platform:" << platform;

                while (it.hasNext())
                {
                    QRegularExpressionMatch m = it.next();
                    QString fname = m.captured(1);
                    bool platOk = platform.isEmpty() || fname.contains(platform, Qt::CaseInsensitive);
                    bool valOk = fname.contains(valRe);
                    if (platOk && valOk)
                    {
                        eofFileName = fname;
                        downloadUrl = listingUrl + fname;
                        foundEof = true;
                        qDebug() << "[OrbitWorker] POEORB matched:" << fname;
                        break;
                    }
                }

                if (!foundEof) {
                    qDebug() << "[OrbitWorker] no POEORB match in listing, total EOF files found:" << hrefRe.globalMatch(html).hasNext();
                }
            }
            else
            {
                qDebug() << "[OrbitWorker] listing fetch error:" << listingReply->error() << listingReply->errorString();
            }
            listingReply->deleteLater();
        }

        // 如果没有找到 POEORB (精密轨道)，尝试寻找 RESORB (重构轨道)
        if (!foundEof)
        {
            qDebug() << "[OrbitWorker] POEORB not found, trying RESORB from s1qc...";
            InSARLogManager::LogInfo("OrbitSourceWorker", QString("未检索到 POEORB，尝试搜索 RESORB..."));

            QString resorbListingUrl = "https://s1qc.asf.alaska.edu/aux_resorb/";
            qDebug() << "[OrbitWorker] fetching RESORB listing:" << resorbListingUrl;

            QNetworkAccessManager resManager;
            QNetworkRequest resRequest((QUrl(resorbListingUrl)));
            resRequest.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
            QNetworkReply* resReply = resManager.get(resRequest);

            QEventLoop resLoop;
            connect(resReply, &QNetworkReply::finished, &resLoop, &QEventLoop::quit);
            resLoop.exec();

            if (resReply->error() == QNetworkReply::NoError)
            {
                QString html = QString::fromUtf8(resReply->readAll());
                qDebug() << "[OrbitWorker] RESORB listing HTML size:" << html.size();

                QRegularExpression hrefRe("<a\\s+href=\"([^\"]+\\.EOF)\"", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatchIterator it = hrefRe.globalMatch(html);

                // RESORB 时效窗口精确匹配：提取 V{start}_{end} 字段，校验成像时刻是否在区间内
                QRegularExpression resValidityRe("V(\\d{8}T\\d{6})_(\\d{8}T\\d{6})");
                while (it.hasNext())
                {
                    QRegularExpressionMatch m = it.next();
                    QString fname = m.captured(1);
                    bool platOk = platform.isEmpty() || fname.contains(platform, Qt::CaseInsensitive);
                    if (!platOk) continue;

                    QRegularExpressionMatch vm = resValidityRe.match(fname);
                    if (!vm.hasMatch()) continue;
                    QDateTime startVal = QDateTime::fromString(vm.captured(1), "yyyyMMddTHHmmss");
                    QDateTime endVal   = QDateTime::fromString(vm.captured(2), "yyyyMMddTHHmmss");

                    bool timeOk = imgTime.isValid()
                        ? (imgTime >= startVal && imgTime <= endVal)
                        : fname.contains(dateStr);  // 无采集时间时回退到日期匹配
                    if (timeOk)
                    {
                        eofFileName = fname;
                        downloadUrl = resorbListingUrl + fname;
                        foundEof = true;
                        qDebug() << "[OrbitWorker] RESORB matched:" << fname;
                        break;
                    }
                }
            }
            else
            {
                qDebug() << "[OrbitWorker] RESORB listing error:" << resReply->error();
            }
            resReply->deleteLater();
        }

        if (foundEof && !downloadUrl.isEmpty())
        {
            QString finalSavePath = cacheDir + "/" + eofFileName;

            // 检查本地缓存是否已存在该同名轨道文件且大小非空
            if (QFile::exists(finalSavePath) && QFileInfo(finalSavePath).size() > 0)
            {
                successCount++;
                InSARLogManager::LogInfo("OrbitSourceWorker", QString("精密轨道文件已在本地缓存中存在，跳过下载：%1").arg(eofFileName));
            }
            else
            {
                emit updateProcess(10 + i * 80 / total, QStringLiteral("正在下载轨道文件 %1...").arg(eofFileName));
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
        }
        else
        {
            InSARLogManager::LogWarning("OrbitSourceWorker", QString("未在服务器上检索到影像 [%1] 的任何有效精轨/重构轨数据。").arg(QFileInfo(h5File).fileName()));
        }
    }

    qDebug() << "[OrbitWorker] fetch_orbits() done, successCount:" << successCount << "/" << total;
    emit updateProcess(100, QStringLiteral("精密轨道数据下载完成！成功匹配数：%1/%2").arg(successCount).arg(total));
    emit endProcess();
}
