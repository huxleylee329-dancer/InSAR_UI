#include "TSXBatchImportValidationWidget.h"

#include "FormatConversion.h"
#include "NodeDetailWindow.hpp"
#include "NodeUtils.h"
#include "TSXBatchImportNode.h"

#include "gdal_priv.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QFutureWatcher>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace QtNodes {
namespace {

const QString kPass = QStringLiteral("PASS");
const QString kFailed = QStringLiteral("FAILED");
const QString kNotVerifiable = QStringLiteral("NOT_VERIFIABLE");
const QString kNotImported = QStringLiteral("NOT_IMPORTED");
const QString kNotConsumed = QStringLiteral("NOT_CONSUMED");

struct TsxValidationCheck
{
    QString group;
    QString name;
    QString sourceValue;
    QString h5Value;
    QString status;
};

struct TsxSourceFile
{
    QString relativePath;
    QString category;
    QString mapping;
    QString status;
};

struct TsxSourceProduct
{
    QString xmlPath;
    QString productDirectory;
    QString cosarPath;
    QString georefPath;
    QString parseError;
    QHash<QString, QString> fields;
    QVector<QVector<double>> stateVectors;
    QVector<QVector<double>> gcps;
    QVector<QVector<double>> combinedDoppler;
    QVector<TsxSourceFile> files;
};

struct TsxValidationReport
{
    QString h5Path;
    QString sourceXmlPath;
    QString status;
    QString summary;
    QVector<TsxValidationCheck> checks;
    QVector<TsxSourceFile> sourceFiles;
};

struct TsxValidationSnapshot
{
    QString projectRoot;
    QString nodeName;
    QString runId;
    QStringList h5Paths;
    QStringList committedPaths;
    ExecutionState state = ExecutionState::Idle;
    // This is used only to discard callbacks from a changed node. The journal
    // revision below identifies the persisted committed execution.
    std::uint64_t nodeExecutionRevision = 0;
    std::uint64_t journalExecutionRevision = 0;
    int manifestVersion = 0;
    std::shared_ptr<std::atomic_bool> cancelled;
    bool valid = false;
    QString error;
    QString detailError;
};

QString statusText(const QString& status)
{
    if (status == kPass) return QStringLiteral("[ 通过 ]");
    if (status == kFailed) return QStringLiteral("[ 失败 ]");
    if (status == kNotImported) return QStringLiteral("[ 未导入 ]");
    if (status == kNotConsumed) return QStringLiteral("[ 当前流程未消费 ]");
    return QStringLiteral("[ 无法验证 ]");
}

QString statusName(const QString& status)
{
    if (status == kPass) return QStringLiteral("通过");
    if (status == kFailed) return QStringLiteral("失败");
    if (status == kNotImported) return QStringLiteral("存在未导入内容");
    if (status == kNotConsumed) return QStringLiteral("通过（含未消费辅助内容）");
    return QStringLiteral("无法验证");
}

QColor statusColor(const QString& status)
{
    if (status == kPass) return QColor(16, 185, 129);
    if (status == kFailed) return QColor(239, 68, 68);
    if (status == kNotImported) return QColor(100, 116, 139);
    if (status == kNotConsumed) return QColor(100, 116, 139);
    return QColor(245, 158, 11);
}

bool cancelled(const TsxValidationSnapshot& snapshot)
{
    return snapshot.cancelled && snapshot.cancelled->load();
}

QString formatNumber(double value, int precision = 9)
{
    return QString::number(value, 'g', precision);
}

bool nearlyEqual(double left, double right, double absoluteTolerance = 1e-8,
                 double relativeTolerance = 1e-10)
{
    const double difference = std::abs(left - right);
    return difference <= absoluteTolerance + relativeTolerance * std::max(std::abs(left), std::abs(right));
}

QString sourceFileCategory(const QString& relativePath)
{
    if (relativePath.startsWith(QStringLiteral("IMAGEDATA/"), Qt::CaseInsensitive)) {
        return QStringLiteral("原始复数影像");
    }
    if (relativePath.compare(QStringLiteral("ANNOTATION/GEOREF.xml"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("几何控制网格");
    }
    if (relativePath.startsWith(QStringLiteral("ANNOTATION/DOPPLER_CENTROID_"), Qt::CaseInsensitive)) {
        return QStringLiteral("独立多普勒注释");
    }
    if (relativePath.startsWith(QStringLiteral("ANNOTATION/RFANTPAT_PHASE_"), Qt::CaseInsensitive)) {
        return QStringLiteral("天线相位注释");
    }
    if (relativePath.startsWith(QStringLiteral("AUXRASTER/"), Qt::CaseInsensitive)) {
        return QStringLiteral("辅助栅格");
    }
    if (relativePath.startsWith(QStringLiteral("PREVIEW/"), Qt::CaseInsensitive)) {
        return QStringLiteral("产品预览");
    }
    if (relativePath.startsWith(QStringLiteral("SUPPORT/"), Qt::CaseInsensitive)) {
        return QStringLiteral("支持文件");
    }
    return QStringLiteral("主产品元数据");
}

void addSourceFile(TsxSourceProduct& product, const QString& absolutePath)
{
    const QString relative = QDir(product.productDirectory).relativeFilePath(absolutePath)
        .replace('\\', '/');
    TsxSourceFile file;
    file.relativePath = relative;
    file.category = sourceFileCategory(relative);

    if (QFileInfo(absolutePath).absoluteFilePath() == QFileInfo(product.xmlPath).absoluteFilePath()) {
        file.mapping = QStringLiteral("主 XML：采集、轨道与测量元数据已核对");
        file.status = kPass;
    } else if (relative.startsWith(QStringLiteral("IMAGEDATA/"), Qt::CaseInsensitive)) {
        file.mapping = QStringLiteral("写入 s_re / s_im");
        file.status = kPass;
    } else if (relative.compare(QStringLiteral("ANNOTATION/GEOREF.xml"), Qt::CaseInsensitive) == 0) {
        file.mapping = QStringLiteral("写入 gcps 与定位拟合系数");
        file.status = kPass;
    } else if (relative.startsWith(QStringLiteral("ANNOTATION/DOPPLER_CENTROID_"), Qt::CaseInsensitive)) {
        file.mapping = QStringLiteral("H5 多普勒来自主 XML combinedDoppler；此独立注释当前流程未消费");
        file.status = kNotConsumed;
    } else if (relative.startsWith(QStringLiteral("ANNOTATION/RFANTPAT_PHASE_"), Qt::CaseInsensitive)) {
        file.mapping = QStringLiteral("天线相位注释当前流程未消费");
        file.status = kNotConsumed;
    } else if (relative.startsWith(QStringLiteral("AUXRASTER/"), Qt::CaseInsensitive) ||
               relative.startsWith(QStringLiteral("PREVIEW/"), Qt::CaseInsensitive) ||
               relative.startsWith(QStringLiteral("SUPPORT/"), Qt::CaseInsensitive)) {
        file.mapping = QStringLiteral("当前流程未消费的辅助内容");
        file.status = kNotConsumed;
    } else {
        file.mapping = QStringLiteral("未建立 H5 映射");
        file.status = kNotImported;
    }
    product.files.append(file);
}

bool readTsxGeoref(const QString& georefPath, TsxSourceProduct& product)
{
    QFile georefFile(georefPath);
    if (!georefFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        product.parseError = QStringLiteral("无法读取 GEOREF.xml：%1").arg(georefPath);
        return false;
    }
    QXmlStreamReader reader(&georefFile);
    bool inGridPoint = false;
    QHash<QString, double> gridPoint;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) continue;
        const QString name = reader.name().toString();
        if (name == QStringLiteral("gridPoint")) {
            inGridPoint = true;
            gridPoint.clear();
            continue;
        }
        if (!inGridPoint || (name != QStringLiteral("lon") && name != QStringLiteral("lat") &&
                             name != QStringLiteral("height") && name != QStringLiteral("row") &&
                             name != QStringLiteral("col") && name != QStringLiteral("inc"))) {
            continue;
        }
        bool ok = false;
        const double value = reader.readElementText().trimmed().toDouble(&ok);
        if (!ok) {
            product.parseError = QStringLiteral("GEOREF 字段无法解析：%1").arg(name);
            return false;
        }
        gridPoint.insert(name, value);
        if (gridPoint.size() == 6) {
            QVector<double> orderedPoint;
            orderedPoint << gridPoint.value(QStringLiteral("lon"))
                         << gridPoint.value(QStringLiteral("lat"))
                         << gridPoint.value(QStringLiteral("height"))
                         << gridPoint.value(QStringLiteral("row"))
                         << gridPoint.value(QStringLiteral("col"))
                         << gridPoint.value(QStringLiteral("inc"));
            product.gcps.append(orderedPoint);
            inGridPoint = false;
        }
    }
    if (reader.hasError() || product.gcps.isEmpty()) {
        product.parseError = reader.hasError()
            ? QStringLiteral("GEOREF.xml 格式错误：%1").arg(reader.errorString())
            : QStringLiteral("GEOREF.xml 不包含可用控制点。");
        return false;
    }
    return true;
}

bool readTsxSource(const QString& xmlPath, TsxSourceProduct& product)
{
    product = TsxSourceProduct();
    product.xmlPath = QFileInfo(xmlPath).absoluteFilePath();
    product.productDirectory = QFileInfo(product.xmlPath).absolutePath();
    if (!QFileInfo(product.xmlPath).isFile()) {
        product.parseError = QStringLiteral("源 XML 不存在：%1").arg(product.xmlPath);
        return false;
    }

    QFile xmlFile(product.xmlPath);
    if (!xmlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        product.parseError = QStringLiteral("无法读取源 XML：%1").arg(product.xmlPath);
        return false;
    }

    QXmlStreamReader reader(&xmlFile);
    bool inStateVector = false;
    bool inGridPoint = false;
    bool inCombinedDoppler = false;
    QVector<double> stateVector;
    QVector<double> gridPoint;
    QVector<double> combinedDoppler;
    QStringList acquisitionTimes;

    const QSet<QString> scalarNames = QSet<QString>()
        << QStringLiteral("mission") << QStringLiteral("productVariant")
        << QStringLiteral("projection") << QStringLiteral("imageDataType")
        << QStringLiteral("imageDataFormat") << QStringLiteral("polarisation")
        << QStringLiteral("imagingMode") << QStringLiteral("lookDirection")
        << QStringLiteral("orbitDirection") << QStringLiteral("numberOfRows")
        << QStringLiteral("numberOfColumns")
        << QStringLiteral("incidenceAngle") << QStringLiteral("headingAngle")
        << QStringLiteral("commonPRF") << QStringLiteral("azimuthResolution")
        << QStringLiteral("slantRangeResolution") << QStringLiteral("projectedSpacingAzimuth")
        << QStringLiteral("commonRSF") << QStringLiteral("filename");

    bool inInstrument = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement() && reader.name() == QStringLiteral("instrument")) {
            inInstrument = false;
            continue;
        }
        if (!reader.isStartElement()) {
            continue;
        }

