#include "FileScanner.h"

#include "PathUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace {

// 简单 glob 匹配，支持 '*' 与 '?'；text 与 pattern 均需已转为小写。
bool globMatch(const QString &text, const QString &pattern)
{
    int t = 0;
    int p = 0;
    int star = -1;
    int mark = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern.at(p) == QLatin1Char('?') || pattern.at(p) == text.at(t))) {
            ++t;
            ++p;
        } else if (p < pattern.size() && pattern.at(p) == QLatin1Char('*')) {
            star = p++;
            mark = t;
        } else if (star >= 0) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern.at(p) == QLatin1Char('*'))
        ++p;
    return p == pattern.size();
}

} // namespace

QStringList FileScanner::defaultExcludes()
{
    return {QStringLiteral("Mods/**"),
            QStringLiteral("Data/Config/**"),
            QStringLiteral("EasyAntiCheat/**"),
            QStringLiteral("boot.config"),
            QStringLiteral("serverconfig.xml"),
            QStringLiteral("steam_appid.txt"),
            QStringLiteral("*.log"),
            QStringLiteral("*.dmp"),
            QStringLiteral("Logs/**"),
            QStringLiteral("crashes/**")};
}

bool FileScanner::isExcluded(const QString &relativePath, const QStringList &patterns)
{
    QString path = QDir::fromNativeSeparators(relativePath).toLower();
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);

    for (const QString &rawPattern : patterns) {
        QString pattern = QDir::fromNativeSeparators(rawPattern.trimmed()).toLower();
        if (pattern.isEmpty())
            continue;

        // "Mods/**" 表示该目录及其全部内容。
        if (pattern.endsWith(QStringLiteral("/**"))) {
            const QString prefix = pattern.left(pattern.size() - 3);
            if (path == prefix || path.startsWith(prefix + QLatin1Char('/')))
                return true;
            continue;
        }

        // "**/name" 表示任意层级的该名称。
        if (pattern.startsWith(QStringLiteral("**/"))) {
            const QString suffix = pattern.mid(3);
            if (path == suffix || path.endsWith(QLatin1Char('/') + suffix))
                return true;
            continue;
        }

        if (globMatch(path, pattern))
            return true;
    }

    return false;
}

ScanResult FileScanner::scan(const QString &root, const QStringList &patterns,
                             const std::atomic_bool *cancel,
                             const std::function<void(int, qint64)> &progress)
{
    ScanResult result;

    QDir rootDir(root);
    if (!rootDir.exists()) {
        result.error = QStringLiteral("目录不存在：%1").arg(root);
        return result;
    }

    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancel && cancel->load()) {
            result.canceled = true;
            break;
        }

        const QString absolutePath = it.next();
        const QString relativePath = PathUtils::normalizedRelative(root, absolutePath);
        if (isExcluded(relativePath, patterns))
            continue;

        const QFileInfo info(absolutePath);
        FileEntry entry;
        entry.relativePath = relativePath;
        entry.absolutePath = QDir::toNativeSeparators(absolutePath);
        entry.size = info.size();
        entry.mtimeMs = info.lastModified().toMSecsSinceEpoch();

        result.totalBytes += entry.size;
        ++result.fileCount;
        result.files.append(entry);

        if (progress && (result.fileCount % 200 == 0))
            progress(result.fileCount, result.totalBytes);
    }

    if (progress)
        progress(result.fileCount, result.totalBytes);
    return result;
}
