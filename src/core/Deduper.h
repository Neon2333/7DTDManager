#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

// 去重计划中的单个可共享文件。
struct DedupPlanEntry
{
    QString relativePath;
    QString sourcePath;
    QString basePath;
    qint64 size = 0;
    qint64 baseMtimeMs = 0;
};

// 去重干跑结果。
struct DedupPlan
{
    QString sourceRoot;
    QString baseRoot;
    QVector<DedupPlanEntry> entries;
    qint64 savingsBytes = 0;
    int sameCount = 0;
    int alreadyLinkedCount = 0;
    int differentCount = 0;
    int missingInBaseCount = 0;
    int hashFailedCount = 0;
    bool canceled = false;
    QString error;
};

// 去重执行结果。
struct DedupApplyResult
{
    bool ok = false;
    bool canceled = false;
    int linkedCount = 0;
    qint64 savedBytes = 0;
    int failedCount = 0;
    QStringList errors;
    QString error;
};

// 去重参数。
struct DedupOptions
{
    QStringList excludes;
    QString baseManifestPath;
    QString sourceManifestPath;
    const std::atomic_bool *cancel = nullptr;
    std::function<void(int, const QString &)> progress;
};

// 清单漂移项：基准文件在生成清单后发生变化。
struct ManifestDrift
{
    QString relativePath;
    QString reason;
};

// 多目录占用统计。
struct UsageStats
{
    qint64 logicalBytes = 0;
    qint64 physicalBytes = 0;
    int fileCount = 0;
    QHash<QString, qint64> logicalByRoot;
    QHash<QString, qint64> physicalByRoot;
    QHash<QString, int> countByRoot;
};

// 基于硬链接的版本间文件去重。
class Deduper
{
public:
    // 干跑：比较 sourceRoot 与 baseRoot，生成可共享文件清单。
    static DedupPlan plan(const QString &sourceRoot, const QString &baseRoot,
                          const DedupOptions &options);

    // 执行计划中的每个替换；逐文件失败不中断，最终汇总。
    static DedupApplyResult apply(const DedupPlan &plan, const DedupOptions &options);

    // 统计多个目录的逻辑大小、物理占用（按文件身份去重）与文件数。
    static UsageStats calculateUsage(const QStringList &roots,
                                     const std::atomic_bool *cancel = nullptr,
                                     const std::function<void(int, const QString &)> &progress = {});

    // 校验基准清单：文件缺失、大小或 mtime 变化都视为漂移。
    static QVector<ManifestDrift> verifyManifest(const QString &root, const QString &manifestPath,
                                                 int *entryCount = nullptr);

    // 解除目录下所有链接数大于 1 的文件，使其恢复独立副本。
    static bool breakAllLinks(const QString &root, const std::atomic_bool *cancel,
                              const std::function<void(int, const QString &)> &progress,
                              int *brokenCount = nullptr, QString *error = nullptr);
};
