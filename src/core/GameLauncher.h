#pragma once

#include <QString>

// 一次游戏启动请求。
struct LaunchRequest
{
    QString versionId;
    QString versionDir;     // 版本目录
    QString logDir;         // 工具日志目录
    QString worldName;      // 自动加载的世界名（对应 GamePrefs.GameWorld）
    QString saveName;       // 自动加载的存档名（对应 GamePrefs.GameName）

    bool noEac = true;
    bool skipIntro = false;
    bool autoLoad = false;  // 使用 -LoadSaveGame=true + -GameWorld/-GameName 自动进入存档
    QString extraArgs;
};

// 启动结果。
struct LaunchResult
{
    bool ok = false;
    QString error;
    QString logPath;
    qint64 pid = 0;
};

// 直接启动版本目录中的 7DaysToDie.exe（不复制到 Steam 目录）。
class GameLauncher
{
public:
    static LaunchResult launch(const LaunchRequest &request);

    // 生成一次启动的专属日志路径。
    static QString makeLogPath(const QString &logDir, const QString &versionId);
};
