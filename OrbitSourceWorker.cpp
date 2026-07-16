#include "OrbitSourceWorker.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "tinyxml.h"
#include "FormatConversion.h"
#include "CDSELoginDialog.h"
#include <QAuthenticator>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QSet>
#include <algorithm>

namespace {
const char* kAsfPoeUrl = "https://s1qc.asf.alaska.edu/aux_poeorb/";
const char* kAsfResUrl = "https://s1qc.asf.alaska.edu/aux_resorb/";
const char* kCdseTokenUrl = "https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/openid-connect/token";
const char* kCdseCatalogueUrl = "https://catalogue.dataspace.copernicus.eu/odata/v1/Products";
const char* kCdseDownloadHost = "zipper.dataspace.copernicus.eu";

QDateTime parseUtcDateTime(const QString& value)
{
    QDateTime result = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!result.isValid()) {
        result = QDateTime::fromString(value, Qt::ISODate);
    }
    if (result.isValid()) {
        if (result.timeSpec() == Qt::LocalTime) {
            result.setTimeSpec(Qt::UTC);
        } else {
            result = result.toUTC();
        }
    }
    return result;
}

QString sourceName(int source)
{
    return source == 1 ? QStringLiteral("ESA CDSE") : QStringLiteral("NASA ASF");
}
}

OrbitSourceWorker::OrbitSourceWorker(QObject* parent)
    : BaseWorker(parent)
{
}

OrbitSourceWorker::~OrbitSourceWorker()
{
}

bool OrbitSourceWorker::convertOrbitSource(int value, OrbitSource& source, QString& errorMessage) const
{
    if (value == static_cast<int>(OrbitSource::Asf)) {
        source = OrbitSource::Asf;
        return true;
    }
    if (value == static_cast<int>(OrbitSource::Cdse)) {
        source = OrbitSource::Cdse;
        return true;
    }
    errorMessage = QStringLiteral("未知的轨道数据源索引：%1").arg(value);
    return false;
}

