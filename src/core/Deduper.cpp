#include "Deduper.h"

#include "FileScanner.h"
#include "HardLink.h"
#include "HashService.h"
#include "PathUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

// 清单中的哈希缓存项。
struct CachedHash
{
    qint64 size = -1;
    qint64 mtimeMs = 0;
    QByteArray hash;
};

using HashCache = QHash<QString, CachedHash>;

// 将 Win32 错误码转为可读字符串。
QString win32Error(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                          | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0,
                                      nullptr);
    QString text;
    if (size && buffer)
        text = QString::fromWCharArray(buffer, static_cast<int>(size)).trimmed();
    if (buffer)
        LocalFree(buffer);
    if (text.isEmpty())
        text = QStringLiteral("错误码 %1").arg(code);
    return text;
}

// 读取哈希清单；root 不匹配时视为空缓存。
HashCache loadManifest(const QString &path, const QString &root)
{
    HashCache cache;
    if (path.isEmpty())
        return cache;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return cache;

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return cache;

    const QJsonObject object = document.object();
    if (QDir::cleanPath(object.value(QStringLiteral("root")).toString())
        != QDir::cleanPath(root))
        return cache;

    const QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        const QString relative = item.value(QStringLiteral("path")).toString();
        const QString hash = item.value(QStringLiteral("sha256")).toString();
        if (relative.isEmpty() || hash.isEmpty())
            continue;
        CachedHash cached;
        cached.size = static_cast<qint64>(item.value(QStringLiteral("size")).toDouble(-1));
        cached.mtimeMs = static_cast<qint64>(item.value(QStringLiteral("mtime")).toDouble(0));
        cached.hash = hash.toLatin1();
        cache.insert(relative, cached);
    }
    return cache;
}

// 保存哈希清单。
void saveManifest(const QString &path, const QString &root, const HashCache &cache)
{
    if (path.isEmpty())
        return;

    QJsonArray entries;
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it) {
        if (it.value().hash.isEmpty())
            continue;
        QJsonObject item;
        item.insert(QStringLiteral("path"), it.key());
        item.insert(QStringLiteral("size"), static_cast<double>(it.value().size));
        item.insert(QStringLiteral("mtime"), static_cast<double>(it.value().mtimeMs));
        item.insert(QStringLiteral("sha256"), QString::fromLatin1(it.value().hash));
        entries.append(item);
    }

    QJsonObject object;
    object.insert(QStringLiteral("schema"), 1);
    object.insert(QStringLiteral("root"), QDir::toNativeSeparators(root));
    object.insert(QStringLiteral("entries"), entries);

    PathUtils::ensureDir(QFileInfo(path).absolutePath(), nullptr);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.commit();
}

// 获取文件哈希；优先使用 size+mtime 匹配的缓存。
QByteArray hashOf(const FileEntry &entry, HashCache &cache, const std::atomic_bool *cancel,
                  bool *canceled)
{
    const auto it = cache.constFind(entry.relativePath);
    if (it != cache.constEnd() && it->size == entry.size && it->mtimeMs == entry.mtimeMs
        && !it->hash.isEmpty()) {
        return it->hash;
    }

    QByteArray hash = HashService::sha256File(entry.absolutePath, nullptr, cancel);
    if (hash.isEmpty()) {
        if (cancel && cancel->load() && canceled)
            *canceled = true;
        return {};
    }

    CachedHash cached;
    cached.size = entry.size;
    cached.mtimeMs = entry.mtimeMs;
    cached.hash = hash;
    cache.insert(entry.relativePath, cached);
    return hash;
}

// 生成同目录下的唯一临时路径。
QString siblingTempPath(const QString &path, const QString &tag)
{
    static int counter = 0;
    return path + QStringLiteral(".%1-%2-%3")
                      .arg(tag)
                      .arg(GetCurrentProcessId())
                      .arg(++counter);
}

} // namespace