        const QString name = reader.name().toString();
        if (name == QStringLiteral("instrument")) {
            inInstrument = true;
            continue;
        }
        if (name == QStringLiteral("stateVec")) {
            inStateVector = true;
            stateVector.clear();
            continue;
        }
        if (name == QStringLiteral("gridPoint")) {
            inGridPoint = true;
            gridPoint.clear();
            continue;
        }
        if (name == QStringLiteral("combinedDoppler")) {
            inCombinedDoppler = true;
            combinedDoppler.clear();
            continue;
        }

        if (inStateVector && name == QStringLiteral("timeGPSFraction")) {
            bool ok = false;
            const double fraction = reader.readElementText().trimmed().toDouble(&ok);
            if (!ok || stateVector.isEmpty()) {
                product.parseError = QStringLiteral("轨道 GPS 小数时间无法解析。");
                return false;
            }
            stateVector[0] += fraction;
            continue;
        }

        if (inStateVector && (name == QStringLiteral("timeGPS") || name == QStringLiteral("posX") ||
                              name == QStringLiteral("posY") || name == QStringLiteral("posZ") ||
                              name == QStringLiteral("velX") || name == QStringLiteral("velY") ||
                              name == QStringLiteral("velZ"))) {
            bool ok = false;
            const double value = reader.readElementText().trimmed().toDouble(&ok);
            if (!ok) {
                product.parseError = QStringLiteral("轨道字段无法解析：%1").arg(name);
                return false;
            }
            stateVector.append(value);
            if (stateVector.size() == 7) {
                product.stateVectors.append(stateVector);
                inStateVector = false;
            }
            continue;
        }

        if (inGridPoint && (name == QStringLiteral("lon") || name == QStringLiteral("lat") ||
                            name == QStringLiteral("height") || name == QStringLiteral("row") ||
                            name == QStringLiteral("col") || name == QStringLiteral("inc"))) {
            bool ok = false;
            const double value = reader.readElementText().trimmed().toDouble(&ok);
            if (!ok) {
                product.parseError = QStringLiteral("GEOREF 字段无法解析：%1").arg(name);
                return false;
            }
            gridPoint.append(value);
            if (gridPoint.size() == 6) {
                product.gcps.append(gridPoint);
                inGridPoint = false;
            }
            continue;
        }

        if (inCombinedDoppler && (name == QStringLiteral("referencePoint") || name == QStringLiteral("coefficient"))) {
            bool ok = false;
            const double value = reader.readElementText().trimmed().toDouble(&ok);
            if (!ok) {
                product.parseError = QStringLiteral("主 XML 多普勒字段无法解析：%1").arg(name);
                return false;
            }
            combinedDoppler.append(value);
            if (combinedDoppler.size() == 4) {
                product.combinedDoppler.append(combinedDoppler);
                inCombinedDoppler = false;
            }
            continue;
        }

