#pragma once

#include "BaseWorker.h"
#include <QStringList>

/**
 * @brief 电离层 Split-Spectrum 校正 Worker
 * 读取已配准的主从 SLC 影像对，通过频谱分割生成高/低频子频带干涉图，
 * 利用色散关系求解电离层相位延迟屏 (IPS)。
 */
class IonosphericCorrectionWorker : public BaseWorker
{
    Q_OBJECT

public:
    explicit IonosphericCorrectionWorker(QObject* parent = nullptr);
    ~IonosphericCorrectionWorker();

public slots:
    void doCorrection(double subbandRatio, double filterStrength, bool outputTEC,
                      QString save_path, QString project_name,
                      QString node_name, QString file_name,
                      QStringList slcNames, QStringList slcPaths);

signals:
    void cancelled();
    void outputsGenerated(const QStringList& outputNames, const QStringList& outputPaths);
};
