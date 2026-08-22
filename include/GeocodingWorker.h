#pragma once
#include "BaseWorker.h"

struct GeocodingFileResult {
    QString dstNode;
    QString geocodeName;
    QString geocodePath;
    QString relativePath;
    QString rankLevel;
    // coherence-1.1 输出沿用输入 H5 的语义；非 coherence 产品为空。
    QString coherenceSemantics;
};
Q_DECLARE_METATYPE(GeocodingFileResult)

class GeocodingWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit GeocodingWorker(QObject* parent = nullptr);
    ~GeocodingWorker();

public slots:
    void Geocoding(
        int type,
        int multi_rg,
        int multi_az,
        QString savePath,
        QStringList inputPaths,
        QString productLevel,
        int masterIndex,
        QString dstNode
    );

    void GeocodingWithDem(
        int type,
        int multi_rg,
        int multi_az,
        QString savePath,
        QStringList inputPaths,
        QString productLevel,
        int masterIndex,
        QString dstNode,
        QString demPath
    );

signals:
    void cancelled();
    void geocodingGenerated(const GeocodingFileResult& result);
};
