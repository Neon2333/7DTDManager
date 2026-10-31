#pragma once

#include <QString>

#include <atomic>
#include <functional>

// Steam 清单中与版本相关的元数据。
struct SteamMetadata
{
    QString buildId;
    QString betaKey;
    QString appManifestPath;
};

// 复制导入结果。
struct ImportResult
{
    bool ok = false;
    bool canceled = false;
    QString targetPath;
    QString error;
    int fileCount = 0;
    qint64 totalBytes = 0;
    SteamMetadata steam;
};

// 从 Steam 安装目录或任意完整游戏目录复制出一个完整版本快照。
class GameImporter
{
public:
    // 校验目录是否为完整游戏目录。
    static bool validateSource(const QString &sourceDir, QString *error = nullptr);

    // 读取 steamapps\appmanifest_251570.acf 中的 buildid / BetaKey。
    static SteamMetadata readSteamMetadata(const QString &sourceDir);

    // 递归复制 sourceDir 到 targetPath；失败时清理已复制内容。
    static ImportResult importDirectory(
        const QString &sourceDir, const QString &targetPath,
        const std::atomic_bool *cancel = nullptr,
        const std::function<void(int, const QString &)> &progress = {});
};
