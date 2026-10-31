#include "HardLink.h"

#include "PathUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

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

// 打开文件句柄以读取文件身份；目录同样适用。
HANDLE openForIdentity(const QString &path)
{
    const QString winPath = PathUtils::toWin32Path(path);
    return CreateFileW(reinterpret_cast<LPCWSTR>(winPath.utf16()), FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
}

} // namespace

QString FileIdentity::key() const
{
    return QStringLiteral("%1:%2").arg(volumeSerial, 8, 16, QLatin1Char('0')).arg(fileIndex, 16, 16,
                                                                                QLatin1Char('0'));
}

bool createHardLink(const QString &linkPath, const QString &targetPath, QString *error)
{
    if (linkPath.isEmpty() || targetPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("硬链接路径为空");
        return false;
    }

    const QString winLink = PathUtils::toWin32Path(linkPath);
    const QString winTarget = PathUtils::toWin32Path(targetPath);
    if (!CreateHardLinkW(reinterpret_cast<LPCWSTR>(winLink.utf16()),
                         reinterpret_cast<LPCWSTR>(winTarget.utf16()), nullptr)) {
        if (error)
            *error = QStringLiteral("创建硬链接失败：%1").arg(win32Error(GetLastError()));
        return false;
    }

    FileIdentity linkId;
    FileIdentity targetId;
    if (!queryFileIdentity(linkPath, &linkId, error) || !queryFileIdentity(targetPath, &targetId, error))
        return false;
    if (linkId.key() != targetId.key()) {
        if (error)
            *error = QStringLiteral("硬链接校验失败：链接与目标不是同一文件");
        return false;
    }
    return true;
}

bool queryFileIdentity(const QString &path, FileIdentity *identity, QString *error)
{
    if (!identity) {
        if (error)
            *error = QStringLiteral("输出参数为空");
        return false;
    }
    *identity = FileIdentity{};

    HANDLE handle = openForIdentity(path);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error)
            *error = QStringLiteral("无法打开文件 %1：%2").arg(path, win32Error(GetLastError()));
        return false;
    }

    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok = GetFileInformationByHandle(handle, &info);
    const DWORD lastError = GetLastError();
    CloseHandle(handle);

    if (!ok) {
        if (error)
            *error = QStringLiteral("读取文件身份失败：%1").arg(win32Error(lastError));
        return false;
    }

    identity->volumeSerial = info.dwVolumeSerialNumber;
    identity->fileIndex = (static_cast<quint64>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    identity->linkCount = info.nNumberOfLinks;
    identity->valid = true;
    return true;
}

bool breakHardLink(const QString &path, QString *error)
{
    FileIdentity identity;
    if (!queryFileIdentity(path, &identity, error))
        return false;
    if (identity.linkCount <= 1)
        return true;

    const QFileInfo info(path);
    const QString tempPath = info.absolutePath() + QLatin1Char('\\') + info.fileName()
                             + QStringLiteral(".unlink-") + PathUtils::timestamp() + QStringLiteral(".tmp");

    const QString winSource = PathUtils::toWin32Path(path);
    const QString winTemp = PathUtils::toWin32Path(tempPath);
    if (!CopyFileW(reinterpret_cast<LPCWSTR>(winSource.utf16()),
                   reinterpret_cast<LPCWSTR>(winTemp.utf16()), FALSE)) {
        if (error)
            *error = QStringLiteral("复制文件以解除链接失败：%1").arg(win32Error(GetLastError()));
        return false;
    }

    if (!MoveFileExW(reinterpret_cast<LPCWSTR>(winTemp.utf16()),
                     reinterpret_cast<LPCWSTR>(winSource.utf16()),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(reinterpret_cast<LPCWSTR>(winTemp.utf16()));
        if (error)
            *error = QStringLiteral("替换原文件失败：%1").arg(win32Error(code));
        return false;
    }

    return true;
}
