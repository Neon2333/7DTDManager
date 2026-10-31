#include "LogWatcher.h"

#include <QFileInfo>
#include <QRegularExpression>

LogWatcher::LogWatcher(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &LogWatcher::poll);
}

void LogWatcher::start(const QString &logPath, int timeoutMs)
{
    stop();

    // 游戏可能尚未创建日志文件，轮询时再尝试打开。
    m_file.setFileName(logPath);
    m_offset = 0;
    m_pending.clear();
    m_timeoutMs = timeoutMs;
    m_gotVersion = false;
    m_gotUserData = false;
    m_gotAutoLoad = false;
    m_sawAutoLoadFailure = false;
    m_elapsed.start();
    m_timer.start();
}

void LogWatcher::stop()
{
    m_timer.stop();
    if (m_file.isOpen())
        m_file.close();
}

void LogWatcher::poll()
{
    if (m_elapsed.elapsed() >= m_timeoutMs) {
        stop();
        emit finished();
        return;
    }

    if (!m_file.isOpen()) {
        if (!QFileInfo::exists(m_file.fileName()))
            return;
        if (!m_file.open(QIODevice::ReadOnly)) {
            stop();
            emit finished();
            return;
        }
        m_offset = 0;
    }

    if (!m_file.seek(m_offset)) {
        stop();
        emit finished();
        return;
    }

    const QByteArray chunk = m_file.readAll();
    m_offset = m_file.pos();
    if (chunk.isEmpty())
        return;

    m_pending += QString::fromUtf8(chunk);
    int newline = m_pending.indexOf(QLatin1Char('\n'));
    while (newline >= 0) {
        const QString line = m_pending.left(newline).trimmed();
        m_pending.remove(0, newline + 1);
        if (!line.isEmpty())
            processLine(line);
        newline = m_pending.indexOf(QLatin1Char('\n'));
    }
}

void LogWatcher::processLine(const QString &line)
{
    // 游戏日志行带有时间戳前缀，因此不能使用行首锚点。
    static const QRegularExpression versionRe(
        QStringLiteral(R"(Version:\s*(V\s*\d+\.\d+(?:\s*\(b\d+\))?))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression userDataRe(
        QStringLiteral(R"(UserDataFolder:\s*(.+?)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    if (!m_gotVersion) {
        const auto match = versionRe.match(line);
        if (match.hasMatch()) {
            m_gotVersion = true;
            emit versionDetected(match.captured(1).trimmed());
        }
    }

    if (!m_gotUserData) {
        const auto match = userDataRe.match(line);
        if (match.hasMatch()) {
            m_gotUserData = true;
            emit userDataFolderDetected(match.captured(1));
        }
    }

    if (line.contains(QStringLiteral("Loaded world file from different version"),
                      Qt::CaseInsensitive)) {
        emit versionMismatchDetected(line);
    }

    if (!m_gotAutoLoad) {
        const QString lower = line.toLower();
        // 跳过启动参数回显，避免把 "-LoadSaveGame=" 本身误判为加载结果。
        if (lower.contains(QStringLiteral("command line")))
            return;

        if (lower.contains(QStringLiteral("[loadsavegame]"))) {
            // 结论行：游戏已决定继续现有存档，或已判定无法加载。
            if (lower.contains(QStringLiteral("loading existing save game"))
                || lower.contains(QStringLiteral("creating new save game"))) {
                m_gotAutoLoad = true;
                emit autoLoadResult(true, line);
            } else if (lower.contains(QStringLiteral("does not exist"))
                       || lower.contains(QStringLiteral("can not load archived"))
                       || lower.contains(QStringLiteral("failed to automate"))) {
                m_gotAutoLoad = true;
                m_sawAutoLoadFailure = true;
                emit autoLoadResult(false, line);
            }
            // 其余行（如 Advanced to state、After restart）不是结论，继续等待。
        }
    }
}
