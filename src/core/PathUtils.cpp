#include "PathUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace PathUtils {

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

// 打开卷根并读取文件系统名与序列号。
bool queryVolume(const QString &path, QString *fsName, DWORD *serial, QString *error)
{
    const QString root = volumeRoot(path);
    if (root.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法确定卷根：%1").arg(path);
        return false;
    }

    const QString winRoot = toWin32Path(root);
    wchar_t fsBuffer[MAX_PATH + 1] = {};
    DWORD serialValue = 0;
    if (!GetVolumeInformationW(reinterpret_cast<LPCWSTR>(winRoot.utf16()), nullptr, 0, &serialValue,
                               nullptr, nullptr, fsBuffer, MAX_PATH)) {
        if (error)
            *error = QStringLiteral("读取卷信息失败：%1").arg(win32Error(GetLastError()));
        return false;
    }

    if (fsName)
        *fsName = QString::fromWCharArray(fsBuffer);
    if (serial)
        *serial = serialValue;
    return true;
}

} // namespace

QString appDataDir()
{
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        return QDir::toNativeSeparators(appData) + QStringLiteral("\\7DTDManager");

    return QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

QString defaultSavesDir()
{
    const QString appData = qEnvironmentVariable("APPDATA");
    if (appData.isEmpty())
        return QString();
    return QDir::toNativeSeparators(appData) + QStringLiteral("\\7DaysToDie\\Saves");
}

QString toWin32Path(const QString &path)
{
    QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QStringLiteral("\\\\?\\")))
        return native;

    if (native.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);

    return QStringLiteral("\\\\?\\") + native;
}

QString fromWin32Path(const QString &path)
{
    if (path.startsWith(QStringLiteral("\\\\?\\UNC\\")))
        return QStringLiteral("\\\\") + path.mid(8);
    if (path.startsWith(QStringLiteral("\\\\?\\")))
        return path.mid(4);
    return path;
}

QString volumeRoot(const QString &path)
{
    if (path.isEmpty())
        return QString();

    const QString winPath = toWin32Path(path);
    wchar_t buffer[MAX_PATH + 1] = {};
    if (!GetVolumePathNameW(reinterpret_cast<LPCWSTR>(winPath.utf16()), buffer, MAX_PATH))
        return QString();
    return QDir::toNativeSeparators(QString::fromWCharArray(buffer));
}

quint32 volumeSerial(const QString &path)
{
    DWORD serial = 0;
    if (!queryVolume(path, nullptr, &serial, nullptr))
        return 0;
    return static_cast<quint32>(serial);
}

bool isNtfs(const QString &path, QString *error)
{
    QString fsName;
    if (!queryVolume(path, &fsName, nullptr, error))
        return false;
    if (fsName.compare(QStringLiteral("NTFS"), Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("卷 %1 的文件系统为 %2，硬链接要求 NTFS")
                         .arg(volumeRoot(path), fsName);
        return false;
    }
    return true;
}

bool sameVolume(const QString &a, const QString &b, QString *error)
{
    const quint32 sa = volumeSerial(a);
    const quint32 sb = volumeSerial(b);
    if (sa == 0 || sb == 0) {
        if (error)
            *error = QStringLiteral("无法读取卷序列号，无法确认是否同一卷");
        return false;
    }
    if (sa != sb) {
        if (error)
            *error = QStringLiteral("两个目录不在同一卷（%1 与 %2）")
                         .arg(volumeRoot(a), volumeRoot(b));
        return false;
    }
    return true;
}

bool isUnderSteamCommon(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(path).toLower();
    if (!normalized.endsWith(QLatin1Char('/')))
        normalized += QLatin1Char('/');
    return normalized.contains(QStringLiteral("/steamapps/common/"));
}

bool isCompleteGameDir(const QString &path, QString *missing)
{
    QDir dir(path);
    if (!dir.exists()) {
        if (missing)
            *missing = QStringLiteral("目录不存在");
        return false;
    }

    const QStringList required = {QStringLiteral("7DaysToDie.exe"),
                                  QStringLiteral("7DaysToDie_Data"), QStringLiteral("Data")};
    QStringList missingItems;
    for (const QString &item : required) {
        const QFileInfo info(dir.filePath(item));
        if (!info.exists())
            missingItems << item;
    }

    if (!missingItems.isEmpty()) {
        if (missing)
            *missing = missingItems.join(QStringLiteral("、"));
        return false;
    }
    return true;
}

bool isDirectory(const QString &path)
{
    return !path.isEmpty() && QFileInfo(path).isDir();
}

bool ensureDir(const QString &path, QString *error)
{
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("路径为空");
        return false;
    }
    if (QDir().mkpath(path))
        return true;
    if (error)
        *error = QStringLiteral("无法创建目录：%1").arg(path);
    return false;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("-");

    constexpr double kb = 1024.0;
    constexpr double mb = kb * 1024.0;
    constexpr double gb = mb * 1024.0;
    constexpr double tb = gb * 1024.0;

    const double value = static_cast<double>(bytes);
    if (value >= tb)
        return QStringLiteral("%1 TB").arg(value / tb, 0, 'f', 2);
    if (value >= gb)
        return QStringLiteral("%1 GB").arg(value / gb, 0, 'f', 2);
    if (value >= mb)
        return QStringLiteral("%1 MB").arg(value / mb, 0, 'f', 1);
    if (value >= kb)
        return QStringLiteral("%1 KB").arg(value / kb, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

QString timestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString normalizedRelative(const QString &root, const QString &absolutePath)
{
    QString relative = QDir(root).relativeFilePath(absolutePath);
    relative = QDir::fromNativeSeparators(relative);
    return relative;
}

} // namespace PathUtils
