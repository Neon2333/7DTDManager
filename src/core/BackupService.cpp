#include "BackupService.h"

#include "PathUtils.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

// 生成不与已有备份冲突的时间戳目录名。
QString uniqueBackupName(const QString &parentDir)
{
    const QString base = PathUtils::timestamp();
    QString name = base;
    int suffix = 2;
    while (QFileInfo::exists(QDir(parentDir).filePath(name)))
        name = QStringLiteral("%1_%2").arg(base).arg(suffix++);
    return name;
}

} // namespace

BackupResult BackupService::backupSave(const QString &saveDir, const QString &backupRoot,
                                       const QString &world, const QString &saveName,
                                       const QString &version, const std::atomic_bool *cancel,
                                       const std::function<void(int, const QString &)> &progress)
{
    BackupResult result;

    if (!PathUtils::isDirectory(saveDir)) {
        result.error = QStringLiteral("存档目录不存在：%1").arg(saveDir);
        return result;
    }

    const QString parentDir = QDir(backupRoot).filePath(world + QLatin1Char('/') + saveName);
    if (!PathUtils::ensureDir(parentDir, &result.error))
        return result;

    const QString destination = QDir(parentDir).filePath(uniqueBackupName(parentDir));
    if (!PathUtils::ensureDir(destination, &result.error))
        return result;

    // 先统计文件，用于进度百分比。
    QVector<QPair<QString, qint64>> files;
    qint64 totalBytes = 0;
    QDirIterator countIt(saveDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (countIt.hasNext()) {
        const QString path = countIt.next();
        const qint64 size = QFileInfo(path).size();
        files.append({path, size});
        totalBytes += size;
    }

    qint64 copiedBytes = 0;
    int copiedFiles = 0;
    for (const auto &file : files) {
        if (cancel && cancel->load()) {
            result.canceled = true;
            result.error = QStringLiteral("已取消");
            QDir(destination).removeRecursively();
            return result;
        }

        const QString relative = PathUtils::normalizedRelative(saveDir, file.first);
        const QString target = QDir(destination).filePath(relative);
        if (!PathUtils::ensureDir(QFileInfo(target).absolutePath(), &result.error)) {
            QDir(destination).removeRecursively();
            return result;
        }

        if (!QFile::copy(file.first, target)) {
            result.error = QStringLiteral("复制失败：%1").arg(file.first);
            QDir(destination).removeRecursively();
            return result;
        }

        copiedBytes += file.second;
        ++copiedFiles;
        if (progress) {
            const int percent = totalBytes > 0 ? static_cast<int>(copiedBytes * 100 / totalBytes) : 0;
            progress(percent, QStringLiteral("备份 %1/%2").arg(copiedFiles).arg(files.size()));
        }
    }

    // 写入备份元数据。
    QJsonObject meta;
    meta.insert(QStringLiteral("version"), version);
    meta.insert(QStringLiteral("source"), QDir::toNativeSeparators(saveDir));
    meta.insert(QStringLiteral("world"), world);
    meta.insert(QStringLiteral("saveName"), saveName);
    meta.insert(QStringLiteral("createdAt"),
                QDateTime::currentDateTime().toString(Qt::ISODate));
    meta.insert(QStringLiteral("files"), copiedFiles);
    meta.insert(QStringLiteral("bytes"), static_cast<double>(copiedBytes));

    QSaveFile metaFile(QDir(destination).filePath(QStringLiteral("backup.json")));
    if (metaFile.open(QIODevice::WriteOnly)) {
        metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
        metaFile.commit();
    }

    result.ok = true;
    result.path = QDir::toNativeSeparators(destination);
    return result;
}

int BackupService::pruneBackups(const QString &backupRoot, const QString &world,
                                const QString &saveName, int keep)
{
    if (keep < 0)
        return 0;

    const QString parentDir = QDir(backupRoot).filePath(world + QLatin1Char('/') + saveName);
    QDir dir(parentDir);
    if (!dir.exists())
        return 0;

    const QStringList entries =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    const int removeCount = entries.size() - keep;
    if (removeCount <= 0)
        return 0;

    int removed = 0;
    for (int i = 0; i < removeCount; ++i) {
        QDir subDir(dir.filePath(entries.at(i)));
        if (subDir.removeRecursively())
            ++removed;
    }
    return removed;
}