QString OrbitSourceWorker::getOriginalGranuleName(const QString& h5FilePath, const QString& projectDir)
{
    Q_UNUSED(projectDir);
    QString granuleName;
    std::string sourceFileStr;
    bool readOk = false;
    {
        NodeUtils::Hdf5Locker locker(h5FilePath);
        readOk = NodeUtils::readStringFromH5(h5FilePath, "source_1", sourceFileStr);
    }

    if (readOk && !sourceFileStr.empty()) {
        const QFileInfo info(QString::fromStdString(sourceFileStr));
        granuleName = info.fileName().compare("manifest.safe", Qt::CaseInsensitive) == 0
            ? info.dir().dirName() : info.completeBaseName();
    }

    if (granuleName.isEmpty()) {
        std::string comment;
        bool commentOk = false;
        {
            NodeUtils::Hdf5Locker locker(h5FilePath);
            commentOk = NodeUtils::readStringFromH5(h5FilePath, "comment", comment);
        }
        if (commentOk) {
            QRegularExpression re("(S1[AB]_IW_SLC__[^\\s]+)", QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch match = re.match(QString::fromStdString(comment));
            if (match.hasMatch()) {
                granuleName = match.captured(1);
            }
        }
    }

    return granuleName.isEmpty() ? QFileInfo(h5FilePath).completeBaseName() : granuleName;
}

bool OrbitSourceWorker::readSlcInfo(const QString& h5FilePath, const QString& projectDir,
    SlcInfo& info, QString& errorMessage)
{
    const QString granule = getOriginalGranuleName(h5FilePath, projectDir);
    if (granule.contains("S1A", Qt::CaseInsensitive)) {
        info.platform = "S1A";
    } else if (granule.contains("S1B", Qt::CaseInsensitive)) {
        info.platform = "S1B";
    }

    std::string startText;
    std::string stopText;
    std::string sensorText;
    {
        NodeUtils::Hdf5Locker locker(h5FilePath);
        NodeUtils::readStringFromH5(h5FilePath, "acquisition_start_time", startText);
        NodeUtils::readStringFromH5(h5FilePath, "acquisition_stop_time", stopText);
        NodeUtils::readStringFromH5(h5FilePath, "sensor", sensorText);
    }

    info.acquisitionStart = parseUtcDateTime(QString::fromStdString(startText));
    info.acquisitionStop = parseUtcDateTime(QString::fromStdString(stopText));
    if (!info.acquisitionStart.isValid()) {
        QRegularExpression timeRe("(\\d{8}T\\d{6})");
        const QRegularExpressionMatch match = timeRe.match(granule);
        if (match.hasMatch()) {
            info.acquisitionStart = QDateTime::fromString(match.captured(1), "yyyyMMddTHHmmss");
            info.acquisitionStart.setTimeSpec(Qt::UTC);
            info.acquisitionStop = info.acquisitionStart;
        }
    }
    if (!info.acquisitionStop.isValid()) {
        info.acquisitionStop = info.acquisitionStart;
    }

    if (info.platform.isEmpty()) {
        const QString sensor = QString::fromStdString(sensorText).toUpper();
        if (sensor.contains("SENTINEL-1A") || sensor == "S1A") {
            info.platform = "S1A";
        } else if (sensor.contains("SENTINEL-1B") || sensor == "S1B") {
            info.platform = "S1B";
        }
    }

    if (!info.acquisitionStart.isValid()) {
        errorMessage = QStringLiteral("无法读取影像 %1 的成像时间。").arg(QFileInfo(h5FilePath).fileName());
        return false;
    }
    return true;
}

bool OrbitSourceWorker::parseOrbitValidity(const QString& fileName, QDateTime& validStart,
    QDateTime& validEnd) const
{
    QRegularExpression re("V(\\d{8}T\\d{6})_(\\d{8}T\\d{6})", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(fileName);
    if (!match.hasMatch()) {
        return false;
    }
    validStart = QDateTime::fromString(match.captured(1), "yyyyMMddTHHmmss");
    validEnd = QDateTime::fromString(match.captured(2), "yyyyMMddTHHmmss");
    validStart.setTimeSpec(Qt::UTC);
    validEnd.setTimeSpec(Qt::UTC);
    return validStart.isValid() && validEnd.isValid() && validStart <= validEnd;
}

bool OrbitSourceWorker::productCovers(const OrbitProduct& product, const SlcInfo& info) const
{
    return product.validStart.isValid() && product.validEnd.isValid()
        && product.validStart <= info.acquisitionStart
        && product.validEnd >= info.acquisitionStop;
}

bool OrbitSourceWorker::findCachedOrbit(const QString& cacheDir, const SlcInfo& info,
    bool precise, OrbitProduct& product) const
{
    QDir dir(cacheDir);
    if (!dir.exists()) {
        return false;
    }

    QList<OrbitProduct> matches;
    const QFileInfoList files = dir.entryInfoList(QStringList() << "*.EOF", QDir::Files, QDir::Name);
    for (const QFileInfo& file : files) {
        if (file.size() <= 0) {
            continue;
        }
        const QString name = file.fileName();
        const bool filePrecise = name.contains("POEORB", Qt::CaseInsensitive);
        const bool fileRestituted = name.contains("RESORB", Qt::CaseInsensitive);
        if ((precise && !filePrecise) || (!precise && !fileRestituted)) {
            continue;
        }
        if (!info.platform.isEmpty() && !name.contains(info.platform, Qt::CaseInsensitive)) {
            continue;
        }

        OrbitProduct candidate;
        candidate.fileName = name;
        candidate.downloadUrl = QUrl::fromLocalFile(file.absoluteFilePath());
        candidate.isPrecise = precise;
        if (!parseOrbitValidity(name, candidate.validStart, candidate.validEnd) || !productCovers(candidate, info)) {
            continue;
        }
        QString xmlError;
        if (!validateOrbitXml(file.absoluteFilePath(), candidate.validStart, candidate.validEnd, xmlError)) {
            InSARLogManager::LogWarning("OrbitSourceWorker",
                QStringLiteral("缓存轨道文件完整性校验失败，已自动清理并跳过：%1。错误信息：%2")
                .arg(file.fileName()).arg(xmlError));
            QFile::remove(file.absoluteFilePath());
            continue;
        }
        matches.append(candidate);
    }

    if (matches.isEmpty()) {
        return false;
    }
    std::sort(matches.begin(), matches.end(), [](const OrbitProduct& left, const OrbitProduct& right) {
        const qint64 leftSpan = left.validStart.secsTo(left.validEnd);
        const qint64 rightSpan = right.validStart.secsTo(right.validEnd);
        if (leftSpan != rightSpan) return leftSpan < rightSpan;
        return left.fileName < right.fileName;
    });
    product = matches.first();
    return true;
}

OrbitSourceWorker::NetworkResult OrbitSourceWorker::performRequest(const QNetworkRequest& request,
    const QByteArray& method, const QByteArray& body, int timeoutMs)
{
    NetworkResult result;
    QNetworkAccessManager manager;
    QNetworkReply* reply = nullptr;
    if (method == "POST") {
        reply = manager.post(request, body);
    } else {
        reply = manager.get(request);
    }

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QTimer stopTimer;
    stopTimer.setInterval(100);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, this, [&]() {
        result.timedOut = true;
        reply->abort();
    });
    connect(&stopTimer, &QTimer::timeout, this, [&]() {
        if (isStopRequested()) {
            result.cancelled = true;
            reply->abort();
        }
    });
    timeoutTimer.start(timeoutMs);
    stopTimer.start();
    loop.exec();
    timeoutTimer.stop();
    stopTimer.stop();

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.networkError = reply->error();
    result.data = reply->readAll();
    result.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    result.errorString = reply->errorString();
    result.redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    reply->deleteLater();
    return result;
}

bool OrbitSourceWorker::findAsfOrbit(const SlcInfo& info, bool precise, OrbitProduct& product,
    QString& errorMessage)
{
    const QString listingUrl = precise ? QString::fromLatin1(kAsfPoeUrl) : QString::fromLatin1(kAsfResUrl);
    QNetworkRequest request{QUrl(listingUrl)};
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    const NetworkResult response = performRequest(request, "GET");
    if (response.cancelled) {
        errorMessage = QStringLiteral("用户中止了轨道检索。");
        return false;
    }
    if (response.timedOut) {
        errorMessage = QStringLiteral("ASF 轨道目录请求超时。");
        return false;
    }
    if (response.networkError != QNetworkReply::NoError || response.statusCode < 200 || response.statusCode >= 300) {
        errorMessage = QStringLiteral("ASF 轨道目录请求失败：HTTP %1，%2")
            .arg(response.statusCode).arg(response.errorString);
        return false;
    }

    QList<OrbitProduct> candidates;
    const QString html = QString::fromUtf8(response.data);
    QRegularExpression hrefRe("<a\\s+href=\"([^\"]+\\.EOF)\"", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator iterator = hrefRe.globalMatch(html);
    while (iterator.hasNext()) {
        const QString fileName = QUrl::fromPercentEncoding(iterator.next().captured(1).toUtf8());
        if (!info.platform.isEmpty() && !fileName.contains(info.platform, Qt::CaseInsensitive)) {
            continue;
        }
        if (precise != fileName.contains("POEORB", Qt::CaseInsensitive)) {
            continue;
        }
        OrbitProduct candidate;
        candidate.fileName = fileName;
        candidate.downloadUrl = QUrl(listingUrl + fileName);
        candidate.isPrecise = precise;
        if (parseOrbitValidity(fileName, candidate.validStart, candidate.validEnd) && productCovers(candidate, info)) {
            candidates.append(candidate);
        }
    }

    if (candidates.isEmpty()) {
        errorMessage = QStringLiteral("ASF 未找到覆盖成像时段的 %1 文件。")
            .arg(precise ? "POEORB" : "RESORB");
        return false;
    }
    std::sort(candidates.begin(), candidates.end(), [](const OrbitProduct& left, const OrbitProduct& right) {
        const qint64 leftSpan = left.validStart.secsTo(left.validEnd);
        const qint64 rightSpan = right.validStart.secsTo(right.validEnd);
        return leftSpan == rightSpan ? left.fileName < right.fileName : leftSpan < rightSpan;
    });
    product = candidates.first();
    return true;
}

void OrbitSourceWorker::clearCdseCredentials()
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    settings.remove("Orbit/CDSEUser");
    settings.remove("Orbit/CDSEPassword");
    m_cdseAccessToken.clear();
    m_cdseTokenExpiry = QDateTime();
}

bool OrbitSourceWorker::ensureCdseToken(QString& errorMessage, bool forceRefresh)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_cdseAccessToken.isEmpty()) {
        const QString sessionToken = CDSELoginDialog::sessionAccessToken();
        const QDateTime sessionExpiry = CDSELoginDialog::sessionTokenExpiry();
        if (!sessionToken.isEmpty() && sessionExpiry.isValid() && now.secsTo(sessionExpiry) > 60) {
            m_cdseAccessToken = sessionToken;
            m_cdseTokenExpiry = sessionExpiry;
        }
    }
    if (!forceRefresh && !m_cdseAccessToken.isEmpty()
        && (!m_cdseTokenExpiry.isValid() || now.secsTo(m_cdseTokenExpiry) > 60)) {
        return true;
    }

    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    const QString username = QString::fromUtf8(QByteArray::fromBase64(
        settings.value("Orbit/CDSEUser", "").toString().toUtf8()));
    const QString password = QString::fromUtf8(QByteArray::fromBase64(
        settings.value("Orbit/CDSEPassword", "").toString().toUtf8()));
    if (username.isEmpty() || password.isEmpty()) {
        errorMessage = QStringLiteral("缺少 ESA CDSE 登录凭据。");
        return false;
    }

    QUrlQuery form;
    form.addQueryItem("client_id", "cdse-public");
    form.addQueryItem("username", username);
    form.addQueryItem("password", password);
    form.addQueryItem("grant_type", "password");
    QNetworkRequest request{QUrl(kCdseTokenUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Accept", "application/json");
    const NetworkResult response = performRequest(request, "POST", form.query(QUrl::FullyEncoded).toUtf8(), 20000);

    if (response.cancelled) {
        errorMessage = QStringLiteral("用户中止了 CDSE 认证。");
        return false;
    }
    if (response.timedOut) {
        errorMessage = QStringLiteral("CDSE 认证请求超时。");
        return false;
    }

    QJsonParseError parseError;
    const QJsonObject object = QJsonDocument::fromJson(response.data, &parseError).object();
    if ((response.statusCode == 400 || response.statusCode == 401)
        && response.networkError != QNetworkReply::OperationCanceledError) {
        clearCdseCredentials();
        errorMessage = QStringLiteral("CDSE 凭据无效：%1")
            .arg(object.value("error_description").toString(object.value("error").toString()));
        return false;
    }
    if (response.statusCode == 429) {
        errorMessage = QStringLiteral("CDSE 认证服务请求过于频繁（HTTP 429），请稍后重试。");
        return false;
    }
    if (response.networkError != QNetworkReply::NoError || response.statusCode < 200 || response.statusCode >= 300) {
        errorMessage = QStringLiteral("CDSE 认证失败：HTTP %1，%2")
            .arg(response.statusCode).arg(response.errorString);
        return false;
    }
    if (parseError.error != QJsonParseError::NoError) {
        errorMessage = QStringLiteral("CDSE 认证响应不是有效 JSON。");
        return false;
    }

    m_cdseAccessToken = object.value("access_token").toString();
    if (m_cdseAccessToken.isEmpty()) {
        errorMessage = QStringLiteral("CDSE 认证响应中缺少 access_token。");
        return false;
    }
    const int expiresIn = object.value("expires_in").toInt(600);
    m_cdseTokenExpiry = now.addSecs(qMax(60, expiresIn));
    return true;
}

bool OrbitSourceWorker::findCdseOrbit(const SlcInfo& info, bool precise, OrbitProduct& product,
    QString& errorMessage)
{
    if (!ensureCdseToken(errorMessage)) {
        return false;
    }

    const QString orbitType = precise ? "AUX_POEORB" : "AUX_RESORB";
    const QString windowStart = info.acquisitionStart.addDays(-2).toString("yyyy-MM-ddTHH:mm:ss.zzzZ");
    const QString windowEnd = info.acquisitionStop.addDays(2).toString("yyyy-MM-ddTHH:mm:ss.zzzZ");
    QString filter = QString("Collection/Name eq 'SENTINEL-1' and contains(Name,'%1')").arg(orbitType);
    if (!info.platform.isEmpty()) {
        filter += QString(" and contains(Name,'%1')").arg(info.platform);
    }
    filter += QString(" and ContentDate/Start lt %1 and ContentDate/End gt %2")
        .arg(windowEnd, windowStart);

    QUrl url(kCdseCatalogueUrl);
    QUrlQuery query;
    query.addQueryItem("$filter", filter);
    query.addQueryItem("$orderby", "ContentDate/Start desc");
    query.addQueryItem("$top", "100");
    query.addQueryItem("$expand", "Attributes");
    url.setQuery(query);

    QList<OrbitProduct> candidates;
    QSet<QString> visitedPages;
    int pageCount = 0;
    bool authRetried = false;
    while (url.isValid() && !url.isEmpty() && pageCount < 100) {
        if (isStopRequested()) {
            errorMessage = QStringLiteral("用户中止了 CDSE 产品检索。");
            return false;
        }
        const QString pageKey = url.toString(QUrl::FullyEncoded);
        if (visitedPages.contains(pageKey)) {
            errorMessage = QStringLiteral("CDSE Catalogue 返回了循环分页链接。");
            return false;
        }
        visitedPages.insert(pageKey);
        pageCount++;

        QNetworkRequest request(url);
        request.setRawHeader("Accept", "application/json");
        NetworkResult response = performRequest(request, "GET");
        if (response.statusCode == 401 && !authRetried) {
            authRetried = true;
            if (!ensureCdseToken(errorMessage, true)) {
                return false;
            }
            continue;
        }
        if (response.statusCode == 401) {
            clearCdseCredentials();
            errorMessage = QStringLiteral("CDSE Catalogue 重新认证后仍拒绝访问（HTTP 401）。");
            return false;
        }
        if (response.cancelled) {
            errorMessage = QStringLiteral("用户中止了 CDSE 产品检索。");
            return false;
        }
        if (response.timedOut) {
            errorMessage = QStringLiteral("CDSE Catalogue 请求超时。");
            return false;
        }
        if (response.statusCode == 429) {
            errorMessage = QStringLiteral("CDSE Catalogue 请求过于频繁（HTTP 429），请稍后重试。");
            return false;
        }
        if (response.networkError != QNetworkReply::NoError || response.statusCode < 200 || response.statusCode >= 300) {
            errorMessage = QStringLiteral("CDSE Catalogue 请求失败：HTTP %1，%2")
                .arg(response.statusCode).arg(response.errorString);
            return false;
        }

        QJsonParseError parseError;
        const QJsonObject root = QJsonDocument::fromJson(response.data, &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            errorMessage = QStringLiteral("CDSE Catalogue 返回了无法解析的 JSON。");
            return false;
        }
        const QJsonArray values = root.value("value").toArray();
        for (const QJsonValue& value : values) {
            const QJsonObject object = value.toObject();
            OrbitProduct candidate;
            candidate.id = object.value("Id").toString();
            candidate.fileName = object.value("Name").toString();
            candidate.isPrecise = precise;
            if (candidate.id.isEmpty() || candidate.fileName.isEmpty()
                || !candidate.fileName.contains(orbitType, Qt::CaseInsensitive)) {
                continue;
            }
            if (!info.platform.isEmpty() && !candidate.fileName.contains(info.platform, Qt::CaseInsensitive)) {
                continue;
            }
            if (!parseOrbitValidity(candidate.fileName, candidate.validStart, candidate.validEnd)
                || !productCovers(candidate, info)) {
                continue;
            }

            const QJsonArray checksums = object.value("Checksum").toArray();
            if (!checksums.isEmpty()) {
                const QJsonObject checksum = checksums.first().toObject();
                candidate.checksumAlgorithm = checksum.value("Algorithm").toString();
                candidate.checksum = checksum.value("Value").toString();
            }
            candidate.downloadUrl = QUrl(QString("https://%1/odata/v1/Products(%2)/$value")
                .arg(kCdseDownloadHost, candidate.id));
            candidates.append(candidate);
        }

        const QString nextLink = root.value("@odata.nextLink").toString();
        if (!nextLink.isEmpty()) {
            url = QUrl(nextLink);
        } else if (values.size() == 100) {
            QUrlQuery nextQuery(url);
            nextQuery.removeAllQueryItems("$skip");
            nextQuery.addQueryItem("$skip", QString::number(pageCount * 100));
            url.setQuery(nextQuery);
        } else {
            url = QUrl();
        }
    }

    if (candidates.isEmpty()) {
        errorMessage = QStringLiteral("CDSE 未找到覆盖成像时段的 %1 文件。").arg(orbitType);
        return false;
    }
    std::sort(candidates.begin(), candidates.end(), [](const OrbitProduct& left, const OrbitProduct& right) {
        const qint64 leftSpan = left.validStart.secsTo(left.validEnd);
        const qint64 rightSpan = right.validStart.secsTo(right.validEnd);
        return leftSpan == rightSpan ? left.fileName < right.fileName : leftSpan < rightSpan;
    });
    product = candidates.first();
    return true;
}

bool OrbitSourceWorker::verifyDownloadedFile(const OrbitProduct& product, const QString& filePath,
    QString& errorMessage) const
{
    const QFileInfo info(filePath);
    if (!info.exists() || info.size() <= 0) {
        errorMessage = QStringLiteral("下载的轨道文件为空。");
        return false;
    }
    QDateTime start;
    QDateTime end;
    if (!parseOrbitValidity(product.fileName, start, end)) {
        errorMessage = QStringLiteral("下载产品名称不包含有效轨道时段：%1").arg(product.fileName);
        return false;
    }

    if (!product.checksum.isEmpty()) {
        const QString algorithm = product.checksumAlgorithm.toUpper();
        QCryptographicHash::Algorithm hashAlgorithm;
        if (algorithm == "MD5") {
            hashAlgorithm = QCryptographicHash::Md5;
        } else if (algorithm == "SHA-1" || algorithm == "SHA1") {
            hashAlgorithm = QCryptographicHash::Sha1;
        } else if (algorithm == "SHA-256" || algorithm == "SHA256") {
            hashAlgorithm = QCryptographicHash::Sha256;
        } else {
            return true;
        }
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            errorMessage = QStringLiteral("无法读取已下载的轨道文件进行校验。");
            return false;
        }
        QCryptographicHash hash(hashAlgorithm);
        while (!file.atEnd()) {
            hash.addData(file.read(1024 * 1024));
        }
        if (QString::fromLatin1(hash.result().toHex()).compare(product.checksum, Qt::CaseInsensitive) != 0) {
            errorMessage = QStringLiteral("轨道文件校验和不匹配。");
            return false;
        }
    }
    QString xmlError;
    if (!validateOrbitXml(filePath, start, end, xmlError)) {
        errorMessage = QStringLiteral("轨道文件 XML 完整性验证失败：%1").arg(xmlError);
        return false;
    }
    return true;
}