        if (name == QStringLiteral("timeUTC")) {
            acquisitionTimes.append(reader.readElementText().trimmed());
            continue;
        }
        if (name == QStringLiteral("centerFrequency")) {
            const QString value = reader.readElementText().trimmed();
            if (inInstrument || !product.fields.contains(name)) {
                product.fields.insert(name, value);
            }
            continue;
        }
        if (scalarNames.contains(name)) {
            const QString value = reader.readElementText().trimmed();
            if (name == QStringLiteral("filename")) {
                // The product XML lists itself and annotation filenames before
                // the actual complex image. Only the COSAR filename is the
                // source raster consumed by TSX2h5.
                if (value.endsWith(QStringLiteral(".cos"), Qt::CaseInsensitive)) {
                    product.fields.insert(QStringLiteral("cosarFilename"), value);
                }
            } else if (!product.fields.contains(name)) {
                product.fields.insert(name, value);
            }
        }
    }
    if (reader.hasError()) {
        product.parseError = QStringLiteral("源 XML 格式错误：%1").arg(reader.errorString());
        return false;
    }

    if (acquisitionTimes.size() >= 2) {
        product.fields.insert(QStringLiteral("acquisitionStart"), acquisitionTimes.at(0));
        product.fields.insert(QStringLiteral("acquisitionStop"), acquisitionTimes.at(1));
    }
    const QString imageName = product.fields.value(QStringLiteral("cosarFilename"));
    product.cosarPath = QDir(product.productDirectory).absoluteFilePath(
        QStringLiteral("IMAGEDATA/") + imageName);
    product.georefPath = QDir(product.productDirectory).absoluteFilePath(QStringLiteral("ANNOTATION/GEOREF.xml"));
    if (!readTsxGeoref(product.georefPath, product)) {
        return false;
    }

    QDirIterator files(product.productDirectory, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        addSourceFile(product, files.next());
    }
    std::sort(product.files.begin(), product.files.end(), [](const TsxSourceFile& left, const TsxSourceFile& right) {
        return left.relativePath.compare(right.relativePath, Qt::CaseInsensitive) < 0;
    });

    const bool isMainProduct = product.fields.value(QStringLiteral("productVariant")) == QStringLiteral("SSC") &&
        product.fields.value(QStringLiteral("imageDataType")) == QStringLiteral("COMPLEX") &&
        product.fields.value(QStringLiteral("imageDataFormat")) == QStringLiteral("COSAR");
    if (!isMainProduct) {
        product.parseError = QStringLiteral("所选 XML 不是 TerraSAR-X SSC 主产品 XML。");
        return false;
    }
    return true;
}

bool readH5String(const QString& h5Path, const QString& dataset, QString& value, QString& error)
{
    std::string raw;
    if (!NodeUtils::readStringFromH5(h5Path, dataset, raw, &error)) {
        return false;
    }
    value = QString::fromStdString(raw).trimmed();
    return true;
}

void addCheck(TsxValidationReport& report, const QString& group, const QString& name,
              const QString& sourceValue, const QString& h5Value, const QString& status)
{
    TsxValidationCheck check;
    check.group = group;
    check.name = name;
    check.sourceValue = sourceValue;
    check.h5Value = h5Value;
    check.status = status;
    report.checks.append(check);
}

QString matrixShape(const cv::Mat& matrix)
{
    return QStringLiteral("%1 x %2").arg(matrix.rows).arg(matrix.cols);
}

void addPixelSampleCheck(TsxValidationReport& report, const TsxSourceProduct& source,
                         const QString& h5Path, int rows, int columns)
{
    const QString group = QStringLiteral("复数影像");
    if (!QFileInfo(source.cosarPath).isFile()) {
        addCheck(report, group, QStringLiteral("COSAR 与 H5 像素抽样（5 点）"),
                 QStringLiteral("未找到 %1").arg(QFileInfo(source.cosarPath).fileName()),
                 QStringLiteral("未执行"), kNotVerifiable);
        return;
    }
    if (rows < 1 || columns < 1) {
        addCheck(report, group, QStringLiteral("COSAR 与 H5 像素抽样（5 点）"),
                 QStringLiteral("原始 COSAR 可用"), QStringLiteral("H5 尺寸无效"), kFailed);
        return;
    }

    GDALAllRegister();
    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(source.cosarPath.toLocal8Bit().constData(), GA_ReadOnly));
    if (!dataset || dataset->GetRasterCount() != 1) {
        if (dataset) GDALClose(dataset);
        addCheck(report, group, QStringLiteral("COSAR 与 H5 像素抽样（5 点）"),
                 QStringLiteral("无法按复数单波段读取 COSAR"), QStringLiteral("未执行"), kNotVerifiable);
        return;
    }

    GDALRasterBand* band = dataset->GetRasterBand(1);
    const QVector<QPoint> points = QVector<QPoint>()
        << QPoint(0, 0) << QPoint(columns - 1, 0) << QPoint(0, rows - 1)
        << QPoint(columns - 1, rows - 1) << QPoint(columns / 2, rows / 2);
    int matched = 0;
    QStringList diagnostics;
    FormatConversion conversion;
    NodeUtils::Hdf5Locker locker(h5Path, 200);
    if (!locker.isLocked()) {
        GDALClose(dataset);
        addCheck(report, group, QStringLiteral("COSAR 与 H5 像素抽样（5 点）"),
                 QStringLiteral("原始 COSAR 可用"), QStringLiteral("HDF5 访问锁超时"), kNotVerifiable);
        return;
    }

    for (int index = 0; index < points.size(); ++index) {
        const QPoint point = points.at(index);
        short sourceValue[2] = {0, 0};
        cv::Mat realPart;
        cv::Mat imaginaryPart;
        const bool sourceOk = band && band->RasterIO(GF_Read, point.x(), point.y(), 1, 1,
            sourceValue, 1, 1, GDT_CInt16, 0, 0) == CE_None;
        const bool h5Ok = conversion.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_re",
            point.y(), point.x(), 1, 1, realPart) == 0 &&
            conversion.read_subarray_from_h5(h5Path.toLocal8Bit().constData(), "s_im",
                point.y(), point.x(), 1, 1, imaginaryPart) == 0 &&
            !realPart.empty() && !imaginaryPart.empty();
        if (!sourceOk || !h5Ok) {
            diagnostics.append(QStringLiteral("点%1读取失败").arg(index + 1));
            continue;
        }
        realPart.convertTo(realPart, CV_64F);
        imaginaryPart.convertTo(imaginaryPart, CV_64F);
        const double realValue = realPart.at<double>(0, 0);
        const double imaginaryValue = imaginaryPart.at<double>(0, 0);
        if (nearlyEqual(sourceValue[0], realValue, 0.0, 0.0) &&
            nearlyEqual(sourceValue[1], imaginaryValue, 0.0, 0.0)) {
            ++matched;
        } else {
            diagnostics.append(QStringLiteral("点%1：COSAR[%2,%3]，H5[%4,%5]")
                .arg(index + 1).arg(sourceValue[0]).arg(sourceValue[1])
                .arg(formatNumber(realValue)).arg(formatNumber(imaginaryValue)));
        }
    }
    GDALClose(dataset);
    addCheck(report, group, QStringLiteral("COSAR 与 H5 像素抽样（5 点）"),
             QStringLiteral("5 个 CInt16 复数像素"),
             matched == points.size() ? QStringLiteral("5/5 实部和虚部逐值一致")
                 : QStringLiteral("%1/5 一致；%2").arg(matched).arg(diagnostics.join(QStringLiteral("；"))),
             matched == points.size() ? kPass : kFailed);
}

