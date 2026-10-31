#include "SaveInstaller.h"

#include "PathUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QVector>

namespace {

// 在 parentDir 中生成不与现有路径冲突的临时目录名。
QString uniqueSiblingPath(const QString &parentDir, const QString &name,
                          const QString &suffix)
{
    const QString base = QStringLiteral(".%1.%2-%3").arg(name, suffix, PathUtils::timestamp());
    QString path = QDir(parentDir).filePath(base);
    int sequence = 2;
    while (QFileInfo::exists(path)) {
        path = QDir(parentDir).filePath(
            QStringLiteral("%1_%2").arg(base).arg(sequence++));
    }
    return path;
}

} // namespace

SaveInstallResult SaveInstaller::install(
    const QString &sourceDir, const QString &destinationDir,
    const std::atomic_bool *cancel,
    const std::function<void(int percent, const QString &message)> &progress,
    const QString &label)
{
    SaveInstallResult result;

    if (!PathUtils::isDirectory(sourceDir)) {
        result.error = QStringLiteral("源%1目录不存在：%2").arg(label, sourceDir);
        return result;
    }
    if (!QFileInfo(QDir(sourceDir).filePath(QStringLiteral("main.ttw"))).isFile()) {
        result.error = QStringLiteral("源目录不是有效%1（缺少 main.ttw）：%2")
                           .arg(label, sourceDir);
        return result;
    }
    if (QFileInfo::exists(destinationDir) && !PathUtils::isDirectory(destinationDir)) {
        result.error = QStringLiteral("目标路径已存在且不是目录：%1").arg(destinationDir);
        return result;
    }

    // 先统计文件与总大小，用于进度百分比。
    QVector<QPair<QString, qint64>> files;
    qint64 totalBytes = 0;
    QDirIterator it(sourceDir, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const qint64 size = QFileInfo(path).size();
        files.append({path, size});
        totalBytes += size;
    }
    if (files.isEmpty()) {
        result.error = QStringLiteral("源%1目录为空：%2").arg(label, sourceDir);
        return result;
    }

    const QString parentDir = QFileInfo(destinationDir).absolutePath();
    QString error;
    if (!PathUtils::ensureDir(parentDir, &error)) {
        result.error = error;
        return result;
    }

    const QString destinationName = QFileInfo(destinationDir).fileName();
    const QString stagingDir =
        uniqueSiblingPath(parentDir, destinationName, QStringLiteral("installing"));
    if (!PathUtils::ensureDir(stagingDir, &error)) {
        result.error = error;
        return result;
    }

    qint64 copiedBytes = 0;
    int copiedFiles = 0;
    for (const auto &file : files) {
        if (cancel && cancel->load()) {
            result.canceled = true;
            result.error = QStringLiteral("已取消");
            QDir(stagingDir).removeRecursively();
            return result;
        }

        const QString relative = PathUtils::normalizedRelative(sourceDir, file.first);
        const QString target = QDir(stagingDir).filePath(relative);
        if (!PathUtils::ensureDir(QFileInfo(target).absolutePath(), &error)) {
            result.error = error;
            QDir(stagingDir).removeRecursively();
            return result;
        }
        if (!QFile::copy(file.first, target)) {
            result.error = QStringLiteral("复制失败：%1").arg(file.first);
            QDir(stagingDir).removeRecursively();
            return result;
        }

        copiedBytes += file.second;
        ++copiedFiles;
        if (progress) {
            const int percent =
                totalBytes > 0 ? static_cast<int>(copiedBytes * 100 / totalBytes) : 100;
            progress(percent, QStringLiteral("复制%1 %2/%3")
                                  .arg(label)
                                  .arg(copiedFiles)
                                  .arg(files.size()));
        }
    }

    // 复制完成后替换目标目录；旧目录先移开，失败时恢复。
    const bool destinationExists = PathUtils::isDirectory(destinationDir);
    QString replacedDir;
    if (destinationExists) {
        replacedDir =
            uniqueSiblingPath(parentDir, destinationName, QStringLiteral("replaced"));
        if (!QDir().rename(destinationDir, replacedDir)) {
            result.error = QStringLiteral("无法替换目标%1：%2").arg(label, destinationDir);
            QDir(stagingDir).removeRecursively();
            return result;
        }
    }

    if (!QDir().rename(stagingDir, destinationDir)) {
        if (destinationExists)
            QDir().rename(replacedDir, destinationDir);
        QDir(stagingDir).removeRecursively();
        result.error = QStringLiteral("无法写入目标%1：%2").arg(label, destinationDir);
        return result;
    }

    if (destinationExists)
        QDir(replacedDir).removeRecursively();

    result.ok = true;
    result.fileCount = copiedFiles;
    result.totalBytes = copiedBytes;
    return result;
}
