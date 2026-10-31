#pragma once

#include <QString>

#include <atomic>
#include <functional>

// 存档安装结果。
struct SaveInstallResult
{
    bool ok = false;
    bool canceled = false;
    QString error;
    int fileCount = 0;
    qint64 totalBytes = 0;
};

// 将存档目录完整复制到目标位置，支持覆盖替换、进度、取消与失败回滚。
class SaveInstaller
{
public:
    // 把 sourceDir 复制为 destinationDir；label 用于错误和进度提示（如“存档”“世界”）。
    // 目标已存在时先移开旧目录，复制成功后再删除；失败时恢复旧目录。
    static SaveInstallResult install(
        const QString &sourceDir, const QString &destinationDir,
        const std::atomic_bool *cancel = nullptr,
        const std::function<void(int percent, const QString &message)> &progress = {},
        const QString &label = QStringLiteral("存档"));
};
