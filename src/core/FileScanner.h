#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

// 单个文件条目；relativePath 统一使用 '/' 分隔。
struct FileEntry
{
    QString relativePath;
    QString absolutePath;
    qint64 size = 0;
    qint64 mtimeMs = 0;
};

// 目录扫描结果。
struct ScanResult
{
    QVector<FileEntry> files;
    qint64 totalBytes = 0;
    int fileCount = 0;
    bool canceled = false;
    QString error;
};

// 递归枚举目录中的文件，并按排除规则过滤。
class FileScanner
{
public:
    // 默认不参与硬链接共享的路径/文件模式。
    static QStringList defaultExcludes();

    // 判断相对路径是否命中任一排除模式（大小写不敏感，'/' 分隔）。
    static bool isExcluded(const QString &relativePath, const QStringList &patterns);

    // 扫描目录。progress 每处理若干文件回调一次（文件数, 累计字节数）。
    static ScanResult scan(const QString &root, const QStringList &patterns,
                           const std::atomic_bool *cancel = nullptr,
                           const std::function<void(int, qint64)> &progress = {});
};