DedupPlan Deduper::plan(const QString &sourceRoot, const QString &baseRoot,
                        const DedupOptions &options)
{
    DedupPlan plan;
    plan.sourceRoot = QDir::toNativeSeparators(sourceRoot);
    plan.baseRoot = QDir::toNativeSeparators(baseRoot);

    if (!PathUtils::isDirectory(sourceRoot) || !PathUtils::isDirectory(baseRoot)) {
        plan.error = QStringLiteral("源目录或基准目录不存在");
        return plan;
    }

    if (!PathUtils::isNtfs(sourceRoot, &plan.error))
        return plan;
    if (!PathUtils::isNtfs(baseRoot, &plan.error))
        return plan;
    if (!PathUtils::sameVolume(sourceRoot, baseRoot, &plan.error))
        return plan;

    const ScanResult baseScan = FileScanner::scan(baseRoot, options.excludes, options.cancel);
    if (baseScan.canceled) {
        plan.canceled = true;
        return plan;
    }

    QHash<QString, FileEntry> baseFiles;
    for (const FileEntry &entry : baseScan.files)
        baseFiles.insert(entry.relativePath, entry);

    HashCache baseCache = loadManifest(options.baseManifestPath, baseRoot);
    HashCache sourceCache = loadManifest(options.sourceManifestPath, sourceRoot);

    const ScanResult sourceScan = FileScanner::scan(sourceRoot, options.excludes, options.cancel);
    if (sourceScan.canceled) {
        plan.canceled = true;
        return plan;
    }

    const int total = qMax(1, sourceScan.fileCount);
    int processed = 0;

    for (const FileEntry &sourceEntry : sourceScan.files) {
        if (options.cancel && options.cancel->load()) {
            plan.canceled = true;
            break;
        }

        ++processed;

        const auto baseIt = baseFiles.constFind(sourceEntry.relativePath);
        if (baseIt == baseFiles.constEnd()) {
            ++plan.missingInBaseCount;
            continue;
        }

        if (baseIt->size != sourceEntry.size) {
            ++plan.differentCount;
            continue;
        }

        // 已经是同一文件（例如重复执行去重）则跳过。
        FileIdentity sourceId;
        FileIdentity baseId;
        if (queryFileIdentity(sourceEntry.absolutePath, &sourceId)
            && queryFileIdentity(baseIt->absolutePath, &baseId)
            && sourceId.key() == baseId.key()) {
            ++plan.alreadyLinkedCount;
            continue;
        }

        if (options.progress) {
            const int percent = processed * 90 / total;
            options.progress(percent, QStringLiteral("计算哈希：%1").arg(sourceEntry.relativePath));
        }

        bool canceled = false;
        const QByteArray sourceHash = hashOf(sourceEntry, sourceCache, options.cancel, &canceled);
        if (canceled) {
            plan.canceled = true;
            break;
        }
        if (sourceHash.isEmpty()) {
            ++plan.hashFailedCount;
            continue;
        }

        const QByteArray baseHash = hashOf(*baseIt, baseCache, options.cancel, &canceled);
        if (canceled) {
            plan.canceled = true;
            break;
        }
        if (baseHash.isEmpty()) {
            ++plan.hashFailedCount;
            continue;
        }

        if (sourceHash != baseHash) {
            ++plan.differentCount;
            continue;
        }

        DedupPlanEntry entry;
        entry.relativePath = sourceEntry.relativePath;
        entry.sourcePath = sourceEntry.absolutePath;
        entry.basePath = baseIt->absolutePath;
        entry.size = sourceEntry.size;
        entry.baseMtimeMs = baseIt->mtimeMs;
        plan.entries.append(entry);
        plan.savingsBytes += entry.size;
    }

    plan.sameCount = plan.entries.size();

    saveManifest(options.baseManifestPath, baseRoot, baseCache);
    saveManifest(options.sourceManifestPath, sourceRoot, sourceCache);

    if (options.progress)
        options.progress(90, QStringLiteral("分析完成：%1 个文件可共享").arg(plan.sameCount));

    return plan;
}