bool OrbitSourceWorker::validateOrbitXml(const QString& filePath, const QDateTime& expectedStart,
    const QDateTime& expectedEnd, QString& errorMessage) const
{
    TiXmlDocument doc;
    if (!doc.LoadFile(filePath.toLocal8Bit().constData())) {
        errorMessage = QStringLiteral("无法作为 XML 解析，文件可能已损坏或非 XML 格式。");
        return false;
    }

    TiXmlElement* root = doc.RootElement();
    if (!root || strcmp(root->Value(), "Earth_Explorer_File") != 0) {
        errorMessage = QStringLiteral("缺少根元素 <Earth_Explorer_File>。");
        return false;
    }

    TiXmlElement* header = root->FirstChildElement("Earth_Explorer_Header");
    if (!header) {
        errorMessage = QStringLiteral("缺少 <Earth_Explorer_Header> 节点。");
        return false;
    }

    TiXmlElement* fixedHeader = header->FirstChildElement("Fixed_Header");
    if (!fixedHeader) {
        errorMessage = QStringLiteral("缺少 <Fixed_Header> 节点。");
        return false;
    }

    TiXmlElement* valPeriod = fixedHeader->FirstChildElement("Validity_Period");
    if (!valPeriod) {
        errorMessage = QStringLiteral("缺少 <Validity_Period> 节点。");
        return false;
    }

    TiXmlElement* valStartElem = valPeriod->FirstChildElement("Validity_Start");
    TiXmlElement* valStopElem = valPeriod->FirstChildElement("Validity_Stop");
    if (!valStartElem || !valStopElem) {
        errorMessage = QStringLiteral("缺少 <Validity_Start> 或 <Validity_Stop> 节点。");
        return false;
    }

    const char* valStartText = valStartElem->GetText();
    const char* valStopText = valStopElem->GetText();
    if (!valStartText || !valStopText) {
        errorMessage = QStringLiteral("读取 Validity_Start/Stop 文本内容为空。");
        return false;
    }

    QString startStr = QString::fromUtf8(valStartText).trimmed();
    if (startStr.startsWith("UTC=", Qt::CaseInsensitive)) {
        startStr = startStr.mid(4);
    }
    QDateTime fileValidStart = QDateTime::fromString(startStr, "yyyy-MM-ddTHH:mm:ss");
    fileValidStart.setTimeSpec(Qt::UTC);

    QString stopStr = QString::fromUtf8(valStopText).trimmed();
    if (stopStr.startsWith("UTC=", Qt::CaseInsensitive)) {
        stopStr = stopStr.mid(4);
    }
    QDateTime fileValidEnd = QDateTime::fromString(stopStr, "yyyy-MM-ddTHH:mm:ss");
    fileValidEnd.setTimeSpec(Qt::UTC);

    if (!fileValidStart.isValid() || !fileValidEnd.isValid()) {
        errorMessage = QStringLiteral("解析文件的 Validity 字段为时间格式失败。");
        return false;
    }

    // 允许 1 秒以内的误差，主要是由于可能存在的微小舍入或字符串格式差异
    if (qAbs(fileValidStart.secsTo(expectedStart)) > 1 || qAbs(fileValidEnd.secsTo(expectedEnd)) > 1) {
        errorMessage = QStringLiteral("文件内容 validity 时间与文件名解析结果不匹配。文件: %1 - %2, 预估: %3 - %4")
            .arg(fileValidStart.toString("yyyyMMddTHHmmss"))
            .arg(fileValidEnd.toString("yyyyMMddTHHmmss"))
            .arg(expectedStart.toString("yyyyMMddTHHmmss"))
            .arg(expectedEnd.toString("yyyyMMddTHHmmss"));
        return false;
    }

    return true;
}

