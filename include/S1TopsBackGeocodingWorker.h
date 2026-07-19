#pragma once
#include "BaseWorker.h"
#include <atomic>
#include <memory>
#include <mutex>

class Sentinel1BackGeocoding;

class S1TopsBackGeocodingWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit S1TopsBackGeocodingWorker(QObject* parent = nullptr);
    ~S1TopsBackGeocodingWorker();

    void setDemPath(const QString& path) { m_demPath = path; }
    void setRangeRefine(bool enable) { m_bRangeRefine = enable; }

    /// 在启动任务前重置本次任务的停止请求。
    void prepareForStart();

    /// 可由 UI 线程直接调用；只操作原子标志和 DLL 的线程安全取消接口。
    void requestCancel() noexcept;

public slots:
    void S1_TOPS_BackGeocoding(
        int images_number,
        int masterIndex,
        QString savePath,
        QString dstProject,
        QString srcNode,
        QString dstNode,
        QStandardItemModel* model,
        bool b_ESD = true
    );

signals:
    void registrationFinished(
        const QStringList& regisH5Paths,
        const QString& dstNode,
        const QString& dstProject,
        const QString& savePath,
        int masterIndex,
        bool hasQualityWarning,
        const QStringList& qualityWarnings
    );
    void cancelled(const QStringList& cleanupFailures);

private:
    std::shared_ptr<Sentinel1BackGeocoding> activeBackGeocoding() const;

    QString m_demPath;
    bool m_bRangeRefine = false;
    std::atomic<bool> m_stopRequested{false};
    mutable std::mutex m_backGeocodingMutex;
    std::shared_ptr<Sentinel1BackGeocoding> m_backGeocoding;
};
