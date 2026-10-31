#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

// 后台任务上下文，供任务线程报告进度、检查取消与标记失败。
class TaskContext
{
public:
    // 用户是否已请求取消。
    bool isCanceled() const;

    // 取消标志指针，可直接传给需要 std::atomic_bool* 的辅助函数。
    const std::atomic_bool *cancelFlag() const { return m_cancel; }

    // 报告进度（0-100）与状态文本。
    void report(int percent, const QString &message);

    // 标记任务失败并记录原因。
    void fail(const QString &message);

    bool failed() const;
    QString failureMessage() const;

private:
    friend class TaskRunner;
    TaskContext(std::atomic_bool *cancel, std::function<void(int, const QString &)> reporter,
                bool *failed, QString *failureMessage);

    std::atomic_bool *m_cancel = nullptr;
    std::function<void(int, const QString &)> m_reporter;
    bool *m_failed = nullptr;
    QString *m_failureMessage = nullptr;
};

// 在 QtConcurrent 线程中执行单个任务，并通过信号回到 UI 线程。
class TaskRunner : public QObject
{
    Q_OBJECT

public:
    using Task = std::function<void(TaskContext &)>;

    explicit TaskRunner(QObject *parent = nullptr);
    ~TaskRunner() override;

    bool isRunning() const { return m_running; }

    // 启动任务；已有任务运行时拒绝。
    void start(const QString &title, Task task);

    // 请求取消当前任务。
    void cancel();

signals:
    void started(const QString &title);
    void progress(int percent, const QString &message);
    void finished(bool ok, const QString &message);

private:
    std::atomic_bool m_cancel{false};
    bool m_running = false;
    bool m_failed = false;
    QString m_failureMessage;
    QFutureWatcher<void> *m_watcher = nullptr;
};