bool OrbitSourceWorker::downloadOrbitFile(OrbitSource source, const OrbitProduct& product,
    const QString& savePath, QString& errorMessage, bool allowAuthRetry)
{
    if (source == OrbitSource::Cdse && !ensureCdseToken(errorMessage)) {
        return false;
    }
    if (source == OrbitSource::Cdse
        && (product.downloadUrl.scheme() != "https"
            || product.downloadUrl.host().compare(kCdseDownloadHost, Qt::CaseInsensitive) != 0)) {
        errorMessage = QStringLiteral("拒绝向非 CDSE HTTPS 主机发送访问令牌。");
        return false;
    }

    QUrl currentUrl = product.downloadUrl;
    NetworkResult response;
    for (int redirectCount = 0; redirectCount <= 5; ++redirectCount) {
        QNetworkRequest request(currentUrl);
        if (source == OrbitSource::Cdse) {
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            request.setRawHeader("Authorization", "Bearer " + m_cdseAccessToken.toUtf8());
        } else {
            request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
            QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
            const QString username = QString::fromUtf8(QByteArray::fromBase64(
                settings.value("DEM/EarthdataUser", "").toString().toUtf8()));
            const QString password = QString::fromUtf8(QByteArray::fromBase64(
                settings.value("DEM/EarthdataPassword", "").toString().toUtf8()));
            if (!username.isEmpty() && !password.isEmpty()) {
                request.setRawHeader("Authorization", "Basic "
                    + QString("%1:%2").arg(username, password).toUtf8().toBase64());
            }
        }

        response = performRequest(request, "GET", QByteArray(), 120000);
        if (source != OrbitSource::Cdse || response.redirectUrl.isEmpty()) {
            break;
        }

        const QUrl redirectedUrl = currentUrl.resolved(response.redirectUrl);
        const QString host = redirectedUrl.host().toLower();
        if (redirectedUrl.scheme() != "https"
            || !(host == "dataspace.copernicus.eu" || host.endsWith(".dataspace.copernicus.eu"))) {
            errorMessage = QStringLiteral("CDSE 下载重定向到了不受信任的主机：%1").arg(host);
            return false;
        }
        currentUrl = redirectedUrl;
        if (redirectCount == 5) {
            errorMessage = QStringLiteral("CDSE 下载重定向次数过多。");
            return false;
        }
    }
    if (source == OrbitSource::Cdse && response.statusCode == 401) {
        if (!allowAuthRetry) {
            clearCdseCredentials();
            errorMessage = QStringLiteral("CDSE 访问令牌无效，重新认证后仍被拒绝（HTTP 401）。");
            return false;
        }
        if (!ensureCdseToken(errorMessage, true)) {
            return false;
        }
        return downloadOrbitFile(source, product, savePath, errorMessage, false);
    }
    if (response.cancelled) {
        errorMessage = QStringLiteral("用户中止了轨道文件下载。");
        return false;
    }
    if (response.timedOut) {
        errorMessage = QStringLiteral("轨道文件下载超时。");
        return false;
    }
    if (response.statusCode == 429) {
        errorMessage = QStringLiteral("轨道下载请求过于频繁（HTTP 429），请稍后重试。");
        return false;
    }
    if (response.networkError != QNetworkReply::NoError || response.statusCode < 200 || response.statusCode >= 300) {
        errorMessage = QStringLiteral("轨道文件下载失败：HTTP %1，%2")
            .arg(response.statusCode).arg(response.errorString);
        return false;
    }
    if (response.contentType.contains("html", Qt::CaseInsensitive)) {
        errorMessage = QStringLiteral("轨道下载返回了 HTML 页面，认证可能已失效。");
        return false;
    }

    const QString partPath = savePath + ".part";
    QFile::remove(partPath);
    QFile partFile(partPath);
    if (!partFile.open(QIODevice::WriteOnly) || partFile.write(response.data) != response.data.size()) {
        partFile.close();
        QFile::remove(partPath);
        errorMessage = QStringLiteral("无法完整写入轨道临时文件：%1").arg(partPath);
        return false;
    }
    partFile.close();
    if (!verifyDownloadedFile(product, partPath, errorMessage)) {
        QFile::remove(partPath);
        return false;
    }
    if (QFile::exists(savePath) && !QFile::remove(savePath)) {
        QFile::remove(partPath);
        errorMessage = QStringLiteral("无法替换已有轨道缓存：%1").arg(savePath);
        return false;
    }
    if (!QFile::rename(partPath, savePath)) {
        QFile::remove(partPath);
        errorMessage = QStringLiteral("无法提交轨道缓存文件：%1").arg(savePath);
        return false;
    }
    return true;
}

