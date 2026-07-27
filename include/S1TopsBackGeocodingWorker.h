#pragma once
#include "BaseWorker.h"
#include "InSARLogManager.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <QStringList>

class Sentinel1BackGeocoding;
struct InSARDiagnosticEvent;

class S1TopsBackGeocodingWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit S1TopsBackGeocodingWorker(QObject* parent = nullptr);
    ~S1TopsBackGeocodingWorker();

    void setDemPath(const QString& path) { m_demPath = path; }
    void setRangeRefine(bool enable) { m_bRangeRefine = enable; }
    void setRecoverRefinementTransaction(bool enable) { m_recoverRefinementTransaction = enable; }
    void setTaskLogContext(const TaskLogContext& context) { m_taskLogContext = context; }

    /// 在启动任务前重置本次任务的停止请求。
    void prepareForStart();

    /// 可由 UI 线程直接调用；只操作原子标志和 DLL 的线程安全取消接口。
    void requestCancel() noexcept;

public slots:
    void S1_TOPS_BackGeocoding(
        int masterIndex,
        QString savePath,
        QString dstProject,
        QString dstNode,
        QStringList inputPaths,
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
    static void __stdcall onNativeDiagnostic(const InSARDiagnosticEvent* event, void* userData) noexcept;
    void appendNativeDiagnostic(const InSARDiagnosticEvent* event) noexcept;
    std::shared_ptr<Sentinel1BackGeocoding> activeBackGeocoding() const;

    QString m_demPath;
    bool m_bRangeRefine = false;
    bool m_recoverRefinementTransaction = false;
    std::atomic<bool> m_stopRequested{false};
    mutable std::mutex m_backGeocodingMutex;
    std::shared_ptr<Sentinel1BackGeocoding> m_backGeocoding;
    TaskLogContext m_taskLogContext;
};