TsxValidationReport validateSelection(const TsxValidationSnapshot& snapshot,
                                      const QString& h5Path, const QString& sourceXmlPath)
{
    TsxValidationReport report;
    report.h5Path = h5Path;
    report.sourceXmlPath = sourceXmlPath;
    if (!snapshot.valid) {
        report.status = kNotVerifiable;
        report.summary = snapshot.error;
        return report;
    }

    if (cancelled(snapshot)) {
        report.status = kNotVerifiable;
        report.summary = QStringLiteral("验证已取消。");
        return report;
    }
    if (!QFileInfo(h5Path).isFile()) {
        addCheck(report, QStringLiteral("已提交输出"), QStringLiteral("H5 文件可用性"),
                 QStringLiteral("无源端字段"), QStringLiteral("manifest 声明的 H5 文件不存在"), kFailed);
        report.status = kFailed;
        report.summary = QStringLiteral("已提交 H5 文件不存在。");
        return report;
    }

    int realRows = 0;
    int realColumns = 0;
    int imaginaryRows = 0;
    int imaginaryColumns = 0;
    QString realError;
    QString imaginaryError;
    const bool realOk = NodeUtils::probeH5DatasetMetadata(h5Path, QStringLiteral("s_re"),
        &realRows, &realColumns, &realError);
    const bool imaginaryOk = NodeUtils::probeH5DatasetMetadata(h5Path, QStringLiteral("s_im"),
        &imaginaryRows, &imaginaryColumns, &imaginaryError);
    const bool complexShapeOk = realOk && imaginaryOk && realRows > 0 && realColumns > 0 &&
        realRows == imaginaryRows && realColumns == imaginaryColumns;
    addCheck(report, QStringLiteral("复数影像"), QStringLiteral("H5 复数数据集可用性"),
             QStringLiteral("由节点 XML 清单自动匹配"),
             complexShapeOk ? QStringLiteral("s_re/s_im=%1 x %2").arg(realRows).arg(realColumns)
                 : QStringLiteral("s_re：%1；s_im：%2")
                    .arg(realOk ? QStringLiteral("尺寸无效或不一致") : realError,
                         imaginaryOk ? QStringLiteral("尺寸无效或不一致") : imaginaryError),
             complexShapeOk ? kPass : kFailed);
    if (cancelled(snapshot)) {
        report.status = kNotVerifiable;
        report.summary = QStringLiteral("验证已取消。");
        return report;
    }
    if (sourceXmlPath.isEmpty()) {
        addCheck(report, QStringLiteral("产品身份"), QStringLiteral("XML 与 H5 对应关系"),
                 QStringLiteral("节点已保存 XML 清单中没有此输出名"),
                 QFileInfo(h5Path).fileName(), kFailed);
        report.status = kFailed;
        report.summary = QStringLiteral("节点 XML 清单无法与已提交 H5 建立一一对应关系。");
        return report;
    }

    TsxSourceProduct source;
    if (!readTsxSource(sourceXmlPath, source)) {
        addCheck(report, QStringLiteral("源产品"), QStringLiteral("源 XML 解析"),
                 sourceXmlPath, source.parseError, kFailed);
        report.status = kFailed;
        report.summary = QStringLiteral("源产品 XML 无法用于核对。");
        return report;
    }
    report.sourceFiles = source.files;

    QString h5FileType;
    QString h5Sensor;
    QString h5Polarization;
    QString h5Mode;
    QString h5LookSide;
    QString h5OrbitDirection;
    QString h5Error;
    const bool identityReadable = readH5String(h5Path, QStringLiteral("file_type"), h5FileType, h5Error) &&
        readH5String(h5Path, QStringLiteral("sensor"), h5Sensor, h5Error) &&
        readH5String(h5Path, QStringLiteral("polarization"), h5Polarization, h5Error) &&
        readH5String(h5Path, QStringLiteral("imaging_mode"), h5Mode, h5Error) &&
        readH5String(h5Path, QStringLiteral("lookside"), h5LookSide, h5Error) &&
        readH5String(h5Path, QStringLiteral("orbit_dir"), h5OrbitDirection, h5Error);
    const QString sourceIdentity = QStringLiteral("%1；%2；%3；%4")
        .arg(source.fields.value(QStringLiteral("productVariant")), source.fields.value(QStringLiteral("projection")),
             source.fields.value(QStringLiteral("imageDataType")), source.fields.value(QStringLiteral("imageDataFormat")));
    const bool productStructureOk = identityReadable && h5FileType == QStringLiteral("SLC") && complexShapeOk;
    addCheck(report, QStringLiteral("产品身份"), QStringLiteral("SSC 复数产品结构"), sourceIdentity,
             identityReadable ? QStringLiteral("file_type=%1；s_re/s_im=%2 x %3")
                 .arg(h5FileType).arg(realRows).arg(realColumns) : h5Error,
             productStructureOk ? kPass : kFailed);

    const QString sourceIdentityFields = QStringLiteral("传感器=%1；极化=%2；模式=%3；视向=%4；轨向=%5")
        .arg(source.fields.value(QStringLiteral("mission")), source.fields.value(QStringLiteral("polarisation")),
             source.fields.value(QStringLiteral("imagingMode")), source.fields.value(QStringLiteral("lookDirection")),
             source.fields.value(QStringLiteral("orbitDirection")));
    const QString h5IdentityFields = identityReadable ? QStringLiteral("传感器=%1；极化=%2；模式=%3；视向=%4；轨向=%5")
        .arg(h5Sensor, h5Polarization, h5Mode, h5LookSide, h5OrbitDirection) : h5Error;
    const bool identityOk = identityReadable && h5Sensor == source.fields.value(QStringLiteral("mission")) &&
        h5Polarization == source.fields.value(QStringLiteral("polarisation")) &&
        h5Mode == source.fields.value(QStringLiteral("imagingMode")) &&
        h5LookSide == source.fields.value(QStringLiteral("lookDirection")) &&
        h5OrbitDirection == source.fields.value(QStringLiteral("orbitDirection"));
    addCheck(report, QStringLiteral("产品身份"), QStringLiteral("传感器、极化、模式与轨道方向"),
             sourceIdentityFields, h5IdentityFields, identityOk ? kPass : kFailed);

    QString h5Start;
    QString h5Stop;
    const bool timesReadable = readH5String(h5Path, QStringLiteral("acquisition_start_time"), h5Start, h5Error) &&
        readH5String(h5Path, QStringLiteral("acquisition_stop_time"), h5Stop, h5Error);
    const QString sourceTimes = QStringLiteral("开始=%1；结束=%2")
        .arg(source.fields.value(QStringLiteral("acquisitionStart")), source.fields.value(QStringLiteral("acquisitionStop")));
    const QString h5Times = timesReadable ? QStringLiteral("开始=%1；结束=%2").arg(h5Start, h5Stop) : h5Error;
    const bool timesOk = timesReadable && h5Start == source.fields.value(QStringLiteral("acquisitionStart")) &&
        h5Stop == source.fields.value(QStringLiteral("acquisitionStop"));
    addCheck(report, QStringLiteral("采集与采样"), QStringLiteral("采集开始与结束时间"),
             sourceTimes, h5Times, timesOk ? kPass : kFailed);

    bool sourceRowsOk = false;
    bool sourceColumnsOk = false;
    const int sourceRows = source.fields.value(QStringLiteral("numberOfRows")).toInt(&sourceRowsOk);
    const int sourceColumns = source.fields.value(QStringLiteral("numberOfColumns")).toInt(&sourceColumnsOk);
    int h5Rows = 0;
    int h5Columns = 0;
    const bool dimensionsReadable = NodeUtils::readScalarFromH5(h5Path, QStringLiteral("azimuth_len"), h5Rows, &h5Error) &&
        NodeUtils::readScalarFromH5(h5Path, QStringLiteral("range_len"), h5Columns, &h5Error);
    const bool dimensionsOk = sourceRowsOk && sourceColumnsOk && dimensionsReadable && complexShapeOk &&
        sourceRows == h5Rows && sourceColumns == h5Columns && h5Rows == realRows && h5Columns == realColumns;
    addCheck(report, QStringLiteral("采集与采样"), QStringLiteral("行列数与复数影像尺寸"),
             QStringLiteral("XML=%1 x %2").arg(sourceRows).arg(sourceColumns),
             dimensionsReadable ? QStringLiteral("azimuth_len/range_len=%1 x %2；s_re/s_im=%3 x %4")
                 .arg(h5Rows).arg(h5Columns).arg(realRows).arg(realColumns) : h5Error,
             dimensionsOk ? kPass : kFailed);

    bool sourceFrequencyOk = false;
    bool sourcePrfOk = false;
    bool sourceIncidenceOk = false;
    const double sourceFrequency = source.fields.value(QStringLiteral("centerFrequency")).toDouble(&sourceFrequencyOk);
    const double sourcePrf = source.fields.value(QStringLiteral("commonPRF")).toDouble(&sourcePrfOk);
    const double sourceIncidence = source.fields.value(QStringLiteral("incidenceAngle")).toDouble(&sourceIncidenceOk);
    double h5Frequency = 0.0;
    double h5Prf = 0.0;
    double h5Incidence = 0.0;
    const bool measurementReadable = NodeUtils::readScalarFromH5(h5Path, QStringLiteral("carrier_frequency"), h5Frequency, &h5Error) &&
        NodeUtils::readScalarFromH5(h5Path, QStringLiteral("prf"), h5Prf, &h5Error) &&
        NodeUtils::readScalarFromH5(h5Path, QStringLiteral("inc_center"), h5Incidence, &h5Error);
    const bool measurementOk = sourceFrequencyOk && sourcePrfOk && sourceIncidenceOk && measurementReadable &&
        nearlyEqual(sourceFrequency, h5Frequency, 1e-3) && nearlyEqual(sourcePrf, h5Prf, 1e-6) &&
        nearlyEqual(sourceIncidence, h5Incidence, 1e-10);
    addCheck(report, QStringLiteral("采集与采样"), QStringLiteral("载波频率、PRF 与中心入射角"),
             QStringLiteral("%1 Hz；%2 Hz；%3 度").arg(formatNumber(sourceFrequency)).arg(formatNumber(sourcePrf)).arg(formatNumber(sourceIncidence)),
             measurementReadable ? QStringLiteral("%1 Hz；%2 Hz；%3 度").arg(formatNumber(h5Frequency)).arg(formatNumber(h5Prf)).arg(formatNumber(h5Incidence)) : h5Error,
             measurementOk ? kPass : kFailed);

    addPixelSampleCheck(report, source, h5Path, realRows, realColumns);
    if (cancelled(snapshot)) {
        report.status = kNotVerifiable;
        report.summary = QStringLiteral("验证已取消。");
        return report;
    }

    cv::Mat h5Gcps;
    const bool h5GcpsReadable = NodeUtils::readMatFromH5(h5Path, QStringLiteral("gcps"), h5Gcps, CV_64F, &h5Error);
    double maximumGcpDifference = 0.0;
    bool gcpsOk = h5GcpsReadable && h5Gcps.rows == source.gcps.size() && h5Gcps.cols == 6;
    if (gcpsOk) {
        for (int row = 0; row < h5Gcps.rows && gcpsOk; ++row) {
            for (int column = 0; column < h5Gcps.cols; ++column) {
                const double difference = std::abs(source.gcps.at(row).at(column) - h5Gcps.at<double>(row, column));
                maximumGcpDifference = std::max(maximumGcpDifference, difference);
                if (difference != 0.0) gcpsOk = false;
            }
        }
    }
    addCheck(report, QStringLiteral("几何定位"), QStringLiteral("GEOREF 控制点（全部）"),
             QStringLiteral("%1 点，lon/lat/height/row/col/inc").arg(source.gcps.size()),
             h5GcpsReadable ? QStringLiteral("%1；最大绝对差=%2").arg(matrixShape(h5Gcps)).arg(formatNumber(maximumGcpDifference)) : h5Error,
             gcpsOk ? kPass : kFailed);

    cv::Mat h5Orbit;
    const bool h5OrbitReadable = NodeUtils::readMatFromH5(h5Path, QStringLiteral("state_vec"), h5Orbit, CV_64F, &h5Error);
    double maximumPositionVelocityDifference = 0.0;
    double minimumTimeDifference = 0.0;
    double maximumTimeDifference = 0.0;
    bool orbitPositionVelocityOk = h5OrbitReadable && h5Orbit.rows == source.stateVectors.size() && h5Orbit.cols == 7;
    bool orbitTimesOk = orbitPositionVelocityOk;
    if (orbitPositionVelocityOk) {
        for (int row = 0; row < h5Orbit.rows; ++row) {
            const double timeDifference = h5Orbit.at<double>(row, 0) - source.stateVectors.at(row).at(0);
            if (row == 0) minimumTimeDifference = maximumTimeDifference = timeDifference;
            else {
                minimumTimeDifference = std::min(minimumTimeDifference, timeDifference);
                maximumTimeDifference = std::max(maximumTimeDifference, timeDifference);
            }
            if (timeDifference != 0.0) orbitTimesOk = false;
            for (int column = 1; column < h5Orbit.cols; ++column) {
                const double difference = std::abs(source.stateVectors.at(row).at(column) - h5Orbit.at<double>(row, column));
                maximumPositionVelocityDifference = std::max(maximumPositionVelocityDifference, difference);
                if (difference != 0.0) orbitPositionVelocityOk = false;
            }
        }
    }
    addCheck(report, QStringLiteral("轨道与多普勒"), QStringLiteral("轨道位置与速度（全部 state_vec）"),
             QStringLiteral("%1 x 7").arg(source.stateVectors.size()),
             h5OrbitReadable ? QStringLiteral("%1；位置/速度最大绝对差=%2").arg(matrixShape(h5Orbit)).arg(formatNumber(maximumPositionVelocityDifference)) : h5Error,
             orbitPositionVelocityOk ? kPass : kFailed);
    addCheck(report, QStringLiteral("轨道与多普勒"), QStringLiteral("轨道 GPS 时间（全部 state_vec）"),
             QStringLiteral("XML timeGPS，%1 条").arg(source.stateVectors.size()),
             h5OrbitReadable ? QStringLiteral("H5 - XML：最小=%1 s，最大=%2 s")
                 .arg(formatNumber(minimumTimeDifference)).arg(formatNumber(maximumTimeDifference)) : h5Error,
             orbitTimesOk ? kPass : kFailed);

    cv::Mat h5Doppler;
    const bool h5DopplerReadable = NodeUtils::readMatFromH5(h5Path, QStringLiteral("doppler_centroid"), h5Doppler, CV_64F, &h5Error);
    double maximumDopplerDifference = 0.0;
    bool dopplerOk = h5DopplerReadable && h5Doppler.rows == source.combinedDoppler.size() && h5Doppler.cols == 4;
    if (dopplerOk) {
        for (int row = 0; row < h5Doppler.rows && dopplerOk; ++row) {
            for (int column = 0; column < h5Doppler.cols; ++column) {
                const double difference = std::abs(source.combinedDoppler.at(row).at(column) - h5Doppler.at<double>(row, column));
                maximumDopplerDifference = std::max(maximumDopplerDifference, difference);
                if (difference != 0.0) dopplerOk = false;
            }
        }
    }
    addCheck(report, QStringLiteral("轨道与多普勒"), QStringLiteral("主 XML combinedDoppler（全部）"),
             QStringLiteral("%1 x 4").arg(source.combinedDoppler.size()),
             h5DopplerReadable ? QStringLiteral("%1；最大绝对差=%2").arg(matrixShape(h5Doppler)).arg(formatNumber(maximumDopplerDifference)) : h5Error,
             dopplerOk ? kPass : kFailed);

    bool hasFailure = false;
    bool hasNotImported = false;
    bool hasNotConsumed = false;
    bool hasNotVerifiable = false;
    for (const TsxValidationCheck& check : report.checks) {
        hasFailure = hasFailure || check.status == kFailed;
        hasNotImported = hasNotImported || check.status == kNotImported;
        hasNotVerifiable = hasNotVerifiable || check.status == kNotVerifiable;
    }
    for (const TsxSourceFile& file : report.sourceFiles) {
        hasNotImported = hasNotImported || file.status == kNotImported;
        hasNotConsumed = hasNotConsumed || file.status == kNotConsumed;
    }
    if (hasFailure) {
        report.status = kFailed;
        report.summary = QStringLiteral("选定源产品与 H5 的关键数据核对发现差异。");
    } else if (hasNotImported) {
        report.status = kNotImported;
        report.summary = QStringLiteral("源产品中仍有未建立 H5 映射的内容。");
    } else if (hasNotVerifiable) {
        report.status = kNotVerifiable;
        report.summary = QStringLiteral("部分字段缺少可比较证据。");
    } else {
        report.status = kPass;
        report.summary = hasNotConsumed
            ? QStringLiteral("选定源产品的核心导入数据核对通过；辅助内容未被当前流程消费。")
            : QStringLiteral("选定源产品的核心导入数据核对通过。");
    }
    return report;
}

