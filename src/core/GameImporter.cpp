#include "GameImporter.h"

#include "PathUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

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

} // namespace

bool GameImporter::validateSource(const QString &sourceDir, QString *error)
{
    QString missing;
    if (!PathUtils::isCompleteGameDir(sourceDir, &missing)) {
        if (error)
            *error = QStringLiteral("不是完整游戏目录，缺少：%1").arg(missing);
        return false;
    }
    return true;
}

SteamMetadata GameImporter::readSteamMetadata(const QString &sourceDir)
{
    SteamMetadata metadata;

    QString normalized = QDir::fromNativeSeparators(QDir(sourceDir).absolutePath());
    normalized = QDir::cleanPath(normalized);
    const QString marker = QStringLiteral("/steamapps/common/");
    const int index = normalized.toLower().indexOf(marker);
    if (index < 0)
        return metadata;

    const QString steamApps = normalized.left(index + QStringLiteral("/steamapps").size());
    const QString manifestPath = steamApps + QStringLiteral("/appmanifest_251570.acf");
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
        return metadata;

    const QString content = QString::fromUtf8(file.readAll());
    static const QRegularExpression buildRe(
        QStringLiteral(R"re("buildid"\s+"([^"]+)")re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression betaRe(
        QStringLiteral(R"re("BetaKey"\s+"([^"]+)")re"),
        QRegularExpression::CaseInsensitiveOption);

    const auto buildMatch = buildRe.match(content);
    if (buildMatch.hasMatch())
        metadata.buildId = buildMatch.captured(1);

    const auto betaMatch = betaRe.match(content);
    if (betaMatch.hasMatch())
        metadata.betaKey = betaMatch.captured(1);

    metadata.appManifestPath = QDir::toNativeSeparators(manifestPath);
    return metadata;
}

ImportResult GameImporter::importDirectory(
    const QString &sourceDir, const QString &targetPath, const std::atomic_bool *cancel,
    const std::function<void(int, const QString &)> &progress)
{
    ImportResult result;
    result.targetPath = QDir::toNativeSeparators(targetPath);

    QString error;
    if (!validateSource(sourceDir, &error)) {
        result.error = error;
        return result;
    }

    if (QFileInfo::exists(targetPath)) {
        result.error = QStringLiteral("目标目录已存在：%1").arg(targetPath);
        return result;
    }

    if (!PathUtils::isNtfs(QFileInfo(targetPath).absolutePath(), &error)) {
        result.error = error;
        return result;
    }

    if (!PathUtils::ensureDir(targetPath, &error)) {
        result.error = error;
        return result;
    }

    // 统计文件数量用于进度。
    int totalFiles = 0;
    QDirIterator countIt(sourceDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (countIt.hasNext()) {
        countIt.next();
        ++totalFiles;
    }

    result.steam = readSteamMetadata(sourceDir);

    QDir root(sourceDir);
    QDirIterator it(sourceDir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const QString relative = PathUtils::normalizedRelative(sourceDir, sourcePath);
        if (!PathUtils::ensureDir(QDir(targetPath).filePath(relative), &error)) {
            result.error = error;
            QDir(targetPath).removeRecursively();
            return result;
        }
    }

    QDirIterator fileIt(sourceDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (fileIt.hasNext()) {
        if (cancel && cancel->load()) {
            result.canceled = true;
            result.error = QStringLiteral("已取消");
            QDir(targetPath).removeRecursively();
            return result;
        }

        const QString sourcePath = fileIt.next();
        const QString relative = PathUtils::normalizedRelative(sourceDir, sourcePath);
        const QString targetFile = QDir(targetPath).filePath(relative);

        if (!PathUtils::ensureDir(QFileInfo(targetFile).absolutePath(), &error)) {
            result.error = error;
            QDir(targetPath).removeRecursively();
            return result;
        }

        const QString winSource = PathUtils::toWin32Path(sourcePath);
        const QString winTarget = PathUtils::toWin32Path(targetFile);
        if (!CopyFileW(reinterpret_cast<LPCWSTR>(winSource.utf16()),
                       reinterpret_cast<LPCWSTR>(winTarget.utf16()), TRUE)) {
            result.error = QStringLiteral("复制失败：%1（%2）")
                               .arg(sourcePath, win32Error(GetLastError()));
            QDir(targetPath).removeRecursively();
            return result;
        }

        ++result.fileCount;
        result.totalBytes += QFileInfo(sourcePath).size();
        if (progress) {
            const int percent = totalFiles > 0 ? result.fileCount * 100 / totalFiles : 0;
            progress(percent, QStringLiteral("复制 %1/%2：%3")
                                  .arg(result.fileCount)
                                  .arg(totalFiles)
                                  .arg(relative));
        }
    }

    result.ok = true;
    return result;
}