bool OrbitSourceWorker::resolveAndDownloadOrbit(OrbitSource source, const QString& h5FilePath,
    const QString& projectDir, const QString& cacheDir, QString& localEofPath, bool& isPrecise,
    QString& errorMessage)
{
    SlcInfo info;
    if (!readSlcInfo(h5FilePath, projectDir, info, errorMessage)) {
        return false;
    }

    OrbitProduct product;
    for (int typeIndex = 0; typeIndex < 2; ++typeIndex) {
        const bool precise = (typeIndex == 0);
        if (findCachedOrbit(cacheDir, info, precise, product)) {
            localEofPath = QDir(cacheDir).absoluteFilePath(product.fileName);
            isPrecise = precise;
            return true;
        }

        QString searchError;
        const bool found = source == OrbitSource::Cdse
            ? findCdseOrbit(info, precise, product, searchError)
            : findAsfOrbit(info, precise, product, searchError);
        if (!found) {
            if (isStopRequested() || searchError.contains("超时") || searchError.contains("失败")
                || searchError.contains("凭据") || searchError.contains("认证") || searchError.contains("429")) {
                errorMessage = searchError;
                return false;
            }
            errorMessage = searchError;
            continue;
        }

        const QString savePath = QDir(cacheDir).absoluteFilePath(product.fileName);
        if (!downloadOrbitFile(source, product, savePath, errorMessage)) {
            return false;
        }
        localEofPath = savePath;
        isPrecise = precise;
        return true;
    }
    return false;
}

