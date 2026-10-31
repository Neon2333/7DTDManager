#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTimer>

// 轮询游戏启动日志，解析版本、UserDataFolder 与自动加载结果。
class LogWatcher : public QObject
{
    Q_OBJECT

public:
    explicit LogWatcher(QObject *parent = nullptr);

    // 开始监视日志；timeoutMs 到期后发出 finished。
    // 默认 10 分钟：Steam 登录/EULA 拉取可能让主菜单晚于 3 分钟才出现。
    void start(const QString &logPath, int timeoutMs = 600000);
    void stop();

    bool isRunning() const { return m_timer.isActive(); }

signals:
    // 解析到 "Version: V x.y (bN)" 或旧格式 "Version: Alpha x.y (bN)"。
    void versionDetected(const QString &version);

    // 解析到 "UserDataFolder: ..."。
    void userDataFolderDetected(const QString &path);

    // 自动加载结果：ok=true 表示日志显示已加载存档。
    void autoLoadResult(bool ok, const QString &detail);

    // 日志中出现存档被其他版本改写。
    void versionMismatchDetected(const QString &detail);

    // 监视结束。
    void finished();

private:
    void poll();
    void processLine(const QString &line);

    QTimer m_timer;
    QFile m_file;
    qint64 m_offset = 0;
    QString m_pending;
    QElapsedTimer m_elapsed;
    int m_timeoutMs = 600000;
    bool m_gotVersion = false;
    bool m_gotUserData = false;
    bool m_gotAutoLoad = false;
    bool m_sawAutoLoadFailure = false;
};