DedupApplyResult Deduper::apply(const DedupPlan &plan, const DedupOptions &options)
{
    DedupApplyResult result;

    if (!plan.error.isEmpty()) {
        result.error = plan.error;
        return result;
    }

    const int total = qMax(1, plan.entries.size());
    int processed = 0;

    for (const DedupPlanEntry &entry : plan.entries) {
        if (options.cancel && options.cancel->load()) {
            result.canceled = true;
            break;
        }

        ++processed;
        if (options.progress) {
            const int percent = 90 + processed * 10 / total;
            options.progress(percent, QStringLiteral("建立硬链接：%1").arg(entry.relativePath));
        }

        // 基准文件必须仍然存在，且大小/mtime 与计划一致。
        const QFileInfo baseInfo(entry.basePath);
        if (!baseInfo.exists() || baseInfo.size() != entry.size
            || baseInfo.lastModified().toMSecsSinceEpoch() != entry.baseMtimeMs) {
            result.failedCount++;
            result.errors << QStringLiteral("基准文件已变化：%1").arg(entry.relativePath);
            continue;
        }

        // 如果源文件已经是同一文件，直接跳过。
        FileIdentity sourceId;
        FileIdentity baseId;
        if (queryFileIdentity(entry.sourcePath, &sourceId)
            && queryFileIdentity(entry.basePath, &baseId) && sourceId.key() == baseId.key()) {
            ++result.linkedCount;
            continue;
        }

        const QString tempLink = siblingTempPath(entry.sourcePath, QStringLiteral("dedup-new"));
        const QString oldPath = siblingTempPath(entry.sourcePath, QStringLiteral("dedup-old"));
        DeleteFileW(reinterpret_cast<LPCWSTR>(PathUtils::toWin32Path(tempLink).utf16()));
        DeleteFileW(reinterpret_cast<LPCWSTR>(PathUtils::toWin32Path(oldPath).utf16()));

        QString error;
        if (!createHardLink(tempLink, entry.basePath, &error)) {
            result.failedCount++;
            result.errors << QStringLiteral("%1：%2").arg(entry.relativePath, error);
            continue;
        }

        const QString winSource = PathUtils::toWin32Path(entry.sourcePath);
        const QString winTemp = PathUtils::toWin32Path(tempLink);
        const QString winOld = PathUtils::toWin32Path(oldPath);

        if (!MoveFileExW(reinterpret_cast<LPCWSTR>(winSource.utf16()),
                         reinterpret_cast<LPCWSTR>(winOld.utf16()),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            result.failedCount++;
            result.errors << QStringLiteral("无法移走原文件 %1：%2")
                                 .arg(entry.relativePath, win32Error(GetLastError()));
            DeleteFileW(reinterpret_cast<LPCWSTR>(winTemp.utf16()));
            continue;
        }

        if (!MoveFileExW(reinterpret_cast<LPCWSTR>(winTemp.utf16()),
                         reinterpret_cast<LPCWSTR>(winSource.utf16()),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD code = GetLastError();
            MoveFileExW(reinterpret_cast<LPCWSTR>(winOld.utf16()),
                        reinterpret_cast<LPCWSTR>(winSource.utf16()),
                        MOVEFILE_REPLACE_EXISTING);
            DeleteFileW(reinterpret_cast<LPCWSTR>(winTemp.utf16()));
            result.failedCount++;
            result.errors << QStringLiteral("替换文件失败 %1：%2")
                                 .arg(entry.relativePath, win32Error(code));
            continue;
        }

        SetFileAttributesW(reinterpret_cast<LPCWSTR>(winOld.utf16()), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(reinterpret_cast<LPCWSTR>(winOld.utf16()));

        ++result.linkedCount;
        result.savedBytes += entry.size;
    }

    result.ok = !result.canceled && result.failedCount == 0;
    return result;
}

UsageStats Deduper::calculateUsage(const QStringList &roots, const std::atomic_bool *cancel,
                                   const std::function<void(int, const QString &)> &progress)
{
    UsageStats stats;
    QHash<QString, qint64> globalUnique;

    const int totalRoots = qMax(1, static_cast<int>(roots.size()));
    int rootIndex = 0;

    for (const QString &root : roots) {
        if (cancel && cancel->load())
            break;

        ++rootIndex;
        const ScanResult scan = FileScanner::scan(root, {}, cancel);
        QHash<QString, qint64> rootUnique;

        int processed = 0;
        for (const FileEntry &entry : scan.files) {
            if (cancel && cancel->load())
                break;

            ++processed;
            if (progress && (processed % 500 == 0)) {
                const int percent = ((rootIndex - 1) * 100 + processed * 100
                                     / qMax(1, scan.fileCount))
                                    / totalRoots;
                progress(percent, QStringLiteral("统计占用：%1（%2/%3）")
                                      .arg(QFileInfo(root).fileName())
                                      .arg(processed)
                                      .arg(scan.fileCount));
            }

            stats.logicalBytes += entry.size;
            ++stats.fileCount;
            stats.logicalByRoot[root] += entry.size;
            ++stats.countByRoot[root];

            FileIdentity identity;
            QString key;
            if (queryFileIdentity(entry.absolutePath, &identity) && identity.valid)
                key = identity.key();
            else
                key = QStringLiteral("%1|%2").arg(root, entry.relativePath);

            if (!globalUnique.contains(key))
                globalUnique.insert(key, entry.size);
            if (!rootUnique.contains(key)) {
                rootUnique.insert(key, entry.size);
                stats.physicalByRoot[root] += entry.size;
            }
        }
    }

    for (auto it = globalUnique.constBegin(); it != globalUnique.constEnd(); ++it)
        stats.physicalBytes += it.value();

    if (progress)
        progress(100, QStringLiteral("占用统计完成"));
    return stats;
}

QVector<ManifestDrift> Deduper::verifyManifest(const QString &root, const QString &manifestPath,
                                               int *entryCount)
{
    QVector<ManifestDrift> drifts;
    if (entryCount)
        *entryCount = 0;

    const HashCache cache = loadManifest(manifestPath, root);
    if (entryCount)
        *entryCount = cache.size();

    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it) {
        const QString absolutePath = QDir(root).filePath(it.key());
        const QFileInfo info(absolutePath);
        if (!info.exists() || !info.isFile()) {
            drifts.append({it.key(), QStringLiteral("文件缺失")});
            continue;
        }
        if (info.size() != it.value().size) {
            drifts.append({it.key(), QStringLiteral("大小变化")});
            continue;
        }
        if (info.lastModified().toMSecsSinceEpoch() != it.value().mtimeMs)
            drifts.append({it.key(), QStringLiteral("修改时间变化")});
    }

    return drifts;
}

bool Deduper::breakAllLinks(const QString &root, const std::atomic_bool *cancel,
                            const std::function<void(int, const QString &)> &progress,
                            int *brokenCount, QString *error)
{
    if (brokenCount)
        *brokenCount = 0;

    if (!PathUtils::isDirectory(root)) {
        if (error)
            *error = QStringLiteral("目录不存在：%1").arg(root);
        return false;
    }

    const ScanResult scan = FileScanner::scan(root, {}, cancel);
    if (scan.canceled)
        return false;

    const int total = qMax(1, scan.fileCount);
    int processed = 0;
    int broken = 0;

    for (const FileEntry &entry : scan.files) {
        if (cancel && cancel->load())
            return false;

        ++processed;
        if (progress && (processed % 100 == 0)) {
            progress(processed * 100 / total,
                     QStringLiteral("解除链接 %1/%2").arg(processed).arg(scan.fileCount));
        }

        FileIdentity identity;
        if (!queryFileIdentity(entry.absolutePath, &identity) || identity.linkCount <= 1)
            continue;

        QString breakError;
        if (!breakHardLink(entry.absolutePath, &breakError)) {
            if (error)
                *error = QStringLiteral("%1：%2").arg(entry.relativePath, breakError);
            return false;
        }
        ++broken;
    }

    if (brokenCount)
        *brokenCount = broken;
    if (progress)
        progress(100, QStringLiteral("已解除 %1 个文件的链接").arg(broken));
    return true;
}