void OrbitSourceWorker::fetch_orbits(QString projectPath, QString projectName, QStringList filePaths,
    int orbitSource, QString cacheDir, QStandardItemModel* model)
{
    Q_UNUSED(projectName);
    Q_UNUSED(model);
    OrbitSource source;
    QString errorMessage;
    if (!convertOrbitSource(orbitSource, source, errorMessage)) {
        emit errorProcess(errorMessage);
        return;
    }
    if (filePaths.isEmpty()) {
        emit errorProcess(QStringLiteral("没有可用于匹配轨道的 Sentinel-1 H5 文件。"));
        return;
    }

    const QString projectDir = projectPath.endsWith(".insar", Qt::CaseInsensitive)
        ? QFileInfo(projectPath).absolutePath() : projectPath;
    if (!QDir().mkpath(cacheDir)) {
        emit errorProcess(QStringLiteral("无法创建轨道缓存目录：%1").arg(cacheDir));
        return;
    }

    emit updateProcess(0, QStringLiteral("开始从 %1 检索轨道数据……").arg(sourceName(static_cast<int>(source))));
    int successCount = 0;
    for (int i = 0; i < filePaths.size(); ++i) {
        if (isStopRequested()) {
            emit errorProcess(QStringLiteral("用户中止了轨道下载。"));
            return;
        }
        emit updateProcess(10 + i * 80 / filePaths.size(),
            QStringLiteral("正在匹配影像 %1 的轨道文件……").arg(QFileInfo(filePaths.at(i)).fileName()));
        QString eofPath;
        bool precise = false;
        QString itemError;
        if (resolveAndDownloadOrbit(source, filePaths.at(i), projectDir, cacheDir,
            eofPath, precise, itemError)) {
            successCount++;
            InSARLogManager::LogInfo("OrbitSourceWorker", QString("%1 匹配轨道：%2")
                .arg(QFileInfo(filePaths.at(i)).fileName(), QFileInfo(eofPath).fileName()));
        } else {
            InSARLogManager::LogWarning("OrbitSourceWorker", itemError);
            if (isStopRequested() || itemError.contains("超时") || itemError.contains("失败")
                || itemError.contains("凭据") || itemError.contains("认证") || itemError.contains("429")) {
                emit errorProcess(itemError);
                return;
            }
        }
    }
    emit updateProcess(100, QStringLiteral("轨道数据下载完成，成功匹配 %1/%2。")
        .arg(successCount).arg(filePaths.size()));
    emit endProcess();
}