QString outputNameForXml(const QString& xmlPath)
{
    const QString baseName = QFileInfo(xmlPath).baseName();
    const int marker = baseName.lastIndexOf(QLatin1Char('T'));
    return marker >= 8 ? baseName.mid(marker - 8, 8) + QStringLiteral(".h5") : QString();
}

class TSXBatchImportValidationWidget : public QWidget
{
public:
    explicit TSXBatchImportValidationWidget(TSXBatchImportNode* node, QWidget* parent)
        : QWidget(parent)
        , m_node(node)
    {
        setupUi();
        refresh();
    }

    ~TSXBatchImportValidationWidget() override
    {
        ++m_epoch;
        if (m_cancelToken) m_cancelToken->store(true);
        if (m_activeWatcher) m_activeWatcher->cancel();
    }

private:
    void setupUi()
    {
        const bool dark = NodeDetailWindow::isDarkTheme(this);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(12);

        auto* statusCard = new QFrame(this);
        statusCard->setFrameShape(QFrame::StyledPanel);
        statusCard->setStyleSheet(dark
            ? QStringLiteral("QFrame { background-color: rgba(55, 65, 81, 0.4); border: 1px solid #374151; border-radius: 6px; padding: 12px; }")
            : QStringLiteral("QFrame { background-color: rgba(243, 244, 246, 0.6); border: 1px solid #E5E7EB; border-radius: 6px; padding: 12px; }"));
        auto* cardLayout = new QVBoxLayout(statusCard);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(4);
        auto* titleRow = new QHBoxLayout();
        m_title = new QLabel(QStringLiteral("导入结果验证"), statusCard);
        m_title->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: bold; color: %1;")
            .arg(dark ? QStringLiteral("#93C5FD") : QStringLiteral("#1D4ED8")));
        m_refresh = new QPushButton(QStringLiteral("刷新"), statusCard);
        m_refresh->setToolTip(QStringLiteral("重新读取已提交 manifest 快照"));
        titleRow->addWidget(m_title);
        titleRow->addStretch(1);
        titleRow->addWidget(m_refresh);
        cardLayout->addLayout(titleRow);
        m_summary = new QLabel(statusCard);
        m_summary->setWordWrap(true);
        m_summary->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;")
            .arg(dark ? QStringLiteral("#D1D5DB") : QStringLiteral("#4B5563")));
        cardLayout->addWidget(m_summary);
        layout->addWidget(statusCard);

        auto* splitter = new QSplitter(Qt::Vertical, this);
        m_productList = new QListWidget(splitter);
        m_productList->setFixedHeight(68);
        m_productList->setSelectionMode(QAbstractItemView::SingleSelection);
        splitter->addWidget(m_productList);

        m_table = new QTableWidget(splitter);
        m_table->setColumnCount(4);
        m_table->setHorizontalHeaderLabels(QStringList() << QStringLiteral("检查项") << QStringLiteral("源产品")
            << QStringLiteral("H5 输出") << QStringLiteral("状态"));
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_table->verticalHeader()->setVisible(false);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        splitter->addWidget(m_table);

        splitter->setChildrenCollapsible(false);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes(QList<int>() << 68 << 700);
        layout->addWidget(splitter, 1);

        connect(m_refresh, &QPushButton::clicked, this, [this]() { refresh(); });
        connect(m_productList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) { onProductSelected(current); });
    }

    TsxValidationSnapshot captureSnapshot() const
    {
        TsxValidationSnapshot snapshot;
        if (!m_node) {
            snapshot.error = QStringLiteral("验证对象已销毁。");
            return snapshot;
        }
        snapshot.state = m_node->executionState();
        snapshot.nodeExecutionRevision = m_node->executionRevision();
        if (snapshot.state != ExecutionState::Completed && snapshot.state != ExecutionState::Warning) {
            snapshot.error = QStringLiteral("节点未完成执行，无法读取已提交结果快照。");
            return snapshot;
        }
        snapshot.projectRoot = m_node->projectPath();
        snapshot.nodeName = m_node->validationOutputNodeName();
        QString manifestError;
        if (snapshot.projectRoot.isEmpty() || snapshot.nodeName.isEmpty() ||
            !NodeUtils::loadCommittedOutputManifestReadOnly(snapshot.projectRoot, snapshot.nodeName,
                snapshot.committedPaths, snapshot.runId, snapshot.journalExecutionRevision,
                snapshot.manifestVersion, &manifestError)) {
            snapshot.error = QStringLiteral("无法读取稳定的已提交结果快照。");
            snapshot.detailError = manifestError;
            return snapshot;
        }
        for (const QString& path : snapshot.committedPaths) {
            if (QFileInfo(path).suffix().compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0) {
                snapshot.h5Paths.append(path);
            }
        }
        if (snapshot.h5Paths.isEmpty()) {
            snapshot.error = QStringLiteral("已提交 manifest 未包含 H5 主产品。");
            return snapshot;
        }
        snapshot.valid = true;
        return snapshot;
    }

    void refresh()
    {
        ++m_epoch;
        if (m_cancelToken) m_cancelToken->store(true);
        if (m_activeWatcher) m_activeWatcher->cancel();
        m_snapshot = captureSnapshot();
        m_hasSnapshot = m_snapshot.valid;
        if (!m_snapshot.valid) {
            showNotVerifiable(m_snapshot.error, m_snapshot.detailError);
            return;
        }

        QSignalBlocker productBlocker(m_productList);
        m_productList->clear();
        rebuildSourceMapping();
        for (const QString& h5Path : m_snapshot.h5Paths) {
            auto* item = new QListWidgetItem(QFileInfo(h5Path).fileName(), m_productList);
            item->setData(Qt::UserRole, h5Path);
            item->setToolTip(h5Path);
        }
        if (m_productList->count() > 0) {
            m_productList->setCurrentRow(0);
        }
        m_title->setText(QStringLiteral("导入结果验证"));
        m_summary->setText(QStringLiteral("选择 H5 查看它与节点已保存 XML 输入的逐项核对结果。"));
        startValidation();
    }

    void rebuildSourceMapping()
    {
        m_sourceByH5.clear();
        m_sourceMappingErrors.clear();
        QHash<QString, QStringList> sourcesByOutput;
        if (m_node) {
            for (const QString& xmlPath : m_node->validationSourceXmlPaths()) {
                const QString outputName = outputNameForXml(xmlPath).toCaseFolded();
                if (!outputName.isEmpty()) {
                    sourcesByOutput[outputName].append(QFileInfo(xmlPath).absoluteFilePath());
                }
            }
        }
        for (const QString& h5Path : m_snapshot.h5Paths) {
            const QString outputName = QFileInfo(h5Path).fileName().toCaseFolded();
            const QStringList sources = sourcesByOutput.value(outputName);
            if (sources.size() == 1) {
                m_sourceByH5.insert(h5Path, sources.first());
            } else if (sources.isEmpty()) {
                m_sourceMappingErrors.insert(h5Path,
                    QStringLiteral("节点已保存 XML 清单中没有与 %1 对应的输入文件。").arg(QFileInfo(h5Path).fileName()));
            } else {
                m_sourceMappingErrors.insert(h5Path,
                    QStringLiteral("多个 XML 会生成同一输出 %1，不能进行一一核对。").arg(QFileInfo(h5Path).fileName()));
            }
        }
    }

    QString selectedH5Path() const
    {
        QListWidgetItem* item = m_productList->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    }

    QString selectedSourcePath() const
    {
        return m_sourceByH5.value(selectedH5Path());
    }

    void onProductSelected(QListWidgetItem* current)
    {
        if (!current) return;
        startValidation();
    }

    void startValidation()
    {
        if (!m_hasSnapshot) return;
        const QString h5Path = selectedH5Path();
        if (h5Path.isEmpty()) return;
        ++m_epoch;
        if (m_cancelToken) m_cancelToken->store(true);
        if (m_activeWatcher) m_activeWatcher->cancel();
        const std::uint64_t epoch = m_epoch;
        m_cancelToken = std::make_shared<std::atomic_bool>(false);
        TsxValidationSnapshot snapshot = m_snapshot;
        snapshot.cancelled = m_cancelToken;
        const QString sourcePath = selectedSourcePath();
        m_title->setText(QStringLiteral("正在核对选定产品..."));
        m_summary->setText(sourcePath.isEmpty()
            ? m_sourceMappingErrors.value(h5Path, QStringLiteral("节点 XML 清单没有可用的对应输入。"))
            : QStringLiteral("正在核对节点已保存 XML 输入与已提交 H5。"));
        m_table->setRowCount(0);
        auto* watcher = new QFutureWatcher<TsxValidationReport>(this);
        m_activeWatcher = watcher;
        connect(watcher, &QFutureWatcher<TsxValidationReport>::finished, this,
            [this, watcher, epoch, h5Path, sourcePath, snapshot]() {
                const TsxValidationReport report = watcher->result();
                if (m_activeWatcher == watcher) m_activeWatcher = nullptr;
                watcher->deleteLater();
                publishReport(report, epoch, h5Path, sourcePath, snapshot);
            });
        watcher->setFuture(QtConcurrent::run([snapshot, h5Path, sourcePath]() {
            return validateSelection(snapshot, h5Path, sourcePath);
        }));
    }

    bool snapshotIsStillCurrent(const TsxValidationSnapshot& snapshot) const
    {
        if (!m_node || m_node->executionRevision() != snapshot.nodeExecutionRevision ||
            (m_node->executionState() != ExecutionState::Completed &&
             m_node->executionState() != ExecutionState::Warning)) {
            return false;
        }
        QStringList currentPaths;
        QString currentRunId;
        std::uint64_t currentRevision = 0;
        int currentManifestVersion = 0;
        QString error;
        if (!NodeUtils::loadCommittedOutputManifestReadOnly(m_node->projectPath(),
                m_node->validationOutputNodeName(), currentPaths, currentRunId, currentRevision,
                currentManifestVersion, &error) ||
            currentRunId != snapshot.runId ||
            currentRevision != snapshot.journalExecutionRevision ||
            currentManifestVersion != snapshot.manifestVersion ||
            currentPaths.size() != snapshot.committedPaths.size()) {
            return false;
        }
        for (int i = 0; i < currentPaths.size(); ++i) {
            if (currentPaths.at(i) != snapshot.committedPaths.at(i)) {
                return false;
            }
        }
        return true;
    }

    void publishReport(const TsxValidationReport& report, std::uint64_t epoch,
                       const QString& h5Path, const QString& sourcePath,
                       const TsxValidationSnapshot& snapshot)
    {
        if (epoch != m_epoch || h5Path != selectedH5Path() || sourcePath != selectedSourcePath()) return;
        if (!snapshotIsStillCurrent(snapshot)) {
            m_title->setText(QStringLiteral("导入结果验证已过期"));
            m_summary->setText(QStringLiteral("验证期间已提交的输出快照、节点状态或文件元数据发生变化。"));
            m_table->setRowCount(0);
            return;
        }

        m_table->setRowCount(0);
        auto addGroupRow = [this](const QString& groupName) {
            const int groupRow = m_table->rowCount();
            m_table->insertRow(groupRow);
            auto* item = new QTableWidgetItem(groupName);
            item->setFont(QFont(QString(), 9, QFont::Bold));
            item->setBackground(NodeDetailWindow::isDarkTheme(this) ? QColor(55, 65, 81, 100) : QColor(243, 244, 246));
            m_table->setItem(groupRow, 0, item);
            m_table->setSpan(groupRow, 0, 1, 4);
        };

        QString group;
        for (const TsxValidationCheck& check : report.checks) {
            if (check.group != group) {
                group = check.group;
                addGroupRow(group);
            }
            const int row = m_table->rowCount();
            m_table->insertRow(row);
            auto* name = new QTableWidgetItem(QStringLiteral("  ") + check.name);
            auto* source = new QTableWidgetItem(check.sourceValue);
            auto* h5 = new QTableWidgetItem(check.h5Value);
            auto* status = new QTableWidgetItem(statusText(check.status));
            source->setToolTip(check.sourceValue);
            h5->setToolTip(check.h5Value);
            status->setForeground(QBrush(statusColor(check.status)));
            status->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(row, 0, name);
            m_table->setItem(row, 1, source);
            m_table->setItem(row, 2, h5);
            m_table->setItem(row, 3, status);
        }

        if (report.sourceFiles.isEmpty()) {
            addGroupRow(QStringLiteral("源产品文件清单"));
            const int row = m_table->rowCount();
            m_table->insertRow(row);
            m_table->setItem(row, 0, new QTableWidgetItem(sourcePath.isEmpty()
                ? QStringLiteral("  节点 XML 清单没有对应源产品") : QStringLiteral("  未读取到源目录内容")));
            auto* status = new QTableWidgetItem(statusText(kNotVerifiable));
            status->setForeground(QBrush(statusColor(kNotVerifiable)));
            status->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(row, 3, status);
        } else {
            QVector<const TsxSourceFile*> verifiedFiles;
            QVector<const TsxSourceFile*> unmappedFiles;
            QVector<const TsxSourceFile*> unverifiableFiles;
            QVector<const TsxSourceFile*> notConsumedFiles;
            for (const TsxSourceFile& file : report.sourceFiles) {
                if (file.status == kPass) {
                    verifiedFiles.append(&file);
                } else if (file.status == kNotImported) {
                    unmappedFiles.append(&file);
                } else if (file.status == kNotConsumed) {
                    notConsumedFiles.append(&file);
                } else {
                    unverifiableFiles.append(&file);
                }
            }

            const auto addSourceFiles = [this, &addGroupRow](const QString& groupName,
                                                               const QVector<const TsxSourceFile*>& files) {
                if (files.isEmpty()) return;
                addGroupRow(groupName);
                for (const TsxSourceFile* file : files) {
                    const int row = m_table->rowCount();
                    m_table->insertRow(row);
                    auto* name = new QTableWidgetItem(QStringLiteral("  ") + file->relativePath);
                    auto* category = new QTableWidgetItem(file->category);
                    auto* mapping = new QTableWidgetItem(file->mapping);
                    auto* status = new QTableWidgetItem(statusText(file->status));
                    name->setToolTip(file->relativePath);
                    mapping->setToolTip(file->mapping);
                    status->setForeground(QBrush(statusColor(file->status)));
                    status->setTextAlignment(Qt::AlignCenter);
                    m_table->setItem(row, 0, name);
                    m_table->setItem(row, 1, category);
                    m_table->setItem(row, 2, mapping);
                    m_table->setItem(row, 3, status);
                }
            };

            addSourceFiles(QStringLiteral("已导入并核对的源内容"), verifiedFiles);
            addSourceFiles(QStringLiteral("未建立 H5 映射的内容"), unmappedFiles);
            addSourceFiles(QStringLiteral("无法核对的源内容"), unverifiableFiles);
            addSourceFiles(QStringLiteral("当前流程未消费的辅助内容"), notConsumedFiles);
        }

        int passed = 0;
        int failed = 0;
        int notImported = 0;
        int notConsumed = 0;
        int notVerifiable = 0;
        for (const TsxValidationCheck& check : report.checks) {
            if (check.status == kPass) ++passed;
            else if (check.status == kFailed) ++failed;
            else if (check.status == kNotImported) ++notImported;
            else if (check.status == kNotConsumed) ++notConsumed;
            else ++notVerifiable;
        }
        for (const TsxSourceFile& file : report.sourceFiles) {
            if (file.status == kNotImported) ++notImported;
            else if (file.status == kNotConsumed) ++notConsumed;
        }
        m_title->setText(QStringLiteral("导入结果验证：%1").arg(statusName(report.status)));
        m_summary->setText(QStringLiteral("H5：%1；通过：%2，失败：%3，未导入：%4，当前流程未消费：%5，无法验证：%6。%7")
            .arg(QFileInfo(report.h5Path).fileName()).arg(passed).arg(failed).arg(notImported)
            .arg(notConsumed).arg(notVerifiable).arg(report.summary));
    }

    void showNotVerifiable(const QString& summary, const QString& detail)
    {
        m_hasSnapshot = false;
        m_title->setText(QStringLiteral("导入结果验证：无法验证"));
        m_summary->setText(summary.isEmpty() ? QStringLiteral("当前没有可验证的稳定已提交结果快照。") : summary);
        m_table->setRowCount(1);
        m_table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("已提交结果快照")));
        auto* h5 = new QTableWidgetItem(summary);
        h5->setToolTip(detail);
        m_table->setItem(0, 2, h5);
        auto* status = new QTableWidgetItem(statusText(kNotVerifiable));
        status->setForeground(QBrush(statusColor(kNotVerifiable)));
        status->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(0, 3, status);
    }

    QPointer<TSXBatchImportNode> m_node;
    QLabel* m_title = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_refresh = nullptr;
    QListWidget* m_productList = nullptr;
    QTableWidget* m_table = nullptr;
    QFutureWatcher<TsxValidationReport>* m_activeWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    TsxValidationSnapshot m_snapshot;
    QHash<QString, QString> m_sourceByH5;
    QHash<QString, QString> m_sourceMappingErrors;
    bool m_hasSnapshot = false;
    std::uint64_t m_epoch = 0;
};

} // namespace

QWidget* createTsxBatchImportValidationWidget(TSXBatchImportNode* node, QWidget* parent)
{
    return new TSXBatchImportValidationWidget(node, parent);
}

} // namespace QtNodes
