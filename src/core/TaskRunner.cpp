#include "TaskRunner.h"

#include <QMetaObject>
#include <QtConcurrent>

TaskContext::TaskContext(std::atomic_bool *cancel,
                         std::function<void(int, const QString &)> reporter, bool *failed,
                         QString *failureMessage)
    : m_cancel(cancel)
    , m_reporter(std::move(reporter))
    , m_failed(failed)
    , m_failureMessage(failureMessage)
{
}

bool TaskContext::isCanceled() const
{
    return m_cancel && m_cancel->load();
}

void TaskContext::report(int percent, const QString &message)
{
    if (m_reporter)
        m_reporter(percent, message);
}

void TaskContext::fail(const QString &message)
{
    if (m_failed)
        *m_failed = true;
    if (m_failureMessage)
        *m_failureMessage = message;
}

bool TaskContext::failed() const
{
    return m_failed && *m_failed;
}

QString TaskContext::failureMessage() const
{
    return m_failureMessage ? *m_failureMessage : QString();
}

TaskRunner::TaskRunner(QObject *parent)
    : QObject(parent)
{
}

TaskRunner::~TaskRunner()
{
    if (m_watcher) {
        m_cancel.store(true);
        m_watcher->waitForFinished();
    }
}

void TaskRunner::start(const QString &title, Task task)
{
    if (m_running)
        return;

    m_running = true;
    m_cancel.store(false);
    m_failed = false;
    m_failureMessage.clear();

    emit started(title);

    m_watcher = new QFutureWatcher<void>(this);
    connect(m_watcher, &QFutureWatcher<void>::finished, this, [this] {
        m_running = false;
        const bool canceled = m_cancel.load();
        const bool ok = !m_failed && !canceled;
        QString message;
        if (m_failed)
            message = m_failureMessage;
        else if (canceled)
            message = QStringLiteral("已取消");
        else
            message = QStringLiteral("完成");
        emit finished(ok, message);

        if (m_watcher) {
            m_watcher->deleteLater();
            m_watcher = nullptr;
        }
    });

    m_watcher->setFuture(QtConcurrent::run([this, task = std::move(task)]() mutable {
        TaskContext context(
            &m_cancel,
            [this](int percent, const QString &message) {
                QMetaObject::invokeMethod(
                    this, [this, percent, message] { emit progress(percent, message); },
                    Qt::QueuedConnection);
            },
            &m_failed, &m_failureMessage);
        task(context);
    }));
}

void TaskRunner::cancel()
{
    m_cancel.store(true);
}