void OrbitSourceWorker::fetch_and_apply_orbits(QString projectPath, QString projectName,
    QStringList filePaths, int orbitSource, QString cacheDir, QString targetDirName,
    QStandardItemModel* model)
{
    Q_UNUSED(projectName);
    Q_UNUSED(model);
    OrbitSource source;
    QString errorMessage;
    if (!convertOrbitSource(orbitSource, source, errorMessage)) {
        emit errorProcess(errorMessage);
        return;
    }
    if (filePaths.isEmpty()) {
        emit errorProcess(QStringLiteral("没有可用于应用轨道的 Sentinel-1 H5 文件。"));
        return;
    }

    const QString projectDir = projectPath.endsWith(".insar", Qt::CaseInsensitive)
        ? QFileInfo(projectPath).absolutePath() : projectPath;
    if (!QDir().mkpath(cacheDir)) {
        emit errorProcess(QStringLiteral("无法创建轨道缓存目录：%1").arg(cacheDir));
        return;
    }

    emit updateProcess(0, QStringLiteral("开始从 %1 检索并匹配轨道数据……").arg(sourceName(static_cast<int>(source))));
    QHash<QString, QString> matchedOrbits;
    QHash<QString, bool> preciseFlags;
    for (int i = 0; i < filePaths.size(); ++i) {
        emit updateProcess(5 + i * 50 / filePaths.size(),
            QStringLiteral("正在匹配影像 %1 的轨道文件……").arg(QFileInfo(filePaths.at(i)).fileName()));
        QString eofPath;
        bool precise = false;
        QString itemError;
        if (resolveAndDownloadOrbit(source, filePaths.at(i), projectDir, cacheDir,
            eofPath, precise, itemError)) {
            matchedOrbits.insert(filePaths.at(i), eofPath);
            preciseFlags.insert(filePaths.at(i), precise);
        } else {
            InSARLogManager::LogWarning("OrbitSourceWorker", itemError);
            if (isStopRequested() || itemError.contains("超时") || itemError.contains("失败")
                || itemError.contains("凭据") || itemError.contains("认证") || itemError.contains("429")) {
                emit errorProcess(itemError);
                return;
            }
        }
    }

    emit updateProcess(60, QStringLiteral("轨道匹配完成，正在复制 H5 并应用轨道……"));
    int podApplyOk = 0;
    int podApplyFail = 0;
    int podSkipped = 0;
    QStringList newH5Paths;
    const QString targetDirPath = QDir(projectDir).absoluteFilePath(targetDirName);
    if (!QDir().mkpath(targetDirPath)) {
        emit errorProcess(QStringLiteral("无法创建轨道输出目录：%1").arg(targetDirPath));
        return;
    }

    FormatConversion conversion;
    for (int i = 0; i < filePaths.size(); ++i) {
        if (isStopRequested()) {
            emit errorProcess(QStringLiteral("用户中止了轨道应用。"));
            return;
        }
        const QString h5Path = filePaths.at(i);
        emit updateProcess(60 + i * 35 / filePaths.size(),
            QStringLiteral("正在应用影像 %1 的轨道……").arg(QFileInfo(h5Path).fileName()));

        std::string startText;
        std::string stopText;
        {
            NodeUtils::Hdf5Locker locker(h5Path);
            NodeUtils::readStringFromH5(h5Path, "acquisition_start_time", startText);
            NodeUtils::readStringFromH5(h5Path, "acquisition_stop_time", stopText);
        }

        const QString newH5Path = QDir(targetDirPath).absoluteFilePath(QFileInfo(h5Path).fileName());
        if (QFile::exists(newH5Path) && !QFile::remove(newH5Path)) {
            podApplyFail++;
            continue;
        }
        if (!QFile::copy(h5Path, newH5Path)) {
            podApplyFail++;
            continue;
        }

        const QString sourceJpg = h5Path.left(h5Path.lastIndexOf('.')) + ".jpg";
        const QString targetJpg = newH5Path.left(newH5Path.lastIndexOf('.')) + ".jpg";
        if (QFile::exists(sourceJpg)) {
            QFile::remove(targetJpg);
            QFile::copy(sourceJpg, targetJpg);
        }
        newH5Paths.append(newH5Path);

        const QString eofPath = matchedOrbits.value(h5Path);
        if (eofPath.isEmpty()) {
            podSkipped++;
            continue;
        }

        int result = -1;
        {
            NodeUtils::Hdf5Locker locker(newH5Path);
            double startGps = 0.0;
            double stopGps = 1e12;
            conversion.utc2gps(startText.c_str(), &startGps);
            conversion.utc2gps(stopText.c_str(), &stopGps);
            const QString nativeEof = QDir::toNativeSeparators(eofPath);
            const QString nativeH5 = QDir::toNativeSeparators(newH5Path);
            result = conversion.read_POD(nativeEof.toLocal8Bit().constData(), startGps, stopGps,
                nativeH5.toLocal8Bit().constData());
            if (result >= 0) {
                const QString orbitType = preciseFlags.value(h5Path)
                    ? "Precise (POE)" : "Reconstructed (RES)";
                conversion.write_str_to_h5(nativeH5.toLocal8Bit().constData(), "orbit_type",
                    orbitType.toStdString().c_str());
            }
        }
        if (result >= 0) {
            podApplyOk++;
        } else {
            podApplyFail++;
        }
    }

    std::sort(newH5Paths.begin(), newH5Paths.end(), [](const QString& left, const QString& right) {
        return QString::compare(left, right, Qt::CaseInsensitive) < 0;
    });
    emit updateProcess(100, QStringLiteral("轨道应用完成。"));
    emit applyOrbitsFinished(newH5Paths, podApplyOk, podApplyFail, podSkipped, targetDirName);
}
