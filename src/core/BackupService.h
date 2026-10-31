#pragma once

#include <QString>

#include <atomic>
#include <functional>

// 存档备份结果。
struct BackupResult
{
    bool ok = false;
    bool canceled = false;
    QString path;
    QString error;
};

// 存档目录的递归备份与保留策略。
class BackupService
{
public:
    // 把 saveDir 备份到 <backupRoot>\<world>\<saveName>\<时间戳>\。
    static BackupResult backupSave(const QString &saveDir, const QString &backupRoot,
                                   const QString &world, const QString &saveName,
                                   const QString &version, const std::atomic_bool *cancel = nullptr,
                                   const std::function<void(int, const QString &)> &progress = {});

    // 只保留最近 keep 份备份，返回删除的数量。
    static int pruneBackups(const QString &backupRoot, const QString &world,
                            const QString &saveName, int keep);
};
